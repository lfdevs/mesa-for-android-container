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

#include "vk_format.h"
#include "vk_util.h"

static void
anv_render_pass_add_subpass_dep(struct anv_device *device,
                                struct anv_render_pass *pass,
                                const VkSubpassDependency2KHR *dep)
{
   /* From the Vulkan 1.2.195 spec:
    *
    *    "If an instance of VkMemoryBarrier2 is included in the pNext chain,
    *    srcStageMask, dstStageMask, srcAccessMask, and dstAccessMask
    *    parameters are ignored. The synchronization and access scopes instead
    *    are defined by the parameters of VkMemoryBarrier2."
    */
   const VkMemoryBarrier2KHR *barrier =
      vk_find_struct_const(dep->pNext, MEMORY_BARRIER_2_KHR);
   VkAccessFlags2KHR src_access_mask =
      barrier ? barrier->srcAccessMask : dep->srcAccessMask;
   VkAccessFlags2KHR dst_access_mask =
      barrier ? barrier->dstAccessMask : dep->dstAccessMask;

   if (dep->dstSubpass == VK_SUBPASS_EXTERNAL) {
      pass->subpass_flushes[pass->subpass_count] |=
         anv_pipe_invalidate_bits_for_access_flags(device, dst_access_mask);
   } else {
      assert(dep->dstSubpass < pass->subpass_count);
      pass->subpass_flushes[dep->dstSubpass] |=
         anv_pipe_invalidate_bits_for_access_flags(device, dst_access_mask);
   }

   if (dep->srcSubpass == VK_SUBPASS_EXTERNAL) {
      pass->subpass_flushes[0] |=
         anv_pipe_flush_bits_for_access_flags(device, src_access_mask);
   } else {
      assert(dep->srcSubpass < pass->subpass_count);
      pass->subpass_flushes[dep->srcSubpass + 1] |=
         anv_pipe_flush_bits_for_access_flags(device, src_access_mask);
   }
}

