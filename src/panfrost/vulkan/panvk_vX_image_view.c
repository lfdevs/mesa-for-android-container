/*
 * Copyright © 2025 Arm Ltd.
 * Copyright © 2021 Collabora Ltd.
 * Copyright © 2026 Google LLC
 *
 * Derived from tu_image.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "vk_format.h"
#include "vk_log.h"
#include "vk_ycbcr_conversion.h"

#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_image_view.h"
#include "panvk_priv_bo.h"

#include "pan_afbc.h"
#include "pan_texture.h"

#include "genxml/gen_macros.h"

static enum mali_texture_dimension
panvk_view_type_to_mali_tex_dim(VkImageViewType type)
{
   switch (type) {
   case VK_IMAGE_VIEW_TYPE_1D:
   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      return MALI_TEXTURE_DIMENSION_1D;
   case VK_IMAGE_VIEW_TYPE_2D:
   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
      return MALI_TEXTURE_DIMENSION_2D;
   case VK_IMAGE_VIEW_TYPE_3D:
      return MALI_TEXTURE_DIMENSION_3D;
   case VK_IMAGE_VIEW_TYPE_CUBE:
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
      return MALI_TEXTURE_DIMENSION_CUBE;
   default:
      UNREACHABLE("Invalid view type");
   }
}

static struct pan_image_view
panvk_storage_pview(const struct panvk_image_view *view,
                    const struct pan_image_view *pview)
{
   struct pan_image_view spview = *pview;

   /* A storage view only sees its Z-slice range. The range defaults to the
    * full view depth, so non-sliced views are left unchanged
    * (VK_EXT_image_sliced_view_of_3d).
    */
   if (spview.dim == MALI_TEXTURE_DIMENSION_3D) {
      spview.first_layer_or_z_slice = view->vk.storage.z_slice_offset;
      spview.last_layer_or_z_slice =
         view->vk.storage.z_slice_offset + view->vk.storage.z_slice_count - 1;
   }

   return spview;
}

