/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"

static enum isl_channel_select
remap_swizzle(VkComponentSwizzle swizzle,
              struct isl_swizzle format_swizzle)
{
   switch (swizzle) {
   case VK_COMPONENT_SWIZZLE_ZERO:  return ISL_CHANNEL_SELECT_ZERO;
   case VK_COMPONENT_SWIZZLE_ONE:   return ISL_CHANNEL_SELECT_ONE;
   case VK_COMPONENT_SWIZZLE_R:     return format_swizzle.r;
   case VK_COMPONENT_SWIZZLE_G:     return format_swizzle.g;
   case VK_COMPONENT_SWIZZLE_B:     return format_swizzle.b;
   case VK_COMPONENT_SWIZZLE_A:     return format_swizzle.a;
   default:
      UNREACHABLE("Invalid swizzle");
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
                             struct anv_surface_state *state_inout)
{
   uint32_t plane = anv_image_aspect_to_plane(image, aspect);
   if (image->emu_plane_format != VK_FORMAT_UNDEFINED) {
      const uint16_t view_bpb = isl_format_get_layout(view_in->format)->bpb;
      const uint16_t plane_bpb = isl_format_get_layout(
            image->planes[plane].primary_surface.isl.format)->bpb;

      /* We should redirect to the hidden plane when the original view format
       * is compressed or when the view usage is storage.  But we don't always
       * have visibility to the original view format so we also check for size
       * compatibility.
       */
      if (isl_format_is_compressed(view_in->format) ||
          (view_usage & ISL_SURF_USAGE_STORAGE_BIT) ||
          view_bpb != plane_bpb) {
         plane = image->n_planes;
         assert(isl_format_get_layout(
                  image->planes[plane].primary_surface.isl.format)->bpb ==
                view_bpb);
      }
   }

   const struct anv_surface *surface = &image->planes[plane].primary_surface,
      *aux_surface = &image->planes[plane].aux_surface;

   struct isl_view view = *view_in;
   view.usage |= view_usage;

   if (view_usage == ISL_SURF_USAGE_RENDER_TARGET_BIT)
      view.swizzle = anv_swizzle_for_render(view.swizzle);

   /* Propagate the protection flag of the image to the view. */
   view_usage |= surface->isl.usage & ISL_SURF_USAGE_PROTECTED_BIT;

   /* If this is a HiZ buffer we can sample from with a programmable clear
    * value (SKL+), define the clear value to the optimal constant.
    */
   union isl_color_value default_clear_color = { .u32 = { 0, } };
   if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
      default_clear_color = anv_image_hiz_clear_value(image);

   if (!clear_color)
      clear_color = &default_clear_color;

   const struct anv_address address =
      anv_image_address(image, &surface->memory_range);

   void *surface_state_map = state_inout->state_data.data;

   const struct isl_surf *isl_surf = &surface->isl;

   struct isl_surf tmp_surf;
   uint64_t offset_B = 0;
   uint32_t tile_x_sa = 0, tile_y_sa = 0;
   if (isl_format_is_compressed(surface->isl.format) &&
       !isl_format_is_compressed(view.format)) {
      /* We're creating an uncompressed view of a compressed surface. This is
       * allowed but only for a single level/layer.
       */
      assert(surface->isl.samples == 1);
      assert(view.levels == 1);

      ASSERTED bool ok =
         isl_surf_get_uncompressed_surf(&device->isl_dev, isl_surf, &view,
                                        &tmp_surf, &view,
                                        &offset_B, &tile_x_sa, &tile_y_sa);
      assert(ok);
      isl_surf = &tmp_surf;
   }

   state_inout->address = anv_address_add(address, offset_B);

   struct anv_address aux_address = ANV_NULL_ADDRESS;
   if (aux_usage != ISL_AUX_USAGE_NONE)
      aux_address = anv_image_address(image, &aux_surface->memory_range);
   state_inout->aux_address = aux_address;

   const struct anv_address clear_address =
      anv_image_get_clear_color_addr(device, image, view.format, aspect,
                                     view_usage & ISL_SURF_USAGE_TEXTURE_BIT);
   state_inout->clear_address = clear_address;

