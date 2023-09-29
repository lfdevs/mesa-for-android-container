/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_pipeline.h"

#include "common/freedreno_guardband.h"

#include "ir3/ir3_nir.h"
#include "main/menums.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_serialize.h"
#include "spirv/nir_spirv.h"
#include "util/u_debug.h"
#include "util/mesa-sha1.h"
#include "vk_pipeline.h"
#include "vk_render_pass.h"
#include "vk_util.h"

#include "tu_cmd_buffer.h"
#include "tu_cs.h"
#include "tu_device.h"
#include "tu_knl.h"
#include "tu_formats.h"
#include "tu_lrz.h"
#include "tu_pass.h"

/* Emit IB that preloads the descriptors that the shader uses */

static void
emit_load_state(struct tu_cs *cs, unsigned opcode, enum a6xx_state_type st,
                enum a6xx_state_block sb, unsigned base, unsigned offset,
                unsigned count)
{
   /* Note: just emit one packet, even if count overflows NUM_UNIT. It's not
    * clear if emitting more packets will even help anything. Presumably the
    * descriptor cache is relatively small, and these packets stop doing
    * anything when there are too many descriptors.
    */
   tu_cs_emit_pkt7(cs, opcode, 3);
   tu_cs_emit(cs,
              CP_LOAD_STATE6_0_STATE_TYPE(st) |
              CP_LOAD_STATE6_0_STATE_SRC(SS6_BINDLESS) |
              CP_LOAD_STATE6_0_STATE_BLOCK(sb) |
              CP_LOAD_STATE6_0_NUM_UNIT(MIN2(count, 1024-1)));
   tu_cs_emit_qw(cs, offset | (base << 28));
}

static unsigned
tu6_load_state_size(struct tu_pipeline *pipeline,
                    struct tu_pipeline_layout *layout)
{
   const unsigned load_state_size = 4;
   unsigned size = 0;
   for (unsigned i = 0; i < layout->num_sets; i++) {
      if (!(pipeline->active_desc_sets & (1u << i)))
         continue;

      struct tu_descriptor_set_layout *set_layout = layout->set[i].layout;
      for (unsigned j = 0; j < set_layout->binding_count; j++) {
         struct tu_descriptor_set_binding_layout *binding = &set_layout->binding[j];
         unsigned count = 0;
         /* See comment in tu6_emit_load_state(). */
         VkShaderStageFlags stages = pipeline->active_stages & binding->shader_stages;
         unsigned stage_count = util_bitcount(stages);

         if (!binding->array_size)
            continue;

         switch (binding->type) {
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            /* IBO-backed resources only need one packet for all graphics stages */
            if (stage_count)
               count += 1;
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLER:
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            /* Textures and UBO's needs a packet for each stage */
            count = stage_count;
            break;
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            /* Because of how we pack combined images and samplers, we
             * currently can't use one packet for the whole array.
             */
            count = stage_count * binding->array_size * 2;
            break;
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
         case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
            break;
         default:
            unreachable("bad descriptor type");
         }
         size += count * load_state_size;
      }
   }
   return size;
}

static void
tu6_emit_load_state(struct tu_pipeline *pipeline,
                    struct tu_pipeline_layout *layout)
{
   unsigned size = tu6_load_state_size(pipeline, layout);
   if (size == 0)
      return;

   struct tu_cs cs;
   tu_cs_begin_sub_stream(&pipeline->cs, size, &cs);

   for (unsigned i = 0; i < layout->num_sets; i++) {
      /* From 13.2.7. Descriptor Set Binding:
       *
       *    A compatible descriptor set must be bound for all set numbers that
       *    any shaders in a pipeline access, at the time that a draw or
       *    dispatch command is recorded to execute using that pipeline.
       *    However, if none of the shaders in a pipeline statically use any
       *    bindings with a particular set number, then no descriptor set need
       *    be bound for that set number, even if the pipeline layout includes
       *    a non-trivial descriptor set layout for that set number.
       *
       * This means that descriptor sets unused by the pipeline may have a
       * garbage or 0 BINDLESS_BASE register, which will cause context faults
       * when prefetching descriptors from these sets. Skip prefetching for
       * descriptors from them to avoid this. This is also an optimization,
       * since these prefetches would be useless.
       */
      if (!(pipeline->active_desc_sets & (1u << i)))
         continue;

      struct tu_descriptor_set_layout *set_layout = layout->set[i].layout;
      for (unsigned j = 0; j < set_layout->binding_count; j++) {
         struct tu_descriptor_set_binding_layout *binding = &set_layout->binding[j];
         unsigned base = i;
         unsigned offset = binding->offset / 4;
         /* Note: amber sets VK_SHADER_STAGE_ALL for its descriptor layout, and
          * zink has descriptors for each stage in the push layout even if some
          * stages aren't present in a used pipeline.  We don't want to emit
          * loads for unused descriptors.
          */
         VkShaderStageFlags stages = pipeline->active_stages & binding->shader_stages;
         unsigned count = binding->array_size;

         /* If this is a variable-count descriptor, then the array_size is an
          * upper bound on the size, but we don't know how many descriptors
          * will actually be used. Therefore we can't pre-load them here.
          */
         if (j == set_layout->binding_count - 1 &&
             set_layout->has_variable_descriptors)
            continue;

         if (count == 0 || stages == 0)
            continue;
         switch (binding->type) {
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            base = MAX_SETS;
            offset = (layout->set[i].dynamic_offset_start +
                      binding->dynamic_offset_offset) / 4;
            FALLTHROUGH;
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
            unsigned mul = binding->size / (A6XX_TEX_CONST_DWORDS * 4);
            /* IBO-backed resources only need one packet for all graphics stages */
            if (stages & ~VK_SHADER_STAGE_COMPUTE_BIT) {
               emit_load_state(&cs, CP_LOAD_STATE6, ST6_SHADER, SB6_IBO,
                               base, offset, count * mul);
            }
            if (stages & VK_SHADER_STAGE_COMPUTE_BIT) {
               emit_load_state(&cs, CP_LOAD_STATE6_FRAG, ST6_IBO, SB6_CS_SHADER,
                               base, offset, count * mul);
            }
            break;
         }
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
         case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
            /* nothing - input attachments and inline uniforms don't use bindless */
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLER:
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: {
            tu_foreach_stage(stage, stages) {
               emit_load_state(&cs, tu6_stage2opcode(stage),
                               binding->type == VK_DESCRIPTOR_TYPE_SAMPLER ?
                               ST6_SHADER : ST6_CONSTANTS,
                               tu6_stage2texsb(stage), base, offset, count);
            }
            break;
         }
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            base = MAX_SETS;
            offset = (layout->set[i].dynamic_offset_start +
                      binding->dynamic_offset_offset) / 4;
            FALLTHROUGH;
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
            tu_foreach_stage(stage, stages) {
               emit_load_state(&cs, tu6_stage2opcode(stage), ST6_UBO,
                               tu6_stage2shadersb(stage), base, offset, count);
            }
            break;
         }
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            tu_foreach_stage(stage, stages) {
               /* TODO: We could emit less CP_LOAD_STATE6 if we used
                * struct-of-arrays instead of array-of-structs.
                */
               for (unsigned i = 0; i < count; i++) {
                  unsigned tex_offset = offset + 2 * i * A6XX_TEX_CONST_DWORDS;
                  unsigned sam_offset = offset + (2 * i + 1) * A6XX_TEX_CONST_DWORDS;
                  emit_load_state(&cs, tu6_stage2opcode(stage),
                                  ST6_CONSTANTS, tu6_stage2texsb(stage),
                                  base, tex_offset, 1);
                  emit_load_state(&cs, tu6_stage2opcode(stage),
                                  ST6_SHADER, tu6_stage2texsb(stage),
                                  base, sam_offset, 1);
               }
            }
            break;
         }
         default:
            unreachable("bad descriptor type");
         }
      }
   }

   pipeline->load_state = tu_cs_end_draw_state(&pipeline->cs, &cs);
}

struct tu_pipeline_builder
{
   struct tu_device *device;
   void *mem_ctx;
   struct vk_pipeline_cache *cache;
   const VkAllocationCallbacks *alloc;
   const VkGraphicsPipelineCreateInfo *create_info;

   struct tu_pipeline_layout layout;

   struct tu_compiled_shaders *compiled_shaders;

   struct tu_const_state const_state[MESA_SHADER_FRAGMENT + 1];
   struct ir3_shader_variant *variants[MESA_SHADER_FRAGMENT + 1];
   struct ir3_shader_variant *binning_variant;
   uint64_t shader_iova[MESA_SHADER_FRAGMENT + 1];
   uint64_t binning_vs_iova;

   uint32_t additional_cs_reserve_size;

   struct tu_pvtmem_config pvtmem;

   bool rasterizer_discard;
   /* these states are affectd by rasterizer_discard */
   uint8_t unscaled_input_fragcoord;

   /* Each library defines at least one piece of state in
    * VkGraphicsPipelineLibraryFlagsEXT, and libraries cannot overlap, so
    * there can be at most as many libraries as pieces of state, of which
    * there are currently 4.
    */
#define MAX_LIBRARIES 4

   unsigned num_libraries;
   struct tu_graphics_lib_pipeline *libraries[MAX_LIBRARIES];

   /* This is just the state that we are compiling now, whereas the final
    * pipeline will include the state from the libraries.
    */
   VkGraphicsPipelineLibraryFlagsEXT state;

   /* The stages we are compiling now. */
   VkShaderStageFlags active_stages;

   bool fragment_density_map;

   struct vk_graphics_pipeline_all_state all_state;
   struct vk_graphics_pipeline_state graphics_state;
};

static bool
tu_logic_op_reads_dst(VkLogicOp op)
{
   switch (op) {
   case VK_LOGIC_OP_CLEAR:
   case VK_LOGIC_OP_COPY:
   case VK_LOGIC_OP_COPY_INVERTED:
   case VK_LOGIC_OP_SET:
      return false;
   default:
      return true;
   }
}

static bool
tu_blend_state_is_dual_src(const struct vk_color_blend_state *cb)
{
   for (unsigned i = 0; i < cb->attachment_count; i++) {
      if (tu_blend_factor_is_dual_src((VkBlendFactor)cb->attachments[i].src_color_blend_factor) ||
          tu_blend_factor_is_dual_src((VkBlendFactor)cb->attachments[i].dst_color_blend_factor) ||
          tu_blend_factor_is_dual_src((VkBlendFactor)cb->attachments[i].src_alpha_blend_factor) ||
          tu_blend_factor_is_dual_src((VkBlendFactor)cb->attachments[i].dst_alpha_blend_factor))
         return true;
   }

   return false;
}

static const struct xs_config {
   uint16_t reg_sp_xs_ctrl;
   uint16_t reg_sp_xs_config;
   uint16_t reg_sp_xs_instrlen;
   uint16_t reg_hlsq_xs_ctrl;
   uint16_t reg_sp_xs_first_exec_offset;
   uint16_t reg_sp_xs_pvt_mem_hw_stack_offset;
} xs_config[] = {
   [MESA_SHADER_VERTEX] = {
      REG_A6XX_SP_VS_CTRL_REG0,
      REG_A6XX_SP_VS_CONFIG,
      REG_A6XX_SP_VS_INSTRLEN,
      REG_A6XX_HLSQ_VS_CNTL,
      REG_A6XX_SP_VS_OBJ_FIRST_EXEC_OFFSET,
      REG_A6XX_SP_VS_PVT_MEM_HW_STACK_OFFSET,
   },
   [MESA_SHADER_TESS_CTRL] = {
      REG_A6XX_SP_HS_CTRL_REG0,
      REG_A6XX_SP_HS_CONFIG,
      REG_A6XX_SP_HS_INSTRLEN,
      REG_A6XX_HLSQ_HS_CNTL,
      REG_A6XX_SP_HS_OBJ_FIRST_EXEC_OFFSET,
      REG_A6XX_SP_HS_PVT_MEM_HW_STACK_OFFSET,
   },
   [MESA_SHADER_TESS_EVAL] = {
      REG_A6XX_SP_DS_CTRL_REG0,
      REG_A6XX_SP_DS_CONFIG,
      REG_A6XX_SP_DS_INSTRLEN,
      REG_A6XX_HLSQ_DS_CNTL,
      REG_A6XX_SP_DS_OBJ_FIRST_EXEC_OFFSET,
      REG_A6XX_SP_DS_PVT_MEM_HW_STACK_OFFSET,
   },
   [MESA_SHADER_GEOMETRY] = {
      REG_A6XX_SP_GS_CTRL_REG0,
      REG_A6XX_SP_GS_CONFIG,
      REG_A6XX_SP_GS_INSTRLEN,
      REG_A6XX_HLSQ_GS_CNTL,
      REG_A6XX_SP_GS_OBJ_FIRST_EXEC_OFFSET,
      REG_A6XX_SP_GS_PVT_MEM_HW_STACK_OFFSET,
   },
   [MESA_SHADER_FRAGMENT] = {
      REG_A6XX_SP_FS_CTRL_REG0,
      REG_A6XX_SP_FS_CONFIG,
      REG_A6XX_SP_FS_INSTRLEN,
      REG_A6XX_HLSQ_FS_CNTL,
      REG_A6XX_SP_FS_OBJ_FIRST_EXEC_OFFSET,
      REG_A6XX_SP_FS_PVT_MEM_HW_STACK_OFFSET,
   },
   [MESA_SHADER_COMPUTE] = {
      REG_A6XX_SP_CS_CTRL_REG0,
      REG_A6XX_SP_CS_CONFIG,
      REG_A6XX_SP_CS_INSTRLEN,
      REG_A6XX_HLSQ_CS_CNTL,
      REG_A6XX_SP_CS_OBJ_FIRST_EXEC_OFFSET,
      REG_A6XX_SP_CS_PVT_MEM_HW_STACK_OFFSET,
   },
};

static uint32_t
tu_xs_get_immediates_packet_size_dwords(const struct ir3_shader_variant *xs)
{
   const struct ir3_const_state *const_state = ir3_const_state(xs);
   uint32_t base = const_state->offsets.immediate;
   int32_t size = DIV_ROUND_UP(const_state->immediates_count, 4);

   /* truncate size to avoid writing constants that shader
    * does not use:
    */
   size = MIN2(size + base, xs->constlen) - base;

   return MAX2(size, 0) * 4;
}

/* We allocate fixed-length substreams for shader state, however some
 * parts of the state may have unbound length. Their additional space
 * requirements should be calculated here.
 */
static uint32_t
tu_xs_get_additional_cs_size_dwords(const struct ir3_shader_variant *xs)
{
   const struct ir3_const_state *const_state = ir3_const_state(xs);

   uint32_t size = tu_xs_get_immediates_packet_size_dwords(xs);

   /* Variable number of UBO upload ranges. */
   size += 4 * const_state->ubo_state.num_enabled;

   /* Variable number of dwords for the primitive map */
   size += xs->input_size;

   size += xs->constant_data_size / 4;

   return size;
}

void
tu6_emit_xs_config(struct tu_cs *cs,
                   gl_shader_stage stage, /* xs->type, but xs may be NULL */
                   const struct ir3_shader_variant *xs)
{
   const struct xs_config *cfg = &xs_config[stage];

   if (!xs) {
      /* shader stage disabled */
      tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_config, 1);
      tu_cs_emit(cs, 0);

      tu_cs_emit_pkt4(cs, cfg->reg_hlsq_xs_ctrl, 1);
      tu_cs_emit(cs, 0);
      return;
   }

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_config, 1);
   tu_cs_emit(cs, A6XX_SP_VS_CONFIG_ENABLED |
                  COND(xs->bindless_tex, A6XX_SP_VS_CONFIG_BINDLESS_TEX) |
                  COND(xs->bindless_samp, A6XX_SP_VS_CONFIG_BINDLESS_SAMP) |
                  COND(xs->bindless_ibo, A6XX_SP_VS_CONFIG_BINDLESS_IBO) |
                  COND(xs->bindless_ubo, A6XX_SP_VS_CONFIG_BINDLESS_UBO) |
                  A6XX_SP_VS_CONFIG_NTEX(xs->num_samp) |
                  A6XX_SP_VS_CONFIG_NSAMP(xs->num_samp));

   tu_cs_emit_pkt4(cs, cfg->reg_hlsq_xs_ctrl, 1);
   tu_cs_emit(cs, A6XX_HLSQ_VS_CNTL_CONSTLEN(xs->constlen) |
                  A6XX_HLSQ_VS_CNTL_ENABLED);
}

void
tu6_emit_xs(struct tu_cs *cs,
            gl_shader_stage stage, /* xs->type, but xs may be NULL */
            const struct ir3_shader_variant *xs,
            const struct tu_pvtmem_config *pvtmem,
            uint64_t binary_iova)
{
   const struct xs_config *cfg = &xs_config[stage];

   if (!xs) {
      /* shader stage disabled */
      return;
   }

   enum a6xx_threadsize thrsz =
      xs->info.double_threadsize ? THREAD128 : THREAD64;
   switch (stage) {
   case MESA_SHADER_VERTEX:
      tu_cs_emit_regs(cs, A6XX_SP_VS_CTRL_REG0(
               .halfregfootprint = xs->info.max_half_reg + 1,
               .fullregfootprint = xs->info.max_reg + 1,
               .branchstack = ir3_shader_branchstack_hw(xs),
               .mergedregs = xs->mergedregs,
      ));
      break;
   case MESA_SHADER_TESS_CTRL:
      tu_cs_emit_regs(cs, A6XX_SP_HS_CTRL_REG0(
               .halfregfootprint = xs->info.max_half_reg + 1,
               .fullregfootprint = xs->info.max_reg + 1,
               .branchstack = ir3_shader_branchstack_hw(xs),
      ));
      break;
   case MESA_SHADER_TESS_EVAL:
      tu_cs_emit_regs(cs, A6XX_SP_DS_CTRL_REG0(
               .halfregfootprint = xs->info.max_half_reg + 1,
               .fullregfootprint = xs->info.max_reg + 1,
               .branchstack = ir3_shader_branchstack_hw(xs),
      ));
      break;
   case MESA_SHADER_GEOMETRY:
      tu_cs_emit_regs(cs, A6XX_SP_GS_CTRL_REG0(
               .halfregfootprint = xs->info.max_half_reg + 1,
               .fullregfootprint = xs->info.max_reg + 1,
               .branchstack = ir3_shader_branchstack_hw(xs),
      ));
      break;
   case MESA_SHADER_FRAGMENT:
      tu_cs_emit_regs(cs, A6XX_SP_FS_CTRL_REG0(
               .halfregfootprint = xs->info.max_half_reg + 1,
               .fullregfootprint = xs->info.max_reg + 1,
               .branchstack = ir3_shader_branchstack_hw(xs),
               .threadsize = thrsz,
               .varying = xs->total_in != 0,
               .lodpixmask = xs->need_full_quad,
               /* unknown bit, seems unnecessary */
               .unk24 = true,
               .pixlodenable = xs->need_pixlod,
               .mergedregs = xs->mergedregs,
      ));
      break;
   case MESA_SHADER_COMPUTE:
      tu_cs_emit_regs(cs, A6XX_SP_CS_CTRL_REG0(
               .halfregfootprint = xs->info.max_half_reg + 1,
               .fullregfootprint = xs->info.max_reg + 1,
               .branchstack = ir3_shader_branchstack_hw(xs),
               .threadsize = thrsz,
               .mergedregs = xs->mergedregs,
      ));
      break;
   default:
      unreachable("bad shader stage");
   }

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_instrlen, 1);
   tu_cs_emit(cs, xs->instrlen);

   /* emit program binary & private memory layout
    * binary_iova should be aligned to 1 instrlen unit (128 bytes)
    */

   assert((binary_iova & 0x7f) == 0);
   assert((pvtmem->iova & 0x1f) == 0);

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_first_exec_offset, 7);
   tu_cs_emit(cs, 0);
   tu_cs_emit_qw(cs, binary_iova);
   tu_cs_emit(cs,
              A6XX_SP_VS_PVT_MEM_PARAM_MEMSIZEPERITEM(pvtmem->per_fiber_size));
   tu_cs_emit_qw(cs, pvtmem->iova);
   tu_cs_emit(cs, A6XX_SP_VS_PVT_MEM_SIZE_TOTALPVTMEMSIZE(pvtmem->per_sp_size) |
                  COND(pvtmem->per_wave, A6XX_SP_VS_PVT_MEM_SIZE_PERWAVEMEMLAYOUT));

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_pvt_mem_hw_stack_offset, 1);
   tu_cs_emit(cs, A6XX_SP_VS_PVT_MEM_HW_STACK_OFFSET_OFFSET(pvtmem->per_sp_size));

   uint32_t shader_preload_size =
      MIN2(xs->instrlen, cs->device->physical_device->info->a6xx.instr_cache_size);

   tu_cs_emit_pkt7(cs, tu6_stage2opcode(stage), 3);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(0) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(stage)) |
                  CP_LOAD_STATE6_0_NUM_UNIT(shader_preload_size));
   tu_cs_emit_qw(cs, binary_iova);

   /* emit immediates */

   const struct ir3_const_state *const_state = ir3_const_state(xs);
   uint32_t base = const_state->offsets.immediate;
   unsigned immediate_size = tu_xs_get_immediates_packet_size_dwords(xs);

   if (immediate_size > 0) {
      tu_cs_emit_pkt7(cs, tu6_stage2opcode(stage), 3 + immediate_size);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(base) |
                 CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                 CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                 CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(stage)) |
                 CP_LOAD_STATE6_0_NUM_UNIT(immediate_size / 4));
      tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
      tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));

      tu_cs_emit_array(cs, const_state->immediates, immediate_size);
   }

   if (const_state->constant_data_ubo != -1) {
      uint64_t iova = binary_iova + xs->info.constant_data_offset;

      /* Upload UBO state for the constant data. */
      tu_cs_emit_pkt7(cs, tu6_stage2opcode(stage), 5);
      tu_cs_emit(cs,
                 CP_LOAD_STATE6_0_DST_OFF(const_state->constant_data_ubo) |
                 CP_LOAD_STATE6_0_STATE_TYPE(ST6_UBO)|
                 CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                 CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(stage)) |
                 CP_LOAD_STATE6_0_NUM_UNIT(1));
      tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
      tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
      int size_vec4s = DIV_ROUND_UP(xs->constant_data_size, 16);
      tu_cs_emit_qw(cs,
                    iova |
                    (uint64_t)A6XX_UBO_1_SIZE(size_vec4s) << 32);

      /* Upload the constant data to the const file if needed. */
      const struct ir3_ubo_analysis_state *ubo_state = &const_state->ubo_state;

      for (int i = 0; i < ubo_state->num_enabled; i++) {
         if (ubo_state->range[i].ubo.block != const_state->constant_data_ubo ||
             ubo_state->range[i].ubo.bindless) {
            continue;
         }

         uint32_t start = ubo_state->range[i].start;
         uint32_t end = ubo_state->range[i].end;
         uint32_t size = MIN2(end - start,
                              (16 * xs->constlen) - ubo_state->range[i].offset);

         tu_cs_emit_pkt7(cs, tu6_stage2opcode(stage), 3);
         tu_cs_emit(cs,
                    CP_LOAD_STATE6_0_DST_OFF(ubo_state->range[i].offset / 16) |
                    CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                    CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                    CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(stage)) |
                    CP_LOAD_STATE6_0_NUM_UNIT(size / 16));
         tu_cs_emit_qw(cs, iova + start);
      }
   }

   /* emit statically-known FS driver param */
   if (stage == MESA_SHADER_FRAGMENT && const_state->num_driver_params > 0) {
      uint32_t base = const_state->offsets.driver_param;
      int32_t size = DIV_ROUND_UP(MAX2(const_state->num_driver_params, 4), 4);
      size = MAX2(MIN2(size + base, xs->constlen) - base, 0);

      if (size > 0) {
         tu_cs_emit_pkt7(cs, tu6_stage2opcode(stage), 3 + 4);
         tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(base) |
                    CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                    CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                    CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(stage)) |
                    CP_LOAD_STATE6_0_NUM_UNIT(size));
         tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
         tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));

         tu_cs_emit(cs, xs->info.double_threadsize ? 128 : 64);
         tu_cs_emit(cs, 0);
         tu_cs_emit(cs, 0);
         tu_cs_emit(cs, 0);
      }
   }
}

static void
tu6_emit_dynamic_offset(struct tu_cs *cs,
                        const struct ir3_shader_variant *xs,
                        struct tu_pipeline_builder *builder)
{
   if (!xs || builder->const_state[xs->type].dynamic_offset_loc == UINT32_MAX)
      return;

   tu_cs_emit_pkt7(cs, tu6_stage2opcode(xs->type), 3 + MAX_SETS);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(builder->const_state[xs->type].dynamic_offset_loc / 4) |
              CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
              CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
              CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(xs->type)) |
              CP_LOAD_STATE6_0_NUM_UNIT(DIV_ROUND_UP(MAX_SETS, 4)));
   tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
   tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));

   for (unsigned i = 0; i < MAX_SETS; i++) {
      unsigned dynamic_offset_start =
         builder->layout.set[i].dynamic_offset_start / (A6XX_TEX_CONST_DWORDS * 4);
      tu_cs_emit(cs, i < builder->layout.num_sets ? dynamic_offset_start : 0);
   }
}

static void
tu6_emit_shared_consts_enable(struct tu_cs *cs, bool enable)
{
   /* Enable/disable shared constants */
   tu_cs_emit_regs(cs, A6XX_HLSQ_SHARED_CONSTS(.enable = enable));
   tu_cs_emit_regs(cs, A6XX_SP_MODE_CONTROL(.constant_demotion_enable = true,
                                            .isammode = ISAMMODE_GL,
                                            .shared_consts_enable = enable));
}

static void
tu6_emit_cs_config(struct tu_cs *cs,
                   const struct ir3_shader_variant *v,
                   const struct tu_pvtmem_config *pvtmem,
                   uint64_t binary_iova)
{
   bool shared_consts_enable = ir3_const_state(v)->shared_consts_enable;
   tu6_emit_shared_consts_enable(cs, shared_consts_enable);

   tu_cs_emit_regs(cs, A6XX_HLSQ_INVALIDATE_CMD(
         .cs_state = true,
         .cs_ibo = true,
         .cs_shared_const = shared_consts_enable));

   tu6_emit_xs_config(cs, MESA_SHADER_COMPUTE, v);
   tu6_emit_xs(cs, MESA_SHADER_COMPUTE, v, pvtmem, binary_iova);

   uint32_t shared_size = MAX2(((int)v->shared_size - 1) / 1024, 1);
   tu_cs_emit_pkt4(cs, REG_A6XX_SP_CS_UNKNOWN_A9B1, 1);
   tu_cs_emit(cs, A6XX_SP_CS_UNKNOWN_A9B1_SHARED_SIZE(shared_size) |
                  A6XX_SP_CS_UNKNOWN_A9B1_UNK6);

   if (cs->device->physical_device->info->a6xx.has_lpac) {
      tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_CS_UNKNOWN_B9D0, 1);
      tu_cs_emit(cs, A6XX_HLSQ_CS_UNKNOWN_B9D0_SHARED_SIZE(shared_size) |
                     A6XX_HLSQ_CS_UNKNOWN_B9D0_UNK6);
   }

   uint32_t local_invocation_id =
      ir3_find_sysval_regid(v, SYSTEM_VALUE_LOCAL_INVOCATION_ID);
   uint32_t work_group_id =
      ir3_find_sysval_regid(v, SYSTEM_VALUE_WORKGROUP_ID);

   enum a6xx_threadsize thrsz = v->info.double_threadsize ? THREAD128 : THREAD64;
   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_CS_CNTL_0, 2);
   tu_cs_emit(cs,
              A6XX_HLSQ_CS_CNTL_0_WGIDCONSTID(work_group_id) |
              A6XX_HLSQ_CS_CNTL_0_WGSIZECONSTID(regid(63, 0)) |
              A6XX_HLSQ_CS_CNTL_0_WGOFFSETCONSTID(regid(63, 0)) |
              A6XX_HLSQ_CS_CNTL_0_LOCALIDREGID(local_invocation_id));
   tu_cs_emit(cs, A6XX_HLSQ_CS_CNTL_1_LINEARLOCALIDREGID(regid(63, 0)) |
                  A6XX_HLSQ_CS_CNTL_1_THREADSIZE(thrsz));

   if (cs->device->physical_device->info->a6xx.has_lpac) {
      tu_cs_emit_pkt4(cs, REG_A6XX_SP_CS_CNTL_0, 2);
      tu_cs_emit(cs,
                 A6XX_SP_CS_CNTL_0_WGIDCONSTID(work_group_id) |
                 A6XX_SP_CS_CNTL_0_WGSIZECONSTID(regid(63, 0)) |
                 A6XX_SP_CS_CNTL_0_WGOFFSETCONSTID(regid(63, 0)) |
                 A6XX_SP_CS_CNTL_0_LOCALIDREGID(local_invocation_id));
      tu_cs_emit(cs, A6XX_SP_CS_CNTL_1_LINEARLOCALIDREGID(regid(63, 0)) |
                     A6XX_SP_CS_CNTL_1_THREADSIZE(thrsz));
   }
}

#define TU6_EMIT_VFD_DEST_MAX_DWORDS (MAX_VERTEX_ATTRIBS + 2)

