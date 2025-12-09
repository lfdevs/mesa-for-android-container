/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pvr_pass.h"

#include <stdbool.h>
#include <stdint.h>

#include "hwdef/rogue_hw_utils.h"
#include "pvr_bo.h"
#include "pvr_device.h"
#include "pvr_device_info.h"
#include "pvr_entrypoints.h"
#include "pvr_formats.h"
#include "pvr_hw_pass.h"
#include "pvr_macros.h"
#include "pvr_pds.h"
#include "pvr_physical_device.h"
#include "pvr_pipeline.h"
#include "pvr_types.h"
#include "pvr_usc.h"
#include "util/macros.h"
#include "vk_alloc.h"
#include "vk_format.h"
#include "vk_log.h"
#include "vk_render_pass.h"
#include "vk_util.h"

static inline bool pvr_subpass_has_msaa_input_attachment(
   struct pvr_render_subpass *subpass,
   const VkRenderPassCreateInfo2 *pCreateInfo)
{
   for (uint32_t i = 0; i < subpass->input_count; i++) {
      const uint32_t attachment = subpass->input_attachments[i].attachment_idx;

      if (pCreateInfo->pAttachments[attachment].samples > 1)
         return true;
   }

   return false;
}

static bool pvr_is_subpass_initops_flush_needed(
   const struct pvr_render_pass *pass,
   const struct pvr_renderpass_hwsetup_render *hw_render)
{
   struct pvr_render_subpass *subpass = &pass->subpasses[0];
   uint32_t render_loadop_mask = 0;
   uint32_t color_attachment_mask;

   for (uint32_t i = 0; i < hw_render->color_init_count; i++) {
      if (hw_render->color_init[i].op != VK_ATTACHMENT_LOAD_OP_DONT_CARE)
         render_loadop_mask |= (1 << hw_render->color_init[i].index);
   }

   /* If there are no load ops then there's nothing to flush. */
   if (render_loadop_mask == 0)
      return false;

   /* If the first subpass has any input attachments, they need to be
    * initialized with the result of the load op. Since the input attachment
    * may be read from fragments with an opaque pass type, the load ops must be
    * flushed or else they would be obscured and eliminated by HSR.
    */
   if (subpass->input_count != 0)
      return true;

   color_attachment_mask = 0;

   for (uint32_t i = 0; i < subpass->color_count; i++) {
      const uint32_t color_idx = subpass->color_attachments[i];

      if (color_idx != VK_ATTACHMENT_UNUSED)
         color_attachment_mask |= (1 << pass->attachments[color_idx].index);
   }

   /* If the first subpass does not write to all attachments which have a load
    * op then the load ops need to be flushed to ensure they don't get obscured
    * and removed by HSR.
    */
   return (render_loadop_mask & color_attachment_mask) != render_loadop_mask;
}

static void
pvr_init_subpass_isp_userpass(struct pvr_renderpass_hwsetup *hw_setup,
                              struct pvr_render_pass *pass,
                              struct pvr_render_subpass *subpasses)
{
   uint32_t subpass_idx = 0;

   for (uint32_t i = 0; i < hw_setup->render_count; i++) {
      struct pvr_renderpass_hwsetup_render *hw_render = &hw_setup->renders[i];
      const uint32_t initial_isp_userpass =
         (uint32_t)pvr_is_subpass_initops_flush_needed(pass, hw_render);

      for (uint32_t j = 0; j < hw_render->subpass_count; j++) {
         subpasses[subpass_idx].isp_userpass =
            (j + initial_isp_userpass) & ROGUE_CR_ISP_CTL_UPASS_START_SIZE_MAX;
         subpass_idx++;
      }
   }

   assert(subpass_idx == pass->subpass_count);
}

static inline bool pvr_has_output_register_writes(
   const struct pvr_renderpass_hwsetup_render *hw_render)
{
   for (uint32_t i = 0; i < hw_render->init_setup.num_render_targets; i++) {
      struct usc_mrt_resource *mrt_resource =
         &hw_render->init_setup.mrt_resources[i];

      if (mrt_resource->type == USC_MRT_RESOURCE_TYPE_OUTPUT_REG)
         return true;
   }

   return false;
}

VkResult pvr_pds_unitex_state_program_create_and_upload(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   uint32_t texture_kicks,
   uint32_t uniform_kicks,
   struct pvr_pds_upload *const pds_upload_out)
{
   struct pvr_pds_pixel_shader_sa_program program = {
      .num_texture_dma_kicks = texture_kicks,
      .num_uniform_dma_kicks = uniform_kicks,
   };
   uint32_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   pvr_pds_set_sizes_pixel_shader_uniform_texture_code(&program);

   staging_buffer_size = PVR_DW_TO_BYTES(program.code_size);

   staging_buffer = vk_alloc2(&device->vk.alloc,
                              allocator,
                              staging_buffer_size,
                              8U,
                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   pvr_pds_generate_pixel_shader_sa_code_segment(&program, staging_buffer);

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               NULL,
                               0U,
                               0U,
                               staging_buffer,
                               program.code_size,
                               16U,
                               16U,
                               pds_upload_out);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, allocator, staging_buffer);
      return result;
   }

   vk_free2(&device->vk.alloc, allocator, staging_buffer);

   return VK_SUCCESS;
}

