/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2012-2015 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include "ilo_debug.h"
#include "ilo_image.h"
#include "ilo_vma.h"
#include "ilo_state_surface.h"

static bool
surface_set_gen6_null_SURFACE_STATE(struct ilo_state_surface *surf,
                                    const struct ilo_dev *dev)
{
   uint32_t dw0, dw3;

   ILO_DEV_ASSERT(dev, 6, 6);

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 71:
    *
    *     "All of the remaining fields in surface state are ignored for null
    *      surfaces, with the following exceptions:
    *
    *        - [DevSNB+]: Width, Height, Depth, and LOD fields must match the
    *          depth buffer's corresponding state for all render target
    *          surfaces, including null.
    *        - Surface Format must be R8G8B8A8_UNORM."
    *
    * From the Sandy Bridge PRM, volume 4 part 1, page 82:
    *
    *     "If Surface Type is SURFTYPE_NULL, this field (Tiled Surface) must
    *      be true"
    *
    * Note that we ignore the first exception for all surface types.
    */
   dw0 = GEN6_SURFTYPE_NULL << GEN6_SURFACE_DW0_TYPE__SHIFT |
         GEN6_FORMAT_R8G8B8A8_UNORM << GEN6_SURFACE_DW0_FORMAT__SHIFT;
   dw3 = GEN6_TILING_X << GEN6_SURFACE_DW3_TILING__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(surf->surface) >= 6);
   surf->surface[0] = dw0;
   surf->surface[1] = 0;
   surf->surface[2] = 0;
   surf->surface[3] = dw3;
   surf->surface[4] = 0;
   surf->surface[5] = 0;

   return true;
}

static bool
surface_set_gen7_null_SURFACE_STATE(struct ilo_state_surface *surf,
                                    const struct ilo_dev *dev)
{
   uint32_t dw0;

   ILO_DEV_ASSERT(dev, 7, 8);

   dw0 = GEN6_SURFTYPE_NULL << GEN7_SURFACE_DW0_TYPE__SHIFT |
         GEN6_FORMAT_R8G8B8A8_UNORM << GEN7_SURFACE_DW0_FORMAT__SHIFT;
   if (ilo_dev_gen(dev) >= ILO_GEN(8))
      dw0 |= GEN6_TILING_X << GEN8_SURFACE_DW0_TILING__SHIFT;
   else
      dw0 |= GEN6_TILING_X << GEN7_SURFACE_DW0_TILING__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(surf->surface) >= 13);
   surf->surface[0] = dw0;
   memset(&surf->surface[1], 0, sizeof(uint32_t) *
         (((ilo_dev_gen(dev) >= ILO_GEN(8)) ? 13 : 8) - 1));

   return true;
}

static uint32_t
surface_get_gen6_buffer_offset_alignment(const struct ilo_dev *dev,
                                         const struct ilo_state_surface_buffer_info *info)
{
   uint32_t alignment;

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Ivy Bridge PRM, volume 4 part 1, page 68:
    *
    *     "The Base Address for linear render target surfaces and surfaces
    *      accessed with the typed surface read/write data port messages must
    *      be element-size aligned, for non-YUV surface formats, or a multiple
    *      of 2 element-sizes for YUV surface formats.  Other linear surfaces
    *      have no alignment requirements (byte alignment is sufficient)."
    *
    *     "Certain message types used to access surfaces have more stringent
    *      alignment requirements. Please refer to the specific message
    *      documentation for additional restrictions."
    */
   switch (info->access) {
   case ILO_STATE_SURFACE_ACCESS_SAMPLER:
      /* no alignment requirements */
      alignment = 1;
      break;
   case ILO_STATE_SURFACE_ACCESS_DP_RENDER:
   case ILO_STATE_SURFACE_ACCESS_DP_TYPED:
      /* element-size aligned */
      alignment = info->format_size;

      assert(info->struct_size % alignment == 0);
      break;
   case ILO_STATE_SURFACE_ACCESS_DP_UNTYPED:
      /*
       * Nothing is said about Untyped* messages, but I think they require the
       * base address to be DWord aligned.
       */
      alignment = 4;

      /*
       * From the Ivy Bridge PRM, volume 4 part 1, page 70:
       *
       *     "For linear surfaces with Surface Type of SURFTYPE_STRBUF, the
       *      pitch must be a multiple of 4 bytes."
       */
      if (info->struct_size > 1)
         assert(info->struct_size % alignment == 0);
      break;
   case ILO_STATE_SURFACE_ACCESS_DP_DATA:
      /*
       * From the Ivy Bridge PRM, volume 4 part 1, page 233, 235, and 237:
       *
       *     "the surface base address must be OWord aligned"
       *
       * for OWord Block Read/Write, Unaligned OWord Block Read, and OWord
       * Dual Block Read/Write.
       *
       * From the Ivy Bridge PRM, volume 4 part 1, page 246 and 249:
       *
       *     "The surface base address must be DWord aligned"
       *
       * for DWord Scattered Read/Write and Byte Scattered Read/Write.
       */
      alignment = (info->format_size > 4) ? 16 : 4;

      /*
       * From the Ivy Bridge PRM, volume 4 part 1, page 233, 235, 237, and
       * 246:
       *
       *     "the surface pitch is ignored, the surface is treated as a
       *      1-dimensional surface. An element size (pitch) of 16 bytes is
       *      used to determine the size of the buffer for out-of-bounds
       *      checking if using the surface state model."
       *
       * for OWord Block Read/Write, Unaligned OWord Block Read, OWord
       * Dual Block Read/Write, and DWord Scattered Read/Write.
       *
       * From the Ivy Bridge PRM, volume 4 part 1, page 248:
       *
       *     "The surface pitch is ignored, the surface is treated as a
       *      1-dimensional surface. An element size (pitch) of 4 bytes is
       *      used to determine the size of the buffer for out-of-bounds
       *      checking if using the surface state model."
       *
       * for Byte Scattered Read/Write.
       *
       * It is programmable on Gen7.5+.
       */
      if (ilo_dev_gen(dev) < ILO_GEN(7.5)) {
         const int fixed = (info->format_size > 1) ? 16 : 4;
         assert(info->struct_size == fixed);
      }
      break;
   case ILO_STATE_SURFACE_ACCESS_DP_SVB:
      /*
       * From the Sandy Bridge PRM, volume 4 part 1, page 259:
       *
       *     "Both the surface base address and surface pitch must be DWord
       *      aligned."
       */
      alignment = 4;

      assert(info->struct_size % alignment == 0);
      break;
   default:
      assert(!"unknown access");
      alignment = 1;
      break;
   }

   return alignment;
}