static void
tu6_emit_vfd_dest(struct tu_cs *cs,
                  const struct ir3_shader_variant *vs)
{
   int32_t input_for_attr[MAX_VERTEX_ATTRIBS];
   uint32_t attr_count = 0;

   for (unsigned i = 0; i < MAX_VERTEX_ATTRIBS; i++)
      input_for_attr[i] = -1;

   for (unsigned i = 0; i < vs->inputs_count; i++) {
      if (vs->inputs[i].sysval || vs->inputs[i].regid == regid(63, 0))
         continue;

      assert(vs->inputs[i].slot >= VERT_ATTRIB_GENERIC0);
      unsigned loc = vs->inputs[i].slot - VERT_ATTRIB_GENERIC0;
      input_for_attr[loc] = i;
      attr_count = MAX2(attr_count, loc + 1);
   }

   tu_cs_emit_regs(cs,
                   A6XX_VFD_CONTROL_0(
                     .fetch_cnt = attr_count, /* decode_cnt for binning pass ? */
                     .decode_cnt = attr_count));

   if (attr_count)
      tu_cs_emit_pkt4(cs, REG_A6XX_VFD_DEST_CNTL_INSTR(0), attr_count);

   for (unsigned i = 0; i < attr_count; i++) {
      if (input_for_attr[i] >= 0) {
            unsigned input_idx = input_for_attr[i];
            tu_cs_emit(cs, A6XX_VFD_DEST_CNTL_INSTR(0,
                             .writemask = vs->inputs[input_idx].compmask,
                             .regid = vs->inputs[input_idx].regid).value);
      } else {
            tu_cs_emit(cs, A6XX_VFD_DEST_CNTL_INSTR(0,
                             .writemask = 0,
                             .regid = regid(63, 0)).value);
      }
   }
}

static void
tu6_emit_vs_system_values(struct tu_cs *cs,
                          const struct ir3_shader_variant *vs,
                          const struct ir3_shader_variant *hs,
                          const struct ir3_shader_variant *ds,
                          const struct ir3_shader_variant *gs,
                          bool primid_passthru)
{
   const uint32_t vertexid_regid =
         ir3_find_sysval_regid(vs, SYSTEM_VALUE_VERTEX_ID);
   const uint32_t instanceid_regid =
         ir3_find_sysval_regid(vs, SYSTEM_VALUE_INSTANCE_ID);
   const uint32_t tess_coord_x_regid = hs ?
         ir3_find_sysval_regid(ds, SYSTEM_VALUE_TESS_COORD) :
         regid(63, 0);
   const uint32_t tess_coord_y_regid = VALIDREG(tess_coord_x_regid) ?
         tess_coord_x_regid + 1 :
         regid(63, 0);
   const uint32_t hs_rel_patch_regid = hs ?
         ir3_find_sysval_regid(hs, SYSTEM_VALUE_REL_PATCH_ID_IR3) :
         regid(63, 0);
   const uint32_t ds_rel_patch_regid = hs ?
         ir3_find_sysval_regid(ds, SYSTEM_VALUE_REL_PATCH_ID_IR3) :
         regid(63, 0);
   const uint32_t hs_invocation_regid = hs ?
         ir3_find_sysval_regid(hs, SYSTEM_VALUE_TCS_HEADER_IR3) :
         regid(63, 0);
   const uint32_t gs_primitiveid_regid = gs ?
         ir3_find_sysval_regid(gs, SYSTEM_VALUE_PRIMITIVE_ID) :
         regid(63, 0);
   const uint32_t vs_primitiveid_regid = hs ?
         ir3_find_sysval_regid(hs, SYSTEM_VALUE_PRIMITIVE_ID) :
         gs_primitiveid_regid;
   const uint32_t ds_primitiveid_regid = ds ?
         ir3_find_sysval_regid(ds, SYSTEM_VALUE_PRIMITIVE_ID) :
         regid(63, 0);
   const uint32_t gsheader_regid = gs ?
         ir3_find_sysval_regid(gs, SYSTEM_VALUE_GS_HEADER_IR3) :
         regid(63, 0);

   /* Note: we currently don't support multiview with tess or GS. If we did,
    * and the HW actually works, then we'd have to somehow share this across
    * stages. Note that the blob doesn't support this either.
    */
   const uint32_t viewid_regid =
      ir3_find_sysval_regid(vs, SYSTEM_VALUE_VIEW_INDEX);

   tu_cs_emit_pkt4(cs, REG_A6XX_VFD_CONTROL_1, 6);
   tu_cs_emit(cs, A6XX_VFD_CONTROL_1_REGID4VTX(vertexid_regid) |
                  A6XX_VFD_CONTROL_1_REGID4INST(instanceid_regid) |
                  A6XX_VFD_CONTROL_1_REGID4PRIMID(vs_primitiveid_regid) |
                  A6XX_VFD_CONTROL_1_REGID4VIEWID(viewid_regid));
   tu_cs_emit(cs, A6XX_VFD_CONTROL_2_REGID_HSRELPATCHID(hs_rel_patch_regid) |
                  A6XX_VFD_CONTROL_2_REGID_INVOCATIONID(hs_invocation_regid));
   tu_cs_emit(cs, A6XX_VFD_CONTROL_3_REGID_DSRELPATCHID(ds_rel_patch_regid) |
                  A6XX_VFD_CONTROL_3_REGID_TESSX(tess_coord_x_regid) |
                  A6XX_VFD_CONTROL_3_REGID_TESSY(tess_coord_y_regid) |
                  A6XX_VFD_CONTROL_3_REGID_DSPRIMID(ds_primitiveid_regid));
   tu_cs_emit(cs, 0x000000fc); /* VFD_CONTROL_4 */
   tu_cs_emit(cs, A6XX_VFD_CONTROL_5_REGID_GSHEADER(gsheader_regid) |
                  0xfc00); /* VFD_CONTROL_5 */
   tu_cs_emit(cs, COND(primid_passthru, A6XX_VFD_CONTROL_6_PRIMID_PASSTHRU)); /* VFD_CONTROL_6 */
}

static void
tu6_setup_streamout(struct tu_cs *cs,
                    const struct ir3_shader_variant *v,
                    struct ir3_shader_linkage *l)
{
   const struct ir3_stream_output_info *info = &v->stream_output;
   /* Note: 64 here comes from the HW layout of the program RAM. The program
    * for stream N is at DWORD 64 * N.
    */
#define A6XX_SO_PROG_DWORDS 64
   uint32_t prog[A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS] = {};
   BITSET_DECLARE(valid_dwords, A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS) = {0};

   /* TODO: streamout state should be in a non-GMEM draw state */

   /* no streamout: */
   if (info->num_outputs == 0) {
      unsigned sizedw = 4;
      if (cs->device->physical_device->info->a6xx.tess_use_shared)
         sizedw += 2;

      tu_cs_emit_pkt7(cs, CP_CONTEXT_REG_BUNCH, sizedw);
      tu_cs_emit(cs, REG_A6XX_VPC_SO_CNTL);
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, REG_A6XX_VPC_SO_STREAM_CNTL);
      tu_cs_emit(cs, 0);

      if (cs->device->physical_device->info->a6xx.tess_use_shared) {
         tu_cs_emit(cs, REG_A6XX_PC_SO_STREAM_CNTL);
         tu_cs_emit(cs, 0);
      }

      return;
   }

   for (unsigned i = 0; i < info->num_outputs; i++) {
      const struct ir3_stream_output *out = &info->output[i];
      unsigned k = out->register_index;
      unsigned idx;

      /* Skip it, if it's an output that was never assigned a register. */
      if (k >= v->outputs_count || v->outputs[k].regid == INVALID_REG)
         continue;

      /* linkage map sorted by order frag shader wants things, so
       * a bit less ideal here..
       */
      for (idx = 0; idx < l->cnt; idx++)
         if (l->var[idx].slot == v->outputs[k].slot)
            break;

      assert(idx < l->cnt);

      for (unsigned j = 0; j < out->num_components; j++) {
         unsigned c   = j + out->start_component;
         unsigned loc = l->var[idx].loc + c;
         unsigned off = j + out->dst_offset;  /* in dwords */

         assert(loc < A6XX_SO_PROG_DWORDS * 2);
         unsigned dword = out->stream * A6XX_SO_PROG_DWORDS + loc/2;
         if (loc & 1) {
            prog[dword] |= A6XX_VPC_SO_PROG_B_EN |
                           A6XX_VPC_SO_PROG_B_BUF(out->output_buffer) |
                           A6XX_VPC_SO_PROG_B_OFF(off * 4);
         } else {
            prog[dword] |= A6XX_VPC_SO_PROG_A_EN |
                           A6XX_VPC_SO_PROG_A_BUF(out->output_buffer) |
                           A6XX_VPC_SO_PROG_A_OFF(off * 4);
         }
         BITSET_SET(valid_dwords, dword);
      }
   }

   unsigned prog_count = 0;
   unsigned start, end;
   BITSET_FOREACH_RANGE(start, end, valid_dwords,
                        A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS) {
      prog_count += end - start + 1;
   }

   const bool emit_pc_so_stream_cntl =
      cs->device->physical_device->info->a6xx.tess_use_shared &&
      v->type == MESA_SHADER_TESS_EVAL;

   if (emit_pc_so_stream_cntl)
      prog_count += 1;

   tu_cs_emit_pkt7(cs, CP_CONTEXT_REG_BUNCH, 10 + 2 * prog_count);
   tu_cs_emit(cs, REG_A6XX_VPC_SO_STREAM_CNTL);
   tu_cs_emit(cs, A6XX_VPC_SO_STREAM_CNTL_STREAM_ENABLE(info->streams_written) |
                  COND(info->stride[0] > 0,
                       A6XX_VPC_SO_STREAM_CNTL_BUF0_STREAM(1 + info->buffer_to_stream[0])) |
                  COND(info->stride[1] > 0,
                       A6XX_VPC_SO_STREAM_CNTL_BUF1_STREAM(1 + info->buffer_to_stream[1])) |
                  COND(info->stride[2] > 0,
                       A6XX_VPC_SO_STREAM_CNTL_BUF2_STREAM(1 + info->buffer_to_stream[2])) |
                  COND(info->stride[3] > 0,
                       A6XX_VPC_SO_STREAM_CNTL_BUF3_STREAM(1 + info->buffer_to_stream[3])));
   for (uint32_t i = 0; i < 4; i++) {
      tu_cs_emit(cs, REG_A6XX_VPC_SO_BUFFER_STRIDE(i));
      tu_cs_emit(cs, info->stride[i]);
   }
   bool first = true;
   BITSET_FOREACH_RANGE(start, end, valid_dwords,
                        A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS) {
      tu_cs_emit(cs, REG_A6XX_VPC_SO_CNTL);
      tu_cs_emit(cs, COND(first, A6XX_VPC_SO_CNTL_RESET) |
                     A6XX_VPC_SO_CNTL_ADDR(start));
      for (unsigned i = start; i < end; i++) {
         tu_cs_emit(cs, REG_A6XX_VPC_SO_PROG);
         tu_cs_emit(cs, prog[i]);
      }
      first = false;
   }

   if (emit_pc_so_stream_cntl) {
      /* Possibly not tess_use_shared related, but the combination of
       * tess + xfb fails some tests if we don't emit this.
       */
      tu_cs_emit(cs, REG_A6XX_PC_SO_STREAM_CNTL);
      tu_cs_emit(cs, A6XX_PC_SO_STREAM_CNTL_STREAM_ENABLE(info->streams_written));
   }
}

static void
tu6_emit_const(struct tu_cs *cs, uint32_t opcode, uint32_t base,
               enum a6xx_state_block block, uint32_t offset,
               uint32_t size, const uint32_t *dwords) {
   assert(size % 4 == 0);

   tu_cs_emit_pkt7(cs, opcode, 3 + size);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(base) |
         CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
         CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
         CP_LOAD_STATE6_0_STATE_BLOCK(block) |
         CP_LOAD_STATE6_0_NUM_UNIT(size / 4));

   tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
   tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
   dwords = (uint32_t *)&((uint8_t *)dwords)[offset];

   tu_cs_emit_array(cs, dwords, size);
}

static void
tu6_emit_link_map(struct tu_cs *cs,
                  const struct ir3_shader_variant *producer,
                  const struct ir3_shader_variant *consumer,
                  enum a6xx_state_block sb)
{
   const struct ir3_const_state *const_state = ir3_const_state(consumer);
   uint32_t base = const_state->offsets.primitive_map;
   int size = DIV_ROUND_UP(consumer->input_size, 4);

   size = (MIN2(size + base, consumer->constlen) - base) * 4;
   if (size <= 0)
      return;

   tu6_emit_const(cs, CP_LOAD_STATE6_GEOM, base, sb, 0, size,
                         producer->output_loc);
}

static enum a6xx_tess_output
primitive_to_tess(enum mesa_prim primitive) {
   switch (primitive) {
   case MESA_PRIM_POINTS:
      return TESS_POINTS;
   case MESA_PRIM_LINE_STRIP:
      return TESS_LINES;
   case MESA_PRIM_TRIANGLE_STRIP:
      return TESS_CW_TRIS;
   default:
      unreachable("");
   }
}

static int
tu6_vpc_varying_mode(const struct ir3_shader_variant *fs,
                     const struct ir3_shader_variant *last_shader,
                     uint32_t index,
                     uint8_t *interp_mode,
                     uint8_t *ps_repl_mode)
{
   enum
   {
      INTERP_SMOOTH = 0,
      INTERP_FLAT = 1,
      INTERP_ZERO = 2,
      INTERP_ONE = 3,
   };
   enum
   {
      PS_REPL_NONE = 0,
      PS_REPL_S = 1,
      PS_REPL_T = 2,
      PS_REPL_ONE_MINUS_T = 3,
   };

   const uint32_t compmask = fs->inputs[index].compmask;

   /* NOTE: varyings are packed, so if compmask is 0xb then first, second, and
    * fourth component occupy three consecutive varying slots
    */
   int shift = 0;
   *interp_mode = 0;
   *ps_repl_mode = 0;
   if (fs->inputs[index].slot == VARYING_SLOT_PNTC) {
      if (compmask & 0x1) {
         *ps_repl_mode |= PS_REPL_S << shift;
         shift += 2;
      }
      if (compmask & 0x2) {
         *ps_repl_mode |= PS_REPL_T << shift;
         shift += 2;
      }
      if (compmask & 0x4) {
         *interp_mode |= INTERP_ZERO << shift;
         shift += 2;
      }
      if (compmask & 0x8) {
         *interp_mode |= INTERP_ONE << 6;
         shift += 2;
      }
   } else if (fs->inputs[index].slot == VARYING_SLOT_LAYER ||
              fs->inputs[index].slot == VARYING_SLOT_VIEWPORT) {
      /* If the last geometry shader doesn't statically write these, they're
       * implicitly zero and the FS is supposed to read zero.
       */
      const gl_varying_slot slot = (gl_varying_slot) fs->inputs[index].slot;
      if (ir3_find_output(last_shader, slot) < 0 &&
          (compmask & 0x1)) {
         *interp_mode |= INTERP_ZERO;
      } else {
         *interp_mode |= INTERP_FLAT;
      }
   } else if (fs->inputs[index].flat) {
      for (int i = 0; i < 4; i++) {
         if (compmask & (1 << i)) {
            *interp_mode |= INTERP_FLAT << shift;
            shift += 2;
         }
      }
   }

   return util_bitcount(compmask) * 2;
}

static void
tu6_emit_vpc_varying_modes(struct tu_cs *cs,
                           const struct ir3_shader_variant *fs,
                           const struct ir3_shader_variant *last_shader)
{
   uint32_t interp_modes[8] = { 0 };
   uint32_t ps_repl_modes[8] = { 0 };
   uint32_t interp_regs = 0;

   if (fs) {
      for (int i = -1;
           (i = ir3_next_varying(fs, i)) < (int) fs->inputs_count;) {

         /* get the mode for input i */
         uint8_t interp_mode;
         uint8_t ps_repl_mode;
         const int bits =
            tu6_vpc_varying_mode(fs, last_shader, i, &interp_mode, &ps_repl_mode);

         /* OR the mode into the array */
         const uint32_t inloc = fs->inputs[i].inloc * 2;
         uint32_t n = inloc / 32;
         uint32_t shift = inloc % 32;
         interp_modes[n] |= interp_mode << shift;
         ps_repl_modes[n] |= ps_repl_mode << shift;
         if (shift + bits > 32) {
            n++;
            shift = 32 - shift;

            interp_modes[n] |= interp_mode >> shift;
            ps_repl_modes[n] |= ps_repl_mode >> shift;
         }
         interp_regs = MAX2(interp_regs, n + 1);
      }
   }

   if (interp_regs) {
      tu_cs_emit_pkt4(cs, REG_A6XX_VPC_VARYING_INTERP_MODE(0), interp_regs);
      tu_cs_emit_array(cs, interp_modes, interp_regs);

      tu_cs_emit_pkt4(cs, REG_A6XX_VPC_VARYING_PS_REPL_MODE(0), interp_regs);
      tu_cs_emit_array(cs, ps_repl_modes, interp_regs);
   }
}

void
tu6_emit_vpc(struct tu_cs *cs,
             const struct ir3_shader_variant *vs,
             const struct ir3_shader_variant *hs,
             const struct ir3_shader_variant *ds,
             const struct ir3_shader_variant *gs,
             const struct ir3_shader_variant *fs)
{
   /* note: doesn't compile as static because of the array regs.. */
   const struct reg_config {
      uint16_t reg_sp_xs_out_reg;
      uint16_t reg_sp_xs_vpc_dst_reg;
      uint16_t reg_vpc_xs_pack;
      uint16_t reg_vpc_xs_clip_cntl;
      uint16_t reg_gras_xs_cl_cntl;
      uint16_t reg_pc_xs_out_cntl;
      uint16_t reg_sp_xs_primitive_cntl;
      uint16_t reg_vpc_xs_layer_cntl;
      uint16_t reg_gras_xs_layer_cntl;
   } reg_config[] = {
      [MESA_SHADER_VERTEX] = {
         REG_A6XX_SP_VS_OUT_REG(0),
         REG_A6XX_SP_VS_VPC_DST_REG(0),
         REG_A6XX_VPC_VS_PACK,
         REG_A6XX_VPC_VS_CLIP_CNTL,
         REG_A6XX_GRAS_VS_CL_CNTL,
         REG_A6XX_PC_VS_OUT_CNTL,
         REG_A6XX_SP_VS_PRIMITIVE_CNTL,
         REG_A6XX_VPC_VS_LAYER_CNTL,
         REG_A6XX_GRAS_VS_LAYER_CNTL
      },
      [MESA_SHADER_TESS_CTRL] = {
         0,
         0,
         0,
         0,
         0,
         REG_A6XX_PC_HS_OUT_CNTL,
         0,
         0,
         0
      },
      [MESA_SHADER_TESS_EVAL] = {
         REG_A6XX_SP_DS_OUT_REG(0),
         REG_A6XX_SP_DS_VPC_DST_REG(0),
         REG_A6XX_VPC_DS_PACK,
         REG_A6XX_VPC_DS_CLIP_CNTL,
         REG_A6XX_GRAS_DS_CL_CNTL,
         REG_A6XX_PC_DS_OUT_CNTL,
         REG_A6XX_SP_DS_PRIMITIVE_CNTL,
         REG_A6XX_VPC_DS_LAYER_CNTL,
         REG_A6XX_GRAS_DS_LAYER_CNTL
      },
      [MESA_SHADER_GEOMETRY] = {
         REG_A6XX_SP_GS_OUT_REG(0),
         REG_A6XX_SP_GS_VPC_DST_REG(0),
         REG_A6XX_VPC_GS_PACK,
         REG_A6XX_VPC_GS_CLIP_CNTL,
         REG_A6XX_GRAS_GS_CL_CNTL,
         REG_A6XX_PC_GS_OUT_CNTL,
         REG_A6XX_SP_GS_PRIMITIVE_CNTL,
         REG_A6XX_VPC_GS_LAYER_CNTL,
         REG_A6XX_GRAS_GS_LAYER_CNTL
      },
   };

   const struct ir3_shader_variant *last_shader;
   if (gs) {
      last_shader = gs;
   } else if (hs) {
      last_shader = ds;
   } else {
      last_shader = vs;
   }

   const struct reg_config *cfg = &reg_config[last_shader->type];

   struct ir3_shader_linkage linkage = {
      .primid_loc = 0xff,
      .clip0_loc = 0xff,
      .clip1_loc = 0xff,
   };
   if (fs)
      ir3_link_shaders(&linkage, last_shader, fs, true);

   if (last_shader->stream_output.num_outputs)
      ir3_link_stream_out(&linkage, last_shader);

   /* We do this after linking shaders in order to know whether PrimID
    * passthrough needs to be enabled.
    */
   bool primid_passthru = linkage.primid_loc != 0xff;
   tu6_emit_vs_system_values(cs, vs, hs, ds, gs, primid_passthru);

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_VAR_DISABLE(0), 4);
   tu_cs_emit(cs, ~linkage.varmask[0]);
   tu_cs_emit(cs, ~linkage.varmask[1]);
   tu_cs_emit(cs, ~linkage.varmask[2]);
   tu_cs_emit(cs, ~linkage.varmask[3]);

   /* a6xx finds position/pointsize at the end */
   const uint32_t pointsize_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_PSIZ);
   const uint32_t layer_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_LAYER);
   const uint32_t view_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_VIEWPORT);
   const uint32_t clip0_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_CLIP_DIST0);
   const uint32_t clip1_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_CLIP_DIST1);
   uint32_t flags_regid = gs ?
      ir3_find_output_regid(gs, VARYING_SLOT_GS_VERTEX_FLAGS_IR3) : 0;

   uint32_t pointsize_loc = 0xff, position_loc = 0xff, layer_loc = 0xff, view_loc = 0xff;

   if (layer_regid != regid(63, 0)) {
      layer_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_LAYER, layer_regid, 0x1, linkage.max_loc);
   }

   if (view_regid != regid(63, 0)) {
      view_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_VIEWPORT, view_regid, 0x1, linkage.max_loc);
   }

   unsigned extra_pos = 0;

   for (unsigned i = 0; i < last_shader->outputs_count; i++) {
      if (last_shader->outputs[i].slot != VARYING_SLOT_POS)
         continue;

      if (position_loc == 0xff)
         position_loc = linkage.max_loc;

      ir3_link_add(&linkage, last_shader->outputs[i].slot,
                   last_shader->outputs[i].regid,
                   0xf, position_loc + 4 * last_shader->outputs[i].view);
      extra_pos = MAX2(extra_pos, last_shader->outputs[i].view);
   }

   if (pointsize_regid != regid(63, 0)) {
      pointsize_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_PSIZ, pointsize_regid, 0x1, linkage.max_loc);
   }

   uint8_t clip_cull_mask = last_shader->clip_mask | last_shader->cull_mask;

   /* Handle the case where clip/cull distances aren't read by the FS */
   uint32_t clip0_loc = linkage.clip0_loc, clip1_loc = linkage.clip1_loc;
   if (clip0_loc == 0xff && clip0_regid != regid(63, 0)) {
      clip0_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_CLIP_DIST0, clip0_regid,
                   clip_cull_mask & 0xf, linkage.max_loc);
   }
   if (clip1_loc == 0xff && clip1_regid != regid(63, 0)) {
      clip1_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_CLIP_DIST1, clip1_regid,
                   clip_cull_mask >> 4, linkage.max_loc);
   }

   tu6_setup_streamout(cs, last_shader, &linkage);

   /* The GPU hangs on some models when there are no outputs (xs_pack::CNT),
    * at least when a DS is the last stage, so add a dummy output to keep it
    * happy if there aren't any. We do this late in order to avoid emitting
    * any unused code and make sure that optimizations don't remove it.
    */
   if (linkage.cnt == 0)
      ir3_link_add(&linkage, 0, 0, 0x1, linkage.max_loc);

   /* map outputs of the last shader to VPC */
   assert(linkage.cnt <= 32);
   const uint32_t sp_out_count = DIV_ROUND_UP(linkage.cnt, 2);
   const uint32_t sp_vpc_dst_count = DIV_ROUND_UP(linkage.cnt, 4);
   uint32_t sp_out[16] = {0};
   uint32_t sp_vpc_dst[8] = {0};
   for (uint32_t i = 0; i < linkage.cnt; i++) {
      ((uint16_t *) sp_out)[i] =
         A6XX_SP_VS_OUT_REG_A_REGID(linkage.var[i].regid) |
         A6XX_SP_VS_OUT_REG_A_COMPMASK(linkage.var[i].compmask);
      ((uint8_t *) sp_vpc_dst)[i] =
         A6XX_SP_VS_VPC_DST_REG_OUTLOC0(linkage.var[i].loc);
   }

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_out_reg, sp_out_count);
   tu_cs_emit_array(cs, sp_out, sp_out_count);

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_vpc_dst_reg, sp_vpc_dst_count);
   tu_cs_emit_array(cs, sp_vpc_dst, sp_vpc_dst_count);

   tu_cs_emit_pkt4(cs, cfg->reg_vpc_xs_pack, 1);
   tu_cs_emit(cs, A6XX_VPC_VS_PACK_POSITIONLOC(position_loc) |
                  A6XX_VPC_VS_PACK_PSIZELOC(pointsize_loc) |
                  A6XX_VPC_VS_PACK_STRIDE_IN_VPC(linkage.max_loc) |
                  A6XX_VPC_VS_PACK_EXTRAPOS(extra_pos));

   tu_cs_emit_pkt4(cs, cfg->reg_vpc_xs_clip_cntl, 1);
   tu_cs_emit(cs, A6XX_VPC_VS_CLIP_CNTL_CLIP_MASK(clip_cull_mask) |
                  A6XX_VPC_VS_CLIP_CNTL_CLIP_DIST_03_LOC(clip0_loc) |
                  A6XX_VPC_VS_CLIP_CNTL_CLIP_DIST_47_LOC(clip1_loc));

   tu_cs_emit_pkt4(cs, cfg->reg_gras_xs_cl_cntl, 1);
   tu_cs_emit(cs, A6XX_GRAS_VS_CL_CNTL_CLIP_MASK(last_shader->clip_mask) |
                  A6XX_GRAS_VS_CL_CNTL_CULL_MASK(last_shader->cull_mask));

   const struct ir3_shader_variant *geom_shaders[] = { vs, hs, ds, gs };

   for (unsigned i = 0; i < ARRAY_SIZE(geom_shaders); i++) {
      const struct ir3_shader_variant *shader = geom_shaders[i];
      if (!shader)
         continue;

      bool primid = shader->type != MESA_SHADER_VERTEX &&
         VALIDREG(ir3_find_sysval_regid(shader, SYSTEM_VALUE_PRIMITIVE_ID));

      tu_cs_emit_pkt4(cs, reg_config[shader->type].reg_pc_xs_out_cntl, 1);
      if (shader == last_shader) {
         tu_cs_emit(cs, A6XX_PC_VS_OUT_CNTL_STRIDE_IN_VPC(linkage.max_loc) |
                        CONDREG(pointsize_regid, A6XX_PC_VS_OUT_CNTL_PSIZE) |
                        CONDREG(layer_regid, A6XX_PC_VS_OUT_CNTL_LAYER) |
                        CONDREG(view_regid, A6XX_PC_VS_OUT_CNTL_VIEW) |
                        COND(primid, A6XX_PC_VS_OUT_CNTL_PRIMITIVE_ID) |
                        A6XX_PC_VS_OUT_CNTL_CLIP_MASK(clip_cull_mask));
      } else {
         tu_cs_emit(cs, COND(primid, A6XX_PC_VS_OUT_CNTL_PRIMITIVE_ID));
      }
   }

   /* if vertex_flags somehow gets optimized out, your gonna have a bad time: */
   if (gs)
      assert(flags_regid != INVALID_REG);

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_primitive_cntl, 1);
   tu_cs_emit(cs, A6XX_SP_VS_PRIMITIVE_CNTL_OUT(linkage.cnt) |
                  A6XX_SP_GS_PRIMITIVE_CNTL_FLAGS_REGID(flags_regid));

   tu_cs_emit_pkt4(cs, cfg->reg_vpc_xs_layer_cntl, 1);
   tu_cs_emit(cs, A6XX_VPC_VS_LAYER_CNTL_LAYERLOC(layer_loc) |
                  A6XX_VPC_VS_LAYER_CNTL_VIEWLOC(view_loc));

   tu_cs_emit_pkt4(cs, cfg->reg_gras_xs_layer_cntl, 1);
   tu_cs_emit(cs, CONDREG(layer_regid, A6XX_GRAS_GS_LAYER_CNTL_WRITES_LAYER) |
                  CONDREG(view_regid, A6XX_GRAS_GS_LAYER_CNTL_WRITES_VIEW));

   tu_cs_emit_regs(cs, A6XX_PC_PRIMID_PASSTHRU(primid_passthru));

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_CNTL_0, 1);
   tu_cs_emit(cs, A6XX_VPC_CNTL_0_NUMNONPOSVAR(fs ? fs->total_in : 0) |
                  COND(fs && fs->total_in, A6XX_VPC_CNTL_0_VARYING) |
                  A6XX_VPC_CNTL_0_PRIMIDLOC(linkage.primid_loc) |
                  A6XX_VPC_CNTL_0_VIEWIDLOC(linkage.viewid_loc));

   if (hs) {
      tu_cs_emit_pkt4(cs, REG_A6XX_PC_TESS_NUM_VERTEX, 1);
      tu_cs_emit(cs, hs->tess.tcs_vertices_out);

      tu6_emit_link_map(cs, vs, hs, SB6_HS_SHADER);
      tu6_emit_link_map(cs, hs, ds, SB6_DS_SHADER);
   }


   if (gs) {
      uint32_t vertices_out, invocations, vec4_size;
      uint32_t prev_stage_output_size = ds ? ds->output_size : vs->output_size;

      if (hs) {
         tu6_emit_link_map(cs, ds, gs, SB6_GS_SHADER);
      } else {
         tu6_emit_link_map(cs, vs, gs, SB6_GS_SHADER);
      }
      vertices_out = gs->gs.vertices_out - 1;
      enum a6xx_tess_output output = primitive_to_tess((enum mesa_prim) gs->gs.output_primitive);
      invocations = gs->gs.invocations - 1;
      /* Size of per-primitive alloction in ldlw memory in vec4s. */
      vec4_size = gs->gs.vertices_in *
                  DIV_ROUND_UP(prev_stage_output_size, 4);

      tu_cs_emit_pkt4(cs, REG_A6XX_PC_PRIMITIVE_CNTL_5, 1);
      tu_cs_emit(cs,
            A6XX_PC_PRIMITIVE_CNTL_5_GS_VERTICES_OUT(vertices_out) |
            A6XX_PC_PRIMITIVE_CNTL_5_GS_OUTPUT(output) |
            A6XX_PC_PRIMITIVE_CNTL_5_GS_INVOCATIONS(invocations));

      tu_cs_emit_pkt4(cs, REG_A6XX_VPC_GS_PARAM, 1);
      tu_cs_emit(cs, 0xff);

      tu_cs_emit_pkt4(cs, REG_A6XX_PC_PRIMITIVE_CNTL_6, 1);
      tu_cs_emit(cs, A6XX_PC_PRIMITIVE_CNTL_6_STRIDE_IN_VPC(vec4_size));

      uint32_t prim_size = prev_stage_output_size;
      if (prim_size > 64)
         prim_size = 64;
      else if (prim_size == 64)
         prim_size = 63;
      tu_cs_emit_pkt4(cs, REG_A6XX_SP_GS_PRIM_SIZE, 1);
      tu_cs_emit(cs, prim_size);
   }

   tu6_emit_vpc_varying_modes(cs, fs, last_shader);
}

