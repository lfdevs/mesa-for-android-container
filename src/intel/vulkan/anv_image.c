/*
 * Copyright © 2015 Intel Corporation
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
 */

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "drm-uapi/drm_fourcc.h"

#include "anv_private.h"
#include "util/debug.h"
#include "vk_util.h"
#include "util/u_math.h"

#include "vk_format_info.h"

static isl_surf_usage_flags_t
choose_isl_surf_usage(VkImageCreateFlags vk_create_flags,
                      VkImageUsageFlags vk_usage,
                      isl_surf_usage_flags_t isl_extra_usage,
                      VkImageAspectFlagBits aspect)
{
   isl_surf_usage_flags_t isl_usage = isl_extra_usage;

   if (vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT)
      isl_usage |= ISL_SURF_USAGE_TEXTURE_BIT;

   if (vk_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
      isl_usage |= ISL_SURF_USAGE_TEXTURE_BIT;

   if (vk_usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
      isl_usage |= ISL_SURF_USAGE_RENDER_TARGET_BIT;

   if (vk_create_flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
      isl_usage |= ISL_SURF_USAGE_CUBE_BIT;

   /* Even if we're only using it for transfer operations, clears to depth and
    * stencil images happen as depth and stencil so they need the right ISL
    * usage bits or else things will fall apart.
    */
   switch (aspect) {
   case VK_IMAGE_ASPECT_DEPTH_BIT:
      isl_usage |= ISL_SURF_USAGE_DEPTH_BIT;
      break;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      isl_usage |= ISL_SURF_USAGE_STENCIL_BIT;
      break;
   case VK_IMAGE_ASPECT_COLOR_BIT:
   case VK_IMAGE_ASPECT_PLANE_0_BIT:
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
      break;
   default:
      unreachable("bad VkImageAspect");
   }

   if (vk_usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
      /* blorp implements transfers by sampling from the source image. */
      isl_usage |= ISL_SURF_USAGE_TEXTURE_BIT;
   }

   if (vk_usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT &&
       aspect == VK_IMAGE_ASPECT_COLOR_BIT) {
      /* blorp implements transfers by rendering into the destination image.
       * Only request this with color images, as we deal with depth/stencil
       * formats differently. */
      isl_usage |= ISL_SURF_USAGE_RENDER_TARGET_BIT;
   }

   return isl_usage;
}

static isl_tiling_flags_t
choose_isl_tiling_flags(const struct gen_device_info *devinfo,
                        const struct anv_image_create_info *anv_info,
                        const struct isl_drm_modifier_info *isl_mod_info,
                        bool legacy_scanout)
{
   const VkImageCreateInfo *base_info = anv_info->vk_info;
   isl_tiling_flags_t flags = 0;

   switch (base_info->tiling) {
   default:
      unreachable("bad VkImageTiling");
   case VK_IMAGE_TILING_OPTIMAL:
      flags = ISL_TILING_ANY_MASK;
      break;
   case VK_IMAGE_TILING_LINEAR:
      flags = ISL_TILING_LINEAR_BIT;
      break;
   case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT:
      assert(isl_mod_info);
      flags = 1 << isl_mod_info->tiling;
   }

   if (anv_info->isl_tiling_flags)
      flags &= anv_info->isl_tiling_flags;

   if (legacy_scanout) {
      isl_tiling_flags_t legacy_mask = ISL_TILING_LINEAR_BIT;
      if (devinfo->has_tiling_uapi)
         legacy_mask |= ISL_TILING_X_BIT;
      flags &= legacy_mask;
   }

   assert(flags);

   return flags;
}

static void
add_surface(struct anv_image *image, struct anv_surface *surf, uint32_t plane)
{
   assert(surf->isl.size_B > 0); /* isl surface must be initialized */

   if (image->disjoint) {
      surf->offset = align_u32(image->planes[plane].size,
                               surf->isl.alignment_B);
      /* Plane offset is always 0 when it's disjoint. */
   } else {
      surf->offset = align_u32(image->size, surf->isl.alignment_B);
      /* Determine plane's offset only once when the first surface is added. */
      if (image->planes[plane].size == 0)
         image->planes[plane].offset = image->size;
   }

   image->size = surf->offset + surf->isl.size_B;
   image->planes[plane].size = (surf->offset + surf->isl.size_B) - image->planes[plane].offset;

   image->alignment = MAX2(image->alignment, surf->isl.alignment_B);
   image->planes[plane].alignment = MAX2(image->planes[plane].alignment,
                                         surf->isl.alignment_B);
}

/**
 * Do hardware limitations require the image plane to use a shadow surface?
 *
 * If hardware limitations force us to use a shadow surface, then the same
 * limitations may also constrain the tiling of the primary surface; therefore
 * paramater @a inout_primary_tiling_flags.
 *
 * If the image plane is a separate stencil plane and if the user provided
 * VkImageStencilUsageCreateInfoEXT, then @a usage must be stencilUsage.
 *
 * @see anv_image::planes[]::shadow_surface
 */
static bool
anv_image_plane_needs_shadow_surface(const struct gen_device_info *devinfo,
                                     struct anv_format_plane plane_format,
                                     VkImageTiling vk_tiling,
                                     VkImageUsageFlags vk_plane_usage,
                                     VkImageCreateFlags vk_create_flags,
                                     isl_tiling_flags_t *inout_primary_tiling_flags)
{
   if (devinfo->gen <= 8 &&
       (vk_create_flags & VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT) &&
       vk_tiling == VK_IMAGE_TILING_OPTIMAL) {
      /* We must fallback to a linear surface because we may not be able to
       * correctly handle the offsets if tiled. (On gen9,
       * RENDER_SURFACE_STATE::X/Y Offset are sufficient). To prevent garbage
       * performance while texturing, we maintain a tiled shadow surface.
       */
      assert(isl_format_is_compressed(plane_format.isl_format));

      if (inout_primary_tiling_flags) {
         *inout_primary_tiling_flags = ISL_TILING_LINEAR_BIT;
      }

      return true;
   }

   if (devinfo->gen <= 7 &&
       plane_format.aspect == VK_IMAGE_ASPECT_STENCIL_BIT &&
       (vk_plane_usage & VK_IMAGE_USAGE_SAMPLED_BIT)) {
      /* gen7 can't sample from W-tiled surfaces. */
      return true;
   }

   return false;
}

bool
anv_formats_ccs_e_compatible(const struct gen_device_info *devinfo,
                             VkImageCreateFlags create_flags,
                             VkFormat vk_format,
                             VkImageTiling vk_tiling,
                             const VkImageFormatListCreateInfoKHR *fmt_list)
{
   enum isl_format format =
      anv_get_isl_format(devinfo, vk_format,
                         VK_IMAGE_ASPECT_COLOR_BIT, vk_tiling);

   if (!isl_format_supports_ccs_e(devinfo, format))
      return false;

   if (!(create_flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT))
      return true;

   if (!fmt_list || fmt_list->viewFormatCount == 0)
      return false;

   for (uint32_t i = 0; i < fmt_list->viewFormatCount; i++) {
      enum isl_format view_format =
         anv_get_isl_format(devinfo, fmt_list->pViewFormats[i],
                            VK_IMAGE_ASPECT_COLOR_BIT, vk_tiling);

      if (!isl_formats_are_ccs_e_compatible(devinfo, format, view_format))
         return false;
   }

   return true;
}

/**
 * For color images that have an auxiliary surface, request allocation for an
 * additional buffer that mainly stores fast-clear values. Use of this buffer
 * allows us to access the image's subresources while being aware of their
 * fast-clear values in non-trivial cases (e.g., outside of a render pass in
 * which a fast clear has occurred).
 *
 * In order to avoid having multiple clear colors for a single plane of an
 * image (hence a single RENDER_SURFACE_STATE), we only allow fast-clears on
 * the first slice (level 0, layer 0).  At the time of our testing (Jan 17,
 * 2018), there were no known applications which would benefit from fast-
 * clearing more than just the first slice.
 *
 * The fast clear portion of the image is laid out in the following order:
 *
 *  * 1 or 4 dwords (depending on hardware generation) for the clear color
 *  * 1 dword for the anv_fast_clear_type of the clear color
 *  * On gen9+, 1 dword per level and layer of the image (3D levels count
 *    multiple layers) in level-major order for compression state.
 *
 * For the purpose of discoverability, the algorithm used to manage
 * compression and fast-clears is described here:
 *
 *  * On a transition from UNDEFINED or PREINITIALIZED to a defined layout,
 *    all of the values in the fast clear portion of the image are initialized
 *    to default values.
 *
 *  * On fast-clear, the clear value is written into surface state and also
 *    into the buffer and the fast clear type is set appropriately.  Both
 *    setting the fast-clear value in the buffer and setting the fast-clear
 *    type happen from the GPU using MI commands.
 *
 *  * Whenever a render or blorp operation is performed with CCS_E, we call
 *    genX(cmd_buffer_mark_image_written) to set the compression state to
 *    true (which is represented by UINT32_MAX).
 *
 *  * On pipeline barrier transitions, the worst-case transition is computed
 *    from the image layouts.  The command streamer inspects the fast clear
 *    type and compression state dwords and constructs a predicate.  The
 *    worst-case resolve is performed with the given predicate and the fast
 *    clear and compression state is set accordingly.
 *
 * See anv_layout_to_aux_usage and anv_layout_to_fast_clear_type functions for
 * details on exactly what is allowed in what layouts.
 *
 * On gen7-9, we do not have a concept of indirect clear colors in hardware.
 * In order to deal with this, we have to do some clear color management.
 *
 *  * For LOAD_OP_LOAD at the top of a renderpass, we have to copy the clear
 *    value from the buffer into the surface state with MI commands.
 *
 *  * For any blorp operations, we pass the address to the clear value into
 *    blorp and it knows to copy the clear color.
 */
static void
add_aux_state_tracking_buffer(struct anv_image *image,
                              uint32_t plane,
                              const struct anv_device *device)
{
   assert(image && device);
   assert(image->planes[plane].aux_usage != ISL_AUX_USAGE_NONE &&
          image->aspects & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV);

   /* Compressed images must be tiled and therefore everything should be 4K
    * aligned.  The CCS has the same alignment requirements.  This is good
    * because we need at least dword-alignment for MI_LOAD/STORE operations.
    */
   assert(image->alignment % 4 == 0);
   assert((image->planes[plane].offset + image->planes[plane].size) % 4 == 0);

   /* This buffer should be at the very end of the plane. */
   if (image->disjoint) {
      assert(image->planes[plane].size ==
             (image->planes[plane].offset + image->planes[plane].size));
   } else {
      assert(image->size ==
             (image->planes[plane].offset + image->planes[plane].size));
   }

   const unsigned clear_color_state_size = device->info.gen >= 10 ?
      device->isl_dev.ss.clear_color_state_size :
      device->isl_dev.ss.clear_value_size;

   /* Clear color and fast clear type */
   unsigned state_size = clear_color_state_size + 4;

   /* We only need to track compression on CCS_E surfaces. */
   if (image->planes[plane].aux_usage == ISL_AUX_USAGE_CCS_E) {
      if (image->type == VK_IMAGE_TYPE_3D) {
         for (uint32_t l = 0; l < image->levels; l++)
            state_size += anv_minify(image->extent.depth, l) * 4;
      } else {
         state_size += image->levels * image->array_size * 4;
      }
   }

   /* Add some padding to make sure the fast clear color state buffer starts at
    * a 4K alignment. We believe that 256B might be enough, but due to lack of
    * testing we will leave this as 4K for now.
    */
   image->planes[plane].size = align_u64(image->planes[plane].size, 4096);
   image->size = align_u64(image->size, 4096);

   assert(image->planes[plane].offset % 4096 == 0);

   image->planes[plane].fast_clear_state_offset =
      image->planes[plane].offset + image->planes[plane].size;

   image->planes[plane].size += state_size;
   image->size += state_size;
}

/**
 * The return code indicates whether creation of the VkImage should continue
 * or fail, not whether the creation of the aux surface succeeded.  If the aux
 * surface is not required (for example, by neither hardware nor DRM format
 * modifier), then this may return VK_SUCCESS when creation of the aux surface
 * fails.
 */
static VkResult
add_aux_surface_if_supported(struct anv_device *device,
                             struct anv_image *image,
                             uint32_t plane,
                             struct anv_format_plane plane_format,
                             const VkImageFormatListCreateInfoKHR *fmt_list,
                             isl_surf_usage_flags_t isl_extra_usage_flags)
{
   VkImageAspectFlags aspect = plane_format.aspect;
   bool ok;

   /* The aux surface must not be already added. */
   assert(image->planes[plane].aux_surface.isl.size_B == 0);

   if ((isl_extra_usage_flags & ISL_SURF_USAGE_DISABLE_AUX_BIT))
      return VK_SUCCESS;

   if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
      /* We don't advertise that depth buffers could be used as storage
       * images.
       */
       assert(!(image->usage & VK_IMAGE_USAGE_STORAGE_BIT));

      /* Allow the user to control HiZ enabling. Disable by default on gen7
       * because resolves are not currently implemented pre-BDW.
       */
      if (!(image->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
         /* It will never be used as an attachment, HiZ is pointless. */
         return VK_SUCCESS;
      }

      if (device->info.gen == 7) {
         anv_perf_warn(device, image, "Implement gen7 HiZ");
         return VK_SUCCESS;
      }

      if (image->levels > 1) {
         anv_perf_warn(device, image, "Enable multi-LOD HiZ");
         return VK_SUCCESS;
      }

      if (device->info.gen == 8 && image->samples > 1) {
         anv_perf_warn(device, image, "Enable gen8 multisampled HiZ");
         return VK_SUCCESS;
      }

      if (unlikely(INTEL_DEBUG & DEBUG_NO_HIZ))
         return VK_SUCCESS;

      ok = isl_surf_get_hiz_surf(&device->isl_dev,
                                 &image->planes[plane].surface.isl,
                                 &image->planes[plane].aux_surface.isl);
      assert(ok);
      if (!isl_surf_supports_ccs(&device->isl_dev,
                                 &image->planes[plane].surface.isl)) {
         image->planes[plane].aux_usage = ISL_AUX_USAGE_HIZ;
      } else if (image->usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                                 VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) &&
                 image->samples == 1) {
         /* If it's used as an input attachment or a texture and it's
          * single-sampled (this is a requirement for HiZ+CCS write-through
          * mode), use write-through mode so that we don't need to resolve
          * before texturing.  This will make depth testing a bit slower but
          * texturing faster.
          *
          * TODO: This is a heuristic trade-off; we haven't tuned it at all.
          */
         assert(device->info.gen >= 12);
         image->planes[plane].aux_usage = ISL_AUX_USAGE_HIZ_CCS_WT;
      } else {
         assert(device->info.gen >= 12);
         image->planes[plane].aux_usage = ISL_AUX_USAGE_HIZ_CCS;
      }
      add_surface(image, &image->planes[plane].aux_surface, plane);
   } else if ((aspect & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV) && image->samples == 1) {
      if (image->n_planes != 1) {
         /* Multiplanar images seem to hit a sampler bug with CCS and R16G16
          * format. (Putting the clear state a page/4096bytes further fixes
          * the issue).
          */
         return VK_SUCCESS;
      }

      if ((image->create_flags & VK_IMAGE_CREATE_ALIAS_BIT)) {
         /* The image may alias a plane of a multiplanar image. Above we ban
          * CCS on multiplanar images.
          */
         return VK_SUCCESS;
      }

      if (!isl_format_supports_rendering(&device->info,
                                         plane_format.isl_format)) {
         /* Disable CCS because it is not useful (we can't render to the image
          * with CCS enabled).  While it may be technically possible to enable
          * CCS for this case, we currently don't have things hooked up to get
          * it working.
          */
         anv_perf_warn(device, image,
                       "This image format doesn't support rendering. "
                       "Not allocating an CCS buffer.");
         return VK_SUCCESS;
      }

      if (unlikely(INTEL_DEBUG & DEBUG_NO_RBC))
         return VK_SUCCESS;

      ok = isl_surf_get_ccs_surf(&device->isl_dev,
                                 &image->planes[plane].surface.isl,
                                 &image->planes[plane].aux_surface.isl,
                                 NULL, 0);
      if (!ok)
         return VK_SUCCESS;

      /* Choose aux usage */
      if (!(image->usage & VK_IMAGE_USAGE_STORAGE_BIT) &&
          anv_formats_ccs_e_compatible(&device->info,
                                       image->create_flags,
                                       image->vk_format,
                                       image->tiling,
                                       fmt_list)) {
         /* For images created without MUTABLE_FORMAT_BIT set, we know that
          * they will always be used with the original format.  In particular,
          * they will always be used with a format that supports color
          * compression.  If it's never used as a storage image, then it will
          * only be used through the sampler or the as a render target.  This
          * means that it's safe to just leave compression on at all times for
          * these formats.
          */
         image->planes[plane].aux_usage = ISL_AUX_USAGE_CCS_E;
      } else if (device->info.gen >= 12) {
         anv_perf_warn(device, image,
                       "The CCS_D aux mode is not yet handled on "
                       "Gen12+. Not allocating a CCS buffer.");
         image->planes[plane].aux_surface.isl.size_B = 0;
         return VK_SUCCESS;
      } else {
         image->planes[plane].aux_usage = ISL_AUX_USAGE_CCS_D;
      }

      if (!device->physical->has_implicit_ccs)
         add_surface(image, &image->planes[plane].aux_surface, plane);

      add_aux_state_tracking_buffer(image, plane, device);
   } else if ((aspect & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV) && image->samples > 1) {
      assert(!(image->usage & VK_IMAGE_USAGE_STORAGE_BIT));
      ok = isl_surf_get_mcs_surf(&device->isl_dev,
                                 &image->planes[plane].surface.isl,
                                 &image->planes[plane].aux_surface.isl);
      if (!ok)
         return VK_SUCCESS;

      image->planes[plane].aux_usage = ISL_AUX_USAGE_MCS;
      add_surface(image, &image->planes[plane].aux_surface, plane);
      add_aux_state_tracking_buffer(image, plane, device);
   }

   return VK_SUCCESS;
}

/**
 * Initialize the anv_image::*_surface selected by \a aspect. Then update the
 * image's memory requirements (that is, the image's size and alignment).
 */
static VkResult
make_surface(struct anv_device *device,
             struct anv_image *image,
             const VkImageFormatListCreateInfoKHR *fmt_list,
             uint32_t stride,
             isl_tiling_flags_t tiling_flags,
             isl_surf_usage_flags_t isl_extra_usage_flags,
             VkImageAspectFlagBits aspect)
{
   VkResult result;
   bool ok;

   static const enum isl_surf_dim vk_to_isl_surf_dim[] = {
      [VK_IMAGE_TYPE_1D] = ISL_SURF_DIM_1D,
      [VK_IMAGE_TYPE_2D] = ISL_SURF_DIM_2D,
      [VK_IMAGE_TYPE_3D] = ISL_SURF_DIM_3D,
   };

   image->extent = anv_sanitize_image_extent(image->type, image->extent);

   const unsigned plane = anv_image_aspect_to_plane(image->aspects, aspect);
   const  struct anv_format_plane plane_format =
      anv_get_format_plane(&device->info, image->vk_format, aspect, image->tiling);
   struct anv_surface *anv_surf = &image->planes[plane].surface;

   const isl_surf_usage_flags_t usage =
      choose_isl_surf_usage(image->create_flags, image->usage,
                            isl_extra_usage_flags, aspect);

   VkImageUsageFlags plane_vk_usage =
      aspect == VK_IMAGE_ASPECT_STENCIL_BIT ?
      image->stencil_usage : image->usage;

   bool needs_shadow =
      anv_image_plane_needs_shadow_surface(&device->info,
                                   plane_format,
                                   image->tiling,
                                   plane_vk_usage,
                                   image->create_flags,
                                   &tiling_flags);

   ok = isl_surf_init(&device->isl_dev, &anv_surf->isl,
      .dim = vk_to_isl_surf_dim[image->type],
      .format = plane_format.isl_format,
      .width = image->extent.width / plane_format.denominator_scales[0],
      .height = image->extent.height / plane_format.denominator_scales[1],
      .depth = image->extent.depth,
      .levels = image->levels,
      .array_len = image->array_size,
      .samples = image->samples,
      .min_alignment_B = 0,
      .row_pitch_B = stride,
      .usage = usage,
      .tiling_flags = tiling_flags);

   if (!ok)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   image->planes[plane].aux_usage = ISL_AUX_USAGE_NONE;

   add_surface(image, anv_surf, plane);

   if (needs_shadow) {
      ok = isl_surf_init(&device->isl_dev, &image->planes[plane].shadow_surface.isl,
         .dim = vk_to_isl_surf_dim[image->type],
         .format = plane_format.isl_format,
         .width = image->extent.width,
         .height = image->extent.height,
         .depth = image->extent.depth,
         .levels = image->levels,
         .array_len = image->array_size,
         .samples = image->samples,
         .min_alignment_B = 0,
         .row_pitch_B = stride,
         .usage = ISL_SURF_USAGE_TEXTURE_BIT |
                  (usage & ISL_SURF_USAGE_CUBE_BIT),
         .tiling_flags = ISL_TILING_ANY_MASK);

      /* isl_surf_init() will fail only if provided invalid input. Invalid input
       * is illegal in Vulkan.
       */
      assert(ok);

      add_surface(image, &image->planes[plane].shadow_surface, plane);
   }

   result = add_aux_surface_if_supported(device, image, plane, plane_format,
                                         fmt_list, isl_extra_usage_flags);
   if (result != VK_SUCCESS)
      return result;

   assert((image->planes[plane].offset + image->planes[plane].size) == image->size);

   /* Upper bound of the last surface should be smaller than the plane's
    * size.
    */
   assert((MAX2(image->planes[plane].surface.offset,
                image->planes[plane].aux_surface.offset) +
           (image->planes[plane].aux_surface.isl.size_B > 0 ?
            image->planes[plane].aux_surface.isl.size_B :
            image->planes[plane].surface.isl.size_B)) <=
          (image->planes[plane].offset + image->planes[plane].size));

   if (image->planes[plane].aux_usage != ISL_AUX_USAGE_NONE) {
      /* assert(image->planes[plane].fast_clear_state_offset == */
      /*        (image->planes[plane].aux_surface.offset + image->planes[plane].aux_surface.isl.size_B)); */
      assert(image->planes[plane].fast_clear_state_offset <
             (image->planes[plane].offset + image->planes[plane].size));
   }

   return VK_SUCCESS;
}

static uint32_t
score_drm_format_mod(uint64_t modifier)
{
   switch (modifier) {
   case DRM_FORMAT_MOD_LINEAR: return 1;
   case I915_FORMAT_MOD_X_TILED: return 2;
   case I915_FORMAT_MOD_Y_TILED: return 3;
   case I915_FORMAT_MOD_Y_TILED_CCS: return 4;
   default: unreachable("bad DRM format modifier");
   }
}

static const struct isl_drm_modifier_info *
choose_drm_format_mod(const struct anv_physical_device *device,
                      uint32_t modifier_count, const uint64_t *modifiers)
{
   uint64_t best_mod = UINT64_MAX;
   uint32_t best_score = 0;

   for (uint32_t i = 0; i < modifier_count; ++i) {
      uint32_t score = score_drm_format_mod(modifiers[i]);
      if (score > best_score) {
         best_mod = modifiers[i];
         best_score = score;
      }
   }

   if (best_score > 0)
      return isl_drm_modifier_get_info(best_mod);
   else
      return NULL;
}

VkResult
anv_image_create(VkDevice _device,
                 const struct anv_image_create_info *create_info,
                 const VkAllocationCallbacks* alloc,
                 VkImage *pImage)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   const VkImageCreateInfo *pCreateInfo = create_info->vk_info;
   const struct isl_drm_modifier_info *isl_mod_info = NULL;
   struct anv_image *image = NULL;
   VkResult r;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);

   const struct wsi_image_create_info *wsi_info =
      vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);

   if (pCreateInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      const VkImageDrmFormatModifierListCreateInfoEXT *mod_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);
      isl_mod_info = choose_drm_format_mod(device->physical,
                                           mod_info->drmFormatModifierCount,
                                           mod_info->pDrmFormatModifiers);
      assert(isl_mod_info);
   }

   anv_assert(pCreateInfo->mipLevels > 0);
   anv_assert(pCreateInfo->arrayLayers > 0);
   anv_assert(pCreateInfo->samples > 0);
   anv_assert(pCreateInfo->extent.width > 0);
   anv_assert(pCreateInfo->extent.height > 0);
   anv_assert(pCreateInfo->extent.depth > 0);

   image = vk_zalloc2(&device->vk.alloc, alloc, sizeof(*image), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!image)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &image->base, VK_OBJECT_TYPE_IMAGE);
   image->type = pCreateInfo->imageType;
   image->extent = pCreateInfo->extent;
   image->vk_format = pCreateInfo->format;
   image->format = anv_get_format(pCreateInfo->format);
   image->aspects = vk_format_aspects(image->vk_format);
   image->levels = pCreateInfo->mipLevels;
   image->array_size = pCreateInfo->arrayLayers;
   image->samples = pCreateInfo->samples;
   image->usage = pCreateInfo->usage;
   image->create_flags = pCreateInfo->flags;
   image->tiling = pCreateInfo->tiling;
   image->disjoint = pCreateInfo->flags & VK_IMAGE_CREATE_DISJOINT_BIT;
   image->needs_set_tiling = wsi_info && wsi_info->scanout;
   image->drm_format_mod = isl_mod_info ? isl_mod_info->modifier :
                                          DRM_FORMAT_MOD_INVALID;

   if (image->aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
      image->stencil_usage = pCreateInfo->usage;
      const VkImageStencilUsageCreateInfoEXT *stencil_usage_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_STENCIL_USAGE_CREATE_INFO_EXT);
      if (stencil_usage_info)
         image->stencil_usage = stencil_usage_info->stencilUsage;
   }

   /* In case of external format, We don't know format yet,
    * so skip the rest for now.
    */
   if (create_info->external_format) {
      image->external_format = true;
      *pImage = anv_image_to_handle(image);
      return VK_SUCCESS;
   }

   const struct anv_format *format = anv_get_format(image->vk_format);
   assert(format != NULL);

   const isl_tiling_flags_t isl_tiling_flags =
      choose_isl_tiling_flags(&device->info, create_info, isl_mod_info,
                              image->needs_set_tiling);

   image->n_planes = format->n_planes;

   const VkImageFormatListCreateInfoKHR *fmt_list =
      vk_find_struct_const(pCreateInfo->pNext,
                           IMAGE_FORMAT_LIST_CREATE_INFO_KHR);

   uint32_t b;
   for_each_bit(b, image->aspects) {
      r = make_surface(device, image, fmt_list, create_info->stride,
                       isl_tiling_flags, create_info->isl_extra_usage_flags,
                       (1 << b));
      if (r != VK_SUCCESS)
         goto fail;
   }

   *pImage = anv_image_to_handle(image);

   return VK_SUCCESS;