static VkResult pvr_pds_fragment_program_create_and_upload(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   pco_shader *fs,
   struct pvr_suballoc_bo *shader_bo,
   struct pvr_pds_upload *pds_frag_prog,
   bool msaa)
{
   struct pvr_pds_kickusc_program program = { 0 };
   pco_data *fs_data = pco_shader_data(fs);
   uint32_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   const pvr_dev_addr_t exec_addr =
      PVR_DEV_ADDR_OFFSET(shader_bo->dev_addr, fs_data->common.entry_offset);

   /* Note this is not strictly required to be done before calculating the
    * staging_buffer_size in this particular case. It can also be done after
    * allocating the buffer. The size from pvr_pds_kick_usc() is constant.
    */
   pvr_pds_setup_doutu(&program.usc_task_control,
                       exec_addr.addr,
                       fs_data->common.temps,
                       msaa ? ROGUE_PDSINST_DOUTU_SAMPLE_RATE_FULL
                            : ROGUE_PDSINST_DOUTU_SAMPLE_RATE_INSTANCE,
                       fs_data->fs.uses.phase_change);

   pvr_pds_kick_usc(&program, NULL, 0, false, PDS_GENERATE_SIZES);

   staging_buffer_size = PVR_DW_TO_BYTES(program.code_size + program.data_size);

   staging_buffer = vk_alloc2(&device->vk.alloc,
                              allocator,
                              staging_buffer_size,
                              8,
                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   pvr_pds_kick_usc(&program,
                    staging_buffer,
                    0,
                    false,
                    PDS_GENERATE_CODEDATA_SEGMENTS);

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               &staging_buffer[0],
                               program.data_size,
                               16,
                               &staging_buffer[program.data_size],
                               program.code_size,
                               16,
                               16,
                               pds_frag_prog);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, allocator, staging_buffer);
      return result;
   }

   vk_free2(&device->vk.alloc, allocator, staging_buffer);

   return VK_SUCCESS;
}

static VkResult
pvr_load_op_shader_generate(struct pvr_device *device,
                            const VkAllocationCallbacks *allocator,
                            struct pvr_load_op *load_op)
{
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   const uint32_t cache_line_size = pvr_get_slc_cache_line_size(dev_info);

   pco_shader *loadop = pvr_uscgen_loadop(device->pdevice->pco_ctx, load_op);

   VkResult result = pvr_gpu_upload_usc(device,
                                        pco_shader_binary_data(loadop),
                                        pco_shader_binary_size(loadop),
                                        cache_line_size,
                                        &load_op->usc_frag_prog_bo);

   if (result != VK_SUCCESS) {
      ralloc_free(loadop);
      return result;
   }

   const bool msaa = load_op->clears_loads_state.unresolved_msaa_mask &
                     load_op->clears_loads_state.rt_load_mask;

   result =
      pvr_pds_fragment_program_create_and_upload(device,
                                                 allocator,
                                                 loadop,
                                                 load_op->usc_frag_prog_bo,
                                                 &load_op->pds_frag_prog,
                                                 msaa);

   load_op->temps_count = pco_shader_data(loadop)->common.temps;
   ralloc_free(loadop);

   if (result != VK_SUCCESS)
      goto err_free_usc_frag_prog_bo;

   /* Manually hard coding `texture_kicks` to 1 since we'll pack everything into
    * one buffer to be DMAed. See `pvr_load_op_data_create_and_upload()`, where
    * we upload the buffer and upload the code section.
    */
   result = pvr_pds_unitex_state_program_create_and_upload(
      device,
      allocator,
      1U,
      0U,
      &load_op->pds_tex_state_prog);
   if (result != VK_SUCCESS)
      goto err_free_pds_frag_prog;

   return VK_SUCCESS;

err_free_pds_frag_prog:
   pvr_bo_suballoc_free(load_op->pds_frag_prog.pvr_bo);

err_free_usc_frag_prog_bo:
   pvr_bo_suballoc_free(load_op->usc_frag_prog_bo);

   return result;
}

/* TODO: pvr_subpass_load_op_init() and pvr_render_load_op_init() are quite
 * similar. See if we can dedup them?
 */
