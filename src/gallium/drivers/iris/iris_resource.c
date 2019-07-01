/*
 * Copyright © 2017 Intel Corporation
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file iris_resource.c
 *
 * Resources are images, buffers, and other objects used by the GPU.
 *
 * XXX: explain resources
 */

#include <stdio.h>
#include <errno.h>
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/os_memory.h"
#include "util/u_cpu_detect.h"
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "util/u_threaded_context.h"
#include "util/u_transfer.h"
#include "util/u_transfer_helper.h"
#include "util/u_upload_mgr.h"
#include "util/ralloc.h"
#include "iris_batch.h"
#include "iris_context.h"
#include "iris_resource.h"
#include "iris_screen.h"
#include "intel/dev/gen_debug.h"
#include "isl/isl.h"
#include "drm-uapi/drm_fourcc.h"
#include "drm-uapi/i915_drm.h"

enum modifier_priority {
   MODIFIER_PRIORITY_INVALID = 0,
   MODIFIER_PRIORITY_LINEAR,
   MODIFIER_PRIORITY_X,
   MODIFIER_PRIORITY_Y,
   MODIFIER_PRIORITY_Y_CCS,
};

static const uint64_t priority_to_modifier[] = {
   [MODIFIER_PRIORITY_INVALID] = DRM_FORMAT_MOD_INVALID,
   [MODIFIER_PRIORITY_LINEAR] = DRM_FORMAT_MOD_LINEAR,
   [MODIFIER_PRIORITY_X] = I915_FORMAT_MOD_X_TILED,
   [MODIFIER_PRIORITY_Y] = I915_FORMAT_MOD_Y_TILED,
   [MODIFIER_PRIORITY_Y_CCS] = I915_FORMAT_MOD_Y_TILED_CCS,
};

static bool
modifier_is_supported(const struct gen_device_info *devinfo,
                      uint64_t modifier)
{
   /* XXX: do something real */
   switch (modifier) {
   case I915_FORMAT_MOD_Y_TILED:
   case I915_FORMAT_MOD_X_TILED:
   case DRM_FORMAT_MOD_LINEAR:
      return true;
   case I915_FORMAT_MOD_Y_TILED_CCS:
   case DRM_FORMAT_MOD_INVALID:
   default:
      return false;
   }
}

static uint64_t
select_best_modifier(struct gen_device_info *devinfo,
                     const uint64_t *modifiers,
                     int count)
{
   enum modifier_priority prio = MODIFIER_PRIORITY_INVALID;

   for (int i = 0; i < count; i++) {
      if (!modifier_is_supported(devinfo, modifiers[i]))
         continue;

      switch (modifiers[i]) {
      case I915_FORMAT_MOD_Y_TILED_CCS:
         prio = MAX2(prio, MODIFIER_PRIORITY_Y_CCS);
         break;
      case I915_FORMAT_MOD_Y_TILED:
         prio = MAX2(prio, MODIFIER_PRIORITY_Y);
         break;
      case I915_FORMAT_MOD_X_TILED:
         prio = MAX2(prio, MODIFIER_PRIORITY_X);
         break;
      case DRM_FORMAT_MOD_LINEAR:
         prio = MAX2(prio, MODIFIER_PRIORITY_LINEAR);
         break;
      case DRM_FORMAT_MOD_INVALID:
      default:
         break;
      }
   }

   return priority_to_modifier[prio];
}

static enum isl_surf_dim
target_to_isl_surf_dim(enum pipe_texture_target target)
{
   switch (target) {
   case PIPE_BUFFER:
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
      return ISL_SURF_DIM_1D;
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_CUBE_ARRAY:
      return ISL_SURF_DIM_2D;
   case PIPE_TEXTURE_3D:
      return ISL_SURF_DIM_3D;
   case PIPE_MAX_TEXTURE_TYPES:
      break;
   }
   unreachable("invalid texture type");
}

static void
iris_query_dmabuf_modifiers(struct pipe_screen *pscreen,
                            enum pipe_format pfmt,
                            int max,
                            uint64_t *modifiers,
                            unsigned int *external_only,
                            int *count)
{
   struct iris_screen *screen = (void *) pscreen;
   const struct gen_device_info *devinfo = &screen->devinfo;

   uint64_t all_modifiers[] = {
      DRM_FORMAT_MOD_LINEAR,
      I915_FORMAT_MOD_X_TILED,
      I915_FORMAT_MOD_Y_TILED,
      // XXX: (broken) I915_FORMAT_MOD_Y_TILED_CCS,
   };

   int supported_mods = 0;

   for (int i = 0; i < ARRAY_SIZE(all_modifiers); i++) {
      if (!modifier_is_supported(devinfo, all_modifiers[i]))
         continue;

      if (supported_mods < max) {
         if (modifiers)
            modifiers[supported_mods] = all_modifiers[i];

         if (external_only)
            external_only[supported_mods] = util_format_is_yuv(pfmt);
      }

      supported_mods++;
   }

   *count = supported_mods;
}

static isl_surf_usage_flags_t
pipe_bind_to_isl_usage(unsigned bindings)
{
   isl_surf_usage_flags_t usage = 0;

   if (bindings & PIPE_BIND_RENDER_TARGET)
      usage |= ISL_SURF_USAGE_RENDER_TARGET_BIT;

   if (bindings & PIPE_BIND_SAMPLER_VIEW)
      usage |= ISL_SURF_USAGE_TEXTURE_BIT;

   if (bindings & (PIPE_BIND_SHADER_IMAGE | PIPE_BIND_SHADER_BUFFER))
      usage |= ISL_SURF_USAGE_STORAGE_BIT;

   if (bindings & PIPE_BIND_DISPLAY_TARGET)
      usage |= ISL_SURF_USAGE_DISPLAY_BIT;

   return usage;
}

struct pipe_resource *
iris_resource_get_separate_stencil(struct pipe_resource *p_res)
{
   /* For packed depth-stencil, we treat depth as the primary resource
    * and store S8 as the "second plane" resource.
    */
   return p_res->next;
}

static void
iris_resource_set_separate_stencil(struct pipe_resource *p_res,
                                   struct pipe_resource *stencil)
{
   assert(util_format_has_depth(util_format_description(p_res->format)));
   pipe_resource_reference(&p_res->next, stencil);
}

void
iris_get_depth_stencil_resources(struct pipe_resource *res,
                                 struct iris_resource **out_z,
                                 struct iris_resource **out_s)
{
   if (!res) {
      *out_z = NULL;
      *out_s = NULL;
      return;
   }

   if (res->format != PIPE_FORMAT_S8_UINT) {
      *out_z = (void *) res;
      *out_s = (void *) iris_resource_get_separate_stencil(res);
   } else {
      *out_z = NULL;
      *out_s = (void *) res;
   }
}

void
iris_resource_disable_aux(struct iris_resource *res)
{
   iris_bo_unreference(res->aux.bo);
   iris_bo_unreference(res->aux.clear_color_bo);
   free(res->aux.state);

   res->aux.usage = ISL_AUX_USAGE_NONE;
   res->aux.possible_usages = 1 << ISL_AUX_USAGE_NONE;
   res->aux.sampler_usages = 1 << ISL_AUX_USAGE_NONE;
   res->aux.surf.size_B = 0;
   res->aux.bo = NULL;
   res->aux.clear_color_bo = NULL;
   res->aux.state = NULL;
}

static void
iris_resource_destroy(struct pipe_screen *screen,
                      struct pipe_resource *resource)
{
   struct iris_resource *res = (struct iris_resource *)resource;