static bool
surface_validate_gen6_buffer(const struct ilo_dev *dev,
                             const struct ilo_state_surface_buffer_info *info)
{
   uint32_t alignment;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (info->offset + info->size > info->vma->vm_size) {
      ilo_warn("invalid buffer range\n");
      return false;
   }

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 81:
    *
    *     "For surfaces of type SURFTYPE_BUFFER: [0,2047] -> [1B, 2048B]
    *      For surfaces of type SURFTYPE_STRBUF: [0,2047] -> [1B, 2048B]"
    */
   if (!info->struct_size || info->struct_size > 2048) {
      ilo_warn("invalid buffer struct size\n");
      return false;
   }

   alignment = surface_get_gen6_buffer_offset_alignment(dev, info);
   if (info->offset % alignment || info->vma->vm_alignment % alignment) {
      ilo_warn("bad buffer offset\n");
      return false;
   }

   /* no STRBUF on Gen6 */
   if (info->format == GEN6_FORMAT_RAW && info->struct_size > 1)
      assert(ilo_dev_gen(dev) >= ILO_GEN(7));

   /* SVB writes are Gen6 only */
   if (info->access == ILO_STATE_SURFACE_ACCESS_DP_SVB)
      assert(ilo_dev_gen(dev) == ILO_GEN(6));

   /*
    * From the Ivy Bridge PRM, volume 4 part 1, page 83:
    *
    *     "NOTE: "RAW" is supported only with buffers and structured buffers
    *      accessed via the untyped surface read/write and untyped atomic
    *      operation messages, which do not have a column in the table."
    *
    * From the Ivy Bridge PRM, volume 4 part 1, page 252:
    *
    *     "For untyped messages, the Surface Format must be RAW and the
    *      Surface Type must be SURFTYPE_BUFFER or SURFTYPE_STRBUF."
    */
   assert((info->access == ILO_STATE_SURFACE_ACCESS_DP_UNTYPED) ==
          (info->format == GEN6_FORMAT_RAW));

   return true;
}

static bool
surface_get_gen6_buffer_struct_count(const struct ilo_dev *dev,
                                     const struct ilo_state_surface_buffer_info *info,
                                     uint32_t *count)
{
   uint32_t max_struct, c;

   ILO_DEV_ASSERT(dev, 6, 8);

   c = info->size / info->struct_size;
   if (info->format_size < info->size - info->struct_size * c)
      c++;

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 77:
    *
    *     "For buffer surfaces, the number of entries in the buffer ranges
    *      from 1 to 2^27."
    *
    * From the Ivy Bridge PRM, volume 4 part 1, page 68:
    *
    *     "For typed buffer and structured buffer surfaces, the number of
    *      entries in the buffer ranges from 1 to 2^27.  For raw buffer
    *      surfaces, the number of entries in the buffer is the number of
    *      bytes which can range from 1 to 2^30."
    *
    * From the Ivy Bridge PRM, volume 4 part 1, page 69:
    *
    *      For SURFTYPE_BUFFER: The low two bits of this field (Width) must be
    *      11 if the Surface Format is RAW (the size of the buffer must be a
    *      multiple of 4 bytes)."
    */
   max_struct = 1 << 27;
   if (info->format == GEN6_FORMAT_RAW && info->struct_size == 1) {
      if (ilo_dev_gen(dev) >= ILO_GEN(7))
         max_struct = 1 << 30;

      c &= ~3;
   }

   if (!c || c > max_struct) {
      ilo_warn("too many or zero buffer structs\n");
      return false;
   }

   *count = c - 1;

   return true;
}

static bool
surface_set_gen6_buffer_SURFACE_STATE(struct ilo_state_surface *surf,
                                     const struct ilo_dev *dev,
                                     const struct ilo_state_surface_buffer_info *info)
{
   uint32_t dw0, dw1, dw2, dw3;
   uint32_t struct_count;
   int width, height, depth;

   ILO_DEV_ASSERT(dev, 6, 6);

   if (!surface_validate_gen6_buffer(dev, info) ||
       !surface_get_gen6_buffer_struct_count(dev, info, &struct_count))
      return false;

   /* bits [6:0] */
   width  = (struct_count & 0x0000007f);
   /* bits [19:7] */
   height = (struct_count & 0x000fff80) >> 7;
   /* bits [26:20] */
   depth  = (struct_count & 0x07f00000) >> 20;

   dw0 = GEN6_SURFTYPE_BUFFER << GEN6_SURFACE_DW0_TYPE__SHIFT |
         info->format << GEN6_SURFACE_DW0_FORMAT__SHIFT;
   dw1 = info->offset;
   dw2 = height << GEN6_SURFACE_DW2_HEIGHT__SHIFT |
         width << GEN6_SURFACE_DW2_WIDTH__SHIFT;
   dw3 = depth << GEN6_SURFACE_DW3_DEPTH__SHIFT |
         (info->struct_size - 1) << GEN6_SURFACE_DW3_PITCH__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(surf->surface) >= 6);
   surf->surface[0] = dw0;
   surf->surface[1] = dw1;
   surf->surface[2] = dw2;
   surf->surface[3] = dw3;
   surf->surface[4] = 0;
   surf->surface[5] = 0;

   surf->type = GEN6_SURFTYPE_BUFFER;
   surf->min_lod = 0;
   surf->mip_count = 0;

   return true;
}

