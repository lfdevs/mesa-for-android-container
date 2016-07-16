/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ***************************************************************************/

#include "pipe/p_screen.h"
#include "pipe/p_defines.h"
#include "util/u_memory.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_cpu_detect.h"
#include "util/u_format_s3tc.h"

#include "state_tracker/sw_winsys.h"

extern "C" {
#include "gallivm/lp_bld_limits.h"
}

#include "swr_public.h"
#include "swr_screen.h"
#include "swr_context.h"
#include "swr_resource.h"
#include "swr_fence.h"
#include "gen_knobs.h"

#include "jit_api.h"

#include <stdio.h>

/* MSVC case instensitive compare */
#if defined(PIPE_CC_MSVC)
   #define strcasecmp lstrcmpiA
#endif

/*
 * Max texture sizes
 * XXX Check max texture size values against core and sampler.
 */
#define SWR_MAX_TEXTURE_SIZE (4 * 1048 * 1048 * 1024ULL) /* 4GB */
#define SWR_MAX_TEXTURE_2D_LEVELS 14  /* 8K x 8K for now */
#define SWR_MAX_TEXTURE_3D_LEVELS 12  /* 2K x 2K x 2K for now */
#define SWR_MAX_TEXTURE_CUBE_LEVELS 14  /* 8K x 8K for now */
#define SWR_MAX_TEXTURE_ARRAY_LAYERS 512 /* 8K x 512 / 8K x 8K x 512 */

static const char *
swr_get_name(struct pipe_screen *screen)
{
   return "SWR";
}

static const char *
swr_get_vendor(struct pipe_screen *screen)
{
   return "Intel Corporation";
}

