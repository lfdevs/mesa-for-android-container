/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2024 Arm Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "genxml/gen_macros.h"

#include "drm-uapi/panthor_drm.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_draw.h"
#include "panvk_cmd_fb_preload.h"
#include "panvk_cmd_meta.h"
#include "panvk_cmd_ts.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_image_view.h"
#include "panvk_instance.h"
#include "panvk_instr.h"
#include "panvk_priv_bo.h"
#include "panvk_query_pool.h"
#include "panvk_shader.h"
#include "panvk_tracepoints.h"

#include "pan_desc.h"
#include "pan_earlyzs.h"
#include "pan_encoder.h"
#include "pan_format.h"
#include "pan_jc.h"
#include "pan_props.h"
#include "pan_samples.h"
#include "pan_shader.h"

#include "util/bitscan.h"
#include "vk_format.h"
#include "vk_meta.h"
#include "vk_pipeline_layout.h"
#include "vk_render_pass.h"

static enum cs_reg_perm
provoking_vertex_fn_reg_perm_cb(struct cs_builder *b, unsigned reg)
{
   return CS_REG_RW;
}

#define PROVOKING_VERTEX_FN_MAX_SIZE 512

static size_t
generate_fn_set_fbds_provoking_vertex(struct panvk_device *dev,
                                      struct cs_buffer fn_mem, bool has_zs_ext,
                                      uint32_t rt_count,
                                      uint32_t *dump_region_size)
{
   const struct drm_panthor_csif_info *csif_info =
      panthor_kmod_get_csif_props(dev->kmod.dev);

   struct cs_builder b;
   struct cs_builder_conf conf = {
      .nr_registers = csif_info->cs_reg_count,
      .nr_kernel_registers = MAX2(csif_info->unpreserved_cs_reg_count, 4),
      .reg_perm = provoking_vertex_fn_reg_perm_cb,
      .ls_sb_slot = SB_ID(LS),
   };
   cs_builder_init(&b, &conf, fn_mem);

   struct cs_function function;
   struct cs_function_ctx function_ctx = {
      .ctx_reg = cs_subqueue_ctx_reg(&b),
      .dump_addr_offset =
         offsetof(struct panvk_cs_subqueue_context, reg_dump_addr),
   };

   cs_function_def(&b, &function, function_ctx) {
      uint32_t fbd_sz = get_fbd_size(has_zs_ext, rt_count);

      /* argument passed in by the caller */
      struct cs_index fbd_count = cs_scratch_reg32(&b, 0);

      /* normal scratch regs */
      struct cs_index scratch_reg = cs_scratch_reg32(&b, 1);
      struct cs_index fbd_addr = cs_scratch_reg64(&b, 2);

      cs_add64(&b, fbd_addr, cs_sr_reg64(&b, FRAGMENT, FBD_POINTER), 0);

      cs_while(&b, MALI_CS_CONDITION_GREATER, fbd_count) {
         /* provoking_vertex flag is bit 14 of word 11 */
         unsigned offset = 11 * 4;
         cs_load32_to(&b, scratch_reg, fbd_addr, offset);
         cs_flush_loads(&b);
         cs_add32(&b, scratch_reg, scratch_reg, -(1 << 14));
         cs_store32(&b, scratch_reg, fbd_addr, offset);
         cs_flush_stores(&b);

         cs_add32(&b, fbd_count, fbd_count, -1);
         cs_add64(&b, fbd_addr, fbd_addr, fbd_sz);
      }
   }

   assert(cs_is_valid(&b));
   cs_finish(&b);

   *dump_region_size = function.dump_size;

   return function.length * sizeof(uint64_t);
}

static uint32_t
get_fn_set_fbds_provoking_vertex_idx(bool has_zs_ext, uint32_t rt_count)
{
   assert(rt_count >= 1 && rt_count <= MAX_RTS);
   uint32_t idx = has_zs_ext * MAX_RTS + (rt_count - 1);
   assert(idx < 2 * MAX_RTS);
   return idx;
}

static uint32_t
calc_fn_set_fbds_provoking_vertex_idx(struct panvk_cmd_buffer *cmdbuf)
{
   const struct pan_fb_info *fb = &cmdbuf->state.gfx.render.fb.info;
   bool has_zs_ext = fb->zs.view.zs || fb->zs.view.s;
   uint32_t rt_count = MAX2(fb->rt_count, 1);

   return get_fn_set_fbds_provoking_vertex_idx(has_zs_ext, rt_count);
}

VkResult
panvk_per_arch(device_draw_context_init)(struct panvk_device *dev)
{
   dev->draw_ctx = vk_zalloc(&dev->vk.alloc,
            sizeof(struct panvk_device_draw_context),
            _Alignof(struct panvk_device_draw_context),
            VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (dev->draw_ctx == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   VkResult result = panvk_priv_bo_create(
      dev, PROVOKING_VERTEX_FN_MAX_SIZE * 2 * MAX_RTS, 0,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &dev->draw_ctx->fns_bo);
   if (result != VK_SUCCESS)
      goto free_draw_ctx;

   for (uint32_t has_zs_ext = 0; has_zs_ext <= 1; has_zs_ext++) {
      for (uint32_t rt_count = 1; rt_count <= MAX_RTS; rt_count++) {
         uint32_t idx =
            get_fn_set_fbds_provoking_vertex_idx(has_zs_ext, rt_count);
         /* Check that we have calculated a fn_stride if we need it to offset
          * addresses. */
         assert(idx == 0 ||
                dev->draw_ctx->fn_set_fbds_provoking_vertex_stride != 0);
         size_t offset =
            idx * dev->draw_ctx->fn_set_fbds_provoking_vertex_stride;

         struct cs_buffer fn_mem = {
            .cpu = dev->draw_ctx->fns_bo->addr.host + offset,
            .gpu = dev->draw_ctx->fns_bo->addr.dev + offset,
            .capacity = PROVOKING_VERTEX_FN_MAX_SIZE / sizeof(uint64_t),
         };

         uint32_t dump_region_size;
         size_t fn_length =
            generate_fn_set_fbds_provoking_vertex(dev, fn_mem, has_zs_ext,
                                                  rt_count, &dump_region_size);

         /* All functions must have the same length */
         assert(idx == 0 ||
                fn_length == dev->draw_ctx->fn_set_fbds_provoking_vertex_stride);
         dev->draw_ctx->fn_set_fbds_provoking_vertex_stride = fn_length;
         dev->dump_region_size[PANVK_SUBQUEUE_VERTEX_TILER] =
            MAX2(dev->dump_region_size[PANVK_SUBQUEUE_VERTEX_TILER],
                 dump_region_size);
      }
   }

   return VK_SUCCESS;

free_draw_ctx:
   vk_free(&dev->vk.alloc, dev->draw_ctx);
   return result;
}

void
panvk_per_arch(device_draw_context_cleanup)(struct panvk_device *dev)
{
   panvk_priv_bo_unref(dev->draw_ctx->fns_bo);
   vk_free(&dev->vk.alloc, dev->draw_ctx);
}

static void
emit_vs_attrib(struct panvk_cmd_buffer *cmdbuf,
               uint32_t attrib_idx, uint32_t vb_desc_offset,
               struct mali_attribute_packed *desc)
{
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_vertex_input_state *vi = dyns->vi;
   const struct vk_vertex_attribute_state *attrib_info =
      &vi->attributes[attrib_idx];
   const struct vk_vertex_binding_state *buf_info =
      &vi->bindings[attrib_info->binding];
   const uint32_t stride = dyns->vi_binding_strides[attrib_info->binding];
   bool per_instance = buf_info->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE;
   enum pipe_format f = vk_format_to_pipe_format(attrib_info->format);
   unsigned buf_idx = vb_desc_offset + attrib_info->binding;

   pan_pack(desc, ATTRIBUTE, cfg) {
      cfg.offset = attrib_info->offset;

      if (per_instance)
         cfg.offset += cmdbuf->state.gfx.sysvals.vs.base_instance * stride;

      cfg.format = GENX(pan_format_from_pipe_format)(f)->hw;
      cfg.table = 0;
      cfg.buffer_index = buf_idx;
      cfg.stride = stride;
      if (!per_instance) {
         /* Per-vertex */
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_VERTEX;
         cfg.offset_enable = true;
      } else if (buf_info->divisor == 1) {
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_INSTANCE;
      } else if (buf_info->divisor == 0) {
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D;
         /* HW doesn't support a zero divisor, but we can achieve the same by
          * not using a divisor and setting the stride to zero */
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_INSTANCE;
         cfg.stride = 0;
      } else if (util_is_power_of_two_or_zero(buf_info->divisor)) {
         /* Per-instance, POT divisor */
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D_POT_DIVISOR;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_INSTANCE;
         cfg.divisor_r = __builtin_ctz(buf_info->divisor);
      } else {
         /* Per-instance, NPOT divisor */
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_INSTANCE;
         cfg.divisor_d = pan_compute_npot_divisor(
            buf_info->divisor, &cfg.divisor_r, &cfg.divisor_e);
      }
   }
}

static bool
vs_driver_set_is_dirty(struct panvk_cmd_buffer *cmdbuf)
{
   return dyn_gfx_state_dirty(cmdbuf, VI) ||
          dyn_gfx_state_dirty(cmdbuf, VI_BINDINGS_VALID) ||
          dyn_gfx_state_dirty(cmdbuf, VI_BINDING_STRIDES) ||
          gfx_state_dirty(cmdbuf, VB) || gfx_state_dirty(cmdbuf, VS) ||
          gfx_state_dirty(cmdbuf, DESC_STATE);
}

static VkResult
prepare_vs_driver_set(struct panvk_cmd_buffer *cmdbuf,
                      const struct panvk_draw_info *draw)
{
   if (!vs_driver_set_is_dirty(cmdbuf))
      return VK_SUCCESS;

   struct panvk_shader_desc_state *vs_desc_state = &cmdbuf->state.gfx.vs.desc;
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_vertex_input_state *vi = dyns->vi;
   uint32_t vb_count = 0;

   cmdbuf->state.gfx.vi.attribs_changing_on_base_instance = 0;
   u_foreach_bit(i, vi->attributes_valid) {
      const struct vk_vertex_binding_state *binding =
         &vi->bindings[vi->attributes[i].binding];
      const uint32_t stride =
         dyns->vi_binding_strides[vi->attributes[i].binding];

      if (binding->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE && stride != 0) {
         cmdbuf->state.gfx.vi.attribs_changing_on_base_instance |=
            BITFIELD_BIT(i);
      }

      vb_count = MAX2(vi->attributes[i].binding + 1, vb_count);
   }

   uint32_t vb_offset = vs->desc_info.dyn_bufs.count + MAX_VS_ATTRIBS + 1;
   uint32_t desc_count = vb_offset + vb_count;
   uint32_t repeat_count = 1;

   if (draw->indirect.draw_count > 1 &&
       cmdbuf->state.gfx.vi.attribs_changing_on_base_instance != 0)
      repeat_count = draw->indirect.draw_count;

   const struct panvk_descriptor_state *desc_state =
      &cmdbuf->state.gfx.desc_state;
   struct pan_ptr driver_set = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, repeat_count * desc_count * PANVK_DESCRIPTOR_SIZE,
      PANVK_DESCRIPTOR_SIZE);
   struct panvk_opaque_desc *descs = driver_set.cpu;

   if (!driver_set.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   for (uint32_t r = 0; r < repeat_count; r++) {
      for (uint32_t i = 0; i < MAX_VS_ATTRIBS; i++) {
         if (vi->attributes_valid & BITFIELD_BIT(i)) {
            emit_vs_attrib(cmdbuf, i, vb_offset,
                           (struct mali_attribute_packed *)(&descs[i]));
         } else {
            /* Write a NullDescriptor and rely on OOB behavior */
            pan_cast_and_pack(&descs[i], NULL_DESCRIPTOR, cfg)
               ;
         }
      }

      /* Dummy sampler always comes right after the vertex attribs. */
      pan_cast_and_pack(&descs[MAX_VS_ATTRIBS], SAMPLER, cfg) {
         cfg.clamp_integer_array_indices = false;
      }

      panvk_per_arch(cmd_fill_dyn_bufs)(
         desc_state, vs,
         (struct mali_buffer_packed *)(&descs[MAX_VS_ATTRIBS + 1]));

      for (uint32_t i = 0; i < vb_count; i++) {
         const struct panvk_attrib_buf *vb = &cmdbuf->state.gfx.vb.bufs[i];
         const bool nulldesc = (vb->address == 0 && vb->size == 0);

         if ((vi->bindings_valid & BITFIELD_BIT(i)) && !nulldesc) {
            pan_cast_and_pack(&descs[vb_offset + i], BUFFER, cfg) {
               cfg.address = vb->address;
               cfg.size = vb->size;
            }
         } else {
            /* Write a NullDescriptor and rely on OOB behavior */
            pan_cast_and_pack(&descs[vb_offset + i], NULL_DESCRIPTOR, cfg)
               ;
         }
      }

      descs += desc_count;
   }

   vs_desc_state->driver_set.dev_addr = driver_set.gpu;
   vs_desc_state->driver_set.size = desc_count * PANVK_DESCRIPTOR_SIZE;
   gfx_state_set_dirty(cmdbuf, DESC_STATE);
   return VK_SUCCESS;
}

static uint32_t
get_varying_slots(const struct panvk_cmd_buffer *cmdbuf)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   uint32_t varying_slots = 0;

   if (fs) {
      unsigned vs_vars = vs->info.varyings.output_count;
      unsigned fs_vars = fs->info.varyings.input_count;
      varying_slots = MAX2(vs_vars, fs_vars);
   }

   return varying_slots;
}

static void
emit_varying_descs(const struct panvk_cmd_buffer *cmdbuf,
                   struct mali_attribute_packed *descs)
{
   uint32_t varying_slots = get_varying_slots(cmdbuf);
   /* Assumes 16 byte slots. We could do better. */
   uint32_t varying_size = varying_slots * 16;

   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));

   for (uint32_t i = 0; i < varying_slots; i++) {
      const struct pan_shader_varying *var = &fs->info.varyings.input[i];
      /* Skip special varyings. */
      if (var->location < VARYING_SLOT_VAR0)
         continue;

      uint32_t loc = var->location - VARYING_SLOT_VAR0;
      pan_pack(&descs[i], ATTRIBUTE, cfg) {
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_VERTEX_PACKET;
         cfg.offset_enable = false;
         cfg.format = GENX(pan_format_from_pipe_format)(var->format)->hw;
         cfg.table = 61;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_VERTEX;
         cfg.offset = 1024 + (loc * 16);
         /* On v12+, the hardware-controlled buffer is at index 1 for varyings */
         cfg.buffer_index = PAN_ARCH >= 12 ? 1 : 0;
         cfg.attribute_stride = varying_size;
         cfg.packet_stride = varying_size + 16;
      }
   }
}

static VkResult
prepare_fs_driver_set(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(cmdbuf->state.gfx.fs.shader);
   const struct panvk_descriptor_state *desc_state =
      &cmdbuf->state.gfx.desc_state;
   /* If the shader is using LD_VAR_BUF[_IMM], we do not have to set up
    * Attribute Descriptors for varying loads. */
   uint32_t num_varying_attr_descs =
      panvk_use_ld_var_buf(fs) ? 0 : fs->desc_info.max_varying_loads;
   uint32_t desc_count =
      fs->desc_info.dyn_bufs.count + num_varying_attr_descs + 1;
   struct pan_ptr driver_set = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, desc_count * PANVK_DESCRIPTOR_SIZE, PANVK_DESCRIPTOR_SIZE);
   struct panvk_opaque_desc *descs = driver_set.cpu;

   if (desc_count && !driver_set.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   if (num_varying_attr_descs > 0)
      emit_varying_descs(cmdbuf, (struct mali_attribute_packed *)(&descs[0]));

   /* Dummy sampler always comes right after the varyings. */
   pan_cast_and_pack(&descs[num_varying_attr_descs], SAMPLER, cfg) {
      cfg.clamp_integer_array_indices = false;
   }

   panvk_per_arch(cmd_fill_dyn_bufs)(
      desc_state, fs,
      (struct mali_buffer_packed *)(&descs[num_varying_attr_descs + 1]));

   fs_desc_state->driver_set.dev_addr = driver_set.gpu;
   fs_desc_state->driver_set.size = desc_count * PANVK_DESCRIPTOR_SIZE;
   gfx_state_set_dirty(cmdbuf, DESC_STATE);
   return VK_SUCCESS;
}

static bool
has_depth_att(struct panvk_cmd_buffer *cmdbuf)
{
   return (cmdbuf->state.gfx.render.bound_attachments &
           MESA_VK_RP_ATTACHMENT_DEPTH_BIT) != 0;
}

static bool
has_stencil_att(struct panvk_cmd_buffer *cmdbuf)
{
   return (cmdbuf->state.gfx.render.bound_attachments &
           MESA_VK_RP_ATTACHMENT_STENCIL_BIT) != 0;
}

static bool
writes_depth(struct panvk_cmd_buffer *cmdbuf)
{
   const struct vk_depth_stencil_state *ds =
      &cmdbuf->vk.dynamic_graphics_state.ds;

   return has_depth_att(cmdbuf) && ds->depth.test_enable &&
          ds->depth.write_enable && ds->depth.compare_op != VK_COMPARE_OP_NEVER;
}

static bool
writes_stencil(struct panvk_cmd_buffer *cmdbuf)
{
   const struct vk_depth_stencil_state *ds =
      &cmdbuf->vk.dynamic_graphics_state.ds;

   return has_stencil_att(cmdbuf) && ds->stencil.test_enable &&
          ((ds->stencil.front.write_mask &&
            (ds->stencil.front.op.fail != VK_STENCIL_OP_KEEP ||
             ds->stencil.front.op.pass != VK_STENCIL_OP_KEEP ||
             ds->stencil.front.op.depth_fail != VK_STENCIL_OP_KEEP)) ||
           (ds->stencil.back.write_mask &&
            (ds->stencil.back.op.fail != VK_STENCIL_OP_KEEP ||
             ds->stencil.back.op.pass != VK_STENCIL_OP_KEEP ||
             ds->stencil.back.op.depth_fail != VK_STENCIL_OP_KEEP)));
}

static bool
ds_test_always_passes(struct panvk_cmd_buffer *cmdbuf)
{
   const struct vk_depth_stencil_state *ds =
      &cmdbuf->vk.dynamic_graphics_state.ds;

   if (!has_depth_att(cmdbuf))
      return true;

   if (ds->depth.test_enable && ds->depth.compare_op != VK_COMPARE_OP_ALWAYS)
      return false;

   if (ds->stencil.test_enable &&
       (ds->stencil.front.op.compare != VK_COMPARE_OP_ALWAYS ||
        ds->stencil.back.op.compare != VK_COMPARE_OP_ALWAYS))
      return false;

   return true;
}

