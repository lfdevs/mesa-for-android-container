/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_mrt.h"

#include "vk_log.h"

#include "pvr_cmd_buffer.h"
#include "pvr_csb.h"
#include "pvr_device.h"
#include "pvr_formats.h"
#include "pvr_image.h"
#include "pvr_mrt.h"
#include "pvr_pass.h"
#include "pvr_physical_device.h"

#include "hwdef/rogue_hw_utils.h"
#include "util/macros.h"
#include "vulkan/vulkan_core.h"
#include "vulkan/runtime/vk_graphics_state.h"
#include "vulkan/runtime/vk_image.h"

/* Which parts of the output registers/a tile buffer are currently allocated. */
struct pvr_mrt_alloc_mask {
   /* Bit array. A bit is set if the corresponding dword is allocated. */
   BITSET_DECLARE(allocs, 8U);
};

struct pvr_mrt_alloc_ctx {
   /* Which pixel output registers are allocated. */
   struct pvr_mrt_alloc_mask output_reg;

   /* Range of allocated output registers. */
   uint32_t output_regs_count;

   /* Number of tile buffers allocated. */
   uint32_t tile_buffers_count;

   /* Which parts of each tile buffer are allocated. Length is
    * tile_buffers_count.
    */
   struct pvr_mrt_alloc_mask tile_buffers[PVR_MAX_TILE_BUFFER_COUNT];
};

static uint32_t pvr_get_accum_format_bitsize(VkFormat vk_format)
{
   return pvr_get_pbe_accum_format_size_in_bytes(vk_format) * 8;
}

/**
 * Check if there is space in a buffer for storing a render target of a
 * specified size.
 */
static int32_t pvr_mrt_alloc_from_buffer(const struct pvr_device_info *dev_info,
                                         struct pvr_mrt_alloc_mask *buffer,
                                         uint32_t pixel_size)
{
   const uint32_t max_out_regs = rogue_get_max_output_regs_per_pixel(dev_info);
   uint32_t alignment = 1U;

   if (PVR_HAS_FEATURE(dev_info, pbe2_in_xe)) {
      /* For a 64-bit/128-bit source format: the start offset must be even. */
      if (pixel_size == 2U || pixel_size == 4U)
         alignment = 2U;
   }

   assert(pixel_size <= max_out_regs);

   for (uint32_t i = 0U; i <= (max_out_regs - pixel_size); i += alignment) {
      if (!BITSET_TEST_COUNT(buffer->allocs, i, pixel_size)) {
         BITSET_SET_COUNT(buffer->allocs, i, pixel_size);
         return i;
      }
   }

   return -1;
}

void pvr_arch_init_mrt_desc(VkFormat format, struct usc_mrt_desc *desc)
{
   uint32_t pixel_size_in_chunks;
   uint32_t pixel_size_in_bits;

   /* TODO: Add support for packing multiple attachments into the same
    * register
    */
   const uint32_t part_bits = 0;
   if (vk_format_is_color(format) &&
       pvr_arch_get_pbe_accum_format(format) == PVR_PBE_ACCUM_FORMAT_INVALID) {
      /* The VkFormat is not supported as a color attachment so `0`.
       * vulkan doesn't seem to restrict vkCreateRenderPass() to supported
       * formats only.
       */
      pixel_size_in_bits = 0;
   } else {
      /* TODO: handle IMG_PIXFMT_A8_UNORM
       *  For alpha only formats alpha is still placed in channel 3, so channels
       *  0-2 need to be allocated but are left unused
       */
      pixel_size_in_bits = pvr_get_accum_format_bitsize(format);
   }

   desc->intermediate_size = DIV_ROUND_UP(pixel_size_in_bits, CHAR_BIT);

   pixel_size_in_chunks = DIV_ROUND_UP(pixel_size_in_bits, 32U);
   for (uint32_t j = 0U; j < pixel_size_in_chunks; j++)
      desc->valid_mask[j] = ~0;

   if (part_bits > 0U)
      desc->valid_mask[pixel_size_in_chunks] = BITFIELD_MASK(part_bits);
}

