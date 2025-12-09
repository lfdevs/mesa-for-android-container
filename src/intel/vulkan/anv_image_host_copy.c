/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>

#include "anv_private.h"
#include "util/macros.h"
#include "util/texcompress_astc.h"
#include "util/u_cpu_detect.h"
#include "util/u_debug.h"
#include "vk_util.h"

#define TMP_BUFFER_SIZE 4096

static inline VkOffset3D
vk_offset3d_to_el(enum isl_format format, VkOffset3D offset)
{
   const struct isl_format_layout *fmt_layout =
      isl_format_get_layout(format);
   return (VkOffset3D) {
      .x = offset.x / fmt_layout->bw,
      .y = offset.y / fmt_layout->bh,
      .z = offset.z / fmt_layout->bd,
   };
}

static inline VkOffset3D
vk_el_to_offset3d(enum isl_format format, VkOffset3D offset)
{
   const struct isl_format_layout *fmt_layout =
      isl_format_get_layout(format);
   return (VkOffset3D) {
      .x = offset.x * fmt_layout->bw,
      .y = offset.y * fmt_layout->bh,
      .z = offset.z * fmt_layout->bd,
   };
}

static inline VkExtent3D
vk_extent3d_to_el(enum isl_format format, VkExtent3D extent)
{
   const struct isl_format_layout *fmt_layout =
      isl_format_get_layout(format);
   return (VkExtent3D) {
      .width  = DIV_ROUND_UP(extent.width,  fmt_layout->bw),
      .height = DIV_ROUND_UP(extent.height, fmt_layout->bh),
      .depth  = DIV_ROUND_UP(extent.depth,  fmt_layout->bd),
   };
}

static inline VkExtent3D
vk_el_to_extent3d(enum isl_format format, VkExtent3D extent)
{
   const struct isl_format_layout *fmt_layout =
      isl_format_get_layout(format);
   return (VkExtent3D) {
      .width  = extent.width  * fmt_layout->bw,
      .height = extent.height * fmt_layout->bh,
      .depth  = extent.depth  * fmt_layout->bd,
   };
}

static void
get_image_offset_el(const struct isl_surf *surf, unsigned level, unsigned z,
                    uint32_t *out_x0_el, uint32_t *out_y0_el)
{
   ASSERTED uint32_t z0_el, a0_el;
   if (surf->dim == ISL_SURF_DIM_3D) {
      isl_surf_get_image_offset_el(surf, level, 0, z,
                                   out_x0_el, out_y0_el, &z0_el, &a0_el);
   } else {
      isl_surf_get_image_offset_el(surf, level, z, 0,
                                   out_x0_el, out_y0_el, &z0_el, &a0_el);
   }
   assert(z0_el == 0 && a0_el == 0);
}

/* Compute extent parameters for use with tiled_memcpy functions.
 * xs are in units of bytes and ys are in units of strides.
 */
static inline void
tile_extents(const struct isl_surf *surf,
             const VkOffset3D *offset_el,
             const VkExtent3D *extent_el,
             unsigned level, int z,
             uint32_t *x1_B, uint32_t *x2_B,
             uint32_t *y1_el, uint32_t *y2_el)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(surf->format);
   const unsigned cpp = fmtl->bpb / 8;

   /* z contains offset->z */
   assert (z >= offset_el->z);

   unsigned x0_el, y0_el;
   get_image_offset_el(surf, level, z, &x0_el, &y0_el);

   *x1_B = (offset_el->x + x0_el) * cpp;
   *y1_el = offset_el->y + y0_el;
   *x2_B = (offset_el->x + extent_el->width + x0_el) * cpp;
   *y2_el = offset_el->y + extent_el->height + y0_el;
}