fail:
   if (image)
      vk_free2(&device->vk.alloc, alloc, image);

   return r;
}

static struct anv_image *
anv_swapchain_get_image(VkSwapchainKHR swapchain,
                        uint32_t index)
{
   uint32_t n_images = index + 1;
   VkImage *images = malloc(sizeof(*images) * n_images);
   VkResult result = wsi_common_get_images(swapchain, &n_images, images);

   if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
      free(images);
      return NULL;
   }

   ANV_FROM_HANDLE(anv_image, image, images[index]);
   free(images);

   return image;
}

static VkResult
anv_image_from_swapchain(VkDevice device,
                         const VkImageCreateInfo *pCreateInfo,
                         const VkImageSwapchainCreateInfoKHR *swapchain_info,
                         const VkAllocationCallbacks *pAllocator,
                         VkImage *pImage)
{
   struct anv_image *swapchain_image = anv_swapchain_get_image(swapchain_info->swapchain, 0);
   assert(swapchain_image);

   assert(swapchain_image->type == pCreateInfo->imageType);
   assert(swapchain_image->vk_format == pCreateInfo->format);
   assert(swapchain_image->extent.width == pCreateInfo->extent.width);
   assert(swapchain_image->extent.height == pCreateInfo->extent.height);
   assert(swapchain_image->extent.depth == pCreateInfo->extent.depth);
   assert(swapchain_image->array_size == pCreateInfo->arrayLayers);
   /* Color attachment is added by the wsi code. */
   assert(swapchain_image->usage == (pCreateInfo->usage | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));

   VkImageCreateInfo local_create_info;
   local_create_info = *pCreateInfo;
   local_create_info.pNext = NULL;
   /* The following parameters are implictly selected by the wsi code. */
   local_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
   local_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
   local_create_info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

   /* If the image has a particular modifier, specify that modifier. */
   VkImageDrmFormatModifierListCreateInfoEXT local_modifier_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
      .drmFormatModifierCount = 1,
      .pDrmFormatModifiers = &swapchain_image->drm_format_mod,
   };
   if (swapchain_image->drm_format_mod != DRM_FORMAT_MOD_INVALID)
      __vk_append_struct(&local_create_info, &local_modifier_info);

   return anv_image_create(device,
      &(struct anv_image_create_info) {
         .vk_info = &local_create_info,
         .external_format = swapchain_image->external_format,
      },
      pAllocator,
      pImage);
}