static VkResult
prepare_tex_descs(struct panvk_image_view *view)
{
   /* Use a temporary pan_image_view so we can tweak it for texture
    * descriptor emission without changing the original definition.
    */
   struct pan_image_view pview = view->pview;
   struct panvk_image *image =
      container_of(view->vk.image, struct panvk_image, vk);
   struct panvk_device *dev = to_panvk_device(view->vk.base.device);
#if PAN_ARCH >= 9
   bool has_storage = (view->vk.usage & VK_IMAGE_USAGE_STORAGE_BIT) &&
                      !vk_format_is_compressed(view->vk.format);
#endif

   if (util_format_is_depth_or_stencil(view->pview.format)) {
      /* Vulkan wants R001, where the depth/stencil is stored in the red
       * component. Tweak the swizzle so we get what Vulkan wants.
       */
      static const unsigned char r001[4] = {
         PIPE_SWIZZLE_X,
         PIPE_SWIZZLE_0,
         PIPE_SWIZZLE_0,
         PIPE_SWIZZLE_1,
      };

      util_format_compose_swizzles(r001, view->pview.swizzle, pview.swizzle);
   }
#if PAN_ARCH == 7
   /* v7 requires AFBC reswizzle. */
   else if (!pan_format_is_yuv(view->pview.format) &&
            pan_afbc_supports_format(PAN_ARCH, view->pview.format))
      GENX(pan_texture_afbc_reswizzle)(&pview);
#endif

   /* If the view contains both stencil and depth, we need to keep only the
    * depth. We'll create another texture with only the stencil.
    */
   if (view->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
      /* View and image formats must match. */
      assert(view->vk.format == vk_format_depth_only(image->vk.format) ||
             view->vk.format == image->vk.format);
      pview.format = panvk_image_depth_only_pfmt(image);
   }

   const uint32_t tex_desc_count =
      panvk_image_get_tex_count(PAN_ARCH, view->vk.format);
   uint32_t tex_payload_size = GENX(pan_texture_estimate_payload_size)(&pview);

   struct panvk_pool_alloc_info alloc_info = {
      .alignment = GENX(pan_texture_get_payload_alignment)(&pview),
      .size = tex_payload_size * tex_desc_count,
   };

#if PAN_ARCH >= 9
   uint32_t storage_payload_size = 0;
   if (has_storage) {
      /* We'll need a second set of Texture Descriptors for storage use. */
      storage_payload_size = tex_payload_size * tex_desc_count;
      alloc_info.size += storage_payload_size;
   }
#endif

   view->mem = panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
   if (!panvk_priv_mem_check_alloc(view->mem))
      return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   panvk_priv_mem_write_array(view->mem, 0, uint8_t, alloc_info.size, cpu_ptr) {
      struct pan_ptr ptr = {
         .gpu = panvk_priv_mem_dev_addr(view->mem),
         .cpu = cpu_ptr,
      };

#if PAN_ARCH >= 9
      struct pan_ptr storage_ptr = ptr;
      if (has_storage) {
         uint32_t storage_payload_offset =
            alloc_info.size - storage_payload_size;
         storage_ptr = pan_ptr_offset(storage_ptr, storage_payload_offset);
      }
#endif

      if (tex_desc_count > 1) {
         memset(pview.planes, 0, sizeof(pview.planes));

         for (uint32_t plane = 0; plane < tex_desc_count; plane++) {
            VkFormat plane_format =
               vk_format_get_plane_format(view->vk.view_format, plane);

            /* We need a per-plane pview. */
            pview.planes[0] = view->pview.planes[plane];
            pview.format = vk_format_to_pipe_format(plane_format);

            GENX(pan_sampled_texture_emit)(&pview, &view->descs.tex[plane],
                                           &ptr);
#if PAN_ARCH >= 9
            if (has_storage) {
               struct pan_image_view spview = panvk_storage_pview(view, &pview);
               GENX(pan_storage_texture_emit)(
                  &spview, &view->descs.storage_tex[plane], &storage_ptr);
               storage_ptr = pan_ptr_offset(storage_ptr, tex_payload_size);
            }
#endif

            ptr = pan_ptr_offset(ptr, tex_payload_size);
         }
      } else {
         GENX(pan_sampled_texture_emit)(&pview, &view->descs.tex[0], &ptr);
#if PAN_ARCH >= 9
         if (has_storage) {
            struct pan_image_view spview = panvk_storage_pview(view, &pview);
            GENX(pan_storage_texture_emit)(&spview, &view->descs.storage_tex[0],
                                           &storage_ptr);
         }
#endif
      }
   }

   return VK_SUCCESS;
}