static void
anv_copy_image_memory(struct anv_device *device,
                      const struct isl_surf *surf,
                      const struct anv_image_binding *binding,
                      uint64_t binding_offset,
                      void *mem_ptr,
                      uint64_t mem_row_pitch_B,
                      const VkOffset3D *offset_el,
                      const VkExtent3D *extent_el,
                      uint32_t level,
                      uint32_t base_img_array_layer,
                      uint32_t base_img_z_offset_px,
                      uint32_t array_layer,
                      uint32_t z_offset_px,
                      bool mem_to_img)
{
   const struct isl_format_layout *fmt_layout =
      isl_format_get_layout(surf->format);
   const uint32_t bs = fmt_layout->bpb / 8;
   void *img_ptr = binding->host_map + binding->map_delta + binding_offset;

   uint64_t start_tile_B, end_tile_B;
   isl_surf_get_image_range_B_tile(surf, level,
                                   base_img_array_layer + array_layer,
                                   base_img_z_offset_px + z_offset_px,
                                   &start_tile_B, &end_tile_B);

#ifdef SUPPORT_INTEL_INTEGRATED_GPUS
   const bool need_invalidate_flush =
      (binding->address.bo->flags & ANV_BO_ALLOC_HOST_COHERENT) == 0 &&
      device->physical->memory.need_flush;
   if (need_invalidate_flush && !mem_to_img)
      util_flush_inval_range(img_ptr + start_tile_B, end_tile_B - start_tile_B);
#endif

   uint32_t img_depth_or_layer = MAX2(base_img_array_layer + array_layer,
                                      base_img_z_offset_px + z_offset_px);

   if (surf->tiling == ISL_TILING_LINEAR) {
      uint64_t img_col_offset = offset_el->x * bs;
      uint64_t row_copy_size = extent_el->width * bs;
      for (uint32_t h_el = 0; h_el < extent_el->height; h_el++) {
         uint64_t mem_row_offset = h_el * mem_row_pitch_B;
         uint64_t img_row = h_el + offset_el->y;
         uint64_t img_offset =
            start_tile_B + img_row * surf->row_pitch_B + img_col_offset;
         assert((img_offset + row_copy_size) <= binding->memory_range.size);

         if (mem_to_img)
            memcpy(img_ptr + img_offset, mem_ptr + mem_row_offset, row_copy_size);
         else
            memcpy(mem_ptr + mem_row_offset, img_ptr + img_offset, row_copy_size);
      }
   } else {
      uint32_t x1, x2, y1, y2;
      tile_extents(surf, offset_el, extent_el, level, img_depth_or_layer,
                   &x1, &x2, &y1, &y2);

      if (mem_to_img) {
         isl_memcpy_linear_to_tiled(x1, x2, y1, y2,
                                    img_ptr,
                                    mem_ptr,
                                    surf->row_pitch_B,
                                    mem_row_pitch_B,
                                    false,
                                    surf->tiling,
                                    ISL_MEMCPY);
      } else {
         isl_memcpy_tiled_to_linear(x1, x2, y1, y2,
                                    mem_ptr,
                                    img_ptr,
                                    mem_row_pitch_B,
                                    surf->row_pitch_B,
                                    false,
                                    surf->tiling,
#if defined(USE_SSE41)
                                    util_get_cpu_caps()->has_sse4_1 ?
                                    ISL_MEMCPY_STREAMING_LOAD :
#endif
                                    ISL_MEMCPY);
      }
   }

#ifdef SUPPORT_INTEL_INTEGRATED_GPUS
   if (need_invalidate_flush && mem_to_img)
      util_flush_range(img_ptr + start_tile_B, end_tile_B - start_tile_B);
#endif
}

static uint64_t
calc_mem_row_pitch_B(enum isl_format format,
                     uint64_t api_row_length_px,
                     const VkExtent3D *extent_px)
{
   const struct isl_format_layout *fmt_layout =
      isl_format_get_layout(format);
   const uint32_t bs = fmt_layout->bpb / 8;

   return api_row_length_px != 0 ?
      (bs * DIV_ROUND_UP(api_row_length_px, fmt_layout->bw)) :
      (bs * DIV_ROUND_UP(extent_px->width, fmt_layout->bw));
}

static uint64_t
calc_mem_height_pitch_B(enum isl_format format,
                        uint64_t row_pitch_B,
                        uint64_t api_height_px,
                        const VkExtent3D *extent_px)
{
   const struct isl_format_layout *fmt_layout =
      isl_format_get_layout(format);

   return api_height_px != 0 ?
      (row_pitch_B * DIV_ROUND_UP(api_height_px, fmt_layout->bh)) :
      (row_pitch_B * DIV_ROUND_UP(extent_px->height, fmt_layout->bh));
}

/* TODO: Get rid of this.
 *
 * For three component RGB images created with optimal layout, we actually
 * create an RGBX or RGBA(with swizzle ALPHA_ONE), as the HW cannot handle
 * tiling of non-power of 2 formats. This is a problem for host image copy, as
 * the isl_memcpy functions are not prepared to deal with the RGB <-> RGBX
 * conversion necessary.
 */