VkResult
anv_CreateImage(VkDevice device,
                const VkImageCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator,
                VkImage *pImage)
{
   const VkExternalMemoryImageCreateInfo *create_info =
      vk_find_struct_const(pCreateInfo->pNext, EXTERNAL_MEMORY_IMAGE_CREATE_INFO);

   if (create_info && (create_info->handleTypes &
       VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID))
      return anv_image_from_external(device, pCreateInfo, create_info,
                                     pAllocator, pImage);

   bool use_external_format = false;
   const VkExternalFormatANDROID *ext_format =
      vk_find_struct_const(pCreateInfo->pNext, EXTERNAL_FORMAT_ANDROID);

   /* "If externalFormat is zero, the effect is as if the
    * VkExternalFormatANDROID structure was not present. Otherwise, the image
    * will have the specified external format."
    */
   if (ext_format && ext_format->externalFormat != 0)
      use_external_format = true;

   const VkNativeBufferANDROID *gralloc_info =
      vk_find_struct_const(pCreateInfo->pNext, NATIVE_BUFFER_ANDROID);
   if (gralloc_info)
      return anv_image_from_gralloc(device, pCreateInfo, gralloc_info,
                                    pAllocator, pImage);

   const VkImageSwapchainCreateInfoKHR *swapchain_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);
   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE)
      return anv_image_from_swapchain(device, pCreateInfo, swapchain_info,
                                      pAllocator, pImage);

   return anv_image_create(device,
      &(struct anv_image_create_info) {
         .vk_info = pCreateInfo,
         .external_format = use_external_format,
      },
      pAllocator,
      pImage);
}