static enum a6xx_tex_prefetch_cmd
tu6_tex_opc_to_prefetch_cmd(opc_t tex_opc)
{
   switch (tex_opc) {
   case OPC_SAM:
      return TEX_PREFETCH_SAM;
   default:
      unreachable("Unknown tex opc for prefeth cmd");
   }
}

void
tu6_emit_fs_inputs(struct tu_cs *cs, const struct ir3_shader_variant *fs)
{
   uint32_t face_regid, coord_regid, zwcoord_regid, samp_id_regid;
   uint32_t ij_regid[IJ_COUNT];
   uint32_t smask_in_regid;

   bool sample_shading = fs->per_samp | fs->key.sample_shading;
   bool enable_varyings = fs->total_in > 0;

   samp_id_regid   = ir3_find_sysval_regid(fs, SYSTEM_VALUE_SAMPLE_ID);
   smask_in_regid  = ir3_find_sysval_regid(fs, SYSTEM_VALUE_SAMPLE_MASK_IN);
   face_regid      = ir3_find_sysval_regid(fs, SYSTEM_VALUE_FRONT_FACE);
   coord_regid     = ir3_find_sysval_regid(fs, SYSTEM_VALUE_FRAG_COORD);
   zwcoord_regid   = VALIDREG(coord_regid) ? coord_regid + 2 : regid(63, 0);
   for (unsigned i = 0; i < ARRAY_SIZE(ij_regid); i++)
      ij_regid[i] = ir3_find_sysval_regid(fs, SYSTEM_VALUE_BARYCENTRIC_PERSP_PIXEL + i);

   if (fs->num_sampler_prefetch > 0) {
      /* It seems like ij_pix is *required* to be r0.x */
      assert(!VALIDREG(ij_regid[IJ_PERSP_PIXEL]) ||
             ij_regid[IJ_PERSP_PIXEL] == regid(0, 0));
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_PREFETCH_CNTL, 1 + fs->num_sampler_prefetch);
   tu_cs_emit(cs, A6XX_SP_FS_PREFETCH_CNTL_COUNT(fs->num_sampler_prefetch) |
                     COND(!VALIDREG(ij_regid[IJ_PERSP_PIXEL]),
                          A6XX_SP_FS_PREFETCH_CNTL_IJ_WRITE_DISABLE));
   for (int i = 0; i < fs->num_sampler_prefetch; i++) {
      const struct ir3_sampler_prefetch *prefetch = &fs->sampler_prefetch[i];
      tu_cs_emit(cs, A6XX_SP_FS_PREFETCH_CMD_SRC(prefetch->src) |
                     A6XX_SP_FS_PREFETCH_CMD_SAMP_ID(prefetch->samp_id) |
                     A6XX_SP_FS_PREFETCH_CMD_TEX_ID(prefetch->tex_id) |
                     A6XX_SP_FS_PREFETCH_CMD_DST(prefetch->dst) |
                     A6XX_SP_FS_PREFETCH_CMD_WRMASK(prefetch->wrmask) |
                     COND(prefetch->half_precision, A6XX_SP_FS_PREFETCH_CMD_HALF) |
                     COND(prefetch->bindless, A6XX_SP_FS_PREFETCH_CMD_BINDLESS) |
                     A6XX_SP_FS_PREFETCH_CMD_CMD(
                        tu6_tex_opc_to_prefetch_cmd(prefetch->tex_opc)));
   }

   if (fs->num_sampler_prefetch > 0) {
      tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_BINDLESS_PREFETCH_CMD(0), fs->num_sampler_prefetch);
      for (int i = 0; i < fs->num_sampler_prefetch; i++) {
         const struct ir3_sampler_prefetch *prefetch = &fs->sampler_prefetch[i];
         tu_cs_emit(cs,
                    A6XX_SP_FS_BINDLESS_PREFETCH_CMD_SAMP_ID(prefetch->samp_bindless_id) |
                    A6XX_SP_FS_BINDLESS_PREFETCH_CMD_TEX_ID(prefetch->tex_bindless_id));
      }
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_CONTROL_1_REG, 5);
   tu_cs_emit(cs, 0x7);
   tu_cs_emit(cs, A6XX_HLSQ_CONTROL_2_REG_FACEREGID(face_regid) |
                  A6XX_HLSQ_CONTROL_2_REG_SAMPLEID(samp_id_regid) |
                  A6XX_HLSQ_CONTROL_2_REG_SAMPLEMASK(smask_in_regid) |
                  A6XX_HLSQ_CONTROL_2_REG_CENTERRHW(ij_regid[IJ_PERSP_CENTER_RHW]));
   tu_cs_emit(cs, A6XX_HLSQ_CONTROL_3_REG_IJ_PERSP_PIXEL(ij_regid[IJ_PERSP_PIXEL]) |
                  A6XX_HLSQ_CONTROL_3_REG_IJ_LINEAR_PIXEL(ij_regid[IJ_LINEAR_PIXEL]) |
                  A6XX_HLSQ_CONTROL_3_REG_IJ_PERSP_CENTROID(ij_regid[IJ_PERSP_CENTROID]) |
                  A6XX_HLSQ_CONTROL_3_REG_IJ_LINEAR_CENTROID(ij_regid[IJ_LINEAR_CENTROID]));
   tu_cs_emit(cs, A6XX_HLSQ_CONTROL_4_REG_XYCOORDREGID(coord_regid) |
                  A6XX_HLSQ_CONTROL_4_REG_ZWCOORDREGID(zwcoord_regid) |
                  A6XX_HLSQ_CONTROL_4_REG_IJ_PERSP_SAMPLE(ij_regid[IJ_PERSP_SAMPLE]) |
                  A6XX_HLSQ_CONTROL_4_REG_IJ_LINEAR_SAMPLE(ij_regid[IJ_LINEAR_SAMPLE]));
   tu_cs_emit(cs, 0xfcfc);

   enum a6xx_threadsize thrsz = fs->info.double_threadsize ? THREAD128 : THREAD64;
   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_FS_CNTL_0, 1);
   tu_cs_emit(cs, A6XX_HLSQ_FS_CNTL_0_THREADSIZE(thrsz) |
                  COND(enable_varyings, A6XX_HLSQ_FS_CNTL_0_VARYINGS));

   bool need_size = fs->frag_face || fs->fragcoord_compmask != 0;
   bool need_size_persamp = false;
   if (VALIDREG(ij_regid[IJ_PERSP_CENTER_RHW])) {
      if (sample_shading)
         need_size_persamp = true;
      else
         need_size = true;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_CNTL, 1);
   tu_cs_emit(cs,
         CONDREG(ij_regid[IJ_PERSP_PIXEL], A6XX_GRAS_CNTL_IJ_PERSP_PIXEL) |
         CONDREG(ij_regid[IJ_PERSP_CENTROID], A6XX_GRAS_CNTL_IJ_PERSP_CENTROID) |
         CONDREG(ij_regid[IJ_PERSP_SAMPLE], A6XX_GRAS_CNTL_IJ_PERSP_SAMPLE) |
         CONDREG(ij_regid[IJ_LINEAR_PIXEL], A6XX_GRAS_CNTL_IJ_LINEAR_PIXEL) |
         CONDREG(ij_regid[IJ_LINEAR_CENTROID], A6XX_GRAS_CNTL_IJ_LINEAR_CENTROID) |
         CONDREG(ij_regid[IJ_LINEAR_SAMPLE], A6XX_GRAS_CNTL_IJ_LINEAR_SAMPLE) |
         COND(need_size, A6XX_GRAS_CNTL_IJ_LINEAR_PIXEL) |
         COND(need_size_persamp, A6XX_GRAS_CNTL_IJ_LINEAR_SAMPLE) |
         COND(fs->fragcoord_compmask != 0, A6XX_GRAS_CNTL_COORD_MASK(fs->fragcoord_compmask)));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_RENDER_CONTROL0, 2);
   tu_cs_emit(cs,
         CONDREG(ij_regid[IJ_PERSP_PIXEL], A6XX_RB_RENDER_CONTROL0_IJ_PERSP_PIXEL) |
         CONDREG(ij_regid[IJ_PERSP_CENTROID], A6XX_RB_RENDER_CONTROL0_IJ_PERSP_CENTROID) |
         CONDREG(ij_regid[IJ_PERSP_SAMPLE], A6XX_RB_RENDER_CONTROL0_IJ_PERSP_SAMPLE) |
         CONDREG(ij_regid[IJ_LINEAR_PIXEL], A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_PIXEL) |
         CONDREG(ij_regid[IJ_LINEAR_CENTROID], A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_CENTROID) |
         CONDREG(ij_regid[IJ_LINEAR_SAMPLE], A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_SAMPLE) |
         COND(need_size, A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_PIXEL) |
         COND(enable_varyings, A6XX_RB_RENDER_CONTROL0_UNK10) |
         COND(need_size_persamp, A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_SAMPLE) |
         COND(fs->fragcoord_compmask != 0,
                           A6XX_RB_RENDER_CONTROL0_COORD_MASK(fs->fragcoord_compmask)));
   tu_cs_emit(cs,
         A6XX_RB_RENDER_CONTROL1_FRAGCOORDSAMPLEMODE(
            sample_shading ? FRAGCOORD_SAMPLE : FRAGCOORD_CENTER) |
         CONDREG(smask_in_regid, A6XX_RB_RENDER_CONTROL1_SAMPLEMASK) |
         CONDREG(samp_id_regid, A6XX_RB_RENDER_CONTROL1_SAMPLEID) |
         CONDREG(ij_regid[IJ_PERSP_CENTER_RHW], A6XX_RB_RENDER_CONTROL1_CENTERRHW) |
         COND(fs->post_depth_coverage, A6XX_RB_RENDER_CONTROL1_POSTDEPTHCOVERAGE)  |
         COND(fs->frag_face, A6XX_RB_RENDER_CONTROL1_FACENESS));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_SAMPLE_CNTL, 1);
   tu_cs_emit(cs, COND(sample_shading, A6XX_RB_SAMPLE_CNTL_PER_SAMP_MODE));

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_LRZ_PS_INPUT_CNTL, 1);
   tu_cs_emit(cs, CONDREG(samp_id_regid, A6XX_GRAS_LRZ_PS_INPUT_CNTL_SAMPLEID) |
              A6XX_GRAS_LRZ_PS_INPUT_CNTL_FRAGCOORDSAMPLEMODE(
                 sample_shading ? FRAGCOORD_SAMPLE : FRAGCOORD_CENTER));

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SAMPLE_CNTL, 1);
   tu_cs_emit(cs, COND(sample_shading, A6XX_GRAS_SAMPLE_CNTL_PER_SAMP_MODE));
}

static void
tu6_emit_fs_outputs(struct tu_cs *cs,
                    const struct ir3_shader_variant *fs,
                    struct tu_pipeline *pipeline)
{
   uint32_t smask_regid, posz_regid, stencilref_regid;

   posz_regid      = ir3_find_output_regid(fs, FRAG_RESULT_DEPTH);
   smask_regid     = ir3_find_output_regid(fs, FRAG_RESULT_SAMPLE_MASK);
   stencilref_regid = ir3_find_output_regid(fs, FRAG_RESULT_STENCIL);

   int output_reg_count = 0;
   uint32_t fragdata_regid[8];

   assert(!fs->color0_mrt);
   for (uint32_t i = 0; i < ARRAY_SIZE(fragdata_regid); i++) {
      fragdata_regid[i] = ir3_find_output_regid(fs, FRAG_RESULT_DATA0 + i);
      if (VALIDREG(fragdata_regid[i]))
         output_reg_count = i + 1;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_OUTPUT_CNTL0, 1);
   tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_CNTL0_DEPTH_REGID(posz_regid) |
                  A6XX_SP_FS_OUTPUT_CNTL0_SAMPMASK_REGID(smask_regid) |
                  A6XX_SP_FS_OUTPUT_CNTL0_STENCILREF_REGID(stencilref_regid) |
                  COND(fs->dual_src_blend, A6XX_SP_FS_OUTPUT_CNTL0_DUAL_COLOR_IN_ENABLE));

   /* There is no point in having component enabled which is not written
    * by the shader. Per VK spec it is an UB, however a few apps depend on
    * attachment not being changed if FS doesn't have corresponding output.
    */
   uint32_t fs_render_components = 0;

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_OUTPUT_REG(0), output_reg_count);
   for (uint32_t i = 0; i < output_reg_count; i++) {
      tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_REG_REGID(fragdata_regid[i]) |
                     (COND(fragdata_regid[i] & HALF_REG_ID,
                           A6XX_SP_FS_OUTPUT_REG_HALF_PRECISION)));

      if (VALIDREG(fragdata_regid[i])) {
         fs_render_components |= 0xf << (i * 4);
      }
   }

   tu_cs_emit_regs(cs,
                   A6XX_SP_FS_RENDER_COMPONENTS(.dword = fs_render_components));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_FS_OUTPUT_CNTL0, 1);
   tu_cs_emit(cs, COND(fs->writes_pos, A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_Z) |
                  COND(fs->writes_smask, A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_SAMPMASK) |
                  COND(fs->writes_stencilref, A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_STENCILREF) |
                  COND(fs->dual_src_blend, A6XX_RB_FS_OUTPUT_CNTL0_DUAL_COLOR_IN_ENABLE));

   tu_cs_emit_regs(cs,
                   A6XX_RB_RENDER_COMPONENTS(.dword = fs_render_components));

   if (pipeline) {
      if (fs->has_kill) {
         pipeline->lrz.lrz_status |= TU_LRZ_FORCE_DISABLE_WRITE;
      }
      if (fs->no_earlyz || fs->writes_pos) {
         pipeline->lrz.lrz_status = TU_LRZ_FORCE_DISABLE_LRZ;
      }
      pipeline->lrz.fs.has_kill = fs->has_kill;
      pipeline->lrz.fs.early_fragment_tests = fs->fs.early_fragment_tests;

      if (!fs->fs.early_fragment_tests &&
          (fs->no_earlyz || fs->writes_pos || fs->writes_stencilref || fs->writes_smask)) {
         pipeline->lrz.force_late_z = true;
      }

      pipeline->lrz.fs.force_early_z = fs->fs.early_fragment_tests;
   }
}

static void
tu6_emit_vs_params(struct tu_cs *cs,
                   const struct ir3_const_state *const_state,
                   unsigned constlen,
                   unsigned param_stride,
                   unsigned num_vertices)
{
   uint32_t vs_params[4] = {
      param_stride * num_vertices * 4,  /* vs primitive stride */
      param_stride * 4,                 /* vs vertex stride */
      0,
      0,
   };
   uint32_t vs_base = const_state->offsets.primitive_param;
   tu6_emit_const(cs, CP_LOAD_STATE6_GEOM, vs_base, SB6_VS_SHADER, 0,
                  ARRAY_SIZE(vs_params), vs_params);
}

static void
tu_get_tess_iova(struct tu_device *dev,
                 uint64_t *tess_factor_iova,
                 uint64_t *tess_param_iova)
{
   /* Create the shared tess factor BO the first time tess is used on the device. */
   if (!dev->tess_bo) {
      mtx_lock(&dev->mutex);
      if (!dev->tess_bo)
         tu_bo_init_new(dev, &dev->tess_bo, TU_TESS_BO_SIZE, TU_BO_ALLOC_NO_FLAGS, "tess");
      mtx_unlock(&dev->mutex);
   }

   *tess_factor_iova = dev->tess_bo->iova;
   *tess_param_iova = dev->tess_bo->iova + TU_TESS_FACTOR_SIZE;
}

static const enum mesa_vk_dynamic_graphics_state tu_patch_control_points_state[] = {
   MESA_VK_DYNAMIC_TS_PATCH_CONTROL_POINTS,
};

static unsigned 
tu6_patch_control_points_size(struct tu_device *dev,
                              const struct tu_pipeline *pipeline,
                              uint32_t patch_control_points)
{
#define EMIT_CONST_DWORDS(const_dwords) (4 + const_dwords)
   return EMIT_CONST_DWORDS(4) +
      EMIT_CONST_DWORDS(pipeline->program.hs_param_dwords) + 2 + 2 + 2;
#undef EMIT_CONST_DWORDS
}

void
tu6_emit_patch_control_points(struct tu_cs *cs,
                              const struct tu_pipeline *pipeline,
                              uint32_t patch_control_points)
{
   if (!(pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT))
      return;

   struct tu_device *dev = cs->device;

   tu6_emit_vs_params(cs,
                      &pipeline->program.link[MESA_SHADER_VERTEX].const_state,
                      pipeline->program.link[MESA_SHADER_VERTEX].constlen,
                      pipeline->program.vs_param_stride,
                      patch_control_points);

   uint64_t tess_factor_iova, tess_param_iova;
   tu_get_tess_iova(dev, &tess_factor_iova, &tess_param_iova);

   uint32_t hs_params[8] = {
      pipeline->program.vs_param_stride * patch_control_points * 4,  /* hs primitive stride */
      pipeline->program.vs_param_stride * 4,                         /* hs vertex stride */
      pipeline->program.hs_param_stride,
      patch_control_points,
      tess_param_iova,
      tess_param_iova >> 32,
      tess_factor_iova,
      tess_factor_iova >> 32,
   };

   const struct ir3_const_state *hs_const =
      &pipeline->program.link[MESA_SHADER_TESS_CTRL].const_state;
   uint32_t hs_base = hs_const->offsets.primitive_param;
   tu6_emit_const(cs, CP_LOAD_STATE6_GEOM, hs_base, SB6_HS_SHADER, 0,
                  pipeline->program.hs_param_dwords, hs_params);

   uint32_t patch_local_mem_size_16b =
      patch_control_points * pipeline->program.vs_param_stride / 4;

   /* Total attribute slots in HS incoming patch. */
   tu_cs_emit_pkt4(cs, REG_A6XX_PC_HS_INPUT_SIZE, 1);
   tu_cs_emit(cs, patch_local_mem_size_16b);

   const uint32_t wavesize = 64;
   const uint32_t vs_hs_local_mem_size = 16384;

   uint32_t max_patches_per_wave;
   if (dev->physical_device->info->a6xx.tess_use_shared) {
      /* HS invocations for a patch are always within the same wave,
       * making barriers less expensive. VS can't have barriers so we
       * don't care about VS invocations being in the same wave.
       */
      max_patches_per_wave = wavesize / pipeline->program.hs_vertices_out;
   } else {
      /* VS is also in the same wave */
      max_patches_per_wave =
         wavesize / MAX2(patch_control_points,
                         pipeline->program.hs_vertices_out);
   }

   uint32_t patches_per_wave =
      MIN2(vs_hs_local_mem_size / (patch_local_mem_size_16b * 16),
           max_patches_per_wave);

   uint32_t wave_input_size = DIV_ROUND_UP(
      patches_per_wave * patch_local_mem_size_16b * 16, 256);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_HS_WAVE_INPUT_SIZE, 1);
   tu_cs_emit(cs, wave_input_size);

   /* maximum number of patches that can fit in tess factor/param buffers */
   uint32_t subdraw_size = MIN2(TU_TESS_FACTOR_SIZE / ir3_tess_factor_stride(pipeline->tess.patch_type),
                        TU_TESS_PARAM_SIZE / (pipeline->program.hs_param_stride * 4));
   /* convert from # of patches to draw count */
   subdraw_size *= patch_control_points;

   tu_cs_emit_pkt7(cs, CP_SET_SUBDRAW_SIZE, 1);
   tu_cs_emit(cs, subdraw_size);
}

static void
tu6_emit_geom_tess_consts(struct tu_cs *cs,
                          const struct ir3_shader_variant *vs,
                          const struct ir3_shader_variant *hs,
                          const struct ir3_shader_variant *ds,
                          const struct ir3_shader_variant *gs)
{
   struct tu_device *dev = cs->device;

   if (gs && !hs) {
      tu6_emit_vs_params(cs, ir3_const_state(vs), vs->constlen,
                         vs->output_size, gs->gs.vertices_in);
   }

   if (hs) {
      uint64_t tess_factor_iova, tess_param_iova;
      tu_get_tess_iova(dev, &tess_factor_iova, &tess_param_iova);

      uint32_t ds_params[8] = {
         gs ? ds->output_size * gs->gs.vertices_in * 4 : 0,  /* ds primitive stride */
         ds->output_size * 4,                                /* ds vertex stride */
         hs->output_size,                                    /* hs vertex stride (dwords) */
         hs->tess.tcs_vertices_out,
         tess_param_iova,
         tess_param_iova >> 32,
         tess_factor_iova,
         tess_factor_iova >> 32,
      };

      uint32_t ds_base = ds->const_state->offsets.primitive_param;
      uint32_t ds_param_dwords = MIN2((ds->constlen - ds_base) * 4, ARRAY_SIZE(ds_params));
      tu6_emit_const(cs, CP_LOAD_STATE6_GEOM, ds_base, SB6_DS_SHADER, 0,
                     ds_param_dwords, ds_params);
   }

   if (gs) {
      const struct ir3_shader_variant *prev = ds ? ds : vs;
      uint32_t gs_params[4] = {
         prev->output_size * gs->gs.vertices_in * 4,  /* gs primitive stride */
         prev->output_size * 4,                 /* gs vertex stride */
         0,
         0,
      };
      uint32_t gs_base = gs->const_state->offsets.primitive_param;
      tu6_emit_const(cs, CP_LOAD_STATE6_GEOM, gs_base, SB6_GS_SHADER, 0,
                     ARRAY_SIZE(gs_params), gs_params);
   }
}

static void
tu6_emit_program_config(struct tu_cs *cs,
                        struct tu_pipeline_builder *builder)
{
   STATIC_ASSERT(MESA_SHADER_VERTEX == 0);

   bool shared_consts_enable = tu6_shared_constants_enable(&builder->layout,
         builder->device->compiler);
   tu6_emit_shared_consts_enable(cs, shared_consts_enable);

   tu_cs_emit_regs(cs, A6XX_HLSQ_INVALIDATE_CMD(
         .vs_state = true,
         .hs_state = true,
         .ds_state = true,
         .gs_state = true,
         .fs_state = true,
         .gfx_ibo = true,
         .gfx_shared_const = shared_consts_enable));
   for (size_t stage_idx = MESA_SHADER_VERTEX;
        stage_idx < ARRAY_SIZE(builder->shader_iova); stage_idx++) {
      gl_shader_stage stage = (gl_shader_stage) stage_idx;
      tu6_emit_xs_config(cs, stage, builder->variants[stage]);
   }
}

static void
tu6_emit_program(struct tu_cs *cs,
                 struct tu_pipeline_builder *builder,
                 bool binning_pass,
                 struct tu_pipeline *pipeline)
{
   const struct ir3_shader_variant *vs = builder->variants[MESA_SHADER_VERTEX];
   const struct ir3_shader_variant *bs = builder->binning_variant;
   const struct ir3_shader_variant *hs = builder->variants[MESA_SHADER_TESS_CTRL];
   const struct ir3_shader_variant *ds = builder->variants[MESA_SHADER_TESS_EVAL];
   const struct ir3_shader_variant *gs = builder->variants[MESA_SHADER_GEOMETRY];
   const struct ir3_shader_variant *fs = builder->variants[MESA_SHADER_FRAGMENT];
   gl_shader_stage stage = MESA_SHADER_VERTEX;
   bool multi_pos_output = vs->multi_pos_output;

  /* Don't use the binning pass variant when GS is present because we don't
   * support compiling correct binning pass variants with GS.
   */
   if (binning_pass && !gs) {
      vs = bs;
      tu6_emit_xs(cs, stage, bs, &builder->pvtmem, builder->binning_vs_iova);
      tu6_emit_dynamic_offset(cs, bs, builder);
      stage = (gl_shader_stage) (stage + 1);
   }

   for (; stage < ARRAY_SIZE(builder->shader_iova);
        stage = (gl_shader_stage) (stage + 1)) {
      const struct ir3_shader_variant *xs = builder->variants[stage];

      if (stage == MESA_SHADER_FRAGMENT && binning_pass)
         fs = xs = NULL;

      tu6_emit_xs(cs, stage, xs, &builder->pvtmem, builder->shader_iova[stage]);
      tu6_emit_dynamic_offset(cs, xs, builder);
   }

   uint32_t multiview_views = util_logbase2(builder->graphics_state.rp->view_mask) + 1;
   uint32_t multiview_cntl = builder->graphics_state.rp->view_mask ?
      A6XX_PC_MULTIVIEW_CNTL_ENABLE |
      A6XX_PC_MULTIVIEW_CNTL_VIEWS(multiview_views) |
      COND(!multi_pos_output, A6XX_PC_MULTIVIEW_CNTL_DISABLEMULTIPOS)
      : 0;

   /* Copy what the blob does here. This will emit an extra 0x3f
    * CP_EVENT_WRITE when multiview is disabled. I'm not exactly sure what
    * this is working around yet.
    */
   if (builder->device->physical_device->info->a6xx.has_cp_reg_write) {
      tu_cs_emit_pkt7(cs, CP_REG_WRITE, 3);
      tu_cs_emit(cs, CP_REG_WRITE_0_TRACKER(UNK_EVENT_WRITE));
      tu_cs_emit(cs, REG_A6XX_PC_MULTIVIEW_CNTL);
   } else {
      tu_cs_emit_pkt4(cs, REG_A6XX_PC_MULTIVIEW_CNTL, 1);
   }
   tu_cs_emit(cs, multiview_cntl);

   tu_cs_emit_pkt4(cs, REG_A6XX_VFD_MULTIVIEW_CNTL, 1);
   tu_cs_emit(cs, multiview_cntl);

   if (multiview_cntl &&
       builder->device->physical_device->info->a6xx.supports_multiview_mask) {
      tu_cs_emit_pkt4(cs, REG_A6XX_PC_MULTIVIEW_MASK, 1);
      tu_cs_emit(cs, builder->graphics_state.rp->view_mask);
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_HS_WAVE_INPUT_SIZE, 1);
   tu_cs_emit(cs, 0);

   tu6_emit_vfd_dest(cs, vs);

   tu6_emit_vpc(cs, vs, hs, ds, gs, fs);

   if (fs) {
      tu6_emit_fs_inputs(cs, fs);
      tu6_emit_fs_outputs(cs, fs, pipeline);
      pipeline->program.per_samp = fs->per_samp || fs->key.sample_shading;
   } else {
      /* TODO: check if these can be skipped if fs is disabled */
      struct ir3_shader_variant dummy_variant = {};
      tu6_emit_fs_inputs(cs, &dummy_variant);
      tu6_emit_fs_outputs(cs, &dummy_variant, NULL);
   }

   if (gs || hs) {
      tu6_emit_geom_tess_consts(cs, vs, hs, ds, gs);
   }
}