   if (resource->target == PIPE_BUFFER)
      util_range_destroy(&res->valid_buffer_range);

   iris_resource_disable_aux(res);

   iris_bo_unreference(res->bo);
   free(res);
}

static struct iris_resource *
iris_alloc_resource(struct pipe_screen *pscreen,
                    const struct pipe_resource *templ)
{
   struct iris_resource *res = calloc(1, sizeof(struct iris_resource));
   if (!res)
      return NULL;

   res->base = *templ;
   res->base.screen = pscreen;
   pipe_reference_init(&res->base.reference, 1);

   res->aux.possible_usages = 1 << ISL_AUX_USAGE_NONE;
   res->aux.sampler_usages = 1 << ISL_AUX_USAGE_NONE;

   if (templ->target == PIPE_BUFFER)
      util_range_init(&res->valid_buffer_range);

   return res;
}

unsigned
iris_get_num_logical_layers(const struct iris_resource *res, unsigned level)
{
   if (res->surf.dim == ISL_SURF_DIM_3D)
      return minify(res->surf.logical_level0_px.depth, level);
   else
      return res->surf.logical_level0_px.array_len;
}

static enum isl_aux_state **
create_aux_state_map(struct iris_resource *res, enum isl_aux_state initial)
{
   uint32_t total_slices = 0;
   for (uint32_t level = 0; level < res->surf.levels; level++)
      total_slices += iris_get_num_logical_layers(res, level);

   const size_t per_level_array_size =
      res->surf.levels * sizeof(enum isl_aux_state *);

   /* We're going to allocate a single chunk of data for both the per-level
    * reference array and the arrays of aux_state.  This makes cleanup
    * significantly easier.
    */
   const size_t total_size =
      per_level_array_size + total_slices * sizeof(enum isl_aux_state);

   void *data = malloc(total_size);
   if (!data)
      return NULL;

   enum isl_aux_state **per_level_arr = data;
   enum isl_aux_state *s = data + per_level_array_size;
   for (uint32_t level = 0; level < res->surf.levels; level++) {
      per_level_arr[level] = s;
      const unsigned level_layers = iris_get_num_logical_layers(res, level);
      for (uint32_t a = 0; a < level_layers; a++)
         *(s++) = initial;
   }
   assert((void *)s == data + total_size);

   return per_level_arr;
}

/**
 * Allocate the initial aux surface for a resource based on aux.usage
 */
static bool
iris_resource_alloc_aux(struct iris_screen *screen, struct iris_resource *res)
{
   struct isl_device *isl_dev = &screen->isl_dev;
   enum isl_aux_state initial_state;
   UNUSED bool ok = false;
   uint8_t memset_value = 0;
   uint32_t alloc_flags = 0;
   const struct gen_device_info *devinfo = &screen->devinfo;
   const unsigned clear_color_state_size = devinfo->gen >= 10 ?
      screen->isl_dev.ss.clear_color_state_size :
      (devinfo->gen >= 9 ? screen->isl_dev.ss.clear_value_size : 0);

   assert(!res->aux.bo);

   switch (res->aux.usage) {
   case ISL_AUX_USAGE_NONE:
      res->aux.surf.size_B = 0;
      break;
   case ISL_AUX_USAGE_HIZ:
      initial_state = ISL_AUX_STATE_AUX_INVALID;
      memset_value = 0;
      ok = isl_surf_get_hiz_surf(isl_dev, &res->surf, &res->aux.surf);
      break;
   case ISL_AUX_USAGE_MCS:
      /* The Ivybridge PRM, Vol 2 Part 1 p326 says:
       *
       *    "When MCS buffer is enabled and bound to MSRT, it is required
       *     that it is cleared prior to any rendering."
       *
       * Since we only use the MCS buffer for rendering, we just clear it
       * immediately on allocation.  The clear value for MCS buffers is all
       * 1's, so we simply memset it to 0xff.
       */
      initial_state = ISL_AUX_STATE_CLEAR;
      memset_value = 0xFF;
      ok = isl_surf_get_mcs_surf(isl_dev, &res->surf, &res->aux.surf);
      break;
   case ISL_AUX_USAGE_CCS_D:
   case ISL_AUX_USAGE_CCS_E:
      /* When CCS_E is used, we need to ensure that the CCS starts off in
       * a valid state.  From the Sky Lake PRM, "MCS Buffer for Render
       * Target(s)":
       *
       *    "If Software wants to enable Color Compression without Fast
       *     clear, Software needs to initialize MCS with zeros."
       *
       * A CCS value of 0 indicates that the corresponding block is in the
       * pass-through state which is what we want.
       *
       * For CCS_D, do the same thing.  On Gen9+, this avoids having any
       * undefined bits in the aux buffer.
       */
      initial_state = ISL_AUX_STATE_PASS_THROUGH;
      alloc_flags |= BO_ALLOC_ZEROED;
      ok = isl_surf_get_ccs_surf(isl_dev, &res->surf, &res->aux.surf, 0);
      break;
   }

   /* No work is needed for a zero-sized auxiliary buffer. */
   if (res->aux.surf.size_B == 0)
      return true;

   /* Assert that ISL gave us a valid aux surf */
   assert(ok);

   /* Create the aux_state for the auxiliary buffer. */
   res->aux.state = create_aux_state_map(res, initial_state);
   if (!res->aux.state)
      return false;

   uint64_t size = res->aux.surf.size_B;

   /* Allocate space in the buffer for storing the clear color. On modern
    * platforms (gen > 9), we can read it directly from such buffer.
    *
    * On gen <= 9, we are going to store the clear color on the buffer
    * anyways, and copy it back to the surface state during state emission.
    */
   res->aux.clear_color_offset = size;
   size += clear_color_state_size;

   /* Allocate the auxiliary buffer.  ISL has stricter set of alignment rules
    * the drm allocator.  Therefore, one can pass the ISL dimensions in terms
    * of bytes instead of trying to recalculate based on different format
    * block sizes.
    */
   res->aux.bo = iris_bo_alloc_tiled(screen->bufmgr, "aux buffer", size,
                                     IRIS_MEMZONE_OTHER, I915_TILING_Y,
                                     res->aux.surf.row_pitch_B, alloc_flags);
   if (!res->aux.bo) {
      return false;
   }

   if (!(alloc_flags & BO_ALLOC_ZEROED)) {
      void *map = iris_bo_map(NULL, res->aux.bo, MAP_WRITE | MAP_RAW);

      if (!map) {
         iris_resource_disable_aux(res);
         return false;
      }

      if (memset_value != 0)
         memset(map, memset_value, res->aux.surf.size_B);

      /* Zero the indirect clear color to match ::fast_clear_color. */
      memset((char *)map + res->aux.clear_color_offset, 0,
             clear_color_state_size);

      iris_bo_unmap(res->aux.bo);
   }

   if (clear_color_state_size > 0) {
      res->aux.clear_color_bo = res->aux.bo;
      iris_bo_reference(res->aux.clear_color_bo);
   }

   if (res->aux.usage == ISL_AUX_USAGE_HIZ) {
      for (unsigned level = 0; level < res->surf.levels; ++level) {
         uint32_t width = u_minify(res->surf.phys_level0_sa.width, level);
         uint32_t height = u_minify(res->surf.phys_level0_sa.height, level);

         /* Disable HiZ for LOD > 0 unless the width/height are 8x4 aligned.
          * For LOD == 0, we can grow the dimensions to make it work.
          */
         if (level == 0 || ((width & 7) == 0 && (height & 3) == 0))
            res->aux.has_hiz |= 1 << level;
      }
   }

   return true;
}