void
anv_DestroyImage(VkDevice _device, VkImage _image,
                 const VkAllocationCallbacks *pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, image, _image);

   if (!image)
      return;

   for (uint32_t p = 0; p < image->n_planes; ++p) {
      if (image->planes[p].bo_is_owned) {
         assert(image->planes[p].address.bo != NULL);
         anv_device_release_bo(device, image->planes[p].address.bo);
      }
   }

   vk_object_base_finish(&image->base);
   vk_free2(&device->vk.alloc, pAllocator, image);
}

static void anv_image_bind_memory_plane(struct anv_device *device,
                                        struct anv_image *image,
                                        uint32_t plane,
                                        struct anv_device_memory *memory,
                                        uint32_t memory_offset)
{
   assert(!image->planes[plane].bo_is_owned);

   if (!memory) {
      image->planes[plane].address = ANV_NULL_ADDRESS;
      return;
   }

   image->planes[plane].address = (struct anv_address) {
      .bo = memory->bo,
      .offset = memory_offset,
   };

   /* If we're on a platform that uses implicit CCS and our buffer does not
    * have any implicit CCS data, disable compression on that image.
    */
   if (device->physical->has_implicit_ccs && !memory->bo->has_implicit_ccs)
      image->planes[plane].aux_usage = ISL_AUX_USAGE_NONE;
}

/* We are binding AHardwareBuffer. Get a description, resolve the
 * format and prepare anv_image properly.
 */
static void
resolve_ahw_image(struct anv_device *device,
                  struct anv_image *image,
                  struct anv_device_memory *mem)
{
#if defined(ANDROID) && ANDROID_API_LEVEL >= 26
   assert(mem->ahw);
   AHardwareBuffer_Desc desc;
   AHardwareBuffer_describe(mem->ahw, &desc);

   /* Check tiling. */
   int i915_tiling = anv_gem_get_tiling(device, mem->bo->gem_handle);
   VkImageTiling vk_tiling;
   isl_tiling_flags_t isl_tiling_flags = 0;

   switch (i915_tiling) {
   case I915_TILING_NONE:
      vk_tiling = VK_IMAGE_TILING_LINEAR;
      isl_tiling_flags = ISL_TILING_LINEAR_BIT;
      break;
   case I915_TILING_X:
      vk_tiling = VK_IMAGE_TILING_OPTIMAL;
      isl_tiling_flags = ISL_TILING_X_BIT;
      break;
   case I915_TILING_Y:
      vk_tiling = VK_IMAGE_TILING_OPTIMAL;
      isl_tiling_flags = ISL_TILING_Y0_BIT;
      break;
   case -1:
   default:
      unreachable("Invalid tiling flags.");
   }

   assert(vk_tiling == VK_IMAGE_TILING_LINEAR ||
          vk_tiling == VK_IMAGE_TILING_OPTIMAL);

   /* Check format. */
   VkFormat vk_format = vk_format_from_android(desc.format, desc.usage);
   enum isl_format isl_fmt = anv_get_isl_format(&device->info,
                                                vk_format,
                                                VK_IMAGE_ASPECT_COLOR_BIT,
                                                vk_tiling);
   assert(isl_fmt != ISL_FORMAT_UNSUPPORTED);

   /* Handle RGB(X)->RGBA fallback. */
   switch (desc.format) {
   case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:
   case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
      if (isl_format_is_rgb(isl_fmt))
         isl_fmt = isl_format_rgb_to_rgba(isl_fmt);
      break;
   }

   /* Now we are able to fill anv_image fields properly and create
    * isl_surface for it.
    */
   image->vk_format = vk_format;
   image->format = anv_get_format(vk_format);
   image->aspects = vk_format_aspects(image->vk_format);
   image->n_planes = image->format->n_planes;

   uint32_t stride = desc.stride *
                     (isl_format_get_layout(isl_fmt)->bpb / 8);

   uint32_t b;
   for_each_bit(b, image->aspects) {
      VkResult r = make_surface(device, image, NULL, stride, isl_tiling_flags,
                                ISL_SURF_USAGE_DISABLE_AUX_BIT, (1 << b));
      assert(r == VK_SUCCESS);
   }
#endif
}

VkResult anv_BindImageMemory(
    VkDevice                                    _device,
    VkImage                                     _image,
    VkDeviceMemory                              _memory,
    VkDeviceSize                                memoryOffset)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_device_memory, mem, _memory);
   ANV_FROM_HANDLE(anv_image, image, _image);

   if (mem->ahw)
      resolve_ahw_image(device, image, mem);

   uint32_t aspect_bit;
   anv_foreach_image_aspect_bit(aspect_bit, image, image->aspects) {
      uint32_t plane =
         anv_image_aspect_to_plane(image->aspects, 1UL << aspect_bit);
      anv_image_bind_memory_plane(device, image, plane, mem, memoryOffset);
   }

   return VK_SUCCESS;
}

VkResult anv_BindImageMemory2(
    VkDevice                                    _device,
    uint32_t                                    bindInfoCount,
    const VkBindImageMemoryInfo*                pBindInfos)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   for (uint32_t i = 0; i < bindInfoCount; i++) {
      const VkBindImageMemoryInfo *bind_info = &pBindInfos[i];
      ANV_FROM_HANDLE(anv_device_memory, mem, bind_info->memory);
      ANV_FROM_HANDLE(anv_image, image, bind_info->image);

      /* Resolve will alter the image's aspects, do this first. */
      if (mem && mem->ahw)
         resolve_ahw_image(device, image, mem);

      VkImageAspectFlags aspects = image->aspects;
      vk_foreach_struct_const(s, bind_info->pNext) {
         switch (s->sType) {
         case VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO: {
            const VkBindImagePlaneMemoryInfo *plane_info =
               (const VkBindImagePlaneMemoryInfo *) s;

            aspects = plane_info->planeAspect;
            break;
         }
         case VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR: {
            const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
               (const VkBindImageMemorySwapchainInfoKHR *) s;
            struct anv_image *swapchain_image =
               anv_swapchain_get_image(swapchain_info->swapchain,
                                       swapchain_info->imageIndex);
            assert(swapchain_image);
            assert(image->aspects == swapchain_image->aspects);
            assert(mem == NULL);

            uint32_t aspect_bit;
            anv_foreach_image_aspect_bit(aspect_bit, image, aspects) {
               uint32_t plane =
                  anv_image_aspect_to_plane(image->aspects, 1UL << aspect_bit);
               struct anv_device_memory mem = {
                  .bo = swapchain_image->planes[plane].address.bo,
               };
               anv_image_bind_memory_plane(device, image, plane,
                                           &mem, bind_info->memoryOffset);
            }
            break;
         }
         default:
            anv_debug_ignored_stype(s->sType);
            break;
         }
      }

      /* VkBindImageMemorySwapchainInfoKHR requires memory to be
       * VK_NULL_HANDLE. In such case, just carry one with the next bind
       * item.
       */
      if (!mem)
         continue;

      uint32_t aspect_bit;
      anv_foreach_image_aspect_bit(aspect_bit, image, aspects) {
         uint32_t plane =
            anv_image_aspect_to_plane(image->aspects, 1UL << aspect_bit);
         anv_image_bind_memory_plane(device, image, plane,
                                     mem, bind_info->memoryOffset);
      }
   }

   return VK_SUCCESS;
}

void anv_GetImageSubresourceLayout(
    VkDevice                                    device,
    VkImage                                     _image,
    const VkImageSubresource*                   subresource,
    VkSubresourceLayout*                        layout)
{
   ANV_FROM_HANDLE(anv_image, image, _image);

   const struct anv_surface *surface;
   if (subresource->aspectMask == VK_IMAGE_ASPECT_PLANE_1_BIT &&
       image->drm_format_mod != DRM_FORMAT_MOD_INVALID &&
       isl_drm_modifier_has_aux(image->drm_format_mod)) {
      surface = &image->planes[0].aux_surface;
   } else {
      uint32_t plane = anv_image_aspect_to_plane(image->aspects,
                                                 subresource->aspectMask);
      surface = &image->planes[plane].surface;
   }

   assert(__builtin_popcount(subresource->aspectMask) == 1);

   layout->offset = surface->offset;
   layout->rowPitch = surface->isl.row_pitch_B;
   layout->depthPitch = isl_surf_get_array_pitch(&surface->isl);
   layout->arrayPitch = isl_surf_get_array_pitch(&surface->isl);

   if (subresource->mipLevel > 0 || subresource->arrayLayer > 0) {
      assert(surface->isl.tiling == ISL_TILING_LINEAR);

      uint32_t offset_B;
      isl_surf_get_image_offset_B_tile_sa(&surface->isl,
                                          subresource->mipLevel,
                                          subresource->arrayLayer,
                                          0 /* logical_z_offset_px */,
                                          &offset_B, NULL, NULL);
      layout->offset += offset_B;
      layout->size = layout->rowPitch * anv_minify(image->extent.height,
                                                   subresource->mipLevel);
   } else {
      layout->size = surface->isl.size_B;
   }
}

VkResult anv_GetImageDrmFormatModifierPropertiesEXT(
    VkDevice                                    device,
    VkImage                                     _image,
    VkImageDrmFormatModifierPropertiesEXT*      pProperties)
{
   ANV_FROM_HANDLE(anv_image, image, _image);

   assert(pProperties->sType ==
          VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT);

   pProperties->drmFormatModifier = image->drm_format_mod;

   return VK_SUCCESS;
}