static inline enum mali_func
translate_compare_func(VkCompareOp comp)
{
   STATIC_ASSERT(VK_COMPARE_OP_NEVER == (VkCompareOp)MALI_FUNC_NEVER);
   STATIC_ASSERT(VK_COMPARE_OP_LESS == (VkCompareOp)MALI_FUNC_LESS);
   STATIC_ASSERT(VK_COMPARE_OP_EQUAL == (VkCompareOp)MALI_FUNC_EQUAL);
   STATIC_ASSERT(VK_COMPARE_OP_LESS_OR_EQUAL == (VkCompareOp)MALI_FUNC_LEQUAL);
   STATIC_ASSERT(VK_COMPARE_OP_GREATER == (VkCompareOp)MALI_FUNC_GREATER);
   STATIC_ASSERT(VK_COMPARE_OP_NOT_EQUAL == (VkCompareOp)MALI_FUNC_NOT_EQUAL);
   STATIC_ASSERT(VK_COMPARE_OP_GREATER_OR_EQUAL ==
                 (VkCompareOp)MALI_FUNC_GEQUAL);
   STATIC_ASSERT(VK_COMPARE_OP_ALWAYS == (VkCompareOp)MALI_FUNC_ALWAYS);

   return (enum mali_func)comp;
}

static enum mali_stencil_op
translate_stencil_op(VkStencilOp in)
{
   switch (in) {
   case VK_STENCIL_OP_KEEP:
      return MALI_STENCIL_OP_KEEP;
   case VK_STENCIL_OP_ZERO:
      return MALI_STENCIL_OP_ZERO;
   case VK_STENCIL_OP_REPLACE:
      return MALI_STENCIL_OP_REPLACE;
   case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
      return MALI_STENCIL_OP_INCR_SAT;
   case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
      return MALI_STENCIL_OP_DECR_SAT;
   case VK_STENCIL_OP_INCREMENT_AND_WRAP:
      return MALI_STENCIL_OP_INCR_WRAP;
   case VK_STENCIL_OP_DECREMENT_AND_WRAP:
      return MALI_STENCIL_OP_DECR_WRAP;
   case VK_STENCIL_OP_INVERT:
      return MALI_STENCIL_OP_INVERT;
   default:
      UNREACHABLE("Invalid stencil op");
   }
}

static enum mali_draw_mode
translate_prim_topology(VkPrimitiveTopology in)
{
   /* Test VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA separately, as it's not
    * part of the VkPrimitiveTopology enum.
    */
   if (in == VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA)
      return MALI_DRAW_MODE_TRIANGLES;

   switch (in) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return MALI_DRAW_MODE_POINTS;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return MALI_DRAW_MODE_LINES;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return MALI_DRAW_MODE_LINE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      return MALI_DRAW_MODE_TRIANGLES;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return MALI_DRAW_MODE_TRIANGLE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return MALI_DRAW_MODE_TRIANGLE_FAN;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
   default:
      UNREACHABLE("Invalid primitive type");
   }
}

static VkResult
update_tls(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_tls_state *state = &cmdbuf->state.tls;
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(cmdbuf->state.gfx.fs.shader);
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   if (!cmdbuf->state.gfx.tsd) {
      if (!state->desc.gpu) {
         state->desc = panvk_cmd_alloc_desc(cmdbuf, LOCAL_STORAGE);
         if (!state->desc.gpu)
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      }

      cmdbuf->state.gfx.tsd = state->desc.gpu;

      cs_update_vt_ctx(b) {
#if PAN_ARCH >= 12
         cs_move64_to(b, cs_sr_reg64(b, IDVS, VERTEX_TSD),
                      state->desc.gpu);
         cs_move64_to(b, cs_sr_reg64(b, IDVS, FRAGMENT_TSD),
                      state->desc.gpu);
#else
         cs_move64_to(b, cs_sr_reg64(b, IDVS, TSD_0), state->desc.gpu);
#endif
      }
   }

   state->info.tls.size =
      MAX3(vs->info.tls_size, fs ? fs->info.tls_size : 0, state->info.tls.size);
   return VK_SUCCESS;
}

static enum mali_index_type
index_size_to_index_type(uint32_t size)
{
   switch (size) {
   case 0:
      return MALI_INDEX_TYPE_NONE;
   case 1:
      return MALI_INDEX_TYPE_UINT8;
   case 2:
      return MALI_INDEX_TYPE_UINT16;
   case 4:
      return MALI_INDEX_TYPE_UINT32;
   default:
      assert(!"Invalid index size");
      return MALI_INDEX_TYPE_NONE;
   }
}

static VkResult
prepare_blend(struct panvk_cmd_buffer *cmdbuf)
{
   bool dirty = dyn_gfx_state_dirty(cmdbuf, MS_ALPHA_TO_ONE_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, CB_LOGIC_OP_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, CB_LOGIC_OP) ||
                dyn_gfx_state_dirty(cmdbuf, CB_ATTACHMENT_COUNT) ||
                dyn_gfx_state_dirty(cmdbuf, CB_COLOR_WRITE_ENABLES) ||
                dyn_gfx_state_dirty(cmdbuf, CB_BLEND_ENABLES) ||
                dyn_gfx_state_dirty(cmdbuf, CB_BLEND_EQUATIONS) ||
                dyn_gfx_state_dirty(cmdbuf, CB_WRITE_MASKS) ||
                dyn_gfx_state_dirty(cmdbuf, CB_BLEND_CONSTANTS) ||
                dyn_gfx_state_dirty(cmdbuf, COLOR_ATTACHMENT_MAP) ||
                fs_user_dirty(cmdbuf) || gfx_state_dirty(cmdbuf, RENDER_STATE);

   if (!dirty)
      return VK_SUCCESS;

   uint32_t bd_count = MAX2(cmdbuf->state.gfx.render.fb.info.rt_count, 1);
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   struct pan_ptr ptr = panvk_cmd_alloc_desc_array(cmdbuf, bd_count, BLEND);
   struct mali_blend_packed *bds = ptr.cpu;

   if (bd_count && !ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   panvk_per_arch(blend_emit_descs)(cmdbuf, bds);

   cs_update_vt_ctx(b)
      cs_move64_to(b, cs_sr_reg64(b, IDVS, BLEND_DESC), ptr.gpu | bd_count);

   return VK_SUCCESS;
}

#if PAN_ARCH >= 12
static void
prepare_vp(struct panvk_cmd_buffer *cmdbuf)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   const VkViewport *viewport =
      &cmdbuf->vk.dynamic_graphics_state.vp.viewports[0];
   const VkRect2D *scissor = &cmdbuf->vk.dynamic_graphics_state.vp.scissors[0];

   /* XXX: Switch scissor_array_enable to true and use array based variant
    * for future proofness */

   if (dyn_gfx_state_dirty(cmdbuf, VP_SCISSORS)) {
      struct mali_scissor_packed scissor_box;
      pan_pack(&scissor_box, SCISSOR, cfg) {
         assert(scissor->offset.x >= 0 && scissor->offset.y >= 0);

         int minx = scissor->offset.x;
         int miny = scissor->offset.y;
         int maxx = scissor->offset.x + scissor->extent.width;
         int maxy = scissor->offset.y + scissor->extent.height;

         /* Make sure we don't end up with a max < min when width/height is 0 */
         maxx = maxx > minx ? maxx - 1 : maxx;
         maxy = maxy > miny ? maxy - 1 : maxy;

         /* Clamp scissor to valid range */
         cfg.scissor_minimum_x = CLAMP(minx, 0, UINT16_MAX);
         cfg.scissor_minimum_y = CLAMP(miny, 0, UINT16_MAX);
         cfg.scissor_maximum_x = CLAMP(maxx, 0, UINT16_MAX);
         cfg.scissor_maximum_y = CLAMP(maxy, 0, UINT16_MAX);
      }

      struct mali_scissor_packed *scissor_box_ptr = &scissor_box;
      cs_move64_to(b, cs_sr_reg64(b, IDVS, SCISSOR_BOX),
                   *((uint64_t *)scissor_box_ptr));
   }

   if (dyn_gfx_state_dirty(cmdbuf, VP_VIEWPORTS) ||
       dyn_gfx_state_dirty(cmdbuf, VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE) ||
       dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLIP_ENABLE) ||
       dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE)) {
      struct mali_viewport_packed mali_viewport;
      pan_pack(&mali_viewport, VIEWPORT, cfg) {
         /* The spec says "width must be greater than 0.0" */
         assert(viewport->width >= 0);
         int minx = (int)viewport->x;
         int maxx = (int)(viewport->x + viewport->width);

         /* Viewport height can be negative */
         int miny =
            MIN2((int)viewport->y, (int)(viewport->y + viewport->height));
         int maxy =
            MAX2((int)viewport->y, (int)(viewport->y + viewport->height));

         /* Make sure we don't end up with a max < min when width/height is 0 */
         maxx = maxx > minx ? maxx - 1 : maxx;
         maxy = maxy > miny ? maxy - 1 : maxy;

         /* Clamp viewport to valid range */
         cfg.min_x = CLAMP(minx, 0, UINT16_MAX);
         cfg.min_y = CLAMP(miny, 0, UINT16_MAX);
         cfg.max_x = CLAMP(maxx, 0, UINT16_MAX);
         cfg.max_y = CLAMP(maxy, 0, UINT16_MAX);

         float z_min, z_max;
         panvk_depth_range(&cmdbuf->state.gfx,
                           &cmdbuf->vk.dynamic_graphics_state.vp, &z_min, &z_max);
         cfg.min_depth = CLAMP(z_min, 0.0f, 1.0f);
         cfg.max_depth = CLAMP(z_max, 0.0f, 1.0f);
      }

      uint64_t *mali_viewport_ptr = (uint64_t *)&mali_viewport;
      cs_move64_to(b, cs_sr_reg64(b, IDVS, VIEWPORT_HIGH),
                   mali_viewport_ptr[0]);
      cs_move64_to(b, cs_sr_reg64(b, IDVS, VIEWPORT_LOW),
                   mali_viewport_ptr[1]);
   }
}
#else
static void
prepare_vp(struct panvk_cmd_buffer *cmdbuf)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   const VkViewport *viewport =
      &cmdbuf->vk.dynamic_graphics_state.vp.viewports[0];
   const VkRect2D *scissor = &cmdbuf->vk.dynamic_graphics_state.vp.scissors[0];

   if (dyn_gfx_state_dirty(cmdbuf, VP_VIEWPORTS) ||
       dyn_gfx_state_dirty(cmdbuf, VP_SCISSORS)) {
      struct mali_scissor_packed scissor_box;
      pan_pack(&scissor_box, SCISSOR, cfg) {

         /* The spec says "width must be greater than 0.0" */
         assert(viewport->width >= 0);
         int minx = (int)viewport->x;
         int maxx = (int)(viewport->x + viewport->width);

         /* Viewport height can be negative */
         int miny =
            MIN2((int)viewport->y, (int)(viewport->y + viewport->height));
         int maxy =
            MAX2((int)viewport->y, (int)(viewport->y + viewport->height));

         assert(scissor->offset.x >= 0 && scissor->offset.y >= 0);
         minx = MAX2(scissor->offset.x, minx);
         miny = MAX2(scissor->offset.y, miny);
         maxx = MIN2(scissor->offset.x + scissor->extent.width, maxx);
         maxy = MIN2(scissor->offset.y + scissor->extent.height, maxy);

         /* Make sure we don't end up with a max < min when width/height is 0 */
         maxx = maxx > minx ? maxx - 1 : maxx;
         maxy = maxy > miny ? maxy - 1 : maxy;

         /* Clamp viewport scissor to valid range */
         cfg.scissor_minimum_x = CLAMP(minx, 0, UINT16_MAX);
         cfg.scissor_minimum_y = CLAMP(miny, 0, UINT16_MAX);
         cfg.scissor_maximum_x = CLAMP(maxx, 0, UINT16_MAX);
         cfg.scissor_maximum_y = CLAMP(maxy, 0, UINT16_MAX);
      }

      struct mali_scissor_packed *scissor_box_ptr = &scissor_box;
      cs_move64_to(b, cs_sr_reg64(b, IDVS, SCISSOR_BOX),
                   *((uint64_t *)scissor_box_ptr));
   }

   if (dyn_gfx_state_dirty(cmdbuf, VP_VIEWPORTS) ||
       dyn_gfx_state_dirty(cmdbuf, VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE) ||
       dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLIP_ENABLE) ||
       dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE)) {
      float z_min, z_max;
      panvk_depth_range(&cmdbuf->state.gfx,
                        &cmdbuf->vk.dynamic_graphics_state.vp, &z_min, &z_max);
      cs_move32_to(b, cs_sr_reg32(b, IDVS, LOW_DEPTH_CLAMP), fui(z_min));
      cs_move32_to(b, cs_sr_reg32(b, IDVS, HIGH_DEPTH_CLAMP), fui(z_max));
   }
}
#endif

#if PAN_ARCH >= 12
static inline uint64_t
get_vs_all_spd(const struct panvk_cmd_buffer *cmdbuf)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   assert(vs);
   const struct vk_input_assembly_state *ia =
      &cmdbuf->vk.dynamic_graphics_state.ia;
   return ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST
             ? panvk_priv_mem_dev_addr(vs->spds.all_points)
             : panvk_priv_mem_dev_addr(vs->spds.all_triangles);
}
#else
static inline uint64_t
get_vs_pos_spd(const struct panvk_cmd_buffer *cmdbuf)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   assert(vs);
   const struct vk_input_assembly_state *ia =
      &cmdbuf->vk.dynamic_graphics_state.ia;
   return ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST
             ? panvk_priv_mem_dev_addr(vs->spds.pos_points)
             : panvk_priv_mem_dev_addr(vs->spds.pos_triangles);
}
#endif

static void
prepare_tiler_primitive_size(struct panvk_cmd_buffer *cmdbuf)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   const struct vk_input_assembly_state *ia =
      &cmdbuf->vk.dynamic_graphics_state.ia;
   float primitive_size;

   if (!dyn_gfx_state_dirty(cmdbuf, IA_PRIMITIVE_TOPOLOGY) &&
       !dyn_gfx_state_dirty(cmdbuf, RS_LINE_WIDTH) &&
       !gfx_state_dirty(cmdbuf, VS))
      return;

   switch (ia->primitive_topology) {
   /* From the Vulkan spec 1.3.293:
    *
    *    "If maintenance5 is enabled and a value is not written to a variable
    *    decorated with PointSize, a value of 1.0 is used as the size of
    *    points."
    *
    * If no point size is written, ensure that the size is always 1.0f.
    * On v13+, the point size default to 1.0f.
    */
#if PAN_ARCH < 13
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: {
      const struct panvk_shader_variant *vs =
         panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);

      if (vs->info.vs.writes_point_size)
         return;

      primitive_size = 1.0f;
      break;
   }
#endif
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      primitive_size = cmdbuf->vk.dynamic_graphics_state.rs.line.width;
      break;
   default:
      return;
   }

#if PAN_ARCH >= 13
   cs_move32_to(b, cs_sr_reg32(b, IDVS, LINE_WIDTH),
                fui(primitive_size));
#else
   cs_move32_to(b, cs_sr_reg32(b, IDVS, PRIMITIVE_SIZE),
                fui(primitive_size));
#endif
}

static uint32_t
calc_enabled_layer_count(struct panvk_cmd_buffer *cmdbuf)
{
   return cmdbuf->state.gfx.render.view_mask ?
      util_bitcount(cmdbuf->state.gfx.render.view_mask) :
      cmdbuf->state.gfx.render.layer_count;
}

static uint32_t
calc_fbd_size(struct panvk_cmd_buffer *cmdbuf)
{
   const struct pan_fb_info *fb = &cmdbuf->state.gfx.render.fb.info;
   bool has_zs_ext = fb->zs.view.zs || fb->zs.view.s;
   uint32_t rt_count = MAX2(fb->rt_count, 1);

   return get_fbd_size(has_zs_ext, rt_count);
}

static uint32_t
calc_render_descs_size(struct panvk_cmd_buffer *cmdbuf)
{
   const uint32_t ir_scratch_fbd = 1;
   uint32_t fbd_count = calc_enabled_layer_count(cmdbuf) + ir_scratch_fbd;
   uint32_t td_count = DIV_ROUND_UP(cmdbuf->state.gfx.render.layer_count,
                                    MAX_LAYERS_PER_TILER_DESC);

   return (calc_fbd_size(cmdbuf) * fbd_count) +
          (td_count * pan_size(TILER_CONTEXT));
}

static void
cs_render_desc_ringbuf_reserve(struct cs_builder *b, uint32_t size)
{
   /* Make sure we don't allocate more than the ringbuf size. */
   assert(size <= RENDER_DESC_RINGBUF_SIZE);

   /* Make sure the allocation is 64-byte aligned. */
   assert(util_is_aligned(size, 64));

   struct cs_index ringbuf_sync = cs_scratch_reg64(b, 0);
   struct cs_index sz_reg = cs_scratch_reg32(b, 2);

   cs_load64_to(
      b, ringbuf_sync, cs_subqueue_ctx_reg(b),
      offsetof(struct panvk_cs_subqueue_context, render.desc_ringbuf.syncobj));

   /* Wait for the other end to release memory. */
   cs_move32_to(b, sz_reg, size - 1);
   cs_sync32_wait(b, false, MALI_CS_CONDITION_GREATER, sz_reg, ringbuf_sync);

   /* Decrement the syncobj to reflect the fact we're reserving memory. */
   cs_move32_to(b, sz_reg, -size);
   cs_sync32_add(b, false, MALI_CS_SYNC_SCOPE_CSG, sz_reg, ringbuf_sync,
                 cs_now());
}