static boolean
swr_is_format_supported(struct pipe_screen *screen,
                        enum pipe_format format,
                        enum pipe_texture_target target,
                        unsigned sample_count,
                        unsigned bind)
{
   struct sw_winsys *winsys = swr_screen(screen)->winsys;
   const struct util_format_description *format_desc;

   assert(target == PIPE_BUFFER || target == PIPE_TEXTURE_1D
          || target == PIPE_TEXTURE_1D_ARRAY
          || target == PIPE_TEXTURE_2D
          || target == PIPE_TEXTURE_2D_ARRAY
          || target == PIPE_TEXTURE_RECT
          || target == PIPE_TEXTURE_3D
          || target == PIPE_TEXTURE_CUBE
          || target == PIPE_TEXTURE_CUBE_ARRAY);

   format_desc = util_format_description(format);
   if (!format_desc)
      return FALSE;

   if (sample_count > 1)
      return FALSE;

   if (bind
       & (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT | PIPE_BIND_SHARED)) {
      if (!winsys->is_displaytarget_format_supported(winsys, bind, format))
         return FALSE;
   }

   if (bind & PIPE_BIND_RENDER_TARGET) {
      if (format_desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS)
         return FALSE;

      if (mesa_to_swr_format(format) == (SWR_FORMAT)-1)
         return FALSE;

      /*
       * Although possible, it is unnatural to render into compressed or YUV
       * surfaces. So disable these here to avoid going into weird paths
       * inside the state trackers.
       */
      if (format_desc->block.width != 1 || format_desc->block.height != 1)
         return FALSE;
   }

   if (bind & PIPE_BIND_DEPTH_STENCIL) {
      if (format_desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
         return FALSE;

      if (mesa_to_swr_format(format) == (SWR_FORMAT)-1)
         return FALSE;
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_BPTC ||
       format_desc->layout == UTIL_FORMAT_LAYOUT_ASTC) {
      return FALSE;
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_ETC &&
       format != PIPE_FORMAT_ETC1_RGB8) {
      return FALSE;
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_S3TC) {
      return util_format_s3tc_enabled;
   }

   return TRUE;
}

static int
swr_get_param(struct pipe_screen *screen, enum pipe_cap param)
{
   switch (param) {
   case PIPE_CAP_NPOT_TEXTURES:
   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
      return 1;
   case PIPE_CAP_TWO_SIDED_STENCIL:
      return 1;
   case PIPE_CAP_SM3:
      return 1;
   case PIPE_CAP_ANISOTROPIC_FILTER:
      return 0;
   case PIPE_CAP_POINT_SPRITE:
      return 1;
   case PIPE_CAP_MAX_RENDER_TARGETS:
      return PIPE_MAX_COLOR_BUFS;
   case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
      return 1;
   case PIPE_CAP_OCCLUSION_QUERY:
   case PIPE_CAP_QUERY_TIME_ELAPSED:
   case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
      return 1;
   case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
      return 1;
   case PIPE_CAP_TEXTURE_SHADOW_MAP:
      return 1;
   case PIPE_CAP_TEXTURE_SWIZZLE:
      return 1;
   case PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK:
      return 0;
   case PIPE_CAP_MAX_TEXTURE_2D_LEVELS:
      return SWR_MAX_TEXTURE_2D_LEVELS;
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      return SWR_MAX_TEXTURE_3D_LEVELS;
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      return SWR_MAX_TEXTURE_CUBE_LEVELS;
   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
      return 1;
   case PIPE_CAP_INDEP_BLEND_ENABLE:
      return 1;
   case PIPE_CAP_INDEP_BLEND_FUNC:
      return 1;
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
      return 0; // Don't support lower left frag coord.
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
      return 1;
   case PIPE_CAP_DEPTH_CLIP_DISABLE:
      return 1;
   case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
      return MAX_SO_STREAMS;
   case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
   case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
      return MAX_ATTRIBUTES;
   case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
   case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
      return 1024;
   case PIPE_CAP_MAX_VERTEX_STREAMS:
      return 1;
   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      return 2048;
   case PIPE_CAP_PRIMITIVE_RESTART:
      return 1;
   case PIPE_CAP_SHADER_STENCIL_EXPORT:
      return 1;
   case PIPE_CAP_TGSI_INSTANCEID:
   case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
   case PIPE_CAP_START_INSTANCE:
      return 1;
   case PIPE_CAP_SEAMLESS_CUBE_MAP:
   case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
      return 1;
   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
      return SWR_MAX_TEXTURE_ARRAY_LAYERS;
   case PIPE_CAP_MIN_TEXEL_OFFSET:
      return -8;
   case PIPE_CAP_MAX_TEXEL_OFFSET:
      return 7;
   case PIPE_CAP_CONDITIONAL_RENDER:
      return 1;
   case PIPE_CAP_TEXTURE_BARRIER:
      return 0;
   case PIPE_CAP_FRAGMENT_COLOR_CLAMPED:
   case PIPE_CAP_VERTEX_COLOR_UNCLAMPED: /* draw module */
   case PIPE_CAP_VERTEX_COLOR_CLAMPED: /* draw module */
      return 1;
   case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
      return 1;
   case PIPE_CAP_GLSL_FEATURE_LEVEL:
      return 330;
   case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
      return 0;
   case PIPE_CAP_COMPUTE:
      return 0;
   case PIPE_CAP_USER_VERTEX_BUFFERS:
   case PIPE_CAP_USER_INDEX_BUFFERS:
   case PIPE_CAP_USER_CONSTANT_BUFFERS:
   case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
   case PIPE_CAP_TGSI_VS_LAYER_VIEWPORT:
      return 1;
   case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
      return 16;
   case PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS:
   case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_TEXTURE_MULTISAMPLE:
      return 0;
   case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
      return 64;
   case PIPE_CAP_QUERY_TIMESTAMP:
      return 1;
   case PIPE_CAP_CUBE_MAP_ARRAY:
      return 0;
   case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
      return 1;
   case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
      return 65536;
   case PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
      return 0;
   case PIPE_CAP_TGSI_TEXCOORD:
   case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
      return 0;
   case PIPE_CAP_MAX_VIEWPORTS:
      return 1;
   case PIPE_CAP_ENDIANNESS:
      return PIPE_ENDIAN_NATIVE;
   case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
   case PIPE_CAP_TEXTURE_GATHER_SM5:
      return 0;
   case PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT:
      return 1;
   case PIPE_CAP_TEXTURE_QUERY_LOD:
   case PIPE_CAP_SAMPLE_SHADING:
   case PIPE_CAP_TEXTURE_GATHER_OFFSETS:
   case PIPE_CAP_TGSI_VS_WINDOW_SPACE_POSITION:
   case PIPE_CAP_TGSI_FS_FINE_DERIVATIVE:
   case PIPE_CAP_SAMPLER_VIEW_TARGET:
      return 0;
   case PIPE_CAP_FAKE_SW_MSAA:
      return 1;
   case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
   case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
      return 0;
   case PIPE_CAP_DRAW_INDIRECT:
      return 1;

   case PIPE_CAP_VENDOR_ID:
      return 0xFFFFFFFF;
   case PIPE_CAP_DEVICE_ID:
      return 0xFFFFFFFF;
   case PIPE_CAP_ACCELERATED:
      return 0;
   case PIPE_CAP_VIDEO_MEMORY: {
      /* XXX: Do we want to return the full amount of system memory ? */
      uint64_t system_memory;

      if (!os_get_total_physical_memory(&system_memory))
         return 0;

      return (int)(system_memory >> 20);
   }
   case PIPE_CAP_UMA:
      return 1;
   case PIPE_CAP_CONDITIONAL_RENDER_INVERTED:
      return 1;
   case PIPE_CAP_CLIP_HALFZ:
      return 1;
   case PIPE_CAP_VERTEXID_NOBASE:
      return 0;
   case PIPE_CAP_POLYGON_OFFSET_CLAMP:
      return 1;
   case PIPE_CAP_MULTISAMPLE_Z_RESOLVE:
      return 0;
   case PIPE_CAP_RESOURCE_FROM_USER_MEMORY:
      return 0; // xxx
   case PIPE_CAP_DEVICE_RESET_STATUS_QUERY:
      return 0;
   case PIPE_CAP_MAX_SHADER_PATCH_VARYINGS:
      return 0;
   case PIPE_CAP_DEPTH_BOUNDS_TEST:
      return 0; // xxx
   case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
   case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
      return 1;
   case PIPE_CAP_TGSI_TXQS:
   case PIPE_CAP_FORCE_PERSAMPLE_INTERP:
   case PIPE_CAP_SHAREABLE_SHADERS:
   case PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS:
   case PIPE_CAP_CLEAR_TEXTURE:
   case PIPE_CAP_DRAW_PARAMETERS:
   case PIPE_CAP_TGSI_PACK_HALF_FLOAT:
   case PIPE_CAP_MULTI_DRAW_INDIRECT:
   case PIPE_CAP_MULTI_DRAW_INDIRECT_PARAMS:
   case PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL:
   case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL:
   case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
   case PIPE_CAP_INVALIDATE_BUFFER:
   case PIPE_CAP_GENERATE_MIPMAP:
   case PIPE_CAP_STRING_MARKER:
   case PIPE_CAP_BUFFER_SAMPLER_VIEW_RGBA_ONLY:
   case PIPE_CAP_SURFACE_REINTERPRET_BLOCKS:
   case PIPE_CAP_QUERY_BUFFER_OBJECT:
   case PIPE_CAP_QUERY_MEMORY_INFO:
   case PIPE_CAP_ROBUST_BUFFER_ACCESS_BEHAVIOR:
   case PIPE_CAP_PCI_GROUP:
   case PIPE_CAP_PCI_BUS:
   case PIPE_CAP_PCI_DEVICE:
   case PIPE_CAP_PCI_FUNCTION:
   case PIPE_CAP_FRAMEBUFFER_NO_ATTACHMENT:
   case PIPE_CAP_CULL_DISTANCE:
   case PIPE_CAP_PRIMITIVE_RESTART_FOR_PATCHES:
      return 0;
   }

   /* should only get here on unhandled cases */
   debug_printf("Unexpected PIPE_CAP %d query\n", param);
   return 0;
}

static int
swr_get_shader_param(struct pipe_screen *screen,
                     unsigned shader,
                     enum pipe_shader_cap param)
{
   if (shader == PIPE_SHADER_VERTEX || shader == PIPE_SHADER_FRAGMENT)
      return gallivm_get_shader_param(param);

   // Todo: geometry, tesselation, compute
   return 0;
}


static float
swr_get_paramf(struct pipe_screen *screen, enum pipe_capf param)
{
   switch (param) {
   case PIPE_CAPF_MAX_LINE_WIDTH:
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
   case PIPE_CAPF_MAX_POINT_WIDTH:
      return 255.0; /* arbitrary */
   case PIPE_CAPF_MAX_POINT_WIDTH_AA:
      return 0.0;
   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return 0.0;
   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return 0.0;
   case PIPE_CAPF_GUARD_BAND_LEFT:
   case PIPE_CAPF_GUARD_BAND_TOP:
   case PIPE_CAPF_GUARD_BAND_RIGHT:
   case PIPE_CAPF_GUARD_BAND_BOTTOM:
      return 0.0;
   }
   /* should only get here on unhandled cases */
   debug_printf("Unexpected PIPE_CAPF %d query\n", param);
   return 0.0;
}

SWR_FORMAT
mesa_to_swr_format(enum pipe_format format)
{
   const struct util_format_description *format_desc =
      util_format_description(format);
   if (!format_desc)
      return (SWR_FORMAT)-1;

   // more robust check would be comparing all attributes of the formats
   // luckily format names are mostly standardized
   for (int i = 0; i < NUM_SWR_FORMATS; i++) {
      const SWR_FORMAT_INFO &swr_desc = GetFormatInfo((SWR_FORMAT)i);

      if (!strcasecmp(format_desc->short_name, swr_desc.name))
         return (SWR_FORMAT)i;
   }

   // ... with some exceptions
   switch (format) {
   case PIPE_FORMAT_R8G8B8A8_SRGB:
      return R8G8B8A8_UNORM_SRGB;
   case PIPE_FORMAT_B8G8R8A8_SRGB:
      return B8G8R8A8_UNORM_SRGB;
   case PIPE_FORMAT_I8_UNORM:
      return R8_UNORM;
   case PIPE_FORMAT_Z16_UNORM:
      return R16_UNORM;
   case PIPE_FORMAT_Z24X8_UNORM:
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      return R24_UNORM_X8_TYPELESS;
   case PIPE_FORMAT_Z32_FLOAT:
      return R32_FLOAT;
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      return R32_FLOAT_X8X24_TYPELESS;
   case PIPE_FORMAT_L8A8_UNORM:
      return R8G8_UNORM;
   default:
      break;
   }

   debug_printf("asked to convert unsupported format %s\n",
                format_desc->name);
   return (SWR_FORMAT)-1;
}

static boolean
swr_displaytarget_layout(struct swr_screen *screen, struct swr_resource *res)
{
   struct sw_winsys *winsys = screen->winsys;
   struct sw_displaytarget *dt;

   UINT stride;
   dt = winsys->displaytarget_create(winsys,
                                     res->base.bind,
                                     res->base.format,
                                     res->alignedWidth,
                                     res->alignedHeight,
                                     64, NULL,
                                     &stride);

   if (dt == NULL)
      return FALSE;

   void *map = winsys->displaytarget_map(winsys, dt, 0);

   res->display_target = dt;
   res->swr.pBaseAddress = (uint8_t*) map;

   /* Clear the display target surface */
   if (map)
      memset(map, 0, res->alignedHeight * stride);

   winsys->displaytarget_unmap(winsys, dt);

   return TRUE;
}

static boolean
swr_texture_layout(struct swr_screen *screen,
                   struct swr_resource *res,
                   boolean allocate)
{
   struct pipe_resource *pt = &res->base;

   pipe_format fmt = pt->format;
   const struct util_format_description *desc = util_format_description(fmt);

   res->has_depth = util_format_has_depth(desc);
   res->has_stencil = util_format_has_stencil(desc);

   if (res->has_stencil && !res->has_depth)
      fmt = PIPE_FORMAT_R8_UINT;

   res->swr.width = pt->width0;
   res->swr.height = pt->height0;
   res->swr.depth = pt->depth0;
   res->swr.type = swr_convert_target_type(pt->target);
   res->swr.tileMode = SWR_TILE_NONE;
   res->swr.format = mesa_to_swr_format(fmt);
   res->swr.numSamples = (1 << pt->nr_samples);

   SWR_FORMAT_INFO finfo = GetFormatInfo(res->swr.format);

   unsigned total_size = 0;
   unsigned width = pt->width0;
   unsigned height = pt->height0;
   unsigned depth = pt->depth0;
   unsigned layers = pt->array_size;

   for (int level = 0; level <= pt->last_level; level++) {
      unsigned alignedWidth, alignedHeight;
      unsigned num_slices;

      if (pt->bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DEPTH_STENCIL)) {
         alignedWidth = align(width, KNOB_MACROTILE_X_DIM);
         alignedHeight = align(height, KNOB_MACROTILE_Y_DIM);
      } else {
         alignedWidth = width;
         alignedHeight = height;
      }

      if (level == 0) {
         res->alignedWidth = alignedWidth;
         res->alignedHeight = alignedHeight;
      }

      res->row_stride[level] = alignedWidth * finfo.Bpp;
      res->img_stride[level] = res->row_stride[level] * alignedHeight;
      res->mip_offsets[level] = total_size;

      if (pt->target == PIPE_TEXTURE_3D)
         num_slices = depth;
      else if (pt->target == PIPE_TEXTURE_1D_ARRAY
               || pt->target == PIPE_TEXTURE_2D_ARRAY
               || pt->target == PIPE_TEXTURE_CUBE
               || pt->target == PIPE_TEXTURE_CUBE_ARRAY)
         num_slices = layers;
      else
         num_slices = 1;

      total_size += res->img_stride[level] * num_slices;
      if (total_size > SWR_MAX_TEXTURE_SIZE)
         return FALSE;

      width = u_minify(width, 1);
      height = u_minify(height, 1);
      depth = u_minify(depth, 1);
   }

   res->swr.halign = res->alignedWidth;
   res->swr.valign = res->alignedHeight;
   res->swr.pitch = res->row_stride[0];

   if (allocate) {
      res->swr.pBaseAddress = (uint8_t *)AlignedMalloc(total_size, 64);

      if (res->has_depth && res->has_stencil) {
         SWR_FORMAT_INFO finfo = GetFormatInfo(res->secondary.format);
         res->secondary.width = pt->width0;
         res->secondary.height = pt->height0;
         res->secondary.depth = pt->depth0;
         res->secondary.type = SURFACE_2D;
         res->secondary.tileMode = SWR_TILE_NONE;
         res->secondary.format = R8_UINT;
         res->secondary.numSamples = (1 << pt->nr_samples);
         res->secondary.pitch = res->alignedWidth * finfo.Bpp;

         res->secondary.pBaseAddress = (uint8_t *)AlignedMalloc(
            res->alignedHeight * res->secondary.pitch, 64);
      }
   }

   return TRUE;
}

