/*
 * Copyright 2024 Autodesk, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "vk_instance.h"
#include "vk_physical_device.h"
#include "vk_util.h"

#include "util/timespec.h"
#include "util/u_vector.h"

#include "wsi_common_entrypoints.h"
#include "wsi_common_private.h"
#include "wsi_common_metal_layer.h"

#include "vulkan/vulkan_core.h"

#include <assert.h>

struct wsi_metal {
   struct wsi_interface base;

   struct wsi_device *wsi;

   const VkAllocationCallbacks *alloc;
   VkPhysicalDevice physical_device;
};

static VkResult
wsi_metal_surface_get_support(VkIcdSurfaceBase *surface,
                                 struct wsi_device *wsi_device,
                                 uint32_t queueFamilyIndex,
                                 VkBool32* pSupported)
{
   *pSupported = true;
   return VK_SUCCESS;
}

static const VkPresentModeKHR present_modes[] = {
   VK_PRESENT_MODE_IMMEDIATE_KHR,
   VK_PRESENT_MODE_FIFO_KHR,
};

static VkResult
wsi_metal_surface_get_capabilities(VkIcdSurfaceBase *surface,
                                 struct wsi_device *wsi_device,
                                 VkSurfaceCapabilitiesKHR* caps)
{
   VkIcdSurfaceMetal *metal_surface = (VkIcdSurfaceMetal *)surface;
   assert(metal_surface->pLayer);

   wsi_metal_layer_size(metal_surface->pLayer,
      &caps->currentExtent.width,
      &caps->currentExtent.height);

   if (!caps->currentExtent.width && !caps->currentExtent.height)
      caps->currentExtent.width = caps->currentExtent.height = UINT32_MAX;

   caps->minImageCount = 2;
   caps->maxImageCount = 3;

   caps->minImageExtent = (VkExtent2D) { 1, 1 };
   caps->maxImageExtent = (VkExtent2D) {
      wsi_device->maxImageDimension2D,
      wsi_device->maxImageDimension2D,
   };

   caps->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   caps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   caps->maxImageArrayLayers = 1;

   caps->supportedCompositeAlpha =
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR |
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;

   caps->supportedUsageFlags = wsi_caps_get_image_usage();

   VK_FROM_HANDLE(vk_physical_device, pdevice, wsi_device->pdevice);
   if (pdevice->supported_extensions.EXT_attachment_feedback_loop_layout)
      caps->supportedUsageFlags |= VK_IMAGE_USAGE_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;

   return VK_SUCCESS;
}

static VkResult
wsi_metal_surface_get_capabilities2(VkIcdSurfaceBase *surface,
                                       struct wsi_device *wsi_device,
                                       const void *info_next,
                                       VkSurfaceCapabilities2KHR* caps)
{
   assert(caps->sType == VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR);

   const VkSurfacePresentModeKHR *present_mode =
      (const VkSurfacePresentModeKHR *)vk_find_struct_const(info_next, SURFACE_PRESENT_MODE_KHR);

   VkResult result =
      wsi_metal_surface_get_capabilities(surface, wsi_device,
                                      &caps->surfaceCapabilities);

   vk_foreach_struct(ext, caps->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_SURFACE_PROTECTED_CAPABILITIES_KHR: {
         VkSurfaceProtectedCapabilitiesKHR *protected = (void *)ext;
         protected->supportsProtected = VK_FALSE;
         break;
      }

      case VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_KHR: {
         /* TODO: support scaling */
         VkSurfacePresentScalingCapabilitiesKHR *scaling =
            (VkSurfacePresentScalingCapabilitiesKHR *)ext;
         scaling->supportedPresentScaling = 0;
         scaling->supportedPresentGravityX = 0;
         scaling->supportedPresentGravityY = 0;
         scaling->minScaledImageExtent = caps->surfaceCapabilities.minImageExtent;
         scaling->maxScaledImageExtent = caps->surfaceCapabilities.maxImageExtent;
         break;
      }

      case VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_KHR: {
         /* Unsupported, just report the input present mode. */
         VkSurfacePresentModeCompatibilityKHR *compat =
            (VkSurfacePresentModeCompatibilityKHR *)ext;
         if (compat->pPresentModes) {
            if (compat->presentModeCount) {
               assert(present_mode);
               compat->pPresentModes[0] = present_mode->presentMode;
               compat->presentModeCount = 1;
            }
         } else {
            if (!present_mode)
               wsi_common_vk_warn_once("Use of VkSurfacePresentModeCompatibilityKHR "
                                       "without a VkSurfacePresentModeKHR set. This is an "
                                       "application bug.\n");
            compat->presentModeCount = 1;
         }
         break;
      }

      default:
         /* Ignored */
         break;
      }
   }

   return result;
}