static void
cs_render_desc_ringbuf_move_ptr(struct cs_builder *b, uint32_t size,
                                bool wrap_around)
{
   struct cs_index scratch_reg = cs_scratch_reg32(b, 0);
   struct cs_index ptr_lo = cs_scratch_reg32(b, 2);
   struct cs_index pos = cs_scratch_reg32(b, 4);

   cs_load_to(
      b, cs_scratch_reg_tuple(b, 2, 3), cs_subqueue_ctx_reg(b),
      BITFIELD_MASK(3),
      offsetof(struct panvk_cs_subqueue_context, render.desc_ringbuf.ptr));

   /* Update the relative position and absolute address. */
   cs_add32(b, ptr_lo, ptr_lo, size);
   cs_add32(b, pos, pos, size);

   /* Wrap-around. */
   if (likely(wrap_around)) {
      cs_add32(b, scratch_reg, pos, -RENDER_DESC_RINGBUF_SIZE);

      cs_if(b, MALI_CS_CONDITION_GEQUAL, scratch_reg) {
         cs_add32(b, ptr_lo, ptr_lo, -RENDER_DESC_RINGBUF_SIZE);
         cs_add32(b, pos, pos, -RENDER_DESC_RINGBUF_SIZE);
      }
   }

   cs_store(
      b, cs_scratch_reg_tuple(b, 2, 3), cs_subqueue_ctx_reg(b),
      BITFIELD_MASK(3),
      offsetof(struct panvk_cs_subqueue_context, render.desc_ringbuf.ptr));
   cs_flush_stores(b);
}

static bool get_first_provoking_vertex(struct panvk_cmd_buffer *cmdbuf);

static VkResult
get_tiler_desc(struct panvk_cmd_buffer *cmdbuf)
{
   assert(cmdbuf->state.gfx.render.invalidate_inherited_ctx ||
          !inherits_render_ctx(cmdbuf));

   if (cmdbuf->state.gfx.render.tiler)
      return VK_SUCCESS;

   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(cmdbuf->vk.base.device->physical);
   const bool tracing_enabled = PANVK_DEBUG(TRACE);
   struct pan_tiler_features tiler_features =
      pan_query_tiler_features(&phys_dev->kmod.props);
   bool simul_use =
      cmdbuf->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
   struct pan_ptr tiler_desc = {0};
   struct mali_tiler_context_packed tiler_tmpl;
   uint32_t td_count = DIV_ROUND_UP(cmdbuf->state.gfx.render.layer_count,
                                    MAX_LAYERS_PER_TILER_DESC);

   if (!simul_use) {
      tiler_desc = panvk_cmd_alloc_desc_array(cmdbuf, td_count, TILER_CONTEXT);
      if (!tiler_desc.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   const struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;

   /* At this point, we should know sample count and the tile size should have
    * been calculated */
   assert(fbinfo->nr_samples > 0 && fbinfo->tile_size > 0);

   pan_pack(&tiler_tmpl, TILER_CONTEXT, cfg) {
      unsigned max_levels = tiler_features.max_levels;
      assert(max_levels >= 2);

      /* The tiler chunk start with a header of 64 bytes */
      cfg.hierarchy_mask = panvk_select_tiler_hierarchy_mask(
         phys_dev, &cmdbuf->state.gfx, phys_dev->csf.tiler.chunk_size - 64);
      cfg.fb_width = fbinfo->width;
      cfg.fb_height = fbinfo->height;

#if PAN_ARCH >= 12
      cfg.effective_tile_size = fbinfo->tile_size;
#endif

      cfg.sample_pattern = pan_sample_pattern(fbinfo->nr_samples);

      cfg.first_provoking_vertex = get_first_provoking_vertex(cmdbuf);

      /* This will be overloaded. */
      cfg.layer_count = 1;
      cfg.layer_offset = 0;
   }

   /* When simul_use=true, the tiler descriptors are allocated from the
    * descriptor ringbuf. We set state.gfx.render.tiler to a non-NULL
    * value to satisfy the is_tiler_desc_allocated() tests, but we want
    * it to point to a faulty address so that we can easily detect if it's
    * used in the command stream/framebuffer descriptors. */
   cmdbuf->state.gfx.render.tiler =
      simul_use ? 0xdeadbeefdeadbeefull : tiler_desc.gpu;

   struct cs_index tiler_ctx_addr = cs_sr_reg64(b, IDVS, TILER_CTX);

   if (simul_use) {
      uint32_t descs_sz = calc_render_descs_size(cmdbuf);

      cs_render_desc_ringbuf_reserve(b, descs_sz);

      /* Reserve ringbuf mem. */
      cs_update_vt_ctx(b) {
         cs_load64_to(b, tiler_ctx_addr, cs_subqueue_ctx_reg(b),
                      offsetof(struct panvk_cs_subqueue_context,
                               render.desc_ringbuf.ptr));
      }

      cs_render_desc_ringbuf_move_ptr(b, descs_sz, !tracing_enabled);
   } else {
      cs_update_vt_ctx(b) {
         cs_move64_to(b, tiler_ctx_addr, tiler_desc.gpu);
      }
   }

   /* Reset the polygon list. */
   cs_move64_to(b, cs_scratch_reg64(b, 0), 0);

   /* Lay out words 2, 3 and 5, so they can be stored along the other updates.
    * Word 4 contains layer information and will be updated in the loop. */
   cs_move64_to(b, cs_scratch_reg64(b, 2),
                tiler_tmpl.opaque[2] | (uint64_t)tiler_tmpl.opaque[3] << 32);
   cs_move32_to(b, cs_scratch_reg32(b, 5), tiler_tmpl.opaque[5]);

   /* Load the tiler_heap and geom_buf from the context. */
   cs_load_to(b, cs_scratch_reg_tuple(b, 6, 4), cs_subqueue_ctx_reg(b),
              BITFIELD_MASK(4),
              offsetof(struct panvk_cs_subqueue_context, render.tiler_heap));

   /* If we don't know what provoking vertex mode the application wants yet,
    * leave space to patch it later */
   if (cmdbuf->state.gfx.render.first_provoking_vertex == U_TRISTATE_UNSET) {
      cs_maybe(b, &cmdbuf->state.gfx.render.maybe_set_tds_provoking_vertex)
         /* provoking_vertex flag is bit 18 of word 2 */
         cs_add32(b, cs_scratch_reg32(b, 2), cs_scratch_reg32(b, 2),
                  -(1 << 18));
   }

   /* Fill extra fields with zeroes so we can reset the completed
    * top/bottom and private states. */
   cs_move64_to(b, cs_scratch_reg64(b, 10), 0);
   cs_move64_to(b, cs_scratch_reg64(b, 12), 0);
   cs_move64_to(b, cs_scratch_reg64(b, 14), 0);

   /* Take care of the tiler desc with layer_offset=0 outside of the loop. */
   cs_move32_to(b, cs_scratch_reg32(b, 4),
                MIN2(cmdbuf->state.gfx.render.layer_count - 1,
                     MAX_LAYERS_PER_TILER_DESC - 1));

   /* Replace words 0:13 and 24:31. */
   cs_store(b, cs_scratch_reg_tuple(b, 0, 16), tiler_ctx_addr,
            BITFIELD_MASK(16), 0);
   cs_store(b, cs_scratch_reg_tuple(b, 0, 16), tiler_ctx_addr,
            BITFIELD_RANGE(0, 2) | BITFIELD_RANGE(10, 6), 64);
   cs_store(b, cs_scratch_reg_tuple(b, 0, 16), tiler_ctx_addr,
            BITFIELD_RANGE(0, 2) | BITFIELD_RANGE(10, 6), 96);

   uint32_t remaining_layers =
      td_count > 1
         ? cmdbuf->state.gfx.render.layer_count % MAX_LAYERS_PER_TILER_DESC
         : 0;
   uint32_t full_td_count =
      cmdbuf->state.gfx.render.layer_count / MAX_LAYERS_PER_TILER_DESC;

   if (remaining_layers) {
      int32_t layer_offset =
         -(cmdbuf->state.gfx.render.layer_count - remaining_layers) &
         BITFIELD_MASK(9);

      /* If the last tiler descriptor is not full, we emit it outside of the
       * loop to pass the right layer count. All this would be a lot simpler
       * if we had OR/AND instructions, but here we are. */
      cs_update_vt_ctx(b)
         cs_add64(b, tiler_ctx_addr, tiler_ctx_addr,
                  pan_size(TILER_CONTEXT) * full_td_count);
      cs_move32_to(b, cs_scratch_reg32(b, 4),
                   (layer_offset << 8) | (remaining_layers - 1));
      cs_store(b, cs_scratch_reg_tuple(b, 0, 16), tiler_ctx_addr,
               BITFIELD_MASK(16), 0);
      cs_store(b, cs_scratch_reg_tuple(b, 0, 16), tiler_ctx_addr,
               BITFIELD_RANGE(0, 2) | BITFIELD_RANGE(10, 6), 64);
      cs_store(b, cs_scratch_reg_tuple(b, 0, 16), tiler_ctx_addr,
               BITFIELD_RANGE(0, 2) | BITFIELD_RANGE(10, 6), 96);

      cs_update_vt_ctx(b)
         cs_add64(b, tiler_ctx_addr, tiler_ctx_addr,
                  -pan_size(TILER_CONTEXT));
   } else if (full_td_count > 1) {
      cs_update_vt_ctx(b)
         cs_add64(b, tiler_ctx_addr, tiler_ctx_addr,
                  pan_size(TILER_CONTEXT) * (full_td_count - 1));
   }

   if (full_td_count > 1) {
      struct cs_index counter_reg = cs_scratch_reg32(b, 17);
      uint32_t layer_offset =
         (-MAX_LAYERS_PER_TILER_DESC * (full_td_count - 1)) & BITFIELD_MASK(9);

      cs_move32_to(b, counter_reg, full_td_count - 1);
      cs_move32_to(b, cs_scratch_reg32(b, 4),
                   (layer_offset << 8) | (MAX_LAYERS_PER_TILER_DESC - 1));

      /* We iterate the remaining full tiler descriptors in reverse order, so we
       * can start from the smallest layer offset, and increment it by
       * MAX_LAYERS_PER_TILER_DESC << 8 at each iteration. Again, the split is
       * mostly due to the lack of AND instructions, and the fact layer_offset
       * is a 9-bit signed integer inside a 32-bit word, which ADD32 can't deal
       * with unless the number we add is positive.
       */
      cs_while(b, MALI_CS_CONDITION_GREATER, counter_reg) {
         /* Replace words 0:13 and 24:31. */
         cs_store(b, cs_scratch_reg_tuple(b, 0, 16), tiler_ctx_addr,
                  BITFIELD_MASK(16), 0);
         cs_store(b, cs_scratch_reg_tuple(b, 0, 16), tiler_ctx_addr,
                  BITFIELD_RANGE(0, 2) | BITFIELD_RANGE(10, 6), 64);
         cs_store(b, cs_scratch_reg_tuple(b, 0, 16), tiler_ctx_addr,
                  BITFIELD_RANGE(0, 2) | BITFIELD_RANGE(10, 6), 96);

         cs_add32(b, cs_scratch_reg32(b, 4), cs_scratch_reg32(b, 4),
                  MAX_LAYERS_PER_TILER_DESC << 8);

         cs_add32(b, counter_reg, counter_reg, -1);
         cs_update_vt_ctx(b)
            cs_add64(b, tiler_ctx_addr, tiler_ctx_addr,
                     -pan_size(TILER_CONTEXT));
      }
   }

   /* Flush all stores to tiler_ctx_addr. */
   cs_flush_stores(b);

   struct cs_index next_iter_sb_scratch = cs_scratch_reg_tuple(b, 0, 2);
   panvk_per_arch(cs_next_iter_sb)(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER,
                                   next_iter_sb_scratch);

   cs_heap_operation(b, MALI_CS_HEAP_OPERATION_VERTEX_TILER_STARTED, cs_now());
   return VK_SUCCESS;
}

static uint8_t
prepare_fb_desc(struct panvk_cmd_buffer *cmdbuf, struct pan_fb_info *fbinfo,
                uint32_t layer, void *fbd)
{
   struct pan_tiler_context tiler_ctx = {
      .valhall.layer_offset = layer - (layer % MAX_LAYERS_PER_TILER_DESC),
   };

   if (!(cmdbuf->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) &&
       (cmdbuf->state.gfx.render.tiler != 0)) {
      uint32_t td_idx = layer / MAX_LAYERS_PER_TILER_DESC;

      tiler_ctx.valhall.desc =
         cmdbuf->state.gfx.render.tiler + (td_idx * pan_size(TILER_CONTEXT));
   }

   return GENX(pan_emit_fbd)(fbinfo, layer, NULL, &tiler_ctx, fbd);
}

static VkResult
prepare_incremental_rendering_fbinfos(
   struct panvk_cmd_buffer *cmdbuf, const struct pan_fb_info *fbinfo,
   struct pan_fb_info ir_fbinfos[PANVK_IR_PASS_COUNT])
{
   /* First incremental rendering pass: don't discard result */

   struct pan_fb_info *ir_fb = &ir_fbinfos[PANVK_IR_FIRST_PASS];

   memcpy(ir_fb, fbinfo, sizeof(*ir_fb));
   for (unsigned i = 0; i < fbinfo->rt_count; i++)
      ir_fb->rts[i].discard = false;
   ir_fb->zs.discard.z = false;
   ir_fb->zs.discard.s = false;

   /* Subsequent incremental rendering passes: preload old content and don't
    * discard result */

   struct pan_fb_info *prev_ir_fb = ir_fb;
   ir_fb = &ir_fbinfos[PANVK_IR_MIDDLE_PASS];
   memcpy(ir_fb, prev_ir_fb, sizeof(*ir_fb));

   bool preload_changed = false;

   for (unsigned i = 0; i < fbinfo->rt_count; i++) {
      if (fbinfo->rts[i].view && !fbinfo->rts[i].preload) {
         ir_fb->rts[i].preload = true;
         preload_changed = true;
      }

      if (ir_fb->rts[i].clear) {
         ir_fb->rts[i].clear = false;
         preload_changed = true;
      }
   }
   if (fbinfo->zs.view.zs && !fbinfo->zs.preload.z && !fbinfo->zs.preload.s) {
      ir_fb->zs.preload.z = true;
      ir_fb->zs.preload.s = true;
      preload_changed = true;
   } else if (fbinfo->zs.view.s && !fbinfo->zs.preload.s) {
      ir_fb->zs.preload.s = true;
      preload_changed = true;
   }

   if (ir_fb->zs.clear.z || ir_fb->zs.clear.s) {
      ir_fb->zs.clear.z = false;
      ir_fb->zs.clear.s = false;
      preload_changed = true;
   }

   if (preload_changed) {
      memset(&ir_fb->bifrost.pre_post.dcds, 0x0,
             sizeof(ir_fb->bifrost.pre_post.dcds));
      VkResult result = panvk_per_arch(cmd_fb_preload)(cmdbuf, ir_fb);
      if (result != VK_SUCCESS)
         return result;
   }

   /* Last incremental rendering pass: preload previous content and deal with
    * results as specified by user */

   prev_ir_fb = ir_fb;
   ir_fb = &ir_fbinfos[PANVK_IR_LAST_PASS];
   memcpy(ir_fb, prev_ir_fb, sizeof(*ir_fb));

   for (unsigned i = 0; i < fbinfo->rt_count; i++)
      ir_fb->rts[i].discard = fbinfo->rts[i].discard;
   ir_fb->zs.discard.z = fbinfo->zs.discard.z;
   ir_fb->zs.discard.s = fbinfo->zs.discard.s;

   return VK_SUCCESS;
}

static VkResult
get_fb_descs(struct panvk_cmd_buffer *cmdbuf)
{
   assert(cmdbuf->state.gfx.render.invalidate_inherited_ctx ||
          !inherits_render_ctx(cmdbuf));

   if (cmdbuf->state.gfx.render.fbds.gpu ||
       !cmdbuf->state.gfx.render.layer_count)
      return VK_SUCCESS;

   const uint32_t ir_scratch_fbd = 1;
   uint32_t fbd_sz = calc_fbd_size(cmdbuf);
   uint32_t fbds_sz =
      (calc_enabled_layer_count(cmdbuf) + ir_scratch_fbd) * fbd_sz;

   cmdbuf->state.gfx.render.fbds = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, fbds_sz, pan_alignment(FRAMEBUFFER));
   if (!cmdbuf->state.gfx.render.fbds.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;

   /* At this point, we should know sample count and the tile size should have
    * been calculated */
   assert(fbinfo->nr_samples > 0 && fbinfo->tile_size > 0);

   bool simul_use =
      cmdbuf->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

   /* The only bit we patch in FBDs is the tiler pointer. If tiler is not
    * involved (clear job) or if the update can happen in place (not
    * simultaneous use of the command buffer), we can avoid the
    * copy.
    *
    * According to VUID-VkSubmitInfo2KHR-commandBuffer-06192 and
    * VUID-VkSubmitInfo2KHR-commandBuffer-06010, suspend/resume operations
    * can't cross the vkQueueSubmit2() boundary, so no need to dynamically
    * allocate descriptors in that case:
    * "
    *   If any commandBuffer member of an element of pCommandBufferInfos
    *   contains any suspended render pass instances, they must be resumed by a
    *   render pass instance later in submission order within
    *   pCommandBufferInfos.
    *
    *   If any commandBuffer member of an element of pCommandBufferInfos
    *   contains any resumed render pass instances, they must be suspended by a
    *   render pass instance earlier in submission order within
    *   pCommandBufferInfos.
    * "
    */
   bool copy_fbds = simul_use && cmdbuf->state.gfx.render.tiler;
   struct pan_ptr fbds = cmdbuf->state.gfx.render.fbds;
   uint32_t fbd_flags = 0;

   fbinfo->sample_positions =
      dev->sample_positions->addr.dev +
      pan_sample_positions_offset(pan_sample_pattern(fbinfo->nr_samples));
   fbinfo->first_provoking_vertex = get_first_provoking_vertex(cmdbuf);

   VkResult result = panvk_per_arch(cmd_fb_preload)(cmdbuf, fbinfo);
   if (result != VK_SUCCESS)
      return result;

   struct pan_fb_info ir_fbinfos[PANVK_IR_PASS_COUNT];
   result = prepare_incremental_rendering_fbinfos(cmdbuf, fbinfo, ir_fbinfos);
   if (result != VK_SUCCESS)
      return result;

   /* We prepare all FB descriptors upfront. For multiview, only create FBDs
    * for enabled views. */
   uint32_t view_mask_temp = cmdbuf->state.gfx.render.view_mask;
   uint32_t enabled_layer_count = calc_enabled_layer_count(cmdbuf);
   bool multiview = cmdbuf->state.gfx.render.view_mask;

   for (uint32_t i = 0; i < enabled_layer_count; i++) {
      uint32_t layer_idx = multiview ? u_bit_scan(&view_mask_temp) : i;

      uint32_t layer_offset = fbd_sz * i;
      uint32_t new_fbd_flags =
         prepare_fb_desc(cmdbuf, fbinfo, layer_idx, fbds.cpu + layer_offset);

      /* Make sure all FBDs have the same flags. */
      assert(i == 0 || new_fbd_flags == fbd_flags);
      fbd_flags = new_fbd_flags;
   }

   const bool has_zs_ext = fbinfo->zs.view.zs || fbinfo->zs.view.s;
   const uint32_t rt_count = MAX2(fbinfo->rt_count, 1);

   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_FRAGMENT);
   for (uint32_t ir_pass = 0; ir_pass < PANVK_IR_PASS_COUNT; ir_pass++) {
      /* We use the scratch FBD to initialize our IR pass data, then copy
       * only IR relevant FBD sections to the subqueue context.
       */
      void *scratch_fbd_init_memory = fbds.cpu + (fbd_sz * enabled_layer_count);

      const uint64_t ir_pass_info_offset =
         TILER_OOM_CTX_FIELD_OFFSET(ir_desc_infos) +
         ir_pass * sizeof(struct panvk_ir_desc_info);

      /* Construct our temporary full IR FBD */
      uint32_t new_fbd_flags = prepare_fb_desc(cmdbuf, &ir_fbinfos[ir_pass], 0,
                                               scratch_fbd_init_memory);
      /* Make sure all FBDs have the same flags. */
      assert(new_fbd_flags == fbd_flags);

      {
         struct mali_framebuffer_packed *scratch_fbd = scratch_fbd_init_memory;

         /* Copy IR FBD data word0, dword6 and word12 */
         struct cs_index fbd_registers = cs_scratch_reg_tuple(b, 0, 4);
         cs_move32_to(b, cs_scratch_reg32(b, 0), scratch_fbd->opaque[0]);
         cs_move32_to(b, cs_scratch_reg32(b, 1), scratch_fbd->opaque[6]);
         cs_move32_to(b, cs_scratch_reg32(b, 2), scratch_fbd->opaque[7]);
         cs_move32_to(b, cs_scratch_reg32(b, 3), scratch_fbd->opaque[12]);
         cs_store(
            b, fbd_registers, cs_subqueue_ctx_reg(b), BITFIELD_MASK(4),
            ir_pass_info_offset + offsetof(struct panvk_ir_desc_info, fbd));

         /* Move past base FBD */
         scratch_fbd_init_memory += pan_size(FRAMEBUFFER);
      }

      /* Copy IR DBD word0 if present */
      if (has_zs_ext) {
         struct mali_zs_crc_extension_packed *scratch_zs_crc = scratch_fbd_init_memory;

         struct cs_index crc_zs_ext_reg = cs_scratch_reg32(b, 4);

         cs_move32_to(b, crc_zs_ext_reg, scratch_zs_crc->opaque[0]);
         cs_store32(b, crc_zs_ext_reg, cs_subqueue_ctx_reg(b),
                    ir_pass_info_offset +
                       offsetof(struct panvk_ir_desc_info, crc_zs_word0));

         /* Move past crc_zs_ext */
         scratch_fbd_init_memory += pan_size(ZS_CRC_EXTENSION);
      }

      {
         /* Assume we have sufficient scratch to avoid wait */
         assert(rt_count + 5 < CS_REG_SCRATCH_COUNT);

         /* Copy IR RTD word1 */
         for (uint32_t rt = 0; rt < rt_count; rt++) {
            struct mali_render_target_packed *scratch_rtd = scratch_fbd_init_memory;
            struct cs_index rt_reg = cs_scratch_reg32(b, 5 + rt);

            const uint64_t ir_rt_info_offset =
               offsetof(struct panvk_ir_desc_info, rtd_word1) +
               rt * sizeof(uint32_t);

            cs_move32_to(b, rt_reg, scratch_rtd->opaque[1]);
            cs_store32(b, rt_reg, cs_subqueue_ctx_reg(b),
                       ir_pass_info_offset + ir_rt_info_offset);

            /* Move past current RT */
            scratch_fbd_init_memory += pan_size(RENDER_TARGET);
         }
      }

   }

   /* Wait for ir pass info to complete */
   cs_wait_slot(b, SB_ID(LS));

   bool unset_provoking_vertex =
      cmdbuf->state.gfx.render.first_provoking_vertex == U_TRISTATE_UNSET;

   if (copy_fbds) {
      struct cs_index cur_tiler = cs_reg64(b, 38);
      struct cs_index dst_fbd_ptr = cs_sr_reg64(b, FRAGMENT, FBD_POINTER);
      struct cs_index fbd_idx = cs_reg32(b, 47);
      struct cs_index src_fbd_ptr = cs_reg64(b, 48);
      struct cs_index remaining_layers_in_td = cs_reg32(b, 50);
      uint32_t td_count = DIV_ROUND_UP(cmdbuf->state.gfx.render.layer_count,
                                       MAX_LAYERS_PER_TILER_DESC);

      cs_update_frag_ctx(b) {
         cs_load64_to(b, cur_tiler, cs_subqueue_ctx_reg(b),
                      offsetof(struct panvk_cs_subqueue_context,
                               render.desc_ringbuf.ptr));
         cs_add64(b, dst_fbd_ptr, cur_tiler,
                  pan_size(TILER_CONTEXT) * td_count);
      }

      cs_move64_to(b, src_fbd_ptr, fbds.gpu);

      /* Copy FBDs for layer regular pass */
      cs_move32_to(b, remaining_layers_in_td, MAX_LAYERS_PER_TILER_DESC);
      cs_move32_to(b, fbd_idx, enabled_layer_count);
      cs_while(b, MALI_CS_CONDITION_GREATER, fbd_idx) {
         cs_add32(b, fbd_idx, fbd_idx, -1);

         /* Our loop is copying 64-bytes at a time, so make sure the
          * framebuffer size is aligned on 64-bytes. */
         assert(fbd_sz == ALIGN_POT(fbd_sz, 64));

         for (uint32_t fbd_off = 0; fbd_off < fbd_sz; fbd_off += 64) {
            if (fbd_off == 0) {
               cs_load_to(b, cs_scratch_reg_tuple(b, 0, 14), src_fbd_ptr,
                          BITFIELD_MASK(14), fbd_off);
               cs_add64(b, cs_scratch_reg64(b, 14), cur_tiler, 0);

               /* If we don't know what provoking vertex mode the
                * application wants yet, leave space to patch it later. */
               if (unset_provoking_vertex) {
                  /* Provoking_vertex flag is bit 14 of word 11 */
                  struct cs_index word = cs_scratch_reg32(b, 11);
                  cs_maybe(
                     b,
                     &cmdbuf->state.gfx.render.maybe_set_fbds_provoking_vertex)
                     cs_add32(b, word, word, -(1 << 14));
               }
            } else {
               cs_load_to(b, cs_scratch_reg_tuple(b, 0, 16), src_fbd_ptr,
                          BITFIELD_MASK(16), fbd_off);
            }
            cs_store(b, cs_scratch_reg_tuple(b, 0, 16), dst_fbd_ptr,
                     BITFIELD_MASK(16), fbd_off);
         }

         /* Finish stores to pass_dst_fbd_ptr. */
         cs_flush_stores(b);

         cs_add64(b, src_fbd_ptr, src_fbd_ptr, fbd_sz);
         cs_update_frag_ctx(b)
            cs_add64(b, dst_fbd_ptr, dst_fbd_ptr, fbd_sz);

         cs_add32(b, remaining_layers_in_td, remaining_layers_in_td, -1);
         cs_if(b, MALI_CS_CONDITION_LEQUAL, remaining_layers_in_td) {
            cs_update_frag_ctx(b)
               cs_add64(b, cur_tiler, cur_tiler, pan_size(TILER_CONTEXT));
            cs_move32_to(b, remaining_layers_in_td,
                         MAX_LAYERS_PER_TILER_DESC);
         }
      }

      cs_update_frag_ctx(b) {
         uint32_t full_td_count =
            cmdbuf->state.gfx.render.layer_count / MAX_LAYERS_PER_TILER_DESC;

         /* If the last tiler descriptor is not full, cur_tiler points to the
          * last tiler descriptor, not the FBD that follows. */
         if (full_td_count < td_count)
            cs_add64(b, dst_fbd_ptr, cur_tiler,
                     fbd_flags + pan_size(TILER_CONTEXT));
         else
            cs_add64(b, dst_fbd_ptr, cur_tiler, fbd_flags);

         cs_add64(b, cur_tiler, cur_tiler,
                  -(full_td_count * pan_size(TILER_CONTEXT)));
      }
   } else {
      cs_update_frag_ctx(b) {
         cs_move64_to(b, cs_sr_reg64(b, FRAGMENT, FBD_POINTER),
                      fbds.gpu | fbd_flags);
         cs_move64_to(b, cs_reg64(b, 38), cmdbuf->state.gfx.render.tiler);
      }

      /* If we don't know what provoking vertex mode the application wants yet,
       * leave space to patch it later */
      if (cmdbuf->state.gfx.render.first_provoking_vertex == U_TRISTATE_UNSET) {
         const uint32_t ir_scratch_fbd = 1;
         uint32_t fbd_count = calc_enabled_layer_count(cmdbuf) + ir_scratch_fbd;
         /* passed to fn_set_fbds_provoking_vertex */
         struct cs_index fbd_count_reg = cs_scratch_reg32(b, 0);
         cs_move32_to(b, fbd_count_reg, fbd_count);

         struct cs_index length_reg = cs_scratch_reg32(b, 1);
         struct cs_index addr_reg = cs_scratch_reg64(b, 2);
         uint32_t fn_idx = calc_fn_set_fbds_provoking_vertex_idx(cmdbuf);
         uint32_t fn_stride =
            dev->draw_ctx->fn_set_fbds_provoking_vertex_stride;
         uint32_t fn_addr =
            dev->draw_ctx->fns_bo->addr.dev + fn_idx * fn_stride;
         cs_move64_to(b, addr_reg, fn_addr);
         cs_move32_to(b, length_reg, fn_stride);

         cs_maybe(b, &cmdbuf->state.gfx.render.maybe_set_fbds_provoking_vertex)
            cs_call(b, addr_reg, length_reg);
      }
   }

   return VK_SUCCESS;
}

static bool
get_first_provoking_vertex(struct panvk_cmd_buffer *cmdbuf)
{
   switch (cmdbuf->state.gfx.render.first_provoking_vertex) {
      case U_TRISTATE_NO:
         return false;
      case U_TRISTATE_YES:
         return true;
      /* If we don't know the provoking vertex mode yet, guess that it will
       * be PROVOKING_VERTEX_MODE_FIRST. This is the vulkan default, and so
       * likely to be right more often. */
      case U_TRISTATE_UNSET:
         return true;
      default:
         UNREACHABLE("Invalid u_tristate");
   }
}

static void
set_provoking_vertex_mode(struct panvk_cmd_buffer *cmdbuf,
                          enum u_tristate first_provoking_vertex)
{
   struct panvk_cmd_graphics_state *state = &cmdbuf->state.gfx;

   if (first_provoking_vertex != U_TRISTATE_UNSET) {
      /* If this is not the first draw, first_provoking_vertex should match
       * the one from the previous draws. Unfortunately, we can't check it
       * when the render pass is inherited. */
      assert(state->render.first_provoking_vertex == U_TRISTATE_UNSET ||
             state->render.first_provoking_vertex == first_provoking_vertex);
      state->render.first_provoking_vertex = first_provoking_vertex;
   }

   /* If the application uses PROVOKING_VERTEX_MODE_LAST after we previously
    * emitted FBDs/TDs with the wrong mode set, patch the CS to flip the
    * provoking vertex mode bits. */
   if (state->render.first_provoking_vertex == U_TRISTATE_NO &&
       state->render.maybe_set_tds_provoking_vertex)
   {
      struct cs_builder *b =
         panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
      cs_patch_maybe(b, state->render.maybe_set_tds_provoking_vertex);
      state->render.maybe_set_tds_provoking_vertex = NULL;
   }
   if (state->render.first_provoking_vertex == U_TRISTATE_NO &&
       state->render.maybe_set_fbds_provoking_vertex) {
      struct cs_builder *b =
         panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_FRAGMENT);
      cs_patch_maybe(b, state->render.maybe_set_fbds_provoking_vertex);
      state->render.maybe_set_fbds_provoking_vertex = NULL;
   }
}