static VkImageUsageFlags
vk_image_layout_to_usage_flags(VkImageLayout layout,
                               VkImageAspectFlagBits aspect)
{
   assert(util_bitcount(aspect) == 1);

   switch (layout) {
   case VK_IMAGE_LAYOUT_UNDEFINED:
   case VK_IMAGE_LAYOUT_PREINITIALIZED:
      return 0u;

   case VK_IMAGE_LAYOUT_GENERAL:
      return ~0u;

   case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      assert(aspect & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV);
      return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      assert(aspect & (VK_IMAGE_ASPECT_DEPTH_BIT |
                       VK_IMAGE_ASPECT_STENCIL_BIT));
      return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

   case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
      assert(aspect & VK_IMAGE_ASPECT_DEPTH_BIT);
      return vk_image_layout_to_usage_flags(
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect);

   case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
      assert(aspect & VK_IMAGE_ASPECT_STENCIL_BIT);
      return vk_image_layout_to_usage_flags(
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect);

   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
      assert(aspect & (VK_IMAGE_ASPECT_DEPTH_BIT |
                       VK_IMAGE_ASPECT_STENCIL_BIT));
      return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
             VK_IMAGE_USAGE_SAMPLED_BIT |
             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

   case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
      assert(aspect & VK_IMAGE_ASPECT_DEPTH_BIT);
      return vk_image_layout_to_usage_flags(
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, aspect);

   case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
      assert(aspect & VK_IMAGE_ASPECT_STENCIL_BIT);
      return vk_image_layout_to_usage_flags(
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, aspect);

   case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      return VK_IMAGE_USAGE_SAMPLED_BIT |
             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

   case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

   case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      return VK_IMAGE_USAGE_TRANSFER_DST_BIT;

   case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
      if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
         return vk_image_layout_to_usage_flags(
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, aspect);
      } else if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
         return vk_image_layout_to_usage_flags(
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect);
      } else {
         assert(!"Must be a depth/stencil aspect");
         return 0;
      }

   case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
      if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
         return vk_image_layout_to_usage_flags(
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect);
      } else if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
         return vk_image_layout_to_usage_flags(
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, aspect);
      } else {
         assert(!"Must be a depth/stencil aspect");
         return 0;
      }

   case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      assert(aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      /* This needs to be handled specially by the caller */
      return 0;

   case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:
      assert(aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      return vk_image_layout_to_usage_flags(VK_IMAGE_LAYOUT_GENERAL, aspect);

   case VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV:
      assert(aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      return VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV;

   case VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT:
      assert(aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      return VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT;

   case VK_IMAGE_LAYOUT_MAX_ENUM:
      unreachable("Invalid image layout.");
   }

   unreachable("Invalid image layout.");
}

static bool
vk_image_layout_is_read_only(VkImageLayout layout,
                             VkImageAspectFlagBits aspect)
{
   assert(util_bitcount(aspect) == 1);

   switch (layout) {
   case VK_IMAGE_LAYOUT_UNDEFINED:
   case VK_IMAGE_LAYOUT_PREINITIALIZED:
      return true; /* These are only used for layout transitions */

   case VK_IMAGE_LAYOUT_GENERAL:
   case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
   case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
   case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:
   case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
   case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
      return false;

   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
   case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
   case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
   case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
   case VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV:
   case VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT:
   case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
   case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
      return true;

   case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
      return aspect == VK_IMAGE_ASPECT_DEPTH_BIT;

   case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
      return aspect == VK_IMAGE_ASPECT_STENCIL_BIT;

   case VK_IMAGE_LAYOUT_MAX_ENUM:
      unreachable("Invalid image layout.");
   }

   unreachable("Invalid image layout.");
}

/**
 * This function returns the assumed isl_aux_state for a given VkImageLayout.
 * Because Vulkan image layouts don't map directly to isl_aux_state enums, the
 * returned enum is the assumed worst case.
 *
 * @param devinfo The device information of the Intel GPU.
 * @param image The image that may contain a collection of buffers.
 * @param aspect The aspect of the image to be accessed.
 * @param layout The current layout of the image aspect(s).
 *
 * @return The primary buffer that should be used for the given layout.
 */
enum isl_aux_state
anv_layout_to_aux_state(const struct gen_device_info * const devinfo,
                        const struct anv_image * const image,
                        const VkImageAspectFlagBits aspect,
                        const VkImageLayout layout)
{
   /* Validate the inputs. */

   /* The devinfo is needed as the optimal buffer varies across generations. */
   assert(devinfo != NULL);

   /* The layout of a NULL image is not properly defined. */
   assert(image != NULL);

   /* The aspect must be exactly one of the image aspects. */
   assert(util_bitcount(aspect) == 1 && (aspect & image->aspects));

   /* Determine the optimal buffer. */

   uint32_t plane = anv_image_aspect_to_plane(image->aspects, aspect);

   /* If we don't have an aux buffer then aux state makes no sense */
   const enum isl_aux_usage aux_usage = image->planes[plane].aux_usage;
   assert(aux_usage != ISL_AUX_USAGE_NONE);

   /* All images that use an auxiliary surface are required to be tiled. */
   assert(image->planes[plane].surface.isl.tiling != ISL_TILING_LINEAR);

   /* Stencil has no aux */
   assert(aspect != VK_IMAGE_ASPECT_STENCIL_BIT);

   /* Handle a few special cases */
   switch (layout) {
   /* Invalid layouts */
   case VK_IMAGE_LAYOUT_MAX_ENUM:
      unreachable("Invalid image layout.");

   /* Undefined layouts
    *
    * The pre-initialized layout is equivalent to the undefined layout for
    * optimally-tiled images.  We can only do color compression (CCS or HiZ)
    * on tiled images.
    */
   case VK_IMAGE_LAYOUT_UNDEFINED:
   case VK_IMAGE_LAYOUT_PREINITIALIZED:
      return ISL_AUX_STATE_AUX_INVALID;

   case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: {
      assert(image->aspects == VK_IMAGE_ASPECT_COLOR_BIT);

      enum isl_aux_state aux_state =
         isl_drm_modifier_get_default_aux_state(image->drm_format_mod);

      switch (aux_state) {
      default:
         assert(!"unexpected isl_aux_state");
      case ISL_AUX_STATE_AUX_INVALID:
         /* The modifier does not support compression. But, if we arrived
          * here, then we have enabled compression on it anyway, in which case
          * we must resolve the aux surface before we release ownership to the
          * presentation engine (because, having no modifier, the presentation
          * engine will not be aware of the aux surface). The presentation
          * engine will not access the aux surface (because it is unware of
          * it), and so the aux surface will still be resolved when we
          * re-acquire ownership.
          *
          * Therefore, at ownership transfers in either direction, there does
          * exist an aux surface despite the lack of modifier and its state is
          * pass-through.
          */
         return ISL_AUX_STATE_PASS_THROUGH;
      case ISL_AUX_STATE_COMPRESSED_NO_CLEAR:
         return ISL_AUX_STATE_COMPRESSED_NO_CLEAR;
      }
   }

   default:
      break;
   }

   const bool read_only = vk_image_layout_is_read_only(layout, aspect);

   const VkImageUsageFlags image_aspect_usage =
      aspect == VK_IMAGE_ASPECT_STENCIL_BIT ? image->stencil_usage :
                                              image->usage;
   const VkImageUsageFlags usage =
      vk_image_layout_to_usage_flags(layout, aspect) & image_aspect_usage;

   bool aux_supported = true;

   if ((usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) && !read_only) {
      /* This image could be used as both an input attachment and a render
       * target (depth, stencil, or color) at the same time and this can cause
       * corruption.
       *
       * We currently only disable aux in this way for depth even though we
       * disable it for color in GL.
       *
       * TODO: Should we be disabling this in more cases?
       */
      if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
         aux_supported = false;
   }

   if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
      aux_supported = false;

   if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
      switch (aux_usage) {
      case ISL_AUX_USAGE_HIZ:
         if (!anv_can_sample_with_hiz(devinfo, image))
            aux_supported = false;
         break;

      case ISL_AUX_USAGE_HIZ_CCS:
         aux_supported = false;
         break;

      case ISL_AUX_USAGE_HIZ_CCS_WT:
         break;

      case ISL_AUX_USAGE_CCS_D:
         aux_supported = false;
         break;

      case ISL_AUX_USAGE_CCS_E:
      case ISL_AUX_USAGE_MCS:
         break;

      default:
         unreachable("Unsupported aux usage");
      }
   }

   switch (aux_usage) {
   case ISL_AUX_USAGE_HIZ:
   case ISL_AUX_USAGE_HIZ_CCS:
   case ISL_AUX_USAGE_HIZ_CCS_WT:
      if (aux_supported) {
         return ISL_AUX_STATE_COMPRESSED_CLEAR;
      } else if (read_only) {
         return ISL_AUX_STATE_RESOLVED;
      } else {
         return ISL_AUX_STATE_AUX_INVALID;
      }

   case ISL_AUX_USAGE_CCS_D:
      /* We only support clear in exactly one state */
      if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
         assert(aux_supported);
         return ISL_AUX_STATE_PARTIAL_CLEAR;
      } else {
         return ISL_AUX_STATE_PASS_THROUGH;
      }

   case ISL_AUX_USAGE_CCS_E:
   case ISL_AUX_USAGE_MCS:
      if (aux_supported) {
         return ISL_AUX_STATE_COMPRESSED_CLEAR;
      } else {
         return ISL_AUX_STATE_PASS_THROUGH;
      }

   default:
      unreachable("Unsupported aux usage");
   }
}

/**
 * This function determines the optimal buffer to use for a given
 * VkImageLayout and other pieces of information needed to make that
 * determination. This does not determine the optimal buffer to use
 * during a resolve operation.
 *
 * @param devinfo The device information of the Intel GPU.
 * @param image The image that may contain a collection of buffers.
 * @param aspect The aspect of the image to be accessed.
 * @param usage The usage which describes how the image will be accessed.
 * @param layout The current layout of the image aspect(s).
 *
 * @return The primary buffer that should be used for the given layout.
 */