static VkResult
tu_setup_pvtmem(struct tu_device *dev,
                struct tu_pipeline *pipeline,
                struct tu_pvtmem_config *config,
                uint32_t pvtmem_bytes,
                bool per_wave)
{
   if (!pvtmem_bytes) {
      memset(config, 0, sizeof(*config));
      return VK_SUCCESS;
   }

   /* There is a substantial memory footprint from private memory BOs being
    * allocated on a per-pipeline basis and it isn't required as the same
    * BO can be utilized by multiple pipelines as long as they have the
    * private memory layout (sizes and per-wave/per-fiber) to avoid being
    * overwritten by other active pipelines using the same BO with differing
    * private memory layouts resulting memory corruption.
    *
    * To avoid this, we create private memory BOs on a per-device level with
    * an associated private memory layout then dynamically grow them when
    * needed and reuse them across pipelines. Growth is done in terms of
    * powers of two so that we can avoid frequent reallocation of the
    * private memory BOs.
    */

   struct tu_pvtmem_bo *pvtmem_bo =
      per_wave ? &dev->wave_pvtmem_bo : &dev->fiber_pvtmem_bo;
   mtx_lock(&pvtmem_bo->mtx);

   if (pvtmem_bo->per_fiber_size < pvtmem_bytes) {
      if (pvtmem_bo->bo)
         tu_bo_finish(dev, pvtmem_bo->bo);

      pvtmem_bo->per_fiber_size =
         util_next_power_of_two(ALIGN(pvtmem_bytes, 512));
      pvtmem_bo->per_sp_size =
         ALIGN(pvtmem_bo->per_fiber_size *
                  dev->physical_device->info->a6xx.fibers_per_sp,
               1 << 12);
      uint32_t total_size =
         dev->physical_device->info->num_sp_cores * pvtmem_bo->per_sp_size;

      VkResult result = tu_bo_init_new(dev, &pvtmem_bo->bo, total_size,
                                       TU_BO_ALLOC_NO_FLAGS, "pvtmem");
      if (result != VK_SUCCESS) {
         mtx_unlock(&pvtmem_bo->mtx);
         return result;
      }
   }

   config->per_wave = per_wave;
   config->per_fiber_size = pvtmem_bo->per_fiber_size;
   config->per_sp_size = pvtmem_bo->per_sp_size;

   pipeline->pvtmem_bo = tu_bo_get_ref(pvtmem_bo->bo);
   config->iova = pipeline->pvtmem_bo->iova;

   mtx_unlock(&pvtmem_bo->mtx);

   return VK_SUCCESS;
}

static bool
contains_all_shader_state(VkGraphicsPipelineLibraryFlagsEXT state)
{
   return (state &
      (VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
       VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT)) ==
      (VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
       VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT);
}

static bool
pipeline_contains_all_shader_state(struct tu_pipeline *pipeline)
{
   return pipeline->type == TU_PIPELINE_GRAPHICS ||
      pipeline->type == TU_PIPELINE_COMPUTE ||
      contains_all_shader_state(tu_pipeline_to_graphics_lib(pipeline)->state);
}

/* Return true if this pipeline contains all of the GPL stages listed but none
 * of the libraries it uses do, so this is "the first time" that all of them
 * are defined together. This is useful for state that needs to be combined
 * from multiple GPL stages.
 */

static bool
set_combined_state(struct tu_pipeline_builder *builder,
                   struct tu_pipeline *pipeline,
                   VkGraphicsPipelineLibraryFlagsEXT state)
{
   if (pipeline->type == TU_PIPELINE_GRAPHICS_LIB &&
       (tu_pipeline_to_graphics_lib(pipeline)->state & state) != state)
      return false;

   for (unsigned i = 0; i < builder->num_libraries; i++) {
      if ((builder->libraries[i]->state & state) == state)
         return false;
   }

   return true;
}

#define TU6_EMIT_VERTEX_INPUT_MAX_DWORDS (MAX_VERTEX_ATTRIBS * 2 + 1)

static VkResult
tu_pipeline_allocate_cs(struct tu_device *dev,
                        struct tu_pipeline *pipeline,
                        struct tu_pipeline_layout *layout,
                        struct tu_pipeline_builder *builder,
                        struct ir3_shader_variant *compute)
{
   uint32_t size = 1024;

   /* graphics case: */
   if (builder) {
      if (builder->state &
          VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT) {
         size += TU6_EMIT_VERTEX_INPUT_MAX_DWORDS;
      }

      if (set_combined_state(builder, pipeline,
                             VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
                             VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT)) {
         size += 2 * TU6_EMIT_VFD_DEST_MAX_DWORDS;
         size += tu6_load_state_size(pipeline, layout);

         for (uint32_t i = 0; i < ARRAY_SIZE(builder->variants); i++) {
            if (builder->variants[i]) {
               size += builder->variants[i]->info.size / 4;
            }
         }

         size += builder->binning_variant->info.size / 4;

         builder->additional_cs_reserve_size = 0;
         for (unsigned i = 0; i < ARRAY_SIZE(builder->variants); i++) {
            struct ir3_shader_variant *variant = builder->variants[i];
            if (variant) {
               builder->additional_cs_reserve_size +=
                  tu_xs_get_additional_cs_size_dwords(variant);

               if (variant->binning) {
                  builder->additional_cs_reserve_size +=
                     tu_xs_get_additional_cs_size_dwords(variant->binning);
               }
            }
         }

         /* The additional size is used twice, once per tu6_emit_program() call. */
         size += builder->additional_cs_reserve_size * 2;
      }
   } else {
      size += tu6_load_state_size(pipeline, layout);

      size += compute->info.size / 4;

      size += tu_xs_get_additional_cs_size_dwords(compute);
   }

   /* Allocate the space for the pipeline out of the device's RO suballocator.
    *
    * Sub-allocating BOs saves memory and also kernel overhead in refcounting of
    * BOs at exec time.
    *
    * The pipeline cache would seem like a natural place to stick the
    * suballocator, except that it is not guaranteed to outlive the pipelines
    * created from it, so you can't store any long-lived state there, and you
    * can't use its EXTERNALLY_SYNCHRONIZED flag to avoid atomics because
    * pipeline destroy isn't synchronized by the cache.
    */
   mtx_lock(&dev->pipeline_mutex);
   VkResult result = tu_suballoc_bo_alloc(&pipeline->bo, &dev->pipeline_suballoc,
                                          size * 4, 128);
   mtx_unlock(&dev->pipeline_mutex);
   if (result != VK_SUCCESS)
      return result;

   tu_cs_init_suballoc(&pipeline->cs, dev, &pipeline->bo);

   return VK_SUCCESS;
}

static void
tu_pipeline_shader_key_init(struct ir3_shader_key *key,
                            const struct tu_pipeline *pipeline,
                            struct tu_pipeline_builder *builder,
                            nir_shader **nir)
{
   /* We set this after we compile to NIR because we need the prim mode */
   key->tessellation = IR3_TESS_NONE;

   for (unsigned i = 0; i < builder->num_libraries; i++) {
      if (!(builder->libraries[i]->state &
            (VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
             VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT)))
         continue;

      const struct ir3_shader_key *library_key =
         &builder->libraries[i]->ir3_key;

      if (library_key->tessellation != IR3_TESS_NONE)
         key->tessellation = library_key->tessellation;
      key->has_gs |= library_key->has_gs;
      key->sample_shading |= library_key->sample_shading;
   }

   for (uint32_t i = 0; i < builder->create_info->stageCount; i++) {
      if (builder->create_info->pStages[i].stage == VK_SHADER_STAGE_GEOMETRY_BIT) {
         key->has_gs = true;
         break;
      }
   }

   if (!(builder->state & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT))
      return;

   if (builder->rasterizer_discard)
      return;

   const VkPipelineMultisampleStateCreateInfo *msaa_info =
      builder->create_info->pMultisampleState;

   /* The 1.3.215 spec says:
    *
    *    Sample shading can be used to specify a minimum number of unique
    *    samples to process for each fragment. If sample shading is enabled,
    *    an implementation must provide a minimum of
    *
    *       max(ceil(minSampleShadingFactor * totalSamples), 1)
    *
    *    unique associated data for each fragment, where
    *    minSampleShadingFactor is the minimum fraction of sample shading.
    *
    * The definition is pretty much the same as OpenGL's GL_SAMPLE_SHADING.
    * They both require unique associated data.
    *
    * There are discussions to change the definition, such that
    * sampleShadingEnable does not imply unique associated data.  Before the
    * discussions are settled and before apps (i.e., ANGLE) are fixed to
    * follow the new and incompatible definition, we should stick to the
    * current definition.
    *
    * Note that ir3_shader_key::sample_shading is not actually used by ir3,
    * just checked in tu6_emit_fs_inputs.  We will also copy the value to
    * tu_shader_key::force_sample_interp in a bit.
    */
   if (msaa_info && msaa_info->sampleShadingEnable)
      key->sample_shading = true;
}

static uint32_t
tu6_get_tessmode(struct tu_shader* shader)
{
   enum tess_primitive_mode primitive_mode = shader->ir3_shader->nir->info.tess._primitive_mode;
   switch (primitive_mode) {
   case TESS_PRIMITIVE_ISOLINES:
      return IR3_TESS_ISOLINES;
   case TESS_PRIMITIVE_TRIANGLES:
      return IR3_TESS_TRIANGLES;
   case TESS_PRIMITIVE_QUADS:
      return IR3_TESS_QUADS;
   case TESS_PRIMITIVE_UNSPECIFIED:
      return IR3_TESS_NONE;
   default:
      unreachable("bad tessmode");
   }
}

static uint64_t
tu_upload_variant(struct tu_pipeline *pipeline,
                  const struct ir3_shader_variant *variant)
{
   struct tu_cs_memory memory;

   if (!variant)
      return 0;

   /* this expects to get enough alignment because shaders are allocated first
    * and total size is always aligned correctly
    * note: an assert in tu6_emit_xs_config validates the alignment
    */
   tu_cs_alloc(&pipeline->cs, variant->info.size / 4, 1, &memory);

   memcpy(memory.map, variant->bin, variant->info.size);
   return memory.iova;
}

static void
tu_append_executable(struct tu_pipeline *pipeline, struct ir3_shader_variant *variant,
                     char *nir_from_spirv)
{
   struct tu_pipeline_executable exe = {
      .stage = variant->type,
      .stats = variant->info,
      .is_binning = variant->binning_pass,
      .nir_from_spirv = nir_from_spirv,
      .nir_final = ralloc_strdup(pipeline->executables_mem_ctx, variant->disasm_info.nir),
      .disasm = ralloc_strdup(pipeline->executables_mem_ctx, variant->disasm_info.disasm),
   };

   util_dynarray_append(&pipeline->executables, struct tu_pipeline_executable, exe);
}

static bool
can_remove_out_var(nir_variable *var, void *data)
{
   return !var->data.explicit_xfb_buffer && !var->data.explicit_xfb_stride;
}

static void
tu_link_shaders(struct tu_pipeline_builder *builder,
                nir_shader **shaders, unsigned shaders_count)
{
   nir_shader *consumer = NULL;
   for (gl_shader_stage stage = (gl_shader_stage) (shaders_count - 1);
        stage >= MESA_SHADER_VERTEX; stage = (gl_shader_stage) (stage - 1)) {
      if (!shaders[stage])
         continue;

      nir_shader *producer = shaders[stage];
      if (!consumer) {
         consumer = producer;
         continue;
      }

      if (nir_link_opt_varyings(producer, consumer)) {
         NIR_PASS_V(consumer, nir_opt_constant_folding);
         NIR_PASS_V(consumer, nir_opt_algebraic);
         NIR_PASS_V(consumer, nir_opt_dce);
      }

      const nir_remove_dead_variables_options out_var_opts = {
         .can_remove_var = can_remove_out_var,
      };
      NIR_PASS_V(producer, nir_remove_dead_variables, nir_var_shader_out, &out_var_opts);

      NIR_PASS_V(consumer, nir_remove_dead_variables, nir_var_shader_in, NULL);

      bool progress = nir_remove_unused_varyings(producer, consumer);

      nir_compact_varyings(producer, consumer, true);
      if (progress) {
         if (nir_lower_global_vars_to_local(producer)) {
            /* Remove dead writes, which can remove input loads */
            NIR_PASS_V(producer, nir_remove_dead_variables, nir_var_shader_temp, NULL);
            NIR_PASS_V(producer, nir_opt_dce);
         }
         nir_lower_global_vars_to_local(consumer);
      }

      consumer = producer;
   }
}

static void
tu_shader_key_init(struct tu_shader_key *key,
                   const VkPipelineShaderStageCreateInfo *stage_info,
                   struct tu_device *dev)
{
   enum ir3_wavesize_option api_wavesize, real_wavesize;

   if (stage_info) {
      if (stage_info->flags &
          VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT) {
         api_wavesize = real_wavesize = IR3_SINGLE_OR_DOUBLE;
      } else {
         const VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *size_info =
            vk_find_struct_const(stage_info->pNext,
                                 PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO);

         if (size_info) {
            if (size_info->requiredSubgroupSize == dev->compiler->threadsize_base) {
               api_wavesize = IR3_SINGLE_ONLY;
            } else {
               assert(size_info->requiredSubgroupSize == dev->compiler->threadsize_base * 2);
               api_wavesize = IR3_DOUBLE_ONLY;
            }
         } else {
            /* Match the exposed subgroupSize. */
            api_wavesize = IR3_DOUBLE_ONLY;
         }

         if (stage_info->flags &
             VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT)
            real_wavesize = api_wavesize;
         else if (api_wavesize == IR3_SINGLE_ONLY)
            real_wavesize = IR3_SINGLE_ONLY;
         else
            real_wavesize = IR3_SINGLE_OR_DOUBLE;
      }
   } else {
      api_wavesize = real_wavesize = IR3_SINGLE_OR_DOUBLE;
   }

   key->api_wavesize = api_wavesize;
   key->real_wavesize = real_wavesize;
}

static void
tu_hash_stage(struct mesa_sha1 *ctx,
              const VkPipelineShaderStageCreateInfo *stage,
              const nir_shader *nir,
              const struct tu_shader_key *key)
{

   if (nir) {
      struct blob blob;
      blob_init(&blob);
      nir_serialize(&blob, nir, true);
      _mesa_sha1_update(ctx, blob.data, blob.size);
      blob_finish(&blob);
   } else {
      unsigned char stage_hash[SHA1_DIGEST_LENGTH];
      vk_pipeline_hash_shader_stage(stage, NULL, stage_hash);
      _mesa_sha1_update(ctx, stage_hash, sizeof(stage_hash));
   }
   _mesa_sha1_update(ctx, key, sizeof(*key));
}

/* Hash flags which can affect ir3 shader compilation which aren't known until
 * logical device creation.
 */
static void
tu_hash_compiler(struct mesa_sha1 *ctx, const struct ir3_compiler *compiler)
{
   _mesa_sha1_update(ctx, &compiler->options.robust_buffer_access2,
                     sizeof(compiler->options.robust_buffer_access2));
   _mesa_sha1_update(ctx, &ir3_shader_debug, sizeof(ir3_shader_debug));
}

static void
tu_hash_shaders(unsigned char *hash,
                const VkPipelineShaderStageCreateInfo **stages,
                nir_shader *const *nir,
                const struct tu_pipeline_layout *layout,
                const struct tu_shader_key *keys,
                const struct ir3_shader_key *ir3_key,
                VkGraphicsPipelineLibraryFlagsEXT state,
                const struct ir3_compiler *compiler)
{
   struct mesa_sha1 ctx;

   _mesa_sha1_init(&ctx);

   if (layout)
      _mesa_sha1_update(&ctx, layout->sha1, sizeof(layout->sha1));

   _mesa_sha1_update(&ctx, ir3_key, sizeof(ir3_key));

   for (int i = 0; i < MESA_SHADER_STAGES; ++i) {
      if (stages[i] || nir[i]) {
         tu_hash_stage(&ctx, stages[i], nir[i], &keys[i]);
      }
   }
   _mesa_sha1_update(&ctx, &state, sizeof(state));
   tu_hash_compiler(&ctx, compiler);
   _mesa_sha1_final(&ctx, hash);
}

static void
tu_hash_compute(unsigned char *hash,
                const VkPipelineShaderStageCreateInfo *stage,
                const struct tu_pipeline_layout *layout,
                const struct tu_shader_key *key,
                const struct ir3_compiler *compiler)
{
   struct mesa_sha1 ctx;

   _mesa_sha1_init(&ctx);

   if (layout)
      _mesa_sha1_update(&ctx, layout->sha1, sizeof(layout->sha1));

   tu_hash_stage(&ctx, stage, NULL, key);

   tu_hash_compiler(&ctx, compiler);
   _mesa_sha1_final(&ctx, hash);
}

static bool
tu_shaders_serialize(struct vk_pipeline_cache_object *object,
                     struct blob *blob);

static struct vk_pipeline_cache_object *
tu_shaders_deserialize(struct vk_pipeline_cache *cache,
                       const void *key_data,
                       size_t key_size,
                       struct blob_reader *blob);

static void
tu_shaders_destroy(struct vk_device *device,
                   struct vk_pipeline_cache_object *object)
{
   struct tu_compiled_shaders *shaders =
      container_of(object, struct tu_compiled_shaders, base);

   for (unsigned i = 0; i < ARRAY_SIZE(shaders->variants); i++)
      ralloc_free(shaders->variants[i]);

   for (unsigned i = 0; i < ARRAY_SIZE(shaders->safe_const_variants); i++)
      ralloc_free(shaders->safe_const_variants[i]);

   vk_pipeline_cache_object_finish(&shaders->base);
   vk_free(&device->alloc, shaders);
}

const struct vk_pipeline_cache_object_ops tu_shaders_ops = {
   .serialize = tu_shaders_serialize,
   .deserialize = tu_shaders_deserialize,
   .destroy = tu_shaders_destroy,
};

static struct tu_compiled_shaders *
tu_shaders_init(struct tu_device *dev, const void *key_data, size_t key_size)
{
   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct tu_compiled_shaders, shaders, 1);
   VK_MULTIALLOC_DECL_SIZE(&ma, char, obj_key_data, key_size);

   if (!vk_multialloc_zalloc(&ma, &dev->vk.alloc,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE))
      return NULL;

   memcpy(obj_key_data, key_data, key_size);
   vk_pipeline_cache_object_init(&dev->vk, &shaders->base,
                                 &tu_shaders_ops, obj_key_data, key_size);

   return shaders;
}

static bool
tu_shaders_serialize(struct vk_pipeline_cache_object *object,
                     struct blob *blob)
{
   struct tu_compiled_shaders *shaders =
      container_of(object, struct tu_compiled_shaders, base);

   blob_write_bytes(blob, shaders->const_state, sizeof(shaders->const_state));
   blob_write_uint8(blob, shaders->active_desc_sets);

   for (unsigned i = 0; i < ARRAY_SIZE(shaders->variants); i++) {
      if (shaders->variants[i]) {
         blob_write_uint8(blob, 1);
         ir3_store_variant(blob, shaders->variants[i]);
      } else {
         blob_write_uint8(blob, 0);
      }

      if (shaders->safe_const_variants[i]) {
         blob_write_uint8(blob, 1);
         ir3_store_variant(blob, shaders->safe_const_variants[i]);
      } else {
         blob_write_uint8(blob, 0);
      }
   }

   return true;
}

static struct vk_pipeline_cache_object *
tu_shaders_deserialize(struct vk_pipeline_cache *cache,
                       const void *key_data,
                       size_t key_size,
                       struct blob_reader *blob)
{
   struct tu_device *dev =
      container_of(cache->base.device, struct tu_device, vk);
   struct tu_compiled_shaders *shaders =
      tu_shaders_init(dev, key_data, key_size);

   if (!shaders)
      return NULL;

   blob_copy_bytes(blob, shaders->const_state, sizeof(shaders->const_state));
   shaders->active_desc_sets = blob_read_uint8(blob);

   for (unsigned i = 0; i < ARRAY_SIZE(shaders->variants); i++) {
      if (blob_read_uint8(blob)) {
         shaders->variants[i] = ir3_retrieve_variant(blob, dev->compiler, NULL);
      }

      if (blob_read_uint8(blob)) {
         shaders->safe_const_variants[i] = ir3_retrieve_variant(blob, dev->compiler, NULL);
      }
   }

   return &shaders->base;
}

static struct tu_compiled_shaders *
tu_pipeline_cache_lookup(struct vk_pipeline_cache *cache,
                         const void *key_data, size_t key_size,
                         bool *application_cache_hit)
{
   struct vk_pipeline_cache_object *object =
      vk_pipeline_cache_lookup_object(cache, key_data, key_size,
                                      &tu_shaders_ops, application_cache_hit);
   if (object)
      return container_of(object, struct tu_compiled_shaders, base);
   else
      return NULL;
}

static struct tu_compiled_shaders *
tu_pipeline_cache_insert(struct vk_pipeline_cache *cache,
                         struct tu_compiled_shaders *shaders)
{
   struct vk_pipeline_cache_object *object =
      vk_pipeline_cache_add_object(cache, &shaders->base);
   return container_of(object, struct tu_compiled_shaders, base);
}

static bool
tu_nir_shaders_serialize(struct vk_pipeline_cache_object *object,
                         struct blob *blob);

static struct vk_pipeline_cache_object *
tu_nir_shaders_deserialize(struct vk_pipeline_cache *cache,
                           const void *key_data,
                           size_t key_size,
                           struct blob_reader *blob);

static void
tu_nir_shaders_destroy(struct vk_device *device,
                       struct vk_pipeline_cache_object *object)
{
   struct tu_nir_shaders *shaders =
      container_of(object, struct tu_nir_shaders, base);

   for (unsigned i = 0; i < ARRAY_SIZE(shaders->nir); i++)
      ralloc_free(shaders->nir[i]);

   vk_pipeline_cache_object_finish(&shaders->base);
   vk_free(&device->alloc, shaders);
}

const struct vk_pipeline_cache_object_ops tu_nir_shaders_ops = {
   .serialize = tu_nir_shaders_serialize,
   .deserialize = tu_nir_shaders_deserialize,
   .destroy = tu_nir_shaders_destroy,
};

static struct tu_nir_shaders *
tu_nir_shaders_init(struct tu_device *dev, const void *key_data, size_t key_size)
{
   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct tu_nir_shaders, shaders, 1);
   VK_MULTIALLOC_DECL_SIZE(&ma, char, obj_key_data, key_size);

   if (!vk_multialloc_zalloc(&ma, &dev->vk.alloc,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE))
      return NULL;

   memcpy(obj_key_data, key_data, key_size);
   vk_pipeline_cache_object_init(&dev->vk, &shaders->base,
                                 &tu_nir_shaders_ops, obj_key_data, key_size);

   return shaders;
}

static bool
tu_nir_shaders_serialize(struct vk_pipeline_cache_object *object,
                         struct blob *blob)
{
   struct tu_nir_shaders *shaders =
      container_of(object, struct tu_nir_shaders, base);

   for (unsigned i = 0; i < ARRAY_SIZE(shaders->nir); i++) {
      if (shaders->nir[i]) {
         blob_write_uint8(blob, 1);
         nir_serialize(blob, shaders->nir[i], true);
      } else {
         blob_write_uint8(blob, 0);
      }
   }

   return true;
}

static struct vk_pipeline_cache_object *
tu_nir_shaders_deserialize(struct vk_pipeline_cache *cache,
                           const void *key_data,
                           size_t key_size,
                           struct blob_reader *blob)
{
   struct tu_device *dev =
      container_of(cache->base.device, struct tu_device, vk);
   struct tu_nir_shaders *shaders =
      tu_nir_shaders_init(dev, key_data, key_size);

   if (!shaders)
      return NULL;

   for (unsigned i = 0; i < ARRAY_SIZE(shaders->nir); i++) {
      if (blob_read_uint8(blob)) {
         shaders->nir[i] =
            nir_deserialize(NULL, ir3_get_compiler_options(dev->compiler), blob);
      }
   }

   return &shaders->base;
}

static struct tu_nir_shaders *
tu_nir_cache_lookup(struct vk_pipeline_cache *cache,
                    const void *key_data, size_t key_size,
                    bool *application_cache_hit)
{
   struct vk_pipeline_cache_object *object =
      vk_pipeline_cache_lookup_object(cache, key_data, key_size,
                                      &tu_nir_shaders_ops, application_cache_hit);
   if (object)
      return container_of(object, struct tu_nir_shaders, base);
   else
      return NULL;
}

static struct tu_nir_shaders *
tu_nir_cache_insert(struct vk_pipeline_cache *cache,
                    struct tu_nir_shaders *shaders)
{
   struct vk_pipeline_cache_object *object =
      vk_pipeline_cache_add_object(cache, &shaders->base);
   return container_of(object, struct tu_nir_shaders, base);
}