static bool
needs_temp_copy(struct anv_image *image, VkHostImageCopyFlags flags)
{
   if (image->vk.tiling != VK_IMAGE_TILING_OPTIMAL ||
       flags & VK_HOST_IMAGE_COPY_MEMCPY_BIT)
      return false;

   /* Skip depth/stencil formats */
   if (vk_format_is_depth_or_stencil(image->vk.format))
      return false;

   /* Need temp copy for RGB formats (3 components) */
   bool is_rgb = util_format_get_nr_components(vk_format_to_pipe_format(image->vk.format)) == 3;

   /* Need temp copy for emulated formats (ASTC) */
   bool is_emulated = image->emu_plane_format != VK_FORMAT_UNDEFINED;

   return is_rgb || is_emulated;
}

/* Callback typedef for converting data through intermediate buffer */
typedef void (*intermediate_conversion_fn)(
   const uint8_t *src,
   uint64_t src_stride_B,
   uint8_t *dst,
   uint64_t dst_stride_B,
   const VkExtent3D *extent,
   const void *user_data);

/* Data structure for RGB conversion parameters */
struct rgb_conversion_params {
   int src_bpp;
   int dst_bpp;
};

/* RGB<->RGBA conversion callback */
static void
rgb_rgba_conversion_callback(const uint8_t *src,
                             uint64_t src_stride_B,
                             uint8_t *dst,
                             uint64_t dst_stride_B,
                             const VkExtent3D *extent,
                             const void *user_data)
{
   const struct rgb_conversion_params *params = user_data;
   int bpp = MIN2(params->src_bpp, params->dst_bpp);

   for (int y = 0; y < extent->height; y++) {
      const uint8_t *row_src = src + y * src_stride_B;
      uint8_t *row_dst = dst + y * dst_stride_B;
      for (int x = 0; x < extent->width; x++) {
         memcpy(row_dst, row_src, bpp);
         row_src += params->src_bpp;
         row_dst += params->dst_bpp;
      }
   }
}

/* ASTC decompression callback */
static void
astc_decompression_callback(const uint8_t *src,
                            uint64_t src_stride_B,
                            uint8_t *dst,
                            uint64_t dst_stride_B,
                            const VkExtent3D *extent,
                            const void *user_data)
{
   const struct util_format_description *desc = user_data;

   _mesa_unpack_astc_2d_ldr(dst, dst_stride_B,
                            src, src_stride_B,
                            extent->width, extent->height,
                            desc->format);
}