static const VkFormat available_surface_formats[] = {
   VK_FORMAT_B8G8R8A8_SRGB,
   VK_FORMAT_B8G8R8A8_UNORM,
   VK_FORMAT_R16G16B16A16_SFLOAT,
   VK_FORMAT_A2R10G10B10_UNORM_PACK32,
   VK_FORMAT_A2B10G10R10_UNORM_PACK32,
};

static const VkColorSpaceKHR available_surface_color_spaces[] = {
   VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
   VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT,
   VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
   VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT,
   VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT,
   VK_COLOR_SPACE_BT709_NONLINEAR_EXT,
   VK_COLOR_SPACE_BT2020_LINEAR_EXT,
   VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT,
   VK_COLOR_SPACE_PASS_THROUGH_EXT,
   VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT,
};

static void
get_sorted_vk_formats(bool force_bgra8_unorm_first, VkFormat *sorted_formats)
{
   for (unsigned i = 0; i < ARRAY_SIZE(available_surface_formats); i++)
      sorted_formats[i] = available_surface_formats[i];

   if (force_bgra8_unorm_first) {
      for (unsigned i = 0; i < ARRAY_SIZE(available_surface_formats); i++) {
         if (sorted_formats[i] == VK_FORMAT_B8G8R8A8_UNORM) {
            sorted_formats[i] = sorted_formats[0];
            sorted_formats[0] = VK_FORMAT_B8G8R8A8_UNORM;
            break;
         }
      }
   }
}

static VkResult
wsi_metal_surface_get_formats(VkIcdSurfaceBase *icd_surface,
                                 struct wsi_device *wsi_device,
                                 uint32_t* pSurfaceFormatCount,
                                 VkSurfaceFormatKHR* pSurfaceFormats)
{
   VK_OUTARRAY_MAKE_TYPED(VkSurfaceFormatKHR, out, pSurfaceFormats, pSurfaceFormatCount);

   VkFormat sorted_formats[ARRAY_SIZE(available_surface_formats)];
   get_sorted_vk_formats(wsi_device->force_bgra8_unorm_first, sorted_formats);

   for (unsigned i = 0; i < ARRAY_SIZE(sorted_formats); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(available_surface_color_spaces); j++) {
         vk_outarray_append_typed(VkSurfaceFormatKHR, &out, f) {
            f->format = sorted_formats[i];
            f->colorSpace = available_surface_color_spaces[j];
         }
      }
   }

   return vk_outarray_status(&out);
}

static VkResult
wsi_metal_surface_get_formats2(VkIcdSurfaceBase *icd_surface,
                                  struct wsi_device *wsi_device,
                                  const void *info_next,
                                  uint32_t* pSurfaceFormatCount,
                                  VkSurfaceFormat2KHR* pSurfaceFormats)
{
   VK_OUTARRAY_MAKE_TYPED(VkSurfaceFormat2KHR, out, pSurfaceFormats, pSurfaceFormatCount);

   VkFormat sorted_formats[ARRAY_SIZE(available_surface_formats)];
   get_sorted_vk_formats(wsi_device->force_bgra8_unorm_first, sorted_formats);

   for (unsigned i = 0; i < ARRAY_SIZE(sorted_formats); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(available_surface_color_spaces); j++) {
         vk_outarray_append_typed(VkSurfaceFormat2KHR, &out, f) {
            assert(f->sType == VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR);
            f->surfaceFormat.format = sorted_formats[i];
            f->surfaceFormat.colorSpace = available_surface_color_spaces[j];
         }
      }
   }

   return vk_outarray_status(&out);
}