static boolean
swr_can_create_resource(struct pipe_screen *screen,
                        const struct pipe_resource *templat)
{
   struct swr_resource res;
   memset(&res, 0, sizeof(res));
   res.base = *templat;
   return swr_texture_layout(swr_screen(screen), &res, false);
}

static struct pipe_resource *
swr_resource_create(struct pipe_screen *_screen,
                    const struct pipe_resource *templat)
{
   struct swr_screen *screen = swr_screen(_screen);
   struct swr_resource *res = CALLOC_STRUCT(swr_resource);
   if (!res)
      return NULL;

   res->base = *templat;
   pipe_reference_init(&res->base.reference, 1);
   res->base.screen = &screen->base;

   if (swr_resource_is_texture(&res->base)) {
      if (res->base.bind & (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT
                            | PIPE_BIND_SHARED)) {
         /* displayable surface
          * first call swr_texture_layout without allocating to finish
          * filling out the SWR_SURFAE_STATE in res */
         swr_texture_layout(screen, res, false);
         if (!swr_displaytarget_layout(screen, res))
            goto fail;
      } else {
         /* texture map */
         if (!swr_texture_layout(screen, res, true))
            goto fail;
      }
   } else {
      /* other data (vertex buffer, const buffer, etc) */
      assert(util_format_get_blocksize(templat->format) == 1);
      assert(templat->height0 == 1);
      assert(templat->depth0 == 1);
      assert(templat->last_level == 0);

      /* Easiest to just call swr_texture_layout, as it sets up
       * SWR_SURFAE_STATE in res */
      if (!swr_texture_layout(screen, res, true))
         goto fail;
   }

   return &res->base;

fail:
   FREE(res);
   return NULL;
}