static void
copy_intermediate(struct anv_device *device,
                  const void *mem_ptr,
                  uint32_t mem_row_pitch_B,
                  enum isl_format mem_format,
                  struct anv_image *image,
                  const struct anv_surface *anv_surf,
                  const void *region_ptr,
                  bool mem_to_img,
                  void *tmp_mem,
                  uint32_t a, uint32_t z,
                  intermediate_conversion_fn callback,
                  const void *callback_data)
{
   /* Extract region fields based on direction.
    * Both VkMemoryToImageCopy and VkImageToMemoryCopy have compatible layouts
    * for the fields we need (imageSubresource, imageOffset, imageExtent).
    */
   const VkMemoryToImageCopy *mem_to_img_region = region_ptr;
   const VkImageToMemoryCopy *img_to_mem_region = region_ptr;
   const VkImageSubresourceLayers *imageSubresource =
      mem_to_img ? &mem_to_img_region->imageSubresource :
                   &img_to_mem_region->imageSubresource;
   VkOffset3D imageOffset =
      mem_to_img ? mem_to_img_region->imageOffset :
                   img_to_mem_region->imageOffset;
   VkExtent3D imageExtent =
      mem_to_img ? mem_to_img_region->imageExtent :
                   img_to_mem_region->imageExtent;
   const struct isl_surf *surf = &anv_surf->isl;
   const struct anv_image_binding *binding =
      &image->bindings[anv_surf->memory_range.binding];
   const struct isl_format_layout *mem_fmt_layout =
      isl_format_get_layout(mem_format);
   uint32_t mem_bpp = mem_fmt_layout->bpb / 8;

   /* There is no requirement that the extent be aligned to the texel block size. */
   VkOffset3D offset_el = vk_offset3d_to_el(mem_format, imageOffset);
   VkExtent3D extent_el = vk_extent3d_to_el(mem_format, imageExtent);

   struct isl_tile_info tile;
   isl_surf_get_tile_info(surf, &tile);

   uint32_t tile_width_B   = tile.phys_extent_B.w;
   uint32_t tile_width_el  = tile.logical_extent_el.w;
   uint32_t tile_height_el = tile.logical_extent_el.h;
   if (tile_width_el == 1 && tile_height_el == 1) {
      tile_width_el = MIN2(4096 / (mem_fmt_layout->bpb / 8), extent_el.width);
      tile_height_el = 4096 / (tile_width_el * (mem_fmt_layout->bpb / 8));
      tile_width_B = tile_width_el / mem_fmt_layout->bw;
   }

   for (uint32_t y_el = 0; y_el < extent_el.height; y_el += tile_height_el) {
      for (uint32_t x_el = 0; x_el < extent_el.width; x_el += tile_width_el) {
         VkOffset3D offset = {
            .x = offset_el.x + x_el,
            .y = offset_el.y + y_el,
         };
         VkExtent3D extent = {
            .width  = MIN2(extent_el.width - x_el, tile_width_el),
            .height = MIN2(extent_el.height - y_el, tile_height_el),
            .depth  = 1,
         };

         VkOffset3D src_offset = {
            .x = x_el,
            .y = y_el,
         };

         const uint8_t *mem_ptr_offset =
            mem_ptr + (src_offset.x * mem_bpp) +
            (src_offset.y * mem_row_pitch_B);

         if (mem_to_img) {
            callback(mem_ptr_offset, mem_row_pitch_B,
                     tmp_mem, tile_width_B,
                     &extent, callback_data);

            anv_copy_image_memory(device, surf,
                                  binding,
                                  anv_surf->memory_range.offset,
                                  tmp_mem,
                                  tile_width_B,
                                  &offset, &extent,
                                  imageSubresource->mipLevel,
                                  imageSubresource->baseArrayLayer,
                                  imageOffset.z,
                                  a, z, mem_to_img);
         } else {
            anv_copy_image_memory(device, surf,
                                  binding,
                                  anv_surf->memory_range.offset,
                                  tmp_mem,
                                  tile_width_B,
                                  &offset, &extent,
                                  imageSubresource->mipLevel,
                                  imageSubresource->baseArrayLayer,
                                  imageOffset.z,
                                  a, z, mem_to_img);

            callback(tmp_mem, tile_width_B,
                     (uint8_t *)mem_ptr_offset, mem_row_pitch_B,
                     &extent, callback_data);
         }
      }
   }
}