static bool
surface_set_gen7_buffer_SURFACE_STATE(struct ilo_state_surface *surf,
                                     const struct ilo_dev *dev,
                                     const struct ilo_state_surface_buffer_info *info)
{
   uint32_t dw0, dw1, dw2, dw3, dw7;
   enum gen_surface_type type;
   uint32_t struct_count;
   int width, height, depth;

   ILO_DEV_ASSERT(dev, 7, 8);

   if (!surface_validate_gen6_buffer(dev, info) ||
       !surface_get_gen6_buffer_struct_count(dev, info, &struct_count))
      return false;

   type = (info->format == GEN6_FORMAT_RAW && info->struct_size > 1) ?
      GEN7_SURFTYPE_STRBUF : GEN6_SURFTYPE_BUFFER;

   /* bits [6:0] */
   width  = (struct_count & 0x0000007f);
   /* bits [20:7] */
   height = (struct_count & 0x001fff80) >> 7;
   /* bits [30:21] */
   depth  = (struct_count & 0x7fe00000) >> 21;

   dw0 = type << GEN7_SURFACE_DW0_TYPE__SHIFT |
         info->format << GEN7_SURFACE_DW0_FORMAT__SHIFT;
   dw1 = (ilo_dev_gen(dev) >= ILO_GEN(8)) ? 0 : info->offset;
   dw2 = GEN_SHIFT32(height, GEN7_SURFACE_DW2_HEIGHT) |
         GEN_SHIFT32(width, GEN7_SURFACE_DW2_WIDTH);
   dw3 = GEN_SHIFT32(depth, GEN7_SURFACE_DW3_DEPTH) |
         GEN_SHIFT32(info->struct_size - 1, GEN7_SURFACE_DW3_PITCH);

   dw7 = 0;
   if (ilo_dev_gen(dev) >= ILO_GEN(7.5)) {
      dw7 |= GEN_SHIFT32(GEN75_SCS_RED,   GEN75_SURFACE_DW7_SCS_R) |
             GEN_SHIFT32(GEN75_SCS_GREEN, GEN75_SURFACE_DW7_SCS_G) |
             GEN_SHIFT32(GEN75_SCS_BLUE,  GEN75_SURFACE_DW7_SCS_B) |
             GEN_SHIFT32(GEN75_SCS_ALPHA, GEN75_SURFACE_DW7_SCS_A);
   }

   STATIC_ASSERT(ARRAY_SIZE(surf->surface) >= 13);
   surf->surface[0] = dw0;
   surf->surface[1] = dw1;
   surf->surface[2] = dw2;
   surf->surface[3] = dw3;
   surf->surface[4] = 0;
   surf->surface[5] = 0;
   surf->surface[6] = 0;
   surf->surface[7] = dw7;
   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      surf->surface[8] = info->offset;
      surf->surface[9] = 0;
      surf->surface[10] = 0;
      surf->surface[11] = 0;
      surf->surface[12] = 0;
   }

   surf->type = type;
   surf->min_lod = 0;
   surf->mip_count = 0;

   return true;
}

static bool
surface_validate_gen6_image(const struct ilo_dev *dev,
                            const struct ilo_state_surface_image_info *info)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   switch (info->access) {
   case ILO_STATE_SURFACE_ACCESS_SAMPLER:
   case ILO_STATE_SURFACE_ACCESS_DP_RENDER:
      break;
   case ILO_STATE_SURFACE_ACCESS_DP_TYPED:
      assert(ilo_dev_gen(dev) >= ILO_GEN(7));
      break;
   default:
      assert(!"unsupported surface access");
      break;
   }

   assert(info->img && info->vma);

   if (info->img->tiling != GEN6_TILING_NONE)
      assert(info->vma->vm_alignment % 4096 == 0);

   if (info->aux_vma) {
      assert(ilo_image_can_enable_aux(info->img, info->level_base));
      /* always tiled */
      assert(info->aux_vma->vm_alignment % 4096 == 0);
   }

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 78:
    *
    *     "For surface types other than SURFTYPE_BUFFER, the Width specified
    *      by this field must be less than or equal to the surface pitch
    *      (specified in bytes via the Surface Pitch field)."
    */
   assert(info->img->bo_stride && info->img->bo_stride <= 512 * 1024 &&
          info->img->width0 <= info->img->bo_stride);

   if (info->type != info->img->type) {
      assert(info->type == GEN6_SURFTYPE_2D &&
             info->img->type == GEN6_SURFTYPE_CUBE);
   }

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 78:
    *
    *     "For cube maps, Width must be set equal to the Height."
    */
   if (info->type == GEN6_SURFTYPE_CUBE)
      assert(info->img->width0 == info->img->height0);

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 72:
    *
    *     "Tile Walk TILEWALK_YMAJOR is UNDEFINED for render target formats
    *      that have 128 bits-per-element (BPE)."
    *
    *     "If Number of Multisamples is set to a value other than
    *      MULTISAMPLECOUNT_1, this field cannot be set to the following
    *      formats:
    *
    *      - any format with greater than 64 bits per element
    *      - any compressed texture format (BC*)
    *      - any YCRCB* format"
    *
    * From the Ivy Bridge PRM, volume 4 part 1, page 63:
    *
    *      If Number of Multisamples is set to a value other than
    *      MULTISAMPLECOUNT_1, this field cannot be set to the following
    *      formats: any format with greater than 64 bits per element, if
    *      Number of Multisamples is MULTISAMPLECOUNT_8, any compressed
    *      texture format (BC*), and any YCRCB* format.
    *
    * TODO
    */

   if (ilo_dev_gen(dev) < ILO_GEN(8) && info->img->tiling == GEN8_TILING_W) {
      ilo_warn("tiling W is not supported\n");
      return false;
   }

   return true;
}

