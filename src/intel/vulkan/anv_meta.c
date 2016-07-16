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

#include "anv_meta.h"

struct anv_render_pass anv_meta_dummy_renderpass = {0};

void
anv_meta_save(struct anv_meta_saved_state *state,
              const struct anv_cmd_buffer *cmd_buffer,
              uint32_t dynamic_mask)
{
   state->old_pipeline = cmd_buffer->state.pipeline;
   state->old_descriptor_set0 = cmd_buffer->state.descriptors[0];
   memcpy(state->old_vertex_bindings, cmd_buffer->state.vertex_bindings,
          sizeof(state->old_vertex_bindings));

   state->dynamic_mask = dynamic_mask;
   anv_dynamic_state_copy(&state->dynamic, &cmd_buffer->state.dynamic,
                          dynamic_mask);
}

void
anv_meta_restore(const struct anv_meta_saved_state *state,
                 struct anv_cmd_buffer *cmd_buffer)
{
   cmd_buffer->state.pipeline = state->old_pipeline;
   cmd_buffer->state.descriptors[0] = state->old_descriptor_set0;
   memcpy(cmd_buffer->state.vertex_bindings, state->old_vertex_bindings,
          sizeof(state->old_vertex_bindings));

   cmd_buffer->state.vb_dirty |= (1 << ANV_META_VERTEX_BINDING_COUNT) - 1;
   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_PIPELINE;
   cmd_buffer->state.descriptors_dirty |= VK_SHADER_STAGE_FRAGMENT_BIT;

   anv_dynamic_state_copy(&cmd_buffer->state.dynamic, &state->dynamic,
                          state->dynamic_mask);
   cmd_buffer->state.dirty |= state->dynamic_mask;

   /* Since we've used the pipeline with the VS disabled, set
    * need_query_wa. See CmdBeginQuery.
    */
   cmd_buffer->state.need_query_wa = true;
}

VkImageViewType
anv_meta_get_view_type(const struct anv_image *image)
{
   switch (image->type) {
   case VK_IMAGE_TYPE_1D: return VK_IMAGE_VIEW_TYPE_1D;
   case VK_IMAGE_TYPE_2D: return VK_IMAGE_VIEW_TYPE_2D;
   case VK_IMAGE_TYPE_3D: return VK_IMAGE_VIEW_TYPE_3D;
   default:
      unreachable("bad VkImageViewType");
   }
}

/**
 * When creating a destination VkImageView, this function provides the needed
 * VkImageViewCreateInfo::subresourceRange::baseArrayLayer.
 */
uint32_t
anv_meta_get_iview_layer(const struct anv_image *dest_image,
                         const VkImageSubresourceLayers *dest_subresource,
                         const VkOffset3D *dest_offset)
{
   switch (dest_image->type) {
   case VK_IMAGE_TYPE_1D:
   case VK_IMAGE_TYPE_2D:
      return dest_subresource->baseArrayLayer;
   case VK_IMAGE_TYPE_3D:
      /* HACK: Vulkan does not allow attaching a 3D image to a framebuffer,
       * but meta does it anyway. When doing so, we translate the
       * destination's z offset into an array offset.
       */
      return dest_offset->z;
   default:
      assert(!"bad VkImageType");
      return 0;
   }
}

static void *
meta_alloc(void* _device, size_t size, size_t alignment,
           VkSystemAllocationScope allocationScope)
{
   struct anv_device *device = _device;
   return device->alloc.pfnAllocation(device->alloc.pUserData, size, alignment,
                                      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
}

static void *
meta_realloc(void* _device, void *original, size_t size, size_t alignment,
             VkSystemAllocationScope allocationScope)
{
   struct anv_device *device = _device;
   return device->alloc.pfnReallocation(device->alloc.pUserData, original,
                                        size, alignment,
                                        VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
}

static void
meta_free(void* _device, void *data)
{
   struct anv_device *device = _device;
   return device->alloc.pfnFree(device->alloc.pUserData, data);
}

VkResult
anv_device_init_meta(struct anv_device *device)
{
   VkResult result;

   device->meta_state.alloc = (VkAllocationCallbacks) {
      .pUserData = device,
      .pfnAllocation = meta_alloc,
      .pfnReallocation = meta_realloc,
      .pfnFree = meta_free,
   };

   result = anv_device_init_meta_clear_state(device);
   if (result != VK_SUCCESS)
      goto fail_clear;

   result = anv_device_init_meta_resolve_state(device);
   if (result != VK_SUCCESS)
      goto fail_resolve;

   result = anv_device_init_meta_blit_state(device);
   if (result != VK_SUCCESS)
      goto fail_blit;

   result = anv_device_init_meta_blit2d_state(device);
   if (result != VK_SUCCESS)
      goto fail_blit2d;

   return VK_SUCCESS;

fail_blit2d:
   anv_device_finish_meta_blit_state(device);
fail_blit:
   anv_device_finish_meta_resolve_state(device);
fail_resolve:
   anv_device_finish_meta_clear_state(device);
fail_clear:
   return result;
}

void
anv_device_finish_meta(struct anv_device *device)
{
   anv_device_finish_meta_resolve_state(device);
   anv_device_finish_meta_clear_state(device);
   anv_device_finish_meta_blit_state(device);
   anv_device_finish_meta_blit2d_state(device);
}
