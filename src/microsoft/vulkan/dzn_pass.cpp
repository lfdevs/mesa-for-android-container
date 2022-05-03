/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "dzn_private.h"

#include "vk_alloc.h"
#include "vk_format.h"

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateRenderPass2(VkDevice dev,
                      const VkRenderPassCreateInfo2KHR *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkRenderPass *pRenderPass)
{
   VK_FROM_HANDLE(dzn_device, device, dev);

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, dzn_render_pass, pass, 1);
   VK_MULTIALLOC_DECL(&ma, dzn_subpass, subpasses,
                      pCreateInfo->subpassCount);
   VK_MULTIALLOC_DECL(&ma, dzn_attachment, attachments,
                      pCreateInfo->attachmentCount);

   if (!vk_multialloc_zalloc2(&ma, &device->vk.alloc, pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pass->base, VK_OBJECT_TYPE_RENDER_PASS);
   pass->subpasses = subpasses;
   pass->subpass_count = pCreateInfo->subpassCount;
   pass->attachments = attachments;
   pass->attachment_count = pCreateInfo->attachmentCount;

   assert(!pass->attachment_count || pass->attachments);
   for (uint32_t i = 0; i < pass->attachment_count; i++) {
      const VkAttachmentDescription2 *attachment = &pCreateInfo->pAttachments[i];

      attachments[i].idx = i;
      attachments[i].format = attachment->format;
      assert(attachments[i].format);
      if (vk_format_is_depth_or_stencil(attachment->format)) {
         attachments[i].clear.depth =
            attachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR;
         attachments[i].clear.stencil =
            attachment->stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR;
      } else {
         attachments[i].clear.color =
            attachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR;
      }
      attachments[i].samples = attachment->samples;
      if (vk_format_has_stencil(attachment->format)) {
         attachments[i].stencil.before =
            dzn_image_layout_to_state(attachment->initialLayout, VK_IMAGE_ASPECT_STENCIL_BIT);
         attachments[i].stencil.after =
            dzn_image_layout_to_state(attachment->finalLayout, VK_IMAGE_ASPECT_STENCIL_BIT);
         attachments[i].stencil.last = attachments[i].stencil.before;
      }

      if (vk_format_has_depth(attachment->format)) {
         attachments[i].before =
            dzn_image_layout_to_state(attachment->initialLayout, VK_IMAGE_ASPECT_STENCIL_BIT);
         attachments[i].after =
            dzn_image_layout_to_state(attachment->finalLayout, VK_IMAGE_ASPECT_STENCIL_BIT);
         attachments[i].last = attachments[i].before;
      } else {
         assert(vk_format_is_color(attachment->format));
         attachments[i].before =
            dzn_image_layout_to_state(attachment->initialLayout, VK_IMAGE_ASPECT_COLOR_BIT);
         attachments[i].after =
            dzn_image_layout_to_state(attachment->finalLayout, VK_IMAGE_ASPECT_COLOR_BIT);
         attachments[i].last = attachments[i].before;
      }
   }

   assert(subpasses);
   for (uint32_t i = 0; i < pass->subpass_count; i++) {
      const VkSubpassDescription2 *subpass = &pCreateInfo->pSubpasses[i];
      const VkSubpassDescription2 *subpass_after = NULL;

      if (i + 1 < pass->subpass_count)
         subpass_after = &pCreateInfo->pSubpasses[i + 1];

      for (uint32_t j = 0; j < subpass->colorAttachmentCount; j++) {
         uint32_t idx = subpass->pColorAttachments[j].attachment;
         subpasses[i].colors[j].idx = idx;
         if (idx != VK_ATTACHMENT_UNUSED) {
            subpasses[i].colors[j].aspects = VK_IMAGE_ASPECT_COLOR_BIT;
            subpasses[i].colors[j].before = attachments[idx].last;
            subpasses[i].colors[j].during =
               dzn_image_layout_to_state(subpass->pColorAttachments[j].layout,
                                         VK_IMAGE_ASPECT_COLOR_BIT);
            attachments[idx].last = subpasses[i].colors[j].during;
            attachments[idx].aspects |= VK_IMAGE_ASPECT_COLOR_BIT;
            subpasses[i].color_count = j + 1;
         }

         idx = subpass->pResolveAttachments ?
               subpass->pResolveAttachments[j].attachment :
               VK_ATTACHMENT_UNUSED;
         subpasses[i].resolve[j].idx = idx;
         if (idx != VK_ATTACHMENT_UNUSED) {
            subpasses[i].resolve[j].aspects = VK_IMAGE_ASPECT_COLOR_BIT;
            subpasses[i].resolve[j].before = attachments[idx].last;
            subpasses[i].resolve[j].during =
               dzn_image_layout_to_state(subpass->pResolveAttachments[j].layout,
                                         VK_IMAGE_ASPECT_COLOR_BIT);
            attachments[idx].last = subpasses[i].resolve[j].during;
            attachments[idx].aspects |= VK_IMAGE_ASPECT_COLOR_BIT;
         }
      }

      subpasses[i].zs.idx = VK_ATTACHMENT_UNUSED;
      if (subpass->pDepthStencilAttachment) {
         uint32_t idx = subpass->pDepthStencilAttachment->attachment;
         subpasses[i].zs.idx = idx;
         if (idx != VK_ATTACHMENT_UNUSED) {
            subpasses[i].zs.aspects = vk_format_aspects(attachments[idx].format);
            subpasses[i].zs.before = attachments[idx].last;
            subpasses[i].zs.during = attachments[idx].last;
            subpasses[i].zs.stencil.before = attachments[idx].stencil.last;
            subpasses[i].zs.stencil.during = attachments[idx].stencil.last;

            if (subpasses[i].zs.aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
               subpasses[i].zs.stencil.during =
                  dzn_image_layout_to_state(subpass->pDepthStencilAttachment->layout,
                                            VK_IMAGE_ASPECT_STENCIL_BIT);
            }

            if (subpasses[i].zs.aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
               subpasses[i].zs.during =
                  dzn_image_layout_to_state(subpass->pDepthStencilAttachment->layout,
                                            VK_IMAGE_ASPECT_DEPTH_BIT);
            }

            attachments[idx].last = subpasses[i].zs.during;
            attachments[idx].stencil.last = subpasses[i].zs.stencil.during;
            attachments[idx].aspects |= subpasses[i].zs.aspects;
         }
      }

      subpasses[i].input_count = subpass->inputAttachmentCount;
      for (uint32_t j = 0; j < subpasses[i].input_count; j++) {
         uint32_t idx = subpass->pInputAttachments[j].attachment;
         subpasses[i].inputs[j].idx = idx;
         if (idx != VK_ATTACHMENT_UNUSED) {
            subpasses[i].inputs[j].aspects = subpass->pInputAttachments[j].aspectMask;
            subpasses[i].inputs[j].before = attachments[idx].last;
            subpasses[i].inputs[j].during = attachments[idx].last;
            subpasses[i].inputs[j].stencil.before = attachments[idx].stencil.last;
            subpasses[i].inputs[j].stencil.during = attachments[idx].stencil.last;

            if (subpasses[i].inputs[j].aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
               subpasses[i].inputs[j].stencil.during =
                  dzn_image_layout_to_state(subpass->pInputAttachments[j].layout,
                                            VK_IMAGE_ASPECT_STENCIL_BIT);
            }

            if (subpasses[i].inputs[j].aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
               subpasses[i].inputs[j].during =
                  dzn_image_layout_to_state(subpass->pInputAttachments[j].layout,
                                            VK_IMAGE_ASPECT_DEPTH_BIT);
               attachments[idx].last = subpasses[i].inputs[j].during;
            } else if (subpasses[i].inputs[j].aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
               subpasses[i].inputs[j].during =
                  dzn_image_layout_to_state(subpass->pInputAttachments[j].layout,
                                            VK_IMAGE_ASPECT_COLOR_BIT);
            }

            attachments[idx].last = subpasses[i].inputs[j].during;
            attachments[idx].stencil.last = subpasses[i].inputs[j].stencil.during;
            attachments[idx].aspects |= subpass->pInputAttachments[j].aspectMask;
         }
      }
   }

   *pRenderPass = dzn_render_pass_to_handle(pass);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroyRenderPass(VkDevice dev,
                      VkRenderPass p,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(dzn_device, device, dev);
   VK_FROM_HANDLE(dzn_render_pass, pass, p);

   if (!pass)
      return;

   vk_object_base_finish(&pass->base);
   vk_free2(&device->vk.alloc, pAllocator, pass);
}


VKAPI_ATTR void VKAPI_CALL
dzn_GetRenderAreaGranularity(VkDevice device,
                             VkRenderPass pass,
                             VkExtent2D *pGranularity)
{
   // FIXME: query the actual optimal granularity
   pGranularity->width = pGranularity->height = 1;
}