#if PAN_ARCH < 9
static void
prepare_attr_buf_descs(struct panvk_image_view *view)
{
   struct panvk_image *image =
      container_of(view->vk.image, struct panvk_image, vk);
   unsigned plane_idx = panvk_image_view_plane_index(view);

   const struct pan_image_props *plane_props =
      &image->planes[plane_idx].image.props;
   const struct pan_image_layout *plane_layout =
      &image->planes[plane_idx].plane.layout;
   const struct pan_image_slice_layout *slayout =
      &plane_layout->slices[view->pview.first_level];
   bool is_3d = plane_props->dim == MALI_TEXTURE_DIMENSION_3D;
   struct pan_image_view spview = panvk_storage_pview(view, &view->pview);
   unsigned offset =
      slayout->offset_B + (spview.first_layer_or_z_slice *
                           (is_3d ? slayout->tiled_or_linear.surface_stride_B
                                  : plane_layout->array_stride_B));

   unsigned nr_samples = image->planes[plane_idx].image.props.nr_samples;
   nr_samples = (nr_samples > 0) ? nr_samples : 1;
   unsigned log2_nr_samples = util_logbase2(nr_samples);

   pan_pack(&view->descs.img_attrib_buf[0], ATTRIBUTE_BUFFER, cfg) {
      /* The format is the only thing we lack to emit attribute descriptors
       * when copying from the set to the attribute tables. Instead of
       * making the descriptor size to store an extra format, we pack
       * the 22-bit format with the texel stride, which is expected to be
       * fit in 7 bits, followed by 3 bits for log2(nr_samples), which we
       * need in order to reconstruct the number of layers in multisampled
       * arrays.
       */
      uint32_t fmt_blksize = util_format_get_blocksize(view->pview.format);
      uint32_t hw_fmt =
         GENX(pan_format_from_pipe_format)(view->pview.format)->hw;

      assert(fmt_blksize <= BITFIELD_MASK(7));
      assert(log2_nr_samples <= BITFIELD_MASK(3));
      assert(hw_fmt <= BITFIELD_MASK(22));

      cfg.type = image->vk.drm_format_mod == DRM_FORMAT_MOD_LINEAR
                    ? MALI_ATTRIBUTE_TYPE_3D_LINEAR
                    : MALI_ATTRIBUTE_TYPE_3D_INTERLEAVED;
      cfg.pointer = image->planes[plane_idx].plane.base + offset;
      cfg.stride = fmt_blksize | (log2_nr_samples << 7) | (hw_fmt << 10);
      cfg.size = pan_image_mip_level_size(&image->planes[plane_idx].image, 0,
                                          view->pview.first_level);
   }

   struct mali_attribute_buffer_packed *buf = &view->descs.img_attrib_buf[1];
   pan_cast_and_pack(buf, ATTRIBUTE_BUFFER_CONTINUATION_3D, cfg) {
      VkExtent3D extent = view->vk.extent;

      cfg.s_dimension = extent.width;
      cfg.t_dimension = extent.height;
      cfg.r_dimension = pan_image_view_layer_or_3d_slice_count(&spview);
      cfg.row_stride = slayout->tiled_or_linear.row_stride_B;
      if (cfg.r_dimension > 1 || nr_samples > 1) {
         cfg.slice_stride = view->pview.dim == MALI_TEXTURE_DIMENSION_3D
                               ? slayout->tiled_or_linear.surface_stride_B
                               : plane_layout->array_stride_B;
      }
      if (nr_samples > 1) {
         cfg.r_dimension *= nr_samples;
         cfg.slice_stride /= nr_samples;
      }
   }
}
#endif

static unsigned
get_yuv_cr_siting(const struct vk_ycbcr_conversion_state *conv)
{
   const enum pipe_format pfmt = vk_format_to_pipe_format(conv->format);
   const struct util_format_description *desc = util_format_description(pfmt);
   const bool x_mid = conv->chroma_offsets[0] == VK_CHROMA_LOCATION_MIDPOINT;
   const bool y_mid = conv->chroma_offsets[1] == VK_CHROMA_LOCATION_MIDPOINT;

   if (desc->subsampling == PIPE_VIDEO_CHROMA_FORMAT_420) {
      if (x_mid && y_mid)
         return MALI_YUV_CR_SITING_CENTER;
      else if (y_mid)
         return MALI_YUV_CR_SITING_CENTER_Y;
      else if (x_mid)
         return MALI_YUV_CR_SITING_CENTER_X;
   } else if (desc->subsampling == PIPE_VIDEO_CHROMA_FORMAT_422) {
      if (x_mid)
         return MALI_YUV_CR_SITING_CENTER_422;
   }

   /* The default return handles YUV 4:4:4 as well. */
   return MALI_YUV_CR_SITING_CO_SITED;
}