static VkResult
pvr_subpass_load_op_init(struct pvr_device *device,
                         const VkAllocationCallbacks *allocator,
                         const struct pvr_render_pass *pass,
                         struct pvr_renderpass_hwsetup_render *hw_render,
                         uint32_t hw_subpass_idx)
{
   const struct pvr_renderpass_hwsetup_subpass *hw_subpass =
      &hw_render->subpasses[hw_subpass_idx];
   const struct pvr_render_subpass *subpass =
      &pass->subpasses[hw_subpass->index];
   struct pvr_load_op *load_op;
   VkResult result;

   load_op = vk_zalloc2(&device->vk.alloc,
                        allocator,
                        sizeof(*load_op),
                        8,
                        VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!load_op)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   load_op->clears_loads_state.depth_clear_to_reg = PVR_NO_DEPTH_CLEAR_TO_REG;

   if (hw_subpass->z_replicate != -1) {
      const int32_t z_replicate = hw_subpass->z_replicate;

      switch (hw_subpass->depth_initop) {
      case VK_ATTACHMENT_LOAD_OP_LOAD:
         assert(z_replicate < PVR_LOAD_OP_CLEARS_LOADS_MAX_RTS);
         load_op->clears_loads_state.rt_load_mask = BITFIELD_BIT(z_replicate);

         assert(subpass->depth_stencil_attachment != VK_ATTACHMENT_UNUSED);
         load_op->clears_loads_state.dest_vk_format[z_replicate] =
            pass->attachments[subpass->depth_stencil_attachment].vk_format;

         break;

      case VK_ATTACHMENT_LOAD_OP_CLEAR:
         load_op->clears_loads_state.depth_clear_to_reg = z_replicate;
         break;

      default:
         break;
      }
   }

   assert(subpass->color_count <= PVR_LOAD_OP_CLEARS_LOADS_MAX_RTS);
   for (uint32_t i = 0; i < subpass->color_count; i++) {
      const uint32_t attachment_idx = subpass->color_attachments[i];

      assert(attachment_idx < pass->attachment_count);
      load_op->clears_loads_state.dest_vk_format[i] =
         pass->attachments[attachment_idx].vk_format;

      if (pass->attachments[attachment_idx].sample_count > 1)
         load_op->clears_loads_state.unresolved_msaa_mask |= BITFIELD_BIT(i);

      if (hw_subpass->color_initops[i] == VK_ATTACHMENT_LOAD_OP_LOAD)
         load_op->clears_loads_state.rt_load_mask |= BITFIELD_BIT(i);
      else if (hw_subpass->color_initops[i] == VK_ATTACHMENT_LOAD_OP_CLEAR)
         load_op->clears_loads_state.rt_clear_mask |= BITFIELD_BIT(i);
   }

   load_op->is_hw_object = false;
   load_op->subpass = subpass;
   load_op->clears_loads_state.mrt_setup = &hw_subpass->setup;

   result = pvr_load_op_shader_generate(device, allocator, load_op);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, allocator, load_op);
      return result;
   }

   load_op->view_count = 0;
   u_foreach_bit (view_idx, hw_render->view_mask) {
      load_op->view_indices[load_op->view_count] = view_idx;
      load_op->view_count++;
   }

   hw_render->subpasses[hw_subpass_idx].load_op = load_op;

   return VK_SUCCESS;
}

struct pvr_per_view_attachment_first_use_info {
   uint32_t *first_subpass[PVR_MAX_MULTIVIEW];
   uint32_t *first_subpass_memory;
};

/**
 * \brief Returns true if a clear op is needed instead of the hw render reported
 * load op load.
 *
 * The hw render isn't aware of multiview renders so it thinks we're reusing the
 * attachment of a previous subpass even if it's the first time the attachment
 * is used in the render pass, so a clear op gets reported as a load op load
 * instead.
 */
/* FIXME: Investigate whether we can change the HW render code so it reports
 * the correct load operation. This will mean we can get rid of struct
 * pvr_per_view_attachment_first_use_info and struct pvr_load_op_state.
 * Instead we'll be able to have a single render struct load_op like we do for
 * subpasses.
 */
static bool pvr_render_load_op_multiview_load_should_be_clear(
   const struct pvr_render_pass *pass,
   const struct pvr_renderpass_hwsetup_render *hw_render,
   uint32_t hw_render_index,
   const struct pvr_renderpass_colorinit *color_init,
   const struct pvr_per_view_attachment_first_use_info *first_use_info,
   uint32_t view_index)
{
   uint32_t first_use_view_index;

   if (!pass->multiview_enabled)
      return false;

   /* Check we have a load op load to see if we might need to correct the hw
    * render.
    */
   if (color_init->op != VK_ATTACHMENT_LOAD_OP_LOAD)
      return false;

   first_use_view_index =
      first_use_info->first_subpass[view_index][color_init->index];

   /* Check that we're looking at the render where the attachment is used for
    * the first time.
    */
   if (first_use_view_index != hw_render_index)
      return false;

   /* Check that the original load op was a clear op. */
   if (pass->attachments[color_init->index].load_op !=
       VK_ATTACHMENT_LOAD_OP_CLEAR) {
      return false;
   }

   return true;
}

static VkResult pvr_render_load_op_init(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   struct pvr_load_op *const load_op,
   const struct pvr_render_pass *pass,
   const struct pvr_renderpass_hwsetup_render *hw_render,
   uint32_t hw_render_index,
   uint32_t view_index,
   const struct pvr_per_view_attachment_first_use_info *first_use_info)
{
   load_op->clears_loads_state.depth_clear_to_reg = PVR_NO_DEPTH_CLEAR_TO_REG;

   assert(hw_render->color_init_count <= PVR_LOAD_OP_CLEARS_LOADS_MAX_RTS);
   for (uint32_t i = 0; i < hw_render->color_init_count; i++) {
      struct pvr_renderpass_colorinit *color_init = &hw_render->color_init[i];
      bool multiview_load_op_should_be_clear = false;

      assert(color_init->index < pass->attachment_count);
      load_op->clears_loads_state.dest_vk_format[i] =
         pass->attachments[color_init->index].vk_format;

      if (pass->attachments[color_init->index].sample_count > 1)
         load_op->clears_loads_state.unresolved_msaa_mask |= BITFIELD_BIT(i);

      multiview_load_op_should_be_clear =
         pvr_render_load_op_multiview_load_should_be_clear(pass,
                                                           hw_render,
                                                           hw_render_index,
                                                           color_init,
                                                           first_use_info,
                                                           view_index);

      if (color_init->op == VK_ATTACHMENT_LOAD_OP_CLEAR ||
          multiview_load_op_should_be_clear) {
         load_op->clears_loads_state.rt_clear_mask |= BITFIELD_BIT(i);
      } else if (color_init->op == VK_ATTACHMENT_LOAD_OP_LOAD) {
         load_op->clears_loads_state.rt_load_mask |= BITFIELD_BIT(i);
      }
   }

   load_op->is_hw_object = true;
   load_op->hw_render = hw_render;
   load_op->clears_loads_state.mrt_setup = &hw_render->init_setup;
   load_op->view_indices[0] = view_index;
   load_op->view_count = 1;

   return pvr_load_op_shader_generate(device, allocator, load_op);
}