static bool
supports_mcs(const struct isl_surf *surf)
{
   /* MCS compression only applies to multisampled resources. */
   if (surf->samples <= 1)
      return false;

   /* See isl_surf_get_mcs_surf for details. */
   if (surf->samples == 16 && surf->logical_level0_px.width > 8192)
      return false;

   /* Depth and stencil buffers use the IMS (interleaved) layout. */
   if (isl_surf_usage_is_depth_or_stencil(surf->usage))
      return false;

   return true;
}

static bool
supports_ccs(const struct gen_device_info *devinfo,
             const struct isl_surf *surf)
{
   /* Gen9+ only supports CCS for Y-tiled buffers. */
   if (surf->tiling != ISL_TILING_Y0)
      return false;

   /* CCS only supports singlesampled resources. */
   if (surf->samples > 1)
      return false;

   /* The PRM doesn't say this explicitly, but fast-clears don't appear to
    * work for 3D textures until Gen9 where the layout of 3D textures changes
    * to match 2D array textures.
    */
   if (devinfo->gen < 9 && surf->dim != ISL_SURF_DIM_2D)
      return false;

   /* Note: still need to check the format! */

   return true;
}

static struct pipe_resource *
iris_resource_create_for_buffer(struct pipe_screen *pscreen,
                                const struct pipe_resource *templ)
{
   struct iris_screen *screen = (struct iris_screen *)pscreen;
   struct iris_resource *res = iris_alloc_resource(pscreen, templ);

   assert(templ->target == PIPE_BUFFER);
   assert(templ->height0 <= 1);
   assert(templ->depth0 <= 1);
   assert(templ->format == PIPE_FORMAT_NONE ||
          util_format_get_blocksize(templ->format) == 1);

   res->internal_format = templ->format;
   res->surf.tiling = ISL_TILING_LINEAR;

   enum iris_memory_zone memzone = IRIS_MEMZONE_OTHER;
   const char *name = templ->target == PIPE_BUFFER ? "buffer" : "miptree";
   if (templ->flags & IRIS_RESOURCE_FLAG_SHADER_MEMZONE) {
      memzone = IRIS_MEMZONE_SHADER;
      name = "shader kernels";
   } else if (templ->flags & IRIS_RESOURCE_FLAG_SURFACE_MEMZONE) {
      memzone = IRIS_MEMZONE_SURFACE;
      name = "surface state";
   } else if (templ->flags & IRIS_RESOURCE_FLAG_DYNAMIC_MEMZONE) {
      memzone = IRIS_MEMZONE_DYNAMIC;
      name = "dynamic state";
   }

   res->bo = iris_bo_alloc(screen->bufmgr, name, templ->width0, memzone);
   if (!res->bo) {
      iris_resource_destroy(pscreen, &res->base);
      return NULL;
   }

   return &res->base;
}

static struct pipe_resource *
iris_resource_create_with_modifiers(struct pipe_screen *pscreen,
                                    const struct pipe_resource *templ,
                                    const uint64_t *modifiers,
                                    int modifiers_count)
{
   struct iris_screen *screen = (struct iris_screen *)pscreen;
   struct gen_device_info *devinfo = &screen->devinfo;
   struct iris_resource *res = iris_alloc_resource(pscreen, templ);

   if (!res)
      return NULL;

   const struct util_format_description *format_desc =
      util_format_description(templ->format);
   const bool has_depth = util_format_has_depth(format_desc);
   uint64_t modifier =
      select_best_modifier(devinfo, modifiers, modifiers_count);

   isl_tiling_flags_t tiling_flags = ISL_TILING_ANY_MASK;

   if (modifier != DRM_FORMAT_MOD_INVALID) {
      res->mod_info = isl_drm_modifier_get_info(modifier);

      tiling_flags = 1 << res->mod_info->tiling;
   } else {
      if (modifiers_count > 0) {
         fprintf(stderr, "Unsupported modifier, resource creation failed.\n");
         return NULL;
      }

      /* No modifiers - we can select our own tiling. */

      if (has_depth) {
         /* Depth must be Y-tiled */
         tiling_flags = ISL_TILING_Y0_BIT;
      } else if (templ->format == PIPE_FORMAT_S8_UINT) {
         /* Stencil must be W-tiled */
         tiling_flags = ISL_TILING_W_BIT;
      } else if (templ->target == PIPE_BUFFER ||
                 templ->target == PIPE_TEXTURE_1D ||
                 templ->target == PIPE_TEXTURE_1D_ARRAY) {
         /* Use linear for buffers and 1D textures */
         tiling_flags = ISL_TILING_LINEAR_BIT;
      }

      /* Use linear for staging buffers */
      if (templ->usage == PIPE_USAGE_STAGING ||
          templ->bind & (PIPE_BIND_LINEAR | PIPE_BIND_CURSOR) )
         tiling_flags = ISL_TILING_LINEAR_BIT;
   }

   isl_surf_usage_flags_t usage = pipe_bind_to_isl_usage(templ->bind);

   if (templ->target == PIPE_TEXTURE_CUBE ||
       templ->target == PIPE_TEXTURE_CUBE_ARRAY)
      usage |= ISL_SURF_USAGE_CUBE_BIT;

   if (templ->usage != PIPE_USAGE_STAGING) {
      if (templ->format == PIPE_FORMAT_S8_UINT)
         usage |= ISL_SURF_USAGE_STENCIL_BIT;
      else if (has_depth)
         usage |= ISL_SURF_USAGE_DEPTH_BIT;
   }

   enum pipe_format pfmt = templ->format;
   res->internal_format = pfmt;

   /* Should be handled by u_transfer_helper */
   assert(!util_format_is_depth_and_stencil(pfmt));

   struct iris_format_info fmt = iris_format_for_usage(devinfo, pfmt, usage);
   assert(fmt.fmt != ISL_FORMAT_UNSUPPORTED);

   UNUSED const bool isl_surf_created_successfully =
      isl_surf_init(&screen->isl_dev, &res->surf,
                    .dim = target_to_isl_surf_dim(templ->target),
                    .format = fmt.fmt,
                    .width = templ->width0,
                    .height = templ->height0,
                    .depth = templ->depth0,
                    .levels = templ->last_level + 1,
                    .array_len = templ->array_size,
                    .samples = MAX2(templ->nr_samples, 1),
                    .min_alignment_B = 0,
                    .row_pitch_B = 0,
                    .usage = usage,
                    .tiling_flags = tiling_flags);
   assert(isl_surf_created_successfully);

   if (res->mod_info) {
      res->aux.possible_usages |= 1 << res->mod_info->aux_usage;
   } else if (supports_mcs(&res->surf)) {
      res->aux.possible_usages |= 1 << ISL_AUX_USAGE_MCS;
   } else if (has_depth) {
      if (likely(!(INTEL_DEBUG & DEBUG_NO_HIZ)))
         res->aux.possible_usages |= 1 << ISL_AUX_USAGE_HIZ;
   } else if (likely(!(INTEL_DEBUG & DEBUG_NO_RBC)) &&
              supports_ccs(devinfo, &res->surf)) {
      if (isl_format_supports_ccs_e(devinfo, res->surf.format))
         res->aux.possible_usages |= 1 << ISL_AUX_USAGE_CCS_E;

      if (isl_format_supports_ccs_d(devinfo, res->surf.format))
         res->aux.possible_usages |= 1 << ISL_AUX_USAGE_CCS_D;
   }

   res->aux.usage = util_last_bit(res->aux.possible_usages) - 1;

   res->aux.sampler_usages = res->aux.possible_usages;

   /* We don't always support sampling with hiz. But when we do, it must be
    * single sampled.
    */
   if (!devinfo->has_sample_with_hiz || res->surf.samples > 1) {
      res->aux.sampler_usages &= ~(1 << ISL_AUX_USAGE_HIZ);
   }

   const char *name = "miptree";
   enum iris_memory_zone memzone = IRIS_MEMZONE_OTHER;

   unsigned int flags = 0;
   if (templ->usage == PIPE_USAGE_STAGING)
      flags |= BO_ALLOC_COHERENT;

   /* These are for u_upload_mgr buffers only */
   assert(!(templ->flags & (IRIS_RESOURCE_FLAG_SHADER_MEMZONE |
                            IRIS_RESOURCE_FLAG_SURFACE_MEMZONE |
                            IRIS_RESOURCE_FLAG_DYNAMIC_MEMZONE)));

   res->bo = iris_bo_alloc_tiled(screen->bufmgr, name, res->surf.size_B,
                                 memzone,
                                 isl_tiling_to_i915_tiling(res->surf.tiling),
                                 res->surf.row_pitch_B, flags);

   if (!res->bo)
      goto fail;

   if (!iris_resource_alloc_aux(screen, res))
      goto fail;

   return &res->base;

fail:
   fprintf(stderr, "XXX: resource creation failed\n");
   iris_resource_destroy(pscreen, &res->base);
   return NULL;

}