/* Do a second "compile" step on a render pass */
static void
anv_render_pass_compile(struct anv_render_pass *pass)
{
   /* The CreateRenderPass code zeros the entire render pass and also uses a
    * designated initializer for filling these out.  There's no need for us to
    * do it again.
    *
    * for (uint32_t i = 0; i < pass->attachment_count; i++) {
    *    pass->attachments[i].usage = 0;
    *    pass->attachments[i].first_subpass_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    * }
    */

   VkImageUsageFlags all_usage = 0;
   for (uint32_t i = 0; i < pass->subpass_count; i++) {
      struct anv_subpass *subpass = &pass->subpasses[i];

      /* We don't allow depth_stencil_attachment to be non-NULL and be
       * VK_ATTACHMENT_UNUSED.  This way something can just check for NULL
       * and be guaranteed that they have a valid attachment.
       */
      if (subpass->depth_stencil_attachment &&
          subpass->depth_stencil_attachment->attachment == VK_ATTACHMENT_UNUSED)
         subpass->depth_stencil_attachment = NULL;

      if (subpass->ds_resolve_attachment &&
          subpass->ds_resolve_attachment->attachment == VK_ATTACHMENT_UNUSED)
         subpass->ds_resolve_attachment = NULL;

      for (uint32_t j = 0; j < subpass->attachment_count; j++) {
         struct anv_subpass_attachment *subpass_att = &subpass->attachments[j];
         if (subpass_att->attachment == VK_ATTACHMENT_UNUSED)
            continue;

         struct anv_render_pass_attachment *pass_att =
            &pass->attachments[subpass_att->attachment];

         pass_att->usage |= subpass_att->usage;
         pass_att->last_subpass_idx = i;

         all_usage |= subpass_att->usage;

         /* first_subpass_layout only applies to color and depth.
          * See genX(cmd_buffer_setup_attachments)
          */
         if (vk_format_aspects(pass_att->format) != VK_IMAGE_ASPECT_STENCIL_BIT &&
             pass_att->first_subpass_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
            pass_att->first_subpass_layout = subpass_att->layout;
            assert(pass_att->first_subpass_layout != VK_IMAGE_LAYOUT_UNDEFINED);
         }

         if (subpass_att->usage == VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT &&
             subpass->depth_stencil_attachment &&
             subpass_att->attachment == subpass->depth_stencil_attachment->attachment)
            subpass->has_ds_self_dep = true;
      }

      /* We have to handle resolve attachments specially */
      subpass->has_color_resolve = false;
      if (subpass->resolve_attachments) {
         for (uint32_t j = 0; j < subpass->color_count; j++) {
            struct anv_subpass_attachment *color_att =
               &subpass->color_attachments[j];
            struct anv_subpass_attachment *resolve_att =
               &subpass->resolve_attachments[j];
            if (resolve_att->attachment == VK_ATTACHMENT_UNUSED)
               continue;

            subpass->has_color_resolve = true;

            assert(color_att->attachment < pass->attachment_count);
            struct anv_render_pass_attachment *color_pass_att =
               &pass->attachments[color_att->attachment];

            assert(resolve_att->usage == VK_IMAGE_USAGE_TRANSFER_DST_BIT);
            assert(color_att->usage == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
            color_pass_att->usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
         }
      }

      if (subpass->ds_resolve_attachment) {
         struct anv_subpass_attachment *ds_att =
            subpass->depth_stencil_attachment;
         UNUSED struct anv_subpass_attachment *resolve_att =
            subpass->ds_resolve_attachment;

         assert(ds_att->attachment < pass->attachment_count);
         struct anv_render_pass_attachment *ds_pass_att =
            &pass->attachments[ds_att->attachment];

         assert(resolve_att->usage == VK_IMAGE_USAGE_TRANSFER_DST_BIT);
         assert(ds_att->usage == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
         ds_pass_att->usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      }

      for (uint32_t j = 0; j < subpass->attachment_count; j++)
         assert(__builtin_popcount(subpass->attachments[j].usage) == 1);
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
    *
    * From the documentation for vkCmdNextSubpass:
    *
    *    "Moving to the next subpass automatically performs any multisample
    *    resolve operations in the subpass being ended. End-of-subpass
    *    multisample resolves are treated as color attachment writes for the
    *    purposes of synchronization. This applies to resolve operations for
    *    both color and depth/stencil attachments. That is, they are
    *    considered to execute in the
    *    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT pipeline stage and
    *    their writes are synchronized with
    *    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT."
    *
    * Therefore, the above flags concerning color attachments also apply to
    * color and depth/stencil resolve attachments.
    */
   if (all_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
      pass->subpass_flushes[0] |=
         ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT;
   }
   if (all_usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
      pass->subpass_flushes[pass->subpass_count] |=
         ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT;
   }
   if (all_usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      pass->subpass_flushes[pass->subpass_count] |=
         ANV_PIPE_DEPTH_CACHE_FLUSH_BIT;
   }
}

static unsigned
num_subpass_attachments2(const VkSubpassDescription2KHR *desc)
{
   const VkSubpassDescriptionDepthStencilResolveKHR *ds_resolve =
      vk_find_struct_const(desc->pNext,
                           SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE_KHR);
   const VkFragmentShadingRateAttachmentInfoKHR *fsr_attachment =
      vk_find_struct_const(desc->pNext,
                           FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR);

   return desc->inputAttachmentCount +
          desc->colorAttachmentCount +
          (desc->pResolveAttachments ? desc->colorAttachmentCount : 0) +
          (desc->pDepthStencilAttachment != NULL) +
          (ds_resolve && ds_resolve->pDepthStencilResolveAttachment) +
          (fsr_attachment != NULL && fsr_attachment->pFragmentShadingRateAttachment);
}

VkResult anv_CreateRenderPass2(
    VkDevice                                    _device,
    const VkRenderPassCreateInfo2KHR*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2_KHR);

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct anv_render_pass, pass, 1);
   VK_MULTIALLOC_DECL(&ma, struct anv_subpass, subpasses,
                           pCreateInfo->subpassCount);
   VK_MULTIALLOC_DECL(&ma, struct anv_render_pass_attachment, attachments,
                           pCreateInfo->attachmentCount);
   VK_MULTIALLOC_DECL(&ma, enum anv_pipe_bits, subpass_flushes,
                           pCreateInfo->subpassCount + 1);

   uint32_t subpass_attachment_count = 0;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      subpass_attachment_count +=
         num_subpass_attachments2(&pCreateInfo->pSubpasses[i]);
   }
   VK_MULTIALLOC_DECL(&ma, struct anv_subpass_attachment, subpass_attachments,
                      subpass_attachment_count);

   if (!vk_object_multizalloc(&device->vk, &ma, pAllocator,
                              VK_OBJECT_TYPE_RENDER_PASS))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Clear the subpasses along with the parent pass. This required because
    * each array member of anv_subpass must be a valid pointer if not NULL.
    */
   pass->attachment_count = pCreateInfo->attachmentCount;
   pass->subpass_count = pCreateInfo->subpassCount;
   pass->attachments = attachments;
   pass->subpass_flushes = subpass_flushes;

   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      pass->attachments[i] = (struct anv_render_pass_attachment) {
         .format                 = pCreateInfo->pAttachments[i].format,
         .samples                = pCreateInfo->pAttachments[i].samples,
         .load_op                = pCreateInfo->pAttachments[i].loadOp,
         .store_op               = pCreateInfo->pAttachments[i].storeOp,
         .stencil_load_op        = pCreateInfo->pAttachments[i].stencilLoadOp,
         .initial_layout         = pCreateInfo->pAttachments[i].initialLayout,
         .final_layout           = pCreateInfo->pAttachments[i].finalLayout,

         .stencil_initial_layout = vk_att_desc_stencil_layout(&pCreateInfo->pAttachments[i],
                                                       false),
         .stencil_final_layout   = vk_att_desc_stencil_layout(&pCreateInfo->pAttachments[i],
                                                       true),
      };
   }

   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription2KHR *desc = &pCreateInfo->pSubpasses[i];
      struct anv_subpass *subpass = &pass->subpasses[i];

      subpass->input_count = desc->inputAttachmentCount;
      subpass->color_count = desc->colorAttachmentCount;
      subpass->attachment_count = num_subpass_attachments2(desc);
      subpass->attachments = subpass_attachments;
      subpass->view_mask = desc->viewMask;

      if (desc->inputAttachmentCount > 0) {
         subpass->input_attachments = subpass_attachments;
         subpass_attachments += desc->inputAttachmentCount;

         for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
            subpass->input_attachments[j] = (struct anv_subpass_attachment) {
               .usage =          VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
               .attachment =     desc->pInputAttachments[j].attachment,
               .layout =         desc->pInputAttachments[j].layout,
               .stencil_layout = vk_att_ref_stencil_layout(&desc->pInputAttachments[j],
                                                           pCreateInfo->pAttachments),
            };
         }
      }

      if (desc->colorAttachmentCount > 0) {
         subpass->color_attachments = subpass_attachments;
         subpass_attachments += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            subpass->color_attachments[j] = (struct anv_subpass_attachment) {
               .usage =       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
               .attachment =  desc->pColorAttachments[j].attachment,
               .layout =      desc->pColorAttachments[j].layout,
            };
         }
      }

      if (desc->pResolveAttachments) {
         subpass->resolve_attachments = subpass_attachments;
         subpass_attachments += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            subpass->resolve_attachments[j] = (struct anv_subpass_attachment) {
               .usage =       VK_IMAGE_USAGE_TRANSFER_DST_BIT,
               .attachment =  desc->pResolveAttachments[j].attachment,
               .layout =      desc->pResolveAttachments[j].layout,
            };
         }
      }

      if (desc->pDepthStencilAttachment) {
         subpass->depth_stencil_attachment = subpass_attachments++;

         *subpass->depth_stencil_attachment = (struct anv_subpass_attachment) {
            .usage =          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .attachment =     desc->pDepthStencilAttachment->attachment,
            .layout =         desc->pDepthStencilAttachment->layout,
            .stencil_layout = vk_att_ref_stencil_layout(desc->pDepthStencilAttachment,
                                                        pCreateInfo->pAttachments),
         };
      }

      const VkSubpassDescriptionDepthStencilResolveKHR *ds_resolve =
         vk_find_struct_const(desc->pNext,
                              SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE_KHR);

      if (ds_resolve && ds_resolve->pDepthStencilResolveAttachment) {
         subpass->ds_resolve_attachment = subpass_attachments++;

         *subpass->ds_resolve_attachment = (struct anv_subpass_attachment) {
            .usage =          VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .attachment =     ds_resolve->pDepthStencilResolveAttachment->attachment,
            .layout =         ds_resolve->pDepthStencilResolveAttachment->layout,
            .stencil_layout = vk_att_ref_stencil_layout(ds_resolve->pDepthStencilResolveAttachment,
                                                        pCreateInfo->pAttachments),
         };
         subpass->depth_resolve_mode = ds_resolve->depthResolveMode;
         subpass->stencil_resolve_mode = ds_resolve->stencilResolveMode;
      }

      const VkFragmentShadingRateAttachmentInfoKHR *fsr_attachment =
         vk_find_struct_const(desc->pNext,
                              FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR);

      if (fsr_attachment && fsr_attachment->pFragmentShadingRateAttachment) {
         subpass->fsr_attachment = subpass_attachments++;

         *subpass->fsr_attachment = (struct anv_subpass_attachment) {
            .usage =          VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR,
            .attachment =     fsr_attachment->pFragmentShadingRateAttachment->attachment,
            .layout =         fsr_attachment->pFragmentShadingRateAttachment->layout,
         };
         subpass->fsr_extent = fsr_attachment->shadingRateAttachmentTexelSize;
      }

   }

   for (uint32_t i = 0; i < pCreateInfo->dependencyCount; i++) {
      anv_render_pass_add_subpass_dep(device, pass,
                                      &pCreateInfo->pDependencies[i]);
   }

   vk_foreach_struct(ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      default:
         anv_debug_ignored_stype(ext->sType);
      }
   }

   anv_render_pass_compile(pass);

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

   vk_object_free(&device->vk, pAllocator, pass);
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
      if (pass->subpasses[i].depth_stencil_attachment) {
         *pGranularity = (VkExtent2D) { .width = 8, .height = 4 };
         return;
      }
   }

   *pGranularity = (VkExtent2D) { 1, 1 };
}