VkResult
anv_CopyMemoryToImage(
    VkDevice                                 _device,
    const VkCopyMemoryToImageInfo*           pCopyMemoryToImageInfo)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, image, pCopyMemoryToImageInfo->dstImage);

   const bool use_memcpy =
      (pCopyMemoryToImageInfo->flags & VK_HOST_IMAGE_COPY_MEMCPY) != 0;
   const bool temp_copy = needs_temp_copy(image, pCopyMemoryToImageInfo->flags);
   const bool is_emulated = image->emu_plane_format != VK_FORMAT_UNDEFINED;

   void *tmp_mem = NULL;
   if (temp_copy || is_emulated) {
      tmp_mem = vk_alloc(&device->vk.alloc, TMP_BUFFER_SIZE, 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (tmp_mem == NULL)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   for (uint32_t r = 0; r < pCopyMemoryToImageInfo->regionCount; r++) {
      const VkMemoryToImageCopy *region =
         &pCopyMemoryToImageInfo->pRegions[r];
      const uint32_t plane =
         anv_image_aspect_to_plane(image, region->imageSubresource.aspectMask);
      const struct anv_surface *anv_surf =
         &image->planes[plane].primary_surface;
      const struct isl_surf *surf = &anv_surf->isl;
      const struct anv_image_binding *binding =
         &image->bindings[anv_surf->memory_range.binding];

      assert(binding->host_map != NULL);
      void *img_ptr = binding->host_map + binding->map_delta +
                      anv_surf->memory_range.offset;

      const struct anv_format *anv_format =
         anv_get_format(device->physical, image->vk.format);
      struct anv_format_plane anv_plane_format = anv_format->planes[plane];
      const struct util_format_description *desc = NULL;

      if (is_emulated) {
         desc = vk_format_description(image->vk.format);
         if (unlikely(desc->layout != UTIL_FORMAT_LAYOUT_ASTC))
            UNREACHABLE("Unsupported emulated format");
      }

      /* We can use the image format to figure out all the pitches if using
       * memcpy, otherwise memory & image might have different formats, so use
       * the API format of the image.
       */
      enum isl_format mem_format = use_memcpy ?
         surf->format : anv_plane_format.isl_format;

      /* Memory distance between each row */
      uint64_t mem_row_pitch_B =
         calc_mem_row_pitch_B(mem_format, region->memoryRowLength,
                              &region->imageExtent);
      /* Memory distance between each slice (1 3D level or 1 array layer) */
      uint64_t mem_height_pitch_B =
         calc_mem_height_pitch_B(mem_format, mem_row_pitch_B,
                                 region->memoryImageHeight,
                                 &region->imageExtent);

      VkOffset3D offset_el =
         vk_offset3d_to_el(surf->format, region->imageOffset);
      VkExtent3D extent_el =
         vk_extent3d_to_el(surf->format, region->imageExtent);

      uint32_t layer_count =
         vk_image_subresource_layer_count(&image->vk, &region->imageSubresource);
      for (uint32_t a = 0; a < layer_count; a++) {
         for (uint32_t z = 0; z < region->imageExtent.depth; z++) {
            assert((region->imageOffset.z == 0 && z == 0) ||
                   (region->imageSubresource.baseArrayLayer == 0 && a == 0));
            uint64_t mem_row_offset = (z + a) * mem_height_pitch_B;
            const void *mem_ptr = region->pHostPointer + mem_row_offset;
            uint64_t start_tile_B, end_tile_B;
            if (use_memcpy &&
                isl_surf_image_has_unique_tiles(
                   surf,
                   region->imageSubresource.mipLevel,
                   region->imageOffset.z + z +
                   region->imageSubresource.baseArrayLayer + a, 1,
                   &start_tile_B, &end_tile_B)) {
               memcpy(img_ptr + start_tile_B,
                      mem_ptr,
                      end_tile_B - start_tile_B);
            } else {
               if (is_emulated) {
                  anv_copy_image_memory(device, surf,
                                        binding, anv_surf->memory_range.offset,
                                        (void *)mem_ptr,
                                        mem_row_pitch_B,
                                        &offset_el, &extent_el,
                                        region->imageSubresource.mipLevel,
                                        region->imageSubresource.baseArrayLayer,
                                        region->imageOffset.z,
                                        a, z, true /* mem_to_img */);

                  copy_intermediate(
                     device, mem_ptr, mem_row_pitch_B, mem_format,
                     image, &image->planes[image->n_planes].primary_surface,
                     region, true, /* mem_to_img */
                     tmp_mem, a, z,
                     astc_decompression_callback, desc);

               } else if (temp_copy) {
                  mem_format =
                     anv_get_format_plane(device->physical, image->vk.format, plane,
                                          VK_IMAGE_TILING_LINEAR).isl_format;

                  const struct isl_format_layout *mem_fmt_layout =
                     isl_format_get_layout(mem_format);
                  const struct isl_format_layout *surf_fmt_layout =
                     isl_format_get_layout(surf->format);

                  struct rgb_conversion_params params = {
                     .src_bpp = mem_fmt_layout->bpb / 8,
                     .dst_bpp = surf_fmt_layout->bpb / 8,
                  };

                  copy_intermediate(
                     device, mem_ptr, mem_row_pitch_B, mem_format,
                     image, anv_surf,
                     region, true, /* mem_to_img */
                     tmp_mem, a, z,
                     rgb_rgba_conversion_callback, &params);
               } else {
                  anv_copy_image_memory(device, surf,
                                        binding, anv_surf->memory_range.offset,
                                        (void *)mem_ptr,
                                        mem_row_pitch_B,
                                        &offset_el,
                                        &extent_el,
                                        region->imageSubresource.mipLevel,
                                        region->imageSubresource.baseArrayLayer,
                                        region->imageOffset.z,
                                        a, z, true /* mem_to_img */);
               }
            }
         }
      }
   }

   vk_free(&device->vk.alloc, tmp_mem);

   return VK_SUCCESS;
}

VkResult
anv_CopyImageToMemory(
    VkDevice                                 _device,
    const VkCopyImageToMemoryInfo*           pCopyImageToMemoryInfo)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, image, pCopyImageToMemoryInfo->srcImage);

   const bool use_memcpy = (pCopyImageToMemoryInfo->flags &
                            VK_HOST_IMAGE_COPY_MEMCPY) != 0;
   const bool temp_copy = needs_temp_copy(image, pCopyImageToMemoryInfo->flags);
   void *tmp_mem = NULL;
   uint64_t tmp_mem_size = 0;

   for (uint32_t r = 0; r < pCopyImageToMemoryInfo->regionCount; r++) {
      const VkImageToMemoryCopy *region =
         &pCopyImageToMemoryInfo->pRegions[r];
      const uint32_t plane =
         anv_image_aspect_to_plane(image, region->imageSubresource.aspectMask);
      const struct anv_surface *anv_surf =
         &image->planes[plane].primary_surface;
      const struct isl_surf *surf = &anv_surf->isl;
      const struct anv_image_binding *binding =
         &image->bindings[anv_surf->memory_range.binding];

      assert(binding->host_map != NULL);
      const void *img_ptr = binding->host_map + binding->map_delta +
                            anv_surf->memory_range.offset;

      const struct anv_format *anv_format =
         anv_get_format(device->physical, image->vk.format);
      struct anv_format_plane anv_plane_format = anv_format->planes[plane];

      /* We can use the image format to figure out all the pitches if using
       * memcpy, otherwise memory & image might have different formats, so use
       * the API format of the image.
       */
      enum isl_format mem_format = use_memcpy ?
         surf->format : anv_plane_format.isl_format;

      uint64_t tmp_copy_row_pitch_B = 0;

      if (temp_copy) {
         mem_format =
            anv_get_format_plane(device->physical, image->vk.format, plane,
                                 VK_IMAGE_TILING_LINEAR).isl_format;

         tmp_copy_row_pitch_B =
            calc_mem_row_pitch_B(surf->format, 0, &region->imageExtent);
         uint64_t tmp_copy_height_pitch_B =
            calc_mem_height_pitch_B(surf->format, tmp_copy_row_pitch_B, 0,
                                    &region->imageExtent);

         uint64_t tmp_mem_needed_size = tmp_copy_row_pitch_B * tmp_copy_height_pitch_B;
         if (tmp_mem_needed_size > tmp_mem_size) {
            void *new_tmp_mem = vk_realloc(&device->vk.alloc, tmp_mem, tmp_mem_needed_size, 8,
                                 VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
            if (new_tmp_mem == NULL) {
               vk_free(&device->vk.alloc, tmp_mem);
               return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
            }
            tmp_mem = new_tmp_mem;
            tmp_mem_size = tmp_mem_needed_size;
         }
      }

      VkOffset3D offset_el =
         vk_offset3d_to_el(surf->format, region->imageOffset);
      VkExtent3D extent_el =
         vk_extent3d_to_el(surf->format, region->imageExtent);

      /* Memory distance between each row */
      uint64_t mem_row_pitch_B =
         calc_mem_row_pitch_B(mem_format, region->memoryRowLength,
                              &region->imageExtent);
      /* Memory distance between each slice (1 3D level or 1 array layer) */
      uint64_t mem_height_pitch_B =
         calc_mem_height_pitch_B(mem_format, mem_row_pitch_B,
                                 region->memoryImageHeight,
                                 &region->imageExtent);

      uint32_t layer_count =
         vk_image_subresource_layer_count(&image->vk, &region->imageSubresource);
      for (uint32_t a = 0; a < layer_count; a++) {
         for (uint32_t z = 0; z < region->imageExtent.depth; z++) {
            assert((region->imageOffset.z == 0 && z == 0) ||
                   (region->imageSubresource.baseArrayLayer == 0 && a == 0));
            uint64_t mem_row_offset = (z + a) * mem_height_pitch_B;
            void *mem_ptr = region->pHostPointer + mem_row_offset;
            uint64_t start_tile_B, end_tile_B;
            if ((pCopyImageToMemoryInfo->flags &
                 VK_HOST_IMAGE_COPY_MEMCPY) &&
                isl_surf_image_has_unique_tiles(surf,
                   region->imageSubresource.mipLevel,
                   region->imageOffset.z + z +
                   region->imageSubresource.baseArrayLayer + a, 1,
                   &start_tile_B, &end_tile_B)) {
               memcpy(mem_ptr,
                      img_ptr + start_tile_B,
                      end_tile_B - start_tile_B);
            } else {
               if (temp_copy) {
                  /* RGBA->RGB conversion with callback */
                  const struct isl_format_layout *surf_fmt_layout =
                     isl_format_get_layout(surf->format);
                  const struct isl_format_layout *mem_fmt_layout =
                     isl_format_get_layout(mem_format);

                  struct rgb_conversion_params params = {
                     .src_bpp = surf_fmt_layout->bpb / 8,
                     .dst_bpp = mem_fmt_layout->bpb / 8,
                  };

                  copy_intermediate(
                     device, mem_ptr, mem_row_pitch_B, mem_format,
                     image, &image->planes[plane].primary_surface,
                     region, false, /* mem_to_img */
                     tmp_mem, a, z,
                     rgb_rgba_conversion_callback, &params);
               } else {
                  anv_copy_image_memory(device, surf,
                                        binding, anv_surf->memory_range.offset,
                                        mem_ptr,
                                        mem_row_pitch_B,
                                        &offset_el,
                                        &extent_el,
                                        region->imageSubresource.mipLevel,
                                        region->imageSubresource.baseArrayLayer,
                                        region->imageOffset.z,
                                        a, z, false /* mem_to_img */);
               }
            }
         }
      }
   }

   vk_free(&device->vk.alloc, tmp_mem);

   return VK_SUCCESS;
}