static VkResult
get_render_ctx(struct panvk_cmd_buffer *cmdbuf)
{
   panvk_per_arch(cmd_select_tile_size)(cmdbuf);

   VkResult result = get_tiler_desc(cmdbuf);
   if (result != VK_SUCCESS)
      return result;

   return get_fb_descs(cmdbuf);
}

static VkResult
prepare_vs(struct panvk_cmd_buffer *cmdbuf, const struct panvk_draw_info *draw)
{
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   struct panvk_shader_desc_state *vs_desc_state = &cmdbuf->state.gfx.vs.desc;
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   bool upd_res_table = false;

   VkResult result = prepare_vs_driver_set(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return result;

   if (gfx_state_dirty(cmdbuf, VS) || gfx_state_dirty(cmdbuf, DESC_STATE) ||
       vs_driver_set_is_dirty(cmdbuf)) {
      uint32_t repeat_count = 1;

      if (draw->indirect.draw_count > 1 &&
          cmdbuf->state.gfx.vi.attribs_changing_on_base_instance != 0)
         repeat_count = draw->indirect.draw_count;

      result = panvk_per_arch(cmd_prepare_shader_res_table)(
         cmdbuf, desc_state, vs, vs_desc_state, repeat_count);
      if (result != VK_SUCCESS)
         return result;

      upd_res_table = true;
   }

   cs_update_vt_ctx(b) {
      if (upd_res_table)
         cs_move64_to(b, cs_sr_reg64(b, IDVS, VERTEX_SRT),
                      vs_desc_state->res_table);

#if PAN_ARCH >= 12
      if (gfx_state_dirty(cmdbuf, VS))
         cs_move64_to(b, cs_sr_reg64(b, IDVS, VERTEX_SPD), get_vs_all_spd(cmdbuf));
#else
      if (gfx_state_dirty(cmdbuf, VS) ||
          dyn_gfx_state_dirty(cmdbuf, IA_PRIMITIVE_TOPOLOGY))
         cs_move64_to(b, cs_sr_reg64(b, IDVS, VERTEX_POS_SPD),
                      get_vs_pos_spd(cmdbuf));

      if (gfx_state_dirty(cmdbuf, VS))
         cs_move64_to(b, cs_sr_reg64(b, IDVS, VERTEX_VARY_SPD),
                      panvk_priv_mem_dev_addr(vs->spds.var));
#endif
   }

   return VK_SUCCESS;
}

static VkResult
prepare_fs(struct panvk_cmd_buffer *cmdbuf)
{
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   if (fs &&
       (gfx_state_dirty(cmdbuf, FS) || gfx_state_dirty(cmdbuf, DESC_STATE))) {
      VkResult result = prepare_fs_driver_set(cmdbuf);
      if (result != VK_SUCCESS)
         return result;

      result = panvk_per_arch(cmd_prepare_shader_res_table)(
         cmdbuf, desc_state, fs, fs_desc_state, 1);
      if (result != VK_SUCCESS)
         return result;
   }

   cs_update_vt_ctx(b) {
      if (fs_user_dirty(cmdbuf) || gfx_state_dirty(cmdbuf, DESC_STATE))
         cs_move64_to(b, cs_sr_reg64(b, IDVS, FRAGMENT_SRT),
                      fs ? fs_desc_state->res_table : 0);
      if (fs_user_dirty(cmdbuf))
         cs_move64_to(b, cs_sr_reg64(b, IDVS, FRAGMENT_SPD),
                      fs ? panvk_priv_mem_dev_addr(fs->spd) : 0);
   }

   return VK_SUCCESS;
}

static VkResult
prepare_push_uniforms(struct panvk_cmd_buffer *cmdbuf,
                      const struct panvk_draw_info *draw)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   VkResult result;

   if (gfx_state_dirty(cmdbuf, VS_PUSH_UNIFORMS)) {
      uint32_t repeat_count = 1;

      if (draw->indirect.draw_count > 1 &&
          (shader_uses_sysval(vs, graphics, vs.first_vertex) ||
           shader_uses_sysval(vs, graphics, vs.base_instance)))
         repeat_count = draw->indirect.draw_count;

      result =
         panvk_per_arch(cmd_prepare_push_uniforms)(cmdbuf, vs, repeat_count);
      if (result != VK_SUCCESS)
         return result;

      cs_update_vt_ctx(b) {
         cs_move64_to(b, cs_sr_reg64(b, IDVS, VERTEX_FAU),
                      cmdbuf->state.gfx.vs.push_uniforms |
                         ((uint64_t)vs->fau.total_count << 56));
      }
   }

   if (fs_user_dirty(cmdbuf) || gfx_state_dirty(cmdbuf, FS_PUSH_UNIFORMS)) {
      uint64_t fau_ptr = 0;

      if (fs) {
         result = panvk_per_arch(cmd_prepare_push_uniforms)(cmdbuf, fs, 1);
         if (result != VK_SUCCESS)
            return result;

         fau_ptr = cmdbuf->state.gfx.fs.push_uniforms |
                   ((uint64_t)fs->fau.total_count << 56);
      }

      cs_update_vt_ctx(b)
         cs_move64_to(b, cs_sr_reg64(b, IDVS, FRAGMENT_FAU), fau_ptr);
   }

   return VK_SUCCESS;
}