static void
create_ms_views(struct panvk_device *dev, struct panvk_image_view *view,
                const VkImageViewCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator)
{
   /* Don't create extra views for internal views. */
   if (pCreateInfo->flags & VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA)
      return;

   struct panvk_image *source_img =
      panvk_image_from_handle(vk_image_to_handle(view->vk.image));
   const VkImage *target_images = source_img->ms_imgs;

   VkImageViewCreateInfo create_info = *pCreateInfo;

   for (uint32_t idx = 0; idx < ARRAY_SIZE(source_img->ms_imgs); ++idx) {
      if (target_images[idx] == VK_NULL_HANDLE)
         continue;

      create_info.image = target_images[idx];

      assert(vk_image_from_handle(create_info.image)->format ==
             source_img->vk.format);
      assert(view->vk.format == create_info.format);
      assert(!(vk_image_from_handle(create_info.image)->create_flags &
               VK_IMAGE_CREATE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT));

      panvk_per_arch(CreateImageView)(panvk_device_to_handle(dev), &create_info,
                                      pAllocator, &view->ms_views[idx]);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(CreateImageView)(VkDevice _device,
                                const VkImageViewCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkImageView *pView)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_image, image, pCreateInfo->image);
   struct panvk_image_view *view;
   VkResult result;

   view = vk_image_view_create(&device->vk, pCreateInfo,
                               pAllocator, sizeof(*view));
   if (view == NULL)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* vk_image_view_init() sanitizes depth/stencil formats to use the
    * single-plane format, which panvk rely on.  It doesn't do this with
    * driver-internal images, though.  We have to do that ourselves.
    */
   if (view->vk.create_flags & VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA) {
      if (view->vk.aspects == VK_IMAGE_ASPECT_DEPTH_BIT)
         view->vk.view_format = vk_format_depth_only(view->vk.view_format);
      else if (view->vk.aspects == VK_IMAGE_ASPECT_STENCIL_BIT)
         view->vk.view_format = vk_format_stencil_only(view->vk.view_format);
   }

   enum pipe_format pfmt = vk_format_to_pipe_format(view->vk.view_format);
   const VkImageViewASTCDecodeModeEXT *astc_decode =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_VIEW_ASTC_DECODE_MODE_EXT);

   view->pview = (struct pan_image_view){
      .format = pfmt,
      .astc.hdr = util_format_is_astc_hdr(pfmt),
      .astc.narrow = astc_decode &&
                     astc_decode->decodeMode == VK_FORMAT_R8G8B8A8_UNORM,
      .dim = panvk_view_type_to_mali_tex_dim(view->vk.view_type),
      .nr_samples = image->vk.samples,
      .first_level = view->vk.base_mip_level,
      /* MIPmapping in YUV formats is not supported by the HW. */
      .last_level = vk_format_get_ycbcr_info(view->vk.format)
         ? view->vk.base_mip_level
         : view->vk.base_mip_level + view->vk.level_count - 1,
      .min_lod = view->vk.min_lod,
   };

   if (view->vk.view_type == VK_IMAGE_VIEW_TYPE_3D) {
      view->pview.first_layer_or_z_slice = 0;
      view->pview.last_layer_or_z_slice = view->vk.extent.depth - 1;
   } else {
      view->pview.first_layer_or_z_slice = view->vk.base_array_layer;
      view->pview.last_layer_or_z_slice =
         view->vk.base_array_layer + view->vk.layer_count - 1;
   }

   /* Per VUID-VkImageViewCreateInfo-pNext-01970, the view swizzle is identity
    * (effectively ignored) when a VkSamplerYcbcrConversionInfo is chained. For
    * sw YUV handling, the swizzle is extracted and applied in the common YUV
    * lowering pass. For hw YUV texturing, it'd be cheaper to directly apply to
    * the view swizzling. To be noted, the view format is checked here.
    */
   if (panvk_image_use_yuv_tex(PAN_ARCH, view->vk.format)) {
      const VkSamplerYcbcrConversionInfo *ycbcr_info = vk_find_struct_const(
         pCreateInfo->pNext, SAMPLER_YCBCR_CONVERSION_INFO);
      if (ycbcr_info) {
         VK_FROM_HANDLE(vk_ycbcr_conversion, conversion,
                        ycbcr_info->conversion);
         view->vk.swizzle = (VkComponentMapping){
            .r = conversion->state.mapping[0],
            .g = conversion->state.mapping[1],
            .b = conversion->state.mapping[2],
            .a = conversion->state.mapping[3],
         };

         view->pview.yuv.override_cr_siting = true;
         view->pview.yuv.cr_siting = get_yuv_cr_siting(&conversion->state);
      }
   }
   vk_component_mapping_to_pipe_swizzle(view->vk.swizzle, view->pview.swizzle);

   u_foreach_bit(aspect_bit, view->vk.aspects) {
      uint8_t image_plane = panvk_plane_index(image, 1u << aspect_bit);

      /* Place the view plane at index 0 for single-plane views of multiplane
       * formats. Does not apply to YCbCr views of multiplane images since
       * view->vk.aspects for those will contain the full set of plane aspects.
       */
      uint8_t view_plane = (view->vk.aspects == VK_IMAGE_ASPECT_PLANE_1_BIT ||
                            view->vk.aspects == VK_IMAGE_ASPECT_PLANE_2_BIT) ?
                           0 : image_plane;
      view->pview.planes[view_plane] = (struct pan_image_plane_ref){
         .image = PAN_IMAGE_FROM(PAN_ARCH, image, image_plane),
         .plane_idx = PAN_IMAGE_PLANE_INDEX_FROM(PAN_ARCH, image, image_plane),
      };
   }

   /* Depth/stencil are viewed as color for copies. */
   if (view->vk.aspects == VK_IMAGE_ASPECT_COLOR_BIT &&
       panvk_image_is_planar_depth_stencil(image) &&
       vk_format_get_blocksize(view->vk.view_format) == 1) {
      view->pview.planes[0] = (struct pan_image_plane_ref) {
         .image = &image->planes[1].image,
         .plane_idx = 0,
      };
   }

   /* We need to patch the view format when the image contains both
    * depth and stencil but the view only contains one of these components, so
    * we can ignore the component we don't use.
    */
   if (view->vk.aspects == VK_IMAGE_ASPECT_STENCIL_BIT)
      view->pview.format = panvk_image_stencil_only_pfmt(image);
   else if (view->vk.aspects == VK_IMAGE_ASPECT_DEPTH_BIT)
      view->pview.format = panvk_image_depth_only_pfmt(image);

   VkImageUsageFlags tex_usage_mask = VK_IMAGE_USAGE_SAMPLED_BIT;