static struct pipe_resource *
iris_resource_create(struct pipe_screen *pscreen,
                     const struct pipe_resource *templ)
{
   if (templ->target == PIPE_BUFFER)
      return iris_resource_create_for_buffer(pscreen, templ);
   else
      return iris_resource_create_with_modifiers(pscreen, templ, NULL, 0);
}

static uint64_t
tiling_to_modifier(uint32_t tiling)
{
   static const uint64_t map[] = {
      [I915_TILING_NONE]   = DRM_FORMAT_MOD_LINEAR,
      [I915_TILING_X]      = I915_FORMAT_MOD_X_TILED,
      [I915_TILING_Y]      = I915_FORMAT_MOD_Y_TILED,
   };

   assert(tiling < ARRAY_SIZE(map));

   return map[tiling];
}

static struct pipe_resource *
iris_resource_from_user_memory(struct pipe_screen *pscreen,
                               const struct pipe_resource *templ,
                               void *user_memory)
{
   struct iris_screen *screen = (struct iris_screen *)pscreen;
   struct iris_bufmgr *bufmgr = screen->bufmgr;
   struct iris_resource *res = iris_alloc_resource(pscreen, templ);
   if (!res)
      return NULL;

   assert(templ->target == PIPE_BUFFER);

   res->internal_format = templ->format;
   res->bo = iris_bo_create_userptr(bufmgr, "user",
                                    user_memory, templ->width0,
                                    IRIS_MEMZONE_OTHER);
   if (!res->bo) {
      free(res);
      return NULL;
   }

   util_range_add(&res->valid_buffer_range, 0, templ->width0);

   return &res->base;
}

static struct pipe_resource *
iris_resource_from_handle(struct pipe_screen *pscreen,
                          const struct pipe_resource *templ,
                          struct winsys_handle *whandle,
                          unsigned usage)
{
   struct iris_screen *screen = (struct iris_screen *)pscreen;
   struct gen_device_info *devinfo = &screen->devinfo;
   struct iris_bufmgr *bufmgr = screen->bufmgr;
   struct iris_resource *res = iris_alloc_resource(pscreen, templ);
   if (!res)
      return NULL;

   if (whandle->offset != 0) {
      dbg_printf("Attempt to import unsupported winsys offset %u\n",
                 whandle->offset);
      goto fail;
   }

   switch (whandle->type) {
   case WINSYS_HANDLE_TYPE_FD:
      res->bo = iris_bo_import_dmabuf(bufmgr, whandle->handle);
      break;
   case WINSYS_HANDLE_TYPE_SHARED:
      res->bo = iris_bo_gem_create_from_name(bufmgr, "winsys image",
                                             whandle->handle);
      break;
   default:
      unreachable("invalid winsys handle type");
   }
   if (!res->bo)
      return NULL;

   uint64_t modifier = whandle->modifier;
   if (modifier == DRM_FORMAT_MOD_INVALID) {
      modifier = tiling_to_modifier(res->bo->tiling_mode);
   }
   res->mod_info = isl_drm_modifier_get_info(modifier);
   assert(res->mod_info);

   isl_surf_usage_flags_t isl_usage = pipe_bind_to_isl_usage(templ->bind);

   const struct iris_format_info fmt =
      iris_format_for_usage(devinfo, templ->format, isl_usage);
   res->internal_format = templ->format;

   if (templ->target == PIPE_BUFFER) {
      res->surf.tiling = ISL_TILING_LINEAR;
   } else {
      isl_surf_init(&screen->isl_dev, &res->surf,
                    .dim = target_to_isl_surf_dim(templ->target),
                    .format = fmt.fmt,
                    .width = templ->width0,
                    .height = templ->height0,
                    .depth = templ->depth0,
                    .levels = templ->last_level + 1,
                    .array_len = templ->array_size,
                    .samples = MAX2(templ->nr_samples, 1),
                    .min_alignment_B = 0,
                    .row_pitch_B = whandle->stride,
                    .usage = isl_usage,
                    .tiling_flags = 1 << res->mod_info->tiling);

      assert(res->bo->tiling_mode ==
             isl_tiling_to_i915_tiling(res->surf.tiling));

      // XXX: create_ccs_buf_for_image?
      if (!iris_resource_alloc_aux(screen, res))
         goto fail;
   }

   return &res->base;

fail:
   iris_resource_destroy(pscreen, &res->base);
   return NULL;
}

static void
iris_flush_resource(struct pipe_context *ctx, struct pipe_resource *resource)
{
   struct iris_context *ice = (struct iris_context *)ctx;
   struct iris_batch *render_batch = &ice->batches[IRIS_BATCH_RENDER];
   struct iris_resource *res = (void *) resource;
   const struct isl_drm_modifier_info *mod = res->mod_info;

   iris_resource_prepare_access(ice, render_batch, res,
                                0, INTEL_REMAINING_LEVELS,
                                0, INTEL_REMAINING_LAYERS,
                                mod ? mod->aux_usage : ISL_AUX_USAGE_NONE,
                                mod ? mod->supports_clear_color : false);
}

static boolean
iris_resource_get_handle(struct pipe_screen *pscreen,
                         struct pipe_context *ctx,
                         struct pipe_resource *resource,
                         struct winsys_handle *whandle,
                         unsigned usage)
{
   struct iris_resource *res = (struct iris_resource *)resource;

   /* Disable aux usage if explicit flush not set and this is the
    * first time we are dealing with this resource.
    */
   if ((!(usage & PIPE_HANDLE_USAGE_EXPLICIT_FLUSH) && res->aux.usage != 0)) {
      if (p_atomic_read(&resource->reference.count) == 1)
         iris_resource_disable_aux(res);
   }

   /* If this is a buffer, stride should be 0 - no need to special case */
   whandle->stride = res->surf.row_pitch_B;
   whandle->modifier =
      res->mod_info ? res->mod_info->modifier
                    : tiling_to_modifier(res->bo->tiling_mode);

#ifndef NDEBUG
   enum isl_aux_usage allowed_usage =
      res->mod_info ? res->mod_info->aux_usage : ISL_AUX_USAGE_NONE;

   if (res->aux.usage != allowed_usage) {
      enum isl_aux_state aux_state = iris_resource_get_aux_state(res, 0, 0);
      assert(aux_state == ISL_AUX_STATE_RESOLVED ||
             aux_state == ISL_AUX_STATE_PASS_THROUGH);
   }
#endif

   switch (whandle->type) {
   case WINSYS_HANDLE_TYPE_SHARED:
      return iris_bo_flink(res->bo, &whandle->handle) == 0;
   case WINSYS_HANDLE_TYPE_KMS:
      whandle->handle = iris_bo_export_gem_handle(res->bo);
      return true;
   case WINSYS_HANDLE_TYPE_FD:
      return iris_bo_export_dmabuf(res->bo, (int *) &whandle->handle) == 0;
   }

   return false;
}