static VkResult
prepare_ds(struct panvk_cmd_buffer *cmdbuf, struct pan_earlyzs_state earlyzs)
{
   bool dirty = dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_TEST_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_WRITE_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_COMPARE_OP) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_TEST_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_OP) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_COMPARE_MASK) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_WRITE_MASK) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_REFERENCE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLIP_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_BIAS_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_BIAS_FACTORS) ||
                dyn_gfx_state_dirty(cmdbuf, MS_ALPHA_TO_COVERAGE_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, INPUT_ATTACHMENT_MAP) ||
                fs_user_dirty(cmdbuf) || gfx_state_dirty(cmdbuf, OQ);

   if (!dirty)
      return VK_SUCCESS;

   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_depth_stencil_state *ds = &dyns->ds;
   const struct vk_rasterization_state *rs = &dyns->rs;
   bool test_s = has_stencil_att(cmdbuf) && ds->stencil.test_enable;
   bool test_z = has_depth_att(cmdbuf) && ds->depth.test_enable;
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));

   struct pan_ptr zsd = panvk_cmd_alloc_desc(cmdbuf, DEPTH_STENCIL);
   if (!zsd.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pan_cast_and_pack(zsd.cpu, DEPTH_STENCIL, cfg) {
      cfg.stencil_test_enable = test_s;
      if (test_s) {
         cfg.front_compare_function =
            translate_compare_func(ds->stencil.front.op.compare);
         cfg.front_stencil_fail =
            translate_stencil_op(ds->stencil.front.op.fail);
         cfg.front_depth_fail =
            translate_stencil_op(ds->stencil.front.op.depth_fail);
         cfg.front_depth_pass = translate_stencil_op(ds->stencil.front.op.pass);
         cfg.back_compare_function =
            translate_compare_func(ds->stencil.back.op.compare);
         cfg.back_stencil_fail = translate_stencil_op(ds->stencil.back.op.fail);
         cfg.back_depth_fail =
            translate_stencil_op(ds->stencil.back.op.depth_fail);
         cfg.back_depth_pass = translate_stencil_op(ds->stencil.back.op.pass);
      }

      cfg.stencil_from_shader = fs ? fs->info.fs.writes_stencil : 0;
      cfg.front_write_mask = ds->stencil.front.write_mask;
      cfg.back_write_mask = ds->stencil.back.write_mask;
      cfg.front_value_mask = ds->stencil.front.compare_mask;
      cfg.back_value_mask = ds->stencil.back.compare_mask;
      cfg.front_reference_value = ds->stencil.front.reference;
      cfg.back_reference_value = ds->stencil.back.reference;

      cfg.depth_cull_enable = vk_rasterization_state_depth_clip_enable(rs);
      if (rs->depth_clamp_enable)
         cfg.depth_clamp_mode = MALI_DEPTH_CLAMP_MODE_BOUNDS;

      if (fs) {
#if PAN_ARCH == 10
         cfg.shader_read_only_z_s = earlyzs.shader_readonly_zs;
#elif PAN_ARCH == 11
         cfg.separated_dependency_tracking = true;
#endif
         cfg.depth_source = pan_depth_source(&fs->info);
      }

      cfg.depth_write_enable = test_z && ds->depth.write_enable;
      cfg.depth_bias_enable = rs->depth_bias.enable;
      cfg.depth_function = test_z ? translate_compare_func(ds->depth.compare_op)
                                  : MALI_FUNC_ALWAYS;
      cfg.depth_units = rs->depth_bias.constant_factor;
      cfg.depth_factor = rs->depth_bias.slope_factor;
      cfg.depth_bias_clamp = rs->depth_bias.clamp;
   }

   cs_update_vt_ctx(b)
      cs_move64_to(b, cs_sr_reg64(b, IDVS, ZSD), zsd.gpu);

   return VK_SUCCESS;
}

static VkResult
wrap_prev_oq(struct panvk_cmd_buffer *cmdbuf)
{
   uint64_t last_syncobj = cmdbuf->state.gfx.render.oq.last;

   if (!last_syncobj)
      return VK_SUCCESS;

   /* We need to signal n_views consecutive queries for multiview. */
   const uint32_t n_views =
      MAX2(1, util_bitcount(cmdbuf->state.gfx.render.view_mask));

   for (uint32_t view_idx = 0; view_idx < n_views; ++view_idx) {
      struct pan_ptr new_oq_node = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, sizeof(struct panvk_cs_occlusion_query), 8);


      if (!new_oq_node.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      cmdbuf->state.gfx.render.oq.chain = new_oq_node.gpu;

      struct panvk_cs_occlusion_query *oq = new_oq_node.cpu;

      *oq = (struct panvk_cs_occlusion_query){
         .node = {.next = 0},
         .syncobj = last_syncobj + view_idx * sizeof(struct panvk_query_available_obj),
      };

      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_FRAGMENT);
      struct cs_index new_node_ptr = cs_scratch_reg64(b, 0);
      cs_move64_to(b, new_node_ptr, new_oq_node.gpu);
      cs_single_link_list_add_tail(
         b, cs_subqueue_ctx_reg(b),
         offsetof(struct panvk_cs_subqueue_context, render.oq_chain), new_node_ptr,
         offsetof(struct panvk_cs_occlusion_query, node),
         cs_scratch_reg_tuple(b, 10, 4));
   }

   return VK_SUCCESS;
}

static VkResult
prepare_oq(struct panvk_cmd_buffer *cmdbuf)
{
   if (!gfx_state_dirty(cmdbuf, OQ) ||
       cmdbuf->state.gfx.occlusion_query.mode == MALI_OCCLUSION_MODE_DISABLED ||
       cmdbuf->state.gfx.occlusion_query.syncobj ==
          cmdbuf->state.gfx.render.oq.last)
      return VK_SUCCESS;

   VkResult result = wrap_prev_oq(cmdbuf);
   if (result)
      return result;

   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   cs_move64_to(b, cs_sr_reg64(b, IDVS, OQ),
                cmdbuf->state.gfx.occlusion_query.ptr);

   cmdbuf->state.gfx.render.oq.last =
      cmdbuf->state.gfx.occlusion_query.syncobj;
   return VK_SUCCESS;
}

static void
prepare_dcd(struct panvk_cmd_buffer *cmdbuf,
            struct pan_earlyzs_state *earlyzs)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   bool dcd2_dirty =
      fs_user_dirty(cmdbuf) ||
      dyn_gfx_state_dirty(cmdbuf, INPUT_ATTACHMENT_MAP) ||
      dyn_gfx_state_dirty(cmdbuf, COLOR_ATTACHMENT_MAP);
   bool dcd0_dirty =
      dyn_gfx_state_dirty(cmdbuf, RS_RASTERIZER_DISCARD_ENABLE) ||
      dyn_gfx_state_dirty(cmdbuf, RS_CULL_MODE) ||
      dyn_gfx_state_dirty(cmdbuf, RS_LINE_MODE) ||
      dyn_gfx_state_dirty(cmdbuf, RS_FRONT_FACE) ||
      dyn_gfx_state_dirty(cmdbuf, MS_RASTERIZATION_SAMPLES) ||
      dyn_gfx_state_dirty(cmdbuf, MS_SAMPLE_MASK) ||
      dyn_gfx_state_dirty(cmdbuf, MS_ALPHA_TO_COVERAGE_ENABLE) ||
      dyn_gfx_state_dirty(cmdbuf, MS_ALPHA_TO_ONE_ENABLE) ||
      /* writes_depth() uses vk_depth_stencil_state */
      dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_TEST_ENABLE) ||
      dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_WRITE_ENABLE) ||
      dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_COMPARE_OP) ||
      /* writes_stencil() uses vk_depth_stencil_state */
      dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_TEST_ENABLE) ||
      dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_OP) ||
      dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_WRITE_MASK) ||
      /* fpk enablement depends on vk_color_blend_attachment_state */
      dyn_gfx_state_dirty(cmdbuf, CB_BLEND_ENABLES) ||
      dyn_gfx_state_dirty(cmdbuf, CB_BLEND_EQUATIONS) ||
      dyn_gfx_state_dirty(cmdbuf, CB_WRITE_MASKS) ||
      /* line mode needs primitive topology */
      dyn_gfx_state_dirty(cmdbuf, IA_PRIMITIVE_TOPOLOGY) ||
      dyn_gfx_state_dirty(cmdbuf, INPUT_ATTACHMENT_MAP) ||
      fs_user_dirty(cmdbuf) || gfx_state_dirty(cmdbuf, RENDER_STATE) ||
      gfx_state_dirty(cmdbuf, OQ) || dcd2_dirty;
   bool dcd1_dirty = dyn_gfx_state_dirty(cmdbuf, MS_RASTERIZATION_SAMPLES) ||
                     dyn_gfx_state_dirty(cmdbuf, MS_SAMPLE_MASK) ||
                     /* line mode needs primitive topology */
                     dyn_gfx_state_dirty(cmdbuf, RS_LINE_MODE) ||
                     dyn_gfx_state_dirty(cmdbuf, IA_PRIMITIVE_TOPOLOGY) ||
                     fs_user_dirty(cmdbuf) ||
                     gfx_state_dirty(cmdbuf, RENDER_STATE);

   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_rasterization_state *rs =
      &cmdbuf->vk.dynamic_graphics_state.rs;
   const struct vk_input_assembly_state *ia =
      &cmdbuf->vk.dynamic_graphics_state.ia;

   bool alpha_to_coverage = dyns->ms.alpha_to_coverage_enable;
   bool writes_z = writes_depth(cmdbuf);
   bool writes_s = writes_stencil(cmdbuf);
   uint8_t rt_mask = cmdbuf->state.gfx.render.bound_attachments &
                     MESA_VK_RP_ATTACHMENT_ANY_COLOR_BITS;
   uint8_t rt_written = 0, rt_read = 0;

   if (fs) {
      rt_written = color_attachment_written_mask(fs, &dyns->cal);
      rt_read = color_attachment_read_mask(fs, &dyns->ial, rt_mask);
   }

   bool msaa = dyns->ms.rasterization_samples > 1;
   if ((ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST ||
        ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) &&
       rs->line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM) {
      /* we need to disable MSAA when rendering bresenham lines.
       *
       * From the Vulkan spec:
       *   "When Bresenham lines are being rasterized, sample locations may
       *    all be treated as being at the pixel center (this may affect
       *    attribute and depth interpolation).""
       */
       msaa = false;
   }

   if (dcd0_dirty) {
      struct mali_dcd_flags_0_packed dcd0;
      pan_pack(&dcd0, DCD_FLAGS_0, cfg) {
         if (fs) {
            enum pan_earlyzs_zs_tilebuf_read zs_read =
               PAN_EARLYZS_ZS_TILEBUF_NOT_READ;

            if (z_attachment_read(fs, &dyns->ial) ||
                s_attachment_read(fs, &dyns->ial)) {
               if (writes_z || writes_s || PAN_ARCH != 10)
                  zs_read = PAN_EARLYZS_ZS_TILEBUF_READ_NO_OPT;
               else
                  zs_read = PAN_EARLYZS_ZS_TILEBUF_READ_OPT;
            }

            cfg.allow_forward_pixel_to_kill =
               fs->info.fs.can_fpk && !(rt_mask & ~rt_written) &&
               !(rt_read & rt_written) && !alpha_to_coverage &&
               !cmdbuf->state.gfx.cb.info.any_dest_read;

            cfg.allow_forward_pixel_to_be_killed = !fs->info.writes_global;

            bool writes_zs = writes_z || writes_s;
            bool zs_always_passes = ds_test_always_passes(cmdbuf);
            bool oq = cmdbuf->state.gfx.occlusion_query.mode !=
                      MALI_OCCLUSION_MODE_DISABLED;

            *earlyzs =
               pan_earlyzs_get(fs->fs.earlyzs_lut, writes_zs || oq,
                               alpha_to_coverage, zs_always_passes, zs_read);

            cfg.pixel_kill_operation = (enum mali_pixel_kill)earlyzs->kill;
            cfg.zs_update_operation = (enum mali_pixel_kill)earlyzs->update;

            /* Use per-sample shading if required by API. Also use it when a
             * blend shader is used with multisampling, as this is handled by a
             * single ST_TILE in the blend shader with the current sample ID,
             * requiring per-sample shading.
             */
            cfg.evaluate_per_sample =
               (fs->info.fs.sample_shading ||
                cmdbuf->state.gfx.cb.info.needs_shader) &&
               (dyns->ms.rasterization_samples > 1);

            cfg.shader_modifies_coverage = fs->info.fs.writes_coverage ||
                                           fs->info.fs.can_discard ||
                                           alpha_to_coverage;
         } else {
            cfg.allow_forward_pixel_to_kill = true;
            cfg.allow_forward_pixel_to_be_killed = true;
            cfg.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_EARLY;
            cfg.zs_update_operation = MALI_PIXEL_KILL_FORCE_EARLY;
            cfg.overdraw_alpha0 = true;
            cfg.overdraw_alpha1 = true;
         }

         if (rs->line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM)
            cfg.aligned_line_ends = true;

         cfg.front_face_ccw = rs->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE;
         cfg.cull_front_face = (rs->cull_mode & VK_CULL_MODE_FRONT_BIT) != 0;
         cfg.cull_back_face = (rs->cull_mode & VK_CULL_MODE_BACK_BIT) != 0;

         cfg.multisample_enable = msaa;
         cfg.occlusion_query = cmdbuf->state.gfx.occlusion_query.mode;
         cfg.alpha_to_coverage = alpha_to_coverage;
      }

      cs_update_vt_ctx(b)
         cs_move32_to(b, cs_sr_reg32(b, IDVS, DCD0), dcd0.opaque[0]);
   }

   if (dcd1_dirty) {
      struct mali_dcd_flags_1_packed dcd1;
      pan_pack(&dcd1, DCD_FLAGS_1, cfg) {
         cfg.sample_mask = msaa ? dyns->ms.sample_mask : UINT16_MAX;

         if (fs) {
            cfg.render_target_mask =
               (fs->info.outputs_written >> FRAG_RESULT_DATA0) &
               cmdbuf->state.gfx.render.bound_attachments;
         }
      }

      cs_update_vt_ctx(b)
         cs_move32_to(b, cs_sr_reg32(b, IDVS, DCD1), dcd1.opaque[0]);
   }

   if (dcd2_dirty) {
      struct mali_dcd_flags_2_packed dcd2;
      pan_pack(&dcd2, DCD_FLAGS_2, cfg) {
         cfg.read_mask = rt_read;
         cfg.write_mask = rt_written;
#if PAN_ARCH >= 11
         if (fs) {
            cfg.no_shader_depth_read = !z_attachment_read(fs, &dyns->ial);
            cfg.no_shader_stencil_read = !s_attachment_read(fs, &dyns->ial);
         }
#endif
      }

      cs_update_vt_ctx(b)
         cs_move32_to(b, cs_sr_reg32(b, IDVS, DCD2), dcd2.opaque[0]);
   }
}

static void
prepare_index_buffer(struct panvk_cmd_buffer *cmdbuf,
                     struct panvk_draw_info *draw)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   if (draw->index.size && gfx_state_dirty(cmdbuf, IB)) {
      cs_move32_to(b, cs_sr_reg32(b, IDVS, INDEX_BUFFER_SIZE),
                   cmdbuf->state.gfx.ib.size);

      cs_move64_to(b, cs_sr_reg64(b, IDVS, INDEX_BUFFER),
                   cmdbuf->state.gfx.ib.dev_addr);
   }
}

static void
set_tiler_idvs_flags(struct cs_builder *b, struct panvk_cmd_buffer *cmdbuf,
                     struct panvk_draw_info *draw)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_input_assembly_state *ia = &dyns->ia;
   const struct vk_rasterization_state *rs = &dyns->rs;
   struct mali_primitive_flags_packed tiler_idvs_flags;

   /* When drawing non-point primitives, we use the no_psiz variant which has
    * point size writes patched out */
   bool writes_point_size =
      vs->info.vs.writes_point_size &&
      ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
   bool writes_layer = vs->info.outputs_written & VARYING_BIT_LAYER;
   bool extended_fifo = writes_point_size || vs->info.vs.needs_extended_fifo;
   bool writes_prim_id = vs->info.outputs_written & VARYING_BIT_PRIMITIVE_ID;
   bool fs_reads_prim_id = fs ? fs->info.fs.reads_primitive_id : false;

   bool dirty = gfx_state_dirty(cmdbuf, VS) || fs_user_dirty(cmdbuf) ||
                dyn_gfx_state_dirty(cmdbuf, IA_PRIMITIVE_RESTART_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, IA_PRIMITIVE_TOPOLOGY) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLIP_ENABLE);

   if (dirty) {
      pan_pack(&tiler_idvs_flags, PRIMITIVE_FLAGS, cfg) {
         cfg.draw_mode = translate_prim_topology(ia->primitive_topology);
         cfg.primitive_index_enable = fs_reads_prim_id;
         cfg.primitive_index_override = writes_prim_id && fs_reads_prim_id;

#if PAN_ARCH < 13
         cfg.point_size_array_format = writes_point_size
            ? MALI_POINT_SIZE_ARRAY_FORMAT_FP16
            : MALI_POINT_SIZE_ARRAY_FORMAT_NONE;
#endif

         cfg.layer_index_enable = writes_layer;

         cfg.position_fifo_format = extended_fifo
            ? MALI_FIFO_FORMAT_EXTENDED
            : MALI_FIFO_FORMAT_BASIC;

         cfg.low_depth_cull = cfg.high_depth_cull =
            vk_rasterization_state_depth_clip_enable(rs);

         cfg.secondary_shader = vs->info.vs.secondary_enable && fs != NULL;
         cfg.primitive_restart = ia->primitive_restart_enable;
         cfg.view_mask = cmdbuf->state.gfx.render.view_mask;
      }

      cs_move32_to(b, cs_sr_reg32(b, IDVS, TILER_FLAGS), tiler_idvs_flags.opaque[0]);
#if PAN_ARCH >= 11
      struct mali_primitive_flags_2_packed tiler_flags_2;
      pan_pack(&tiler_flags_2, PRIMITIVE_FLAGS_2, cfg) {
      }
      cs_move32_to(b, cs_sr_reg32(b, IDVS, TILER_FLAGS2),
                   tiler_flags_2.opaque[0]);
#endif
   }
}

static struct mali_primitive_flags_packed
get_tiler_flags_override(struct panvk_draw_info *draw)
{
   struct mali_primitive_flags_packed flags_override;
   /* Pack with nodefaults so only explicitly set override fields affect the
    * previously set register values */
   pan_pack_nodefaults(&flags_override, PRIMITIVE_FLAGS, cfg) {
      cfg.index_type = index_size_to_index_type(draw->index.size);
   };

   return flags_override;
}