/* This functions copies from one image to another through an intermediate
 * linear buffer.
 */
static void
copy_image_to_image(struct anv_device *device,
                    struct anv_image *src_image,
                    struct anv_image *dst_image,
                    int src_plane, int dst_plane,
                    const VkImageCopy2 *region,
                    void *tmp_map,
                    void *emu_tmp_map)
{
   const struct anv_surface *src_anv_surf =
      &src_image->planes[src_plane].primary_surface;
   const struct anv_surface *dst_anv_surf =
      &dst_image->planes[dst_plane].primary_surface;
   const struct isl_surf *src_surf = &src_anv_surf->isl;
   const struct isl_surf *dst_surf = &dst_anv_surf->isl;
   const struct anv_image_binding *src_binding =
      &src_image->bindings[src_anv_surf->memory_range.binding];
   const struct anv_image_binding *dst_binding =
      &dst_image->bindings[dst_anv_surf->memory_range.binding];

   struct isl_tile_info src_tile;
   struct isl_tile_info dst_tile;

   isl_surf_get_tile_info(src_surf, &src_tile);
   isl_surf_get_tile_info(dst_surf, &dst_tile);

   uint32_t tile_width_el, tile_height_el;
   if (src_tile.phys_extent_B.w > dst_tile.phys_extent_B.w) {
      tile_width_el  = src_tile.logical_extent_el.w;
      tile_height_el = src_tile.logical_extent_el.h;
   } else {
      tile_width_el  = dst_tile.logical_extent_el.w;
      tile_height_el = dst_tile.logical_extent_el.h;
   }

   /* Only decompress if we're writing to the emulated (decompressed) plane */
   const bool is_emulated = (dst_image->emu_plane_format != VK_FORMAT_UNDEFINED) &&
                            (&dst_image->planes[dst_plane] == &dst_image->planes[dst_image->n_planes]);

   /* There is no requirement that the extent be aligned to the texel block
    * size.
    */
   VkOffset3D src_offset_el =
      vk_offset3d_to_el(src_surf->format, region->srcOffset);
   VkOffset3D dst_offset_el =
      vk_offset3d_to_el(src_surf->format, region->dstOffset);
   VkExtent3D extent_el =
      vk_extent3d_to_el(src_surf->format, region->extent);

   uint32_t linear_stride_B;
   /* linear-to-linear case */
   if (tile_width_el == 1 && tile_height_el == 1) {
      tile_width_el = MIN2(4096 / (src_tile.format_bpb / 8),
                           extent_el.width);
      tile_height_el = 4096 / (tile_width_el * (src_tile.format_bpb / 8));
      linear_stride_B = tile_width_el * src_tile.format_bpb / 8;
   } else {
      linear_stride_B = src_tile.logical_extent_el.w * src_tile.format_bpb / 8;
   }


   uint32_t layer_count =
      vk_image_subresource_layer_count(&src_image->vk, &region->srcSubresource);
   for (uint32_t a = 0; a < layer_count; a++) {
      for (uint32_t z = 0; z < region->extent.depth; z++) {
         for (uint32_t y_el = 0; y_el < extent_el.height; y_el += tile_height_el) {
            for (uint32_t x_el = 0; x_el < extent_el.width; x_el += tile_width_el) {
               VkOffset3D src_offset = {
                  .x = src_offset_el.x + x_el,
                  .y = src_offset_el.y + y_el,
               };
               VkOffset3D dst_offset = {
                  .x = dst_offset_el.x + x_el,
                  .y = dst_offset_el.y + y_el,
               };
               VkExtent3D extent = {
                  .width  = MIN2(extent_el.width - x_el, tile_width_el),
                  .height = MIN2(extent_el.height - y_el, tile_height_el),
                  .depth  = 1,
               };

               anv_copy_image_memory(device, src_surf,
                                     src_binding,
                                     src_anv_surf->memory_range.offset,
                                     tmp_map,
                                     linear_stride_B,
                                     &src_offset, &extent,
                                     region->srcSubresource.mipLevel,
                                     region->srcSubresource.baseArrayLayer,
                                     region->srcOffset.z,
                                     a, z,
                                     false /* mem_to_img */);

               if (is_emulated) {
                  const struct VkMemoryToImageCopy mem_copy = {
                     .sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY,
                     .pNext = NULL,
                     .pHostPointer = tmp_map,
                     .memoryRowLength = linear_stride_B,
                     .memoryImageHeight = 0,
                     .imageSubresource = region->dstSubresource,
                     .imageOffset = vk_el_to_offset3d(src_surf->format,
                                                      dst_offset),
                     .imageExtent = vk_el_to_extent3d(src_surf->format,
                                                      extent),
                  };
                  VkFormat format = dst_image->vk.format;

                  copy_intermediate(
                     device,
                     tmp_map, linear_stride_B,
                     dst_image->planes[dst_plane].primary_surface.isl.format,
                     dst_image, &dst_image->planes[dst_plane].primary_surface,
                     &mem_copy, true, /* mem_to_img */
                     emu_tmp_map, a, z,
                     astc_decompression_callback, &format);
               } else {
                  anv_copy_image_memory(device, dst_surf,
                                        dst_binding,
                                        dst_anv_surf->memory_range.offset,
                                        tmp_map,
                                        linear_stride_B,
                                        &dst_offset, &extent,
                                        region->dstSubresource.mipLevel,
                                        region->dstSubresource.baseArrayLayer,
                                        region->dstOffset.z,
                                        a, z,
                                        true /* mem_to_img */);
               }
            }
         }
      }
   }
}