static void
swr_resource_destroy(struct pipe_screen *p_screen, struct pipe_resource *pt)
{
   struct swr_screen *screen = swr_screen(p_screen);
   struct swr_resource *spr = swr_resource(pt);
   struct pipe_context *pipe = screen->pipe;

   /* Only wait on fence if the resource is being used */
   if (pipe && spr->status) {
      /* But, if there's no fence pending, submit one.
       * XXX: Remove once draw timestamps are implmented. */
      if (!swr_is_fence_pending(screen->flush_fence))
         swr_fence_submit(swr_context(pipe), screen->flush_fence);

      swr_fence_finish(p_screen, screen->flush_fence, 0);
      swr_resource_unused(pt);
   }

   /*
    * Free resource primary surface.  If resource is display target, winsys
    * manages the buffer and will free it on displaytarget_destroy.
    */
   if (spr->display_target) {
      /* display target */
      struct sw_winsys *winsys = screen->winsys;
      winsys->displaytarget_destroy(winsys, spr->display_target);
   } else
      AlignedFree(spr->swr.pBaseAddress);

   AlignedFree(spr->secondary.pBaseAddress);

   FREE(spr);
}


static void
swr_flush_frontbuffer(struct pipe_screen *p_screen,
                      struct pipe_resource *resource,
                      unsigned level,
                      unsigned layer,
                      void *context_private,
                      struct pipe_box *sub_box)
{
   struct swr_screen *screen = swr_screen(p_screen);
   struct sw_winsys *winsys = screen->winsys;
   struct swr_resource *spr = swr_resource(resource);
   struct pipe_context *pipe = screen->pipe;