static VkResult pvr_alloc_mrt(const struct pvr_device_info *dev_info,
                              struct pvr_mrt_alloc_ctx *alloc,
                              struct usc_mrt_setup *setup,
                              unsigned rt,
                              VkFormat format)
{
   struct usc_mrt_resource *resource = &setup->mrt_resources[rt];

   const uint32_t pixel_size =
      DIV_ROUND_UP(pvr_get_accum_format_bitsize(format), 32U);

   const int32_t output_reg =
      pvr_mrt_alloc_from_buffer(dev_info, &alloc->output_reg, pixel_size);

   if (output_reg != -1) {
      resource->type = USC_MRT_RESOURCE_TYPE_OUTPUT_REG;
      resource->reg.output_reg = output_reg;
      resource->reg.offset = 0;

      alloc->output_regs_count =
         MAX2(alloc->output_regs_count, resource->reg.output_reg + pixel_size);
   } else {
      resource->type = USC_MRT_RESOURCE_TYPE_MEMORY;

      unsigned tib = 0;
      for (; tib < alloc->tile_buffers_count; tib++) {
         struct pvr_mrt_alloc_mask *tib_alloc = &alloc->tile_buffers[tib];

         const int32_t tile_buffer_offset =
            pvr_mrt_alloc_from_buffer(dev_info, tib_alloc, pixel_size);

         if (tile_buffer_offset != -1) {
            resource->mem.tile_buffer = tib;
            resource->mem.offset_dw = tile_buffer_offset;
            break;
         }
      }

      if (tib == alloc->tile_buffers_count) {
         if (alloc->tile_buffers_count == PVR_MAX_TILE_BUFFER_COUNT)
            return vk_error(NULL, VK_ERROR_TOO_MANY_OBJECTS);

         resource->mem.tile_buffer = alloc->tile_buffers_count;
         resource->mem.offset_dw = 0;
      }

      /* If needed a new tile buffer than those that were allocated, then wipe
       * it bump the global count.
       */
      if (resource->mem.tile_buffer >= alloc->tile_buffers_count) {
         memset(
            &alloc->tile_buffers[alloc->tile_buffers_count],
            0U,
            sizeof(alloc->tile_buffers[0U]) *
               (resource->mem.tile_buffer + 1U - alloc->tile_buffers_count));
         alloc->tile_buffers_count = resource->mem.tile_buffer + 1U;

         /* Need to add the resource to the new tile buffer */
         struct pvr_mrt_alloc_mask *tib_alloc =
            &alloc->tile_buffers[resource->mem.tile_buffer];

         ASSERTED const int32_t tile_buffer_offset =
            pvr_mrt_alloc_from_buffer(dev_info,
                                      tib_alloc,
                                      pixel_size);
         assert(tile_buffer_offset == 0);
      }

      /* The hardware makes the bit depth of the on-chip storage and memory
       * storage the same so make sure the memory storage is large enough to
       * accommodate the largest render target.
       */
      alloc->output_regs_count =
         MAX2(alloc->output_regs_count, resource->mem.offset_dw + pixel_size);
   }

   pvr_arch_init_mrt_desc(format, &resource->mrt_desc);
   resource->intermediate_size = resource->mrt_desc.intermediate_size;

   setup->num_render_targets++;

   return VK_SUCCESS;
}