static void
surface_get_gen6_image_max_extent(const struct ilo_dev *dev,
                                  const struct ilo_state_surface_image_info *info,
                                  uint16_t *max_w, uint16_t *max_h)
{
   const uint16_t max_size = (ilo_dev_gen(dev) >= ILO_GEN(7)) ? 16384 : 8192;

   ILO_DEV_ASSERT(dev, 6, 8);

   switch (info->type) {
   case GEN6_SURFTYPE_1D:
      *max_w = max_size;
      *max_h = 1;
      break;
   case GEN6_SURFTYPE_2D:
   case GEN6_SURFTYPE_CUBE:
      *max_w = max_size;
      *max_h = max_size;
      break;
   case GEN6_SURFTYPE_3D:
      *max_w = 2048;
      *max_h = 2048;
      break;
   default:
      assert(!"invalid surface type");
      *max_w = 1;
      *max_h = 1;
      break;
   }
}

static bool
surface_get_gen6_image_extent(const struct ilo_dev *dev,
                              const struct ilo_state_surface_image_info *info,
                              uint16_t *width, uint16_t *height)
{
   uint16_t w, h, max_w, max_h;

   ILO_DEV_ASSERT(dev, 6, 8);

   w = info->img->width0;
   h = info->img->height0;

   surface_get_gen6_image_max_extent(dev, info, &max_w, &max_h);
   assert(w && h && w <= max_w && h <= max_h);

   *width = w - 1;
   *height = h - 1;

   return true;
}

static bool
surface_get_gen6_image_slices(const struct ilo_dev *dev,
                              const struct ilo_state_surface_image_info *info,
                              uint16_t *depth, uint16_t *min_array_elem,
                              uint16_t *rt_view_extent)
{
   uint16_t max_slice, d;

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Ivy Bridge PRM, volume 4 part 1, page 63:
    *
    *     "If this field (Surface Array) is enabled, the Surface Type must be
    *      SURFTYPE_1D, SURFTYPE_2D, or SURFTYPE_CUBE. If this field is
    *      disabled and Surface Type is SURFTYPE_1D, SURFTYPE_2D, or
    *      SURFTYPE_CUBE, the Depth field must be set to zero."
    *
    * From the Ivy Bridge PRM, volume 4 part 1, page 69:
    *
    *     "This field (Depth) specifies the total number of levels for a
    *      volume texture or the number of array elements allowed to be
    *      accessed starting at the Minimum Array Element for arrayed
    *      surfaces.  If the volume texture is MIP-mapped, this field
    *      specifies the depth of the base MIP level."
    *
    *     "For SURFTYPE_CUBE:For Sampling Engine Surfaces, the range of this
    *      field is [0,340], indicating the number of cube array elements
    *      (equal to the number of underlying 2D array elements divided by 6).
    *      For other surfaces, this field must be zero."
    *
    *     "Errata: For SURFTYPE_CUBE sampling engine surfaces, the range of
    *      this field is limited to [0,85].
    *
    *      Errata: If Surface Array is enabled, and Depth is between 1024 and
    *      2047, an incorrect array slice may be accessed if the requested
    *      array index in the message is greater than or equal to 4096."
    *
    * The errata are for Gen7-specific, and they limit the number of useable
    * layers to (86 * 6), about 512.
    */

   switch (info->type) {
   case GEN6_SURFTYPE_1D:
   case GEN6_SURFTYPE_2D:
   case GEN6_SURFTYPE_CUBE:
      max_slice = (ilo_dev_gen(dev) >= ILO_GEN(7.5)) ? 2048 : 512;

      assert(info->img->array_size <= max_slice);
      max_slice = info->img->array_size;

      d = info->slice_count;
      if (info->type == GEN6_SURFTYPE_CUBE) {
         if (info->access == ILO_STATE_SURFACE_ACCESS_SAMPLER) {
            if (!d || d % 6) {
               ilo_warn("invalid cube slice count\n");
               return false;
            }

            if (ilo_dev_gen(dev) == ILO_GEN(7) && d > 86 * 6) {
               ilo_warn("cube slice count exceeds Gen7 limit\n");
               return false;
            }
         } else {
            /*
             * Minumum Array Element and Depth must be 0; Render Target View
             * Extent is ignored.
             */
            if (info->slice_base || d != 6) {
               ilo_warn("no cube RT array support in data port\n");
               return false;
            }
         }

         d /= 6;
      }

      if (!info->is_array && d > 1) {
         ilo_warn("non-array surface with non-zero depth\n");
         return false;
      }
      break;
   case GEN6_SURFTYPE_3D:
      max_slice = 2048;

      assert(info->img->depth0 <= max_slice);
      max_slice = u_minify(info->img->depth0, info->level_base);

      d = info->img->depth0;

      if (info->is_array) {
         ilo_warn("3D surfaces cannot be arrays\n");
         return false;
      }
      break;
   default:
      assert(!"invalid surface type");
      return false;
      break;
   }

   if (!info->slice_count ||
       info->slice_base + info->slice_count > max_slice) {
      ilo_warn("invalid slice range\n");
      return false;
   }

   assert(d);
   *depth = d - 1;

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 84:
    *
    *     "For Sampling Engine and Render Target 1D and 2D Surfaces:
    *      This field (Minimum Array Element) indicates the minimum array
    *      element that can be accessed as part of this surface.  This field
    *      is added to the delivered array index before it is used to address
    *      the surface.
    *
    *      For Render Target 3D Surfaces:
    *      This field indicates the minimum `R' coordinate on the LOD
    *      currently being rendered to.  This field is added to the delivered
    *      array index before it is used to address the surface.
    *
    *      For Sampling Engine Cube Surfaces on [DevSNB+] only:
    *      This field indicates the minimum array element in the underlying 2D
    *      surface array that can be accessed as part of this surface (the
    *      cube array index is multipled by 6 to compute this value, although
    *      this field is not restricted to only multiples of 6). This field is
    *      added to the delivered array index before it is used to address the
    *      surface.
    *
    *      For Other Surfaces:
    *      This field must be set to zero."
    *
    * On Gen7+, typed sufaces are treated like sampling engine 1D and 2D
    * surfaces.
    */
   *min_array_elem = info->slice_base;

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 84:
    *
    *     "For Render Target 3D Surfaces:
    *      This field (Render Target View Extent) indicates the extent of the
    *      accessible `R' coordinates minus 1 on the LOD currently being
    *      rendered to.
    *
    *      For Render Target 1D and 2D Surfaces:
    *      This field must be set to the same value as the Depth field.
    *
    *      For Other Surfaces:
    *      This field is ignored."
    */
   *rt_view_extent = info->slice_count - 1;

   return true;
}