static VkResult
tu_pipeline_builder_compile_shaders(struct tu_pipeline_builder *builder,
                                    struct tu_pipeline *pipeline)
{
   VkResult result = VK_SUCCESS;
   const struct ir3_compiler *compiler = builder->device->compiler;
   const VkPipelineShaderStageCreateInfo *stage_infos[MESA_SHADER_STAGES] = {
      NULL
   };
   VkPipelineCreationFeedback pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };
   VkPipelineCreationFeedback stage_feedbacks[MESA_SHADER_STAGES] = { 0 };

   const bool executable_info =
      builder->create_info->flags &
      VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;

   int64_t pipeline_start = os_time_get_nano();

   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(builder->create_info->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

   bool must_compile =
      builder->state & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT;
   for (uint32_t i = 0; i < builder->create_info->stageCount; i++) {
      if (!(builder->active_stages & builder->create_info->pStages[i].stage))
         continue;

      gl_shader_stage stage =
         vk_to_mesa_shader_stage(builder->create_info->pStages[i].stage);
      stage_infos[stage] = &builder->create_info->pStages[i];
      must_compile = true;
   }

   if (tu6_shared_constants_enable(&builder->layout, builder->device->compiler)) {
      pipeline->shared_consts = (struct tu_push_constant_range) {
         .lo = 0,
         .dwords = builder->layout.push_constant_size / 4,
      };
   }

   /* Forward declare everything due to the goto usage */
   nir_shader *nir[ARRAY_SIZE(stage_infos)] = { NULL };
   nir_shader *post_link_nir[ARRAY_SIZE(nir)] = { NULL };
   struct tu_shader *shaders[ARRAY_SIZE(nir)] = { NULL };
   char *nir_initial_disasm[ARRAY_SIZE(stage_infos)] = { NULL };
   struct ir3_shader_variant *safe_const_variants[ARRAY_SIZE(nir)] = { NULL };
   struct tu_shader *last_shader = NULL;

   uint32_t desc_sets = 0;
   uint32_t safe_constlens = 0;

   struct tu_shader_key keys[ARRAY_SIZE(stage_infos)] = { };
   for (gl_shader_stage stage = MESA_SHADER_VERTEX;
        stage < ARRAY_SIZE(keys); stage = (gl_shader_stage) (stage+1)) {
      tu_shader_key_init(&keys[stage], stage_infos[stage], builder->device);
   }

   if (builder->create_info->flags &
       VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT) {
      for (unsigned i = 0; i < builder->num_libraries; i++) {
         struct tu_graphics_lib_pipeline *library = builder->libraries[i];

         for (unsigned j = 0; j < ARRAY_SIZE(library->shaders); j++) {
            if (library->shaders[j].nir) {
               assert(!nir[j]);
               nir[j] = nir_shader_clone(builder->mem_ctx,
                     library->shaders[j].nir);
               keys[j] = library->shaders[j].key;
               must_compile = true;
            }
         }
      }
   }

   struct ir3_shader_key ir3_key = {};
   tu_pipeline_shader_key_init(&ir3_key, pipeline, builder, nir);

   struct tu_compiled_shaders *compiled_shaders = NULL;
   struct tu_nir_shaders *nir_shaders = NULL;
   if (!must_compile)
      goto done;

   if (builder->state &
       VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
      keys[MESA_SHADER_VERTEX].multiview_mask =
         builder->graphics_state.rp->view_mask;
   }

   if (builder->state & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
      keys[MESA_SHADER_FRAGMENT].multiview_mask =
         builder->graphics_state.rp->view_mask;
      keys[MESA_SHADER_FRAGMENT].force_sample_interp = ir3_key.sample_shading;
      keys[MESA_SHADER_FRAGMENT].fragment_density_map =
         builder->fragment_density_map;
      keys[MESA_SHADER_FRAGMENT].unscaled_input_fragcoord =
         builder->unscaled_input_fragcoord;
   }

   unsigned char pipeline_sha1[20];
   tu_hash_shaders(pipeline_sha1, stage_infos, nir, &builder->layout, keys,
                   &ir3_key, builder->state, compiler);

   unsigned char nir_sha1[21];
   memcpy(nir_sha1, pipeline_sha1, sizeof(pipeline_sha1));
   nir_sha1[20] = 'N';

   if (!executable_info) {
      bool cache_hit = false;
      bool application_cache_hit = false;

      compiled_shaders =
         tu_pipeline_cache_lookup(builder->cache, &pipeline_sha1,
                                  sizeof(pipeline_sha1),
                                  &application_cache_hit);

      cache_hit = !!compiled_shaders;

      /* If the user asks us to keep the NIR around, we need to have it for a
       * successful cache hit. If we only have a "partial" cache hit, then we
       * still need to recompile in order to get the NIR.
       */
      if (compiled_shaders &&
          (builder->create_info->flags &
           VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT)) {
         bool nir_application_cache_hit = false;
         nir_shaders =
            tu_nir_cache_lookup(builder->cache, &nir_sha1,
                                sizeof(nir_sha1),
                                &nir_application_cache_hit);

         application_cache_hit &= nir_application_cache_hit;
         cache_hit &= !!nir_shaders;
      }

      if (application_cache_hit && builder->cache != builder->device->mem_cache) {
         pipeline_feedback.flags |=
            VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      }

      if (cache_hit)
         goto done;
   }

   if (builder->create_info->flags &
       VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) {
      return VK_PIPELINE_COMPILE_REQUIRED;
   }

   for (gl_shader_stage stage = MESA_SHADER_VERTEX; stage < ARRAY_SIZE(nir);
        stage = (gl_shader_stage) (stage + 1)) {
      const VkPipelineShaderStageCreateInfo *stage_info = stage_infos[stage];
      if (!stage_info)
         continue;

      int64_t stage_start = os_time_get_nano();

      nir[stage] = tu_spirv_to_nir(builder->device, builder->mem_ctx, stage_info, stage);
      if (!nir[stage]) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      stage_feedbacks[stage].flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;
      stage_feedbacks[stage].duration += os_time_get_nano() - stage_start;
   }

   if (!nir[MESA_SHADER_FRAGMENT] &&
       (builder->state & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT)) {
         const nir_shader_compiler_options *nir_options =
            ir3_get_compiler_options(builder->device->compiler);
         nir_builder fs_b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                                           nir_options,
                                                           "noop_fs");
         nir[MESA_SHADER_FRAGMENT] = fs_b.shader;
   }

   if (executable_info) {
         for (gl_shader_stage stage = MESA_SHADER_VERTEX;
              stage < ARRAY_SIZE(nir);
              stage = (gl_shader_stage) (stage + 1)) {
         if (!nir[stage])
            continue;

         nir_initial_disasm[stage] =
            nir_shader_as_str(nir[stage], pipeline->executables_mem_ctx);
         }
   }

   tu_link_shaders(builder, nir, ARRAY_SIZE(nir));

   if (builder->create_info->flags &
       VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT) {
      nir_shaders =
         tu_nir_shaders_init(builder->device, &nir_sha1, sizeof(nir_sha1));
      for (gl_shader_stage stage = MESA_SHADER_VERTEX;
           stage < ARRAY_SIZE(nir); stage = (gl_shader_stage) (stage + 1)) {
         if (!nir[stage])
            continue;

         nir_shaders->nir[stage] = nir_shader_clone(NULL, nir[stage]);
      }

      nir_shaders = tu_nir_cache_insert(builder->cache, nir_shaders);

      if (compiled_shaders)
         goto done;
   }

   compiled_shaders =
      tu_shaders_init(builder->device, &pipeline_sha1, sizeof(pipeline_sha1));

   if (!compiled_shaders) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   desc_sets = 0;
   for (gl_shader_stage stage = MESA_SHADER_VERTEX; stage < ARRAY_SIZE(nir);
        stage = (gl_shader_stage) (stage + 1)) {
      if (!nir[stage])
         continue;

      int64_t stage_start = os_time_get_nano();

      struct tu_shader *shader =
         tu_shader_create(builder->device, nir[stage], &keys[stage],
                          &builder->layout, builder->alloc);
      if (!shader) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      /* In SPIR-V generated from GLSL, the primitive mode is specified in the
       * tessellation evaluation shader, but in SPIR-V generated from HLSL,
       * the mode is specified in the tessellation control shader. */
      if ((stage == MESA_SHADER_TESS_EVAL || stage == MESA_SHADER_TESS_CTRL) &&
          ir3_key.tessellation == IR3_TESS_NONE) {
         ir3_key.tessellation = tu6_get_tessmode(shader);
      }

      if (stage > MESA_SHADER_TESS_CTRL) {
         if (stage == MESA_SHADER_FRAGMENT) {
            ir3_key.tcs_store_primid = ir3_key.tcs_store_primid ||
               (nir[stage]->info.inputs_read & (1ull << VARYING_SLOT_PRIMITIVE_ID));
         } else {
            ir3_key.tcs_store_primid = ir3_key.tcs_store_primid ||
               BITSET_TEST(nir[stage]->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);
         }
      }

      /* Keep track of the status of each shader's active descriptor sets,
       * which is set in tu_lower_io. */
      desc_sets |= shader->active_desc_sets;

      shaders[stage] = shader;

      stage_feedbacks[stage].duration += os_time_get_nano() - stage_start;
   }

   /* In the the tess-but-not-FS case we don't know whether the FS will read
    * PrimID so we need to unconditionally store it.
    */
   if (nir[MESA_SHADER_TESS_CTRL] && !nir[MESA_SHADER_FRAGMENT])
      ir3_key.tcs_store_primid = true;

   last_shader = shaders[MESA_SHADER_GEOMETRY];
   if (!last_shader)
      last_shader = shaders[MESA_SHADER_TESS_EVAL];
   if (!last_shader)
      last_shader = shaders[MESA_SHADER_VERTEX];

   compiled_shaders->active_desc_sets = desc_sets;

   for (gl_shader_stage stage = MESA_SHADER_VERTEX;
        stage < ARRAY_SIZE(shaders); stage = (gl_shader_stage) (stage + 1)) {
      if (!shaders[stage])
         continue;

      int64_t stage_start = os_time_get_nano();

      compiled_shaders->variants[stage] =
         ir3_shader_create_variant(shaders[stage]->ir3_shader, &ir3_key,
                                   executable_info);
      if (!compiled_shaders->variants[stage])
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      compiled_shaders->const_state[stage] = shaders[stage]->const_state;

      stage_feedbacks[stage].duration += os_time_get_nano() - stage_start;
   }

   safe_constlens = ir3_trim_constlen(compiled_shaders->variants, compiler);

   ir3_key.safe_constlen = true;

   for (gl_shader_stage stage = MESA_SHADER_VERTEX;
        stage < ARRAY_SIZE(shaders); stage = (gl_shader_stage) (stage + 1)) {
      if (!shaders[stage])
         continue;

      if (safe_constlens & (1 << stage)) {
         int64_t stage_start = os_time_get_nano();

         ralloc_free(compiled_shaders->variants[stage]);
         compiled_shaders->variants[stage] =
            ir3_shader_create_variant(shaders[stage]->ir3_shader, &ir3_key,
                                      executable_info);
         if (!compiled_shaders->variants[stage]) {
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto fail;
         }

         stage_feedbacks[stage].duration += os_time_get_nano() - stage_start;
      } else if (contains_all_shader_state(builder->state)) {
         compiled_shaders->safe_const_variants[stage] =
            ir3_shader_create_variant(shaders[stage]->ir3_shader, &ir3_key,
                                      executable_info);
         if (!compiled_shaders->variants[stage]) {
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto fail;
         }
      }
   }

   ir3_key.safe_constlen = false;

   for (gl_shader_stage stage = MESA_SHADER_VERTEX; stage < ARRAY_SIZE(nir);
        stage = (gl_shader_stage) (stage + 1)) {
      if (shaders[stage]) {
         tu_shader_destroy(builder->device, shaders[stage], builder->alloc);
      }
   }

   compiled_shaders =
      tu_pipeline_cache_insert(builder->cache, compiled_shaders);

done:;

   if (compiled_shaders) {
      for (gl_shader_stage stage = MESA_SHADER_VERTEX;
           stage < ARRAY_SIZE(nir); stage = (gl_shader_stage) (stage + 1)) {
         if (compiled_shaders->variants[stage]) {
            tu_append_executable(pipeline, compiled_shaders->variants[stage],
               nir_initial_disasm[stage]);
            builder->variants[stage] = compiled_shaders->variants[stage];
            safe_const_variants[stage] =
               compiled_shaders->safe_const_variants[stage];
            builder->const_state[stage] =
               compiled_shaders->const_state[stage];
         }
      }
   }

   if (nir_shaders) {
      for (gl_shader_stage stage = MESA_SHADER_VERTEX;
           stage < ARRAY_SIZE(nir); stage = (gl_shader_stage) (stage + 1)) {
         if (nir_shaders->nir[stage]) {
            post_link_nir[stage] = nir_shaders->nir[stage];
         }
      }
   }

   /* In the case where we're building a library without link-time
    * optimization but with sub-libraries that retain LTO info, we should
    * retain it ourselves in case another pipeline includes us with LTO.
    */
   for (unsigned i = 0; i < builder->num_libraries; i++) {
      struct tu_graphics_lib_pipeline *library = builder->libraries[i];
      for (gl_shader_stage stage = MESA_SHADER_VERTEX;
           stage < ARRAY_SIZE(library->shaders);
           stage = (gl_shader_stage) (stage + 1)) {
         if (!post_link_nir[stage] && library->shaders[stage].nir) {
            post_link_nir[stage] = library->shaders[stage].nir;
            keys[stage] = library->shaders[stage].key;
         }
      }
   }

   if (!(builder->create_info->flags &
         VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT)) {
      for (unsigned i = 0; i < builder->num_libraries; i++) {
         struct tu_graphics_lib_pipeline *library = builder->libraries[i];
         for (gl_shader_stage stage = MESA_SHADER_VERTEX;
              stage < ARRAY_SIZE(library->shaders);
              stage = (gl_shader_stage) (stage + 1)) {
            if (library->shaders[stage].variant) {
               assert(!builder->variants[stage]);
               builder->variants[stage] = library->shaders[stage].variant;
               safe_const_variants[stage] =
                  library->shaders[stage].safe_const_variant;
               builder->const_state[stage] =
                  library->shaders[stage].const_state;
               post_link_nir[stage] = library->shaders[stage].nir;
            }
         }
      }

      /* Because we added more variants, we need to trim constlen again.
       */
      if (builder->num_libraries > 0) {
         uint32_t safe_constlens = ir3_trim_constlen(builder->variants, compiler);
         for (gl_shader_stage stage = MESA_SHADER_VERTEX;
              stage < ARRAY_SIZE(builder->variants);
              stage = (gl_shader_stage) (stage + 1)) {
            if (safe_constlens & (1u << stage))
               builder->variants[stage] = safe_const_variants[stage];
         }
      }
   }

   if (compiled_shaders)
      pipeline->active_desc_sets = compiled_shaders->active_desc_sets;

   for (unsigned i = 0; i < builder->num_libraries; i++) {
      struct tu_graphics_lib_pipeline *library = builder->libraries[i];
      pipeline->active_desc_sets |= library->base.active_desc_sets;
   }

   if (compiled_shaders && compiled_shaders->variants[MESA_SHADER_TESS_CTRL]) {
      pipeline->tess.patch_type =
         compiled_shaders->variants[MESA_SHADER_TESS_CTRL]->key.tessellation;
   }

   if (pipeline_contains_all_shader_state(pipeline)) {
      struct ir3_shader_variant *vs =
         builder->variants[MESA_SHADER_VERTEX];

      struct ir3_shader_variant *variant;
      if (!vs->stream_output.num_outputs && ir3_has_binning_vs(&vs->key)) {
         tu_append_executable(pipeline, vs->binning, NULL);
         variant = vs->binning;
      } else {
         variant = vs;
      }

      builder->binning_variant = variant;

      builder->compiled_shaders = compiled_shaders;

      /* It doesn't make much sense to use RETAIN_LINK_TIME_OPTIMIZATION_INFO
       * when compiling all stages, but make sure we don't leak.
       */
      if (nir_shaders)
         vk_pipeline_cache_object_unref(&builder->device->vk,
                                        &nir_shaders->base);
   } else {
      struct tu_graphics_lib_pipeline *library =
         tu_pipeline_to_graphics_lib(pipeline);
      library->compiled_shaders = compiled_shaders;
      library->nir_shaders = nir_shaders;
      library->ir3_key = ir3_key;
      for (gl_shader_stage stage = MESA_SHADER_VERTEX;
           stage < ARRAY_SIZE(library->shaders);
           stage = (gl_shader_stage) (stage + 1)) {
         library->shaders[stage].nir = post_link_nir[stage];
         library->shaders[stage].key = keys[stage];
         library->shaders[stage].const_state = builder->const_state[stage];
         library->shaders[stage].variant = builder->variants[stage];
         library->shaders[stage].safe_const_variant =
            safe_const_variants[stage];
      }
   }

   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;
   if (creation_feedback) {
      *creation_feedback->pPipelineCreationFeedback = pipeline_feedback;

      for (uint32_t i = 0; i < builder->create_info->stageCount; i++) {
         gl_shader_stage s =
            vk_to_mesa_shader_stage(builder->create_info->pStages[i].stage);
         creation_feedback->pPipelineStageCreationFeedbacks[i] = stage_feedbacks[s];
      }
   }

   return VK_SUCCESS;

fail:
   for (gl_shader_stage stage = MESA_SHADER_VERTEX; stage < ARRAY_SIZE(nir);
        stage = (gl_shader_stage) (stage + 1)) {
      if (shaders[stage]) {
         tu_shader_destroy(builder->device, shaders[stage], builder->alloc);
      }
   }

   if (compiled_shaders)
      vk_pipeline_cache_object_unref(&builder->device->vk,
                                     &compiled_shaders->base);

   if (nir_shaders)
      vk_pipeline_cache_object_unref(&builder->device->vk,
                                     &nir_shaders->base);

   return result;
}

static void
tu_pipeline_builder_parse_libraries(struct tu_pipeline_builder *builder,
                                    struct tu_pipeline *pipeline)
{
   const VkPipelineLibraryCreateInfoKHR *library_info =
      vk_find_struct_const(builder->create_info->pNext,
                           PIPELINE_LIBRARY_CREATE_INFO_KHR);

   if (library_info) {
      assert(library_info->libraryCount <= MAX_LIBRARIES);
      builder->num_libraries = library_info->libraryCount;
      for (unsigned i = 0; i < library_info->libraryCount; i++) {
         TU_FROM_HANDLE(tu_pipeline, library, library_info->pLibraries[i]);
         builder->libraries[i] = tu_pipeline_to_graphics_lib(library);
      }
   }

   /* Merge in the state from libraries. The program state is a bit special
    * and is handled separately.
    */
   if (pipeline->type == TU_PIPELINE_GRAPHICS_LIB)
      tu_pipeline_to_graphics_lib(pipeline)->state = builder->state;
   for (unsigned i = 0; i < builder->num_libraries; i++) {
      struct tu_graphics_lib_pipeline *library = builder->libraries[i];
      if (pipeline->type == TU_PIPELINE_GRAPHICS_LIB)
         tu_pipeline_to_graphics_lib(pipeline)->state |= library->state;

      if (library->state &
          VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT) {
         pipeline->shared_consts = library->base.shared_consts;
      }

      if (library->state &
          VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
         pipeline->tess = library->base.tess;
      }

      if (library->state &
          VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
         pipeline->ds = library->base.ds;
         pipeline->lrz.fs = library->base.lrz.fs;
         pipeline->lrz.lrz_status |= library->base.lrz.lrz_status;
         pipeline->lrz.force_late_z |= library->base.lrz.force_late_z;
         pipeline->shared_consts = library->base.shared_consts;
      }

      if (library->state &
          VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT) {
         pipeline->output = library->base.output;
         pipeline->lrz.lrz_status |= library->base.lrz.lrz_status;
         pipeline->lrz.force_late_z |= library->base.lrz.force_late_z;
         pipeline->prim_order = library->base.prim_order;
      }

      if ((library->state &
           VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) &&
          (library->state &
           VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT)) {
         pipeline->prim_order = library->base.prim_order;
      }

      pipeline->set_state_mask |= library->base.set_state_mask;

      u_foreach_bit (i, library->base.set_state_mask) {
         pipeline->dynamic_state[i] = library->base.dynamic_state[i];
      }

      if (contains_all_shader_state(library->state)) {
         pipeline->program = library->base.program;
         pipeline->load_state = library->base.load_state;
      }

      vk_graphics_pipeline_state_merge(&builder->graphics_state,
                                       &library->graphics_state);
   }
}

static void
tu_pipeline_builder_parse_layout(struct tu_pipeline_builder *builder,
                                 struct tu_pipeline *pipeline)
{
   TU_FROM_HANDLE(tu_pipeline_layout, layout, builder->create_info->layout);

   if (layout) {
      /* Note: it's still valid to have a layout even if there are libraries.
       * This allows the app to e.g. overwrite an INDEPENDENT_SET layout with
       * a non-INDEPENDENT_SET layout which may make us use a faster path,
       * currently this just affects dynamic offset descriptors.
       */
      builder->layout = *layout;
   } else {
      for (unsigned i = 0; i < builder->num_libraries; i++) {
         struct tu_graphics_lib_pipeline *library = builder->libraries[i];
         builder->layout.num_sets = MAX2(builder->layout.num_sets,
                                         library->num_sets);
         for (unsigned j = 0; j < library->num_sets; j++) {
            if (library->layouts[i])
               builder->layout.set[i].layout = library->layouts[i];
         }

         builder->layout.push_constant_size = library->push_constant_size;
         builder->layout.independent_sets |= library->independent_sets;
      }

      tu_pipeline_layout_init(&builder->layout);
   }

   if (pipeline->type == TU_PIPELINE_GRAPHICS_LIB) {
      struct tu_graphics_lib_pipeline *library =
         tu_pipeline_to_graphics_lib(pipeline);
      library->num_sets = builder->layout.num_sets;
      for (unsigned i = 0; i < library->num_sets; i++) {
         library->layouts[i] = builder->layout.set[i].layout;
         if (library->layouts[i])
            vk_descriptor_set_layout_ref(&library->layouts[i]->vk);
      }
      library->push_constant_size = builder->layout.push_constant_size;
      library->independent_sets = builder->layout.independent_sets;
   }
}

static void
tu_pipeline_set_linkage(struct tu_program_descriptor_linkage *link,
                        struct tu_const_state *const_state,
                        struct ir3_shader_variant *v)
{
   link->const_state = *ir3_const_state(v);
   link->tu_const_state = *const_state;
   link->constlen = v->constlen;
}

static void
tu_pipeline_builder_parse_shader_stages(struct tu_pipeline_builder *builder,
                                        struct tu_pipeline *pipeline)
{
   struct tu_cs prog_cs;

   /* Emit HLSQ_xS_CNTL/HLSQ_SP_xS_CONFIG *first*, before emitting anything
    * else that could depend on that state (like push constants)
    *
    * Note also that this always uses the full VS even in binning pass.  The
    * binning pass variant has the same const layout as the full VS, and
    * the constlen for the VS will be the same or greater than the constlen
    * for the binning pass variant.  It is required that the constlen state
    * matches between binning and draw passes, as some parts of the push
    * consts are emitted in state groups that are shared between the binning
    * and draw passes.
    */
   tu_cs_begin_sub_stream(&pipeline->cs, 512, &prog_cs);
   tu6_emit_program_config(&prog_cs, builder);
   pipeline->program.config_state = tu_cs_end_draw_state(&pipeline->cs, &prog_cs);

   tu_cs_begin_sub_stream(&pipeline->cs, 512 + builder->additional_cs_reserve_size, &prog_cs);
   tu6_emit_program(&prog_cs, builder, false, pipeline);
   pipeline->program.state = tu_cs_end_draw_state(&pipeline->cs, &prog_cs);

   tu_cs_begin_sub_stream(&pipeline->cs, 512 + builder->additional_cs_reserve_size, &prog_cs);
   tu6_emit_program(&prog_cs, builder, true, pipeline);
   pipeline->program.binning_state = tu_cs_end_draw_state(&pipeline->cs, &prog_cs);

   for (unsigned i = 0; i < ARRAY_SIZE(builder->variants); i++) {
      if (!builder->variants[i])
         continue;

      tu_pipeline_set_linkage(&pipeline->program.link[i],
                              &builder->const_state[i],
                              builder->variants[i]);
   }

   struct ir3_shader_variant *vs = builder->variants[MESA_SHADER_VERTEX];
   struct ir3_shader_variant *hs = builder->variants[MESA_SHADER_TESS_CTRL];
   struct ir3_shader_variant *ds = builder->variants[MESA_SHADER_TESS_EVAL];
   struct ir3_shader_variant *gs = builder->variants[MESA_SHADER_GEOMETRY];
   if (hs) {
      pipeline->program.vs_param_stride = vs->output_size;
      pipeline->program.hs_param_stride = hs->output_size;
      pipeline->program.hs_vertices_out = hs->tess.tcs_vertices_out;

      const struct ir3_const_state *hs_const =
         &pipeline->program.link[MESA_SHADER_TESS_CTRL].const_state;
      unsigned hs_constlen =
         pipeline->program.link[MESA_SHADER_TESS_CTRL].constlen;
      uint32_t hs_base = hs_const->offsets.primitive_param;
      pipeline->program.hs_param_dwords =
         MIN2((hs_constlen - hs_base) * 4, 8);

      /* In SPIR-V generated from GLSL, the tessellation primitive params are
       * are specified in the tess eval shader, but in SPIR-V generated from
       * HLSL, they are specified in the tess control shader. */
      const struct ir3_shader_variant *tess =
         ds->tess.spacing == TESS_SPACING_UNSPECIFIED ? hs : ds;
      if (tess->tess.point_mode) {
         pipeline->program.tess_output_lower_left =
            pipeline->program.tess_output_upper_left = TESS_POINTS;
      } else if (tess->tess.primitive_mode == TESS_PRIMITIVE_ISOLINES) {
         pipeline->program.tess_output_lower_left =
            pipeline->program.tess_output_upper_left = TESS_LINES;
      } else if (tess->tess.ccw) {
         /* Tessellation orientation in HW is specified with a lower-left
          * origin, we need to swap them if the origin is upper-left.
          */
         pipeline->program.tess_output_lower_left = TESS_CCW_TRIS;
         pipeline->program.tess_output_upper_left = TESS_CW_TRIS;
      } else {
         pipeline->program.tess_output_lower_left = TESS_CW_TRIS;
         pipeline->program.tess_output_upper_left = TESS_CCW_TRIS;
      }

      switch (tess->tess.spacing) {
      case TESS_SPACING_EQUAL:
         pipeline->program.tess_spacing = TESS_EQUAL;
         break;
      case TESS_SPACING_FRACTIONAL_ODD:
         pipeline->program.tess_spacing = TESS_FRACTIONAL_ODD;
         break;
      case TESS_SPACING_FRACTIONAL_EVEN:
         pipeline->program.tess_spacing = TESS_FRACTIONAL_EVEN;
         break;
      case TESS_SPACING_UNSPECIFIED:
      default:
         unreachable("invalid tess spacing");
      }
   }

   struct ir3_shader_variant *last_shader;
   if (gs)
      last_shader = gs;
   else if (ds)
      last_shader = ds;
   else
      last_shader = vs;

   pipeline->program.per_view_viewport =
      !last_shader->writes_viewport &&
      builder->fragment_density_map &&
      builder->device->physical_device->info->a6xx.has_per_view_viewport;
}

static const enum mesa_vk_dynamic_graphics_state tu_vertex_input_state[] = {
   MESA_VK_DYNAMIC_VI,
};

static unsigned
tu6_vertex_input_size(struct tu_device *dev,
                      const struct vk_vertex_input_state *vi)
{
   return 1 + 2 * util_last_bit(vi->attributes_valid);
}

static void
tu6_emit_vertex_input(struct tu_cs *cs,
                      const struct vk_vertex_input_state *vi)
{
   unsigned attr_count = util_last_bit(vi->attributes_valid);
   if (attr_count != 0)
      tu_cs_emit_pkt4(cs, REG_A6XX_VFD_DECODE_INSTR(0), attr_count * 2);

   for (uint32_t loc = 0; loc < attr_count; loc++) {
      const struct vk_vertex_attribute_state *attr = &vi->attributes[loc];

      if (vi->attributes_valid & (1u << loc)) {
         const struct vk_vertex_binding_state *binding =
            &vi->bindings[attr->binding];

         enum pipe_format pipe_format = vk_format_to_pipe_format(attr->format);
         const struct tu_native_format format = tu6_format_vtx(pipe_format);
         tu_cs_emit(cs, A6XX_VFD_DECODE_INSTR(0,
                          .idx = attr->binding,
                          .offset = attr->offset,
                          .instanced = binding->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE,
                          .format = format.fmt,
                          .swap = format.swap,
                          .unk30 = 1,
                          ._float = !util_format_is_pure_integer(pipe_format)).value);
         tu_cs_emit(cs, A6XX_VFD_DECODE_STEP_RATE(0, binding->divisor).value);
      } else {
         tu_cs_emit(cs, 0);
         tu_cs_emit(cs, 0);
      }
   }
}

static const enum mesa_vk_dynamic_graphics_state tu_vertex_stride_state[] = {
   MESA_VK_DYNAMIC_VI_BINDINGS_VALID,
   MESA_VK_DYNAMIC_VI_BINDING_STRIDES,
};

static unsigned
tu6_vertex_stride_size(struct tu_device *dev,
                       const struct vk_vertex_input_state *vi)
{
   return 1 + 2 * util_last_bit(vi->bindings_valid);
}

static void
tu6_emit_vertex_stride(struct tu_cs *cs, const struct vk_vertex_input_state *vi)
{
   if (vi->bindings_valid) {
      unsigned bindings_count = util_last_bit(vi->bindings_valid);
      tu_cs_emit_pkt7(cs, CP_CONTEXT_REG_BUNCH, 2 * bindings_count);
      for (unsigned i = 0; i < bindings_count; i++) {
         tu_cs_emit(cs, REG_A6XX_VFD_FETCH_STRIDE(i));
         tu_cs_emit(cs, vi->bindings[i].stride);
      }
   }
}

static unsigned
tu6_vertex_stride_size_dyn(struct tu_device *dev,
                           const uint16_t *vi_binding_stride,
                           uint32_t bindings_valid)
{
   return 1 + 2 * util_last_bit(bindings_valid);
}

static void
tu6_emit_vertex_stride_dyn(struct tu_cs *cs, const uint16_t *vi_binding_stride,
                           uint32_t bindings_valid)
{
   if (bindings_valid) {
      unsigned bindings_count = util_last_bit(bindings_valid);
      tu_cs_emit_pkt7(cs, CP_CONTEXT_REG_BUNCH, 2 * bindings_count);
      for (unsigned i = 0; i < bindings_count; i++) {
         tu_cs_emit(cs, REG_A6XX_VFD_FETCH_STRIDE(i));
         tu_cs_emit(cs, vi_binding_stride[i]);
      }
   }
}

static const enum mesa_vk_dynamic_graphics_state tu_viewport_state[] = {
   MESA_VK_DYNAMIC_VP_VIEWPORTS,
   MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT,
   MESA_VK_DYNAMIC_VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE,
};

static unsigned
tu6_viewport_size(struct tu_device *dev, const struct vk_viewport_state *vp)
{
   return 1 + vp->viewport_count * 6 + 1 + vp->viewport_count * 2 +
      1 + vp->viewport_count * 2 + 5;
}

static void
tu6_emit_viewport(struct tu_cs *cs, const struct vk_viewport_state *vp)
{
   VkExtent2D guardband = {511, 511};

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_CL_VPORT_XOFFSET(0), vp->viewport_count * 6);
   for (uint32_t i = 0; i < vp->viewport_count; i++) {
      const VkViewport *viewport = &vp->viewports[i];
      float offsets[3];
      float scales[3];
      scales[0] = viewport->width / 2.0f;
      scales[1] = viewport->height / 2.0f;
      if (vp->depth_clip_negative_one_to_one) {
         scales[2] = 0.5 * (viewport->maxDepth - viewport->minDepth);
      } else {
         scales[2] = viewport->maxDepth - viewport->minDepth;
      }

      offsets[0] = viewport->x + scales[0];
      offsets[1] = viewport->y + scales[1];
      if (vp->depth_clip_negative_one_to_one) {
         offsets[2] = 0.5 * (viewport->minDepth + viewport->maxDepth);
      } else {
         offsets[2] = viewport->minDepth;
      }

      for (uint32_t j = 0; j < 3; j++) {
         tu_cs_emit(cs, fui(offsets[j]));
         tu_cs_emit(cs, fui(scales[j]));
      }

      guardband.width =
         MIN2(guardband.width, fd_calc_guardband(offsets[0], scales[0], false));
      guardband.height =
         MIN2(guardband.height, fd_calc_guardband(offsets[1], scales[1], false));
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL(0), vp->viewport_count * 2);
   for (uint32_t i = 0; i < vp->viewport_count; i++) {
      const VkViewport *viewport = &vp->viewports[i];
      VkOffset2D min;
      VkOffset2D max;
      min.x = (int32_t) viewport->x;
      max.x = (int32_t) ceilf(viewport->x + viewport->width);
      if (viewport->height >= 0.0f) {
         min.y = (int32_t) viewport->y;
         max.y = (int32_t) ceilf(viewport->y + viewport->height);
      } else {
         min.y = (int32_t)(viewport->y + viewport->height);
         max.y = (int32_t) ceilf(viewport->y);
      }
      /* the spec allows viewport->height to be 0.0f */
      if (min.y == max.y)
         max.y++;
      /* allow viewport->width = 0.0f for un-initialized viewports: */
      if (min.x == max.x)
         max.x++;

      min.x = MAX2(min.x, 0);
      min.y = MAX2(min.y, 0);
      max.x = MAX2(max.x, 1);
      max.y = MAX2(max.y, 1);

      assert(min.x < max.x);
      assert(min.y < max.y);

      tu_cs_emit(cs, A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_X(min.x) |
                     A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_Y(min.y));
      tu_cs_emit(cs, A6XX_GRAS_SC_VIEWPORT_SCISSOR_BR_X(max.x - 1) |
                     A6XX_GRAS_SC_VIEWPORT_SCISSOR_BR_Y(max.y - 1));
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_CL_Z_CLAMP(0), vp->viewport_count * 2);
   for (uint32_t i = 0; i < vp->viewport_count; i++) {
      const VkViewport *viewport = &vp->viewports[i];
      tu_cs_emit(cs, fui(MIN2(viewport->minDepth, viewport->maxDepth)));
      tu_cs_emit(cs, fui(MAX2(viewport->minDepth, viewport->maxDepth)));
   }
   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ, 1);
   tu_cs_emit(cs, A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ_HORZ(guardband.width) |
                  A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ_VERT(guardband.height));

   /* TODO: what to do about this and multi viewport ? */
   float z_clamp_min = vp->viewport_count ? MIN2(vp->viewports[0].minDepth, vp->viewports[0].maxDepth) : 0;
   float z_clamp_max = vp->viewport_count ? MAX2(vp->viewports[0].minDepth, vp->viewports[0].maxDepth) : 0;

   tu_cs_emit_regs(cs,
                   A6XX_RB_Z_CLAMP_MIN(z_clamp_min),
                   A6XX_RB_Z_CLAMP_MAX(z_clamp_max));
}

