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

#include "anv_private.h"

VkResult anv_CreateRenderPass(
    VkDevice                                    _device,
    const VkRenderPassCreateInfo*               pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_render_pass *pass;
   size_t size;
   size_t attachments_offset;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);

   size = sizeof(*pass);
   size += pCreateInfo->subpassCount * sizeof(pass->subpasses[0]);
   attachments_offset = size;
   size += pCreateInfo->attachmentCount * sizeof(pass->attachments[0]);

   pass = vk_alloc2(&device->alloc, pAllocator, size, 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pass == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Clear the subpasses along with the parent pass. This required because
    * each array member of anv_subpass must be a valid pointer if not NULL.
    */
   memset(pass, 0, size);
   pass->attachment_count = pCreateInfo->attachmentCount;
   pass->subpass_count = pCreateInfo->subpassCount;
   pass->attachments = (void *) pass + attachments_offset;

   pass->subpass_usages =
      vk_zalloc2(&device->alloc, pAllocator,
                 pass->subpass_count * pass->attachment_count *
                                       sizeof(*pass->subpass_usages),
                 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pass->subpass_usages == NULL) {
      vk_free2(&device->alloc, pAllocator, pass);
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   enum anv_subpass_usage *usages = pass->subpass_usages;
   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      struct anv_render_pass_attachment *att = &pass->attachments[i];

      att->format = pCreateInfo->pAttachments[i].format;
      att->samples = pCreateInfo->pAttachments[i].samples;
      att->usage = 0;
      att->load_op = pCreateInfo->pAttachments[i].loadOp;
      att->store_op = pCreateInfo->pAttachments[i].storeOp;
      att->stencil_load_op = pCreateInfo->pAttachments[i].stencilLoadOp;
      att->initial_layout = pCreateInfo->pAttachments[i].initialLayout;
      att->final_layout = pCreateInfo->pAttachments[i].finalLayout;
      att->subpass_usage = usages;
      usages += pass->subpass_count;
   }

   uint32_t subpass_attachment_count = 0, *p;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription *desc = &pCreateInfo->pSubpasses[i];

      subpass_attachment_count +=
         desc->inputAttachmentCount +
         desc->colorAttachmentCount +
         /* Count colorAttachmentCount again for resolve_attachments */
         desc->colorAttachmentCount;
   }

   pass->subpass_attachments =
      vk_alloc2(&device->alloc, pAllocator,
                 subpass_attachment_count * sizeof(uint32_t), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pass->subpass_attachments == NULL) {
      vk_free2(&device->alloc, pAllocator, pass->subpass_usages);
      vk_free2(&device->alloc, pAllocator, pass);
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   p = pass->subpass_attachments;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription *desc = &pCreateInfo->pSubpasses[i];
      struct anv_subpass *subpass = &pass->subpasses[i];

      subpass->input_count = desc->inputAttachmentCount;
      subpass->color_count = desc->colorAttachmentCount;

      if (desc->inputAttachmentCount > 0) {
         subpass->input_attachments = p;
         p += desc->inputAttachmentCount;

         for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
            uint32_t a = desc->pInputAttachments[j].attachment;
            subpass->input_attachments[j] = a;
            if (a != VK_ATTACHMENT_UNUSED) {
               pass->attachments[a].usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
               pass->attachments[a].subpass_usage[i] |= ANV_SUBPASS_USAGE_INPUT;
               pass->attachments[a].last_subpass_idx = i;

               if (desc->pDepthStencilAttachment &&
                   a == desc->pDepthStencilAttachment->attachment)
                  subpass->has_ds_self_dep = true;
            }
         }
      }

      if (desc->colorAttachmentCount > 0) {
         subpass->color_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            uint32_t a = desc->pColorAttachments[j].attachment;
            subpass->color_attachments[j] = a;
            if (a != VK_ATTACHMENT_UNUSED) {
               pass->attachments[a].usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
               pass->attachments[a].subpass_usage[i] |= ANV_SUBPASS_USAGE_DRAW;
               pass->attachments[a].last_subpass_idx = i;
            }
         }
      }

      subpass->has_resolve = false;
      if (desc->pResolveAttachments) {
         subpass->resolve_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            uint32_t a = desc->pResolveAttachments[j].attachment;
            subpass->resolve_attachments[j] = a;
            if (a != VK_ATTACHMENT_UNUSED) {
               subpass->has_resolve = true;
               uint32_t color_att = desc->pColorAttachments[j].attachment;
               pass->attachments[color_att].usage |=
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
               pass->attachments[a].usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

               pass->attachments[color_att].subpass_usage[i] |=
                  ANV_SUBPASS_USAGE_RESOLVE_SRC;
               pass->attachments[a].subpass_usage[i] |=
                  ANV_SUBPASS_USAGE_RESOLVE_DST;
               pass->attachments[a].last_subpass_idx = i;
            }
         }
      }

      if (desc->pDepthStencilAttachment) {
         uint32_t a = desc->pDepthStencilAttachment->attachment;
         subpass->depth_stencil_attachment = a;
         subpass->depth_stencil_layout =
            desc->pDepthStencilAttachment->layout;
         if (a != VK_ATTACHMENT_UNUSED) {
            pass->attachments[a].usage |=
               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            pass->attachments[a].subpass_usage[i] |= ANV_SUBPASS_USAGE_DRAW;
            pass->attachments[a].last_subpass_idx = i;
         }
      } else {
         subpass->depth_stencil_attachment = VK_ATTACHMENT_UNUSED;
         subpass->depth_stencil_layout = VK_IMAGE_LAYOUT_UNDEFINED;
      }
   }

   *pRenderPass = anv_render_pass_to_handle(pass);

   return VK_SUCCESS;
}

void anv_DestroyRenderPass(
    VkDevice                                    _device,
    VkRenderPass                                _pass,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_render_pass, pass, _pass);

   if (!pass)
      return;

   vk_free2(&device->alloc, pAllocator, pass->subpass_attachments);
   vk_free2(&device->alloc, pAllocator, pass->subpass_usages);
   vk_free2(&device->alloc, pAllocator, pass);
}

void anv_GetRenderAreaGranularity(
    VkDevice                                    device,
    VkRenderPass                                renderPass,
    VkExtent2D*                                 pGranularity)
{
   ANV_FROM_HANDLE(anv_render_pass, pass, renderPass);

   /* This granularity satisfies HiZ fast clear alignment requirements
    * for all sample counts.
    */
   for (unsigned i = 0; i < pass->subpass_count; ++i) {
      if (pass->subpasses[i].depth_stencil_attachment !=
          VK_ATTACHMENT_UNUSED) {
         *pGranularity = (VkExtent2D) { .width = 8, .height = 4 };
         return;
      }
   }

   *pGranularity = (VkExtent2D) { 1, 1 };
}