static bool
surface_get_gen6_image_levels(const struct ilo_dev *dev,
                              const struct ilo_state_surface_image_info *info,
                              uint8_t *min_lod, uint8_t *mip_count)
{
   uint8_t max_level = (ilo_dev_gen(dev) >= ILO_GEN(7)) ? 15 : 14;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(info->img->level_count <= max_level);
   max_level = info->img->level_count;

   if (!info->level_count ||
       info->level_base + info->level_count > max_level) {
      ilo_warn("invalid level range\n");
      return false;
   }

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 79:
    *
    *     "For Sampling Engine Surfaces:
    *      This field (MIP Count / LOD) indicates the number of MIP levels
    *      allowed to be accessed starting at Surface Min LOD, which must be
    *      less than or equal to the number of MIP levels actually stored in
    *      memory for this surface.
    *
    *      Force the mip map access to be between the mipmap specified by the
    *      integer bits of the Min LOD and the ceiling of the value specified
    *      here.
    *
    *      For Render Target Surfaces:
    *      This field defines the MIP level that is currently being rendered
    *      into. This is the absolute MIP level on the surface and is not
    *      relative to the Surface Min LOD field, which is ignored for render
    *      target surfaces.
    *
    *      For Other Surfaces:
    *      This field is reserved : MBZ"
    *
    * From the Sandy Bridge PRM, volume 4 part 1, page 83:
    *
    *     "For Sampling Engine Surfaces:
    *
    *      This field (Surface Min LOD) indicates the most detailed LOD that
    *      can be accessed as part of this surface.  This field is added to
    *      the delivered LOD (sample_l, ld, or resinfo message types) before
    *      it is used to address the surface.
    *
    *      For Other Surfaces:
    *      This field is ignored."
    *
    * On Gen7+, typed sufaces are treated like sampling engine surfaces.
    */
   if (info->access == ILO_STATE_SURFACE_ACCESS_DP_RENDER) {
      assert(info->level_count == 1);

      *min_lod = 0;
      *mip_count = info->level_base;
   } else {
      *min_lod = info->level_base;
      *mip_count = info->level_count - 1;
   }

   return true;
}

static bool
surface_get_gen6_image_sample_count(const struct ilo_dev *dev,
                                    const struct ilo_state_surface_image_info *info,
                                    enum gen_sample_count *sample_count)
{
   int min_gen;

   ILO_DEV_ASSERT(dev, 6, 8);

   switch (info->img->sample_count) {
   case 1:
      *sample_count = GEN6_NUMSAMPLES_1;
      min_gen = ILO_GEN(6);
      break;
   case 2:
      *sample_count = GEN8_NUMSAMPLES_2;
      min_gen = ILO_GEN(8);
      break;
   case 4:
      *sample_count = GEN6_NUMSAMPLES_4;
      min_gen = ILO_GEN(6);
      break;
   case 8:
      *sample_count = GEN7_NUMSAMPLES_8;
      min_gen = ILO_GEN(7);
      break;
   default:
      assert(!"invalid sample count");
      *sample_count = GEN6_NUMSAMPLES_1;
      break;
   }

   assert(ilo_dev_gen(dev) >= min_gen);

   return true;
}