enum isl_aux_usage
anv_layout_to_aux_usage(const struct gen_device_info * const devinfo,
                        const struct anv_image * const image,
                        const VkImageAspectFlagBits aspect,
                        const VkImageUsageFlagBits usage,
                        const VkImageLayout layout)
{
   uint32_t plane = anv_image_aspect_to_plane(image->aspects, aspect);

   /* If there is no auxiliary surface allocated, we must use the one and only
    * main buffer.
    */
   if (image->planes[plane].aux_usage == ISL_AUX_USAGE_NONE)
      return ISL_AUX_USAGE_NONE;

   enum isl_aux_state aux_state =
      anv_layout_to_aux_state(devinfo, image, aspect, layout);

   switch (aux_state) {
   case ISL_AUX_STATE_CLEAR:
      unreachable("We never use this state");

   case ISL_AUX_STATE_PARTIAL_CLEAR:
      assert(image->aspects & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV);
      assert(image->planes[plane].aux_usage == ISL_AUX_USAGE_CCS_D);
      assert(image->samples == 1);
      return ISL_AUX_USAGE_CCS_D;

   case ISL_AUX_STATE_COMPRESSED_CLEAR:
   case ISL_AUX_STATE_COMPRESSED_NO_CLEAR:
      return image->planes[plane].aux_usage;

   case ISL_AUX_STATE_RESOLVED:
      /* We can only use RESOLVED in read-only layouts because any write will
       * either land us in AUX_INVALID or COMPRESSED_NO_CLEAR.  We can do
       * writes in PASS_THROUGH without destroying it so that is allowed.
       */
      assert(vk_image_layout_is_read_only(layout, aspect));
      assert(util_is_power_of_two_or_zero(usage));
      if (usage == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
         /* If we have valid HiZ data and are using the image as a read-only
          * depth/stencil attachment, we should enable HiZ so that we can get
          * faster depth testing.
          */
         return image->planes[plane].aux_usage;
      } else {
         return ISL_AUX_USAGE_NONE;
      }

   case ISL_AUX_STATE_PASS_THROUGH:
   case ISL_AUX_STATE_AUX_INVALID:
      return ISL_AUX_USAGE_NONE;
   }

   unreachable("Invalid isl_aux_state");
}

/**
 * This function returns the level of unresolved fast-clear support of the
 * given image in the given VkImageLayout.
 *
 * @param devinfo The device information of the Intel GPU.
 * @param image The image that may contain a collection of buffers.
 * @param aspect The aspect of the image to be accessed.
 * @param usage The usage which describes how the image will be accessed.
 * @param layout The current layout of the image aspect(s).
 */
enum anv_fast_clear_type
anv_layout_to_fast_clear_type(const struct gen_device_info * const devinfo,
                              const struct anv_image * const image,
                              const VkImageAspectFlagBits aspect,
                              const VkImageLayout layout)
{
   if (INTEL_DEBUG & DEBUG_NO_FAST_CLEAR)
      return ANV_FAST_CLEAR_NONE;

   uint32_t plane = anv_image_aspect_to_plane(image->aspects, aspect);

   /* If there is no auxiliary surface allocated, there are no fast-clears */
   if (image->planes[plane].aux_usage == ISL_AUX_USAGE_NONE)
      return ANV_FAST_CLEAR_NONE;

   /* We don't support MSAA fast-clears on Ivybridge or Bay Trail because they
    * lack the MI ALU which we need to determine the predicates.
    */
   if (devinfo->gen == 7 && !devinfo->is_haswell && image->samples > 1)
      return ANV_FAST_CLEAR_NONE;

   enum isl_aux_state aux_state =
      anv_layout_to_aux_state(devinfo, image, aspect, layout);

   switch (aux_state) {
   case ISL_AUX_STATE_CLEAR:
      unreachable("We never use this state");

   case ISL_AUX_STATE_PARTIAL_CLEAR:
   case ISL_AUX_STATE_COMPRESSED_CLEAR:
      if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
         return ANV_FAST_CLEAR_DEFAULT_VALUE;
      } else if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
         /* When we're in a render pass we have the clear color data from the
          * VkRenderPassBeginInfo and we can use arbitrary clear colors.  They
          * must get partially resolved before we leave the render pass.
          */
         return ANV_FAST_CLEAR_ANY;
      } else if (image->planes[plane].aux_usage == ISL_AUX_USAGE_MCS ||
                 image->planes[plane].aux_usage == ISL_AUX_USAGE_CCS_E) {
         if (devinfo->gen >= 11) {
            /* On ICL and later, the sampler hardware uses a copy of the clear
             * value that is encoded as a pixel value.  Therefore, we can use
             * any clear color we like for sampling.
             */
            return ANV_FAST_CLEAR_ANY;
         } else {
            /* If the image has MCS or CCS_E enabled all the time then we can
             * use fast-clear as long as the clear color is the default value
             * of zero since this is the default value we program into every
             * surface state used for texturing.
             */
            return ANV_FAST_CLEAR_DEFAULT_VALUE;
         }
      } else {
         return ANV_FAST_CLEAR_NONE;
      }

   case ISL_AUX_STATE_COMPRESSED_NO_CLEAR:
   case ISL_AUX_STATE_RESOLVED:
   case ISL_AUX_STATE_PASS_THROUGH:
   case ISL_AUX_STATE_AUX_INVALID:
      return ANV_FAST_CLEAR_NONE;
   }

   unreachable("Invalid isl_aux_state");
}


static struct anv_state
alloc_surface_state(struct anv_device *device)
{
   return anv_state_pool_alloc(&device->surface_state_pool, 64, 64);
}

static enum isl_channel_select
remap_swizzle(VkComponentSwizzle swizzle, VkComponentSwizzle component,
              struct isl_swizzle format_swizzle)
{
   if (swizzle == VK_COMPONENT_SWIZZLE_IDENTITY)
      swizzle = component;

   switch (swizzle) {
   case VK_COMPONENT_SWIZZLE_ZERO:  return ISL_CHANNEL_SELECT_ZERO;
   case VK_COMPONENT_SWIZZLE_ONE:   return ISL_CHANNEL_SELECT_ONE;
   case VK_COMPONENT_SWIZZLE_R:     return format_swizzle.r;
   case VK_COMPONENT_SWIZZLE_G:     return format_swizzle.g;
   case VK_COMPONENT_SWIZZLE_B:     return format_swizzle.b;
   case VK_COMPONENT_SWIZZLE_A:     return format_swizzle.a;
   default:
      unreachable("Invalid swizzle");
   }
}

void
anv_image_fill_surface_state(struct anv_device *device,
                             const struct anv_image *image,
                             VkImageAspectFlagBits aspect,
                             const struct isl_view *view_in,
                             isl_surf_usage_flags_t view_usage,
                             enum isl_aux_usage aux_usage,
                             const union isl_color_value *clear_color,
                             enum anv_image_view_state_flags flags,
                             struct anv_surface_state *state_inout,
                             struct brw_image_param *image_param_out)
{
   uint32_t plane = anv_image_aspect_to_plane(image->aspects, aspect);

   const struct anv_surface *surface = &image->planes[plane].surface,
      *aux_surface = &image->planes[plane].aux_surface;

   struct isl_view view = *view_in;
   view.usage |= view_usage;

   /* For texturing with VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL from a
    * compressed surface with a shadow surface, we use the shadow instead of
    * the primary surface.  The shadow surface will be tiled, unlike the main
    * surface, so it should get significantly better performance.
    */
   if (image->planes[plane].shadow_surface.isl.size_B > 0 &&
       isl_format_is_compressed(view.format) &&
       (flags & ANV_IMAGE_VIEW_STATE_TEXTURE_OPTIMAL)) {
      assert(isl_format_is_compressed(surface->isl.format));
      assert(surface->isl.tiling == ISL_TILING_LINEAR);
      assert(image->planes[plane].shadow_surface.isl.tiling != ISL_TILING_LINEAR);
      surface = &image->planes[plane].shadow_surface;
   }

   /* For texturing from stencil on gen7, we have to sample from a shadow
    * surface because we don't support W-tiling in the sampler.
    */
   if (image->planes[plane].shadow_surface.isl.size_B > 0 &&
       aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
      assert(device->info.gen == 7);
      assert(view_usage & ISL_SURF_USAGE_TEXTURE_BIT);
      surface = &image->planes[plane].shadow_surface;
   }

   if (view_usage == ISL_SURF_USAGE_RENDER_TARGET_BIT)
      view.swizzle = anv_swizzle_for_render(view.swizzle);

   /* On Ivy Bridge and Bay Trail we do the swizzle in the shader */
   if (device->info.gen == 7 && !device->info.is_haswell)
      view.swizzle = ISL_SWIZZLE_IDENTITY;

