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

#include "vk_util.h"

static unsigned
num_subpass_attachments(const VkSubpassDescription *desc)
{
   return desc->inputAttachmentCount +
          desc->colorAttachmentCount +
          (desc->pResolveAttachments ? desc->colorAttachmentCount : 0) +
          (desc->pDepthStencilAttachment != NULL);
}

static void
init_first_subpass_layout(struct anv_render_pass_attachment * const att,
                          const VkAttachmentReference att_ref)
{
   if (att->first_subpass_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
      att->first_subpass_layout = att_ref.layout;
      assert(att->first_subpass_layout != VK_IMAGE_LAYOUT_UNDEFINED);
   }
}

VkResult anv_CreateRenderPass(
    VkDevice                                    _device,
    const VkRenderPassCreateInfo*               pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);

   struct anv_render_pass *pass;
   struct anv_subpass *subpasses;
   struct anv_render_pass_attachment *attachments;
   enum anv_pipe_bits *subpass_flushes;

   ANV_MULTIALLOC(ma);
   anv_multialloc_add(&ma, &pass, 1);
   anv_multialloc_add(&ma, &subpasses, pCreateInfo->subpassCount);
   anv_multialloc_add(&ma, &attachments, pCreateInfo->attachmentCount);
   anv_multialloc_add(&ma, &subpass_flushes, pCreateInfo->subpassCount + 1);

   struct anv_subpass_attachment *subpass_attachments;
   uint32_t subpass_attachment_count = 0;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      subpass_attachment_count +=
         num_subpass_attachments(&pCreateInfo->pSubpasses[i]);
   }
   anv_multialloc_add(&ma, &subpass_attachments, subpass_attachment_count);

   if (!anv_multialloc_alloc2(&ma, &device->alloc, pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Clear the subpasses along with the parent pass. This required because
    * each array member of anv_subpass must be a valid pointer if not NULL.
    */
   memset(pass, 0, ma.size);
   pass->attachment_count = pCreateInfo->attachmentCount;
   pass->subpass_count = pCreateInfo->subpassCount;
   pass->attachments = attachments;
   pass->subpass_flushes = subpass_flushes;

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
      att->first_subpass_layout = VK_IMAGE_LAYOUT_UNDEFINED;
   }

   bool has_color = false, has_depth = false, has_input = false;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription *desc = &pCreateInfo->pSubpasses[i];
      struct anv_subpass *subpass = &pass->subpasses[i];

      subpass->input_count = desc->inputAttachmentCount;
      subpass->color_count = desc->colorAttachmentCount;
      subpass->attachment_count = num_subpass_attachments(desc);
      subpass->attachments = subpass_attachments;
      subpass->view_mask = 0;

      if (desc->inputAttachmentCount > 0) {
         subpass->input_attachments = subpass_attachments;
         subpass_attachments += desc->inputAttachmentCount;

         for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
            uint32_t a = desc->pInputAttachments[j].attachment;
            subpass->input_attachments[j] = (struct anv_subpass_attachment) {
               .usage =       VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
               .attachment =  desc->pInputAttachments[j].attachment,
               .layout =      desc->pInputAttachments[j].layout,
            };
            if (a != VK_ATTACHMENT_UNUSED) {
               has_input = true;
               pass->attachments[a].usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
               pass->attachments[a].last_subpass_idx = i;

               init_first_subpass_layout(&pass->attachments[a],
                                         desc->pInputAttachments[j]);
               if (desc->pDepthStencilAttachment &&
                   a == desc->pDepthStencilAttachment->attachment)
                  subpass->has_ds_self_dep = true;
            }
         }
      }

      if (desc->colorAttachmentCount > 0) {
         subpass->color_attachments = subpass_attachments;
         subpass_attachments += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            uint32_t a = desc->pColorAttachments[j].attachment;
            subpass->color_attachments[j] = (struct anv_subpass_attachment) {
               .usage =       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
               .attachment =  desc->pColorAttachments[j].attachment,
               .layout =      desc->pColorAttachments[j].layout,
            };
            if (a != VK_ATTACHMENT_UNUSED) {
               has_color = true;
               pass->attachments[a].usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
               pass->attachments[a].last_subpass_idx = i;

               init_first_subpass_layout(&pass->attachments[a],
                                         desc->pColorAttachments[j]);
            }
         }
      }

      subpass->has_resolve = false;
      if (desc->pResolveAttachments) {
         subpass->resolve_attachments = subpass_attachments;
         subpass_attachments += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            uint32_t a = desc->pResolveAttachments[j].attachment;
            subpass->resolve_attachments[j] = (struct anv_subpass_attachment) {
               .usage =       VK_IMAGE_USAGE_TRANSFER_DST_BIT,
               .attachment =  desc->pResolveAttachments[j].attachment,
               .layout =      desc->pResolveAttachments[j].layout,
            };
            if (a != VK_ATTACHMENT_UNUSED) {
               subpass->has_resolve = true;
               uint32_t color_att = desc->pColorAttachments[j].attachment;
               pass->attachments[color_att].usage |=
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
               pass->attachments[a].usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
               pass->attachments[a].last_subpass_idx = i;

               init_first_subpass_layout(&pass->attachments[a],
                                         desc->pResolveAttachments[j]);
            }
         }
      }

      if (desc->pDepthStencilAttachment) {
         uint32_t a = desc->pDepthStencilAttachment->attachment;
         subpass->depth_stencil_attachment = (struct anv_subpass_attachment) {
            .usage =       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .attachment =  desc->pDepthStencilAttachment->attachment,
            .layout =      desc->pDepthStencilAttachment->layout,
         };
         *subpass_attachments++ = subpass->depth_stencil_attachment;
         if (a != VK_ATTACHMENT_UNUSED) {
            has_depth = true;
            pass->attachments[a].usage |=
               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            pass->attachments[a].last_subpass_idx = i;

            init_first_subpass_layout(&pass->attachments[a],
                                      *desc->pDepthStencilAttachment);
         }
      } else {
         subpass->depth_stencil_attachment = (struct anv_subpass_attachment) {
            .usage =       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .attachment =  VK_ATTACHMENT_UNUSED,
            .layout =   VK_IMAGE_LAYOUT_UNDEFINED,
         };
      }
   }

   for (uint32_t i = 0; i < pCreateInfo->dependencyCount; i++) {
      const VkSubpassDependency *dep = &pCreateInfo->pDependencies[i];
      if (dep->dstSubpass == VK_SUBPASS_EXTERNAL) {
         pass->subpass_flushes[pass->subpass_count] |=
            anv_pipe_invalidate_bits_for_access_flags(dep->dstAccessMask);
      } else {
         assert(dep->dstSubpass < pass->subpass_count);
         pass->subpass_flushes[dep->dstSubpass] |=
            anv_pipe_invalidate_bits_for_access_flags(dep->dstAccessMask);
      }

      if (dep->srcSubpass == VK_SUBPASS_EXTERNAL) {
         pass->subpass_flushes[0] |=
            anv_pipe_flush_bits_for_access_flags(dep->srcAccessMask);
      } else {
         assert(dep->srcSubpass < pass->subpass_count);
         pass->subpass_flushes[dep->srcSubpass + 1] |=
            anv_pipe_flush_bits_for_access_flags(dep->srcAccessMask);
      }
   }

   /* From the Vulkan 1.0.39 spec:
    *
    *    If there is no subpass dependency from VK_SUBPASS_EXTERNAL to the
    *    first subpass that uses an attachment, then an implicit subpass
    *    dependency exists from VK_SUBPASS_EXTERNAL to the first subpass it is
    *    used in. The subpass dependency operates as if defined with the
    *    following parameters:
    *
    *    VkSubpassDependency implicitDependency = {
    *        .srcSubpass = VK_SUBPASS_EXTERNAL;
    *        .dstSubpass = firstSubpass; // First subpass attachment is used in
    *        .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    *        .dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    *        .srcAccessMask = 0;
    *        .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
    *                         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
    *                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
    *                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
    *                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    *        .dependencyFlags = 0;
    *    };
    *
    *    Similarly, if there is no subpass dependency from the last subpass
    *    that uses an attachment to VK_SUBPASS_EXTERNAL, then an implicit
    *    subpass dependency exists from the last subpass it is used in to
    *    VK_SUBPASS_EXTERNAL. The subpass dependency operates as if defined
    *    with the following parameters:
    *
    *    VkSubpassDependency implicitDependency = {
    *        .srcSubpass = lastSubpass; // Last subpass attachment is used in
    *        .dstSubpass = VK_SUBPASS_EXTERNAL;
    *        .srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    *        .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    *        .srcAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
    *                         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
    *                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
    *                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
    *                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    *        .dstAccessMask = 0;
    *        .dependencyFlags = 0;
    *    };
    *
    * We could implement this by walking over all of the attachments and
    * subpasses and checking to see if any of them don't have an external
    * dependency.  Or, we could just be lazy and add a couple extra flushes.
    * We choose to be lazy.
    */
   if (has_input) {
      pass->subpass_flushes[0] |=
         ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT;
   }
   if (has_color) {
      pass->subpass_flushes[pass->subpass_count] |=
         ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT;
   }
   if (has_depth) {
      pass->subpass_flushes[pass->subpass_count] |=
         ANV_PIPE_DEPTH_CACHE_FLUSH_BIT;
   }

   vk_foreach_struct(ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO_KHR: {
         VkRenderPassMultiviewCreateInfoKHR *mv = (void *)ext;

         for (uint32_t i = 0; i < mv->subpassCount; i++) {
            pass->subpasses[i].view_mask = mv->pViewMasks[i];
         }
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
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
      if (pass->subpasses[i].depth_stencil_attachment.attachment !=
          VK_ATTACHMENT_UNUSED) {
         *pGranularity = (VkExtent2D) { .width = 8, .height = 4 };
         return;
      }
   }

   *pGranularity = (VkExtent2D) { 1, 1 };
}