void
anv_dynamic_pass_init(struct anv_dynamic_render_pass *dyn_render_pass,
                      const struct anv_dynamic_pass_create_info *info)
{
   uint32_t att_count;

   att_count = info->colorAttachmentCount;
   if ((info->depthAttachmentFormat != VK_FORMAT_UNDEFINED) ||
       (info->stencilAttachmentFormat != VK_FORMAT_UNDEFINED))
      att_count++;

   struct anv_render_pass *pass = &dyn_render_pass->pass;
   pass->attachment_count = att_count;
   pass->subpass_count = 1;
   pass->attachments = dyn_render_pass->rp_attachments;

   struct anv_subpass *subpass = &dyn_render_pass->subpass;
   subpass->attachment_count = att_count;
   subpass->attachments = dyn_render_pass->sp_attachments;
   if (info->colorAttachmentCount > 0) {
      subpass->color_count = info->colorAttachmentCount;
      subpass->color_attachments = dyn_render_pass->sp_attachments;
   }
   subpass->view_mask = info->viewMask;

   uint32_t att;
   for (att = 0; att < info->colorAttachmentCount; att++) {
      if (info->pColorAttachmentFormats[att] == VK_FORMAT_UNDEFINED)
         continue;
      pass->attachments[att].format = info->pColorAttachmentFormats[att];
      pass->attachments[att].samples = info->rasterizationSamples;
      subpass->attachments[att].attachment = att;
      subpass->attachments[att].usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
   }

   if ((info->depthAttachmentFormat != VK_FORMAT_UNDEFINED) ||
       (info->stencilAttachmentFormat != VK_FORMAT_UNDEFINED)) {
      pass->attachments[att].format = (info->depthAttachmentFormat != VK_FORMAT_UNDEFINED) ? info->depthAttachmentFormat : info->stencilAttachmentFormat;
      pass->attachments[att].samples = info->rasterizationSamples;
      subpass->attachments[att].attachment = att;
      subpass->attachments[att].usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
      subpass->depth_stencil_attachment = &subpass->attachments[att];

      att++;
   }
}