static VkResult
prepare_draw(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_info *draw)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   bool idvs = vs->info.vs.idvs;
   VkResult result;

   assert(vs);

   /* FIXME: support non-IDVS. */
   assert(idvs);

   if (cmdbuf->state.gfx.vk_meta) {
      /* vk_meta doesn't care about the provoking vertex mode, we should use
       * the same mode that the application uses. */
      set_provoking_vertex_mode(cmdbuf, U_TRISTATE_UNSET);
   } else {
      enum u_tristate first_provoking_vertex = u_tristate_make(
         cmdbuf->vk.dynamic_graphics_state.rs.provoking_vertex ==
         VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT);
      set_provoking_vertex_mode(cmdbuf, first_provoking_vertex);
   }

   result = update_tls(cmdbuf);
   if (result != VK_SUCCESS)
      return result;

   if (!cmdbuf->vk.dynamic_graphics_state.rs.rasterizer_discard_enable) {
      const struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
      uint32_t *nr_samples = &cmdbuf->state.gfx.render.fb.nr_samples;
      uint32_t rasterization_samples =
         cmdbuf->vk.dynamic_graphics_state.ms.rasterization_samples;

      /* If there's no attachment, we patch nr_samples to match
       * rasterization_samples, otherwise, we make sure those two numbers match.
       */
      if (!cmdbuf->state.gfx.render.bound_attachments) {
         assert(rasterization_samples > 0);
         *nr_samples = rasterization_samples;
      } else {
         assert(rasterization_samples == *nr_samples);
      }

      /* In case we already emitted tiler/framebuffer descriptors, we ensure
       * that the sample count didn't change
       * XXX: This currently can happen in case we resume a render pass with no
       * attachements and without any draw as the FBD is emitted when suspending.
       */
      assert(fbinfo->nr_samples == 0 ||
             fbinfo->nr_samples == cmdbuf->state.gfx.render.fb.nr_samples);
   }

   if (!inherits_render_ctx(cmdbuf)) {
      result = get_render_ctx(cmdbuf);
      if (result != VK_SUCCESS)
         return result;
   }

   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   uint32_t used_set_mask =
      vs->desc_info.used_set_mask | (fs ? fs->desc_info.used_set_mask : 0);

   if (gfx_state_dirty(cmdbuf, DESC_STATE) || gfx_state_dirty(cmdbuf, VS) ||
       gfx_state_dirty(cmdbuf, FS)) {
      result = panvk_per_arch(cmd_prepare_push_descs)(cmdbuf, desc_state,
                                                      used_set_mask);
      if (result != VK_SUCCESS)
         return result;
   }

   result = prepare_blend(cmdbuf);
   if (result != VK_SUCCESS)
      return result;

   /* Changes to base_instance will modify the offset of per-instance
    * attributes, so we manually invalidate the VI state to trigger a
    * new attribute table generation in that case. */
   if ((draw->instance.base != cmdbuf->state.gfx.sysvals.vs.base_instance ||
        draw->indirect.buffer_dev_addr) &&
       cmdbuf->state.gfx.vi.attribs_changing_on_base_instance != 0)
      BITSET_SET(cmdbuf->vk.dynamic_graphics_state.dirty, MESA_VK_DYNAMIC_VI);

   panvk_per_arch(cmd_prepare_draw_sysvals)(cmdbuf, draw);

   result = prepare_push_uniforms(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return result;

   result = prepare_vs(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return result;

   result = prepare_fs(cmdbuf);
   if (result != VK_SUCCESS)
      return result;

   /* Assumes 16 byte slots. We could do better. */
   uint32_t varying_size = get_varying_slots(cmdbuf) * 16;

   cs_update_vt_ctx(b) {
      prepare_index_buffer(cmdbuf, draw);

      set_tiler_idvs_flags(b, cmdbuf, draw);

      cs_move32_to(b, cs_sr_reg32(b, IDVS, VARY_SIZE), varying_size);

      struct pan_earlyzs_state earlyzs = {0};

      prepare_dcd(cmdbuf, &earlyzs);

      result = prepare_ds(cmdbuf, earlyzs);
      if (result != VK_SUCCESS)
         return result;

      result = prepare_oq(cmdbuf);
      if (result != VK_SUCCESS)
         return result;

      prepare_vp(cmdbuf);
      prepare_tiler_primitive_size(cmdbuf);
   }

   clear_dirty_after_draw(cmdbuf);
   return VK_SUCCESS;
}

static void
panvk_cmd_draw(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_info *draw)
{
   const struct cs_tracing_ctx *tracing_ctx =
      &cmdbuf->state.cs[PANVK_SUBQUEUE_VERTEX_TILER].tracing;
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   VkResult result;

   /* If there's no vertex shader, we can skip the draw. */
   if (!panvk_priv_mem_dev_addr(vs->spd))
      return;

   /* Needs to be done before get_fs() is called because it depends on
    * fs.required being initialized. */
   cmdbuf->state.gfx.fs.required =
      fs_required(&cmdbuf->state.gfx, &cmdbuf->vk.dynamic_graphics_state);

   result = prepare_draw(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return;

   cs_update_vt_ctx(b) {
      cs_move32_to(b, cs_sr_reg32(b, IDVS, GLOBAL_ATTRIBUTE_OFFSET), 0);
      cs_move32_to(b, cs_sr_reg32(b, IDVS, INDEX_COUNT), draw->vertex.count);
      cs_move32_to(b, cs_sr_reg32(b, IDVS, INSTANCE_COUNT),
                   draw->instance.count);
      cs_move32_to(b, cs_sr_reg32(b, IDVS, INDEX_OFFSET), draw->index.offset);
      cs_move32_to(b, cs_sr_reg32(b, IDVS, VERTEX_OFFSET), draw->vertex.base);
      /* NIR expects zero-based instance ID, but even if it did have an
       * intrinsic to load the absolute instance ID, we'd want to keep it
       * zero-based to work around Mali's limitation on non-zero firstInstance
       * when a instance divisor is used.
       */
      cs_move32_to(b, cs_sr_reg32(b, IDVS, INSTANCE_OFFSET), 0);
   }

   struct mali_primitive_flags_packed flags_override =
      get_tiler_flags_override(draw);

   uint32_t idvs_count = DIV_ROUND_UP(cmdbuf->state.gfx.render.layer_count,
                                      MAX_LAYERS_PER_TILER_DESC);

   if (idvs_count > 1) {
      struct cs_index counter_reg = cs_scratch_reg32(b, 17);
      struct cs_index tiler_ctx_addr = cs_sr_reg64(b, IDVS, TILER_CTX);

      cs_move32_to(b, counter_reg, idvs_count);

      cs_while(b, MALI_CS_CONDITION_GREATER, counter_reg) {
#if PAN_ARCH >= 12
         cs_trace_run_idvs2(b, tracing_ctx, cs_scratch_reg_tuple(b, 0, 4),
                            flags_override.opaque[0], true, cs_undef(),
                            MALI_IDVS_SHADING_MODE_EARLY);
#else
         cs_trace_run_idvs(b, tracing_ctx, cs_scratch_reg_tuple(b, 0, 4),
                           flags_override.opaque[0], true,
                           cs_shader_res_sel(0, 0, 1, 0),
                           cs_shader_res_sel(2, 2, 2, 0), cs_undef());
#endif

         cs_add32(b, counter_reg, counter_reg, -1);
         cs_update_vt_ctx(b) {
            cs_add64(b, tiler_ctx_addr, tiler_ctx_addr,
                     pan_size(TILER_CONTEXT));
         }
      }

      cs_update_vt_ctx(b) {
         cs_add64(b, tiler_ctx_addr, tiler_ctx_addr,
                  -(idvs_count * pan_size(TILER_CONTEXT)));
      }
   } else {
#if PAN_ARCH >= 12
      cs_trace_run_idvs2(b, tracing_ctx, cs_scratch_reg_tuple(b, 0, 4),
                         flags_override.opaque[0], true, cs_undef(),
                         MALI_IDVS_SHADING_MODE_EARLY);
#else
      cs_trace_run_idvs(b, tracing_ctx, cs_scratch_reg_tuple(b, 0, 4),
                        flags_override.opaque[0], true,
                        cs_shader_res_sel(0, 0, 1, 0),
                        cs_shader_res_sel(2, 2, 2, 0), cs_undef());
#endif
   }
}

VkResult
panvk_per_arch(cmd_prepare_exec_cmd_for_draws)(
   struct panvk_cmd_buffer *primary,
   struct panvk_cmd_buffer *secondary)
{
   if (!(secondary->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT))
      return VK_SUCCESS;

   if (!inherits_render_ctx(primary)) {
      enum u_tristate first_provoking_vertex =
         secondary->state.gfx.render.first_provoking_vertex;
      set_provoking_vertex_mode(primary, first_provoking_vertex);
      VkResult result  = get_render_ctx(primary);
      if (result != VK_SUCCESS)
         return result;
   }

   return prepare_oq(primary);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDraw)(VkCommandBuffer commandBuffer, uint32_t vertexCount,
                        uint32_t instanceCount, uint32_t firstVertex,
                        uint32_t firstInstance)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (instanceCount == 0 || vertexCount == 0)
      return;

   /* gl_BaseVertexARB is a signed integer, and it should expose the value of
    * firstVertex in a non-indexed draw. */
   assert(firstVertex < INT32_MAX);

   /* gl_BaseInstance is a signed integer, and it should expose the value of
    * firstInstnace. */
   assert(firstInstance < INT32_MAX);

   struct panvk_draw_info draw = {
      .vertex.base = firstVertex,
      .vertex.count = vertexCount,
      .instance.base = firstInstance,
      .instance.count = instanceCount,
   };

   panvk_cmd_draw(cmdbuf, &draw);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndexed)(VkCommandBuffer commandBuffer,
                               uint32_t indexCount, uint32_t instanceCount,
                               uint32_t firstIndex, int32_t vertexOffset,
                               uint32_t firstInstance)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (instanceCount == 0 || indexCount == 0)
      return;

   /* gl_BaseInstance is a signed integer, and it should expose the value of
    * firstInstnace. */
   assert(firstInstance < INT32_MAX);

   struct panvk_draw_info draw = {
      .index.size = cmdbuf->state.gfx.ib.index_size,
      .index.offset = firstIndex,
      .vertex.base = vertexOffset,
      .vertex.count = indexCount,
      .instance.count = instanceCount,
      .instance.base = firstInstance,
   };

   panvk_cmd_draw(cmdbuf, &draw);
}

static void
panvk_cmd_draw_indirect(struct panvk_cmd_buffer *cmdbuf,
                        struct panvk_draw_info *draw)
{
   const struct cs_tracing_ctx *tracing_ctx =
      &cmdbuf->state.cs[PANVK_SUBQUEUE_VERTEX_TILER].tracing;
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   VkResult result;

   /* If there's no vertex shader, we can skip the draw. */
   if (!panvk_priv_mem_dev_addr(vs->spd))
      return;

   /* Needs to be done before get_fs() is called because it depends on
    * fs.required being initialized. */
   cmdbuf->state.gfx.fs.required =
      fs_required(&cmdbuf->state.gfx, &cmdbuf->vk.dynamic_graphics_state);

   /* Layered indirect draw (VK_EXT_shader_viewport_index_layer) needs
    * additional changes. We allow layer_count == 0 because that happens
    * when mixing dynamic rendering and secondary command buffers. Once
    * we decide to support layared+indirect, we'll need to pass the
    * layer_count info through the tiler descriptor, for instance by
    * re-using one of the word that's flagged 'ignored' in the descriptor
    * (word 14:23).
    *
    * Multiview is limited to 8 layers, and so will always fit in one TD.
    * Therefore layered rendering is allowed with multiview. */
   assert(cmdbuf->state.gfx.render.layer_count <= 1 ||
          cmdbuf->state.gfx.render.view_mask);

   /* Force a new push uniform block to be allocated */
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);

   result = prepare_draw(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return;

   struct panvk_shader_desc_state *vs_desc_state =
      &cmdbuf->state.gfx.vs.desc;
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_vertex_input_state *vi = dyns->vi;

   struct mali_primitive_flags_packed flags_override =
      get_tiler_flags_override(draw);

   uint32_t patch_attribs =
      cmdbuf->state.gfx.vi.attribs_changing_on_base_instance;
   uint32_t vs_res_table_size =
      panvk_shader_res_table_count(&cmdbuf->state.gfx.vs.desc) *
      pan_size(RESOURCE);
   bool patch_faus = shader_uses_sysval(vs, graphics, vs.first_vertex) ||
                     shader_uses_sysval(vs, graphics, vs.base_instance);
   struct cs_index draw_params_addr = cs_scratch_reg64(b, 0);
   struct cs_index vs_drv_set = cs_scratch_reg64(b, 2);
   struct cs_index attrib_offset = cs_scratch_reg32(b, 4);
   struct cs_index multiplicand = cs_scratch_reg32(b, 5);
   struct cs_index draw_count = cs_scratch_reg32(b, 6);
   struct cs_index max_draw_count = cs_scratch_reg32(b, 7);
   struct cs_index draw_id = cs_scratch_reg32(b, 7);
   struct cs_index vs_fau_addr = cs_scratch_reg64(b, 8);
   struct cs_index tracing_scratch_regs = cs_scratch_reg_tuple(b, 10, 4);
   uint32_t vs_fau_count = vs->fau.total_count;

   if (draw->indirect.count_buffer_dev_addr) {
      cs_move32_to(b, max_draw_count, draw->indirect.draw_count);
      cs_move64_to(b, draw_params_addr, draw->indirect.count_buffer_dev_addr);
      cs_load32_to(b, draw_count, draw_params_addr, 0);
      cs_umin32(b, draw_count, draw_count, max_draw_count);
   } else {
      cs_move32_to(b, draw_count, draw->indirect.draw_count);
   }

   if (patch_faus)
      cs_move64_to(b, vs_fau_addr, cmdbuf->state.gfx.vs.push_uniforms);

   if (patch_attribs != 0)
      cs_move64_to(b, vs_drv_set, vs_desc_state->driver_set.dev_addr);

   cs_move64_to(b, draw_params_addr, draw->indirect.buffer_dev_addr);
   cs_move32_to(b, draw_id, 0);

   cs_while(b, MALI_CS_CONDITION_GREATER, draw_count) {
      cs_update_vt_ctx(b) {
         cs_move32_to(b, cs_sr_reg32(b, IDVS, GLOBAL_ATTRIBUTE_OFFSET), 0);
         /* Load SR33-37 from indirect buffer. */
         unsigned reg_mask = draw->index.size ? 0b11111 : 0b11011;
         cs_load_to(b, cs_sr_reg_tuple(b, IDVS, INDEX_COUNT, 5),
                    draw_params_addr, reg_mask, 0);
      }

      if (patch_faus) {
         if (shader_uses_sysval(vs, graphics, vs.first_vertex)) {
            cs_store32(b, cs_sr_reg32(b, IDVS, VERTEX_OFFSET), vs_fau_addr,
                       shader_remapped_sysval_offset(
                          vs, sysval_offset(graphics, vs.first_vertex)));
         }

         if (shader_uses_sysval(vs, graphics, vs.base_instance)) {
            cs_store32(b, cs_sr_reg32(b, IDVS, INSTANCE_OFFSET), vs_fau_addr,
                       shader_remapped_sysval_offset(
                          vs, sysval_offset(graphics, vs.base_instance)));
         }
      }

      if (patch_attribs != 0) {
         /* If firstInstance=0, skip the offset adjustment. */
         cs_if(b, MALI_CS_CONDITION_NEQUAL,
               cs_sr_reg32(b, IDVS, INSTANCE_OFFSET)) {
            u_foreach_bit(i, patch_attribs) {
               const struct vk_vertex_attribute_state *attrib_info =
                  &vi->attributes[i];
               const uint32_t stride =
                  dyns->vi_binding_strides[attrib_info->binding];

               cs_load32_to(b, attrib_offset, vs_drv_set,
                            pan_size(ATTRIBUTE) * i + (2 * sizeof(uint32_t)));

               /* Emulated immediate multiply: we walk the bits in
                * base_instance, and accumulate (stride << bit_pos) if the bit
                * is present. This is sub-optimal, but it's simple :-). */
               cs_move_reg32(b, multiplicand,
                             cs_sr_reg32(b, IDVS, INSTANCE_OFFSET));

               /* Flush the loads here so that we don't get automatic flushes
                * over and over again due to the divergent nature of the if/else
                * in the loop below. */
               cs_flush_loads(b);
               for (uint32_t i = 31; i > 0; i--) {
                  uint32_t add = stride << i;

                  /* bit31 is the sign bit, so we don't need to subtract to
                   * check the presence of the bit. */
                  if (i < 31)
                     cs_add32(b, multiplicand, multiplicand, -(1 << i));

                  if (add) {
                     cs_if(b, MALI_CS_CONDITION_LESS, multiplicand)
                        cs_add32(b, multiplicand, multiplicand, 1 << i);
                     cs_else(b)
                        cs_add32(b, attrib_offset, attrib_offset, add);
                  } else {
                     cs_if(b, MALI_CS_CONDITION_LESS, multiplicand)
                        cs_add32(b, multiplicand, multiplicand, 1 << i);
                  }
               }

               cs_if(b, MALI_CS_CONDITION_NEQUAL, multiplicand)
                  cs_add32(b, attrib_offset, attrib_offset, stride);

               cs_store32(b, attrib_offset, vs_drv_set,
                          pan_size(ATTRIBUTE) * i + (2 * sizeof(uint32_t)));
               cs_flush_stores(b);
            }
         }
      }

      /* NIR expects zero-based instance ID, but even if it did have an
       * intrinsic to load the absolute instance ID, we'd want to keep it
       * zero-based to work around Mali's limitation on non-zero firstInstance
       * when a instance divisor is used.
       */
      cs_update_vt_ctx(b)
         cs_move32_to(b, cs_sr_reg32(b, IDVS, INSTANCE_OFFSET), 0);

#if PAN_ARCH >= 12
      cs_trace_run_idvs2(b, tracing_ctx, tracing_scratch_regs,
                         flags_override.opaque[0], true, draw_id,
                         MALI_IDVS_SHADING_MODE_EARLY);
#else
      cs_trace_run_idvs(
         b, tracing_ctx, tracing_scratch_regs, flags_override.opaque[0], true,
         cs_shader_res_sel(0, 0, 1, 0), cs_shader_res_sel(2, 2, 2, 0), draw_id);
#endif

      cs_add32(b, draw_count, draw_count, -1);
      cs_add32(b, draw_id, draw_id, 1);
      cs_add64(b, draw_params_addr, draw_params_addr,
               draw->indirect.stride);

      if (patch_faus) {
         cs_add64(b, vs_fau_addr, vs_fau_addr, vs_fau_count * sizeof(uint64_t));
         cs_update_vt_ctx(b) {
            cs_add64(b, cs_sr_reg64(b, IDVS, VERTEX_FAU),
                     cs_sr_reg64(b, IDVS, VERTEX_FAU),
                     vs_fau_count * sizeof(uint64_t));
         }

      }

      if (patch_attribs != 0) {
         cs_add64(b, vs_drv_set, vs_drv_set,
                  vs_desc_state->driver_set.size);
         cs_update_vt_ctx(b) {
            cs_add64(b, cs_sr_reg64(b, IDVS, VERTEX_SRT),
                     cs_sr_reg64(b, IDVS, VERTEX_SRT), vs_res_table_size);
         }
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndirect)(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                                VkDeviceSize offset, uint32_t drawCount,
                                uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);

   if (drawCount == 0)
      return;

   struct panvk_draw_info draw = {
      .indirect.buffer_dev_addr = panvk_buffer_gpu_ptr(buffer, offset),
      .indirect.draw_count = drawCount,
      .indirect.stride = stride,
   };

   panvk_cmd_draw_indirect(cmdbuf, &draw);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndexedIndirect)(VkCommandBuffer commandBuffer,
                                       VkBuffer _buffer, VkDeviceSize offset,
                                       uint32_t drawCount, uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);

   if (drawCount == 0)
      return;

   struct panvk_draw_info draw = {
      .index.size = cmdbuf->state.gfx.ib.index_size,
      .indirect.buffer_dev_addr = panvk_buffer_gpu_ptr(buffer, offset),
      .indirect.draw_count = drawCount,
      .indirect.stride = stride,
   };

   panvk_cmd_draw_indirect(cmdbuf, &draw);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndirectCount)(VkCommandBuffer commandBuffer,
                                     VkBuffer _buffer,
                                     VkDeviceSize offset,
                                     VkBuffer countBuffer,
                                     VkDeviceSize countBufferOffset,
                                     uint32_t maxDrawCount,
                                     uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);
   VK_FROM_HANDLE(panvk_buffer, count_buffer, countBuffer);

   if (maxDrawCount == 0)
      return;

   struct panvk_draw_info draw = {
      .indirect.buffer_dev_addr = panvk_buffer_gpu_ptr(buffer, offset),
      .indirect.count_buffer_dev_addr =
         panvk_buffer_gpu_ptr(count_buffer, countBufferOffset),
      .indirect.draw_count = maxDrawCount,
      .indirect.stride = stride,
   };

   panvk_cmd_draw_indirect(cmdbuf, &draw);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndexedIndirectCount)(VkCommandBuffer commandBuffer,
                                            VkBuffer _buffer,
                                            VkDeviceSize offset,
                                            VkBuffer countBuffer,
                                            VkDeviceSize countBufferOffset,
                                            uint32_t maxDrawCount,
                                            uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);
   VK_FROM_HANDLE(panvk_buffer, count_buffer, countBuffer);

   if (maxDrawCount == 0)
      return;

   struct panvk_draw_info draw = {
      .index.size = cmdbuf->state.gfx.ib.index_size,
      .indirect.buffer_dev_addr = panvk_buffer_gpu_ptr(buffer, offset),
      .indirect.count_buffer_dev_addr =
         panvk_buffer_gpu_ptr(count_buffer, countBufferOffset),
      .indirect.draw_count = maxDrawCount,
      .indirect.stride = stride,
   };

   panvk_cmd_draw_indirect(cmdbuf, &draw);
}