static VkResult
wsi_metal_surface_get_present_modes(VkIcdSurfaceBase *surface,
                                       struct wsi_device *wsi_device,
                                       uint32_t* pPresentModeCount,
                                       VkPresentModeKHR* pPresentModes)
{
   if (pPresentModes == NULL) {
      *pPresentModeCount = ARRAY_SIZE(present_modes);
      return VK_SUCCESS;
   }

   *pPresentModeCount = MIN2(*pPresentModeCount, ARRAY_SIZE(present_modes));
   typed_memcpy(pPresentModes, present_modes, *pPresentModeCount);

   return *pPresentModeCount < ARRAY_SIZE(present_modes) ? VK_INCOMPLETE : VK_SUCCESS;
}

static VkResult
wsi_metal_surface_get_present_rectangles(VkIcdSurfaceBase *surface,
                                            struct wsi_device *wsi_device,
                                            uint32_t* pRectCount,
                                            VkRect2D* pRects)
{
   VK_OUTARRAY_MAKE_TYPED(VkRect2D, out, pRects, pRectCount);

   vk_outarray_append_typed(VkRect2D, &out, rect) {
      /* We don't know a size so just return the usual "I don't know." */
      *rect = (VkRect2D) {
         .offset = { 0, 0 },
         .extent = { UINT32_MAX, UINT32_MAX },
      };
   }

   return vk_outarray_status(&out);
}

struct wsi_metal_image {
   struct wsi_image base;
   CAMetalDrawable *drawable;
};

struct wsi_metal_swapchain {
   struct wsi_swapchain base;

   VkExtent2D extent;
   VkFormat vk_format;

   VkIcdSurfaceMetal *surface;

   struct wsi_metal_layer_blit_context *blit_context;

   uint32_t current_image_index;
   struct wsi_metal_image images[0];
};
VK_DEFINE_NONDISP_HANDLE_CASTS(wsi_metal_swapchain, base.base, VkSwapchainKHR,
                               VK_OBJECT_TYPE_SWAPCHAIN_KHR)

static struct wsi_image *
wsi_metal_swapchain_get_wsi_image(struct wsi_swapchain *wsi_chain,
                                     uint32_t image_index)
{
   struct wsi_metal_swapchain *chain =
      (struct wsi_metal_swapchain *)wsi_chain;
   return &chain->images[image_index].base;
}

static VkResult
wsi_cmd_blit_image_to_image(const struct wsi_swapchain *chain,
                            const struct wsi_image_info *info,
                            struct wsi_image *image)
{
   /* Should only be called from non-software backends */
   assert(!chain->wsi->sw);
   
   const struct wsi_device *wsi = chain->wsi;
   struct wsi_metal_image *metal_image = container_of(image, struct wsi_metal_image, base);
   VkResult result;
   int queue_count = chain->blit.queue != NULL ? 1 : wsi->queue_family_count;

   for (uint32_t i = 0; i < queue_count; i++) {
      if (!chain->cmd_pools[i])
         continue;

      /* We need to cycle command buffers since the MTLTexture backing the presentable
       * VkImage changes every time it's acquired. We only have one command buffer
       * per blit since we only submit to a single queue which is the blit queue. */
      wsi->FreeCommandBuffers(chain->device, chain->cmd_pools[i], 1u,
                              &image->blit.cmd_buffers[i + queue_count]);

      /* Store the command buffer in flight */
      image->blit.cmd_buffers[i + queue_count] = image->blit.cmd_buffers[i];

      const VkCommandBufferAllocateInfo cmd_buffer_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
         .pNext = NULL,
         .commandPool = chain->cmd_pools[i],
         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
         .commandBufferCount = 1,
      };
      result = wsi->AllocateCommandBuffers(chain->device, &cmd_buffer_info,
                                          &image->blit.cmd_buffers[i]);
      if (result != VK_SUCCESS)
         return result;