static bool
resource_is_busy(struct iris_context *ice,
                 struct iris_resource *res)
{
   bool busy = iris_bo_busy(res->bo);

   for (int i = 0; i < IRIS_BATCH_COUNT; i++)
      busy |= iris_batch_references(&ice->batches[i], res->bo);

   return busy;
}

static void
iris_invalidate_resource(struct pipe_context *ctx,
                         struct pipe_resource *resource)
{
   struct iris_screen *screen = (void *) ctx->screen;
   struct iris_context *ice = (void *) ctx;
   struct iris_resource *res = (void *) resource;

   if (resource->target != PIPE_BUFFER)
      return;

   if (!resource_is_busy(ice, res)) {
      /* The resource is idle, so just mark that it contains no data and
       * keep using the same underlying buffer object.
       */
      util_range_set_empty(&res->valid_buffer_range);
      return;
   }

   /* Otherwise, try and replace the backing storage with a new BO. */

   /* We can't reallocate memory we didn't allocate in the first place. */
   if (res->bo->userptr)
      return;

   // XXX: We should support this.
   if (res->bind_history & PIPE_BIND_STREAM_OUTPUT)
      return;

   struct iris_bo *old_bo = res->bo;
   struct iris_bo *new_bo =
      iris_bo_alloc(screen->bufmgr, res->bo->name, resource->width0,
                    iris_memzone_for_address(old_bo->gtt_offset));
   if (!new_bo)
      return;

   /* Swap out the backing storage */
   res->bo = new_bo;

   /* Rebind the buffer, replacing any state referring to the old BO's
    * address, and marking state dirty so it's reemitted.
    */
   ice->vtbl.rebind_buffer(ice, res, old_bo->gtt_offset);

   util_range_set_empty(&res->valid_buffer_range);

   iris_bo_unreference(old_bo);
}

static void
iris_flush_staging_region(struct pipe_transfer *xfer,
                          const struct pipe_box *flush_box)
{
   if (!(xfer->usage & PIPE_TRANSFER_WRITE))
      return;

   struct iris_transfer *map = (void *) xfer;

   struct pipe_box src_box = *flush_box;

   /* Account for extra alignment padding in staging buffer */
   if (xfer->resource->target == PIPE_BUFFER)
      src_box.x += xfer->box.x % IRIS_MAP_BUFFER_ALIGNMENT;

   struct pipe_box dst_box = (struct pipe_box) {
      .x = xfer->box.x + flush_box->x,
      .y = xfer->box.y + flush_box->y,
      .z = xfer->box.z + flush_box->z,
      .width = flush_box->width,
      .height = flush_box->height,
      .depth = flush_box->depth,
   };

   iris_copy_region(map->blorp, map->batch, xfer->resource, xfer->level,
                    dst_box.x, dst_box.y, dst_box.z, map->staging, 0,
                    &src_box);
}

static void
iris_unmap_copy_region(struct iris_transfer *map)
{
   iris_resource_destroy(map->staging->screen, map->staging);

   map->ptr = NULL;
}

static void
iris_map_copy_region(struct iris_transfer *map)
{
   struct pipe_screen *pscreen = &map->batch->screen->base;
   struct pipe_transfer *xfer = &map->base;
   struct pipe_box *box = &xfer->box;
   struct iris_resource *res = (void *) xfer->resource;

   unsigned extra = xfer->resource->target == PIPE_BUFFER ?
                    box->x % IRIS_MAP_BUFFER_ALIGNMENT : 0;

   struct pipe_resource templ = (struct pipe_resource) {
      .usage = PIPE_USAGE_STAGING,
      .width0 = box->width + extra,
      .height0 = box->height,
      .depth0 = 1,
      .nr_samples = xfer->resource->nr_samples,
      .nr_storage_samples = xfer->resource->nr_storage_samples,
      .array_size = box->depth,
   };

   if (xfer->resource->target == PIPE_BUFFER)
      templ.target = PIPE_BUFFER;
   else if (templ.array_size > 1)
      templ.target = PIPE_TEXTURE_2D_ARRAY;
   else
      templ.target = PIPE_TEXTURE_2D;

   /* Depth, stencil, and ASTC can't be linear surfaces, so we can't use
    * xfer->resource->format directly.  Pick a bpb compatible format so
    * resource creation will succeed; blorp_copy will override it anyway.
    */
   switch (util_format_get_blocksizebits(res->internal_format)) {
   case 8:   templ.format = PIPE_FORMAT_R8_UINT;           break;
   case 16:  templ.format = PIPE_FORMAT_R8G8_UINT;         break;
   case 24:  templ.format = PIPE_FORMAT_R8G8B8_UINT;       break;
   case 32:  templ.format = PIPE_FORMAT_R8G8B8A8_UINT;     break;
   case 48:  templ.format = PIPE_FORMAT_R16G16B16_UINT;    break;
   case 64:  templ.format = PIPE_FORMAT_R16G16B16A16_UINT; break;
   case 96:  templ.format = PIPE_FORMAT_R32G32B32_UINT;    break;
   case 128: templ.format = PIPE_FORMAT_R32G32B32A32_UINT; break;
   default: unreachable("Invalid bpb");
   }

   map->staging = iris_resource_create(pscreen, &templ);
   assert(map->staging);

   if (templ.target != PIPE_BUFFER) {
      struct isl_surf *surf = &((struct iris_resource *) map->staging)->surf;
      xfer->stride = isl_surf_get_row_pitch_B(surf);
      xfer->layer_stride = isl_surf_get_array_pitch(surf);
   }

   if (!(xfer->usage & PIPE_TRANSFER_DISCARD_RANGE)) {
      iris_copy_region(map->blorp, map->batch, map->staging, 0, extra, 0, 0,
                       xfer->resource, xfer->level, box);
      /* Ensure writes to the staging BO land before we map it below. */
      iris_emit_pipe_control_flush(map->batch,
                                   PIPE_CONTROL_RENDER_TARGET_FLUSH |
                                   PIPE_CONTROL_CS_STALL);
   }

   struct iris_bo *staging_bo = iris_resource_bo(map->staging);

   if (iris_batch_references(map->batch, staging_bo))
      iris_batch_flush(map->batch);

   map->ptr =
      iris_bo_map(map->dbg, staging_bo, xfer->usage & MAP_FLAGS) + extra;

   map->unmap = iris_unmap_copy_region;
}

static void
get_image_offset_el(const struct isl_surf *surf, unsigned level, unsigned z,
                    unsigned *out_x0_el, unsigned *out_y0_el)
{
   if (surf->dim == ISL_SURF_DIM_3D) {
      isl_surf_get_image_offset_el(surf, level, 0, z, out_x0_el, out_y0_el);
   } else {
      isl_surf_get_image_offset_el(surf, level, z, 0, out_x0_el, out_y0_el);
   }
}