void
panvk_per_arch(cmd_inherit_render_state)(
   struct panvk_cmd_buffer *cmdbuf,
   const VkCommandBufferBeginInfo *pBeginInfo)
{
   if (cmdbuf->vk.level != VK_COMMAND_BUFFER_LEVEL_SECONDARY ||
       !(pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT))
      return;

   assert(pBeginInfo->pInheritanceInfo);
   char gcbiar_data[VK_GCBIARR_DATA_SIZE(MAX_RTS)];
   const VkRenderingInfo *resume_info =
      vk_get_command_buffer_inheritance_as_rendering_resume(cmdbuf->vk.level,
                                                            pBeginInfo,
                                                            gcbiar_data);
   if (resume_info) {
      panvk_per_arch(cmd_init_render_state)(cmdbuf, resume_info);
      return;
   }

   const VkCommandBufferInheritanceRenderingInfo *inheritance_info =
      vk_get_command_buffer_inheritance_rendering_info(cmdbuf->vk.level,
                                                       pBeginInfo);
   assert(inheritance_info);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;

   cmdbuf->state.gfx.render.first_provoking_vertex = U_TRISTATE_UNSET;
   cmdbuf->state.gfx.render.maybe_set_tds_provoking_vertex = NULL;
   cmdbuf->state.gfx.render.maybe_set_fbds_provoking_vertex = NULL;
   cmdbuf->state.gfx.render.suspended = false;
   cmdbuf->state.gfx.render.flags = inheritance_info->flags;

   gfx_state_set_dirty(cmdbuf, RENDER_STATE);
   memset(cmdbuf->state.gfx.render.fb.crc_valid, 0,
          sizeof(cmdbuf->state.gfx.render.fb.crc_valid));
   memset(&cmdbuf->state.gfx.render.color_attachments, 0,
          sizeof(cmdbuf->state.gfx.render.color_attachments));
   memset(&cmdbuf->state.gfx.render.z_attachment, 0,
          sizeof(cmdbuf->state.gfx.render.z_attachment));
   memset(&cmdbuf->state.gfx.render.s_attachment, 0,
          sizeof(cmdbuf->state.gfx.render.s_attachment));
   cmdbuf->state.gfx.render.bound_attachments = 0;

   cmdbuf->state.gfx.render.view_mask = inheritance_info->viewMask;
   cmdbuf->state.gfx.render.layer_count = inheritance_info->viewMask ?
      util_last_bit(inheritance_info->viewMask) :
      0;

   /* If a draw was performed, the inherited sample count should match our current sample count */
   assert(fbinfo->nr_samples == 0 || inheritance_info->rasterizationSamples == fbinfo->nr_samples);
   *fbinfo = (struct pan_fb_info){
      .tile_buf_budget = pan_query_optimal_tib_size(PAN_ARCH, phys_dev->model),
      .z_tile_buf_budget = pan_query_optimal_z_tib_size(PAN_ARCH, phys_dev->model),
      .tile_size = fbinfo->tile_size,
      .cbuf_allocation = fbinfo->cbuf_allocation,
      .nr_samples = inheritance_info->rasterizationSamples,
      .rt_count = inheritance_info->colorAttachmentCount,
   };
   cmdbuf->state.gfx.render.fb.nr_samples = inheritance_info->rasterizationSamples;

   assert(inheritance_info->colorAttachmentCount <= ARRAY_SIZE(fbinfo->rts));

   for (uint32_t i = 0; i < inheritance_info->colorAttachmentCount; i++) {
      cmdbuf->state.gfx.render.bound_attachments |=
         MESA_VK_RP_ATTACHMENT_COLOR_BIT(i);
      cmdbuf->state.gfx.render.color_attachments.fmts[i] =
         inheritance_info->pColorAttachmentFormats[i];
      cmdbuf->state.gfx.render.color_attachments.samples[i] =
         inheritance_info->rasterizationSamples;
   }

   if (inheritance_info->depthAttachmentFormat) {
      cmdbuf->state.gfx.render.bound_attachments |=
         MESA_VK_RP_ATTACHMENT_DEPTH_BIT;
      cmdbuf->state.gfx.render.z_attachment.fmt =
         inheritance_info->depthAttachmentFormat;
   }

   if (inheritance_info->stencilAttachmentFormat) {
      cmdbuf->state.gfx.render.bound_attachments |=
         MESA_VK_RP_ATTACHMENT_STENCIL_BIT;
      cmdbuf->state.gfx.render.s_attachment.fmt =
         inheritance_info->stencilAttachmentFormat;
   }

   const VkRenderingAttachmentLocationInfoKHR att_loc_info_default = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_LOCATION_INFO_KHR,
      .colorAttachmentCount = inheritance_info->colorAttachmentCount,
   };
   const VkRenderingAttachmentLocationInfoKHR *att_loc_info =
      vk_get_command_buffer_rendering_attachment_location_info(
         cmdbuf->vk.level, pBeginInfo);
   if (att_loc_info == NULL)
      att_loc_info = &att_loc_info_default;

   vk_cmd_set_rendering_attachment_locations(&cmdbuf->vk, att_loc_info);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginRendering)(VkCommandBuffer commandBuffer,
                                  const VkRenderingInfo *pRenderingInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_cmd_graphics_state *state = &cmdbuf->state.gfx;
   bool resuming = pRenderingInfo->flags & VK_RENDERING_RESUMING_BIT;

   panvk_per_arch(cmd_init_render_state)(cmdbuf, pRenderingInfo);

   /* If we're not resuming, the FBD should be NULL. */
   assert(!state->render.fbds.gpu || resuming);

   panvk_per_arch(panvk_instr_begin_work)(PANVK_SUBQUEUE_VERTEX_TILER, cmdbuf,
                                          PANVK_INSTR_WORK_TYPE_RENDER);
   panvk_per_arch(panvk_instr_begin_work)(PANVK_SUBQUEUE_FRAGMENT, cmdbuf,
                                          PANVK_INSTR_WORK_TYPE_RENDER);

   if (!resuming)
      panvk_per_arch(cmd_preload_render_area_border)(cmdbuf, pRenderingInfo);
}

static void
flush_tiling(struct panvk_cmd_buffer *cmdbuf)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   if (!cmdbuf->state.gfx.render.tiler && !inherits_render_ctx(cmdbuf))
      return;

   /* Flush the tiling operations and signal the internal sync object. */
   cs_finish_tiling(b);

   /* We're relying on PANVK_SUBQUEUE_VERTEX_TILER being the first queue to
    * skip an ADD operation on the syncobjs pointer. */
   STATIC_ASSERT(PANVK_SUBQUEUE_VERTEX_TILER == 0);

#if PAN_ARCH >= 11
   struct cs_index sync_addr = cs_scratch_reg64(b, 0);
   struct cs_index add_val = cs_scratch_reg64(b, 2);

   cs_load64_to(b, sync_addr, cs_subqueue_ctx_reg(b),
                offsetof(struct panvk_cs_subqueue_context, syncobjs));

   cs_move64_to(b, add_val, 1);
   cs_heap_operation(b, MALI_CS_HEAP_OPERATION_VERTEX_TILER_COMPLETED,
                     cs_defer_indirect());
   panvk_instr_sync64_add(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER, true,
                          MALI_CS_SYNC_SCOPE_CSG, add_val, sync_addr,
                          cs_defer_indirect());
#else
   struct cs_index sync_addr = cs_scratch_reg64(b, 0);
   struct cs_index iter_sb = cs_scratch_reg32(b, 2);
   struct cs_index cmp_scratch = cs_scratch_reg32(b, 3);
   struct cs_index add_val = cs_scratch_reg64(b, 4);

   cs_load_to(b, cs_scratch_reg_tuple(b, 0, 3), cs_subqueue_ctx_reg(b),
              BITFIELD_MASK(3),
              offsetof(struct panvk_cs_subqueue_context, syncobjs));

   cs_move64_to(b, add_val, 1);

   cs_match_iter_sb(b, x, iter_sb, cmp_scratch) {
      cs_heap_operation(b, MALI_CS_HEAP_OPERATION_VERTEX_TILER_COMPLETED,
                        cs_defer(SB_WAIT_ITER(x), SB_ID(DEFERRED_SYNC)));
      panvk_instr_sync64_add(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER, true,
                             MALI_CS_SYNC_SCOPE_CSG, add_val, sync_addr,
                             cs_defer(SB_WAIT_ITER(x), SB_ID(DEFERRED_SYNC)));
   }
#endif

   /* Update the vertex seqno. */
   ++cmdbuf->state.cs[PANVK_SUBQUEUE_VERTEX_TILER].relative_sync_point;
}

static void
wait_finish_tiling(struct panvk_cmd_buffer *cmdbuf)
{
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_FRAGMENT);
   struct cs_index vt_sync_addr = cs_scratch_reg64(b, 0);
   struct cs_index vt_sync_point = cs_scratch_reg64(b, 2);
   uint64_t rel_vt_sync_point =
      cmdbuf->state.cs[PANVK_SUBQUEUE_VERTEX_TILER].relative_sync_point;

   cs_load64_to(b, vt_sync_addr, cs_subqueue_ctx_reg(b),
                offsetof(struct panvk_cs_subqueue_context, syncobjs));

   cs_add64(b, vt_sync_point,
            cs_progress_seqno_reg(b, PANVK_SUBQUEUE_VERTEX_TILER),
            rel_vt_sync_point);

   panvk_instr_sync64_wait(cmdbuf, PANVK_SUBQUEUE_FRAGMENT, false,
                           MALI_CS_CONDITION_GREATER, vt_sync_point,
                           vt_sync_addr);
}

static uint32_t
calc_tiler_oom_handler_idx(struct panvk_cmd_buffer *cmdbuf)
{
   const struct pan_fb_info *fb = &cmdbuf->state.gfx.render.fb.info;
   bool has_zs_ext = fb->zs.view.zs || fb->zs.view.s;
   uint32_t rt_count = MAX2(fb->rt_count, 1);

   return get_tiler_oom_handler_idx(has_zs_ext, rt_count);
}

static void
setup_tiler_oom_ctx(struct panvk_cmd_buffer *cmdbuf)
{
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_FRAGMENT);

   uint32_t layer_count = cmdbuf->state.gfx.render.layer_count;
   uint32_t td_count = DIV_ROUND_UP(layer_count, MAX_LAYERS_PER_TILER_DESC);
   uint32_t fbd_sz = calc_fbd_size(cmdbuf);
   const uint32_t fbd_scratch_offset = fbd_sz * layer_count;

   struct cs_index counter = cs_scratch_reg32(b, 1);
   cs_move32_to(b, counter, 0);
   cs_store32(b, counter, cs_subqueue_ctx_reg(b),
              TILER_OOM_CTX_FIELD_OFFSET(counter));

   struct cs_index fbd_ptr_reg = cs_sr_reg64(b, FRAGMENT, FBD_POINTER);
   cs_store64(b, fbd_ptr_reg, cs_subqueue_ctx_reg(b),
              TILER_OOM_CTX_FIELD_OFFSET(layer_fbd_ptr));

   struct cs_index scratch_fbd_ptr_reg = cs_scratch_reg64(b, 2);
   cs_add64(b, scratch_fbd_ptr_reg, fbd_ptr_reg, fbd_scratch_offset);
   cs_store64(b, scratch_fbd_ptr_reg, cs_subqueue_ctx_reg(b),
              TILER_OOM_CTX_FIELD_OFFSET(ir_scratch_fbd_ptr));

   struct cs_index td_count_reg = cs_scratch_reg32(b, 4);
   cs_move32_to(b, td_count_reg, td_count);
   cs_store32(b, td_count_reg, cs_subqueue_ctx_reg(b),
              TILER_OOM_CTX_FIELD_OFFSET(td_count));

   struct cs_index layer_count_index = cs_scratch_reg32(b, 5);
   cs_move32_to(b, layer_count_index, layer_count);
   cs_store32(b, layer_count_index, cs_subqueue_ctx_reg(b),
              TILER_OOM_CTX_FIELD_OFFSET(layer_count));

   cs_flush_stores(b);
}