   /* If this is a HiZ buffer we can sample from with a programmable clear
    * value (SKL+), define the clear value to the optimal constant.
    */
   union isl_color_value default_clear_color = { .u32 = { 0, } };
   if (device->info.gen >= 9 && aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
      default_clear_color.f32[0] = ANV_HZ_FC_VAL;
   if (!clear_color)
      clear_color = &default_clear_color;

   const struct anv_address address =
      anv_address_add(image->planes[plane].address, surface->offset);

   if (view_usage == ISL_SURF_USAGE_STORAGE_BIT &&
       !(flags & ANV_IMAGE_VIEW_STATE_STORAGE_WRITE_ONLY) &&
       !isl_has_matching_typed_storage_image_format(&device->info,
                                                    view.format)) {
      /* In this case, we are a writeable storage buffer which needs to be
       * lowered to linear. All tiling and offset calculations will be done in
       * the shader.
       */
      assert(aux_usage == ISL_AUX_USAGE_NONE);
      isl_buffer_fill_state(&device->isl_dev, state_inout->state.map,
                            .address = anv_address_physical(address),
                            .size_B = surface->isl.size_B,
                            .format = ISL_FORMAT_RAW,
                            .swizzle = ISL_SWIZZLE_IDENTITY,
                            .stride_B = 1,
                            .mocs = anv_mocs_for_bo(device, address.bo));
      state_inout->address = address,
      state_inout->aux_address = ANV_NULL_ADDRESS;
      state_inout->clear_address = ANV_NULL_ADDRESS;
   } else {
      if (view_usage == ISL_SURF_USAGE_STORAGE_BIT &&
          !(flags & ANV_IMAGE_VIEW_STATE_STORAGE_WRITE_ONLY)) {
         /* Typed surface reads support a very limited subset of the shader
          * image formats.  Translate it into the closest format the hardware
          * supports.
          */
         assert(aux_usage == ISL_AUX_USAGE_NONE);
         view.format = isl_lower_storage_image_format(&device->info,
                                                      view.format);
      }

      const struct isl_surf *isl_surf = &surface->isl;

      struct isl_surf tmp_surf;
      uint32_t offset_B = 0, tile_x_sa = 0, tile_y_sa = 0;
      if (isl_format_is_compressed(surface->isl.format) &&
          !isl_format_is_compressed(view.format)) {
         /* We're creating an uncompressed view of a compressed surface.  This
          * is allowed but only for a single level/layer.
          */
         assert(surface->isl.samples == 1);
         assert(view.levels == 1);
         assert(view.array_len == 1);

         isl_surf_get_image_surf(&device->isl_dev, isl_surf,
                                 view.base_level,
                                 surface->isl.dim == ISL_SURF_DIM_3D ?
                                    0 : view.base_array_layer,
                                 surface->isl.dim == ISL_SURF_DIM_3D ?
                                    view.base_array_layer : 0,
                                 &tmp_surf,
                                 &offset_B, &tile_x_sa, &tile_y_sa);

         /* The newly created image represents the one subimage we're
          * referencing with this view so it only has one array slice and
          * miplevel.
          */
         view.base_array_layer = 0;
         view.base_level = 0;

         /* We're making an uncompressed view here.  The image dimensions need
          * to be scaled down by the block size.
          */
         const struct isl_format_layout *fmtl =
            isl_format_get_layout(surface->isl.format);
         tmp_surf.logical_level0_px =
            isl_surf_get_logical_level0_el(&tmp_surf);
         tmp_surf.phys_level0_sa = isl_surf_get_phys_level0_el(&tmp_surf);
         tmp_surf.format = view.format;
         tile_x_sa /= fmtl->bw;
         tile_y_sa /= fmtl->bh;

         isl_surf = &tmp_surf;

         if (device->info.gen <= 8) {
            assert(surface->isl.tiling == ISL_TILING_LINEAR);
            assert(tile_x_sa == 0);
            assert(tile_y_sa == 0);
         }
      }

      state_inout->address = anv_address_add(address, offset_B);

      struct anv_address aux_address = ANV_NULL_ADDRESS;
      if (aux_usage != ISL_AUX_USAGE_NONE) {
         aux_address = anv_address_add(image->planes[plane].address,
                                       aux_surface->offset);
      }
      state_inout->aux_address = aux_address;

      struct anv_address clear_address = ANV_NULL_ADDRESS;
      if (device->info.gen >= 10 && aux_usage != ISL_AUX_USAGE_NONE) {
         if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
            clear_address = (struct anv_address) {
               .bo = device->hiz_clear_bo,
               .offset = 0,
            };
         } else {
            clear_address = anv_image_get_clear_color_addr(device, image, aspect);
         }
      }
      state_inout->clear_address = clear_address;

      isl_surf_fill_state(&device->isl_dev, state_inout->state.map,
                          .surf = isl_surf,
                          .view = &view,
                          .address = anv_address_physical(state_inout->address),
                          .clear_color = *clear_color,
                          .aux_surf = &aux_surface->isl,
                          .aux_usage = aux_usage,
                          .aux_address = anv_address_physical(aux_address),
                          .clear_address = anv_address_physical(clear_address),
                          .use_clear_address = !anv_address_is_null(clear_address),
                          .mocs = anv_mocs_for_bo(device,
                                                  state_inout->address.bo),
                          .x_offset_sa = tile_x_sa,
                          .y_offset_sa = tile_y_sa);

      /* With the exception of gen8, the bottom 12 bits of the MCS base address
       * are used to store other information.  This should be ok, however,
       * because the surface buffer addresses are always 4K page aligned.
       */
      uint32_t *aux_addr_dw = state_inout->state.map +
         device->isl_dev.ss.aux_addr_offset;
      assert((aux_address.offset & 0xfff) == 0);
      state_inout->aux_address.offset |= *aux_addr_dw & 0xfff;

      if (device->info.gen >= 10 && clear_address.bo) {
         uint32_t *clear_addr_dw = state_inout->state.map +
                                   device->isl_dev.ss.clear_color_state_offset;
         assert((clear_address.offset & 0x3f) == 0);
         state_inout->clear_address.offset |= *clear_addr_dw & 0x3f;
      }
   }

   if (image_param_out) {
      assert(view_usage == ISL_SURF_USAGE_STORAGE_BIT);
      isl_surf_fill_image_param(&device->isl_dev, image_param_out,
                                &surface->isl, &view);
   }
}

static VkImageAspectFlags
remap_aspect_flags(VkImageAspectFlags view_aspects)
{
   if (view_aspects & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV) {
      if (util_bitcount(view_aspects) == 1)
         return VK_IMAGE_ASPECT_COLOR_BIT;

      VkImageAspectFlags color_aspects = 0;
      for (uint32_t i = 0; i < util_bitcount(view_aspects); i++)
         color_aspects |= VK_IMAGE_ASPECT_PLANE_0_BIT << i;
      return color_aspects;
   }
   /* No special remapping needed for depth & stencil aspects. */
   return view_aspects;
}