#if PAN_ARCH >= 9
   /* Valhall passes a texture descriptor to LEA_TEX. */
   tex_usage_mask |= VK_IMAGE_USAGE_STORAGE_BIT;
#endif

   if (view->vk.usage & tex_usage_mask) {
      result = prepare_tex_descs(view);
      if (result != VK_SUCCESS)
         goto err_destroy_iview;
   }

#if PAN_ARCH < 9
   if (view->vk.usage & VK_IMAGE_USAGE_STORAGE_BIT)
      prepare_attr_buf_descs(view);
#endif

   if (view->vk.image->create_flags &
       VK_IMAGE_CREATE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT)
      create_ms_views(device, view, pCreateInfo, pAllocator);

   *pView = panvk_image_view_to_handle(view);
   return VK_SUCCESS;

err_destroy_iview:
   vk_image_view_destroy(&device->vk, pAllocator, &view->vk);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(DestroyImageView)(VkDevice _device, VkImageView _view,
                                 const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_image_view, view, _view);

   if (!view)
      return;

   for (uint32_t i = 0; i < ARRAY_SIZE(view->ms_views); ++i) {
      panvk_per_arch(DestroyImageView)(_device, view->ms_views[i],
                                       pAllocator);
   }

   panvk_pool_free_mem(&view->mem);
   vk_image_view_destroy(&device->vk, pAllocator, &view->vk);
}