   isl_surf_fill_state(&device->isl_dev, surface_state_map,
                       .surf = isl_surf,
                       .view = &view,
                       .address = anv_address_physical(state_inout->address),
                       .clear_color = *clear_color,
                       .aux_surf = &aux_surface->isl,
                       .aux_usage = aux_usage,
                       .aux_address = anv_address_physical(aux_address),
                       .clear_address = anv_address_physical(clear_address),
                       .use_clear_address =
                          device->isl_dev.ss.clear_color_state_size > 0,
                       .mocs = anv_mocs(device, state_inout->address.bo,
                                        view_usage),
                       .x_offset_sa = tile_x_sa,
                       .y_offset_sa = tile_y_sa,
                       /* Assume robustness with EXT_pipeline_robustness
                        * because this can be turned on/off per pipeline and
                        * we have no visibility on this here.
                        */
                       .robust_image_access =
                          device->vk.enabled_features.robustImageAccess ||
                          device->vk.enabled_features.robustImageAccess2 ||
                          device->vk.enabled_extensions.EXT_pipeline_robustness);

   /* With the exception of gfx8, the bottom 12 bits of the MCS base address
    * are used to store other information. This should be ok, however, because
    * the surface buffer addresses are always 4K page aligned.
    */
   if (!anv_address_is_null(aux_address)) {
      uint32_t *aux_addr_dw = surface_state_map +
         device->isl_dev.ss.aux_addr_offset;
      assert((aux_address.offset & 0xfff) == 0);
      state_inout->aux_address.offset |= *aux_addr_dw & 0xfff;
   }

   if (device->info->ver >= 10 && clear_address.bo) {
      uint32_t *clear_addr_dw = surface_state_map +
         device->isl_dev.ss.clear_color_state_offset;
      assert(util_is_aligned(clear_address.offset, 64));
      state_inout->clear_address.offset |= *clear_addr_dw & 0x3f;
   }

   if (state_inout->state.map)
      memcpy(state_inout->state.map, surface_state_map, ANV_SURFACE_STATE_SIZE);
}

static uint32_t
anv_image_aspect_get_planes(VkImageAspectFlags aspect_mask)
{
   anv_assert_valid_aspect_set(aspect_mask);
   return util_bitcount(aspect_mask);
}

void
anv_image_view_init(struct anv_device *device,
                    struct anv_image_view *iview,
                    const VkImageViewCreateInfo *pCreateInfo,
                    struct anv_state_stream *surface_state_stream)
{
   ANV_FROM_HANDLE(anv_image, image, pCreateInfo->image);

   vk_image_view_init(&device->vk, &iview->vk, pCreateInfo);
   iview->image = image;
   iview->n_planes = anv_image_aspect_get_planes(iview->vk.aspects);
   iview->use_surface_state_stream = surface_state_stream != NULL;