static uint32_t
anv_image_aspect_get_planes(VkImageAspectFlags aspect_mask)
{
   uint32_t planes = 0;

   if (aspect_mask & (VK_IMAGE_ASPECT_COLOR_BIT |
                      VK_IMAGE_ASPECT_DEPTH_BIT |
                      VK_IMAGE_ASPECT_STENCIL_BIT |
                      VK_IMAGE_ASPECT_PLANE_0_BIT))
      planes++;
   if (aspect_mask & VK_IMAGE_ASPECT_PLANE_1_BIT)
      planes++;
   if (aspect_mask & VK_IMAGE_ASPECT_PLANE_2_BIT)
      planes++;

   if ((aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 &&
       (aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
      planes++;

   return planes;
}

VkResult
anv_CreateImageView(VkDevice _device,
                    const VkImageViewCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkImageView *pView)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, image, pCreateInfo->image);
   struct anv_image_view *iview;

   iview = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*iview), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (iview == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &iview->base, VK_OBJECT_TYPE_IMAGE_VIEW);

   const VkImageSubresourceRange *range = &pCreateInfo->subresourceRange;

   assert(range->layerCount > 0);
   assert(range->baseMipLevel < image->levels);

   /* Check if a conversion info was passed. */
   const struct anv_format *conv_format = NULL;
   const VkSamplerYcbcrConversionInfo *conv_info =
      vk_find_struct_const(pCreateInfo->pNext, SAMPLER_YCBCR_CONVERSION_INFO);

   /* If image has an external format, the pNext chain must contain an instance of
    * VKSamplerYcbcrConversionInfo with a conversion object created with the same
    * external format as image."
    */
   assert(!image->external_format || conv_info);

   if (conv_info) {
      ANV_FROM_HANDLE(anv_ycbcr_conversion, conversion, conv_info->conversion);
      conv_format = conversion->format;
   }

   VkImageUsageFlags image_usage = image->usage;
   if (range->aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT |
                            VK_IMAGE_ASPECT_STENCIL_BIT)) {
      assert(!(range->aspectMask & VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV));
      /* From the Vulkan 1.2.131 spec:
       *
       *    "If the image was has a depth-stencil format and was created with
       *    a VkImageStencilUsageCreateInfo structure included in the pNext
       *    chain of VkImageCreateInfo, the usage is calculated based on the
       *    subresource.aspectMask provided:
       *
       *     - If aspectMask includes only VK_IMAGE_ASPECT_STENCIL_BIT, the
       *       implicit usage is equal to
       *       VkImageStencilUsageCreateInfo::stencilUsage.
       *
       *     - If aspectMask includes only VK_IMAGE_ASPECT_DEPTH_BIT, the
       *       implicit usage is equal to VkImageCreateInfo::usage.
       *
       *     - If both aspects are included in aspectMask, the implicit usage
       *       is equal to the intersection of VkImageCreateInfo::usage and
       *       VkImageStencilUsageCreateInfo::stencilUsage.
       */
      if (range->aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT) {
         image_usage = image->stencil_usage;
      } else if (range->aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT) {
         image_usage = image->usage;
      } else {
         assert(range->aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT |
                                      VK_IMAGE_ASPECT_STENCIL_BIT));
         image_usage = image->usage & image->stencil_usage;
      }
   }

   const VkImageViewUsageCreateInfo *usage_info =
      vk_find_struct_const(pCreateInfo, IMAGE_VIEW_USAGE_CREATE_INFO);
   VkImageUsageFlags view_usage = usage_info ? usage_info->usage : image_usage;

   /* View usage should be a subset of image usage */
   assert((view_usage & ~image_usage) == 0);
   assert(view_usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_STORAGE_BIT |
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));

   switch (image->type) {
   default:
      unreachable("bad VkImageType");
   case VK_IMAGE_TYPE_1D:
   case VK_IMAGE_TYPE_2D:
      assert(range->baseArrayLayer + anv_get_layerCount(image, range) - 1 <= image->array_size);
      break;
   case VK_IMAGE_TYPE_3D:
      assert(range->baseArrayLayer + anv_get_layerCount(image, range) - 1
             <= anv_minify(image->extent.depth, range->baseMipLevel));
      break;
   }

   /* First expand aspects to the image's ones (for example
    * VK_IMAGE_ASPECT_COLOR_BIT will be converted to
    * VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT |
    * VK_IMAGE_ASPECT_PLANE_2_BIT for an image of format
    * VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM.
    */
   VkImageAspectFlags expanded_aspects =
      anv_image_expand_aspects(image, range->aspectMask);

   iview->image = image;

   /* Remap the expanded aspects for the image view. For example if only
    * VK_IMAGE_ASPECT_PLANE_1_BIT was given in range->aspectMask, we will
    * convert it to VK_IMAGE_ASPECT_COLOR_BIT since from the point of view of
    * the image view, it only has a single plane.
    */
   iview->aspect_mask = remap_aspect_flags(expanded_aspects);
   iview->n_planes = anv_image_aspect_get_planes(iview->aspect_mask);
   iview->vk_format = pCreateInfo->format;

   /* "If image has an external format, format must be VK_FORMAT_UNDEFINED." */
   assert(!image->external_format || pCreateInfo->format == VK_FORMAT_UNDEFINED);

   /* Format is undefined, this can happen when using external formats. Set
    * view format from the passed conversion info.
    */
   if (iview->vk_format == VK_FORMAT_UNDEFINED && conv_format)
      iview->vk_format = conv_format->vk_format;

   iview->extent = (VkExtent3D) {
      .width  = anv_minify(image->extent.width , range->baseMipLevel),
      .height = anv_minify(image->extent.height, range->baseMipLevel),
      .depth  = anv_minify(image->extent.depth , range->baseMipLevel),
   };

   /* Now go through the underlying image selected planes (computed in
    * expanded_aspects) and map them to planes in the image view.
    */
   uint32_t iaspect_bit, vplane = 0;
   anv_foreach_image_aspect_bit(iaspect_bit, image, expanded_aspects) {
      uint32_t iplane =
         anv_image_aspect_to_plane(image->aspects, 1UL << iaspect_bit);
      VkImageAspectFlags vplane_aspect =
         anv_plane_to_aspect(iview->aspect_mask, vplane);
      struct anv_format_plane format =
         anv_get_format_plane(&device->info, iview->vk_format,
                              vplane_aspect, image->tiling);

      iview->planes[vplane].image_plane = iplane;

      iview->planes[vplane].isl = (struct isl_view) {
         .format = format.isl_format,
         .base_level = range->baseMipLevel,
         .levels = anv_get_levelCount(image, range),
         .base_array_layer = range->baseArrayLayer,
         .array_len = anv_get_layerCount(image, range),
         .swizzle = {
            .r = remap_swizzle(pCreateInfo->components.r,
                               VK_COMPONENT_SWIZZLE_R, format.swizzle),
            .g = remap_swizzle(pCreateInfo->components.g,
                               VK_COMPONENT_SWIZZLE_G, format.swizzle),
            .b = remap_swizzle(pCreateInfo->components.b,
                               VK_COMPONENT_SWIZZLE_B, format.swizzle),
            .a = remap_swizzle(pCreateInfo->components.a,
                               VK_COMPONENT_SWIZZLE_A, format.swizzle),
         },
      };

      if (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_3D) {
         iview->planes[vplane].isl.base_array_layer = 0;
         iview->planes[vplane].isl.array_len = iview->extent.depth;
      }

      if (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_CUBE ||
          pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
         iview->planes[vplane].isl.usage = ISL_SURF_USAGE_CUBE_BIT;
      } else {
         iview->planes[vplane].isl.usage = 0;
      }

      if (view_usage & VK_IMAGE_USAGE_SAMPLED_BIT ||
          (view_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT &&
           !(iview->aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT))) {
         iview->planes[vplane].optimal_sampler_surface_state.state = alloc_surface_state(device);
         iview->planes[vplane].general_sampler_surface_state.state = alloc_surface_state(device);

         enum isl_aux_usage general_aux_usage =
            anv_layout_to_aux_usage(&device->info, image, 1UL << iaspect_bit,
                                    VK_IMAGE_USAGE_SAMPLED_BIT,
                                    VK_IMAGE_LAYOUT_GENERAL);
         enum isl_aux_usage optimal_aux_usage =
            anv_layout_to_aux_usage(&device->info, image, 1UL << iaspect_bit,
                                    VK_IMAGE_USAGE_SAMPLED_BIT,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

         anv_image_fill_surface_state(device, image, 1ULL << iaspect_bit,
                                      &iview->planes[vplane].isl,
                                      ISL_SURF_USAGE_TEXTURE_BIT,
                                      optimal_aux_usage, NULL,
                                      ANV_IMAGE_VIEW_STATE_TEXTURE_OPTIMAL,
                                      &iview->planes[vplane].optimal_sampler_surface_state,
                                      NULL);

         anv_image_fill_surface_state(device, image, 1ULL << iaspect_bit,
                                      &iview->planes[vplane].isl,
                                      ISL_SURF_USAGE_TEXTURE_BIT,
                                      general_aux_usage, NULL,
                                      0,
                                      &iview->planes[vplane].general_sampler_surface_state,
                                      NULL);
      }

      /* NOTE: This one needs to go last since it may stomp isl_view.format */
      if (view_usage & VK_IMAGE_USAGE_STORAGE_BIT) {
         iview->planes[vplane].storage_surface_state.state = alloc_surface_state(device);
         iview->planes[vplane].writeonly_storage_surface_state.state = alloc_surface_state(device);

         anv_image_fill_surface_state(device, image, 1ULL << iaspect_bit,
                                      &iview->planes[vplane].isl,
                                      ISL_SURF_USAGE_STORAGE_BIT,
                                      ISL_AUX_USAGE_NONE, NULL,
                                      0,
                                      &iview->planes[vplane].storage_surface_state,
                                      &iview->planes[vplane].storage_image_param);

         anv_image_fill_surface_state(device, image, 1ULL << iaspect_bit,
                                      &iview->planes[vplane].isl,
                                      ISL_SURF_USAGE_STORAGE_BIT,
                                      ISL_AUX_USAGE_NONE, NULL,
                                      ANV_IMAGE_VIEW_STATE_STORAGE_WRITE_ONLY,
                                      &iview->planes[vplane].writeonly_storage_surface_state,
                                      NULL);
      }

      vplane++;
   }

   *pView = anv_image_view_to_handle(iview);

   return VK_SUCCESS;
}

void
anv_DestroyImageView(VkDevice _device, VkImageView _iview,
                     const VkAllocationCallbacks *pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image_view, iview, _iview);

   if (!iview)
      return;

   for (uint32_t plane = 0; plane < iview->n_planes; plane++) {
      if (iview->planes[plane].optimal_sampler_surface_state.state.alloc_size > 0) {
         anv_state_pool_free(&device->surface_state_pool,
                             iview->planes[plane].optimal_sampler_surface_state.state);
      }

      if (iview->planes[plane].general_sampler_surface_state.state.alloc_size > 0) {
         anv_state_pool_free(&device->surface_state_pool,
                             iview->planes[plane].general_sampler_surface_state.state);
      }

      if (iview->planes[plane].storage_surface_state.state.alloc_size > 0) {
         anv_state_pool_free(&device->surface_state_pool,
                             iview->planes[plane].storage_surface_state.state);
      }

      if (iview->planes[plane].writeonly_storage_surface_state.state.alloc_size > 0) {
         anv_state_pool_free(&device->surface_state_pool,
                             iview->planes[plane].writeonly_storage_surface_state.state);
      }
   }

   vk_object_base_finish(&iview->base);
   vk_free2(&device->vk.alloc, pAllocator, iview);
}


VkResult
anv_CreateBufferView(VkDevice _device,
                     const VkBufferViewCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkBufferView *pView)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_buffer, buffer, pCreateInfo->buffer);
   struct anv_buffer_view *view;

   view = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*view), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!view)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   /* TODO: Handle the format swizzle? */

   vk_object_base_init(&device->vk, &view->base, VK_OBJECT_TYPE_BUFFER_VIEW);
   view->format = anv_get_isl_format(&device->info, pCreateInfo->format,
                                     VK_IMAGE_ASPECT_COLOR_BIT,
                                     VK_IMAGE_TILING_LINEAR);
   const uint32_t format_bs = isl_format_get_layout(view->format)->bpb / 8;
   view->range = anv_buffer_get_range(buffer, pCreateInfo->offset,
                                              pCreateInfo->range);
   view->range = align_down_npot_u32(view->range, format_bs);

   view->address = anv_address_add(buffer->address, pCreateInfo->offset);

   if (buffer->usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) {
      view->surface_state = alloc_surface_state(device);

      anv_fill_buffer_surface_state(device, view->surface_state,
                                    view->format,
                                    view->address, view->range, format_bs);
   } else {
      view->surface_state = (struct anv_state){ 0 };
   }

   if (buffer->usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) {
      view->storage_surface_state = alloc_surface_state(device);
      view->writeonly_storage_surface_state = alloc_surface_state(device);

      enum isl_format storage_format =
         isl_has_matching_typed_storage_image_format(&device->info,
                                                     view->format) ?
         isl_lower_storage_image_format(&device->info, view->format) :
         ISL_FORMAT_RAW;

      anv_fill_buffer_surface_state(device, view->storage_surface_state,
                                    storage_format,
                                    view->address, view->range,
                                    (storage_format == ISL_FORMAT_RAW ? 1 :
                                     isl_format_get_layout(storage_format)->bpb / 8));

      /* Write-only accesses should use the original format. */
      anv_fill_buffer_surface_state(device, view->writeonly_storage_surface_state,
                                    view->format,
                                    view->address, view->range,
                                    isl_format_get_layout(view->format)->bpb / 8);

      isl_buffer_fill_image_param(&device->isl_dev,
                                  &view->storage_image_param,
                                  view->format, view->range);
   } else {
      view->storage_surface_state = (struct anv_state){ 0 };
      view->writeonly_storage_surface_state = (struct anv_state){ 0 };
   }

   *pView = anv_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

void
anv_DestroyBufferView(VkDevice _device, VkBufferView bufferView,
                      const VkAllocationCallbacks *pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_buffer_view, view, bufferView);

   if (!view)
      return;

   if (view->surface_state.alloc_size > 0)
      anv_state_pool_free(&device->surface_state_pool,
                          view->surface_state);

   if (view->storage_surface_state.alloc_size > 0)
      anv_state_pool_free(&device->surface_state_pool,
                          view->storage_surface_state);

   if (view->writeonly_storage_surface_state.alloc_size > 0)
      anv_state_pool_free(&device->surface_state_pool,
                          view->writeonly_storage_surface_state);

   vk_object_base_finish(&view->base);
   vk_free2(&device->vk.alloc, pAllocator, view);
}