static bool
surface_get_gen6_image_alignments(const struct ilo_dev *dev,
                                  const struct ilo_state_surface_image_info *info,
                                  uint32_t *alignments)
{
   uint32_t a = 0;
   bool err = false;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      switch (info->img->align_i) {
      case 4:
         a |= GEN8_SURFACE_DW0_HALIGN_4;
         break;
      case 8:
         a |= GEN8_SURFACE_DW0_HALIGN_8;
         break;
      case 16:
         a |= GEN8_SURFACE_DW0_HALIGN_16;
         break;
      default:
         err = true;
         break;
      }

      switch (info->img->align_j) {
      case 4:
         a |= GEN7_SURFACE_DW0_VALIGN_4;
         break;
      case 8:
         a |= GEN8_SURFACE_DW0_VALIGN_8;
         break;
      case 16:
         a |= GEN8_SURFACE_DW0_VALIGN_16;
         break;
      default:
         err = true;
         break;
      }
   } else if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      switch (info->img->align_i) {
      case 4:
         a |= GEN7_SURFACE_DW0_HALIGN_4;
         break;
      case 8:
         a |= GEN7_SURFACE_DW0_HALIGN_8;
         break;
      default:
         err = true;
         break;
      }

      switch (info->img->align_j) {
      case 2:
         a |= GEN7_SURFACE_DW0_VALIGN_2;
         break;
      case 4:
         a |= GEN7_SURFACE_DW0_VALIGN_4;
         break;
      default:
         err = true;
         break;
      }
   } else {
      if (info->img->align_i != 4)
         err = true;

      switch (info->img->align_j) {
      case 2:
         a |= GEN6_SURFACE_DW5_VALIGN_2;
         break;
      case 4:
         a |= GEN6_SURFACE_DW5_VALIGN_4;
         break;
      default:
         err = true;
         break;
      }
   }

   if (err)
      assert(!"invalid HALIGN or VALIGN");

   *alignments = a;

   return true;
}

static bool
surface_set_gen6_image_SURFACE_STATE(struct ilo_state_surface *surf,
                                     const struct ilo_dev *dev,
                                     const struct ilo_state_surface_image_info *info)
{
   uint16_t width, height, depth, array_base, view_extent;
   uint8_t min_lod, mip_count;
   enum gen_sample_count sample_count;
   uint32_t alignments;
   uint32_t dw0, dw2, dw3, dw4, dw5;

   ILO_DEV_ASSERT(dev, 6, 6);

   if (!surface_validate_gen6_image(dev, info) ||
       !surface_get_gen6_image_extent(dev, info, &width, &height) ||
       !surface_get_gen6_image_slices(dev, info, &depth, &array_base,
                                      &view_extent) ||
       !surface_get_gen6_image_levels(dev, info, &min_lod, &mip_count) ||
       !surface_get_gen6_image_sample_count(dev, info, &sample_count) ||
       !surface_get_gen6_image_alignments(dev, info, &alignments))
      return false;

   /* no ARYSPC_LOD0 */
   assert(info->img->walk != ILO_IMAGE_WALK_LOD);
   /* no UMS/CMS */
   if (info->img->sample_count > 1)
      assert(info->img->interleaved_samples);

   dw0 = info->type << GEN6_SURFACE_DW0_TYPE__SHIFT |
         info->format << GEN6_SURFACE_DW0_FORMAT__SHIFT |
         GEN6_SURFACE_DW0_MIPLAYOUT_BELOW;

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 74:
    *
    *     "CUBE_AVERAGE may only be selected if all of the Cube Face Enable
    *      fields are equal to one."
    *
    * From the Sandy Bridge PRM, volume 4 part 1, page 75-76:
    *
    *     "For SURFTYPE_CUBE Surfaces accessed via the Sampling Engine:
    *      Bits 5:0 of this field (Cube Face Enables) enable the individual
    *      faces of a cube map.  Enabling a face indicates that the face is
    *      present in the cube map, while disabling it indicates that that
    *      face is represented by the texture map's border color. Refer to
    *      Memory Data Formats for the correlation between faces and the cube
    *      map memory layout. Note that storage for disabled faces must be
    *      provided.
    *
    *      For other surfaces:
    *      This field is reserved : MBZ"
    *
    *     "When TEXCOORDMODE_CLAMP is used when accessing a cube map, this
    *      field must be programmed to 111111b (all faces enabled)."
    */
   if (info->type == GEN6_SURFTYPE_CUBE &&
       info->access == ILO_STATE_SURFACE_ACCESS_SAMPLER) {
      dw0 |= GEN6_SURFACE_DW0_CUBE_MAP_CORNER_MODE_AVERAGE |
             GEN6_SURFACE_DW0_CUBE_FACE_ENABLES__MASK;
   }

   dw2 = height << GEN6_SURFACE_DW2_HEIGHT__SHIFT |
         width << GEN6_SURFACE_DW2_WIDTH__SHIFT |
         mip_count << GEN6_SURFACE_DW2_MIP_COUNT_LOD__SHIFT;

   dw3 = depth << GEN6_SURFACE_DW3_DEPTH__SHIFT |
         (info->img->bo_stride - 1) << GEN6_SURFACE_DW3_PITCH__SHIFT |
         info->img->tiling << GEN6_SURFACE_DW3_TILING__SHIFT;

   dw4 = min_lod << GEN6_SURFACE_DW4_MIN_LOD__SHIFT |
         array_base << GEN6_SURFACE_DW4_MIN_ARRAY_ELEMENT__SHIFT |
         view_extent << GEN6_SURFACE_DW4_RT_VIEW_EXTENT__SHIFT |
         sample_count << GEN6_SURFACE_DW4_MULTISAMPLECOUNT__SHIFT;

   dw5 = alignments;

   STATIC_ASSERT(ARRAY_SIZE(surf->surface) >= 6);
   surf->surface[0] = dw0;
   surf->surface[1] = 0;
   surf->surface[2] = dw2;
   surf->surface[3] = dw3;
   surf->surface[4] = dw4;
   surf->surface[5] = dw5;

   surf->type = info->type;
   surf->min_lod = min_lod;
   surf->mip_count = mip_count;

   return true;
}