      const VkCommandBufferBeginInfo begin_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      wsi->BeginCommandBuffer(image->blit.cmd_buffers[i], &begin_info);

      VkImageMemoryBarrier img_mem_barriers[] = {
         {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = NULL,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image->image,
            .subresourceRange = {
               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel = 0,
               .levelCount = 1,
               .baseArrayLayer = 0,
               .layerCount = 1,
            },
         },
         {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = NULL,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image->blit.image,
            .subresourceRange = {
               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel = 0,
               .levelCount = 1,
               .baseArrayLayer = 0,
               .layerCount = 1,
            },
         },
      };
      const uint32_t img_mem_barrier_count = 2;
      wsi->CmdPipelineBarrier(image->blit.cmd_buffers[i],
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0,
                              0, NULL,
                              0, NULL,
                              img_mem_barrier_count, img_mem_barriers);

      struct VkImageCopy image_copy = {
         .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
         .srcOffset = { .x = 0, .y = 0, .z = 0 },
         .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
         .dstOffset = { .x = 0, .y = 0, .z = 0 },
         .extent = info->create.extent,
      };

      wsi->CmdCopyImage(image->blit.cmd_buffers[i],
                        image->image,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        image->blit.image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1, &image_copy);

      img_mem_barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      img_mem_barriers[0].dstAccessMask = 0;
      img_mem_barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      img_mem_barriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      img_mem_barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      img_mem_barriers[1].dstAccessMask = 0;
      img_mem_barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      img_mem_barriers[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      wsi->CmdPipelineBarrier(image->blit.cmd_buffers[i],
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                              0,
                              0, NULL,
                              0, NULL,
                              img_mem_barrier_count, img_mem_barriers);

      result = wsi->EndCommandBuffer(image->blit.cmd_buffers[i]);

      wsi->metal.encode_drawable_present(image->blit.cmd_buffers[i], metal_image->drawable);
   }

   /* Release the drawable since command buffers should have retained the drawable. */
   wsi_metal_release_drawable(metal_image->drawable);
   metal_image->drawable = NULL;

   return result;
}

static VkResult
wsi_metal_swapchain_acquire_next_image(struct wsi_swapchain *wsi_chain,
                                       const VkAcquireNextImageInfoKHR *info,
                                       uint32_t *image_index)
{
   struct wsi_metal_swapchain *chain =
      (struct wsi_metal_swapchain *)wsi_chain;
   struct timespec start_time, end_time;
   struct timespec rel_timeout;

   timespec_from_nsec(&rel_timeout, info->timeout);

   clock_gettime(CLOCK_MONOTONIC, &start_time);
   timespec_add(&end_time, &rel_timeout, &start_time);

   while (1) {
      /* Try to acquire an drawable. Unfortunately we might block for up to 1 second. */
      CAMetalDrawable *drawable = wsi_metal_layer_acquire_drawable(chain->surface->pLayer);
      if (drawable) {
         uint32_t i = (chain->current_image_index++) % chain->base.image_count;
         struct wsi_metal_image *image = &chain->images[i];
         *image_index = i;
         image->drawable = drawable;
         if (!wsi_chain->wsi->sw) {
            chain->base.wsi->metal.bind_drawable_to_vkimage(image->base.blit.image,
                                                            image->drawable);
            /* Since present images will only be backed by MTLTextures after acquisition,
             * we need to re-record the command buffer so it uses the new drawable. */
            wsi_cmd_blit_image_to_image(wsi_chain, &wsi_chain->image_info, &image->base);
         }
         return VK_SUCCESS;
      }

      /* Check for timeout. */
      struct timespec current_time;
      clock_gettime(CLOCK_MONOTONIC, &current_time);
      if (timespec_after(&current_time, &end_time))
         return VK_NOT_READY;
   }
}