struct apply_viewport_state {
   struct vk_viewport_state vp;
   bool share_scale;
};

/* It's a hardware restriction that the window offset (i.e. bin.offset) must
 * be the same for all views. This means that GMEM coordinates cannot be a
 * simple scaling of framebuffer coordinates, because this would require us to
 * scale the window offset and the scale may be different per view. Instead we
 * have to apply a per-bin offset to the GMEM coordinate transform to make
 * sure that the window offset maps to itself. Specifically we need an offset
 * o to the transform:
 *
 * x' = s * x + o
 *
 * so that when we plug in the bin start b_s:
 * 
 * b_s = s * b_s + o
 *
 * and we get:
 *
 * o = b_s - s * b_s
 *
 * We use this form exactly, because we know the bin offset is a multiple of
 * the frag area so s * b_s is an integer and we can compute an exact result
 * easily.
 */

VkOffset2D
tu_fdm_per_bin_offset(VkExtent2D frag_area, VkRect2D bin)
{
   assert(bin.offset.x % frag_area.width == 0);
   assert(bin.offset.y % frag_area.height == 0);

   return (VkOffset2D) {
      bin.offset.x - bin.offset.x / frag_area.width,
      bin.offset.y - bin.offset.y / frag_area.height
   };
}

static void
fdm_apply_viewports(struct tu_cs *cs, void *data, VkRect2D bin, unsigned views,
                    VkExtent2D *frag_areas)
{
   const struct apply_viewport_state *state =
      (const struct apply_viewport_state *)data;

   struct vk_viewport_state vp = state->vp;

   for (unsigned i = 0; i < state->vp.viewport_count; i++) {
      /* Note: If we're using shared scaling, the scale should already be the
       * same across all views, we can pick any view. However the number
       * of viewports and number of views is not guaranteed the same, so we
       * need to pick the 0'th view which always exists to be safe.
       *
       * Conversly, if we're not using shared scaling then the rasterizer in
       * the original pipeline is using only the first viewport, so we need to
       * replicate it across all viewports.
       */
      VkExtent2D frag_area = state->share_scale ? frag_areas[0] : frag_areas[i];
      VkViewport viewport =
         state->share_scale ? state->vp.viewports[i] : state->vp.viewports[0];
      if (frag_area.width == 1 && frag_area.height == 1) {
         vp.viewports[i] = viewport;
         continue;
      }

      float scale_x = (float) 1.0f / frag_area.width;
      float scale_y = (float) 1.0f / frag_area.height;

      vp.viewports[i].minDepth = viewport.minDepth;
      vp.viewports[i].maxDepth = viewport.maxDepth;
      vp.viewports[i].width = viewport.width * scale_x;
      vp.viewports[i].height = viewport.height * scale_y;

      VkOffset2D offset = tu_fdm_per_bin_offset(frag_area, bin);

      vp.viewports[i].x = scale_x * viewport.x + offset.x;
      vp.viewports[i].y = scale_y * viewport.y + offset.y;
   }

   tu6_emit_viewport(cs, &vp);
}

static void
tu6_emit_viewport_fdm(struct tu_cs *cs, struct tu_cmd_buffer *cmd,
                      const struct vk_viewport_state *vp)
{
   unsigned num_views = MAX2(cmd->state.pass->num_views, 1);
   struct apply_viewport_state state = {
      .vp = *vp,
      .share_scale = !cmd->state.pipeline->base.program.per_view_viewport,
   };
   if (!state.share_scale)
      state.vp.viewport_count = num_views;
   unsigned size = tu6_viewport_size(cmd->device, &state.vp);
   tu_cs_begin_sub_stream(&cmd->sub_cs, size, cs);
   tu_create_fdm_bin_patchpoint(cmd, cs, size, fdm_apply_viewports, state);
}

static const enum mesa_vk_dynamic_graphics_state tu_scissor_state[] = {
   MESA_VK_DYNAMIC_VP_SCISSORS,
   MESA_VK_DYNAMIC_VP_SCISSOR_COUNT,
};

static unsigned
tu6_scissor_size(struct tu_device *dev, const struct vk_viewport_state *vp)
{
   return 1 + vp->scissor_count * 2;
}

void
tu6_emit_scissor(struct tu_cs *cs, const struct vk_viewport_state *vp)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SC_SCREEN_SCISSOR_TL(0), vp->scissor_count * 2);

   for (uint32_t i = 0; i < vp->scissor_count; i++) {
      const VkRect2D *scissor = &vp->scissors[i];

      uint32_t min_x = scissor->offset.x;
      uint32_t min_y = scissor->offset.y;
      uint32_t max_x = min_x + scissor->extent.width - 1;
      uint32_t max_y = min_y + scissor->extent.height - 1;

      if (!scissor->extent.width || !scissor->extent.height) {
         min_x = min_y = 1;
         max_x = max_y = 0;
      } else {
         /* avoid overflow */
         uint32_t scissor_max = BITFIELD_MASK(15);
         min_x = MIN2(scissor_max, min_x);
         min_y = MIN2(scissor_max, min_y);
         max_x = MIN2(scissor_max, max_x);
         max_y = MIN2(scissor_max, max_y);
      }

      tu_cs_emit(cs, A6XX_GRAS_SC_SCREEN_SCISSOR_TL_X(min_x) |
                     A6XX_GRAS_SC_SCREEN_SCISSOR_TL_Y(min_y));
      tu_cs_emit(cs, A6XX_GRAS_SC_SCREEN_SCISSOR_BR_X(max_x) |
                     A6XX_GRAS_SC_SCREEN_SCISSOR_BR_Y(max_y));
   }
}

static void
fdm_apply_scissors(struct tu_cs *cs, void *data, VkRect2D bin, unsigned views,
                   VkExtent2D *frag_areas)
{
   const struct apply_viewport_state *state =
      (const struct apply_viewport_state *)data;

   struct vk_viewport_state vp = state->vp;

   for (unsigned i = 0; i < vp.scissor_count; i++) {
      VkExtent2D frag_area = state->share_scale ? frag_areas[0] : frag_areas[i];
      VkRect2D scissor =
         state->share_scale ? state->vp.scissors[i] : state->vp.scissors[0];
      if (frag_area.width == 1 && frag_area.height == 1) {
         vp.scissors[i] = scissor;
         continue;
      }

      /* Transform the scissor following the viewport. It's unclear how this
       * is supposed to handle cases where the scissor isn't aligned to the
       * fragment area, but we round outwards to always render partial
       * fragments if the scissor size equals the framebuffer size and it
       * isn't aligned to the fragment area.
       */
      VkOffset2D offset = tu_fdm_per_bin_offset(frag_area, bin);
      VkOffset2D min = {
         scissor.offset.x / frag_area.width + offset.x,
         scissor.offset.y / frag_area.width + offset.y,
      };
      VkOffset2D max = {
         DIV_ROUND_UP(scissor.offset.x + scissor.extent.width, frag_area.width) + offset.x,
         DIV_ROUND_UP(scissor.offset.y + scissor.extent.height, frag_area.height) + offset.y,
      };

      /* Intersect scissor with the scaled bin, this essentially replaces the
       * window scissor.
       */
      uint32_t scaled_width = bin.extent.width / frag_area.width;
      uint32_t scaled_height = bin.extent.height / frag_area.height;
      vp.scissors[i].offset.x = MAX2(min.x, bin.offset.x);
      vp.scissors[i].offset.y = MAX2(min.y, bin.offset.y);
      vp.scissors[i].extent.width =
         MIN2(max.x, bin.offset.x + scaled_width) - vp.scissors[i].offset.x;
      vp.scissors[i].extent.height =
         MIN2(max.y, bin.offset.y + scaled_height) - vp.scissors[i].offset.y;
   }

   tu6_emit_scissor(cs, &vp);
}

static void
tu6_emit_scissor_fdm(struct tu_cs *cs, struct tu_cmd_buffer *cmd,
                     const struct vk_viewport_state *vp)
{
   unsigned num_views = MAX2(cmd->state.pass->num_views, 1);
   struct apply_viewport_state state = {
      .vp = *vp,
      .share_scale = !cmd->state.pipeline->base.program.per_view_viewport,
   };
   if (!state.share_scale)
      state.vp.scissor_count = num_views;
   unsigned size = tu6_scissor_size(cmd->device, &state.vp);
   tu_cs_begin_sub_stream(&cmd->sub_cs, size, cs);
   tu_create_fdm_bin_patchpoint(cmd, cs, size, fdm_apply_scissors, state);
}

static const enum mesa_vk_dynamic_graphics_state tu_sample_locations_enable_state[] = {
   MESA_VK_DYNAMIC_MS_SAMPLE_LOCATIONS_ENABLE,
};

static unsigned
tu6_sample_locations_enable_size(struct tu_device *dev, bool enable)
{
   return 6;
}

void
tu6_emit_sample_locations_enable(struct tu_cs *cs, bool enable)
{
   uint32_t sample_config =
      COND(enable, A6XX_RB_SAMPLE_CONFIG_LOCATION_ENABLE);

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SAMPLE_CONFIG, 1);
   tu_cs_emit(cs, sample_config);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_SAMPLE_CONFIG, 1);
   tu_cs_emit(cs, sample_config);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_TP_SAMPLE_CONFIG, 1);
   tu_cs_emit(cs, sample_config);
}

static const enum mesa_vk_dynamic_graphics_state tu_sample_locations_state[] = {
   MESA_VK_DYNAMIC_MS_SAMPLE_LOCATIONS,
};

static unsigned
tu6_sample_locations_size(struct tu_device *dev,
                          const struct vk_sample_locations_state *samp_loc)
{
   return 6;
}

void
tu6_emit_sample_locations(struct tu_cs *cs, const struct vk_sample_locations_state *samp_loc)
{
   /* Return if it hasn't been set yet in the dynamic case or the struct is
    * NULL in the static case (because sample locations aren't enabled)
    */
   if (!samp_loc || samp_loc->grid_size.width == 0)
      return;

   assert(samp_loc->grid_size.width == 1);
   assert(samp_loc->grid_size.height == 1);

   uint32_t sample_locations = 0;
   for (uint32_t i = 0; i < samp_loc->per_pixel; i++) {
      /* From VkSampleLocationEXT:
       *
       *    The values specified in a VkSampleLocationEXT structure are always
       *    clamped to the implementation-dependent sample location coordinate
       *    range
       *    [sampleLocationCoordinateRange[0],sampleLocationCoordinateRange[1]]
       */
      float x = CLAMP(samp_loc->locations[i].x, SAMPLE_LOCATION_MIN,
                      SAMPLE_LOCATION_MAX);
      float y = CLAMP(samp_loc->locations[i].y, SAMPLE_LOCATION_MIN,
                      SAMPLE_LOCATION_MAX);

      sample_locations |=
         (A6XX_RB_SAMPLE_LOCATION_0_SAMPLE_0_X(x) |
          A6XX_RB_SAMPLE_LOCATION_0_SAMPLE_0_Y(y)) << i*8;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SAMPLE_LOCATION_0, 1);
   tu_cs_emit(cs, sample_locations);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_SAMPLE_LOCATION_0, 1);
   tu_cs_emit(cs, sample_locations);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_TP_SAMPLE_LOCATION_0, 1);
   tu_cs_emit(cs, sample_locations);
}

static const enum mesa_vk_dynamic_graphics_state tu_depth_bias_state[] = {
   MESA_VK_DYNAMIC_RS_DEPTH_BIAS_FACTORS,
};

static unsigned
tu6_depth_bias_size(struct tu_device *dev,
                    const struct vk_rasterization_state *rs)
{
   return 4;
}

void
tu6_emit_depth_bias(struct tu_cs *cs, const struct vk_rasterization_state *rs)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SU_POLY_OFFSET_SCALE, 3);
   tu_cs_emit(cs, A6XX_GRAS_SU_POLY_OFFSET_SCALE(rs->depth_bias.slope).value);
   tu_cs_emit(cs, A6XX_GRAS_SU_POLY_OFFSET_OFFSET(rs->depth_bias.constant).value);
   tu_cs_emit(cs, A6XX_GRAS_SU_POLY_OFFSET_OFFSET_CLAMP(rs->depth_bias.clamp).value);
}

static const enum mesa_vk_dynamic_graphics_state tu_bandwidth_state[] = {
   MESA_VK_DYNAMIC_CB_LOGIC_OP_ENABLE,
   MESA_VK_DYNAMIC_CB_LOGIC_OP,
   MESA_VK_DYNAMIC_CB_ATTACHMENT_COUNT,
   MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES,
   MESA_VK_DYNAMIC_CB_BLEND_ENABLES,
   MESA_VK_DYNAMIC_CB_WRITE_MASKS,
};

static void
tu_calc_bandwidth(struct tu_bandwidth *bandwidth,
                  const struct vk_color_blend_state *cb,
                  const struct vk_render_pass_state *rp)
{
   bool rop_reads_dst = cb->logic_op_enable && tu_logic_op_reads_dst((VkLogicOp)cb->logic_op);

   uint32_t total_bpp = 0;
   for (unsigned i = 0; i < cb->attachment_count; i++) {
      const struct vk_color_blend_attachment_state *att = &cb->attachments[i];
      if (!(cb->color_write_enables & (1u << i)))
         continue;

      const VkFormat format = rp->color_attachment_formats[i];

      uint32_t write_bpp = 0;
      if (format == VK_FORMAT_UNDEFINED) {
         /* do nothing */
      } else if (att->write_mask == 0xf) {
         write_bpp = vk_format_get_blocksizebits(format);
      } else {
         const enum pipe_format pipe_format = vk_format_to_pipe_format(format);
         for (uint32_t i = 0; i < 4; i++) {
            if (att->write_mask & (1 << i)) {
               write_bpp += util_format_get_component_bits(pipe_format,
                     UTIL_FORMAT_COLORSPACE_RGB, i);
            }
         }
      }
      total_bpp += write_bpp;

      if (rop_reads_dst || att->blend_enable) {
         total_bpp += write_bpp;
      }
   }

   bandwidth->color_bandwidth_per_sample = total_bpp / 8;

   if (rp->attachment_aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
      bandwidth->depth_cpp_per_sample = util_format_get_component_bits(
            vk_format_to_pipe_format(rp->depth_attachment_format),
            UTIL_FORMAT_COLORSPACE_ZS, 0) / 8;
   }

   if (rp->attachment_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
      bandwidth->stencil_cpp_per_sample = util_format_get_component_bits(
            vk_format_to_pipe_format(rp->stencil_attachment_format),
            UTIL_FORMAT_COLORSPACE_ZS, 1) / 8;
   }
}

/* Return true if the blend state reads the color attachments. */
static bool
tu6_calc_blend_lrz(const struct vk_color_blend_state *cb,
                   const struct vk_render_pass_state *rp)
{
   if (cb->logic_op_enable && tu_logic_op_reads_dst((VkLogicOp)cb->logic_op))
      return true;

   for (unsigned i = 0; i < cb->attachment_count; i++) {
      if (rp->color_attachment_formats[i] == VK_FORMAT_UNDEFINED)
         continue;

      const struct vk_color_blend_attachment_state *att = &cb->attachments[i];
      if (att->blend_enable)
         return true;
      if (!(cb->color_write_enables & (1u << i)))
         return true;
      unsigned mask =
         MASK(vk_format_get_nr_components(rp->color_attachment_formats[i]));
      if ((att->write_mask & mask) != mask)
         return true;
   }

   return false;
}

static const enum mesa_vk_dynamic_graphics_state tu_blend_lrz_state[] = {
   MESA_VK_DYNAMIC_CB_LOGIC_OP_ENABLE,
   MESA_VK_DYNAMIC_CB_LOGIC_OP,
   MESA_VK_DYNAMIC_CB_ATTACHMENT_COUNT,
   MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES,
   MESA_VK_DYNAMIC_CB_BLEND_ENABLES,
   MESA_VK_DYNAMIC_CB_WRITE_MASKS,
};

static void
tu_emit_blend_lrz(struct tu_lrz_pipeline *lrz,
                  const struct vk_color_blend_state *cb,
                  const struct vk_render_pass_state *rp)
{
   if (tu6_calc_blend_lrz(cb, rp))
      lrz->lrz_status |= TU_LRZ_FORCE_DISABLE_WRITE | TU_LRZ_READS_DEST;
   lrz->blend_valid = true;
}

static const enum mesa_vk_dynamic_graphics_state tu_blend_state[] = {
   MESA_VK_DYNAMIC_CB_LOGIC_OP_ENABLE,
   MESA_VK_DYNAMIC_CB_LOGIC_OP,
   MESA_VK_DYNAMIC_CB_ATTACHMENT_COUNT,
   MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES,
   MESA_VK_DYNAMIC_CB_BLEND_ENABLES,
   MESA_VK_DYNAMIC_CB_BLEND_EQUATIONS,
   MESA_VK_DYNAMIC_CB_WRITE_MASKS,
   MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE,
   MESA_VK_DYNAMIC_MS_ALPHA_TO_ONE_ENABLE,
   MESA_VK_DYNAMIC_MS_SAMPLE_MASK,
};

static unsigned
tu6_blend_size(struct tu_device *dev,
               const struct vk_color_blend_state *cb,
               bool alpha_to_coverage_enable,
               bool alpha_to_one_enable,
               uint32_t sample_mask)
{
   unsigned num_rts = alpha_to_coverage_enable ?
      MAX2(cb->attachment_count, 1) : cb->attachment_count;
   return 8 + 3 * num_rts;
}

static void
tu6_emit_blend(struct tu_cs *cs,
               const struct vk_color_blend_state *cb,
               bool alpha_to_coverage_enable,
               bool alpha_to_one_enable,
               uint32_t sample_mask)
{
   bool rop_reads_dst = cb->logic_op_enable && tu_logic_op_reads_dst((VkLogicOp)cb->logic_op);
   enum a3xx_rop_code rop = tu6_rop((VkLogicOp)cb->logic_op);

   uint32_t blend_enable_mask = 0;
   for (unsigned i = 0; i < cb->attachment_count; i++) {
      const struct vk_color_blend_attachment_state *att = &cb->attachments[i];
      if (!(cb->color_write_enables & (1u << i)))
         continue;

      if (rop_reads_dst || att->blend_enable) {
         blend_enable_mask |= 1u << i;
      }
   }

   /* This will emit a dummy RB_MRT_*_CONTROL below if alpha-to-coverage is
    * enabled but there are no color attachments, in addition to changing
    * *_FS_OUTPUT_CNTL1.
    */
   unsigned num_rts = alpha_to_coverage_enable ?
      MAX2(cb->attachment_count, 1) : cb->attachment_count;

   bool dual_src_blend = tu_blend_state_is_dual_src(cb);

   tu_cs_emit_regs(cs, A6XX_SP_FS_OUTPUT_CNTL1(.mrt = num_rts));
   tu_cs_emit_regs(cs, A6XX_RB_FS_OUTPUT_CNTL1(.mrt = num_rts));
   tu_cs_emit_regs(cs, A6XX_SP_BLEND_CNTL(.enable_blend = blend_enable_mask,
                                          .unk8 = true,
                                          .dual_color_in_enable =
                                             dual_src_blend,
                                          .alpha_to_coverage =
                                             alpha_to_coverage_enable));
   /* set A6XX_RB_BLEND_CNTL_INDEPENDENT_BLEND only when enabled? */
   tu_cs_emit_regs(cs, A6XX_RB_BLEND_CNTL(.enable_blend = blend_enable_mask,
                                          .independent_blend = true,
                                          .dual_color_in_enable =
                                             dual_src_blend,
                                          .alpha_to_coverage =
                                             alpha_to_coverage_enable,
                                          .alpha_to_one = alpha_to_one_enable,
                                          .sample_mask = sample_mask));

   for (unsigned i = 0; i < num_rts; i++) {
      const struct vk_color_blend_attachment_state *att = &cb->attachments[i];
      if ((cb->color_write_enables & (1u << i)) && i < cb->attachment_count) {
         const enum a3xx_rb_blend_opcode color_op = tu6_blend_op(att->color_blend_op);
         const enum adreno_rb_blend_factor src_color_factor =
            tu6_blend_factor((VkBlendFactor)att->src_color_blend_factor);
         const enum adreno_rb_blend_factor dst_color_factor =
            tu6_blend_factor((VkBlendFactor)att->dst_color_blend_factor);
         const enum a3xx_rb_blend_opcode alpha_op =
            tu6_blend_op(att->alpha_blend_op);
         const enum adreno_rb_blend_factor src_alpha_factor =
            tu6_blend_factor((VkBlendFactor)att->src_alpha_blend_factor);
         const enum adreno_rb_blend_factor dst_alpha_factor =
            tu6_blend_factor((VkBlendFactor)att->dst_alpha_blend_factor);

         tu_cs_emit_regs(cs,
                         A6XX_RB_MRT_CONTROL(i,
                                             .blend = att->blend_enable,
                                             .blend2 = att->blend_enable,
                                             .rop_enable = cb->logic_op_enable,
                                             .rop_code = rop,
                                             .component_enable = att->write_mask),
                         A6XX_RB_MRT_BLEND_CONTROL(i,
                                                   .rgb_src_factor = src_color_factor,
                                                   .rgb_blend_opcode = color_op,
                                                   .rgb_dest_factor = dst_color_factor,
                                                   .alpha_src_factor = src_alpha_factor,
                                                   .alpha_blend_opcode = alpha_op,
                                                   .alpha_dest_factor = dst_alpha_factor));
      } else {
            tu_cs_emit_regs(cs,
                            A6XX_RB_MRT_CONTROL(i,),
                            A6XX_RB_MRT_BLEND_CONTROL(i,));
      }
   }
}

static const enum mesa_vk_dynamic_graphics_state tu_blend_constants_state[] = {
   MESA_VK_DYNAMIC_CB_BLEND_CONSTANTS,
};

static unsigned
tu6_blend_constants_size(struct tu_device *dev,
                         const struct vk_color_blend_state *cb)
{
   return 5;
}

static void
tu6_emit_blend_constants(struct tu_cs *cs, const struct vk_color_blend_state *cb)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLEND_RED_F32, 4);
   tu_cs_emit_array(cs, (const uint32_t *) cb->blend_constants, 4);
}

static const enum mesa_vk_dynamic_graphics_state tu_rast_state[] = {
   MESA_VK_DYNAMIC_RS_DEPTH_CLAMP_ENABLE,
   MESA_VK_DYNAMIC_RS_DEPTH_CLIP_ENABLE,
   MESA_VK_DYNAMIC_RS_POLYGON_MODE,
   MESA_VK_DYNAMIC_RS_CULL_MODE,
   MESA_VK_DYNAMIC_RS_FRONT_FACE,
   MESA_VK_DYNAMIC_RS_DEPTH_BIAS_ENABLE,
   MESA_VK_DYNAMIC_RS_LINE_MODE,
   MESA_VK_DYNAMIC_VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE,
};

uint32_t
tu6_rast_size(struct tu_device *dev,
              const struct vk_rasterization_state *rs,
              const struct vk_viewport_state *vp,
              bool multiview,
              bool per_view_viewport)
{
   return 11 + (dev->physical_device->info->a6xx.has_shading_rate ? 8 : 0);
}

void
tu6_emit_rast(struct tu_cs *cs,
              const struct vk_rasterization_state *rs,
              const struct vk_viewport_state *vp,
              bool multiview,
              bool per_view_viewport)
{
   enum a5xx_line_mode line_mode =
      rs->line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT ?
      BRESENHAM : RECTANGULAR;
   tu_cs_emit_regs(cs,
                   A6XX_GRAS_SU_CNTL(
                     .cull_front = rs->cull_mode & VK_CULL_MODE_FRONT_BIT,
                     .cull_back = rs->cull_mode & VK_CULL_MODE_BACK_BIT,
                     .front_cw = rs->front_face == VK_FRONT_FACE_CLOCKWISE,
                     .linehalfwidth = rs->line.width / 2.0f,
                     .poly_offset = rs->depth_bias.enable,
                     .line_mode = line_mode,
                     .multiview_enable = multiview,
                     .rendertargetindexincr = multiview,
                     .viewportindexincr = multiview && per_view_viewport));

   bool depth_clip_enable = vk_rasterization_state_depth_clip_enable(rs);

   tu_cs_emit_regs(cs, 
                   A6XX_GRAS_CL_CNTL(
                     .znear_clip_disable = !depth_clip_enable,
                     .zfar_clip_disable = !depth_clip_enable,
                     .z_clamp_enable = rs->depth_clamp_enable,
                     .zero_gb_scale_z = vp->depth_clip_negative_one_to_one ? 0 : 1,
                     .vp_clip_code_ignore = 1));;

   enum a6xx_polygon_mode polygon_mode = tu6_polygon_mode(rs->polygon_mode);

   tu_cs_emit_regs(cs,
                   A6XX_VPC_POLYGON_MODE(polygon_mode));

   tu_cs_emit_regs(cs,
                   A6XX_PC_POLYGON_MODE(polygon_mode));

   /* move to hw ctx init? */
   tu_cs_emit_regs(cs,
                   A6XX_GRAS_SU_POINT_MINMAX(.min = 1.0f / 16.0f, .max = 4092.0f),
                   A6XX_GRAS_SU_POINT_SIZE(1.0f));

   if (cs->device->physical_device->info->a6xx.has_shading_rate) {
      tu_cs_emit_regs(cs, A6XX_RB_UNKNOWN_8A00());
      tu_cs_emit_regs(cs, A6XX_RB_UNKNOWN_8A10());
      tu_cs_emit_regs(cs, A6XX_RB_UNKNOWN_8A20());
      tu_cs_emit_regs(cs, A6XX_RB_UNKNOWN_8A30());
   }
}

static const enum mesa_vk_dynamic_graphics_state tu_pc_raster_cntl_state[] = {
   MESA_VK_DYNAMIC_RS_RASTERIZER_DISCARD_ENABLE,
   MESA_VK_DYNAMIC_RS_RASTERIZATION_STREAM,
};

static unsigned
tu6_pc_raster_cntl_size(struct tu_device *dev,
                        const struct vk_rasterization_state *rs)
{
   return 4;
}

static void
tu6_emit_pc_raster_cntl(struct tu_cs *cs,
                        const struct vk_rasterization_state *rs)
{
   tu_cs_emit_regs(cs, A6XX_PC_RASTER_CNTL(
      .stream = rs->rasterization_stream,
      .discard = rs->rasterizer_discard_enable));
   tu_cs_emit_regs(cs, A6XX_VPC_UNKNOWN_9107(
      .raster_discard = rs->rasterizer_discard_enable));
}

static const enum mesa_vk_dynamic_graphics_state tu_ds_state[] = {
   MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE,
   MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE,
   MESA_VK_DYNAMIC_DS_DEPTH_COMPARE_OP,
   MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE,
   MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE,
   MESA_VK_DYNAMIC_DS_STENCIL_OP,
   MESA_VK_DYNAMIC_RS_DEPTH_CLAMP_ENABLE,
};

static unsigned
tu6_ds_size(struct tu_device *dev,
            const struct vk_depth_stencil_state *ds,
            const struct vk_render_pass_state *rp,
            const struct vk_rasterization_state *rs)
{
   return 4;
}

static void
tu6_emit_ds(struct tu_cs *cs,
            const struct vk_depth_stencil_state *ds,
            const struct vk_render_pass_state *rp,
            const struct vk_rasterization_state *rs)
{
   tu_cs_emit_regs(cs, A6XX_RB_STENCIL_CONTROL(
      .stencil_enable = ds->stencil.test_enable,
      .stencil_enable_bf = ds->stencil.test_enable,
      .stencil_read = ds->stencil.test_enable,
      .func = tu6_compare_func((VkCompareOp)ds->stencil.front.op.compare),
      .fail = tu6_stencil_op((VkStencilOp)ds->stencil.front.op.fail),
      .zpass = tu6_stencil_op((VkStencilOp)ds->stencil.front.op.pass),
      .zfail = tu6_stencil_op((VkStencilOp)ds->stencil.front.op.depth_fail),
      .func_bf = tu6_compare_func((VkCompareOp)ds->stencil.back.op.compare),
      .fail_bf = tu6_stencil_op((VkStencilOp)ds->stencil.back.op.fail),
      .zpass_bf = tu6_stencil_op((VkStencilOp)ds->stencil.back.op.pass),
      .zfail_bf = tu6_stencil_op((VkStencilOp)ds->stencil.back.op.depth_fail)));

   if (rp->attachment_aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
      bool depth_test = ds->depth.test_enable;
      enum adreno_compare_func zfunc = tu6_compare_func(ds->depth.compare_op);

      /* On some GPUs it is necessary to enable z test for depth bounds test
       * when UBWC is enabled. Otherwise, the GPU would hang. FUNC_ALWAYS is
       * required to pass z test. Relevant tests:
       *  dEQP-VK.pipeline.extended_dynamic_state.two_draws_dynamic.depth_bounds_test_disable
       *  dEQP-VK.dynamic_state.ds_state.depth_bounds_1
       */
      if (ds->depth.bounds_test.enable &&
          !ds->depth.test_enable &&
          cs->device->physical_device->info->a6xx.depth_bounds_require_depth_test_quirk) {
         depth_test = true;
         zfunc = FUNC_ALWAYS;
      }

      tu_cs_emit_regs(cs, A6XX_RB_DEPTH_CNTL(
         .z_test_enable = depth_test,
         .z_write_enable = ds->depth.test_enable && ds->depth.write_enable,
         .zfunc = zfunc,
         .z_clamp_enable = rs->depth_clamp_enable,
         /* TODO don't set for ALWAYS/NEVER */
         .z_read_enable = ds->depth.test_enable || ds->depth.bounds_test.enable,
         .z_bounds_enable = ds->depth.bounds_test.enable));
   } else {
      tu_cs_emit_regs(cs, A6XX_RB_DEPTH_CNTL());
   }
}