static VkResult
issue_fragment_jobs(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct cs_tracing_ctx *tracing_ctx =
      &cmdbuf->state.cs[PANVK_SUBQUEUE_FRAGMENT].tracing;
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_FRAGMENT);
   bool has_oq_chain = cmdbuf->state.gfx.render.oq.chain != 0;

   struct cs_index next_iter_sb_scratch = cs_scratch_reg_tuple(b, 0, 2);
   panvk_per_arch(cs_next_iter_sb)(cmdbuf, PANVK_SUBQUEUE_FRAGMENT,
                                   next_iter_sb_scratch);

   /* Now initialize the fragment bits. */
   cs_update_frag_ctx(b) {
      cs_move32_to(b, cs_sr_reg32(b, FRAGMENT, BBOX_MIN),
                   (fbinfo->draw_extent.miny << 16) | fbinfo->draw_extent.minx);
      cs_move32_to(b, cs_sr_reg32(b, FRAGMENT, BBOX_MAX),
                   (fbinfo->draw_extent.maxy << 16) | fbinfo->draw_extent.maxx);
   }

   bool simul_use =
      cmdbuf->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

   /* The only bit we patch in FBDs is the tiler pointer. If tiler is not
    * involved (clear job) or if the update can happen in place (not
    * simultaneous use of the command buffer), we can avoid the
    * copy. */
   bool needs_tiling =
      cmdbuf->state.gfx.render.tiler || inherits_render_ctx(cmdbuf);

   /* If the command buffer can run in parallel on different queues, we need
    * to make sure each instance has its own descriptors, unless tiling is
    * not needed (AKA RUN_FRAGMENT used for clears), because then the FBD
    * descriptors are constant (no need to patch them at runtime). */
   bool free_render_descs = simul_use && needs_tiling;
   uint32_t fbd_sz = calc_fbd_size(cmdbuf);
   uint32_t scratch_fbd_offset = fbd_sz * cmdbuf->state.gfx.render.layer_count;
   uint32_t ir_fbd_desc_sz = sizeof(struct panvk_ir_desc_info);
   uint32_t td_count = 0;
   if (needs_tiling) {
      td_count = DIV_ROUND_UP(cmdbuf->state.gfx.render.layer_count,
                              MAX_LAYERS_PER_TILER_DESC);
   }

   /* Update the Tiler OOM context */
   setup_tiler_oom_ctx(cmdbuf);

   /* Enable the oom handler before waiting for the vertex/tiler work.
    * At this point, the tiler oom context has been set up with the correct
    * state for this renderpass, so it's safe to enable. */
   struct cs_index addr_reg = cs_scratch_reg64(b, 0);
   struct cs_index length_reg = cs_scratch_reg32(b, 2);
   uint32_t handler_idx = calc_tiler_oom_handler_idx(cmdbuf);
   uint64_t handler_addr = dev->tiler_oom.handlers_bo->addr.dev +
                           handler_idx * dev->tiler_oom.handler_stride;
   cs_move64_to(b, addr_reg, handler_addr);
   cs_move32_to(b, length_reg, dev->tiler_oom.handler_stride);
   cs_set_exception_handler(b, MALI_CS_EXCEPTION_TYPE_TILER_OOM, addr_reg,
                            length_reg);

   /* Wait for the tiling to be done before submitting the fragment job. */
   wait_finish_tiling(cmdbuf);

   /* Disable the oom handler once the vertex/tiler work has finished.
    * We need to disable the handler at this point as the vertex/tiler subqueue
    * might continue on to the next renderpass and hit an out-of-memory
    * exception prior to the fragment subqueue setting up the tiler oom context
    * for the next renderpass.
    * By disabling the handler here, any exception will be left pending until a
    * new hander is registered, at which point the correct state has been set
    * up. */
   cs_move64_to(b, addr_reg, 0);
   cs_move32_to(b, length_reg, 0);
   cs_set_exception_handler(b, MALI_CS_EXCEPTION_TYPE_TILER_OOM, addr_reg,
                            length_reg);

   /* Use the scratch FBD if incremental render occurred. */
   struct cs_index counter = cs_scratch_reg32(b, 0);
   cs_load32_to(
      b, counter, cs_subqueue_ctx_reg(b),
      offsetof(struct panvk_cs_subqueue_context, tiler_oom_ctx.counter));
   cs_wait_slot(b, SB_ID(LS));
   cs_if(b, MALI_CS_CONDITION_GREATER, counter) {
      cs_update_frag_ctx(b) {
         cs_add64(b, cs_sr_reg64(b, FRAGMENT, FBD_POINTER),
                     cs_sr_reg64(b, FRAGMENT, FBD_POINTER), scratch_fbd_offset);
      }
   }

   /* Applications tend to forget to describe subpass dependencies, especially
    * when it comes to write -> read dependencies on attachments. The
    * proprietary driver forces "others" invalidation as a workaround, and this
    * invalidation even became implicit (done as part of the RUN_FRAGMENT) on
    * v13+. We don't do that in panvk, but we provide a debug flag to help
    * identify those issues. */
   if (PANVK_DEBUG(IMPLICIT_OTHERS_INV)) {
      cs_flush_caches(b, MALI_CS_FLUSH_MODE_NONE, MALI_CS_FLUSH_MODE_NONE,
                      MALI_CS_OTHER_FLUSH_MODE_INVALIDATE, length_reg,
                      cs_defer(0x0, SB_ID(IMM_FLUSH)));
      cs_wait_slot(b, SB_ID(IMM_FLUSH));
   }

   const struct pan_fb_info *fb = &cmdbuf->state.gfx.render.fb.info;
   const bool has_zs_ext = fb->zs.view.zs || fb->zs.view.s;
   const uint32_t rt_count = MAX2(fb->rt_count, 1);

   /* IR was hit: set up IR FBD */
   cs_if(b, MALI_CS_CONDITION_GREATER, counter) {
      /* FBD patching registers */
      struct cs_index scratch_regs = cs_scratch_reg_tuple(b, 0, 5);
      struct cs_index ir_fbd_word_0 = cs_scratch_reg32(b, 5);
      struct cs_index remaining_layers_in_td = cs_scratch_reg32(b, 6);
      struct cs_index layer_count = cs_scratch_reg32(b, 7);
      struct cs_index layer_fbd_ptr_reg = cs_scratch_reg64(b, 8);
      struct cs_index ir_desc_info_ptr = cs_scratch_reg64(b, 10);
      struct cs_index scratch_fbd_ptr_reg = cs_scratch_reg64(b, 12);

      /* Run fragment is only used after FBD patching */
      struct cs_index run_fragment_regs = cs_scratch_reg_tuple(b, 0, 5);

      /* Get base fbd ptr */
      cs_add64(b, layer_fbd_ptr_reg, cs_sr_reg64(b, FRAGMENT, FBD_POINTER), -(int32_t)scratch_fbd_offset);
      cs_add64(b, scratch_fbd_ptr_reg, cs_sr_reg64(b, FRAGMENT, FBD_POINTER), 0);
      cs_move32_to(b, remaining_layers_in_td, MAX_LAYERS_PER_TILER_DESC);

      /* Get ir info ptr */
      cs_add64(b, ir_desc_info_ptr, cs_subqueue_ctx_reg(b),
               TILER_OOM_CTX_FIELD_OFFSET(ir_desc_infos) +
                  ir_fbd_desc_sz * PANVK_IR_LAST_PASS);

      cs_load32_to(b, ir_fbd_word_0, ir_desc_info_ptr,
                   offsetof(struct panvk_ir_desc_info, fbd.word0));

      if (cmdbuf->state.gfx.render.layer_count <= 1) {
         panvk_per_arch(cs_patch_ir_state)(
            b, tracing_ctx, has_zs_ext, rt_count, remaining_layers_in_td,
            layer_fbd_ptr_reg, ir_desc_info_ptr, ir_fbd_word_0,
            scratch_fbd_ptr_reg, scratch_regs);

         cs_trace_run_fragment(b, tracing_ctx, run_fragment_regs, false,
                               MALI_TILE_RENDER_ORDER_Z_ORDER);
      } else {
         cs_move32_to(b, layer_count, cmdbuf->state.gfx.render.layer_count);
         cs_while(b, MALI_CS_CONDITION_GREATER, layer_count) {
            cs_add32(b, layer_count, layer_count, -1);

            panvk_per_arch(cs_patch_ir_state)(
               b, tracing_ctx, has_zs_ext, rt_count, remaining_layers_in_td,
               layer_fbd_ptr_reg, ir_desc_info_ptr, ir_fbd_word_0,
               scratch_fbd_ptr_reg, scratch_regs);

            cs_trace_run_fragment(b, tracing_ctx, run_fragment_regs, false,
                                  MALI_TILE_RENDER_ORDER_Z_ORDER);

            panvk_per_arch(cs_ir_update_registers_to_next_layer)(
               b, has_zs_ext, rt_count, layer_fbd_ptr_reg, ir_fbd_word_0,
               remaining_layers_in_td);

            /* Serialize run fragments since we reuse FBD for the runs */
            cs_wait_slots(b, dev->csf.sb.all_iters_mask);
         }
      }
   }
   cs_else(b) {
      if (cmdbuf->state.gfx.render.layer_count <= 1) {
         cs_trace_run_fragment(b, tracing_ctx, cs_scratch_reg_tuple(b, 0, 4),
                               false, MALI_TILE_RENDER_ORDER_Z_ORDER);
      } else {
         struct cs_index run_fragment_regs = cs_scratch_reg_tuple(b, 0, 4);
         struct cs_index remaining_layers = cs_scratch_reg32(b, 4);

         cs_move32_to(b, remaining_layers, calc_enabled_layer_count(cmdbuf));
         cs_while(b, MALI_CS_CONDITION_GREATER, remaining_layers) {
            cs_add32(b, remaining_layers, remaining_layers, -1);

            cs_trace_run_fragment(b, tracing_ctx, run_fragment_regs, false,
                                  MALI_TILE_RENDER_ORDER_Z_ORDER);

            cs_update_frag_ctx(b)
               cs_add64(b, cs_sr_reg64(b, FRAGMENT, FBD_POINTER),
                        cs_sr_reg64(b, FRAGMENT, FBD_POINTER), fbd_sz);
         }
      }
   }

   struct cs_index sync_addr = cs_scratch_reg64(b, 0);
   struct cs_index add_val = cs_scratch_reg64(b, 4);
   struct cs_index add_val_lo = cs_scratch_reg32(b, 4);
   struct cs_index ringbuf_sync_addr = cs_scratch_reg64(b, 6);
   struct cs_index release_sz = cs_scratch_reg32(b, 8);

   struct cs_index completed = cs_scratch_reg_tuple(b, 10, 4);
   struct cs_index completed_top = cs_scratch_reg64(b, 10);
   struct cs_index completed_bottom = cs_scratch_reg64(b, 12);
   struct cs_index cur_tiler = cs_reg64(b, 38);
   struct cs_index tiler_count = cs_reg32(b, 47);
   struct cs_index oq_chain = cs_scratch_reg64(b, 10);
   struct cs_index oq_chain_lo = cs_scratch_reg32(b, 10);
   struct cs_index oq_syncobj = cs_scratch_reg64(b, 12);

   cs_move64_to(b, add_val, 1);

   if (free_render_descs) {
      cs_move32_to(b, release_sz, calc_render_descs_size(cmdbuf));
      cs_load64_to(b, ringbuf_sync_addr, cs_subqueue_ctx_reg(b),
                   offsetof(struct panvk_cs_subqueue_context,
                            render.desc_ringbuf.syncobj));
   }

   cs_move32_to(b, tiler_count, td_count);

#if PAN_ARCH >= 11
   cs_load64_to(b, sync_addr, cs_subqueue_ctx_reg(b),
                offsetof(struct panvk_cs_subqueue_context, syncobjs));
   cs_add64(b, sync_addr, sync_addr,
            PANVK_SUBQUEUE_FRAGMENT * sizeof(struct panvk_cs_sync64));
#else
   struct cs_index iter_sb = cs_scratch_reg32(b, 2);
   struct cs_index cmp_scratch = cs_scratch_reg32(b, 3);

   cs_load_to(b, cs_scratch_reg_tuple(b, 0, 3), cs_subqueue_ctx_reg(b),
              BITFIELD_MASK(3),
              offsetof(struct panvk_cs_subqueue_context, syncobjs));
   cs_add64(b, sync_addr, sync_addr,
            PANVK_SUBQUEUE_FRAGMENT * sizeof(struct panvk_cs_sync64));
#endif

#if PAN_ARCH >= 11
   {
      const struct cs_async_op async = cs_defer_indirect();
#else
   cs_match_iter_sb(b, x, iter_sb, cmp_scratch) {
      const struct cs_async_op async =
         cs_defer(SB_WAIT_ITER(x), SB_ID(DEFERRED_SYNC));
#endif

      if (td_count == 1) {
         cs_load_to(b, completed, cur_tiler, BITFIELD_MASK(4), 40);
         cs_finish_fragment(b, true, completed_top, completed_bottom, async);
      } else if (td_count > 1) {
         cs_while(b, MALI_CS_CONDITION_GREATER, tiler_count) {
            cs_load_to(b, completed, cur_tiler, BITFIELD_MASK(4), 40);
            cs_finish_fragment(b, false, completed_top, completed_bottom,
                               async);
            cs_update_frag_ctx(b)
               cs_add64(b, cur_tiler, cur_tiler, pan_size(TILER_CONTEXT));
            cs_add32(b, tiler_count, tiler_count, -1);
         }
         cs_frag_end(b, async);
      }

      if (free_render_descs) {
         cs_sync32_add(b, true, MALI_CS_SYNC_SCOPE_CSG, release_sz,
                       ringbuf_sync_addr, async);
      }

      if (has_oq_chain) {
         struct cs_index flush_id = oq_chain_lo;
         cs_move32_to(b, flush_id, 0);

#if PAN_ARCH >= 11
         /* FLUSH_CACHE2 is part of the deferred group so we need to
          * temporarily set DEFERRED_FLUSH here to use the right scoreboard in
          * indirect mode */
         cs_set_state_imm32(b, MALI_CS_SET_STATE_TYPE_SB_SEL_DEFERRED,
                            SB_ID(DEFERRED_FLUSH));
#endif
         cs_flush_caches(b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_CLEAN,
                         MALI_CS_OTHER_FLUSH_MODE_NONE, flush_id, async);
#if PAN_ARCH >= 11
         cs_set_state_imm32(b, MALI_CS_SET_STATE_TYPE_SB_SEL_DEFERRED,
                            SB_ID(DEFERRED_SYNC));
#endif

         cs_load64_to(b, oq_chain, cs_subqueue_ctx_reg(b),
                      offsetof(struct panvk_cs_subqueue_context, render.oq_chain));

         /* For WAR dependency on subqueue_context.render.oq_chain. */
         cs_flush_loads(b);

         /* We use oq_syncobj as a placeholder to reset the oq_chain. */
         cs_move64_to(b, oq_syncobj, 0);
         cs_store64(b, oq_syncobj, cs_subqueue_ctx_reg(b),
                    offsetof(struct panvk_cs_subqueue_context, render.oq_chain));

         cs_single_link_list_for_each_from(b, oq_chain,
                                           struct panvk_cs_occlusion_query, node) {
            cs_load64_to(b, oq_syncobj, oq_chain,
                         offsetof(struct panvk_cs_occlusion_query, syncobj));
            cs_sync32_set(b, true, MALI_CS_SYNC_SCOPE_CSG, add_val_lo, oq_syncobj,
                          cs_defer(SB_MASK(DEFERRED_FLUSH), SB_ID(DEFERRED_SYNC)));
         }
      }

      panvk_instr_sync64_add(cmdbuf, PANVK_SUBQUEUE_FRAGMENT, true,
                             MALI_CS_SYNC_SCOPE_CSG, add_val, sync_addr, async);
   }

   /* Update the ring buffer position. */
   if (free_render_descs) {
      cs_render_desc_ringbuf_move_ptr(b, calc_render_descs_size(cmdbuf),
                                      !tracing_ctx->enabled);
   }

   /* Update the frag seqno. */
   ++cmdbuf->state.cs[PANVK_SUBQUEUE_FRAGMENT].relative_sync_point;


   return VK_SUCCESS;
}

void
panvk_per_arch(cmd_flush_draws)(struct panvk_cmd_buffer *cmdbuf)
{
   /* If there was no draw queued, we don't need to force a preload. */
   if (cmdbuf->state.gfx.render.fbds.gpu || inherits_render_ctx(cmdbuf)) {
      flush_tiling(cmdbuf);
      issue_fragment_jobs(cmdbuf);
      memset(&cmdbuf->state.gfx.render.fbds, 0,
             sizeof(cmdbuf->state.gfx.render.fbds));
      cmdbuf->state.gfx.render.tiler = 0;

      panvk_per_arch(cmd_force_fb_preload)(cmdbuf, NULL);

      /* We inherited the render context, and need to let the primary command
       * buffer know that it's changed. */
      cmdbuf->state.gfx.render.invalidate_inherited_ctx =
         inherits_render_ctx(cmdbuf);

      /* Re-emit the FB/Tiler descs if we inherited them. */
      if (inherits_render_ctx(cmdbuf))
         get_render_ctx(cmdbuf);
   }
}

static void
handle_deferred_queries(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   for (uint32_t sq = 0; sq < PANVK_SUBQUEUE_COUNT; ++sq) {
      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, sq);
      struct cs_index current = cs_scratch_reg64(b, 0);
      struct cs_index reports = cs_scratch_reg64(b, 2);
      struct cs_index next = cs_scratch_reg64(b, 4);
      int offset = sizeof(uint64_t) * sq;

      cs_load64_to(
         b, current, cs_subqueue_ctx_reg(b),
         offsetof(struct panvk_cs_subqueue_context, render.ts_chain.head));

      cs_while(b, MALI_CS_CONDITION_NEQUAL, current) {

         cs_load64_to(b, reports, current,
                      offsetof(struct panvk_cs_timestamp_query, reports));

         cs_if(b, MALI_CS_CONDITION_NEQUAL, reports)
            cs_store_state(b, reports, offset, MALI_CS_STATE_TIMESTAMP,
                           cs_defer(dev->csf.sb.all_iters_mask, SB_ID(LS)));

         cs_load64_to(b, next, current,
                      offsetof(struct panvk_cs_timestamp_query, node.next));

         if (sq == PANVK_QUERY_TS_INFO_SUBQUEUE) {
            /* WAR on panvk_cs_timestamp_query::next. */
            cs_flush_loads(b);
            struct cs_index tmp = cs_scratch_reg64(b, 6);
            cs_move64_to(b, tmp, 0);
            cs_store64(b, tmp, current,
                       offsetof(struct panvk_cs_timestamp_query, node.next));

            cs_single_link_list_add_tail(
               b, cs_subqueue_ctx_reg(b),
               offsetof(struct panvk_cs_subqueue_context, render.ts_done_chain),
               current, offsetof(struct panvk_cs_timestamp_query, node),
               cs_scratch_reg_tuple(b, 10, 4));
         }

         cs_add64(b, current, next, 0);
      }

      cs_move64_to(b, current, 0);
      cs_store64(
         b, current, cs_subqueue_ctx_reg(b),
         offsetof(struct panvk_cs_subqueue_context, render.ts_chain.head));
      cs_flush_stores(b);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndRendering)(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   bool suspending = cmdbuf->state.gfx.render.flags & VK_RENDERING_SUSPENDING_BIT;
   VkResult result;

   if (!suspending) {
      struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;

      /* If no draw was performed, we should ensure sample count is valid and that we emit tile size */
      panvk_per_arch(cmd_select_tile_size)(cmdbuf);

      bool clear = fbinfo->zs.clear.z | fbinfo->zs.clear.s;
      for (unsigned i = 0; i < fbinfo->rt_count; i++)
         clear |= fbinfo->rts[i].clear;

      if (clear && !inherits_render_ctx(cmdbuf)) {
         result = get_fb_descs(cmdbuf);
         if (result != VK_SUCCESS)
            return;
      }

      /* Flush the last occlusion query before ending the render pass if
       * this query has ended while we were inside the render pass. */
      if (cmdbuf->state.gfx.render.oq.last !=
          cmdbuf->state.gfx.occlusion_query.syncobj) {
         result = wrap_prev_oq(cmdbuf);
         if (result != VK_SUCCESS)
            return;
      }

      if (cmdbuf->state.gfx.render.fbds.gpu || inherits_render_ctx(cmdbuf)) {
         flush_tiling(cmdbuf);
         issue_fragment_jobs(cmdbuf);

         handle_deferred_queries(cmdbuf);
      }
   } else if (!inherits_render_ctx(cmdbuf)) {
      /* If we're suspending the render pass and we didn't inherit the render
       * context, we need to emit it now, so it's available when the render pass
       * is resumed. */
      VkResult result = get_render_ctx(cmdbuf);
      if (result != VK_SUCCESS)
         return;
   }

   memset(&cmdbuf->state.gfx.render.fbds, 0,
          sizeof(cmdbuf->state.gfx.render.fbds));
   memset(&cmdbuf->state.gfx.render.oq, 0, sizeof(cmdbuf->state.gfx.render.oq));
   cmdbuf->state.gfx.render.tiler = 0;

   /* If we're finished with this render pass, make sure we reset the flags
    * so any barrier encountered after EndRendering() doesn't try to flush
    * draws. */
   cmdbuf->state.gfx.render.flags = 0;
   cmdbuf->state.gfx.render.suspended = suspending;

   /* If we're not suspending, we need to resolve attachments. */
   if (!suspending)
      panvk_per_arch(cmd_resolve_attachments)(cmdbuf);

   struct panvk_instr_end_args instr_info = {
      .render = {
         .flags = cmdbuf->state.gfx.render.flags,
         .fb = &cmdbuf->state.gfx.render.fb.info,
      }};
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   panvk_per_arch(panvk_instr_end_work_async)(
      PANVK_SUBQUEUE_VERTEX_TILER, cmdbuf, PANVK_INSTR_WORK_TYPE_RENDER,
      &instr_info, cs_defer(dev->csf.sb.all_iters_mask, 0));
   panvk_per_arch(panvk_instr_end_work_async)(
      PANVK_SUBQUEUE_FRAGMENT, cmdbuf, PANVK_INSTR_WORK_TYPE_RENDER,
      &instr_info, cs_defer(dev->csf.sb.all_iters_mask, 0));
}