static VkResult
wsi_metal_swapchain_queue_present(struct wsi_swapchain *wsi_chain,
                                     uint32_t image_index,
                                     uint64_t present_id,
                                     const VkPresentRegionKHR *damage)
{
   struct wsi_metal_swapchain *chain =
      (struct wsi_metal_swapchain *)wsi_chain;

   assert(image_index < chain->base.image_count);

   struct wsi_metal_image *image = &chain->images[image_index];

   if (wsi_chain->wsi->sw) {
      wsi_metal_layer_blit_and_present(chain->blit_context,
         &image->drawable,
         image->base.cpu_map,
         chain->extent.width, chain->extent.height,
         image->base.row_pitches[0]);
   }

   uint32_t width = 0u, height = 0u;
   wsi_metal_layer_size(chain->surface->pLayer, &width, &height);
   bool is_optimal = (width == chain->extent.width && height == chain->extent.height);
   return is_optimal ? VK_SUCCESS : VK_SUBOPTIMAL_KHR;
}

static void
wsi_metal_destroy_image(const struct wsi_metal_swapchain *metal_chain,
                        struct wsi_metal_image *metal_image)
{
   const struct wsi_swapchain *chain = &metal_chain->base;
   const struct wsi_device *wsi = chain->wsi;
   struct wsi_image *image = &metal_image->base;

   /* Software backends can just call common and return */
   if (wsi->sw) {
      wsi_destroy_image(chain, image);
      return;
   }

   /* Required since we allocate 2 per queue, stored with the following layout:
    * cmd_pool 0: 0, queue_count
    * cmd_pool 1: 1, 1 + queue_count
    * ...
    */
   if (image->blit.cmd_buffers) {
      int queue_count =
         chain->blit.queue != NULL ? 1 : wsi->queue_family_count;

      for (uint32_t i = 0; i < queue_count; i++) {
         if (!chain->cmd_pools[i])
            continue;
         wsi->FreeCommandBuffers(chain->device, chain->cmd_pools[i],
                                 1, &image->blit.cmd_buffers[i]);
         wsi->FreeCommandBuffers(chain->device, chain->cmd_pools[i],
                                 1, &image->blit.cmd_buffers[i + queue_count]);
      }
      vk_free(&chain->alloc, image->blit.cmd_buffers);
      image->blit.cmd_buffers = NULL;
   }

   wsi_destroy_image(chain, image);
}

static VkResult
wsi_metal_create_image(const struct wsi_metal_swapchain *metal_chain,
                       const struct wsi_image_info *info,
                       struct wsi_metal_image *metal_image)
{
   const struct wsi_swapchain *chain = &metal_chain->base;
   const struct wsi_device *wsi = chain->wsi;
   struct wsi_image *image = &metal_image->base;

   VkResult result = wsi_create_image(chain, info, image);

   /* Software backends can just call common and return */
   if (wsi->sw || result != VK_SUCCESS)
      return result;

   /* Create VkImages to handle binding at acquisition. */
   result = wsi->CreateImage(chain->device, &chain->image_info.create,
                             &chain->alloc, &image->blit.image);
   if (result != VK_SUCCESS)
      wsi_metal_destroy_image(metal_chain, metal_image);

   return result;
}

static VkResult
wsi_metal_swapchain_destroy(struct wsi_swapchain *wsi_chain,
                               const VkAllocationCallbacks *pAllocator)
{
   struct wsi_metal_swapchain *chain =
      (struct wsi_metal_swapchain *)wsi_chain;

   for (uint32_t i = 0; i < chain->base.image_count; i++) {
      struct wsi_metal_image *image = &chain->images[i];
      if (image->drawable) {
         wsi_metal_release_drawable(image->drawable);
         image->drawable = NULL;
      }

      if (image != VK_NULL_HANDLE)
         wsi_metal_destroy_image(chain, image);
   }

   if (chain->base.wsi->sw)
      wsi_destroy_metal_layer_blit_context(chain->blit_context);

   wsi_swapchain_finish(&chain->base);

   vk_free(pAllocator, chain);

   return VK_SUCCESS;
}