static const enum mesa_vk_dynamic_graphics_state tu_depth_bounds_state[] = {
   MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_BOUNDS,
};

static unsigned
tu6_depth_bounds_size(struct tu_device *dev,
                      const struct vk_depth_stencil_state *ds)
{
   return 3;
}

static void
tu6_emit_depth_bounds(struct tu_cs *cs,
                      const struct vk_depth_stencil_state *ds)
{
   tu_cs_emit_regs(cs,
                   A6XX_RB_Z_BOUNDS_MIN(ds->depth.bounds_test.min),
                   A6XX_RB_Z_BOUNDS_MAX(ds->depth.bounds_test.max));
}

static const enum mesa_vk_dynamic_graphics_state tu_stencil_compare_mask_state[] = {
   MESA_VK_DYNAMIC_DS_STENCIL_COMPARE_MASK,
};

static unsigned
tu6_stencil_compare_mask_size(struct tu_device *dev,
                              const struct vk_depth_stencil_state *ds)
{
   return 2;
}

static void
tu6_emit_stencil_compare_mask(struct tu_cs *cs,
                              const struct vk_depth_stencil_state *ds)
{
   tu_cs_emit_regs(cs, A6XX_RB_STENCILMASK(
      .mask = ds->stencil.front.compare_mask,
      .bfmask = ds->stencil.back.compare_mask));
}

static const enum mesa_vk_dynamic_graphics_state tu_stencil_write_mask_state[] = {
   MESA_VK_DYNAMIC_DS_STENCIL_WRITE_MASK,
};

static unsigned
tu6_stencil_write_mask_size(struct tu_device *dev,
                            const struct vk_depth_stencil_state *ds)
{
   return 2;
}

static void
tu6_emit_stencil_write_mask(struct tu_cs *cs,
                            const struct vk_depth_stencil_state *ds)
{
   tu_cs_emit_regs(cs, A6XX_RB_STENCILWRMASK(
      .wrmask = ds->stencil.front.write_mask,
      .bfwrmask = ds->stencil.back.write_mask));
}

static const enum mesa_vk_dynamic_graphics_state tu_stencil_reference_state[] = {
   MESA_VK_DYNAMIC_DS_STENCIL_REFERENCE,
};

static unsigned
tu6_stencil_reference_size(struct tu_device *dev,
                           const struct vk_depth_stencil_state *ds)
{
   return 2;
}

static void
tu6_emit_stencil_reference(struct tu_cs *cs,
                           const struct vk_depth_stencil_state *ds)
{
   tu_cs_emit_regs(cs, A6XX_RB_STENCILREF(
      .ref = ds->stencil.front.reference,
      .bfref = ds->stencil.back.reference));
}

static inline bool
emit_pipeline_state(BITSET_WORD *keep, BITSET_WORD *remove,
                    BITSET_WORD *pipeline_set,
                    const enum mesa_vk_dynamic_graphics_state *state_array,
                    unsigned num_states, bool extra_cond,
                    struct tu_pipeline_builder *builder)
{
   BITSET_DECLARE(state, MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX) = {};

   /* Unrolling this loop should produce a constant value once the function is
    * inlined, because state_array and num_states are a per-draw-state
    * constant, but GCC seems to need a little encouragement. clang does a
    * little better but still needs a pragma when there are a large number of
    * states.
    */
#if defined(__clang__)
#pragma clang loop unroll(full)
#elif defined(__GNUC__) && __GNUC__ >= 8
#pragma GCC unroll MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX
#endif
   for (unsigned i = 0; i < num_states; i++) {
      BITSET_SET(state, state_array[i]);
   }

   /* If all of the state is set, then after we emit it we can tentatively
    * remove it from the states to set for the pipeline by making it dynamic.
    * If we can't emit it, though, we need to keep around the partial state so
    * that we can emit it later, even if another draw state consumes it. That
    * is, we have to cancel any tentative removal.
    */
   BITSET_DECLARE(temp, MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX);
   memcpy(temp, pipeline_set, sizeof(temp));
   BITSET_AND(temp, temp, state);
   if (!BITSET_EQUAL(temp, state) || !extra_cond) {
      __bitset_or(keep, keep, temp, ARRAY_SIZE(temp));
      return false;
   }
   __bitset_or(remove, remove, state, ARRAY_SIZE(state));
   return true;
}

static void
tu_pipeline_builder_emit_state(struct tu_pipeline_builder *builder,
                               struct tu_pipeline *pipeline)
{
   struct tu_cs cs;
   BITSET_DECLARE(keep, MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX) = {};
   BITSET_DECLARE(remove, MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX) = {};
   BITSET_DECLARE(pipeline_set, MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX) = {};