   if (pipe) {
      swr_fence_finish(p_screen, screen->flush_fence, 0);
      swr_resource_unused(resource);
      SwrEndFrame(swr_context(pipe)->swrContext);
   }

   debug_assert(spr->display_target);
   if (spr->display_target)
      winsys->displaytarget_display(
         winsys, spr->display_target, context_private, sub_box);
}


static void
swr_destroy_screen(struct pipe_screen *p_screen)
{
   struct swr_screen *screen = swr_screen(p_screen);
   struct sw_winsys *winsys = screen->winsys;

   fprintf(stderr, "SWR destroy screen!\n");

   swr_fence_finish(p_screen, screen->flush_fence, 0);
   swr_fence_reference(p_screen, &screen->flush_fence, NULL);

   JitDestroyContext(screen->hJitMgr);

   if (winsys->destroy)
      winsys->destroy(winsys);

   FREE(screen);
}

PUBLIC
struct pipe_screen *
swr_create_screen(struct sw_winsys *winsys)
{
   struct swr_screen *screen = CALLOC_STRUCT(swr_screen);

   if (!screen)
      return NULL;

   if (!getenv("KNOB_MAX_PRIMS_PER_DRAW")) {
      g_GlobalKnobs.MAX_PRIMS_PER_DRAW.Value(49152);
   }

   screen->winsys = winsys;
   screen->base.get_name = swr_get_name;
   screen->base.get_vendor = swr_get_vendor;
   screen->base.is_format_supported = swr_is_format_supported;
   screen->base.context_create = swr_create_context;
   screen->base.can_create_resource = swr_can_create_resource;

   screen->base.destroy = swr_destroy_screen;
   screen->base.get_param = swr_get_param;
   screen->base.get_shader_param = swr_get_shader_param;
   screen->base.get_paramf = swr_get_paramf;

   screen->base.resource_create = swr_resource_create;
   screen->base.resource_destroy = swr_resource_destroy;

   screen->base.flush_frontbuffer = swr_flush_frontbuffer;

   screen->hJitMgr = JitCreateContext(KNOB_SIMD_WIDTH, KNOB_ARCH_STR);

   swr_fence_init(&screen->base);

   util_format_s3tc_init();

   return &screen->base;
}

struct sw_winsys *
swr_get_winsys(struct pipe_screen *pipe)
{
   return ((struct swr_screen *)pipe)->winsys;
}

struct sw_displaytarget *
swr_get_displaytarget(struct pipe_resource *resource)
{
   return ((struct swr_resource *)resource)->display_target;
}