static VkResult
wsi_metal_surface_create_swapchain(VkIcdSurfaceBase *icd_surface,
                                      VkDevice device,
                                      struct wsi_device *wsi_device,
                                      const VkSwapchainCreateInfoKHR* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator,
                                      struct wsi_swapchain **swapchain_out)
{
   VkIcdSurfaceMetal *metal_surface = (VkIcdSurfaceMetal *)icd_surface;
   assert(metal_surface->pLayer);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);

   const int num_images = pCreateInfo->minImageCount;
   const bool opaque_composition =
      pCreateInfo->compositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
   const bool immediate_mode =
      pCreateInfo->presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR;

   VkResult result = wsi_metal_layer_configure(metal_surface->pLayer,
      pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height,
      num_images, pCreateInfo->imageFormat, pCreateInfo->imageColorSpace,
      opaque_composition, immediate_mode);
   if (result != VK_SUCCESS)
      return result;

   struct wsi_metal_swapchain *chain;
   size_t size = sizeof(*chain) + num_images * sizeof(chain->images[0]);
   chain = vk_zalloc(pAllocator, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (chain == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* Software drivers cannot render to an MTLTexture as of now. Rendering to
    * MTLTexture could be supported, but outside of the scope of adding a
    * Metal backend that uses MTLTexture as render target. The software path
    * will render to a CPU texture, and blit it to the presentation MTLTexture
    * at the last moment. */
   const bool is_sw_driver = wsi_device->sw;
   struct wsi_cpu_image_params cpu_params = {
      .base.image_type = WSI_IMAGE_TYPE_CPU,
   };
   struct wsi_base_image_params metal_params = {
      .image_type = WSI_IMAGE_TYPE_METAL,
   };
   struct wsi_base_image_params *params = is_sw_driver ? &cpu_params.base : &metal_params;

   result = wsi_swapchain_init(wsi_device, &chain->base, device,
                               pCreateInfo, params, pAllocator);
   if (result != VK_SUCCESS)
      goto fail_chain_alloc;

   chain->base.destroy = wsi_metal_swapchain_destroy;
   chain->base.get_wsi_image = wsi_metal_swapchain_get_wsi_image;
   chain->base.acquire_next_image = wsi_metal_swapchain_acquire_next_image;
   chain->base.queue_present = wsi_metal_swapchain_queue_present;
   chain->base.present_mode = wsi_swapchain_get_present_mode(wsi_device, pCreateInfo);
   chain->base.image_count = num_images;
   chain->extent = pCreateInfo->imageExtent;
   chain->vk_format = pCreateInfo->imageFormat;
   chain->surface = metal_surface;
   chain->current_image_index = 0;

   uint32_t created_image_count = 0;
   for (uint32_t i = 0; i < chain->base.image_count; i++) {
      result = wsi_metal_create_image(chain, &chain->base.image_info,
                                      &chain->images[i]);
      if (result != VK_SUCCESS)
         goto fail_init_images;

      chain->images[i].drawable = NULL;
      created_image_count++;
   }

   if (is_sw_driver)
      chain->blit_context = wsi_create_metal_layer_blit_context();

   *swapchain_out = &chain->base;

   return VK_SUCCESS;

fail_init_images:
   for (uint32_t i = 0; i < created_image_count; i++)
      wsi_metal_destroy_image(chain, &chain->images[i]);

   wsi_swapchain_finish(&chain->base);

fail_chain_alloc:
   vk_free(pAllocator, chain);

   return result;
}

VkResult
wsi_metal_init_wsi(struct wsi_device *wsi_device,
                    const VkAllocationCallbacks *alloc,
                    VkPhysicalDevice physical_device)
{
   struct wsi_metal *wsi;
   VkResult result;

   wsi = vk_alloc(alloc, sizeof(*wsi), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!wsi) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   wsi->physical_device = physical_device;
   wsi->alloc = alloc;
   wsi->wsi = wsi_device;

   wsi->base.get_support = wsi_metal_surface_get_support;
   wsi->base.get_capabilities2 = wsi_metal_surface_get_capabilities2;
   wsi->base.get_formats = wsi_metal_surface_get_formats;
   wsi->base.get_formats2 = wsi_metal_surface_get_formats2;
   wsi->base.get_present_modes = wsi_metal_surface_get_present_modes;
   wsi->base.get_present_rectangles = wsi_metal_surface_get_present_rectangles;
   wsi->base.create_swapchain = wsi_metal_surface_create_swapchain;

   wsi_device->wsi[VK_ICD_WSI_PLATFORM_METAL] = &wsi->base;

   return VK_SUCCESS;

fail:
   wsi_device->wsi[VK_ICD_WSI_PLATFORM_METAL] = NULL;

   return result;
}

void
wsi_metal_finish_wsi(struct wsi_device *wsi_device,
                    const VkAllocationCallbacks *alloc)
{
   struct wsi_metal *wsi =
      (struct wsi_metal *)wsi_device->wsi[VK_ICD_WSI_PLATFORM_METAL];
   if (!wsi)
      return;

   vk_free(alloc, wsi);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_CreateMetalSurfaceEXT(
   VkInstance _instance,
   const VkMetalSurfaceCreateInfoEXT* pCreateInfo,
   const VkAllocationCallbacks* pAllocator,
   VkSurfaceKHR* pSurface)
{
   VK_FROM_HANDLE(vk_instance, instance, _instance);
   VkIcdSurfaceMetal *surface;

   surface = vk_alloc2(&instance->alloc, pAllocator, sizeof *surface, 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (surface == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   surface->base.platform = VK_ICD_WSI_PLATFORM_METAL;
   surface->pLayer = pCreateInfo->pLayer;
   assert(surface->pLayer);

   *pSurface = VkIcdSurfaceBase_to_handle(&surface->base);
   return VK_SUCCESS;
}

static VkResult
wsi_metal_create_mem(const struct wsi_swapchain *chain,
                     const struct wsi_image_info *info,
                     struct wsi_image *image)
{
   assert(chain->blit.type == WSI_SWAPCHAIN_IMAGE_BLIT);

   const struct wsi_device *wsi = chain->wsi;

   VkMemoryRequirements requirements;
   wsi->GetImageMemoryRequirements(chain->device, image->image, &requirements);

   struct wsi_memory_allocate_info memory_wsi_info = {
      .sType = VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA,
      .pNext = NULL,
      .implicit_sync = false,
   };
   VkMemoryDedicatedAllocateInfo image_mem_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = &memory_wsi_info,
      .image = image->image,
      .buffer = VK_NULL_HANDLE,
   };
   VkMemoryAllocateInfo image_mem_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &image_mem_dedicated_info,
      .allocationSize = requirements.size,
      .memoryTypeIndex = requirements.memoryTypeBits,
   };

   return wsi->AllocateMemory(chain->device, &image_mem_info,
                              &chain->alloc, &image->memory);
}

static VkResult
wsi_metal_allocate_command_buffer(const struct wsi_swapchain *chain,
                                  const struct wsi_image_info *info,
                                  struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   /* We need to create 2 command buffers per queue to be able to ping pong the blit.
    * The first queue_family_count will store the next blit command,
    * and the remaining will store the ones in flight. */
   int cmd_buffer_count =
      chain->blit.queue != NULL ? 2 : wsi->queue_family_count * 2;
   image->blit.cmd_buffers =
      vk_zalloc(&chain->alloc,
                sizeof(VkCommandBuffer) * cmd_buffer_count, 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   return image->blit.cmd_buffers ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

/* Common utilities required by wsi_common.c */
VkResult
wsi_metal_configure_image(const struct wsi_swapchain *chain,
                          const VkSwapchainCreateInfoKHR *pCreateInfo,
                          const struct wsi_metal_image_params *params,
                          struct wsi_image_info *info)
{
   VkResult result =
      wsi_configure_image(chain, pCreateInfo, 0, info);
   if (result != VK_SUCCESS)
      return result;

   if (chain->blit.type != WSI_SWAPCHAIN_NO_BLIT) {
      info->create.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      info->wsi.blit_src = true;
      info->finish_create = wsi_metal_allocate_command_buffer;
      info->select_image_memory_type = wsi_select_device_memory_type;
      info->create_mem = wsi_metal_create_mem;
   }

   return VK_SUCCESS;
}