static bool
surface_set_gen7_image_SURFACE_STATE(struct ilo_state_surface *surf,
                                     const struct ilo_dev *dev,
                                     const struct ilo_state_surface_image_info *info)
{
   uint16_t width, height, depth, array_base, view_extent;
   uint8_t min_lod, mip_count;
   uint32_t alignments;
   enum gen_sample_count sample_count;
   uint32_t dw0, dw1, dw2, dw3, dw4, dw5, dw7;

   ILO_DEV_ASSERT(dev, 7, 8);

   if (!surface_validate_gen6_image(dev, info) ||
       !surface_get_gen6_image_extent(dev, info, &width, &height) ||
       !surface_get_gen6_image_slices(dev, info, &depth, &array_base,
                                      &view_extent) ||
       !surface_get_gen6_image_levels(dev, info, &min_lod, &mip_count) ||
       !surface_get_gen6_image_sample_count(dev, info, &sample_count) ||
       !surface_get_gen6_image_alignments(dev, info, &alignments))
      return false;

   dw0 = info->type << GEN7_SURFACE_DW0_TYPE__SHIFT |
         info->format << GEN7_SURFACE_DW0_FORMAT__SHIFT |
         alignments;

   if (info->is_array)
      dw0 |= GEN7_SURFACE_DW0_IS_ARRAY;

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      dw0 |= info->img->tiling << GEN8_SURFACE_DW0_TILING__SHIFT;
   } else {
      dw0 |= info->img->tiling << GEN7_SURFACE_DW0_TILING__SHIFT;

      if (info->img->walk == ILO_IMAGE_WALK_LOD)
         dw0 |= GEN7_SURFACE_DW0_ARYSPC_LOD0;
      else
         dw0 |= GEN7_SURFACE_DW0_ARYSPC_FULL;
   }

   /*
    * From the Ivy Bridge PRM, volume 4 part 1, page 67:
    *
    *     "For SURFTYPE_CUBE Surfaces accessed via the Sampling Engine: Bits
    *      5:0 of this field (Cube Face Enables) enable the individual faces
    *      of a cube map. Enabling a face indicates that the face is present
    *      in the cube map, while disabling it indicates that that face is
    *      represented by the texture map's border color. Refer to Memory Data
    *      Formats for the correlation between faces and the cube map memory
    *      layout. Note that storage for disabled faces must be provided. For
    *      other surfaces this field is reserved and MBZ."
    *
    *     "When TEXCOORDMODE_CLAMP is used when accessing a cube map, this
    *      field must be programmed to 111111b (all faces enabled). This field
    *      is ignored unless the Surface Type is SURFTYPE_CUBE."
    */
   if (info->type == GEN6_SURFTYPE_CUBE &&
       info->access == ILO_STATE_SURFACE_ACCESS_SAMPLER)
      dw0 |= GEN7_SURFACE_DW0_CUBE_FACE_ENABLES__MASK;

   dw1 = 0;
   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      assert(info->img->walk_layer_height % 4 == 0);
      dw1 |= info->img->walk_layer_height / 4 <<
         GEN8_SURFACE_DW1_QPITCH__SHIFT;
   }

   dw2 = height << GEN7_SURFACE_DW2_HEIGHT__SHIFT |
         width << GEN7_SURFACE_DW2_WIDTH__SHIFT;

   dw3 = depth << GEN7_SURFACE_DW3_DEPTH__SHIFT |
         (info->img->bo_stride - 1) << GEN7_SURFACE_DW3_PITCH__SHIFT;

   if (ilo_dev_gen(dev) == ILO_GEN(7.5))
      dw3 |= 0 << GEN75_SURFACE_DW3_INTEGER_SURFACE_FORMAT__SHIFT;

   dw4 = array_base << GEN7_SURFACE_DW4_MIN_ARRAY_ELEMENT__SHIFT |
         view_extent << GEN7_SURFACE_DW4_RT_VIEW_EXTENT__SHIFT |
         sample_count << GEN7_SURFACE_DW4_MULTISAMPLECOUNT__SHIFT;

   /*
    * MSFMT_MSS means the samples are not interleaved and MSFMT_DEPTH_STENCIL
    * means the samples are interleaved.  The layouts are the same when the
    * number of samples is 1.
    */
   if (info->img->interleaved_samples && info->img->sample_count > 1) {
      assert(info->access != ILO_STATE_SURFACE_ACCESS_DP_RENDER);
      dw4 |= GEN7_SURFACE_DW4_MSFMT_DEPTH_STENCIL;
   } else {
      dw4 |= GEN7_SURFACE_DW4_MSFMT_MSS;
   }

   dw5 = min_lod << GEN7_SURFACE_DW5_MIN_LOD__SHIFT |
         mip_count << GEN7_SURFACE_DW5_MIP_COUNT_LOD__SHIFT;

   dw7 = 0;
   if (ilo_dev_gen(dev) >= ILO_GEN(7.5)) {
      dw7 |= GEN_SHIFT32(GEN75_SCS_RED,   GEN75_SURFACE_DW7_SCS_R) |
             GEN_SHIFT32(GEN75_SCS_GREEN, GEN75_SURFACE_DW7_SCS_G) |
             GEN_SHIFT32(GEN75_SCS_BLUE,  GEN75_SURFACE_DW7_SCS_B) |
             GEN_SHIFT32(GEN75_SCS_ALPHA, GEN75_SURFACE_DW7_SCS_A);
   }

   STATIC_ASSERT(ARRAY_SIZE(surf->surface) >= 13);
   surf->surface[0] = dw0;
   surf->surface[1] = dw1;
   surf->surface[2] = dw2;
   surf->surface[3] = dw3;
   surf->surface[4] = dw4;
   surf->surface[5] = dw5;
   surf->surface[6] = 0;
   surf->surface[7] = dw7;
   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      surf->surface[8] = 0;
      surf->surface[9] = 0;
      surf->surface[10] = 0;
      surf->surface[11] = 0;
      surf->surface[12] = 0;
   }

   surf->type = info->type;
   surf->min_lod = min_lod;
   surf->mip_count = mip_count;

   return true;
}