/**
 * Get pointer offset into stencil buffer.
 *
 * The stencil buffer is W tiled. Since the GTT is incapable of W fencing, we
 * must decode the tile's layout in software.
 *
 * See
 *   - PRM, 2011 Sandy Bridge, Volume 1, Part 2, Section 4.5.2.1 W-Major Tile
 *     Format.
 *   - PRM, 2011 Sandy Bridge, Volume 1, Part 2, Section 4.5.3 Tiling Algorithm
 *
 * Even though the returned offset is always positive, the return type is
 * signed due to
 *    commit e8b1c6d6f55f5be3bef25084fdd8b6127517e137
 *    mesa: Fix return type of  _mesa_get_format_bytes() (#37351)
 */
static intptr_t
s8_offset(uint32_t stride, uint32_t x, uint32_t y, bool swizzled)
{
   uint32_t tile_size = 4096;
   uint32_t tile_width = 64;
   uint32_t tile_height = 64;
   uint32_t row_size = 64 * stride / 2; /* Two rows are interleaved. */

   uint32_t tile_x = x / tile_width;
   uint32_t tile_y = y / tile_height;

   /* The byte's address relative to the tile's base addres. */
   uint32_t byte_x = x % tile_width;
   uint32_t byte_y = y % tile_height;

   uintptr_t u = tile_y * row_size
               + tile_x * tile_size
               + 512 * (byte_x / 8)
               +  64 * (byte_y / 8)
               +  32 * ((byte_y / 4) % 2)
               +  16 * ((byte_x / 4) % 2)
               +   8 * ((byte_y / 2) % 2)
               +   4 * ((byte_x / 2) % 2)
               +   2 * (byte_y % 2)
               +   1 * (byte_x % 2);

   if (swizzled) {
      /* adjust for bit6 swizzling */
      if (((byte_x / 8) % 2) == 1) {
         if (((byte_y / 8) % 2) == 0) {
            u += 64;
         } else {
            u -= 64;
         }
      }
   }

   return u;
}

static void
iris_unmap_s8(struct iris_transfer *map)
{
   struct pipe_transfer *xfer = &map->base;
   const struct pipe_box *box = &xfer->box;
   struct iris_resource *res = (struct iris_resource *) xfer->resource;
   struct isl_surf *surf = &res->surf;
   const bool has_swizzling = false;

   if (xfer->usage & PIPE_TRANSFER_WRITE) {
      uint8_t *untiled_s8_map = map->ptr;
      uint8_t *tiled_s8_map =
         iris_bo_map(map->dbg, res->bo, (xfer->usage | MAP_RAW) & MAP_FLAGS);

      for (int s = 0; s < box->depth; s++) {
         unsigned x0_el, y0_el;
         get_image_offset_el(surf, xfer->level, box->z + s, &x0_el, &y0_el);

         for (uint32_t y = 0; y < box->height; y++) {
            for (uint32_t x = 0; x < box->width; x++) {
               ptrdiff_t offset = s8_offset(surf->row_pitch_B,
                                            x0_el + box->x + x,
                                            y0_el + box->y + y,
                                            has_swizzling);
               tiled_s8_map[offset] =
                  untiled_s8_map[s * xfer->layer_stride + y * xfer->stride + x];
            }
         }
      }
   }

   free(map->buffer);
}

static void
iris_map_s8(struct iris_transfer *map)
{
   struct pipe_transfer *xfer = &map->base;
   const struct pipe_box *box = &xfer->box;
   struct iris_resource *res = (struct iris_resource *) xfer->resource;
   struct isl_surf *surf = &res->surf;

   xfer->stride = surf->row_pitch_B;
   xfer->layer_stride = xfer->stride * box->height;

   /* The tiling and detiling functions require that the linear buffer has
    * a 16-byte alignment (that is, its `x0` is 16-byte aligned).  Here we
    * over-allocate the linear buffer to get the proper alignment.
    */
   map->buffer = map->ptr = malloc(xfer->layer_stride * box->depth);
   assert(map->buffer);

   const bool has_swizzling = false;

   /* One of either READ_BIT or WRITE_BIT or both is set.  READ_BIT implies no
    * INVALIDATE_RANGE_BIT.  WRITE_BIT needs the original values read in unless
    * invalidate is set, since we'll be writing the whole rectangle from our
    * temporary buffer back out.
    */
   if (!(xfer->usage & PIPE_TRANSFER_DISCARD_RANGE)) {
      uint8_t *untiled_s8_map = map->ptr;
      uint8_t *tiled_s8_map =
         iris_bo_map(map->dbg, res->bo, (xfer->usage | MAP_RAW) & MAP_FLAGS);

      for (int s = 0; s < box->depth; s++) {
         unsigned x0_el, y0_el;
         get_image_offset_el(surf, xfer->level, box->z + s, &x0_el, &y0_el);

         for (uint32_t y = 0; y < box->height; y++) {
            for (uint32_t x = 0; x < box->width; x++) {
               ptrdiff_t offset = s8_offset(surf->row_pitch_B,
                                            x0_el + box->x + x,
                                            y0_el + box->y + y,
                                            has_swizzling);
               untiled_s8_map[s * xfer->layer_stride + y * xfer->stride + x] =
                  tiled_s8_map[offset];
            }
         }
      }
   }

   map->unmap = iris_unmap_s8;
}

/* Compute extent parameters for use with tiled_memcpy functions.
 * xs are in units of bytes and ys are in units of strides.
 */
static inline void
tile_extents(const struct isl_surf *surf,
             const struct pipe_box *box,
             unsigned level, int z,
             unsigned *x1_B, unsigned *x2_B,
             unsigned *y1_el, unsigned *y2_el)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(surf->format);
   const unsigned cpp = fmtl->bpb / 8;

   assert(box->x % fmtl->bw == 0);
   assert(box->y % fmtl->bh == 0);

   unsigned x0_el, y0_el;
   get_image_offset_el(surf, level, box->z + z, &x0_el, &y0_el);

   *x1_B = (box->x / fmtl->bw + x0_el) * cpp;
   *y1_el = box->y / fmtl->bh + y0_el;
   *x2_B = (DIV_ROUND_UP(box->x + box->width, fmtl->bw) + x0_el) * cpp;
   *y2_el = DIV_ROUND_UP(box->y + box->height, fmtl->bh) + y0_el;
}

static void
iris_unmap_tiled_memcpy(struct iris_transfer *map)
{
   struct pipe_transfer *xfer = &map->base;
   const struct pipe_box *box = &xfer->box;
   struct iris_resource *res = (struct iris_resource *) xfer->resource;
   struct isl_surf *surf = &res->surf;

   const bool has_swizzling = false;

   if (xfer->usage & PIPE_TRANSFER_WRITE) {
      char *dst =
         iris_bo_map(map->dbg, res->bo, (xfer->usage | MAP_RAW) & MAP_FLAGS);

      for (int s = 0; s < box->depth; s++) {
         unsigned x1, x2, y1, y2;
         tile_extents(surf, box, xfer->level, s, &x1, &x2, &y1, &y2);

         void *ptr = map->ptr + s * xfer->layer_stride;

         isl_memcpy_linear_to_tiled(x1, x2, y1, y2, dst, ptr,
                                    surf->row_pitch_B, xfer->stride,
                                    has_swizzling, surf->tiling, ISL_MEMCPY);
      }
   }
   os_free_aligned(map->buffer);
   map->buffer = map->ptr = NULL;
}