static void pvr_load_op_fini(struct pvr_load_op *load_op)
{
   pvr_bo_suballoc_free(load_op->pds_tex_state_prog.pvr_bo);
   pvr_bo_suballoc_free(load_op->pds_frag_prog.pvr_bo);
   pvr_bo_suballoc_free(load_op->usc_frag_prog_bo);
}

static void pvr_load_op_destroy(struct pvr_device *device,
                                const VkAllocationCallbacks *allocator,
                                struct pvr_load_op *load_op)
{
   pvr_load_op_fini(load_op);
   vk_free2(&device->vk.alloc, allocator, load_op);
}

static void
pvr_render_load_op_state_destroy(struct pvr_device *device,
                                 const VkAllocationCallbacks *pAllocator,
                                 struct pvr_load_op_state *load_op_state)
{
   if (!load_op_state)
      return;

   while (load_op_state->load_op_count--) {
      const uint32_t load_op_idx = load_op_state->load_op_count;
      struct pvr_load_op *load_op = &load_op_state->load_ops[load_op_idx];

      pvr_load_op_fini(load_op);
   }

   vk_free2(&device->vk.alloc, pAllocator, load_op_state);
}

static VkResult pvr_render_load_op_state_create(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   const struct pvr_render_pass *pass,
   const struct pvr_renderpass_hwsetup_render *hw_render,
   uint32_t hw_render_index,
   const struct pvr_per_view_attachment_first_use_info *first_use_info,
   struct pvr_load_op_state **const load_op_state_out)
{
   const uint32_t view_count = util_bitcount(hw_render->view_mask);
   struct pvr_load_op_state *load_op_state;
   struct pvr_load_op *load_ops;
   VkResult result;

   VK_MULTIALLOC(ma);
   vk_multialloc_add(&ma, &load_op_state, __typeof__(*load_op_state), 1);
   vk_multialloc_add(&ma, &load_ops, __typeof__(*load_ops), view_count);