void
anv_dynamic_pass_init_full(struct anv_dynamic_render_pass *dyn_render_pass,
                           const VkRenderingInfoKHR *info)
{
   uint32_t att_count;
   uint32_t color_count = 0, ds_count = 0, fsr_count = 0;
   uint32_t ds_idx, fsr_idx;
   bool has_color_resolve, has_ds_resolve;

   struct anv_render_pass *pass = &dyn_render_pass->pass;
   struct anv_subpass *subpass = &dyn_render_pass->subpass;

   /* We set some of the fields conditionally below, like
    * subpass->ds_resolve_attachment. But the value of this field is used to
    * trigger depth/stencil resolve, so clear things to make sure we don't
    * leave stale values.
    */

   dyn_render_pass->suspending = info->flags & VK_RENDERING_SUSPENDING_BIT_KHR;
   dyn_render_pass->resuming = info->flags & VK_RENDERING_RESUMING_BIT_KHR;

   /* Get the total attachment count by counting color, depth & fragment
    * shading rate views.
    */
   color_count = info->colorAttachmentCount;
   if ((info->pDepthAttachment && info->pDepthAttachment->imageView) ||
       (info->pStencilAttachment && info->pStencilAttachment->imageView))
      ds_count = 1;

   has_color_resolve = false;
   has_ds_resolve = false;
   for (uint32_t i = 0; i < info->colorAttachmentCount; i++) {
      if (info->pColorAttachments[i].resolveMode != VK_RESOLVE_MODE_NONE) {
         has_color_resolve = true;
         break;
      }
   }
   if (has_color_resolve)
      color_count *= 2;

   has_ds_resolve =
      ((info->pDepthAttachment &&
        info->pDepthAttachment->resolveMode != VK_RESOLVE_MODE_NONE) ||
       (info->pStencilAttachment &&
        info->pStencilAttachment->resolveMode != VK_RESOLVE_MODE_NONE));
   if (has_ds_resolve)
      ds_count *= 2;

   const VkRenderingFragmentShadingRateAttachmentInfoKHR *fsr_attachment =
      vk_find_struct_const(info->pNext,
                           RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR);
   if (fsr_attachment && fsr_attachment->imageView != VK_NULL_HANDLE)
      fsr_count = 1;

   att_count = color_count + ds_count + fsr_count;
   ds_idx = color_count;
   fsr_idx = color_count + ds_count;

   /* Setup pass & subpass */
   *pass = (struct anv_render_pass) {
      .subpass_count = 1,
      .attachments = dyn_render_pass->rp_attachments,
      .attachment_count = att_count,
   };

   struct anv_subpass_attachment *subpass_attachments =
      dyn_render_pass->sp_attachments;

   *subpass = (struct anv_subpass) {
      .attachment_count = att_count,
      .attachments = subpass_attachments,
      .color_count = info->colorAttachmentCount,
      .color_attachments = subpass_attachments,
      .has_color_resolve = has_color_resolve,
      .resolve_attachments = subpass_attachments + info->colorAttachmentCount,
      .view_mask = info->viewMask,
   };

   for (uint32_t att = 0; att < info->colorAttachmentCount; att++) {
      if (info->pColorAttachments[att].imageView != VK_NULL_HANDLE) {
         ANV_FROM_HANDLE(anv_image_view, iview, info->pColorAttachments[att].imageView);

         pass->attachments[att]     = (struct anv_render_pass_attachment) {
            .format                 = iview->vk.format,
            .samples                = iview->vk.image->samples,
            .usage                  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
         };
         subpass->color_attachments[att] = (struct anv_subpass_attachment) {
            .usage      = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .attachment = att,
         };
      } else {
         subpass->color_attachments[att] = (struct anv_subpass_attachment) {
            .usage      = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .attachment = VK_ATTACHMENT_UNUSED,
         };
      }

      if (has_color_resolve) {
         if (info->pColorAttachments[att].resolveMode != VK_RESOLVE_MODE_NONE) {
            subpass->resolve_attachments[att] = (struct anv_subpass_attachment) {
               .usage      = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
               .attachment = info->colorAttachmentCount + att,
            };
         } else {
            subpass->resolve_attachments[att] = (struct anv_subpass_attachment) {
               .attachment = VK_ATTACHMENT_UNUSED,
            };
         }
      }
   }

   if (ds_count) {
      /* Easier to reference for the stuff both have in common. */
      const VkRenderingAttachmentInfoKHR *d_att = info->pDepthAttachment;
      const VkRenderingAttachmentInfoKHR *s_att = info->pStencilAttachment;
      const VkRenderingAttachmentInfoKHR *d_or_s_att = d_att ? d_att : s_att;
      VkResolveModeFlagBits depth_resolve_mode = VK_RESOLVE_MODE_NONE;
      VkResolveModeFlagBits stencil_resolve_mode = VK_RESOLVE_MODE_NONE;

      ANV_FROM_HANDLE(anv_image_view, iview, d_or_s_att->imageView);

      pass->attachments[ds_idx] = (struct anv_render_pass_attachment) {
         .format                 = iview->vk.format,
         .samples                = iview->vk.image->samples,
         .usage                  = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      };

      subpass->depth_stencil_attachment = &subpass_attachments[ds_idx];
      *subpass->depth_stencil_attachment = (struct anv_subpass_attachment) {
         .usage            = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
         .attachment       = ds_idx,
      };

      if (d_att && d_att->imageView)
         depth_resolve_mode = d_att->resolveMode;
      if (s_att && s_att->imageView)
         stencil_resolve_mode = s_att->resolveMode;

      if (has_ds_resolve) {
         uint32_t ds_res_idx = ds_idx + 1;

         subpass->ds_resolve_attachment = &subpass_attachments[ds_res_idx];
         *subpass->ds_resolve_attachment = (struct anv_subpass_attachment) {
            .usage            = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .attachment       = ds_res_idx,
         };

         subpass->depth_resolve_mode = depth_resolve_mode;
         subpass->stencil_resolve_mode = stencil_resolve_mode;
      }
   }

   if (fsr_count) {
      ANV_FROM_HANDLE(anv_image_view, iview, fsr_attachment->imageView);

      pass->attachments[fsr_idx] = (struct anv_render_pass_attachment) {
         .format  = iview->vk.format,
         .samples = iview->vk.image->samples,
         .usage   = VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR,
      };

      *subpass->fsr_attachment = (struct anv_subpass_attachment) {
         .usage      = VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR,
         .attachment = fsr_idx,
      };
      subpass->fsr_extent = fsr_attachment->shadingRateAttachmentTexelSize;
   }
}
