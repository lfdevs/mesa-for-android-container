/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_wsi.h"
#include "kk_cmd_buffer.h"
#include "kk_device.h"
#include "kk_dispatch_trampolines.h"
#include "kk_image.h"
#include "kk_instance.h"
#include "wsi_common.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

static PFN_vkVoidFunction
kk_instance_get_proc_addr_unchecked(const struct vk_instance *instance,
                                    const char *name)
{
   PFN_vkVoidFunction func;

   if (instance == NULL || name == NULL)
      return NULL;

   func = vk_instance_dispatch_table_get(&instance->dispatch_table, name);
   if (func != NULL)
      return func;

   func = vk_physical_device_dispatch_table_get(&kk_physical_device_trampolines,
                                                name);
   if (func != NULL)
      return func;

   func = vk_device_dispatch_table_get(&kk_device_trampolines, name);
   if (func != NULL)
      return func;

   return NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
kk_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   VK_FROM_HANDLE(kk_physical_device, pdev, physicalDevice);
   return kk_instance_get_proc_addr_unchecked(pdev->vk.instance, pName);
}

static VkResult
kk_bind_drawable_to_vkimage(VkImage vk_image, void *drawable)
{
   VK_FROM_HANDLE(kk_image, image, vk_image);
   mtl_texture *texture = mtl_drawable_get_texture(drawable);

   /* This should only be called for swapchain binding. */
   assert(image->plane_count == 1);
   struct kk_image_plane *plane = &image->planes[0];
   if (plane->mtl_handle)
      mtl_release(plane->mtl_handle);
   if (plane->mtl_handle_array)
      mtl_release(plane->mtl_handle_array);
   plane->mtl_handle = mtl_retain(texture);
   plane->mtl_handle_array = NULL;
   plane->addr = mtl_texture_get_gpu_resource_id(texture);

   return VK_SUCCESS;
}

static void
kk_encode_drawable_present(VkCommandBuffer vk_cmd, void *drawable)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, vk_cmd);
   mtl_retain(drawable);
   cmd->drawable = drawable;
}

static struct vk_queue *
kk_get_blit_queue(VkDevice device)
{
   /* We only have one queue, so just return that one. */
   VK_FROM_HANDLE(kk_device, dev, device);
   return &dev->queue.vk;
}

VkResult
kk_init_wsi(struct kk_physical_device *pdev)
{
   struct wsi_device_options wsi_options = {.sw_device = false};
   struct wsi_device *wsi = &pdev->wsi_device;
   VkResult result =
      wsi_device_init(wsi, kk_physical_device_to_handle(pdev), kk_wsi_proc_addr,
                      &pdev->vk.instance->alloc,
                      0u,   // Not relevant for metal wsi
                      NULL, // Not relevant for metal
                      &wsi_options);
   if (result != VK_SUCCESS)
      return result;

   wsi->metal.bind_drawable_to_vkimage = kk_bind_drawable_to_vkimage;
   wsi->metal.encode_drawable_present = kk_encode_drawable_present;
   wsi->get_blit_queue = kk_get_blit_queue;

   pdev->vk.wsi_device = wsi;

   return result;
}

void
kk_finish_wsi(struct kk_physical_device *pdev)
{
   pdev->vk.wsi_device = NULL;
   wsi_device_finish(&pdev->wsi_device, &pdev->vk.instance->alloc);
}