uint32_t
ilo_state_surface_buffer_size(const struct ilo_dev *dev,
                              enum ilo_state_surface_access access,
                              uint32_t size, uint32_t *alignment)
{
   switch (access) {
   case ILO_STATE_SURFACE_ACCESS_SAMPLER:
      /*
       * From the Sandy Bridge PRM, volume 1 part 1, page 118:
       *
       *     "For buffers, which have no inherent "height," padding
       *      requirements are different. A buffer must be padded to the next
       *      multiple of 256 array elements, with an additional 16 bytes
       *      added beyond that to account for the L1 cache line."
       *
       * Assuming tightly packed GEN6_FORMAT_R32G32B32A32_FLOAT, the size
       * needs to be padded to 4096 (= 16 * 256).
       */
      *alignment = 1;
      size = align(size, 4096) + 16;
      break;
   case ILO_STATE_SURFACE_ACCESS_DP_RENDER:
   case ILO_STATE_SURFACE_ACCESS_DP_TYPED:
      /* element-size aligned for worst cases */
      *alignment = 16;
      break;
   case ILO_STATE_SURFACE_ACCESS_DP_UNTYPED:
      /* DWord aligned? */
      *alignment = 4;
      break;
   case ILO_STATE_SURFACE_ACCESS_DP_DATA:
      /* OWord aligned */
      *alignment = 16;
      size = align(size, 16);
      break;
   case ILO_STATE_SURFACE_ACCESS_DP_SVB:
      /* always DWord aligned */
      *alignment = 4;
      break;
   default:
      assert(!"unknown access");
      *alignment = 1;
      break;
   }

   return size;
}

bool
ilo_state_surface_init_for_null(struct ilo_state_surface *surf,
                                const struct ilo_dev *dev)
{
   bool ret = true;

   assert(ilo_is_zeroed(surf, sizeof(*surf)));

   if (ilo_dev_gen(dev) >= ILO_GEN(7))
      ret &= surface_set_gen7_null_SURFACE_STATE(surf, dev);
   else
      ret &= surface_set_gen6_null_SURFACE_STATE(surf, dev);

   surf->vma = NULL;
   surf->type = GEN6_SURFTYPE_NULL;
   surf->readonly = true;

   assert(ret);

   return ret;
}

bool
ilo_state_surface_init_for_buffer(struct ilo_state_surface *surf,
                                  const struct ilo_dev *dev,
                                  const struct ilo_state_surface_buffer_info *info)
{
   bool ret = true;

   assert(ilo_is_zeroed(surf, sizeof(*surf)));

   if (ilo_dev_gen(dev) >= ILO_GEN(7))
      ret &= surface_set_gen7_buffer_SURFACE_STATE(surf, dev, info);
   else
      ret &= surface_set_gen6_buffer_SURFACE_STATE(surf, dev, info);

   surf->vma = info->vma;
   surf->readonly = info->readonly;

   assert(ret);

   return ret;
}

bool
ilo_state_surface_init_for_image(struct ilo_state_surface *surf,
                                 const struct ilo_dev *dev,
                                 const struct ilo_state_surface_image_info *info)
{
   bool ret = true;

   assert(ilo_is_zeroed(surf, sizeof(*surf)));

   if (ilo_dev_gen(dev) >= ILO_GEN(7))
      ret &= surface_set_gen7_image_SURFACE_STATE(surf, dev, info);
   else
      ret &= surface_set_gen6_image_SURFACE_STATE(surf, dev, info);

   surf->vma = info->vma;
   surf->aux_vma = info->aux_vma;

   surf->is_integer = info->is_integer;
   surf->readonly = info->readonly;
   surf->scanout = info->img->scanout;

   assert(ret);

   return ret;
}

bool
ilo_state_surface_set_scs(struct ilo_state_surface *surf,
                          const struct ilo_dev *dev,
                          enum gen_surface_scs rgba[4])
{
   const uint32_t scs = GEN_SHIFT32(rgba[0], GEN75_SURFACE_DW7_SCS_R) |
                        GEN_SHIFT32(rgba[1], GEN75_SURFACE_DW7_SCS_G) |
                        GEN_SHIFT32(rgba[2], GEN75_SURFACE_DW7_SCS_B) |
                        GEN_SHIFT32(rgba[3], GEN75_SURFACE_DW7_SCS_A);

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(ilo_dev_gen(dev) >= ILO_GEN(7.5));

   surf->surface[7] = (surf->surface[7] & ~GEN75_SURFACE_DW7_SCS__MASK) | scs;

   return true;
}