VkResult
pvr_arch_mrt_setup_partial_init(struct pvr_device *const device,
                                struct usc_mrt_setup *mrt_setup,
                                uint32_t num_renger_targets,
                                uint32_t num_output_regs,
                                uint32_t num_tile_buffers)
{
   struct usc_mrt_resource *mrt_resources = NULL;

   if (num_renger_targets) {
      const uint32_t size =
         num_renger_targets * sizeof(*mrt_setup->mrt_resources);

      mrt_resources = vk_zalloc(&device->vk.alloc,
                                size,
                                8,
                                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!mrt_resources)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   *mrt_setup = (struct usc_mrt_setup){
      .num_render_targets = num_renger_targets,
      .num_output_regs = num_output_regs,
      .num_tile_buffers = num_tile_buffers,
      .mrt_resources = mrt_resources,
   };

   return VK_SUCCESS;
}

VkResult
pvr_arch_init_usc_mrt_setup(struct pvr_device *device,
                            uint32_t attachment_count,
                            const VkFormat attachment_formats[attachment_count],
                            struct usc_mrt_setup *setup)
{
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   struct pvr_mrt_alloc_ctx alloc = { 0 };
   VkResult result;

   memset(setup, 0, sizeof(*setup));

   if (!attachment_count)
      goto early_exit;

   setup->mrt_resources =
      vk_alloc(&device->vk.alloc,
               sizeof(*setup->mrt_resources) * attachment_count,
               8U,
               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!setup->mrt_resources)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (unsigned i = 0; i < attachment_count; i++) {
      VkFormat att_format = attachment_formats[i];
      assert(att_format != VK_FORMAT_UNDEFINED);

      result = pvr_alloc_mrt(dev_info, &alloc, setup, i, att_format);
      if (result != VK_SUCCESS) {
         result = vk_error(NULL, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto fail;
      }
   }

early_exit:
   setup->num_output_regs = util_next_power_of_two(alloc.output_regs_count);
   setup->num_tile_buffers = alloc.tile_buffers_count;
   return VK_SUCCESS;
fail:
   vk_free(&device->vk.alloc, setup->mrt_resources);
   return result;
}

void pvr_arch_destroy_mrt_setup(const struct pvr_device *device,
                                struct usc_mrt_setup *setup)
{
   if (!setup)
      return;

   vk_free(&device->vk.alloc, setup->mrt_resources);
}

static bool
pvr_rendering_info_needs_load(const struct pvr_dynamic_render_info *dr_info)
{
   for (unsigned i = 0; i < dr_info->hw_render.color_init_count; i++) {
      const uint32_t index = dr_info->hw_render.color_init[i].index;
      if (index == VK_ATTACHMENT_UNUSED)
         continue;

      const VkAttachmentLoadOp op = dr_info->hw_render.color_init[i].op;
      if (op == VK_ATTACHMENT_LOAD_OP_LOAD || op == VK_ATTACHMENT_LOAD_OP_CLEAR)
         return true;
   }

   return false;
}

static VkResult pvr_mrt_load_op_init(struct pvr_device *device,
                                     const VkAllocationCallbacks *alloc,
                                     const struct pvr_render_pass_info *rp_info,
                                     struct pvr_load_op *load_op,
                                     uint32_t view_idx)
{
   const struct pvr_dynamic_render_info *dr_info = rp_info->dr_info;
   VkResult result;

   load_op->clears_loads_state.depth_clear_to_reg = PVR_NO_DEPTH_CLEAR_TO_REG;

   assert(dr_info->hw_render.color_init_count <=
          PVR_LOAD_OP_CLEARS_LOADS_MAX_RTS);
   for (unsigned i = 0; i < dr_info->hw_render.color_init_count; i++) {
      const struct pvr_renderpass_colorinit *color_init =
         &dr_info->hw_render.color_init[i];
      const struct pvr_image *image;

      assert(color_init->index < rp_info->attachment_count);
      load_op->clears_loads_state.dest_vk_format[i] =
         rp_info->attachments[color_init->index]->vk.view_format;

      image = pvr_image_view_get_image(rp_info->attachments[color_init->index]);
      if (image->vk.samples > VK_SAMPLE_COUNT_1_BIT)
         load_op->clears_loads_state.unresolved_msaa_mask |= BITFIELD_BIT(i);

      if (rp_info->attachments[color_init->index]->vk.view_type ==
             VK_IMAGE_VIEW_TYPE_2D &&
          image->vk.image_type == VK_IMAGE_TYPE_3D) {
         load_op->clears_loads_state.rt_2d_view_3d_mask |= BITFIELD_BIT(i);
      }

      switch (color_init->op) {
      case VK_ATTACHMENT_LOAD_OP_CLEAR:
         load_op->clears_loads_state.rt_clear_mask |= BITFIELD_BIT(i);
         break;
      case VK_ATTACHMENT_LOAD_OP_LOAD:
         load_op->clears_loads_state.rt_load_mask |= BITFIELD_BIT(i);
         break;
      case VK_ATTACHMENT_LOAD_OP_DONT_CARE:
      case VK_ATTACHMENT_LOAD_OP_NONE:
         break;
      default:
         UNREACHABLE("unsupported loadOp");
      }
   }

   load_op->clears_loads_state.mrt_setup = &dr_info->hw_render.init_setup;

   result = pvr_arch_load_op_shader_generate(device, alloc, load_op);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, alloc, load_op);
      return result;
   }

   load_op->view_indices[0] = view_idx;
   load_op->view_count = 1;

   load_op->is_hw_object = true;
   load_op->hw_render = &dr_info->hw_render;

   return VK_SUCCESS;
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

void pvr_arch_mrt_load_op_state_cleanup(const struct pvr_device *device,
                                        const VkAllocationCallbacks *alloc,
                                        struct pvr_load_op_state *state)
{
   if (!state)
      return;

   while (state->load_op_count--) {
      const uint32_t load_op_idx = state->load_op_count;
      struct pvr_load_op *load_op = &state->load_ops[load_op_idx];

      pvr_load_op_fini(load_op);
   }

   vk_free2(&device->vk.alloc, alloc, state);
}

static VkResult
pvr_mrt_load_op_state_create(struct pvr_device *device,
                             const VkAllocationCallbacks *alloc,
                             const struct pvr_render_pass_info *rp_info,
                             struct pvr_load_op_state **state)
{
   const struct pvr_dynamic_render_info *dr_info = rp_info->dr_info;
   const uint32_t view_count = util_bitcount(dr_info->hw_render.view_mask);
   struct pvr_load_op_state *load_op_state;
   struct pvr_load_op *load_ops;
   VkResult result;

   VK_MULTIALLOC(ma);
   vk_multialloc_add(&ma, &load_op_state, __typeof__(*load_op_state), 1);
   vk_multialloc_add(&ma, &load_ops, __typeof__(*load_ops), view_count);

   if (!vk_multialloc_zalloc(&ma, alloc, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   load_op_state->load_ops = load_ops;

   u_foreach_bit (view_idx, dr_info->hw_render.view_mask) {
      struct pvr_load_op *const load_op =
         &load_op_state->load_ops[load_op_state->load_op_count];

      result = pvr_mrt_load_op_init(device, alloc, rp_info, load_op, view_idx);
      if (result != VK_SUCCESS)
         goto err_load_op_state_cleanup;

      load_op_state->load_op_count++;
   }

   *state = load_op_state;

   return VK_SUCCESS;

err_load_op_state_cleanup:
   pvr_arch_mrt_load_op_state_cleanup(device, alloc, load_op_state);

   return result;
}

/* TODO: Can we gaurantee that if we have at least one render target there will
 * be a render target allocated as a REG?
 */
static inline bool
pvr_needs_output_register_writes(const struct usc_mrt_setup *setup)
{
   for (uint32_t i = 0; i < setup->num_render_targets; i++) {
      struct usc_mrt_resource *mrt_resource = &setup->mrt_resources[i];

      if (mrt_resource->type == USC_MRT_RESOURCE_TYPE_OUTPUT_REG)
         return true;
   }

   return false;
}

static inline VkResult
pvr_mrt_add_missing_output_register_write(struct usc_mrt_setup *setup,
                                          const VkAllocationCallbacks *alloc)
{
   const uint32_t last = setup->num_render_targets;
   struct usc_mrt_resource *mrt_resources;

   if (pvr_needs_output_register_writes(setup))
      return VK_SUCCESS;

   setup->num_render_targets++;

   mrt_resources =
      vk_realloc(alloc,
                 setup->mrt_resources,
                 setup->num_render_targets * sizeof(*mrt_resources),
                 8U,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!mrt_resources)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   setup->mrt_resources = mrt_resources;

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

VkResult pvr_arch_mrt_load_ops_setup(struct pvr_cmd_buffer *cmd_buffer,
                                     const VkAllocationCallbacks *alloc,
                                     struct pvr_load_op_state **load_op_state)
{
   const struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   const struct pvr_dynamic_render_info *dr_info =
      state->render_pass_info.dr_info;
   struct pvr_device *device = cmd_buffer->device;
   VkResult result = VK_SUCCESS;

   if (dr_info->mrt_setup->num_tile_buffers) {
      result = pvr_device_tile_buffer_ensure_cap(
         device,
         dr_info->mrt_setup->num_tile_buffers);

      if (result != VK_SUCCESS)
         return result;
   }

   if (!pvr_rendering_info_needs_load(dr_info))
      return VK_SUCCESS;

   result =
      pvr_mrt_add_missing_output_register_write(dr_info->mrt_setup, alloc);
   if (result != VK_SUCCESS)
      return result;

   result = pvr_mrt_load_op_state_create(device,
                                         alloc,
                                         &state->render_pass_info,
                                         load_op_state);

   return result;
}

VkResult pvr_arch_pds_unitex_state_program_create_and_upload(
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

VkResult
pvr_arch_load_op_shader_generate(struct pvr_device *device,
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
   result = pvr_arch_pds_unitex_state_program_create_and_upload(
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