   if (!vk_multialloc_zalloc(&ma, allocator, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   load_op_state->load_ops = load_ops;

   u_foreach_bit (view_idx, hw_render->view_mask) {
      struct pvr_load_op *const load_op =
         &load_op_state->load_ops[load_op_state->load_op_count];

      result = pvr_render_load_op_init(device,
                                       allocator,
                                       load_op,
                                       pass,
                                       hw_render,
                                       hw_render_index,
                                       view_idx,
                                       first_use_info);
      if (result != VK_SUCCESS)
         goto err_load_op_state_destroy;

      load_op_state->load_op_count++;
   }

   *load_op_state_out = load_op_state;

   return VK_SUCCESS;

err_load_op_state_destroy:
   pvr_render_load_op_state_destroy(device, allocator, load_op_state);

   return result;
}

#define PVR_SPM_LOAD_IN_BUFFERS_COUNT(dev_info)              \
   ({                                                        \
      int __ret = PVR_MAX_TILE_BUFFER_COUNT;                 \
      if (PVR_HAS_FEATURE(dev_info, eight_output_registers)) \
         __ret -= 4U;                                        \
      __ret;                                                 \
   })

static bool
pvr_is_load_op_needed(const struct pvr_render_pass *pass,
                      struct pvr_renderpass_hwsetup_render *hw_render,
                      const uint32_t subpass_idx)
{
   struct pvr_renderpass_hwsetup_subpass *hw_subpass =
      &hw_render->subpasses[subpass_idx];
   const struct pvr_render_subpass *subpass =
      &pass->subpasses[hw_subpass->index];

   if (hw_subpass->z_replicate != -1 &&
       (hw_subpass->depth_initop == VK_ATTACHMENT_LOAD_OP_LOAD ||
        hw_subpass->depth_initop == VK_ATTACHMENT_LOAD_OP_CLEAR)) {
      return true;
   }

   for (uint32_t i = 0; i < subpass->color_count; i++) {
      if (subpass->color_attachments[i] == VK_ATTACHMENT_UNUSED)
         continue;

      if (hw_subpass->color_initops[i] == VK_ATTACHMENT_LOAD_OP_LOAD ||
          hw_subpass->color_initops[i] == VK_ATTACHMENT_LOAD_OP_CLEAR) {
         return true;
      }
   }

   return false;
}

static VkResult pvr_per_view_attachment_first_use_info_init(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   struct pvr_render_pass *pass,
   struct pvr_per_view_attachment_first_use_info *first_use_info)
{
   size_t alloc_size;

   if (!pass->attachment_count) {
      memset(first_use_info, 0, sizeof(*first_use_info));

      return VK_SUCCESS;
   }

   STATIC_ASSERT(ARRAY_SIZE(first_use_info->first_subpass) ==
                 PVR_MAX_MULTIVIEW);

   alloc_size =
      sizeof(first_use_info->first_subpass_memory[0]) * pass->attachment_count;
   alloc_size *= ARRAY_SIZE(first_use_info->first_subpass);

   first_use_info->first_subpass_memory =
      vk_zalloc2(&device->vk.alloc,
                 allocator,
                 alloc_size,
                 4,
                 VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!first_use_info->first_subpass_memory)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

#define PVR_SUBPASS_INVALID (~0U)
   for (uint32_t i = 0; i < ARRAY_SIZE(first_use_info->first_subpass); i++) {
      first_use_info->first_subpass[i] =
         &first_use_info->first_subpass_memory[i * pass->attachment_count];

      for (uint32_t j = 0; j < pass->attachment_count; j++)
         first_use_info->first_subpass[i][j] = PVR_SUBPASS_INVALID;
   }

   for (uint32_t subpass_idx = 0; subpass_idx < pass->subpass_count;
        subpass_idx++) {
      struct pvr_render_subpass *const subpass = &pass->subpasses[subpass_idx];

      u_foreach_bit (view_idx, subpass->view_mask) {
         for (uint32_t i = 0; i < subpass->color_count; i++) {
            const uint32_t attach_idx = subpass->color_attachments[i];
            uint32_t *first_use =
               &first_use_info->first_subpass[view_idx][attach_idx];

            if (attach_idx < pass->attachment_count &&
                *first_use == PVR_SUBPASS_INVALID) {
               *first_use = subpass_idx;
            }
         }

         for (uint32_t i = 0; i < subpass->input_count; i++) {
            const uint32_t input_attach_idx =
               subpass->input_attachments[i].attachment_idx;
            uint32_t *first_use =
               &first_use_info->first_subpass[view_idx][input_attach_idx];

            if (input_attach_idx < pass->attachment_count &&
                *first_use == PVR_SUBPASS_INVALID) {
               *first_use = subpass_idx;
            }
         }

         if (subpass->depth_stencil_attachment != VK_ATTACHMENT_UNUSED) {
            const uint32_t ds_attach_idx = subpass->depth_stencil_attachment;
            uint32_t *first_use =
               &first_use_info->first_subpass[view_idx][ds_attach_idx];

            if (*first_use == PVR_SUBPASS_INVALID)
               *first_use = subpass_idx;
         }
      }
   }
#undef PVR_SUBPASS_INVALID

   return VK_SUCCESS;
}

static inline void pvr_per_view_attachment_first_use_info_fini(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   struct pvr_per_view_attachment_first_use_info *first_use_info)
{
   vk_free2(&device->vk.alloc, allocator, first_use_info->first_subpass_memory);
}

static inline VkResult pvr_render_add_missing_output_register_write(
   struct pvr_renderpass_hwsetup_render *hw_render,
   const VkAllocationCallbacks *allocator)
{
   const uint32_t last = hw_render->init_setup.num_render_targets;
   struct usc_mrt_resource *mrt_resources;

   /* Add a dummy output register use to the HW render setup if it has no
    * output registers in use.
    */
   if (pvr_has_output_register_writes(hw_render))
      return VK_SUCCESS;

   hw_render->init_setup.num_render_targets++;

   mrt_resources = vk_realloc(allocator,
                              hw_render->init_setup.mrt_resources,
                              hw_render->init_setup.num_render_targets *
                                 sizeof(*mrt_resources),
                              8U,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!mrt_resources)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   hw_render->init_setup.mrt_resources = mrt_resources;

   mrt_resources[last].type = USC_MRT_RESOURCE_TYPE_OUTPUT_REG;
   mrt_resources[last].reg.output_reg = 0U;
   mrt_resources[last].reg.offset = 0U;
   mrt_resources[last].intermediate_size = 4U;
   mrt_resources[last].mrt_desc.intermediate_size = 4U;
   mrt_resources[last].mrt_desc.priority = 0U;
   mrt_resources[last].mrt_desc.valid_mask[0U] = ~0;
   mrt_resources[last].mrt_desc.valid_mask[1U] = ~0;
   mrt_resources[last].mrt_desc.valid_mask[2U] = ~0;
   mrt_resources[last].mrt_desc.valid_mask[3U] = ~0;

   return VK_SUCCESS;
}

static inline void
pvr_subpass_load_op_cleanup(struct pvr_device *device,
                            const VkAllocationCallbacks *allocator,
                            struct pvr_renderpass_hwsetup_render *hw_render,
                            uint32_t subpass_count)
{
   while (subpass_count--) {
      const uint32_t subpass_idx = subpass_count;

      if (hw_render->subpasses[subpass_idx].load_op) {
         pvr_load_op_destroy(device,
                             allocator,
                             hw_render->subpasses[subpass_idx].load_op);
      }
   }
}

static inline VkResult
pvr_subpass_load_op_setup(struct pvr_device *device,
                          const VkAllocationCallbacks *allocator,
                          struct pvr_render_pass *pass,
                          struct pvr_renderpass_hwsetup_render *hw_render)
{
   for (uint32_t i = 0; i < hw_render->subpass_count; i++) {
      VkResult result;

      if (!pvr_is_load_op_needed(pass, hw_render, i))
         continue;

      result = pvr_subpass_load_op_init(device, allocator, pass, hw_render, i);
      if (result != VK_SUCCESS) {
         /* pvr_subpass_load_op_setup() is responsible for cleaning
          * up all load_ops created in this loop for this hw_render.
          */
         pvr_subpass_load_op_cleanup(device, allocator, hw_render, i);
         return result;
      }
   }

   return VK_SUCCESS;
}

static inline VkResult pvr_hw_render_load_ops_setup(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   struct pvr_render_pass *pass,
   struct pvr_renderpass_hwsetup_render *hw_render,
   uint32_t hw_render_idx,
   struct pvr_per_view_attachment_first_use_info *first_use_info)
{
   VkResult result;

   if (hw_render->tile_buffers_count) {
      result = pvr_device_tile_buffer_ensure_cap(
         device,
         hw_render->tile_buffers_count,
         hw_render->eot_setup.tile_buffer_size);
      if (result != VK_SUCCESS)
         return result;
   }

   assert(!hw_render->load_op_state);

   if (hw_render->color_init_count != 0U) {
      struct pvr_load_op_state *load_op_state = NULL;

      result =
         pvr_render_add_missing_output_register_write(hw_render, allocator);
      if (result != VK_SUCCESS)
         return result;

      result = pvr_render_load_op_state_create(device,
                                               allocator,
                                               pass,
                                               hw_render,
                                               hw_render_idx,
                                               first_use_info,
                                               &load_op_state);
      if (result != VK_SUCCESS)
         return result;

      hw_render->load_op_state = load_op_state;
   }

   result = pvr_subpass_load_op_setup(device, allocator, pass, hw_render);
   if (result != VK_SUCCESS) {
      /* pvr_hw_render_load_ops_setup() is responsible for cleaning up only
       * one load_op_state for this hw_render.
       */
      pvr_render_load_op_state_destroy(device,
                                       allocator,
                                       hw_render->load_op_state);
      return result;
   }

   return VK_SUCCESS;
}

static void
pvr_render_pass_load_ops_cleanup(struct pvr_device *device,
                                 const VkAllocationCallbacks *allocator,
                                 struct pvr_render_pass *pass,
                                 uint32_t hw_render_count)
{
   while (hw_render_count--) {
      const uint32_t hw_render_idx = hw_render_count;
      struct pvr_renderpass_hwsetup_render *hw_render =
         &pass->hw_setup->renders[hw_render_idx];

      pvr_subpass_load_op_cleanup(device,
                                  allocator,
                                  hw_render,
                                  hw_render->subpass_count);
      pvr_render_load_op_state_destroy(device,
                                       allocator,
                                       hw_render->load_op_state);
   }
}

static VkResult
pvr_render_pass_load_ops_setup(struct pvr_device *device,
                               const VkAllocationCallbacks *allocator,
                               struct pvr_render_pass *pass)
{
   struct pvr_per_view_attachment_first_use_info first_use_info;
   uint32_t hw_render_idx;
   VkResult result;

   result = pvr_per_view_attachment_first_use_info_init(device,
                                                        allocator,
                                                        pass,
                                                        &first_use_info);
   if (result != VK_SUCCESS)
      goto err_return;

   for (hw_render_idx = 0; hw_render_idx < pass->hw_setup->render_count;
        hw_render_idx++) {
      struct pvr_renderpass_hwsetup_render *hw_render =
         &pass->hw_setup->renders[hw_render_idx];

      result = pvr_hw_render_load_ops_setup(device,
                                            allocator,
                                            pass,
                                            hw_render,
                                            hw_render_idx,
                                            &first_use_info);
      if (result != VK_SUCCESS)
         goto err_pvr_render_pass_load_ops_cleanup;
   }

   pvr_per_view_attachment_first_use_info_fini(device,
                                               allocator,
                                               &first_use_info);

   return VK_SUCCESS;

err_pvr_render_pass_load_ops_cleanup:
   /* pvr_render_pass_load_ops_setup() is responsible for cleaning
    * up all load_ops created in this loop for each hw_render.
    */
   pvr_render_pass_load_ops_cleanup(device, allocator, pass, hw_render_idx);

   pvr_per_view_attachment_first_use_info_fini(device,
                                               allocator,
                                               &first_use_info);

err_return:
   return result;
}

VkResult pvr_CreateRenderPass2(VkDevice _device,
                               const VkRenderPassCreateInfo2 *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkRenderPass *pRenderPass)
{
   struct pvr_render_pass_attachment *attachments;
   VK_FROM_HANDLE(pvr_device, device, _device);
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   struct pvr_render_subpass *subpasses;
   const VkAllocationCallbacks *alloc;
   size_t subpass_attachment_count;
   size_t subpass_input_attachment_count;
   struct pvr_render_input_attachment *subpass_input_attachments;
   uint32_t *subpass_attachments;
   struct pvr_render_pass *pass;
   uint32_t *dep_list;
   bool *flush_on_dep;
   VkResult result;

   alloc = pAllocator ? pAllocator : &device->vk.alloc;

   VK_MULTIALLOC(ma);
   vk_multialloc_add(&ma, &pass, __typeof__(*pass), 1);
   vk_multialloc_add(&ma,
                     &attachments,
                     __typeof__(*attachments),
                     pCreateInfo->attachmentCount);
   vk_multialloc_add(&ma,
                     &subpasses,
                     __typeof__(*subpasses),
                     pCreateInfo->subpassCount);

   subpass_attachment_count = 0;
   subpass_input_attachment_count = 0;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription2 *desc = &pCreateInfo->pSubpasses[i];
      subpass_attachment_count +=
         desc->colorAttachmentCount +
         (desc->pResolveAttachments ? desc->colorAttachmentCount : 0);
      subpass_input_attachment_count += desc->inputAttachmentCount;
   }

   vk_multialloc_add(&ma,
                     &subpass_attachments,
                     __typeof__(*subpass_attachments),
                     subpass_attachment_count);
   vk_multialloc_add(&ma,
                     &subpass_input_attachments,
                     __typeof__(*subpass_input_attachments),
                     subpass_input_attachment_count);
   vk_multialloc_add(&ma,
                     &dep_list,
                     __typeof__(*dep_list),
                     pCreateInfo->dependencyCount);
   vk_multialloc_add(&ma,
                     &flush_on_dep,
                     __typeof__(*flush_on_dep),
                     pCreateInfo->dependencyCount);

   if (!vk_multialloc_zalloc(&ma, alloc, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pass->base, VK_OBJECT_TYPE_RENDER_PASS);
   pass->attachment_count = pCreateInfo->attachmentCount;
   pass->attachments = attachments;
   pass->subpass_count = pCreateInfo->subpassCount;
   pass->subpasses = subpasses;
   pass->max_sample_count = 1;

   /* Copy attachment descriptions. */
   for (uint32_t i = 0; i < pass->attachment_count; i++) {
      const VkAttachmentDescription2 *desc = &pCreateInfo->pAttachments[i];
      struct pvr_render_pass_attachment *attachment = &pass->attachments[i];

      pvr_assert(!(desc->flags & ~VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT));

      attachment->load_op = desc->loadOp;
      attachment->store_op = desc->storeOp;

      attachment->aspects = vk_format_aspects(desc->format);
      if (attachment->aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
         attachment->stencil_load_op = desc->stencilLoadOp;
         attachment->stencil_store_op = desc->stencilStoreOp;
      }

      attachment->vk_format = desc->format;
      attachment->sample_count = desc->samples;
      attachment->initial_layout = desc->initialLayout;
      attachment->index = i;

      /* On cores without gs_rta_support, PBE resolves might depend on writes
       * that occur within the deferred RTA clears that happen after the PBE has
       * written. Since the driver doesn't know at renderpass creation whether
       * RTA clears are needed, PBE resolves can't be used.
       */
      attachment->is_pbe_downscalable =
         PVR_HAS_FEATURE(dev_info, gs_rta_support) &&
         pvr_format_is_pbe_downscalable(&device->pdevice->dev_info,
                                        attachment->vk_format);

      if (attachment->sample_count > pass->max_sample_count)
         pass->max_sample_count = attachment->sample_count;
   }

   /* Count how many dependencies each subpass has. */
   for (uint32_t i = 0; i < pCreateInfo->dependencyCount; i++) {
      const VkSubpassDependency2 *dep = &pCreateInfo->pDependencies[i];

      if (dep->srcSubpass != VK_SUBPASS_EXTERNAL &&
          dep->dstSubpass != VK_SUBPASS_EXTERNAL &&
          dep->srcSubpass != dep->dstSubpass) {
         pass->subpasses[dep->dstSubpass].dep_count++;
      }
   }

   /* Multiview is considered enabled for all subpasses when the viewMask
    * of them all isn't 0. Assume this now and assert later that it holds
    * for each subpass viewMask.
    */
   pass->multiview_enabled = pass->subpass_count &&
                             pCreateInfo->pSubpasses[0].viewMask;

   /* Assign reference pointers to lists, and fill in the attachments list, we
    * need to re-walk the dependencies array later to fill the per-subpass
    * dependencies lists in.
    */
   for (uint32_t i = 0; i < pass->subpass_count; i++) {
      const VkSubpassDescription2 *desc = &pCreateInfo->pSubpasses[i];
      struct pvr_render_subpass *subpass = &pass->subpasses[i];
      const VkSubpassDescriptionDepthStencilResolve *resolve_desc =
         vk_find_struct_const(desc->pNext,
                              SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE);

      subpass->pipeline_bind_point = desc->pipelineBindPoint;
      subpass->view_mask = desc->viewMask;

      assert(!pass->multiview_enabled || subpass->view_mask);

      if (!pass->multiview_enabled)
         subpass->view_mask = 1;

      /* From the Vulkan spec. 1.3.265
       * VUID-VkSubpassDescription2-multisampledRenderToSingleSampled-06872:
       *
       *   "If none of the VK_AMD_mixed_attachment_samples extension, the
       *   VK_NV_framebuffer_mixed_samples extension, or the
       *   multisampledRenderToSingleSampled feature are enabled, all
       *   attachments in pDepthStencilAttachment or pColorAttachments that are
       *   not VK_ATTACHMENT_UNUSED must have the same sample count"
       *
       */
      subpass->sample_count = VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;

      if (desc->pDepthStencilAttachment) {
         uint32_t index = desc->pDepthStencilAttachment->attachment;

         if (index != VK_ATTACHMENT_UNUSED)
            subpass->sample_count = pass->attachments[index].sample_count;

         subpass->depth_stencil_attachment = index;
      } else {
         subpass->depth_stencil_attachment = VK_ATTACHMENT_UNUSED;
      }

      subpass->depth_stencil_resolve_attachment = VK_ATTACHMENT_UNUSED;

      if (resolve_desc) {
         uint32_t index = VK_ATTACHMENT_UNUSED;

         if (resolve_desc->pDepthStencilResolveAttachment)
            index = resolve_desc->pDepthStencilResolveAttachment->attachment;
         else if (subpass->depth_stencil_attachment != VK_ATTACHMENT_UNUSED)
            index = subpass->depth_stencil_attachment;

         if (index != VK_ATTACHMENT_UNUSED) {
            const VkFormat format = pCreateInfo->pAttachments[index].format;
            const bool stencil_has_store = vk_format_has_stencil(format) &&
               (pass->attachments[index].stencil_store_op ==
                VK_ATTACHMENT_STORE_OP_STORE);
            const bool depth_has_store = vk_format_has_depth(format) &&
               (pass->attachments[index].store_op ==
                VK_ATTACHMENT_STORE_OP_STORE);

            if (stencil_has_store || depth_has_store) {
               subpass->stencil_resolve_mode = resolve_desc->stencilResolveMode;
               subpass->depth_resolve_mode = resolve_desc->depthResolveMode;
               subpass->depth_stencil_resolve_attachment = index;
            }
         }
      }

      subpass->color_count = desc->colorAttachmentCount;
      if (subpass->color_count > 0) {
         subpass->color_attachments = subpass_attachments;
         subpass_attachments += subpass->color_count;

         for (uint32_t j = 0; j < subpass->color_count; j++) {
            subpass->color_attachments[j] =
               desc->pColorAttachments[j].attachment;

            if (subpass->color_attachments[j] == VK_ATTACHMENT_UNUSED)
               continue;

            if (subpass->sample_count == VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM) {
               uint32_t index;
               index = subpass->color_attachments[j];
               subpass->sample_count = pass->attachments[index].sample_count;
            }
         }
      }

      if (subpass->sample_count == VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM)
         subpass->sample_count = VK_SAMPLE_COUNT_1_BIT;

      if (desc->pResolveAttachments) {
         subpass->resolve_attachments = subpass_attachments;
         subpass_attachments += subpass->color_count;

         for (uint32_t j = 0; j < subpass->color_count; j++) {
            subpass->resolve_attachments[j] =
               desc->pResolveAttachments[j].attachment;
         }
      }

      subpass->input_count = desc->inputAttachmentCount;
      if (subpass->input_count > 0) {
         subpass->input_attachments = subpass_input_attachments;
         subpass_input_attachments += subpass->input_count;

         for (uint32_t j = 0; j < subpass->input_count; j++) {
            subpass->input_attachments[j].attachment_idx =
               desc->pInputAttachments[j].attachment;
            subpass->input_attachments[j].aspect_mask =
               desc->pInputAttachments[j].aspectMask;
         }
      }

      /* Give the dependencies a slice of the subpass_attachments array. */
      subpass->dep_list = dep_list;
      dep_list += subpass->dep_count;
      subpass->flush_on_dep = flush_on_dep;
      flush_on_dep += subpass->dep_count;

      /* Reset the dependencies count so we can start from 0 and index into
       * the dependencies array.
       */
      subpass->dep_count = 0;
      subpass->index = i;
   }

   /* Compute dependencies and populate dep_list and flush_on_dep. */
   for (uint32_t i = 0; i < pCreateInfo->dependencyCount; i++) {
      const VkSubpassDependency2 *dep = &pCreateInfo->pDependencies[i];

      if (dep->srcSubpass != VK_SUBPASS_EXTERNAL &&
          dep->dstSubpass != VK_SUBPASS_EXTERNAL &&
          dep->srcSubpass != dep->dstSubpass) {
         struct pvr_render_subpass *subpass = &pass->subpasses[dep->dstSubpass];
         const bool is_dep_fb_local =
            vk_subpass_dependency_is_fb_local(dep,
                                              dep->srcStageMask,
                                              dep->dstStageMask);
         const bool dst_has_resolve = subpass->stencil_resolve_mode ||
                                      subpass->depth_resolve_mode;
         const bool src_has_resolve =
            pass->subpasses[dep->srcSubpass].stencil_resolve_mode ||
            pass->subpasses[dep->srcSubpass].depth_resolve_mode;

         subpass->dep_list[subpass->dep_count] = dep->srcSubpass;
         if (dst_has_resolve || src_has_resolve ||
             pvr_subpass_has_msaa_input_attachment(subpass, pCreateInfo) ||
             !is_dep_fb_local) {
            subpass->flush_on_dep[subpass->dep_count] = true;
         }

         subpass->dep_count++;
      }
   }

   pass->max_tilebuffer_count =
      PVR_SPM_LOAD_IN_BUFFERS_COUNT(&device->pdevice->dev_info);

   result =
      pvr_create_renderpass_hwsetup(device, alloc, pass, false, &pass->hw_setup);
   if (result != VK_SUCCESS)
      goto err_free_pass;

   pvr_init_subpass_isp_userpass(pass->hw_setup, pass, pass->subpasses);

   result = pvr_render_pass_load_ops_setup(device, alloc, pass);
   if (result != VK_SUCCESS)
      goto err_destroy_renderpass_hwsetup;

   *pRenderPass = pvr_render_pass_to_handle(pass);

   return VK_SUCCESS;

err_destroy_renderpass_hwsetup:
   pvr_destroy_renderpass_hwsetup(alloc, pass->hw_setup);

err_free_pass:
   vk_object_base_finish(&pass->base);
   vk_free2(&device->vk.alloc, pAllocator, pass);

   return result;
}

void pvr_DestroyRenderPass(VkDevice _device,
                           VkRenderPass _pass,
                           const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_render_pass, pass, _pass);
   const VkAllocationCallbacks *allocator = pAllocator ? pAllocator
                                                       : &device->vk.alloc;

   if (!pass)
      return;

   pvr_render_pass_load_ops_cleanup(device,
                                    allocator,
                                    pass,
                                    pass->hw_setup->render_count);
   pvr_destroy_renderpass_hwsetup(allocator, pass->hw_setup);
   vk_object_base_finish(&pass->base);
   vk_free2(&device->vk.alloc, pAllocator, pass);
}

void pvr_GetRenderAreaGranularity(VkDevice _device,
                                  VkRenderPass renderPass,
                                  VkExtent2D *pGranularity)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;

   /* Granularity does not depend on any settings in the render pass, so return
    * the tile granularity.
    *
    * The default value is based on the minimum value found in all existing
    * cores.
    */
   pGranularity->width = PVR_GET_FEATURE_VALUE(dev_info, tile_size_x, 16);
   pGranularity->height = PVR_GET_FEATURE_VALUE(dev_info, tile_size_y, 16);
}