   /* Now go through the underlying image selected planes and map them to
    * planes in the image view.
    */
   anv_foreach_image_aspect_bit(iaspect_bit, image, iview->vk.aspects) {
      const uint32_t vplane =
         anv_aspect_to_plane(iview->vk.aspects, 1UL << iaspect_bit);

      VkFormat view_format = iview->vk.view_format;
      if (anv_is_compressed_format_emulated(device->physical, view_format)) {
         assert(image->emu_plane_format != VK_FORMAT_UNDEFINED);
         view_format =
            anv_get_compressed_format_emulation(device->physical, view_format);
      }
      const struct anv_format_plane format = anv_get_format_plane(
            device->physical, view_format, vplane, image->vk.tiling);

      iview->planes[vplane].isl = (struct isl_view) {
         .format = format.isl_format,
         .base_level = iview->vk.base_mip_level,
         .levels = iview->vk.level_count,
         .base_array_layer = iview->vk.base_array_layer,
         .array_len = iview->vk.layer_count,
         .min_lod_clamp = iview->vk.min_lod,
         .swizzle = {
            .r = remap_swizzle(iview->vk.swizzle.r, format.swizzle),
            .g = remap_swizzle(iview->vk.swizzle.g, format.swizzle),
            .b = remap_swizzle(iview->vk.swizzle.b, format.swizzle),
            .a = remap_swizzle(iview->vk.swizzle.a, format.swizzle),
         },
      };

      if (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_3D) {
         iview->planes[vplane].isl.base_array_layer = 0;
         iview->planes[vplane].isl.array_len = iview->vk.extent.depth;
      }

      if (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_CUBE ||
          pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
         iview->planes[vplane].isl.usage = ISL_SURF_USAGE_CUBE_BIT;
      } else {
         iview->planes[vplane].isl.usage = 0;
      }

      if (iview->vk.usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
         iview->planes[vplane].optimal_sampler.state =
            anv_device_maybe_alloc_surface_state(device, surface_state_stream);
         iview->planes[vplane].general_sampler.state =
            anv_device_maybe_alloc_surface_state(device, surface_state_stream);

         enum isl_aux_usage general_aux_usage =
            anv_layout_to_aux_usage(device->info, image, 1UL << iaspect_bit,
                                    VK_IMAGE_USAGE_SAMPLED_BIT,
                                    VK_IMAGE_LAYOUT_GENERAL,
                                    VK_QUEUE_GRAPHICS_BIT |
                                    VK_QUEUE_COMPUTE_BIT |
                                    VK_QUEUE_TRANSFER_BIT);
         enum isl_aux_usage optimal_aux_usage =
            anv_layout_to_aux_usage(device->info, image, 1UL << iaspect_bit,
                                    VK_IMAGE_USAGE_SAMPLED_BIT,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_QUEUE_GRAPHICS_BIT |
                                    VK_QUEUE_COMPUTE_BIT |
                                    VK_QUEUE_TRANSFER_BIT);

         anv_image_fill_surface_state(device, image, 1ULL << iaspect_bit,
                                      &iview->planes[vplane].isl,
                                      ISL_SURF_USAGE_TEXTURE_BIT,
                                      optimal_aux_usage, NULL,
                                      ANV_IMAGE_VIEW_STATE_TEXTURE_OPTIMAL,
                                      &iview->planes[vplane].optimal_sampler);

         anv_image_fill_surface_state(device, image, 1ULL << iaspect_bit,
                                      &iview->planes[vplane].isl,
                                      ISL_SURF_USAGE_TEXTURE_BIT,
                                      general_aux_usage, NULL,
                                      0,
                                      &iview->planes[vplane].general_sampler);
      }

      /* NOTE: This one needs to go last since it may stomp isl_view.format */
      if (iview->vk.usage & VK_IMAGE_USAGE_STORAGE_BIT) {
         struct isl_view storage_view = iview->planes[vplane].isl;
         if (iview->vk.view_type == VK_IMAGE_VIEW_TYPE_3D) {
            storage_view.base_array_layer = iview->vk.storage.z_slice_offset;
            storage_view.array_len = iview->vk.storage.z_slice_count;
         }

         enum isl_aux_usage general_aux_usage =
            anv_layout_to_aux_usage(device->info, image, 1UL << iaspect_bit,
                                    VK_IMAGE_USAGE_STORAGE_BIT,
                                    VK_IMAGE_LAYOUT_GENERAL,
                                    VK_QUEUE_GRAPHICS_BIT |
                                    VK_QUEUE_COMPUTE_BIT |
                                    VK_QUEUE_TRANSFER_BIT);
         iview->planes[vplane].storage.state =
            anv_device_maybe_alloc_surface_state(device, surface_state_stream);

         anv_image_fill_surface_state(device, image, 1ULL << iaspect_bit,
                                      &storage_view,
                                      ISL_SURF_USAGE_STORAGE_BIT,
                                      general_aux_usage, NULL,
                                      0,
                                      &iview->planes[vplane].storage);
      }
   }
}

void
anv_image_view_finish(struct anv_image_view *iview)
{
   struct anv_device *device =
      container_of(iview->vk.base.device, struct anv_device, vk);

   if (!iview->use_surface_state_stream) {
      for (uint32_t plane = 0; plane < iview->n_planes; plane++) {
         if (iview->planes[plane].optimal_sampler.state.alloc_size) {
            anv_state_pool_free(&device->bindless_surface_state_pool,
                  iview->planes[plane].optimal_sampler.state);
         }

         if (iview->planes[plane].general_sampler.state.alloc_size) {
            anv_state_pool_free(&device->bindless_surface_state_pool,
                  iview->planes[plane].general_sampler.state);
         }

         if (iview->planes[plane].storage.state.alloc_size) {
            anv_state_pool_free(&device->bindless_surface_state_pool,
                  iview->planes[plane].storage.state);
         }
      }
   }

   vk_image_view_finish(&iview->vk);
}

VkResult
anv_CreateImageView(VkDevice _device,
                    const VkImageViewCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkImageView *pView)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_image_view *iview;

   iview = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*iview), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (iview == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   anv_image_view_init(device, iview, pCreateInfo, NULL);

   *pView = anv_image_view_to_handle(iview);

   return VK_SUCCESS;
}

void
anv_DestroyImageView(VkDevice _device, VkImageView _iview,
                     const VkAllocationCallbacks *pAllocator)
{
   ANV_FROM_HANDLE(anv_image_view, iview, _iview);

   if (!iview)
      return;

   anv_image_view_finish(iview);
   vk_free2(&iview->vk.base.device->alloc, pAllocator, iview);
}