   vk_graphics_pipeline_get_state(&builder->graphics_state, pipeline_set);

#define EMIT_STATE(name, extra_cond)                                          \
   emit_pipeline_state(keep, remove, pipeline_set, tu_##name##_state,         \
                       ARRAY_SIZE(tu_##name##_state), extra_cond, builder)

#define DRAW_STATE_COND(name, id, extra_cond, ...)                            \
   if (EMIT_STATE(name, extra_cond)) {                                        \
      unsigned size = tu6_##name##_size(builder->device, __VA_ARGS__);        \
      if (size > 0) {                                                         \
         tu_cs_begin_sub_stream(&pipeline->cs, size, &cs);                    \
         tu6_emit_##name(&cs, __VA_ARGS__);                                   \
         pipeline->dynamic_state[id] =                                        \
            tu_cs_end_draw_state(&pipeline->cs, &cs);                         \
      }                                                                       \
      pipeline->set_state_mask |= (1u << id);                                 \
   }
#define DRAW_STATE(name, id, ...) DRAW_STATE_COND(name, id, true, __VA_ARGS__)

   DRAW_STATE(vertex_input, TU_DYNAMIC_STATE_VERTEX_INPUT,
              builder->graphics_state.vi);
   DRAW_STATE(vertex_stride, TU_DYNAMIC_STATE_VB_STRIDE,
              builder->graphics_state.vi);
   /* If (a) per-view viewport is used or (b) we don't know yet, then we need
    * to set viewport and stencil state dynamically.
    */
   bool no_per_view_viewport = pipeline_contains_all_shader_state(pipeline) &&
      !pipeline->program.per_view_viewport;
   DRAW_STATE_COND(viewport, VK_DYNAMIC_STATE_VIEWPORT, no_per_view_viewport,
                   builder->graphics_state.vp);
   DRAW_STATE_COND(scissor, VK_DYNAMIC_STATE_SCISSOR, no_per_view_viewport,
              builder->graphics_state.vp);
   DRAW_STATE(sample_locations_enable,
              TU_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE,
              builder->graphics_state.ms->sample_locations_enable);
   DRAW_STATE(sample_locations,
              TU_DYNAMIC_STATE_SAMPLE_LOCATIONS,
              builder->graphics_state.ms->sample_locations);
   DRAW_STATE(depth_bias, VK_DYNAMIC_STATE_DEPTH_BIAS,
              builder->graphics_state.rs);
   bool attachments_valid =
      builder->graphics_state.rp &&
      !(builder->graphics_state.rp->attachment_aspects &
                              VK_IMAGE_ASPECT_METADATA_BIT);
   struct vk_color_blend_state dummy_cb = {};
   const struct vk_color_blend_state *cb = builder->graphics_state.cb;
   if (attachments_valid &&
       !(builder->graphics_state.rp->attachment_aspects &
         VK_IMAGE_ASPECT_COLOR_BIT)) {
      /* If there are no color attachments, then the original blend state may
       * be NULL and the common code sanitizes it to always be NULL. In this
       * case we want to emit an empty blend/bandwidth/etc.  rather than
       * letting it be dynamic (and potentially garbage).
       */
      cb = &dummy_cb;
      BITSET_SET(pipeline_set, MESA_VK_DYNAMIC_CB_LOGIC_OP_ENABLE);
      BITSET_SET(pipeline_set, MESA_VK_DYNAMIC_CB_LOGIC_OP);
      BITSET_SET(pipeline_set, MESA_VK_DYNAMIC_CB_ATTACHMENT_COUNT);
      BITSET_SET(pipeline_set, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES);
      BITSET_SET(pipeline_set, MESA_VK_DYNAMIC_CB_BLEND_ENABLES);
      BITSET_SET(pipeline_set, MESA_VK_DYNAMIC_CB_BLEND_EQUATIONS);
      BITSET_SET(pipeline_set, MESA_VK_DYNAMIC_CB_WRITE_MASKS);
      BITSET_SET(pipeline_set, MESA_VK_DYNAMIC_CB_BLEND_CONSTANTS);
   }
   DRAW_STATE(blend, TU_DYNAMIC_STATE_BLEND, cb,
              builder->graphics_state.ms->alpha_to_coverage_enable,
              builder->graphics_state.ms->alpha_to_one_enable,
              builder->graphics_state.ms->sample_mask);
   if (EMIT_STATE(blend_lrz, attachments_valid))
      tu_emit_blend_lrz(&pipeline->lrz, cb,
                        builder->graphics_state.rp);
   if (EMIT_STATE(bandwidth, attachments_valid))
      tu_calc_bandwidth(&pipeline->bandwidth, cb,
                        builder->graphics_state.rp);
   DRAW_STATE(blend_constants, VK_DYNAMIC_STATE_BLEND_CONSTANTS, cb);
   if (attachments_valid &&
       !(builder->graphics_state.rp->attachment_aspects &
         VK_IMAGE_ASPECT_COLOR_BIT)) {
      /* Don't actually make anything dynamic as that may mean a partially-set
       * state group where the group is NULL which angers common code.
       */
      BITSET_CLEAR(remove, MESA_VK_DYNAMIC_CB_LOGIC_OP_ENABLE);
      BITSET_CLEAR(remove, MESA_VK_DYNAMIC_CB_LOGIC_OP);
      BITSET_CLEAR(remove, MESA_VK_DYNAMIC_CB_ATTACHMENT_COUNT);
      BITSET_CLEAR(remove, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES);
      BITSET_CLEAR(remove, MESA_VK_DYNAMIC_CB_BLEND_ENABLES);
      BITSET_CLEAR(remove, MESA_VK_DYNAMIC_CB_BLEND_EQUATIONS);
      BITSET_CLEAR(remove, MESA_VK_DYNAMIC_CB_WRITE_MASKS);
      BITSET_CLEAR(remove, MESA_VK_DYNAMIC_CB_BLEND_CONSTANTS);
   }
   DRAW_STATE_COND(rast, TU_DYNAMIC_STATE_RAST,
                   pipeline_contains_all_shader_state(pipeline),
                   builder->graphics_state.rs,
                   builder->graphics_state.vp,
                   builder->graphics_state.rp->view_mask != 0,
                   pipeline->program.per_view_viewport);
   DRAW_STATE(pc_raster_cntl, TU_DYNAMIC_STATE_PC_RASTER_CNTL,
              builder->graphics_state.rs);
   DRAW_STATE_COND(ds, TU_DYNAMIC_STATE_DS,
                   attachments_valid,
                   builder->graphics_state.ds,
                   builder->graphics_state.rp,
                   builder->graphics_state.rs);
   DRAW_STATE(depth_bounds, VK_DYNAMIC_STATE_DEPTH_BOUNDS,
              builder->graphics_state.ds);
   DRAW_STATE(depth_bounds, VK_DYNAMIC_STATE_DEPTH_BOUNDS,
              builder->graphics_state.ds);
   DRAW_STATE(stencil_compare_mask, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
              builder->graphics_state.ds);
   DRAW_STATE(stencil_write_mask, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
              builder->graphics_state.ds);
   DRAW_STATE(stencil_reference, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
              builder->graphics_state.ds);
   DRAW_STATE_COND(patch_control_points,
                   TU_DYNAMIC_STATE_PATCH_CONTROL_POINTS,
                   pipeline_contains_all_shader_state(pipeline),
                   pipeline,
                   builder->graphics_state.ts->patch_control_points);
#undef DRAW_STATE
#undef DRAW_STATE_COND
#undef EMIT_STATE

   /* LRZ always needs depth/stencil state at draw time */
   BITSET_SET(keep, MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE);
   BITSET_SET(keep, MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE);
   BITSET_SET(keep, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE);
   BITSET_SET(keep, MESA_VK_DYNAMIC_DS_DEPTH_COMPARE_OP);
   BITSET_SET(keep, MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE);
   BITSET_SET(keep, MESA_VK_DYNAMIC_DS_STENCIL_OP);
   BITSET_SET(keep, MESA_VK_DYNAMIC_DS_STENCIL_WRITE_MASK);
   BITSET_SET(keep, MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE);

   /* MSAA needs line mode */
   BITSET_SET(keep, MESA_VK_DYNAMIC_RS_LINE_MODE);

   /* The patch control points is part of the draw */
   BITSET_SET(keep, MESA_VK_DYNAMIC_TS_PATCH_CONTROL_POINTS);

   /* Vertex buffer state needs to know the max valid binding */
   BITSET_SET(keep, MESA_VK_DYNAMIC_VI_BINDINGS_VALID);

   /* Remove state which has been emitted and we no longer need to set when
    * binding the pipeline by making it "dynamic".
    */
   BITSET_ANDNOT(remove, remove, keep);
   BITSET_OR(builder->graphics_state.dynamic, builder->graphics_state.dynamic,
             remove);
}

static inline bool
emit_draw_state(const struct vk_dynamic_graphics_state *dynamic_state,
                const enum mesa_vk_dynamic_graphics_state *state_array,
                unsigned num_states)
{
   BITSET_DECLARE(state, MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX) = {};

   /* Unrolling this loop should produce a constant value once the function is
    * inlined, because state_array and num_states are a per-draw-state
    * constant, but GCC seems to need a little encouragement. clang does a
    * little better but still needs a pragma when there are a large number of
    * states.
    */
#if defined(__clang__)
#pragma clang loop unroll(full)
#elif defined(__GNUC__) && __GNUC__ >= 8
#pragma GCC unroll MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX
#endif
   for (unsigned i = 0; i < num_states; i++) {
      BITSET_SET(state, state_array[i]);
   }

   BITSET_DECLARE(temp, MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX);
   BITSET_AND(temp, state, dynamic_state->dirty);
   return !BITSET_IS_EMPTY(temp);
}

uint32_t
tu_emit_draw_state(struct tu_cmd_buffer *cmd)
{
   struct tu_cs cs;
   uint32_t dirty_draw_states = 0; 

#define EMIT_STATE(name)                                                      \
   emit_draw_state(&cmd->vk.dynamic_graphics_state, tu_##name##_state,        \
                   ARRAY_SIZE(tu_##name##_state))
#define DRAW_STATE_COND(name, id, extra_cond, ...)                            \
   if ((EMIT_STATE(name) || extra_cond) &&                                    \
       !(cmd->state.pipeline->base.set_state_mask & (1u << id))) {            \
      unsigned size = tu6_##name##_size(cmd->device, __VA_ARGS__);            \
      if (size > 0) {                                                         \
         tu_cs_begin_sub_stream(&cmd->sub_cs, size, &cs);                     \
         tu6_emit_##name(&cs, __VA_ARGS__);                                   \
         cmd->state.dynamic_state[id] =                                       \
            tu_cs_end_draw_state(&cmd->sub_cs, &cs);                          \
      } else {                                                                \
         cmd->state.dynamic_state[id] = {};                                   \
      }                                                                       \
      dirty_draw_states |= (1u << id);                                        \
   }
#define DRAW_STATE_FDM(name, id, ...)                                         \
   if ((EMIT_STATE(name) || (cmd->state.dirty & TU_CMD_DIRTY_FDM)) &&         \
       !(cmd->state.pipeline->base.set_state_mask & (1u << id))) {            \
      if (cmd->state.pipeline_has_fdm) {                                      \
         tu_cs_set_writeable(&cmd->sub_cs, true);                             \
         tu6_emit_##name##_fdm(&cs, cmd, __VA_ARGS__);                        \
         cmd->state.dynamic_state[id] =                                       \
            tu_cs_end_draw_state(&cmd->sub_cs, &cs);                          \
         tu_cs_set_writeable(&cmd->sub_cs, false);                            \
      } else {                                                                \
         unsigned size = tu6_##name##_size(cmd->device, __VA_ARGS__);         \
         if (size > 0) {                                                      \
            tu_cs_begin_sub_stream(&cmd->sub_cs, size, &cs);                  \
            tu6_emit_##name(&cs, __VA_ARGS__);                                \
            cmd->state.dynamic_state[id] =                                    \
               tu_cs_end_draw_state(&cmd->sub_cs, &cs);                       \
         } else {                                                             \
            cmd->state.dynamic_state[id] = {};                                \
         }                                                                    \
         tu_cs_begin_sub_stream(&cmd->sub_cs,                                 \
                                tu6_##name##_size(cmd->device, __VA_ARGS__),  \
                                &cs);                                         \
         tu6_emit_##name(&cs, __VA_ARGS__);                                   \
         cmd->state.dynamic_state[id] =                                       \
            tu_cs_end_draw_state(&cmd->sub_cs, &cs);                          \
      }                                                                       \
      dirty_draw_states |= (1u << id);                                        \
   }
#define DRAW_STATE(name, id, ...) DRAW_STATE_COND(name, id, false, __VA_ARGS__)

   DRAW_STATE(vertex_input, TU_DYNAMIC_STATE_VERTEX_INPUT,
              cmd->vk.dynamic_graphics_state.vi);

   /* Vertex input stride is special because it's part of the vertex input in
    * the pipeline but a separate array when it's dynamic state so we have to
    * use two separate functions.
    */
#define tu6_emit_vertex_stride tu6_emit_vertex_stride_dyn
#define tu6_vertex_stride_size tu6_vertex_stride_size_dyn

   DRAW_STATE(vertex_stride, TU_DYNAMIC_STATE_VB_STRIDE,
              cmd->vk.dynamic_graphics_state.vi_binding_strides,
              cmd->vk.dynamic_graphics_state.vi_bindings_valid);

#undef tu6_emit_vertex_stride
#undef tu6_vertex_stride_size

   DRAW_STATE_FDM(viewport, VK_DYNAMIC_STATE_VIEWPORT,
                  &cmd->vk.dynamic_graphics_state.vp);
   DRAW_STATE_FDM(scissor, VK_DYNAMIC_STATE_SCISSOR,
                  &cmd->vk.dynamic_graphics_state.vp);
   DRAW_STATE(sample_locations_enable,
              TU_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE,
              cmd->vk.dynamic_graphics_state.ms.sample_locations_enable);
   DRAW_STATE(sample_locations,
              TU_DYNAMIC_STATE_SAMPLE_LOCATIONS,
              cmd->vk.dynamic_graphics_state.ms.sample_locations);
   DRAW_STATE(depth_bias, VK_DYNAMIC_STATE_DEPTH_BIAS,
              &cmd->vk.dynamic_graphics_state.rs);
   DRAW_STATE(blend, TU_DYNAMIC_STATE_BLEND,
              &cmd->vk.dynamic_graphics_state.cb,
              cmd->vk.dynamic_graphics_state.ms.alpha_to_coverage_enable,
              cmd->vk.dynamic_graphics_state.ms.alpha_to_one_enable,
              cmd->vk.dynamic_graphics_state.ms.sample_mask);
   if (EMIT_STATE(blend_lrz) ||
       ((cmd->state.dirty & TU_CMD_DIRTY_SUBPASS) &&
        !cmd->state.pipeline->base.lrz.blend_valid)) {
      bool blend_reads_dest = tu6_calc_blend_lrz(&cmd->vk.dynamic_graphics_state.cb,
                                                 &cmd->state.vk_rp);
      if (blend_reads_dest != cmd->state.blend_reads_dest) {
         cmd->state.blend_reads_dest = blend_reads_dest;
         cmd->state.dirty |= TU_CMD_DIRTY_LRZ;
      }
   }
   if (EMIT_STATE(bandwidth) ||
       ((cmd->state.dirty & TU_CMD_DIRTY_SUBPASS) &&
        !cmd->state.pipeline->base.bandwidth.valid))
      tu_calc_bandwidth(&cmd->state.bandwidth, &cmd->vk.dynamic_graphics_state.cb,
                        &cmd->state.vk_rp);
   DRAW_STATE(blend_constants, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
              &cmd->vk.dynamic_graphics_state.cb);
   DRAW_STATE_COND(rast, TU_DYNAMIC_STATE_RAST,
                   cmd->state.dirty & (TU_CMD_DIRTY_SUBPASS |
                                       TU_CMD_DIRTY_PER_VIEW_VIEWPORT),
                   &cmd->vk.dynamic_graphics_state.rs,
                   &cmd->vk.dynamic_graphics_state.vp,
                   cmd->state.vk_rp.view_mask != 0,
                   cmd->state.per_view_viewport);
   DRAW_STATE(pc_raster_cntl, TU_DYNAMIC_STATE_PC_RASTER_CNTL,
              &cmd->vk.dynamic_graphics_state.rs);
   DRAW_STATE_COND(ds, TU_DYNAMIC_STATE_DS,
                   cmd->state.dirty & TU_CMD_DIRTY_SUBPASS,
                   &cmd->vk.dynamic_graphics_state.ds,
                   &cmd->state.vk_rp,
                   &cmd->vk.dynamic_graphics_state.rs);
   DRAW_STATE(depth_bounds, VK_DYNAMIC_STATE_DEPTH_BOUNDS,
              &cmd->vk.dynamic_graphics_state.ds);
   DRAW_STATE(stencil_compare_mask, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
              &cmd->vk.dynamic_graphics_state.ds);
   DRAW_STATE(stencil_write_mask, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
              &cmd->vk.dynamic_graphics_state.ds);
   DRAW_STATE(stencil_reference, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
              &cmd->vk.dynamic_graphics_state.ds);
   DRAW_STATE_COND(patch_control_points,
                   TU_DYNAMIC_STATE_PATCH_CONTROL_POINTS,
                   cmd->state.dirty & TU_CMD_DIRTY_PIPELINE,
                   &cmd->state.pipeline->base,
                   cmd->vk.dynamic_graphics_state.ts.patch_control_points);
#undef DRAW_STATE
#undef DRAW_STATE_COND
#undef EMIT_STATE

   return dirty_draw_states;
}

static void
tu_pipeline_builder_parse_depth_stencil(
   struct tu_pipeline_builder *builder, struct tu_pipeline *pipeline)
{
   const VkPipelineDepthStencilStateCreateInfo *ds_info =
      builder->create_info->pDepthStencilState;

   if ((builder->graphics_state.rp->attachment_aspects &
        VK_IMAGE_ASPECT_METADATA_BIT) ||
       (builder->graphics_state.rp->attachment_aspects &
        VK_IMAGE_ASPECT_DEPTH_BIT)) {
      pipeline->ds.raster_order_attachment_access =
         ds_info->flags &
         (VK_PIPELINE_DEPTH_STENCIL_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_DEPTH_ACCESS_BIT_ARM |
          VK_PIPELINE_DEPTH_STENCIL_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_STENCIL_ACCESS_BIT_ARM);
   }

   /* FDM isn't compatible with LRZ, because the LRZ image uses the original
    * resolution and we would need to use the low resolution.
    *
    * TODO: Use a patchpoint to only disable LRZ for scaled bins.
    */
   if (builder->fragment_density_map)
      pipeline->lrz.lrz_status = TU_LRZ_FORCE_DISABLE_LRZ;
}

static void
tu_pipeline_builder_parse_multisample_and_color_blend(
   struct tu_pipeline_builder *builder, struct tu_pipeline *pipeline)
{
   /* The spec says:
    *
    *    pMultisampleState is a pointer to an instance of the
    *    VkPipelineMultisampleStateCreateInfo, and is ignored if the pipeline
    *    has rasterization disabled.
    *
    * Also,
    *
    *    pColorBlendState is a pointer to an instance of the
    *    VkPipelineColorBlendStateCreateInfo structure, and is ignored if the
    *    pipeline has rasterization disabled or if the subpass of the render
    *    pass the pipeline is created against does not use any color
    *    attachments.
    *
    * We leave the relevant registers stale when rasterization is disabled.
    */
   if (builder->rasterizer_discard) {
      return;
   }

   static const VkPipelineColorBlendStateCreateInfo dummy_blend_info = {};

   const VkPipelineColorBlendStateCreateInfo *blend_info =
      (builder->graphics_state.rp->attachment_aspects &
       VK_IMAGE_ASPECT_COLOR_BIT) ? builder->create_info->pColorBlendState :
      &dummy_blend_info;

   pipeline->lrz.force_late_z |=
      builder->graphics_state.rp->depth_attachment_format == VK_FORMAT_S8_UINT;

   if (builder->graphics_state.rp->attachment_aspects & VK_IMAGE_ASPECT_COLOR_BIT) {
      pipeline->output.raster_order_attachment_access =
         blend_info->flags &
         VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_ARM;
   }
}

static void
tu_pipeline_builder_parse_rasterization_order(
   struct tu_pipeline_builder *builder, struct tu_pipeline *pipeline)
{
   if (builder->rasterizer_discard)
      return;

   bool raster_order_attachment_access =
      pipeline->output.raster_order_attachment_access ||
      pipeline->ds.raster_order_attachment_access ||
      TU_DEBUG(RAST_ORDER);

   /* VK_EXT_blend_operation_advanced would also require ordered access
    * when implemented in the future.
    */

   enum a6xx_single_prim_mode sysmem_prim_mode = NO_FLUSH;
   enum a6xx_single_prim_mode gmem_prim_mode = NO_FLUSH;

   if (raster_order_attachment_access) {
      /* VK_EXT_rasterization_order_attachment_access:
       *
       * This extension allow access to framebuffer attachments when used as
       * both input and color attachments from one fragment to the next,
       * in rasterization order, without explicit synchronization.
       */
      sysmem_prim_mode = FLUSH_PER_OVERLAP_AND_OVERWRITE;
      gmem_prim_mode = FLUSH_PER_OVERLAP;
      pipeline->prim_order.sysmem_single_prim_mode = true;
   } else {
      /* If there is a feedback loop, then the shader can read the previous value
       * of a pixel being written out. It can also write some components and then
       * read different components without a barrier in between. This is a
       * problem in sysmem mode with UBWC, because the main buffer and flags
       * buffer can get out-of-sync if only one is flushed. We fix this by
       * setting the SINGLE_PRIM_MODE field to the same value that the blob does
       * for advanced_blend in sysmem mode if a feedback loop is detected.
       */
      if (builder->graphics_state.rp->pipeline_flags &
          (VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT |
           VK_PIPELINE_CREATE_DEPTH_STENCIL_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT)) {
         sysmem_prim_mode = FLUSH_PER_OVERLAP_AND_OVERWRITE;
         pipeline->prim_order.sysmem_single_prim_mode = true;
      }
   }

   struct tu_cs cs;

   pipeline->prim_order.state_gmem = tu_cs_draw_state(&pipeline->cs, &cs, 2);
   tu_cs_emit_write_reg(&cs, REG_A6XX_GRAS_SC_CNTL,
                        A6XX_GRAS_SC_CNTL_CCUSINGLECACHELINESIZE(2) |
                        A6XX_GRAS_SC_CNTL_SINGLE_PRIM_MODE(gmem_prim_mode));

   pipeline->prim_order.state_sysmem = tu_cs_draw_state(&pipeline->cs, &cs, 2);
   tu_cs_emit_write_reg(&cs, REG_A6XX_GRAS_SC_CNTL,
                        A6XX_GRAS_SC_CNTL_CCUSINGLECACHELINESIZE(2) |
                        A6XX_GRAS_SC_CNTL_SINGLE_PRIM_MODE(sysmem_prim_mode));
}

static void
tu_pipeline_finish(struct tu_pipeline *pipeline,
                   struct tu_device *dev,
                   const VkAllocationCallbacks *alloc)
{
   tu_cs_finish(&pipeline->cs);
   mtx_lock(&dev->pipeline_mutex);
   tu_suballoc_bo_free(&dev->pipeline_suballoc, &pipeline->bo);
   mtx_unlock(&dev->pipeline_mutex);

   if (pipeline->pvtmem_bo)
      tu_bo_finish(dev, pipeline->pvtmem_bo);

   if (pipeline->type == TU_PIPELINE_GRAPHICS_LIB) {
      struct tu_graphics_lib_pipeline *library =
         tu_pipeline_to_graphics_lib(pipeline);
      if (library->compiled_shaders)
         vk_pipeline_cache_object_unref(&dev->vk,
                                        &library->compiled_shaders->base);

      if (library->nir_shaders)
         vk_pipeline_cache_object_unref(&dev->vk,
                                        &library->nir_shaders->base);

      for (unsigned i = 0; i < library->num_sets; i++) {
         if (library->layouts[i])
            vk_descriptor_set_layout_unref(&dev->vk, &library->layouts[i]->vk);
      }

      vk_free2(&dev->vk.alloc, alloc, library->state_data);
   }

   ralloc_free(pipeline->executables_mem_ctx);
}

static VkGraphicsPipelineLibraryFlagBitsEXT
vk_shader_stage_to_pipeline_library_flags(VkShaderStageFlagBits stage)
{
   assert(util_bitcount(stage) == 1);
   switch (stage) {
   case VK_SHADER_STAGE_VERTEX_BIT:
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
   case VK_SHADER_STAGE_GEOMETRY_BIT:
   case VK_SHADER_STAGE_TASK_BIT_EXT:
   case VK_SHADER_STAGE_MESH_BIT_EXT:
      return VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT;
   case VK_SHADER_STAGE_FRAGMENT_BIT:
      return VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT;
   default:
      unreachable("Invalid shader stage");
   }
}

static VkResult
tu_pipeline_builder_build(struct tu_pipeline_builder *builder,
                          struct tu_pipeline **pipeline)
{
   VkResult result;

   if (builder->create_info->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR) {
      *pipeline = (struct tu_pipeline *) vk_object_zalloc(
         &builder->device->vk, builder->alloc,
         sizeof(struct tu_graphics_lib_pipeline),
         VK_OBJECT_TYPE_PIPELINE);
      if (!*pipeline)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      (*pipeline)->type = TU_PIPELINE_GRAPHICS_LIB;
   } else {
      *pipeline = (struct tu_pipeline *) vk_object_zalloc(
         &builder->device->vk, builder->alloc,
         sizeof(struct tu_graphics_pipeline),
         VK_OBJECT_TYPE_PIPELINE);
      if (!*pipeline)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      (*pipeline)->type = TU_PIPELINE_GRAPHICS;
   }

   (*pipeline)->executables_mem_ctx = ralloc_context(NULL);
   util_dynarray_init(&(*pipeline)->executables, (*pipeline)->executables_mem_ctx);

   tu_pipeline_builder_parse_libraries(builder, *pipeline);

   VkShaderStageFlags stages = 0;
   for (unsigned i = 0; i < builder->create_info->stageCount; i++) {
      VkShaderStageFlagBits stage = builder->create_info->pStages[i].stage;

      /* Ignore shader stages that don't need to be imported. */
      if (!(vk_shader_stage_to_pipeline_library_flags(stage) & builder->state))
         continue;

      stages |= stage;
   }
   builder->active_stages = stages;

   (*pipeline)->active_stages = stages;
   for (unsigned i = 0; i < builder->num_libraries; i++)
      (*pipeline)->active_stages |= builder->libraries[i]->base.active_stages;

   /* Compile and upload shaders unless a library has already done that. */
   if ((*pipeline)->program.state.size == 0) {
      tu_pipeline_builder_parse_layout(builder, *pipeline);

      result = tu_pipeline_builder_compile_shaders(builder, *pipeline);
      if (result != VK_SUCCESS) {
         vk_object_free(&builder->device->vk, builder->alloc, *pipeline);
         return result;
      }
   }

   result = tu_pipeline_allocate_cs(builder->device, *pipeline,
                                    &builder->layout, builder, NULL);


   if (set_combined_state(builder, *pipeline,
                          VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
                          VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT)) {
      if (result != VK_SUCCESS) {
         vk_object_free(&builder->device->vk, builder->alloc, *pipeline);
         return result;
      }

      for (uint32_t i = 0; i < ARRAY_SIZE(builder->shader_iova); i++)
         builder->shader_iova[i] =
            tu_upload_variant(*pipeline, builder->variants[i]);

      builder->binning_vs_iova =
         tu_upload_variant(*pipeline, builder->binning_variant);

      /* Setup private memory. Note that because we're sharing the same private
       * memory for all stages, all stages must use the same config, or else
       * fibers from one stage might overwrite fibers in another.
       */

      uint32_t pvtmem_size = 0;
      bool per_wave = true;
      for (uint32_t i = 0; i < ARRAY_SIZE(builder->variants); i++) {
         if (builder->variants[i]) {
            pvtmem_size = MAX2(pvtmem_size, builder->variants[i]->pvtmem_size);
            if (!builder->variants[i]->pvtmem_per_wave)
               per_wave = false;
         }
      }

      if (builder->binning_variant) {
         pvtmem_size = MAX2(pvtmem_size, builder->binning_variant->pvtmem_size);
         if (!builder->binning_variant->pvtmem_per_wave)
            per_wave = false;
      }

      result = tu_setup_pvtmem(builder->device, *pipeline, &builder->pvtmem,
                               pvtmem_size, per_wave);
      if (result != VK_SUCCESS) {
         vk_object_free(&builder->device->vk, builder->alloc, *pipeline);
         return result;
      }

      tu_pipeline_builder_parse_shader_stages(builder, *pipeline);
      tu6_emit_load_state(*pipeline, &builder->layout);
   }

   if (builder->state & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
      tu_pipeline_builder_parse_depth_stencil(builder, *pipeline);
   }

   if (builder->state &
       VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT) {
      tu_pipeline_builder_parse_multisample_and_color_blend(builder, *pipeline);
   }

   if (set_combined_state(builder, *pipeline,
                          VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
                          VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT)) {
      tu_pipeline_builder_parse_rasterization_order(builder, *pipeline);
   }

   tu_pipeline_builder_emit_state(builder, *pipeline);

   if ((*pipeline)->type == TU_PIPELINE_GRAPHICS_LIB) {
      struct tu_graphics_lib_pipeline *library =
         tu_pipeline_to_graphics_lib(*pipeline);
      result = vk_graphics_pipeline_state_copy(&builder->device->vk,
                                               &library->graphics_state,
                                               &builder->graphics_state,
                                               builder->alloc,
                                               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
                                               &library->state_data);
      if (result != VK_SUCCESS) {
         tu_pipeline_finish(*pipeline, builder->device, builder->alloc);
         return result;
      }
   } else {
      struct tu_graphics_pipeline *gfx_pipeline =
         tu_pipeline_to_graphics(*pipeline);
      vk_dynamic_graphics_state_fill(&gfx_pipeline->dynamic_state,
                                     &builder->graphics_state);
      gfx_pipeline->feedback_loop_color =
         (builder->graphics_state.rp->pipeline_flags &
          VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT);
      gfx_pipeline->feedback_loop_ds =
         (builder->graphics_state.rp->pipeline_flags &
          VK_PIPELINE_CREATE_DEPTH_STENCIL_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT);
      gfx_pipeline->feedback_loop_may_involve_textures =
         (gfx_pipeline->feedback_loop_color ||
          gfx_pipeline->feedback_loop_ds) &&
         !builder->graphics_state.rp->feedback_loop_input_only;
      gfx_pipeline->has_fdm = builder->fragment_density_map;
   }

   return VK_SUCCESS;
}

static void
tu_pipeline_builder_finish(struct tu_pipeline_builder *builder)
{
   if (builder->compiled_shaders)
      vk_pipeline_cache_object_unref(&builder->device->vk,
                                     &builder->compiled_shaders->base);
   ralloc_free(builder->mem_ctx);
}

void
tu_fill_render_pass_state(struct vk_render_pass_state *rp,
                          const struct tu_render_pass *pass,
                          const struct tu_subpass *subpass)
{
   rp->view_mask = subpass->multiview_mask;
   rp->color_attachment_count = subpass->color_count;
   rp->pipeline_flags = 0;

   const uint32_t a = subpass->depth_stencil_attachment.attachment;
   rp->depth_attachment_format = VK_FORMAT_UNDEFINED;
   rp->stencil_attachment_format = VK_FORMAT_UNDEFINED;
   rp->attachment_aspects = 0;
   if (a != VK_ATTACHMENT_UNUSED) {
      VkFormat ds_format = pass->attachments[a].format;
      if (vk_format_has_depth(ds_format)) {
         rp->depth_attachment_format = ds_format;
         rp->attachment_aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
      }
      if (vk_format_has_stencil(ds_format)) {
         rp->stencil_attachment_format = ds_format;
         rp->attachment_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
      }
   }

   for (uint32_t i = 0; i < subpass->color_count; i++) {
      const uint32_t a = subpass->color_attachments[i].attachment;
      if (a == VK_ATTACHMENT_UNUSED) {
         rp->color_attachment_formats[i] = VK_FORMAT_UNDEFINED;
         continue;
      }

      rp->color_attachment_formats[i] = pass->attachments[a].format;
      rp->attachment_aspects |= VK_IMAGE_ASPECT_COLOR_BIT;
   }
}

static void
tu_pipeline_builder_init_graphics(
   struct tu_pipeline_builder *builder,
   struct tu_device *dev,
   struct vk_pipeline_cache *cache,
   const VkGraphicsPipelineCreateInfo *create_info,
   const VkAllocationCallbacks *alloc)
{
   *builder = (struct tu_pipeline_builder) {
      .device = dev,
      .mem_ctx = ralloc_context(NULL),
      .cache = cache,
      .alloc = alloc,
      .create_info = create_info,
   };

   const VkGraphicsPipelineLibraryCreateInfoEXT *gpl_info =
      vk_find_struct_const(builder->create_info->pNext, 
                           GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT);

   const VkPipelineLibraryCreateInfoKHR *library_info =
      vk_find_struct_const(builder->create_info->pNext, 
                           PIPELINE_LIBRARY_CREATE_INFO_KHR);

   if (gpl_info) {
      builder->state = gpl_info->flags;
   } else {
      /* Implement this bit of spec text:
       *
       *    If this structure is omitted, and either
       *    VkGraphicsPipelineCreateInfo::flags includes
       *    VK_PIPELINE_CREATE_LIBRARY_BIT_KHR or the
       *    VkGraphicsPipelineCreateInfo::pNext chain includes a
       *    VkPipelineLibraryCreateInfoKHR structure with a libraryCount
       *    greater than 0, it is as if flags is 0. Otherwise if this
       *    structure is omitted, it is as if flags includes all possible
       *    subsets of the graphics pipeline (i.e. a complete graphics
       *    pipeline).
       */
      if ((library_info && library_info->libraryCount > 0) ||
          (builder->create_info->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR)) {
         builder->state = 0;
      } else {
         builder->state =
            VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |
            VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
            VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
            VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;
      }
   }

   bool rasterizer_discard_dynamic = false;
   if (create_info->pDynamicState) {
      for (uint32_t i = 0; i < create_info->pDynamicState->dynamicStateCount; i++) {
         if (create_info->pDynamicState->pDynamicStates[i] ==
               VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE) {
            rasterizer_discard_dynamic = true;
            break;
         }
      }
   }

   builder->rasterizer_discard =
      (builder->state & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) &&
      builder->create_info->pRasterizationState->rasterizerDiscardEnable &&
      !rasterizer_discard_dynamic;

   struct vk_render_pass_state rp_state = {
      .render_pass = builder->create_info->renderPass,
      .subpass = builder->create_info->subpass,
   };
   const struct vk_render_pass_state *driver_rp = NULL;

   builder->unscaled_input_fragcoord = 0;

   /* Extract information we need from the turnip renderpass. This will be
    * filled out automatically if the app is using dynamic rendering or
    * renderpasses are emulated.
    */
   if (!TU_DEBUG(DYNAMIC) &&
       (builder->state &
        (VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
         VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
         VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT)) &&
       builder->create_info->renderPass) {
      const struct tu_render_pass *pass =
         tu_render_pass_from_handle(create_info->renderPass);
      const struct tu_subpass *subpass =
         &pass->subpasses[create_info->subpass];

      rp_state = (struct vk_render_pass_state) {
         .render_pass = builder->create_info->renderPass,
         .subpass = builder->create_info->subpass,
      };

      tu_fill_render_pass_state(&rp_state, pass, subpass);

      rp_state.feedback_loop_input_only = true;

      for (unsigned i = 0; i < subpass->input_count; i++) {
         /* Input attachments stored in GMEM must be loaded with unscaled
          * FragCoord.
          */
         if (subpass->input_attachments[i].patch_input_gmem)
            builder->unscaled_input_fragcoord |= 1u << i;
      }

      /* Feedback loop flags can come from either the user (in which case they
       * may involve textures) or from the driver (in which case they don't).
       */
      VkPipelineCreateFlags feedback_flags = builder->create_info->flags &
         (VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT |
          VK_PIPELINE_CREATE_DEPTH_STENCIL_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT);
      if (feedback_flags) {
         rp_state.feedback_loop_input_only = false;
         rp_state.pipeline_flags |= feedback_flags;
      }

      if (subpass->feedback_loop_color) {
         rp_state.pipeline_flags |=
            VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;
      }

      if (subpass->feedback_loop_ds) {
         rp_state.pipeline_flags |=
            VK_PIPELINE_CREATE_DEPTH_STENCIL_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;
      }

      if (pass->fragment_density_map.attachment != VK_ATTACHMENT_UNUSED) {
         rp_state.pipeline_flags |=
            VK_PIPELINE_CREATE_RENDERING_FRAGMENT_DENSITY_MAP_ATTACHMENT_BIT_EXT;
      }

      builder->unscaled_input_fragcoord = 0;
      for (unsigned i = 0; i < subpass->input_count; i++) {
         /* Input attachments stored in GMEM must be loaded with unscaled
          * FragCoord.
          */
         if (subpass->input_attachments[i].patch_input_gmem)
            builder->unscaled_input_fragcoord |= 1u << i;
      }

      driver_rp = &rp_state;
   }

   vk_graphics_pipeline_state_fill(&dev->vk,
                                   &builder->graphics_state,
                                   builder->create_info,
                                   driver_rp,
                                   &builder->all_state,
                                   NULL, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
                                   NULL);

   if (builder->graphics_state.rp) {
      builder->fragment_density_map = (builder->graphics_state.rp->pipeline_flags &
         VK_PIPELINE_CREATE_RENDERING_FRAGMENT_DENSITY_MAP_ATTACHMENT_BIT_EXT) ||
         TU_DEBUG(FDM);
   }
}

static VkResult
tu_graphics_pipeline_create(VkDevice device,
                            VkPipelineCache pipelineCache,
                            const VkGraphicsPipelineCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipeline *pPipeline)
{
   TU_FROM_HANDLE(tu_device, dev, device);
   TU_FROM_HANDLE(vk_pipeline_cache, cache, pipelineCache);

   cache = cache ? cache : dev->mem_cache;

   struct tu_pipeline_builder builder;
   tu_pipeline_builder_init_graphics(&builder, dev, cache,
                                     pCreateInfo, pAllocator);

   struct tu_pipeline *pipeline = NULL;
   VkResult result = tu_pipeline_builder_build(&builder, &pipeline);
   tu_pipeline_builder_finish(&builder);

   if (result == VK_SUCCESS)
      *pPipeline = tu_pipeline_to_handle(pipeline);
   else
      *pPipeline = VK_NULL_HANDLE;

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateGraphicsPipelines(VkDevice device,
                           VkPipelineCache pipelineCache,
                           uint32_t count,
                           const VkGraphicsPipelineCreateInfo *pCreateInfos,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipeline *pPipelines)
{
   MESA_TRACE_FUNC();
   VkResult final_result = VK_SUCCESS;
   uint32_t i = 0;

   for (; i < count; i++) {
      VkResult result = tu_graphics_pipeline_create(device, pipelineCache,
                                                    &pCreateInfos[i], pAllocator,
                                                    &pPipelines[i]);

      if (result != VK_SUCCESS) {
         final_result = result;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags &
             VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   return final_result;
}

static VkResult
tu_compute_pipeline_create(VkDevice device,
                           VkPipelineCache pipelineCache,
                           const VkComputePipelineCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipeline *pPipeline)
{
   TU_FROM_HANDLE(tu_device, dev, device);
   TU_FROM_HANDLE(vk_pipeline_cache, cache, pipelineCache);
   TU_FROM_HANDLE(tu_pipeline_layout, layout, pCreateInfo->layout);
   const VkPipelineShaderStageCreateInfo *stage_info = &pCreateInfo->stage;
   VkResult result;
   struct ir3_shader_variant *v = NULL;
   uint32_t additional_reserve_size = 0;
   uint64_t shader_iova = 0;

   cache = cache ? cache : dev->mem_cache;

   struct tu_compute_pipeline *pipeline;

   *pPipeline = VK_NULL_HANDLE;

   VkPipelineCreationFeedback pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };

   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

   int64_t pipeline_start = os_time_get_nano();

   pipeline = (struct tu_compute_pipeline *) vk_object_zalloc(
      &dev->vk, pAllocator, sizeof(*pipeline), VK_OBJECT_TYPE_PIPELINE);
   if (!pipeline)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   pipeline->base.type = TU_PIPELINE_COMPUTE;

   pipeline->base.executables_mem_ctx = ralloc_context(NULL);
   util_dynarray_init(&pipeline->base.executables, pipeline->base.executables_mem_ctx);
   pipeline->base.active_stages = VK_SHADER_STAGE_COMPUTE_BIT;

   struct tu_shader_key key = { };
   tu_shader_key_init(&key, stage_info, dev);

   void *pipeline_mem_ctx = ralloc_context(NULL);

   unsigned char pipeline_sha1[20];
   tu_hash_compute(pipeline_sha1, stage_info, layout, &key, dev->compiler);

   struct tu_compiled_shaders *compiled = NULL;

   const bool executable_info = pCreateInfo->flags &
      VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;

   bool application_cache_hit = false;

   if (!executable_info) {
      compiled =
         tu_pipeline_cache_lookup(cache, pipeline_sha1, sizeof(pipeline_sha1),
                                  &application_cache_hit);
   }

   if (application_cache_hit && cache != dev->mem_cache) {
      pipeline_feedback.flags |=
         VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
   }

   if (tu6_shared_constants_enable(layout, dev->compiler)) {
      pipeline->base.shared_consts = (struct tu_push_constant_range) {
         .lo = 0,
         .dwords = layout->push_constant_size / 4,
      };
   }

   char *nir_initial_disasm = NULL;

   if (!compiled) {
      if (pCreateInfo->flags &
          VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) {
         result = VK_PIPELINE_COMPILE_REQUIRED;
         goto fail;
      }

      struct ir3_shader_key ir3_key = {};

      nir_shader *nir = tu_spirv_to_nir(dev, pipeline_mem_ctx, stage_info,
                                        MESA_SHADER_COMPUTE);

      nir_initial_disasm = executable_info ?
         nir_shader_as_str(nir, pipeline->base.executables_mem_ctx) : NULL;

      struct tu_shader *shader =
         tu_shader_create(dev, nir, &key, layout, pAllocator);
      if (!shader) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      compiled = tu_shaders_init(dev, &pipeline_sha1, sizeof(pipeline_sha1));
      if (!compiled) {
         tu_shader_destroy(dev, shader, pAllocator);
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      compiled->active_desc_sets = shader->active_desc_sets;
      compiled->const_state[MESA_SHADER_COMPUTE] = shader->const_state;

      struct ir3_shader_variant *v =
         ir3_shader_create_variant(shader->ir3_shader, &ir3_key, executable_info);

      tu_shader_destroy(dev, shader, pAllocator);

      if (!v) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      compiled->variants[MESA_SHADER_COMPUTE] = v;

      compiled = tu_pipeline_cache_insert(cache, compiled);
   }

   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;

   if (creation_feedback) {
      *creation_feedback->pPipelineCreationFeedback = pipeline_feedback;
      assert(creation_feedback->pipelineStageCreationFeedbackCount == 1);
      creation_feedback->pPipelineStageCreationFeedbacks[0] = pipeline_feedback;
   }

   pipeline->base.active_desc_sets = compiled->active_desc_sets;

   v = compiled->variants[MESA_SHADER_COMPUTE];

   tu_pipeline_set_linkage(&pipeline->base.program.link[MESA_SHADER_COMPUTE],
                           &compiled->const_state[MESA_SHADER_COMPUTE], v);

   result = tu_pipeline_allocate_cs(dev, &pipeline->base, layout, NULL, v);
   if (result != VK_SUCCESS)
      goto fail;

   shader_iova = tu_upload_variant(&pipeline->base, v);

   struct tu_pvtmem_config pvtmem;
   tu_setup_pvtmem(dev, &pipeline->base, &pvtmem, v->pvtmem_size, v->pvtmem_per_wave);

   for (int i = 0; i < 3; i++)
      pipeline->local_size[i] = v->local_size[i];

   pipeline->subgroup_size = v->info.subgroup_size;

   struct tu_cs prog_cs;
   additional_reserve_size = tu_xs_get_additional_cs_size_dwords(v);
   tu_cs_begin_sub_stream(&pipeline->base.cs, 64 + additional_reserve_size, &prog_cs);
   tu6_emit_cs_config(&prog_cs, v, &pvtmem, shader_iova);
   pipeline->base.program.state = tu_cs_end_draw_state(&pipeline->base.cs, &prog_cs);

   tu6_emit_load_state(&pipeline->base, layout);

   tu_append_executable(&pipeline->base, v, nir_initial_disasm);

   pipeline->instrlen = v->instrlen;

   vk_pipeline_cache_object_unref(&dev->vk, &compiled->base);
   ralloc_free(pipeline_mem_ctx);

   *pPipeline = tu_pipeline_to_handle(&pipeline->base);

   return VK_SUCCESS;

fail:
   if (compiled)
      vk_pipeline_cache_object_unref(&dev->vk, &compiled->base);

   ralloc_free(pipeline_mem_ctx);

   vk_object_free(&dev->vk, pAllocator, pipeline);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateComputePipelines(VkDevice device,
                          VkPipelineCache pipelineCache,
                          uint32_t count,
                          const VkComputePipelineCreateInfo *pCreateInfos,
                          const VkAllocationCallbacks *pAllocator,
                          VkPipeline *pPipelines)
{
   MESA_TRACE_FUNC();
   VkResult final_result = VK_SUCCESS;
   uint32_t i = 0;

   for (; i < count; i++) {
      VkResult result = tu_compute_pipeline_create(device, pipelineCache,
                                                   &pCreateInfos[i],
                                                   pAllocator, &pPipelines[i]);
      if (result != VK_SUCCESS) {
         final_result = result;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags &
             VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   return final_result;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyPipeline(VkDevice _device,
                   VkPipeline _pipeline,
                   const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, dev, _device);
   TU_FROM_HANDLE(tu_pipeline, pipeline, _pipeline);

   if (!_pipeline)
      return;

   tu_pipeline_finish(pipeline, dev, pAllocator);
   vk_object_free(&dev->vk, pAllocator, pipeline);
}

#define WRITE_STR(field, ...) ({                                \
   memset(field, 0, sizeof(field));                             \
   UNUSED int _i = snprintf(field, sizeof(field), __VA_ARGS__); \
   assert(_i > 0 && _i < sizeof(field));                        \
})

static const struct tu_pipeline_executable *
tu_pipeline_get_executable(struct tu_pipeline *pipeline, uint32_t index)
{
   assert(index < util_dynarray_num_elements(&pipeline->executables,
                                             struct tu_pipeline_executable));
   return util_dynarray_element(
      &pipeline->executables, struct tu_pipeline_executable, index);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetPipelineExecutablePropertiesKHR(
      VkDevice _device,
      const VkPipelineInfoKHR* pPipelineInfo,
      uint32_t* pExecutableCount,
      VkPipelineExecutablePropertiesKHR* pProperties)
{
   TU_FROM_HANDLE(tu_device, dev, _device);
   TU_FROM_HANDLE(tu_pipeline, pipeline, pPipelineInfo->pipeline);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutablePropertiesKHR, out,
                          pProperties, pExecutableCount);

   util_dynarray_foreach (&pipeline->executables, struct tu_pipeline_executable, exe) {
      vk_outarray_append_typed(VkPipelineExecutablePropertiesKHR, &out, props) {
         gl_shader_stage stage = exe->stage;
         props->stages = mesa_to_vk_shader_stage(stage);

         if (!exe->is_binning)
            WRITE_STR(props->name, "%s", _mesa_shader_stage_to_abbrev(stage));
         else
            WRITE_STR(props->name, "Binning VS");

         WRITE_STR(props->description, "%s", _mesa_shader_stage_to_string(stage));

         props->subgroupSize =
            dev->compiler->threadsize_base * (exe->stats.double_threadsize ? 2 : 1);
      }
   }

   return vk_outarray_status(&out);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetPipelineExecutableStatisticsKHR(
      VkDevice _device,
      const VkPipelineExecutableInfoKHR* pExecutableInfo,
      uint32_t* pStatisticCount,
      VkPipelineExecutableStatisticKHR* pStatistics)
{
   TU_FROM_HANDLE(tu_pipeline, pipeline, pExecutableInfo->pipeline);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableStatisticKHR, out,
                          pStatistics, pStatisticCount);

   const struct tu_pipeline_executable *exe =
      tu_pipeline_get_executable(pipeline, pExecutableInfo->executableIndex);

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Max Waves Per Core");
      WRITE_STR(stat->description,
                "Maximum number of simultaneous waves per core.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.max_waves;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Instruction Count");
      WRITE_STR(stat->description,
                "Total number of IR3 instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.instrs_count;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Code size");
      WRITE_STR(stat->description,
                "Total number of dwords in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.sizedwords;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "NOPs Count");
      WRITE_STR(stat->description,
                "Number of NOP instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.nops_count;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "MOV Count");
      WRITE_STR(stat->description,
                "Number of MOV instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.mov_count;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "COV Count");
      WRITE_STR(stat->description,
                "Number of COV instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.cov_count;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Registers used");
      WRITE_STR(stat->description,
                "Number of registers used in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.max_reg + 1;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Half-registers used");
      WRITE_STR(stat->description,
                "Number of half-registers used in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.max_half_reg + 1;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Instructions with SS sync bit");
      WRITE_STR(stat->description,
                "SS bit is set for instructions which depend on a result "
                "of \"long\" instructions to prevent RAW hazard.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.ss;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Instructions with SY sync bit");
      WRITE_STR(stat->description,
                "SY bit is set for instructions which depend on a result "
                "of loads from global memory to prevent RAW hazard.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.sy;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Estimated cycles stalled on SS");
      WRITE_STR(stat->description,
                "A better metric to estimate the impact of SS syncs.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.sstall;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Estimated cycles stalled on SY");
      WRITE_STR(stat->description,
                "A better metric to estimate the impact of SY syncs.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.systall;
   }

   for (int i = 0; i < ARRAY_SIZE(exe->stats.instrs_per_cat); i++) {
      vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
         WRITE_STR(stat->name, "cat%d instructions", i);
         WRITE_STR(stat->description,
                  "Number of cat%d instructions.", i);
         stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
         stat->value.u64 = exe->stats.instrs_per_cat[i];
      }
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "STP Count");
      WRITE_STR(stat->description,
                "Number of STore Private instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.stp_count;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "LDP Count");
      WRITE_STR(stat->description,
                "Number of LoaD Private instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.ldp_count;
   }

   return vk_outarray_status(&out);
}

static bool
write_ir_text(VkPipelineExecutableInternalRepresentationKHR* ir,
              const char *data)
{
   ir->isText = VK_TRUE;

   size_t data_len = strlen(data) + 1;

   if (ir->pData == NULL) {
      ir->dataSize = data_len;
      return true;
   }

   strncpy((char *) ir->pData, data, ir->dataSize);
   if (ir->dataSize < data_len)
      return false;

   ir->dataSize = data_len;
   return true;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetPipelineExecutableInternalRepresentationsKHR(
    VkDevice _device,
    const VkPipelineExecutableInfoKHR* pExecutableInfo,
    uint32_t* pInternalRepresentationCount,
    VkPipelineExecutableInternalRepresentationKHR* pInternalRepresentations)
{
   TU_FROM_HANDLE(tu_pipeline, pipeline, pExecutableInfo->pipeline);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableInternalRepresentationKHR, out,
                          pInternalRepresentations, pInternalRepresentationCount);
   bool incomplete_text = false;

   const struct tu_pipeline_executable *exe =
      tu_pipeline_get_executable(pipeline, pExecutableInfo->executableIndex);

   if (exe->nir_from_spirv) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR, &out, ir) {
         WRITE_STR(ir->name, "NIR from SPIRV");
         WRITE_STR(ir->description,
                   "Initial NIR before any optimizations");

         if (!write_ir_text(ir, exe->nir_from_spirv))
            incomplete_text = true;
      }
   }

   if (exe->nir_final) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR, &out, ir) {
         WRITE_STR(ir->name, "Final NIR");
         WRITE_STR(ir->description,
                   "Final NIR before going into the back-end compiler");

         if (!write_ir_text(ir, exe->nir_final))
            incomplete_text = true;
      }
   }

   if (exe->disasm) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR, &out, ir) {
         WRITE_STR(ir->name, "IR3 Assembly");
         WRITE_STR(ir->description,
                   "Final IR3 assembly for the generated shader binary");

         if (!write_ir_text(ir, exe->disasm))
            incomplete_text = true;
      }
   }

   return incomplete_text ? VK_INCOMPLETE : vk_outarray_status(&out);
}