VkResult
anv_CopyImageToImage(
    VkDevice                                 _device,
    const VkCopyImageToImageInfo*            pCopyImageToImageInfo)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, src_image, pCopyImageToImageInfo->srcImage);
   ANV_FROM_HANDLE(anv_image, dst_image, pCopyImageToImageInfo->dstImage);

   /* Work with a tile's worth of data */
   void *tmp_map = vk_alloc(&device->vk.alloc, 2 * TMP_BUFFER_SIZE, 8,
                            VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (tmp_map == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   void *emu_tmp_map = tmp_map + TMP_BUFFER_SIZE;

   for (uint32_t r = 0; r < pCopyImageToImageInfo->regionCount; r++) {
      const VkImageCopy2 *region = &pCopyImageToImageInfo->pRegions[r];

      VkImageAspectFlags src_mask = region->srcSubresource.aspectMask,
                         dst_mask = region->dstSubresource.aspectMask;

      assert(anv_image_aspects_compatible(src_mask, dst_mask));

      if (util_bitcount(src_mask) > 1) {
         anv_foreach_image_aspect_bit(aspect_bit, src_image, src_mask) {
            int plane = anv_image_aspect_to_plane(src_image,
                                                  1UL << aspect_bit);
            copy_image_to_image(device, src_image, dst_image,
                                plane, plane, region, tmp_map, emu_tmp_map);
         }
      } else {
         int src_plane = anv_image_aspect_to_plane(src_image, src_mask);
         int dst_plane = anv_image_aspect_to_plane(dst_image, dst_mask);
         copy_image_to_image(device, src_image, dst_image,
                             src_plane, dst_plane, region, tmp_map, emu_tmp_map);
      }
   }

   vk_free(&device->vk.alloc, tmp_map);

   return VK_SUCCESS;
}

VkResult
anv_TransitionImageLayout(
    VkDevice                                 device,
    uint32_t                                 transitionCount,
    const VkHostImageLayoutTransitionInfo*   pTransitions)
{
   /* Our layout transitions are mostly about resolving the auxiliary surface
    * into the main surface. Since we disable the auxiliary surface, there is
    * nothing here for us to do.
    */
   return VK_SUCCESS;
}