static void
iris_map_tiled_memcpy(struct iris_transfer *map)
{
   struct pipe_transfer *xfer = &map->base;
   const struct pipe_box *box = &xfer->box;
   struct iris_resource *res = (struct iris_resource *) xfer->resource;
   struct isl_surf *surf = &res->surf;

   xfer->stride = ALIGN(surf->row_pitch_B, 16);
   xfer->layer_stride = xfer->stride * box->height;

   unsigned x1, x2, y1, y2;
   tile_extents(surf, box, xfer->level, 0, &x1, &x2, &y1, &y2);

   /* The tiling and detiling functions require that the linear buffer has
    * a 16-byte alignment (that is, its `x0` is 16-byte aligned).  Here we
    * over-allocate the linear buffer to get the proper alignment.
    */
   map->buffer =
      os_malloc_aligned(xfer->layer_stride * box->depth, 16);
   assert(map->buffer);
   map->ptr = (char *)map->buffer + (x1 & 0xf);

   const bool has_swizzling = false;

   // XXX: PIPE_TRANSFER_READ?
   if (!(xfer->usage & PIPE_TRANSFER_DISCARD_RANGE)) {
      char *src =
         iris_bo_map(map->dbg, res->bo, (xfer->usage | MAP_RAW) & MAP_FLAGS);

      for (int s = 0; s < box->depth; s++) {
         unsigned x1, x2, y1, y2;
         tile_extents(surf, box, xfer->level, s, &x1, &x2, &y1, &y2);

         /* Use 's' rather than 'box->z' to rebase the first slice to 0. */
         void *ptr = map->ptr + s * xfer->layer_stride;

         isl_memcpy_tiled_to_linear(x1, x2, y1, y2, ptr, src, xfer->stride,
                                    surf->row_pitch_B, has_swizzling,
                                    surf->tiling, ISL_MEMCPY_STREAMING_LOAD);
      }
   }

   map->unmap = iris_unmap_tiled_memcpy;
}

static void
iris_map_direct(struct iris_transfer *map)
{
   struct pipe_transfer *xfer = &map->base;
   struct pipe_box *box = &xfer->box;
   struct iris_resource *res = (struct iris_resource *) xfer->resource;

   void *ptr = iris_bo_map(map->dbg, res->bo, xfer->usage & MAP_FLAGS);

   if (res->base.target == PIPE_BUFFER) {
      xfer->stride = 0;
      xfer->layer_stride = 0;

      map->ptr = ptr + box->x;
   } else {
      struct isl_surf *surf = &res->surf;
      const struct isl_format_layout *fmtl =
         isl_format_get_layout(surf->format);
      const unsigned cpp = fmtl->bpb / 8;
      unsigned x0_el, y0_el;

      get_image_offset_el(surf, xfer->level, box->z, &x0_el, &y0_el);

      xfer->stride = isl_surf_get_row_pitch_B(surf);
      xfer->layer_stride = isl_surf_get_array_pitch(surf);

      map->ptr = ptr + (y0_el + box->y) * xfer->stride + (x0_el + box->x) * cpp;
   }
}

static bool
can_promote_to_async(const struct iris_resource *res,
                     const struct pipe_box *box,
                     enum pipe_transfer_usage usage)
{
   /* If we're writing to a section of the buffer that hasn't even been
    * initialized with useful data, then we can safely promote this write
    * to be unsynchronized.  This helps the common pattern of appending data.
    */
   return res->base.target == PIPE_BUFFER && (usage & PIPE_TRANSFER_WRITE) &&
          !(usage & TC_TRANSFER_MAP_NO_INFER_UNSYNCHRONIZED) &&
          !util_ranges_intersect(&res->valid_buffer_range, box->x,
                                 box->x + box->width);
}

static void *
iris_transfer_map(struct pipe_context *ctx,
                  struct pipe_resource *resource,
                  unsigned level,
                  enum pipe_transfer_usage usage,
                  const struct pipe_box *box,
                  struct pipe_transfer **ptransfer)
{
   struct iris_context *ice = (struct iris_context *)ctx;
   struct iris_resource *res = (struct iris_resource *)resource;
   struct isl_surf *surf = &res->surf;

   if (usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE) {
      /* Replace the backing storage with a fresh buffer for non-async maps */
      if (!(usage & (PIPE_TRANSFER_UNSYNCHRONIZED |
                     TC_TRANSFER_MAP_NO_INVALIDATE)))
         iris_invalidate_resource(ctx, resource);

      /* If we can discard the whole resource, we can discard the range. */
      usage |= PIPE_TRANSFER_DISCARD_RANGE;
   }

   bool map_would_stall = false;

   if (resource->target != PIPE_BUFFER) {
      iris_resource_access_raw(ice, &ice->batches[IRIS_BATCH_RENDER], res,
                               level, box->z, box->depth,
                               usage & PIPE_TRANSFER_WRITE);
   }

   if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED) &&
       can_promote_to_async(res, box, usage)) {
      usage |= PIPE_TRANSFER_UNSYNCHRONIZED;
   }

   if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
      map_would_stall = resource_is_busy(ice, res);

      if (map_would_stall && (usage & PIPE_TRANSFER_DONTBLOCK) &&
                             (usage & PIPE_TRANSFER_MAP_DIRECTLY))
         return NULL;
   }

   if (surf->tiling != ISL_TILING_LINEAR &&
       (usage & PIPE_TRANSFER_MAP_DIRECTLY))
      return NULL;

   struct iris_transfer *map = slab_alloc(&ice->transfer_pool);
   struct pipe_transfer *xfer = &map->base;

   if (!map)
      return NULL;

   memset(map, 0, sizeof(*map));
   map->dbg = &ice->dbg;

   pipe_resource_reference(&xfer->resource, resource);
   xfer->level = level;
   xfer->usage = usage;
   xfer->box = *box;
   *ptransfer = xfer;

   if (usage & PIPE_TRANSFER_WRITE)
      util_range_add(&res->valid_buffer_range, box->x, box->x + box->width);

   /* Avoid using GPU copies for persistent/coherent buffers, as the idea
    * there is to access them simultaneously on the CPU & GPU.  This also
    * avoids trying to use GPU copies for our u_upload_mgr buffers which
    * contain state we're constructing for a GPU draw call, which would
    * kill us with infinite stack recursion.
    */
   bool no_gpu = usage & (PIPE_TRANSFER_PERSISTENT |
                          PIPE_TRANSFER_COHERENT |
                          PIPE_TRANSFER_MAP_DIRECTLY);

   /* GPU copies are not useful for buffer reads.  Instead of stalling to
    * read from the original buffer, we'd simply copy it to a temporary...
    * then stall (a bit longer) to read from that buffer.
    *
    * Images are less clear-cut.  Color resolves are destructive, removing
    * the underlying compression, so we'd rather blit the data to a linear
    * temporary and map that, to avoid the resolve.  (It might be better to
    * a tiled temporary and use the tiled_memcpy paths...)
    */
   if (!(usage & PIPE_TRANSFER_DISCARD_RANGE) &&
       res->aux.usage != ISL_AUX_USAGE_CCS_E &&
       res->aux.usage != ISL_AUX_USAGE_CCS_D) {
      no_gpu = true;
   }

   if ((map_would_stall || res->aux.usage == ISL_AUX_USAGE_CCS_E) && !no_gpu) {
      /* If we need a synchronous mapping and the resource is busy,
       * we copy to/from a linear temporary buffer using the GPU.
       */
      map->batch = &ice->batches[IRIS_BATCH_RENDER];
      map->blorp = &ice->blorp;
      iris_map_copy_region(map);
   } else {
      /* Otherwise we're free to map on the CPU.  Flush if needed. */
      if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
         for (int i = 0; i < IRIS_BATCH_COUNT; i++) {
            if (iris_batch_references(&ice->batches[i], res->bo))
               iris_batch_flush(&ice->batches[i]);
         }
      }

      if (surf->tiling == ISL_TILING_W) {
         /* TODO: Teach iris_map_tiled_memcpy about W-tiling... */
         iris_map_s8(map);
      } else if (surf->tiling != ISL_TILING_LINEAR) {
         iris_map_tiled_memcpy(map);
      } else {
         iris_map_direct(map);
      }
   }

   return map->ptr;
}

static void
iris_transfer_flush_region(struct pipe_context *ctx,
                           struct pipe_transfer *xfer,
                           const struct pipe_box *box)
{
   struct iris_context *ice = (struct iris_context *)ctx;
   struct iris_resource *res = (struct iris_resource *) xfer->resource;
   struct iris_transfer *map = (void *) xfer;

   if (map->staging)
      iris_flush_staging_region(xfer, box);

   for (int i = 0; i < IRIS_BATCH_COUNT; i++) {
      if (ice->batches[i].contains_draw ||
          ice->batches[i].cache.render->entries) {
         iris_batch_maybe_flush(&ice->batches[i], 24);
         iris_flush_and_dirty_for_history(ice, &ice->batches[i], res);
      }
   }

   /* Make sure we flag constants dirty even if there's no need to emit
    * any PIPE_CONTROLs to a batch.
    */
   iris_dirty_for_history(ice, res);
}

static void
iris_transfer_unmap(struct pipe_context *ctx, struct pipe_transfer *xfer)
{
   struct iris_context *ice = (struct iris_context *)ctx;
   struct iris_transfer *map = (void *) xfer;

   if (!(xfer->usage & PIPE_TRANSFER_FLUSH_EXPLICIT)) {
      struct pipe_box flush_box = {
         .x = 0, .y = 0, .z = 0,
         .width  = xfer->box.width,
         .height = xfer->box.height,
         .depth  = xfer->box.depth,
      };
      iris_transfer_flush_region(ctx, xfer, &flush_box);
   }

   if (map->unmap)
      map->unmap(map);

   pipe_resource_reference(&xfer->resource, NULL);
   slab_free(&ice->transfer_pool, map);
}

/**
 * Mark state dirty that needs to be re-emitted when a resource is written.
 */
void
iris_dirty_for_history(struct iris_context *ice,
                       struct iris_resource *res)
{
   uint64_t dirty = 0ull;

   if (res->bind_history & PIPE_BIND_CONSTANT_BUFFER) {
      dirty |= IRIS_DIRTY_CONSTANTS_VS |
               IRIS_DIRTY_CONSTANTS_TCS |
               IRIS_DIRTY_CONSTANTS_TES |
               IRIS_DIRTY_CONSTANTS_GS |
               IRIS_DIRTY_CONSTANTS_FS |
               IRIS_DIRTY_CONSTANTS_CS |
               IRIS_ALL_DIRTY_BINDINGS;
   }

   ice->state.dirty |= dirty;
}

/**
 * Produce a set of PIPE_CONTROL bits which ensure data written to a
 * resource becomes visible, and any stale read cache data is invalidated.
 */
uint32_t
iris_flush_bits_for_history(struct iris_resource *res)
{
   uint32_t flush = PIPE_CONTROL_CS_STALL;

   if (res->bind_history & PIPE_BIND_CONSTANT_BUFFER) {
      flush |= PIPE_CONTROL_CONST_CACHE_INVALIDATE |
               PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE;
   }

   if (res->bind_history & PIPE_BIND_SAMPLER_VIEW)
      flush |= PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE;

   if (res->bind_history & (PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER))
      flush |= PIPE_CONTROL_VF_CACHE_INVALIDATE;

   if (res->bind_history & (PIPE_BIND_SHADER_BUFFER | PIPE_BIND_SHADER_IMAGE))
      flush |= PIPE_CONTROL_DATA_CACHE_FLUSH;

   return flush;
}

void
iris_flush_and_dirty_for_history(struct iris_context *ice,
                                 struct iris_batch *batch,
                                 struct iris_resource *res)
{
   if (res->base.target != PIPE_BUFFER)
      return;

   uint32_t flush = iris_flush_bits_for_history(res);

   /* We've likely used the rendering engine (i.e. BLORP) to write to this
    * surface.  Flush the render cache so the data actually lands.
    */
   if (batch->name != IRIS_BATCH_COMPUTE)
      flush |= PIPE_CONTROL_RENDER_TARGET_FLUSH;

   iris_emit_pipe_control_flush(batch, flush);

   iris_dirty_for_history(ice, res);
}

bool
iris_resource_set_clear_color(struct iris_context *ice,
                              struct iris_resource *res,
                              union isl_color_value color)
{
   if (memcmp(&res->aux.clear_color, &color, sizeof(color)) != 0) {
      res->aux.clear_color = color;
      return true;
   }

   return false;
}

union isl_color_value
iris_resource_get_clear_color(const struct iris_resource *res,
                              struct iris_bo **clear_color_bo,
                              uint64_t *clear_color_offset)
{
   assert(res->aux.bo);

   if (clear_color_bo)
      *clear_color_bo = res->aux.clear_color_bo;
   if (clear_color_offset)
      *clear_color_offset = res->aux.clear_color_offset;
   return res->aux.clear_color;
}

static enum pipe_format
iris_resource_get_internal_format(struct pipe_resource *p_res)
{
   struct iris_resource *res = (void *) p_res;
   return res->internal_format;
}

static const struct u_transfer_vtbl transfer_vtbl = {
   .resource_create       = iris_resource_create,
   .resource_destroy      = iris_resource_destroy,
   .transfer_map          = iris_transfer_map,
   .transfer_unmap        = iris_transfer_unmap,
   .transfer_flush_region = iris_transfer_flush_region,
   .get_internal_format   = iris_resource_get_internal_format,
   .set_stencil           = iris_resource_set_separate_stencil,
   .get_stencil           = iris_resource_get_separate_stencil,
};

void
iris_init_screen_resource_functions(struct pipe_screen *pscreen)
{
   pscreen->query_dmabuf_modifiers = iris_query_dmabuf_modifiers;
   pscreen->resource_create_with_modifiers =
      iris_resource_create_with_modifiers;
   pscreen->resource_create = u_transfer_helper_resource_create;
   pscreen->resource_from_user_memory = iris_resource_from_user_memory;
   pscreen->resource_from_handle = iris_resource_from_handle;
   pscreen->resource_get_handle = iris_resource_get_handle;
   pscreen->resource_destroy = u_transfer_helper_resource_destroy;
   pscreen->transfer_helper =
      u_transfer_helper_create(&transfer_vtbl, true, true, false, true);
}

void
iris_init_resource_functions(struct pipe_context *ctx)
{
   ctx->flush_resource = iris_flush_resource;
   ctx->invalidate_resource = iris_invalidate_resource;
   ctx->transfer_map = u_transfer_helper_transfer_map;
   ctx->transfer_flush_region = u_transfer_helper_transfer_flush_region;
   ctx->transfer_unmap = u_transfer_helper_transfer_unmap;
   ctx->buffer_subdata = u_default_buffer_subdata;
   ctx->texture_subdata = u_default_texture_subdata;
}
