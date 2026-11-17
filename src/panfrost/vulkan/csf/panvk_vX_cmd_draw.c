/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2024 Arm Ltd.
 * Copyright © 2026 NXP
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
#include "panvk_gpu_prerast.h"
#include "panvk_cmd_frame_shaders.h"
#include "panvk_cmd_meta.h"
#include "panvk_cmd_precomp.h"
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
#include "vk_android.h"
#include "vk_format.h"
#include "vk_meta.h"
#include "vk_pipeline_layout.h"
#include "vk_render_pass.h"
#include "poly/geometry.h"

static bool
render_needs_zs_crc_ext(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
   struct pan_fb_desc_info info = {
      .fb = &render->fb.layout,
      .store = &render->fb.store,
   };
   struct pan_fb_desc_info info_spill = {
      .fb = &render->fb.layout,
      .store = &render->fb.spill.store,
   };

   return GENX(pan_fb_needs_zs_crc_ext)(&info) ||
          GENX(pan_fb_needs_zs_crc_ext)(&info_spill);
}

static bool
render_get_crc_info(struct panvk_cmd_buffer *cmdbuf,
                    struct pan_fb_crc_rt_info *crc_info)
{
   struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
   struct pan_fb_desc_info fbd_info = {
      .fb = &render->fb.layout,
      .store = &render->fb.store,
   };
   return GENX(pan_fb_get_crc_rt_info)(&fbd_info, crc_info);
}

static bool
render_needs_crc_patch(struct panvk_cmd_buffer *cmdbuf)
{
   struct pan_fb_crc_rt_info crc_info;
   return render_get_crc_info(cmdbuf, &crc_info);
}

#if PAN_ARCH < 14
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
      panvk_get_csif_props(dev);

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

      cs_add_imm64(&b, fbd_addr, cs_sr_reg64(&b, FRAGMENT, FBD_POINTER), 0);

      cs_while(&b, MALI_CS_CONDITION_GREATER, fbd_count) {
         /* provoking_vertex flag is bit 14 of word 11 */
         unsigned offset = 11 * 4;
         cs_load32_to(&b, scratch_reg, fbd_addr, offset);
         cs_flush_loads(&b);
         cs_add_imm32(&b, scratch_reg, scratch_reg, -(1 << 14));
         cs_store32(&b, scratch_reg, fbd_addr, offset);
         cs_flush_stores(&b);

         cs_add_imm32(&b, fbd_count, fbd_count, -1);
         cs_add_imm64(&b, fbd_addr, fbd_addr, fbd_sz);
      }
   }

   assert(cs_is_valid(&b));
   cs_end(&b);
   cs_builder_fini(&b);

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
   const bool has_zs_ext = render_needs_zs_crc_ext(cmdbuf);
   return get_fn_set_fbds_provoking_vertex_idx(
      has_zs_ext, cmdbuf->state.gfx.render.fb.layout.rt_count);
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

   const uint32_t fns_bo_size = PROVOKING_VERTEX_FN_MAX_SIZE * 2 * MAX_RTS;
   VkResult result = panvk_priv_bo_create(
      dev, fns_bo_size,
      panvk_device_adjust_bo_flags(dev, PAN_KMOD_BO_FLAG_WB_MMAP),
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

   panvk_priv_bo_flush(dev->draw_ctx->fns_bo, 0, fns_bo_size);

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
#endif /* PAN_ARCH < 14 */

static void
prepare_vi(struct panvk_cmd_buffer *cmdbuf)
{
   if (!dyn_gfx_state_dirty(cmdbuf, VI) &&
       !dyn_gfx_state_dirty(cmdbuf, VI_BINDINGS_VALID))
      return;

   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_vertex_input_state *vi = dyns->vi;

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
   }
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
         cfg.offset += cmdbuf->state.gfx.vi.base_instance * stride;

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

static VkResult
prepare_vs_driver_set(struct panvk_cmd_buffer *cmdbuf, uint32_t repeat_count)
{
   const struct panvk_shader_desc_info *vs_desc_info =
      &cmdbuf->state.gfx.vs.shader->desc_info;
   struct panvk_shader_desc_state *vs_desc_state = &cmdbuf->state.gfx.vs.desc;
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_vertex_input_state *vi = dyns->vi;

   uint32_t vb_count = 0;
   u_foreach_bit(i, vi->attributes_valid)
      vb_count = MAX2(vi->attributes[i].binding + 1, vb_count);

   uint32_t vb_offset = vs_desc_info->dyn_bufs.count + MAX_VS_ATTRIBS + 1;
   uint32_t desc_count = vb_offset + vb_count;

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
            /* Write a specialized AttributeDescriptor and rely on OOB behavior */
            pan_cast_and_pack(&descs[i], ATTRIBUTE, cfg) {
               cfg.table = 17; /* Invalid table, ensuring OOB access */
               cfg.format = (MALI_R16F << 12) | MALI_RGB_COMPONENT_ORDER_RGBA;
            }
         }
      }

      /* Dummy sampler always comes right after the vertex attribs. */
      pan_cast_and_pack(&descs[MAX_VS_ATTRIBS], SAMPLER, cfg) {
         cfg.clamp_integer_array_indices = false;
      }

      panvk_per_arch(cmd_fill_dyn_bufs)(
         desc_state, vs_desc_info,
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

/* Only valid to call after prepare_vi() */
static bool
vs_desc_dirty(struct panvk_cmd_buffer *cmdbuf)
{
   /* If any of the attributes change on base instance and the base instance
    * has changed, we need to re-emit regardless of what API state is dirty.
    * prepare_draw() ensures that BASE_INSTANCE is always dirty for indirect
    * draws.
    */
   if (cmdbuf->state.gfx.vi.attribs_changing_on_base_instance &&
       gfx_state_dirty(cmdbuf, BASE_INSTANCE))
      return true;

   return dyn_gfx_state_dirty(cmdbuf, VI) ||
          dyn_gfx_state_dirty(cmdbuf, VI_BINDINGS_VALID) ||
          dyn_gfx_state_dirty(cmdbuf, VI_BINDING_STRIDES) ||
          gfx_state_dirty(cmdbuf, VB) || gfx_state_dirty(cmdbuf, VS) ||
          gfx_state_dirty(cmdbuf, DESC_STATE);
}

static VkResult
prepare_vs_desc(struct panvk_cmd_buffer *cmdbuf,
                const struct panvk_draw_info *draw)
{
   prepare_vi(cmdbuf);

   if (!vs_desc_dirty(cmdbuf))
      return VK_SUCCESS;

   cmdbuf->state.gfx.vs.desc_repeat_count = 0;
   if (draw->indirect.buffer_dev_addr) {
      /* BASE_INSTANCE is always dirty for indirect draws so it's safe to look
       * at the draw info here.
       */
      assert(gfx_state_dirty(cmdbuf, BASE_INSTANCE));
      if (cmdbuf->state.gfx.vi.attribs_changing_on_base_instance)
         cmdbuf->state.gfx.vs.desc_repeat_count = draw->indirect.draw_count;
   }

   const uint32_t repeat_count =
      MAX2(cmdbuf->state.gfx.vs.desc_repeat_count, 1);

   VkResult result = prepare_vs_driver_set(cmdbuf, repeat_count);
   if (result != VK_SUCCESS)
      return result;

   const struct panvk_shader_desc_info *vs_desc_info =
      &cmdbuf->state.gfx.vs.shader->desc_info;
   const struct panvk_descriptor_state *desc_state =
      &cmdbuf->state.gfx.desc_state;
   struct panvk_shader_desc_state *vs_desc_state = &cmdbuf->state.gfx.vs.desc;

   result = panvk_per_arch(cmd_prepare_shader_res_table)(
      cmdbuf, desc_state, vs_desc_info, vs_desc_state, repeat_count);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

static const struct panvk_shader_variant *
raster_vs_variant(const struct panvk_cmd_buffer *cmdbuf, bool generated)
{
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   if (gs)
      return &gs->variants[PANVK_GS_VARIANT_PASSTHROUGH];

   return generated
      ? panvk_shader_gpu_prerast_variant(cmdbuf->state.gfx.vs.shader,
                                         PANVK_VS_VARIANT_GPU_PASSTHROUGH)
      : panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
}

static void
emit_varying_descs(const struct panvk_cmd_buffer *cmdbuf,
                   struct mali_attribute_packed *descs)
{
   const struct panvk_shader_variant *vs = raster_vs_variant(cmdbuf, false);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));

   const struct pan_varying_layout *vs_layout = &vs->info.varyings.formats;
   const struct pan_varying_layout *fs_format = &fs->info.varyings.formats;
   pan_varying_layout_require_layout(vs_layout);
   pan_varying_layout_require_format(fs_format);

   for (uint32_t i = 0; i < fs_format->count; i++) {
      const struct pan_varying_slot *fs_slot =
         pan_varying_layout_slot_at(fs_format, i);

      /* Skip empty slots and special varyings. */
      if (!fs_slot || fs_slot->section != PAN_VARYING_SECTION_GENERIC)
         continue;

      unsigned offset = 0;
      enum pipe_format format = PIPE_FORMAT_NONE;

      const struct pan_varying_slot *vs_slot =
         pan_varying_layout_find_slot(vs_layout, fs_slot->location);
      if (vs_slot) {
         nir_alu_type base_type = nir_alu_type_get_base_type(fs_slot->alu_type);
         nir_alu_type bit_size = nir_alu_type_get_type_size(vs_slot->alu_type);

         offset = vs_slot->offset;
         format = pan_varying_format(base_type | bit_size, vs_slot->ncomps);
      }

      pan_pack(&descs[i], ATTRIBUTE, cfg) {
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_VERTEX_PACKET;
         cfg.offset_enable = false;
         cfg.format = GENX(pan_format_from_pipe_format)(format)->hw;
         cfg.table = 61;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_VERTEX;
         cfg.offset = 1024 + offset;
         /* On v12+, the hardware-controlled buffer is at index 1 for varyings */
         cfg.buffer_index = PAN_ARCH >= 12 ? 1 : 0;
         cfg.attribute_stride = vs_layout->generic_size_B;
         cfg.packet_stride = vs_layout->generic_size_B + 16;
      }
   }
}

static VkResult
prepare_fs_driver_set(struct panvk_cmd_buffer *cmdbuf)
{
   const struct panvk_shader_desc_info *fs_desc_info =
      &cmdbuf->state.gfx.fs.shader->desc_info;
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   const struct panvk_descriptor_state *desc_state =
      &cmdbuf->state.gfx.desc_state;
   /* If the shader is using LD_VAR_BUF[_IMM], we do not have to set up
    * Attribute Descriptors for varying loads. */
   const uint32_t desc_count =
      fs_desc_info->fs_varying_attr_desc_count +
      fs_desc_info->dyn_bufs.count + 1;
   struct pan_ptr driver_set = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, desc_count * PANVK_DESCRIPTOR_SIZE, PANVK_DESCRIPTOR_SIZE);
   struct panvk_opaque_desc *descs = driver_set.cpu;

   if (desc_count && !driver_set.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   if (fs_desc_info->fs_varying_attr_desc_count > 0)
      emit_varying_descs(cmdbuf, (struct mali_attribute_packed *)(&descs[0]));

   /* Dummy sampler always comes right after the varyings. */
   const uint32_t sampler_idx = fs_desc_info->fs_varying_attr_desc_count;
   pan_cast_and_pack(&descs[sampler_idx], SAMPLER, cfg) {
      cfg.clamp_integer_array_indices = false;
   }

   panvk_per_arch(cmd_fill_dyn_bufs)(
      desc_state, fs_desc_info,
      (struct mali_buffer_packed *)(&descs[sampler_idx + 1]));

   fs_desc_state->driver_set.dev_addr = driver_set.gpu;
   fs_desc_state->driver_set.size = desc_count * PANVK_DESCRIPTOR_SIZE;
   gfx_state_set_dirty(cmdbuf, DESC_STATE);
   return VK_SUCCESS;
}

static bool
fs_desc_dirty(struct panvk_cmd_buffer *cmdbuf)
{
   return fs_user_dirty(cmdbuf) ||
          gfx_state_dirty(cmdbuf, VS) ||
          gfx_state_dirty(cmdbuf, DESC_STATE);
}

static VkResult
prepare_fs_desc(struct panvk_cmd_buffer *cmdbuf)
{
   if (!fs_desc_dirty(cmdbuf))
      return VK_SUCCESS;

   const struct panvk_shader *fs = get_fs(cmdbuf);
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;

   if (!fs) {
      memset(fs_desc_state, 0, sizeof(*fs_desc_state));
      return VK_SUCCESS;
   }

   const struct panvk_descriptor_state *desc_state =
      &cmdbuf->state.gfx.desc_state;

   VkResult result = prepare_fs_driver_set(cmdbuf);
   if (result != VK_SUCCESS)
      return result;

   result = panvk_per_arch(cmd_prepare_shader_res_table)(
      cmdbuf, desc_state, &fs->desc_info, fs_desc_state, 1);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

static VkResult
prepare_descs(struct panvk_cmd_buffer *cmdbuf,
              const struct panvk_draw_info *draw)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *fs = get_fs(cmdbuf);
   struct panvk_descriptor_state *desc_state =
      &cmdbuf->state.gfx.desc_state;
   VkResult result;

   if (gfx_state_dirty(cmdbuf, DESC_STATE) ||
       gfx_state_dirty(cmdbuf, VS) ||
       fs_user_dirty(cmdbuf)) {
      uint32_t used_set_mask = vs->desc_info.used_set_mask;
      used_set_mask |= fs ? fs->desc_info.used_set_mask : 0;

      result = panvk_per_arch(cmd_prepare_push_descs)(cmdbuf, desc_state,
                                                      used_set_mask);
      if (result != VK_SUCCESS)
         return result;
   }

   result = prepare_vs_desc(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return result;

   result = prepare_fs_desc(cmdbuf);
   if (result != VK_SUCCESS)
      return result;

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
translate_prim(enum mesa_prim prim)
{
   switch (prim) {
   case MESA_PRIM_POINTS:
      return MALI_DRAW_MODE_POINTS;
   case MESA_PRIM_LINES:
      return MALI_DRAW_MODE_LINES;
   case MESA_PRIM_LINE_STRIP:
      return MALI_DRAW_MODE_LINE_STRIP;
   case MESA_PRIM_TRIANGLES:
      return MALI_DRAW_MODE_TRIANGLES;
   case MESA_PRIM_TRIANGLE_STRIP:
      return MALI_DRAW_MODE_TRIANGLE_STRIP;
   case MESA_PRIM_TRIANGLE_FAN:
      return MALI_DRAW_MODE_TRIANGLE_FAN;
   case MESA_PRIM_LINES_ADJACENCY:
      return MALI_DRAW_MODE_LINES_ADJACENCY;
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      return MALI_DRAW_MODE_LINE_STRIP_ADJACENCY;
   case MESA_PRIM_TRIANGLES_ADJACENCY:
      return MALI_DRAW_MODE_TRIANGLES_ADJACENCY;
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return MALI_DRAW_MODE_TRIANGLE_STRIP_ADJACENCY;
   default:
      UNREACHABLE("Invalid primitive type");
   }
}

static VkResult
update_tls(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_tls_state *state = &cmdbuf->state.tls;
   const struct panvk_shader_variant *vs = raster_vs_variant(cmdbuf, false);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
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
build_blend(struct panvk_cmd_buffer *cmdbuf,
            const struct panvk_shader_variant *fs, uint64_t *bds_gpu)
{
   uint32_t bd_count = cmdbuf->state.gfx.render.fb.layout.rt_count;
   struct pan_ptr ptr = panvk_cmd_alloc_desc_array(cmdbuf, bd_count, BLEND);
   struct mali_blend_packed *bds = ptr.cpu;

   if (bd_count && !ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   if (fs) {
      VkResult result = panvk_per_arch(blend_emit_descs)(cmdbuf, bds);
      if (result != VK_SUCCESS)
         return result;
   } else {
      for (unsigned i = 0; i < bd_count; i++) {
         pan_pack(&bds[i], BLEND, cfg) {
            cfg.enable = false;
            cfg.internal.mode = MALI_BLEND_MODE_OFF;
         }
      }
   }

   *bds_gpu = ptr.gpu;

   return VK_SUCCESS;
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

   uint64_t bds_gpu;
   uint32_t bd_count = cmdbuf->state.gfx.render.fb.layout.rt_count;
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   VkResult result = build_blend(cmdbuf, fs, &bds_gpu);
   if (result != VK_SUCCESS)
      return result;

   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   cs_update_vt_ctx(b)
      cs_move64_to(b, cs_sr_reg64(b, IDVS, BLEND_DESC), bds_gpu | bd_count);

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
       dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE) ||
       dyn_gfx_state_dirty(cmdbuf, VP_DEPTH_CLAMP_RANGE)) {
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
                           &cmdbuf->vk.dynamic_graphics_state.vp,
                           &z_min, &z_max);
         cfg.min_depth = z_min;
         cfg.max_depth = z_max;
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
       dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE) ||
       dyn_gfx_state_dirty(cmdbuf, VP_DEPTH_CLAMP_RANGE)) {
      float z_min, z_max;
      panvk_depth_range(&cmdbuf->state.gfx,
                        &cmdbuf->vk.dynamic_graphics_state.vp, &z_min, &z_max);
      cs_move32_to(b, cs_sr_reg32(b, IDVS, LOW_DEPTH_CLAMP), fui(z_min));
      cs_move32_to(b, cs_sr_reg32(b, IDVS, HIGH_DEPTH_CLAMP), fui(z_max));
   }
}
#endif

static void
prepare_tiler_primitive_size(struct panvk_cmd_buffer *cmdbuf,
                             const struct panvk_draw_info *draw)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   float primitive_size;

   if (!dyn_gfx_state_dirty(cmdbuf, RS_LINE_WIDTH) &&
       !gfx_state_dirty(cmdbuf, VS) &&
       !gfx_state_dirty(cmdbuf, IDVS))
      return;

   switch (u_reduced_prim(cmdbuf->state.gfx.idvs.prim)) {
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
   case MESA_PRIM_POINTS: {
      const struct panvk_shader_variant *vs = raster_vs_variant(cmdbuf, false);

      if (vs->info.vs.writes_point_size)
         return;

      primitive_size = 1.0f;
      break;
   }
#endif
   case MESA_PRIM_LINES:
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
   const bool has_zs_ext = render_needs_zs_crc_ext(cmdbuf);
   return get_fbd_size(has_zs_ext, cmdbuf->state.gfx.render.fb.layout.rt_count);
}

static uint32_t
calc_render_descs_size(struct panvk_cmd_buffer *cmdbuf)
{
   uint32_t fbd_count = calc_enabled_layer_count(cmdbuf);
   uint32_t td_count = DIV_ROUND_UP(cmdbuf->state.gfx.render.layer_count,
                                    MAX_LAYERS_PER_TILER_DESC);

   /* v14+ supports a maximum of one tiler descriptor.
    * This should be enough since the driver reports maxFramebufferLayers
    * and maxMultiviewViewCount lower or equal to MAX_LAYERS_PER_TILER_DESC. */
   assert(PAN_ARCH < 14 || td_count <= 1);
   static_assert(
      PAN_ARCH < 14 || MAX_FRAMEBUFFER_LAYERS <= MAX_LAYERS_PER_TILER_DESC,
      "MAX_FRAMEBUFFER_LAYERS must be <= max amount of layers a Tiler descriptor can index");
   assert(pan_max_multiview_view_count(PAN_ARCH) <= MAX_LAYERS_PER_TILER_DESC);

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
   cs_add_imm32(b, ptr_lo, ptr_lo, size);
   cs_add_imm32(b, pos, pos, size);

   /* Wrap-around. */
   if (likely(wrap_around)) {
      cs_add_imm32(b, scratch_reg, pos, -RENDER_DESC_RINGBUF_SIZE);

      cs_if(b, MALI_CS_CONDITION_GEQUAL, scratch_reg) {
         cs_add_imm32(b, ptr_lo, ptr_lo, -RENDER_DESC_RINGBUF_SIZE);
         cs_add_imm32(b, pos, pos, -RENDER_DESC_RINGBUF_SIZE);
      }
   }

   cs_store(
      b, cs_scratch_reg_tuple(b, 2, 3), cs_subqueue_ctx_reg(b),
      BITFIELD_MASK(3),
      offsetof(struct panvk_cs_subqueue_context, render.desc_ringbuf.ptr));
   cs_flush_stores(b);
}

static bool get_first_provoking_vertex(struct panvk_cmd_buffer *cmdbuf);

/* Write layer count information to the subqueue ctx so that it can be read by
 * secondary cmdbufs */
static void
prepare_layer_count_inherited_ctx(struct panvk_cmd_buffer *cmdbuf)
{
   /* If multiview is enabled, then the view mask is included in
    * VkCommandBufferInheritanceRenderingInfo, so the layer count will be
    * known at record-time in the secondary cmdbuf */
   if (cmdbuf->state.gfx.render.view_mask)
      return;

   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   uint32_t layer_count = cmdbuf->state.gfx.render.layer_count;

#if PAN_ARCH >= 14
   struct cs_index layer_count_reg = cs_scratch_reg32(b, 0);
   cs_move32_to(b, layer_count_reg, layer_count);
   cs_store32(b, layer_count_reg, cs_subqueue_ctx_reg(b),
              offsetof(struct panvk_cs_subqueue_context, render.layer_count));
#else
   uint32_t td_count = DIV_ROUND_UP(layer_count, MAX_LAYERS_PER_TILER_DESC);
   uint32_t last_td_layers =
      layer_count - (td_count - 1) * MAX_LAYERS_PER_TILER_DESC;
   uint32_t last_td_view_mask = BITFIELD_RANGE(0, last_td_layers);

   /* For RUN_FULLSCREEN, HW expects 0 for all flags. Only scissor_array_enable
    * and the layer selection fields can be set to other values. */
   struct mali_primitive_flags_packed last_td_tiler_flags = {0};
   pan_pack(&last_td_tiler_flags, PRIMITIVE_FLAGS, cfg) {
      /* These default to non-zero */
      cfg.low_depth_cull = false;
      cfg.high_depth_cull = false;
      cfg.view_mask = last_td_view_mask;
   }

   /* The two fields are adjacent, so we can issue a single * write */
   STATIC_ASSERT(
      offsetof(struct panvk_cs_subqueue_context, render.last_td_fullscreen_tiler_flags) ==
      offsetof(struct panvk_cs_subqueue_context, render.td_count) + 4);

   struct cs_index values = cs_scratch_reg64(b, 0);
   struct cs_index td_count_reg = cs_extract32(b, values, 0);
   struct cs_index last_td_tiler_flags_reg = cs_extract32(b, values, 1);

   cs_move32_to(b, td_count_reg, td_count);
   cs_move32_to(b, last_td_tiler_flags_reg, last_td_tiler_flags.opaque[0]);

   cs_store64(b, values, cs_subqueue_ctx_reg(b),
              offsetof(struct panvk_cs_subqueue_context, render.td_count));
#endif
}

static void
account_tiler_work(struct panvk_cmd_buffer *cmdbuf, uint64_t work)
{
   if (UINT64_MAX - cmdbuf->state.tiler_work_estimate < work)
      cmdbuf->state.tiler_work_estimate = UINT64_MAX;
   else
      cmdbuf->state.tiler_work_estimate += work;
}

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
      pan_query_tiler_features(&phys_dev->kmod.dev->props);
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

   account_tiler_work(cmdbuf, td_count);

   const struct pan_fb_layout *fb = &cmdbuf->state.gfx.render.fb.layout;

   /* At this point, we should know sample count and the tile size should have
    * been calculated */
   assert(fb->sample_count > 0 && fb->tile_size_px > 0);

   pan_pack(&tiler_tmpl, TILER_CONTEXT, cfg) {
      ASSERTED unsigned max_levels = tiler_features.max_levels;
      assert(max_levels >= 2);

      /* The tiler chunk start with a header of 64 bytes */
      cfg.hierarchy_mask = panvk_select_tiler_hierarchy_mask(
         phys_dev, &cmdbuf->state.gfx, phys_dev->csf.tiler.chunk_size - 64);
      cfg.fb_width = fb->width_px;
      cfg.fb_height = fb->height_px;

#if PAN_ARCH >= 12
      cfg.effective_tile_size = fb->tile_size_px;
#endif

      cfg.sample_pattern = pan_sample_pattern(fb->sample_count);

      cfg.first_provoking_vertex = get_first_provoking_vertex(cmdbuf);

      /* This will be overloaded. */
      cfg.layer_count = 1;

#if PAN_ARCH < 14
      cfg.layer_offset = 0;
#endif
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
         cs_add_imm32(b, cs_scratch_reg32(b, 2), cs_scratch_reg32(b, 2),
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
         cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr,
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
         cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr,
                      -pan_size(TILER_CONTEXT));
   } else if (full_td_count > 1) {
      cs_update_vt_ctx(b)
         cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr,
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

         cs_add_imm32(b, cs_scratch_reg32(b, 4), cs_scratch_reg32(b, 4),
                      MAX_LAYERS_PER_TILER_DESC << 8);

         cs_add_imm32(b, counter_reg, counter_reg, -1);
         cs_update_vt_ctx(b)
            cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr,
                         -pan_size(TILER_CONTEXT));
      }
   }

   prepare_layer_count_inherited_ctx(cmdbuf);

   /* Flush all stores to tiler_ctx_addr. */
   cs_flush_stores(b);

   cs_next_iter_sb(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER,
                   cs_scratch_reg_tuple(b, 0, 2));

   cs_vt_start(b, cs_now());
   return VK_SUCCESS;
}

static struct pan_tiler_context
get_tiler_context(struct panvk_cmd_buffer *cmdbuf, uint32_t layer)
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

   return tiler_ctx;
}

#if PAN_ARCH >= 14
static void
init_layer_fragment_state(const struct pan_fb_desc_info *info,
                          const struct pan_ptr fbd, const bool has_zs_crc_ext)
{
   const struct pan_fb_layout *fb = info->fb;
   const struct pan_fb_load *load = info->load;
   const struct pan_fb_store *store = info->store;
   const struct pan_fb_clean_tile ct = GENX(pan_fb_get_clean_tile)(info);

   struct panvk_fb_layer_state fbd_data = {0};
   fbd_data.tiler = info->tiler_ctx->valhall.desc;

   /* layer_index in flags0 is used to select the right primitive list in
    * the tiler context, and frame_arg is the value that's passed to the
    * fragment shader through r62-r63, which we use to pass gl_Layer. Since
    * the layer_idx only takes 8-bits, we might use the extra 56-bits we
    * have in frame_argument to pass other information to the fragment
    * shader at some point.
    */
   fbd_data.frame_argument = info->layer;

   /* Layer offset is unused on v14+. */
   assert(info->tiler_ctx->valhall.layer_offset == 0);

   struct pan_fb_crc_rt_info crc_info;
   const bool has_crc = GENX(pan_fb_get_crc_rt_info)(info, &crc_info);
   pan_pack(&fbd_data.flags0, FRAGMENT_FLAGS_0, cfg) {
      const bool force_clean_tile = ct.rts || ct.zs || ct.s || has_crc;
      cfg.pre_frame_0 = pan_fix_frame_shader_mode(info->frame_shaders.modes[0],
                                                  force_clean_tile);
      cfg.pre_frame_1 = pan_fix_frame_shader_mode(info->frame_shaders.modes[1],
                                                  force_clean_tile);
      cfg.post_frame = info->frame_shaders.modes[2];

      /* Enabling prepass without pipelineing is generally not good for
       * performance, so disable HSR in that case.
       */
      cfg.hsr_prepass_enable =
         info->allow_hsr_prepass && pan_fb_can_pipeline_zs(fb);
      cfg.hsr_prepass_interleaving_enable = pan_fb_can_pipeline_zs(fb);
      cfg.hsr_prepass_filter_enable = true;
      cfg.hsr_hierarchical_optimizations_enable = true;

      cfg.internal_layer_index = info->layer;
   }

   pan_pack(&fbd_data.flags2, FRAGMENT_FLAGS_2, cfg) {
      if (fb->s_format != PIPE_FORMAT_NONE) {
         cfg.s_clear =
            load && pan_target_has_clear(&load->s) ? load->s.clear.stencil : 0;
         cfg.s_write_enable = store && store->s.store;
      }

      if (fb->z_format != PIPE_FORMAT_NONE) {
         cfg.z_internal_format = pan_get_z_internal_format(fb->z_format);
         cfg.z_write_enable = store && store->zs.store;
      } else {
         cfg.z_internal_format = MALI_Z_INTERNAL_FORMAT_D24;
         assert(!store || !store->zs.store);
      }

      if (has_crc) {
         cfg.crc_read_enable = true;
         cfg.crc_write_enable = true;
         cfg.empty_tile_write_enable = true;
         cfg.empty_tile_read_enable = fb->rt_count == 1;
      }
   }

   fbd_data.z_clear =
      util_bitpack_float(fb->z_format != PIPE_FORMAT_NONE && load && load &&
                               pan_target_has_clear(&load->z)
                            ? load->z.clear.depth
                            : 0);

   fbd_data.dcd_pointer = info->frame_shaders.dcd_pointer;

   /* Set the DBD and RTD pointers. Both must be 64-bytes aligned. */
   {
      uint64_t out_gpu_addr =
         fbd.gpu + ALIGN_POT(sizeof(struct panvk_fb_layer_state), 64);

      if (has_zs_crc_ext) {
         fbd_data.dbd_pointer = out_gpu_addr;
         assert(fbd_data.dbd_pointer % 64 == 0);
         out_gpu_addr += pan_size(ZS_CRC_EXTENSION);
      }

      fbd_data.rtd_pointer = out_gpu_addr;
      assert(fbd_data.rtd_pointer % 64 == 0);
   }

   memcpy(fbd.cpu, &fbd_data, sizeof(fbd_data));
}
#endif /* PAN_ARCH >= 14 */

#if PAN_ARCH == 10
static void
patch_crc_valid(struct cs_builder *b, const struct pan_fb_crc_rt_info *crc_info,
                uint32_t crc_fbd_flags, uint32_t crc_rtd_flags,
                struct cs_index fbd_ptr_reg, uint64_t fb_size,
                uint32_t fbd_stride, uint32_t enabled_layer_count)
{
   if (crc_info->rt == -1 || !crc_info->header_addr)
      return;

   /* Patch FBD with CRC state immediately before use. Invalid state uses
    * write-only initialization; valid state enables TE. */
   struct cs_index header_addr_reg = cs_scratch_reg64(b, 2);
   struct cs_index fbd_flags = cs_scratch_reg32(b, 4);
   struct cs_index rtd_flags = cs_scratch_reg32(b, 5);
   struct cs_index valid = cs_scratch_reg32(b, 6);

   const int flags_offset = 12 * sizeof(uint32_t);
   const int crc_rt_flags_offset = fb_size + pan_size(ZS_CRC_EXTENSION) +
                                   crc_info->rt * pan_size(RENDER_TARGET) +
                                   sizeof(uint32_t);

   const uint32_t valid_fbd_flags = crc_fbd_flags;
   const uint32_t invalid_fbd_flags = (valid_fbd_flags & BITFIELD_MASK(28)) |
                                      BITFIELD_BIT(29) | /* ETE write */
                                      BITFIELD_BIT(31);  /* CRC write */

   const uint32_t valid_rtd_flags = crc_rtd_flags;
   const uint32_t invalid_rtd_flags =
      valid_rtd_flags | BITFIELD_BIT(31); /* Clean-tile write */

   cs_move64_to(b, header_addr_reg, crc_info->header_addr);
   cs_load32_to(b, valid, header_addr_reg, PAN_CRC_VALID_OFFSET);

   /* Invalid: initialize CRC and ETE by setting write only. */
   cs_move32_to(b, fbd_flags, invalid_fbd_flags);
   cs_move32_to(b, rtd_flags, invalid_rtd_flags);
   /* Valid: enable both read and write. */
   cs_if(b, MALI_CS_CONDITION_NEQUAL, valid) {
      cs_move32_to(b, fbd_flags, valid_fbd_flags);
      cs_move32_to(b, rtd_flags, valid_rtd_flags);
   }

   for (uint32_t i = 0; i < enabled_layer_count; i++) {
      /* Store new CRC/ETE read/write flags to the FBD. */
      cs_store32(b, fbd_flags, fbd_ptr_reg, flags_offset);
      cs_store32(b, rtd_flags, fbd_ptr_reg, crc_rt_flags_offset);

      if (i + 1 < enabled_layer_count)
         cs_add_imm64(b, fbd_ptr_reg, fbd_ptr_reg, fbd_stride);
   }
}

static void
mark_crc_valid_after_fragment(struct cs_builder *b,
                              struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct pan_fb_crc_rt_info crc_info;
   if (!pan_fb_is_fully_covered(&cmdbuf->state.gfx.render.fb.layout) ||
       !render_get_crc_info(cmdbuf, &crc_info))
      return;

   struct cs_index state_addr_reg = cs_scratch_reg64(b, 0);
   struct cs_index valid = cs_scratch_reg32(b, 3);
   cs_move64_to(b, state_addr_reg, crc_info.header_addr);
   cs_load32_to(b, valid, state_addr_reg, PAN_CRC_VALID_OFFSET);

   cs_if(b, MALI_CS_CONDITION_EQUAL, valid) {
      cs_wait_slots(b, dev->csf.sb.all_iters_mask);

      struct cs_index counter_reg = cs_scratch_reg32(b, 4);
      cs_load32_to(b, counter_reg, cs_subqueue_ctx_reg(b),
                   TILER_OOM_CTX_FIELD_OFFSET(counter));

      cs_if(b, MALI_CS_CONDITION_EQUAL, counter_reg) {
         cs_move32_to(b, valid, 1);
         cs_store32(b, valid, state_addr_reg, PAN_CRC_VALID_OFFSET);
      }

      cs_flush_stores(b);
   }
}
#endif /* PAN_ARCH == 10 */

#if PAN_ARCH >= 11
static void
patch_crc_init(struct cs_builder *b, struct pan_fb_desc_info *fbd_info,
               struct cs_index fbd_ptr_reg, uint64_t fb_size,
               uint32_t fbd_stride, uint32_t enabled_layer_count)
{
   struct pan_fb_crc_rt_info crc_info;
   if (!GENX(pan_fb_get_crc_rt_info)(fbd_info, &crc_info))
      return;

   struct cs_index header_addr = cs_scratch_reg64(b, 2);
   struct cs_index crc_init = cs_scratch_reg32(b, 4);
   struct cs_index mask = cs_scratch_reg32(b, 5);
   struct cs_index clear_color_lo = cs_scratch_reg32(b, 6);

   const int crc_clear_color_lo_offset = fb_size + 2 * sizeof(uint32_t);

   cs_move64_to(b, header_addr, crc_info.header_addr);
   cs_load32_to(b, crc_init, header_addr, PAN_CRC_INIT_OFFSET);

   cs_move32_to(b, mask, PAN_CRC_INIT_MASK);
   cs_and32(b, crc_init, crc_init, mask);
   cs_not32(b, mask, mask);

   /* CRC reads and writes stay enabled; changing crc_init invalidates
    * previously generated CRC values.
    */
   for (uint32_t i = 0; i < enabled_layer_count; i++) {
      cs_load32_to(b, clear_color_lo, fbd_ptr_reg, crc_clear_color_lo_offset);
      cs_and32(b, clear_color_lo, clear_color_lo, mask);
      cs_or32(b, clear_color_lo, clear_color_lo, crc_init);
      cs_store32(b, clear_color_lo, fbd_ptr_reg, crc_clear_color_lo_offset);

      if (i + 1 < enabled_layer_count)
         cs_add_imm64(b, fbd_ptr_reg, fbd_ptr_reg, fbd_stride);
   }
}
#endif /* PAN_ARCH >= 11 */

static void
invalidate_unselected_crc_rts(struct cs_builder *b,
                              const struct pan_fb_desc_info *info)
{
   struct pan_fb_crc_rt_info crc_info;
   bool has_crc_rt = GENX(pan_fb_get_crc_rt_info)(info, &crc_info);

   for (uint32_t i = 0; i < info->fb->rt_count; i++) {
      const struct pan_fb_store_target *rt = &info->store->rts[i];

      if (!rt->store || !rt->crc_header_addr)
         continue;

      if (has_crc_rt && crc_info.rt == i)
         continue;

      panvk_per_arch(cmd_invalidate_crc)(b, rt->crc_header_addr);
   }
}

static VkResult
get_fb_descs(struct panvk_cmd_buffer *cmdbuf)
{
   assert(cmdbuf->state.gfx.render.invalidate_inherited_ctx ||
          !inherits_render_ctx(cmdbuf));

   if (cmdbuf->state.gfx.render.fbds.gpu ||
       !cmdbuf->state.gfx.render.layer_count)
      return VK_SUCCESS;

   uint32_t view_mask_temp = cmdbuf->state.gfx.render.view_mask;
   uint32_t enabled_layer_count = calc_enabled_layer_count(cmdbuf);
   uint32_t fbd_sz = calc_fbd_size(cmdbuf);
   uint32_t fbds_sz = enabled_layer_count * fbd_sz;

#if PAN_ARCH >= 14
   const unsigned fbds_alignment = alignof(struct panvk_fb_layer_state);
#else
   const unsigned fbds_alignment = pan_alignment(FRAMEBUFFER);
#endif
   cmdbuf->state.gfx.render.fbds =
      panvk_cmd_alloc_dev_mem(cmdbuf, desc, fbds_sz, fbds_alignment);
   if (!cmdbuf->state.gfx.render.fbds.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;

   /* At this point, we should know sample count and the tile size should have
    * been calculated */
   assert(render->fb.layout.sample_count > 0);
   assert(render->fb.layout.tile_size_px > 0);
   const uint8_t sample_count = render->fb.layout.sample_count;

   bool simul_use =
      cmdbuf->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

   /* The only bit we patch in FBDs is the tiler pointer and CRC. If tiler
    * is not involved (clear job) with no CRC, or if the update can happen in
    * place (not simultaneous use of the command buffer), we can avoid the copy.
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
#if PAN_ARCH == 10
   struct pan_fb_crc_rt_info crc_info;
   const bool has_crc_patch = render_get_crc_info(cmdbuf, &crc_info);
   uint32_t crc_fbd_flags = 0;
   uint32_t crc_rtd_flags = 0;
#elif PAN_ARCH >= 11
   const bool has_crc_patch = render_needs_crc_patch(cmdbuf);
#endif
   const bool copy_fbds =
      simul_use && (cmdbuf->state.gfx.render.tiler || has_crc_patch);
   const bool has_zs_crc_ext = render_needs_zs_crc_ext(cmdbuf);
   struct pan_ptr fbds = cmdbuf->state.gfx.render.fbds;
   uint32_t fbd_flags = 0;

   /* We prepare all FB descriptors upfront. For multiview, only create FBDs
    * for enabled views. */
   bool multiview = cmdbuf->state.gfx.render.view_mask;

   struct pan_tiler_context tiler_ctx;
   struct pan_fb_desc_info fbd_info = {
      .fb = &render->fb.layout,
      .load = &render->fb.load,
      .store = &render->fb.store,
      .force_zs_crc_ext = has_zs_crc_ext,
      .sample_pos_array_pointer = dev->sample_positions->addr.dev +
         pan_sample_positions_offset(pan_sample_pattern(sample_count)),
      .provoking_vertex_first = get_first_provoking_vertex(cmdbuf),
      .allow_hsr_prepass = PAN_ARCH >= 13 && PANVK_DEBUG(HSR_PREPASS),
      .tiler_ctx = &tiler_ctx,
   };

   VkResult result = panvk_per_arch(cmd_get_frame_shaders)(
      cmdbuf, fbd_info.fb, fbd_info.load, &render->fb.resolve,
      &fbd_info.frame_shaders);
   if (result != VK_SUCCESS)
      return result;

#if PAN_ARCH >= 14
   const unsigned fb_sz = ALIGN_POT(sizeof(struct panvk_fb_layer_state), 64);
#else
   const unsigned fb_sz = pan_size(FRAMEBUFFER);
#endif
   for (uint32_t i = 0; i < enabled_layer_count; i++) {
      uint32_t layer_idx = multiview ? u_bit_scan(&view_mask_temp) : i;

      fbd_info.layer = layer_idx;
      tiler_ctx = get_tiler_context(cmdbuf, layer_idx);

      const struct pan_ptr fbd = pan_ptr_offset(fbds, fbd_sz * i);
      const struct pan_fb_descs fb_descs = {
#if PAN_ARCH <= 13
         .fbd = fbd.cpu,
#endif
         .zs_crc = has_zs_crc_ext ? fbd.cpu + fb_sz : NULL,
         .rts = has_zs_crc_ext ? fbd.cpu + fb_sz + pan_size(ZS_CRC_EXTENSION)
                               : fbd.cpu + fb_sz,
      };
      uint32_t new_fbd_flags = GENX(pan_emit_fb_desc)(&fbd_info, &fb_descs);
#if PAN_ARCH >= 14
      init_layer_fragment_state(&fbd_info, fbd, has_zs_crc_ext);
#endif

      /* Make sure all FBDs have the same flags. */
      assert(i == 0 || new_fbd_flags == fbd_flags);
      fbd_flags = new_fbd_flags;
#if PAN_ARCH == 10
      if (has_crc_patch) {
         uint32_t fbd_word = fb_descs.fbd->opaque[12];
         uint32_t rtd_word = fb_descs.rts[crc_info.rt].opaque[1];

         assert(i == 0 || fbd_word == crc_fbd_flags);
         assert(i == 0 || rtd_word == crc_rtd_flags);

         crc_fbd_flags = fbd_word;
         crc_rtd_flags = rtd_word;
      }
#endif
   }

#if PAN_ARCH >= 14
   /* fbd_flags is unused on v14+. */
   assert(!fbd_flags);
#endif

   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_FRAGMENT);
   invalidate_unselected_crc_rts(b, &fbd_info);

   for (uint32_t ir_pass = 0; ir_pass < PANVK_IR_PASS_COUNT; ir_pass++) {
      struct pan_ptr ir_fbds =
         panvk_cmd_alloc_dev_mem(cmdbuf, desc, fbds_sz, fbds_alignment);

      if (!ir_fbds.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      uint32_t ir_view_mask_temp = cmdbuf->state.gfx.render.view_mask;
      const struct pan_fb_store *ir_store = ir_pass == PANVK_IR_LAST_PASS
                                               ? &render->fb.store
                                               : &render->fb.spill.store;
#if PAN_ARCH == 10
      /* IR processes only part of the original render and cannot maintain
       * the full-frame CRC table, so disable CRC and ETE in copied FBDs.
       */
      struct pan_fb_store ir_store_without_crc = *ir_store;

      for (uint32_t rt = 0; rt < fbd_info.fb->rt_count; rt++)
         ir_store_without_crc.rts[rt].crc_header_addr = 0;

      ir_store = &ir_store_without_crc;
#endif
      for (uint32_t i = 0; i < enabled_layer_count; i++) {
         uint32_t layer_idx = multiview ? u_bit_scan(&ir_view_mask_temp) : i;

         fbd_info.layer = layer_idx;
         tiler_ctx = get_tiler_context(cmdbuf, layer_idx);
         fbd_info.load = ir_pass == PANVK_IR_FIRST_PASS
                            ? &render->fb.load
                            : &render->fb.spill.load;
         fbd_info.store = ir_store;

         VkResult result = panvk_per_arch(cmd_get_frame_shaders)(
            cmdbuf, fbd_info.fb, fbd_info.load,
            ir_pass == PANVK_IR_LAST_PASS ? &render->fb.resolve : NULL,
            &fbd_info.frame_shaders);
         if (result != VK_SUCCESS)
            return result;

         const struct pan_ptr fbd = pan_ptr_offset(ir_fbds, fbd_sz * i);
         const struct pan_fb_descs fb_descs = {
#if PAN_ARCH <= 13
            .fbd = fbd.cpu,
#endif
            .zs_crc = has_zs_crc_ext ? fbd.cpu + fb_sz : NULL,
            .rts = has_zs_crc_ext ? fbd.cpu + fb_sz + pan_size(ZS_CRC_EXTENSION)
                                  : fbd.cpu + fb_sz,
         };
         ASSERTED uint32_t new_fbd_flags =
            GENX(pan_emit_fb_desc)(&fbd_info, &fb_descs);
#if PAN_ARCH >= 14
         init_layer_fragment_state(&fbd_info, fbd, has_zs_crc_ext);
#endif

         /* Make sure all FBDs have the same flags. */
         assert(new_fbd_flags == fbd_flags);
      }

      static_assert(ARRAY_SIZE(cmdbuf->state.gfx.render.ir.fbds) == PANVK_IR_PASS_COUNT,
                    "ir.fbds array size must match PANVK_IR_PASS_COUNT");
      cmdbuf->state.gfx.render.ir.fbds[ir_pass] = ir_fbds.gpu;
   }

   /* Incremental-rendering loop might set fbd_info.load/store to spill nodes.
    * Set it back to the regular nodes here for CRC patching later. */
   fbd_info.load = &render->fb.load;
   fbd_info.store = &render->fb.store;

   /* Wait for IR info push to complete */
   cs_wait_slot(b, SB_ID(LS));

   if (copy_fbds) {
      struct cs_index cur_tiler = cs_reg64(b, PANVK_CS_REG_TILER_DESC_PTR);
      struct cs_index dst_fbd_ptr = cs_sr_reg64(b, FRAGMENT, FBD_POINTER);
      struct cs_index fbd_idx = cs_reg32(b, 60);
      struct cs_index src_fbd_ptr = cs_reg64(b, 64);
      struct cs_index remaining_layers_in_td = cs_reg32(b, 61);
      uint32_t td_count = DIV_ROUND_UP(cmdbuf->state.gfx.render.layer_count,
                                       MAX_LAYERS_PER_TILER_DESC);

      cs_update_frag_ctx(b) {
         cs_load64_to(b, cur_tiler, cs_subqueue_ctx_reg(b),
                      offsetof(struct panvk_cs_subqueue_context,
                               render.desc_ringbuf.ptr));
         cs_add_imm64(b, dst_fbd_ptr, cur_tiler,
                      pan_size(TILER_CONTEXT) * td_count);
      }

      cs_move64_to(b, src_fbd_ptr, fbds.gpu);

      /* Copy FBDs for layer regular pass */
      cs_move32_to(b, remaining_layers_in_td, MAX_LAYERS_PER_TILER_DESC);
      cs_move32_to(b, fbd_idx, enabled_layer_count);
      cs_while(b, MALI_CS_CONDITION_GREATER, fbd_idx) {
         cs_add_imm32(b, fbd_idx, fbd_idx, -1);

         /* Our loop is copying 64-bytes at a time, so make sure the
          * framebuffer size is aligned on 64-bytes. */
         assert(fbd_sz == ALIGN_POT(fbd_sz, 64));

#if PAN_ARCH >= 14
         for (uint32_t fbd_off = 0; fbd_off < fbd_sz; fbd_off += 64) {
            cs_load_to(b, cs_scratch_reg_tuple(b, 0, 16), src_fbd_ptr,
                       BITFIELD_MASK(16), fbd_off);

            /* Point the copied layer state at its copied tiler, DBD, and RTDs. */
            if (fbd_off == 0) {
               const unsigned dbd_reg =
                  offsetof(struct panvk_fb_layer_state, dbd_pointer) /
                  sizeof(uint32_t);
               const unsigned rtd_reg =
                  offsetof(struct panvk_fb_layer_state, rtd_pointer) /
                  sizeof(uint32_t);

               cs_add_imm64(b, cs_scratch_reg64(b, 0), cur_tiler, 0);

               if (has_zs_crc_ext) {
                  cs_add_imm64(b, cs_scratch_reg64(b, dbd_reg), dst_fbd_ptr,
                               fb_sz);
               } else {
                  cs_move64_to(b, cs_scratch_reg64(b, dbd_reg), 0);
               }

               cs_add_imm64(
                  b, cs_scratch_reg64(b, rtd_reg), dst_fbd_ptr,
                  fb_sz + (has_zs_crc_ext ? pan_size(ZS_CRC_EXTENSION) : 0));
            }

            cs_store(b, cs_scratch_reg_tuple(b, 0, 16), dst_fbd_ptr,
                     BITFIELD_MASK(16), fbd_off);
         }
#else
         bool unset_provoking_vertex =
            cmdbuf->state.gfx.render.first_provoking_vertex == U_TRISTATE_UNSET;
         for (uint32_t fbd_off = 0; fbd_off < fbd_sz; fbd_off += 64) {
            if (fbd_off == 0) {
               cs_load_to(b, cs_scratch_reg_tuple(b, 0, 14), src_fbd_ptr,
                          BITFIELD_MASK(14), fbd_off);

               /* Patch the Tiler pointer. */
               cs_add_imm64(b, cs_scratch_reg64(b, 14), cur_tiler, 0);

               /* If we don't know what provoking vertex mode the
                * application wants yet, leave space to patch it later. */
               if (unset_provoking_vertex) {
                  /* Provoking_vertex flag is bit 14 of word 11 */
                  struct cs_index word = cs_scratch_reg32(b, 11);
                  cs_maybe(
                     b,
                     &cmdbuf->state.gfx.render.maybe_set_fbds_provoking_vertex)
                     cs_add_imm32(b, word, word, -(1 << 14));
               }
            } else {
               cs_load_to(b, cs_scratch_reg_tuple(b, 0, 16), src_fbd_ptr,
                          BITFIELD_MASK(16), fbd_off);
            }
            cs_store(b, cs_scratch_reg_tuple(b, 0, 16), dst_fbd_ptr,
                     BITFIELD_MASK(16), fbd_off);
         }
#endif

         /* Finish stores to pass_dst_fbd_ptr. */
         cs_flush_stores(b);

         if (has_crc_patch) {
            struct cs_index fbd_ptr_reg = cs_scratch_reg64(b, 0);
            cs_move_reg64(b, fbd_ptr_reg, dst_fbd_ptr);
#if PAN_ARCH == 10
            /* Patch CRC read/write flags based on valid state. */
            patch_crc_valid(b, &crc_info, crc_fbd_flags, crc_rtd_flags,
                            fbd_ptr_reg, fb_sz, fbd_sz, 1);
#elif PAN_ARCH >= 11
            /* Patch CRC init for the copied descriptor. */
            patch_crc_init(b, &fbd_info, fbd_ptr_reg, fb_sz, fbd_sz, 1);
#endif
            cs_flush_stores(b);
         }

         cs_add_imm64(b, src_fbd_ptr, src_fbd_ptr, fbd_sz);
         cs_update_frag_ctx(b)
            cs_add_imm64(b, dst_fbd_ptr, dst_fbd_ptr, fbd_sz);

         cs_add_imm32(b, remaining_layers_in_td, remaining_layers_in_td, -1);
         cs_if(b, MALI_CS_CONDITION_LEQUAL, remaining_layers_in_td) {
            cs_update_frag_ctx(b)
               cs_add_imm64(b, cur_tiler, cur_tiler, pan_size(TILER_CONTEXT));
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
            cs_add_imm64(b, dst_fbd_ptr, cur_tiler,
                         fbd_flags + pan_size(TILER_CONTEXT));
         else
            cs_add_imm64(b, dst_fbd_ptr, cur_tiler, fbd_flags);

         cs_add_imm64(b, cur_tiler, cur_tiler,
                      -(full_td_count * pan_size(TILER_CONTEXT)));
      }
   } else {
      if (has_crc_patch) {
         struct cs_index fbd_ptr_reg = cs_scratch_reg64(b, 0);
         cs_move64_to(b, fbd_ptr_reg, fbds.gpu);
#if PAN_ARCH == 10
         patch_crc_valid(b, &crc_info, crc_fbd_flags, crc_rtd_flags,
                         fbd_ptr_reg, fb_sz, fbd_sz, enabled_layer_count);
#elif PAN_ARCH >= 11
         patch_crc_init(b, &fbd_info, fbd_ptr_reg, fb_sz, fbd_sz,
                        enabled_layer_count);
#endif
         cs_flush_stores(b);
      }

      cs_update_frag_ctx(b) {
         cs_move64_to(b, cs_sr_reg64(b, FRAGMENT, FBD_POINTER),
                      fbds.gpu | fbd_flags);
         cs_move64_to(b, cs_reg64(b, PANVK_CS_REG_TILER_DESC_PTR),
                      cmdbuf->state.gfx.render.tiler);
      }

#if PAN_ARCH < 14
      /* If we don't know what provoking vertex mode the application wants yet,
       * leave space to patch it later */
      if (cmdbuf->state.gfx.render.first_provoking_vertex == U_TRISTATE_UNSET) {
         uint32_t fbd_count = calc_enabled_layer_count(cmdbuf);
         /* passed to fn_set_fbds_provoking_vertex */
         struct cs_index fbd_count_reg = cs_scratch_reg32(b, 0);
         cs_move32_to(b, fbd_count_reg, fbd_count);

         struct cs_index length_reg = cs_scratch_reg32(b, 1);
         struct cs_index addr_reg = cs_scratch_reg64(b, 2);
         uint32_t fn_idx = calc_fn_set_fbds_provoking_vertex_idx(cmdbuf);
         uint32_t fn_stride =
            dev->draw_ctx->fn_set_fbds_provoking_vertex_stride;
         uint64_t fn_addr =
            dev->draw_ctx->fns_bo->addr.dev + fn_idx * fn_stride;
         cs_move64_to(b, addr_reg, fn_addr);
         cs_move32_to(b, length_reg, fn_stride);

         cs_maybe(b, &cmdbuf->state.gfx.render.maybe_set_fbds_provoking_vertex)
            cs_call(b, addr_reg, length_reg);
      }
#endif
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

static void
prepare_vs(struct panvk_cmd_buffer *cmdbuf,
           const struct panvk_shader_variant *vs)
{
   struct panvk_shader_desc_state *vs_desc_state = &cmdbuf->state.gfx.vs.desc;
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   cs_update_vt_ctx(b) {
      if (vs_desc_dirty(cmdbuf))
         cs_move64_to(b, cs_sr_reg64(b, IDVS, VERTEX_SRT),
                      vs_desc_state->res_table);

#if PAN_ARCH >= 12
      if (gfx_state_dirty(cmdbuf, VS) ||
          gfx_state_dirty(cmdbuf, IDVS)) {
         const uint64_t spd_addr =
            cmdbuf->state.gfx.idvs.prim == MESA_PRIM_POINTS
            ? panvk_priv_mem_dev_addr(vs->spds.all_points)
            : panvk_priv_mem_dev_addr(vs->spds.all_triangles);
         cs_move64_to(b, cs_sr_reg64(b, IDVS, VERTEX_SPD), spd_addr);
      }
#else
      if (gfx_state_dirty(cmdbuf, VS) ||
          gfx_state_dirty(cmdbuf, IDVS)) {
         const uint64_t pos_spd_addr =
            cmdbuf->state.gfx.idvs.prim == MESA_PRIM_POINTS
            ? panvk_priv_mem_dev_addr(vs->spds.pos_points)
            : panvk_priv_mem_dev_addr(vs->spds.pos_triangles);
         cs_move64_to(b, cs_sr_reg64(b, IDVS, VERTEX_POS_SPD), pos_spd_addr);
      }

      if (gfx_state_dirty(cmdbuf, VS))
         cs_move64_to(b, cs_sr_reg64(b, IDVS, VERTEX_VARY_SPD),
                      panvk_priv_mem_dev_addr(vs->spds.var));
#endif
   }
}

static void
prepare_fs(struct panvk_cmd_buffer *cmdbuf,
           const struct panvk_shader_variant *fs)
{
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   cs_update_vt_ctx(b) {
      if (fs_desc_dirty(cmdbuf))
         cs_move64_to(b, cs_sr_reg64(b, IDVS, FRAGMENT_SRT),
                      fs ? fs_desc_state->res_table : 0);
      if (fs_user_dirty(cmdbuf))
         cs_move64_to(b, cs_sr_reg64(b, IDVS, FRAGMENT_SPD),
                      fs ? panvk_priv_mem_dev_addr(fs->spd) : 0);
   }
}

static VkResult
prepare_push_uniforms(struct panvk_cmd_buffer *cmdbuf,
                      const struct panvk_draw_info *draw,
                      const struct panvk_shader_variant *vs,
                      const struct panvk_shader_variant *fs)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   VkResult result;

   uint32_t vs_repeat_count = 1;
   if (draw->indirect.buffer_dev_addr) {
      /* For indirect draws, VS_PUSH_UNIFORMS are always dirty so it's safe to
       * look at the draw info here.
       */
      assert(gfx_state_dirty(cmdbuf, VS_PUSH_UNIFORMS));
      if (shader_uses_sysval(vs, graphics, vs.first_vertex) ||
          shader_uses_sysval(vs, graphics, vs.base_instance))
         vs_repeat_count = draw->indirect.draw_count;
   }

   if (gfx_state_dirty(cmdbuf, VS_PUSH_UNIFORMS)) {
      struct pan_ptr push_uniforms;
      result = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(
         cmdbuf, vs, &push_uniforms, vs_repeat_count);
      if (result != VK_SUCCESS)
         return result;
      cmdbuf->state.gfx.vs.push_uniforms = push_uniforms.gpu;

      cs_update_vt_ctx(b) {
         cs_move64_to(b, cs_sr_reg64(b, IDVS, VERTEX_FAU),
                      cmdbuf->state.gfx.vs.push_uniforms |
                         ((uint64_t)vs->fau.total_count << 56));
      }
   }

   if (fs_user_dirty(cmdbuf) || gfx_state_dirty(cmdbuf, FS_PUSH_UNIFORMS)) {
      uint64_t fau_ptr = 0;

      if (fs) {
         struct pan_ptr push_uniforms;
         result = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(
            cmdbuf, fs, &push_uniforms, 1);
         if (result != VK_SUCCESS)
            return result;
         cmdbuf->state.gfx.fs.push_uniforms = push_uniforms.gpu;

         fau_ptr = cmdbuf->state.gfx.fs.push_uniforms |
                   ((uint64_t)fs->fau.total_count << 56);
      }

      cs_update_vt_ctx(b)
         cs_move64_to(b, cs_sr_reg64(b, IDVS, FRAGMENT_FAU), fau_ptr);
   }

   return VK_SUCCESS;
}

static VkResult
build_zsd(struct panvk_cmd_buffer *cmdbuf, struct pan_earlyzs_state earlyzs,
          const struct vk_rasterization_state *rs, uint64_t *zsd_gpu)
{
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_depth_stencil_state *ds = &dyns->ds;
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

   *zsd_gpu = zsd.gpu;

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

   uint64_t zsd_gpu;
   const struct vk_rasterization_state *rs =
      &cmdbuf->vk.dynamic_graphics_state.rs;
   VkResult result = build_zsd(cmdbuf, earlyzs, rs, &zsd_gpu);
   if (result != VK_SUCCESS)
      return result;

   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   cs_update_vt_ctx(b)
      cs_move64_to(b, cs_sr_reg64(b, IDVS, ZSD), zsd_gpu);

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

struct panvk_dcd_flags {
   struct mali_dcd_flags_0_packed flags_0;
   struct mali_dcd_flags_1_packed flags_1;
   struct mali_dcd_flags_2_packed flags_2;
   struct pan_earlyzs_state earlyzs;
   uint8_t rt_written;
   uint8_t rt_read;
};

static void
build_dcd_flags(struct panvk_cmd_buffer *cmdbuf,
                const struct panvk_shader_variant *fs, enum mesa_prim prim,
                struct panvk_dcd_flags *out)
{
   memset(out, 0, sizeof(*out));
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_rasterization_state *rs =
      &cmdbuf->vk.dynamic_graphics_state.rs;

   bool alpha_to_coverage = dyns->ms.alpha_to_coverage_enable;
   bool writes_z = writes_depth(cmdbuf);
   bool writes_s = writes_stencil(cmdbuf);
   bool shader_modifies_coverage = false;
   uint8_t rt_mask = cmdbuf->state.gfx.render.bound_attachments &
                     MESA_VK_RP_ATTACHMENT_ANY_COLOR_BITS;

   if (fs) {
      out->rt_written = color_attachment_written_mask(fs, &dyns->cal);
      out->rt_read = color_attachment_read_mask(fs, &dyns->ial, rt_mask);
      shader_modifies_coverage = fs->info.fs.writes_coverage ||
                                 fs->info.fs.can_discard || alpha_to_coverage;
   }

   bool msaa = dyns->ms.rasterization_samples > 1;
   enum mesa_prim reduced_prim = u_reduced_prim(prim);
   if (reduced_prim == MESA_PRIM_LINES &&
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

   pan_pack(&out->flags_0, DCD_FLAGS_0, cfg) {
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

         bool roa_color =
            cmdbuf->vk.dynamic_graphics_state.rasterization_order_access &
            VK_IMAGE_ASPECT_COLOR_BIT;

         cfg.allow_forward_pixel_to_kill =
            fs->info.fs.can_fpk && !(rt_mask & ~out->rt_written) &&
            !(out->rt_read & out->rt_written) && !alpha_to_coverage &&
            !cmdbuf->state.gfx.cb.info.any_dest_read && !roa_color;

         cfg.allow_forward_pixel_to_be_killed = !fs->info.writes_global;

         bool writes_zs = writes_z || writes_s;
         bool zs_always_passes = ds_test_always_passes(cmdbuf);
         bool oq = cmdbuf->state.gfx.occlusion_query.mode !=
                   MALI_OCCLUSION_MODE_DISABLED;

         bool roa_zs =
            cmdbuf->vk.dynamic_graphics_state.rasterization_order_access &
            (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

         if (roa_zs)
            zs_read = PAN_EARLYZS_ZS_TILEBUF_READ_NO_OPT;

         out->earlyzs =
            pan_earlyzs_get(fs->fs.earlyzs_lut, writes_zs || oq,
                            alpha_to_coverage, zs_always_passes, zs_read);

         cfg.pixel_kill_operation = (enum mali_pixel_kill)out->earlyzs.kill;
         cfg.zs_update_operation = (enum mali_pixel_kill)out->earlyzs.update;

         /* Use per-sample shading if required by API. Also use it when a
          * blend shader is used with multisampling, as this is handled by a
          * single ST_TILE in the blend shader with the current sample ID,
          * requiring per-sample shading.
          */
         cfg.evaluate_per_sample = (fs->info.fs.sample_shading ||
                                    cmdbuf->state.gfx.cb.info.needs_shader) &&
                                   (dyns->ms.rasterization_samples > 1);

         cfg.shader_modifies_coverage = shader_modifies_coverage;
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

      /*
       * Vulkan face culling is polygon-facing state.  Points and lines do
       * not have polygon winding, and FrontFacing is defined as true for
       * non-polygon primitives.
       *
       * Do not let the Mali DCD front/back face cull bits discard point/line
       * primitives before rasterization.
       */
      const bool non_polygon = reduced_prim != MESA_PRIM_TRIANGLES;

      cfg.cull_front_face =
         !non_polygon && (rs->cull_mode & VK_CULL_MODE_FRONT_BIT) != 0;
      cfg.cull_back_face =
         !non_polygon && (rs->cull_mode & VK_CULL_MODE_BACK_BIT) != 0;

      cfg.multisample_enable = msaa;
      cfg.occlusion_query = cmdbuf->state.gfx.occlusion_query.mode;
      cfg.alpha_to_coverage = alpha_to_coverage;
      cfg.scissor_to_bounding_box = true;
#if PAN_ARCH >= 11
      cfg.conservative_rast_mode =
         rs->conservative_mode ==
               VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT
            ? MALI_CONSERVATIVE_RAST_MODE_OVER_ESTIMATE
            : MALI_CONSERVATIVE_RAST_MODE_DISABLED;
#if PAN_ARCH < 14
      cfg.cull_zero_area = true;
#endif
#endif
   }

   pan_pack(&out->flags_1, DCD_FLAGS_1, cfg) {
      cfg.sample_mask = dyns->ms.sample_mask;
      cfg.render_target_mask = out->rt_written;
   }

   pan_pack(&out->flags_2, DCD_FLAGS_2, cfg) {
      cfg.read_mask = out->rt_read;
      cfg.write_mask = out->rt_written;
#if PAN_ARCH >= 11
      if (fs) {
         cfg.no_shader_depth_read = !z_attachment_read(fs, &dyns->ial);
         cfg.no_shader_stencil_read = !s_attachment_read(fs, &dyns->ial);
      }
#endif
#if PAN_ARCH >= 13
      if (fs) {
         /* HSR can cull */
         cfg.hsr_can_cull = !fs->info.fs.hsr.ld_tile && out->rt_written &&
                              !(out->rt_read & out->rt_written) &&
                              !cmdbuf->state.gfx.cb.info.any_dest_read;

         /* HSR can_be_culled
            * - FUTURE: We could allow write-only side effects.
            * - FUTURE: we could allow any LD_TILE that's not used in CLPER or
            *   tex_lod operations. */
         bool late_zs = out->earlyzs.update == MALI_PIXEL_KILL_FORCE_LATE;
         cfg.hsr_can_be_culled =
            !fs->info.fs.sidefx &&
            !fs->info.fs.hsr.wait_or_tile_access_before_atest_zsemit &&
            !fs->info.fs.hsr.rasterizer_coverage_read &&
            !fs->info.fs.hsr.ld_tile &&
            !fs->info.fs.hsr.centroid_interpolation &&
            (!shader_modifies_coverage || !cfg.z_write_or_stencil ||
               late_zs);

         /* Late HSR update */
         cfg.hsr_update_operation = shader_modifies_coverage || late_zs
                                       ? MALI_HSR_UPDATE_LATE
                                       : MALI_HSR_UPDATE_EARLY;

         /* Since we do not allow side effects for HSR, we can always have
            * the HSR kill op follow HSR update. */
         cfg.hsr_prepass_kill_operation =
            MALI_HSR_PREPASS_KILL_FOLLOW_HSR_UPDATE;

         /* Whether prepass must run varyings. */
         cfg.enable_varying_shading_in_pre_pass =
            fs->info.fs.hsr.varying_before_atest_zsemit &&
            (cfg.hsr_update_operation == MALI_HSR_UPDATE_LATE ||
             cfg.hsr_prepass_kill_operation == MALI_HSR_PREPASS_KILL_LATE ||
             out->earlyzs.update == MALI_PIXEL_KILL_FORCE_LATE ||
             out->earlyzs.kill == MALI_PIXEL_KILL_FORCE_LATE);

         /* Whether depth is written or stencil is used */
         const struct vk_depth_stencil_state *ds =
            &cmdbuf->vk.dynamic_graphics_state.ds;
         cfg.z_write_or_stencil =
            writes_z || (has_stencil_att(cmdbuf) && ds->stencil.test_enable);
      } else {
         cfg.hsr_can_cull = false;
         cfg.hsr_can_be_culled = false;
         cfg.z_write_or_stencil = false;
         cfg.enable_varying_shading_in_pre_pass = false;
         cfg.hsr_update_operation = MALI_HSR_UPDATE_EARLY;
         cfg.hsr_prepass_kill_operation =
            MALI_HSR_PREPASS_KILL_FOLLOW_HSR_UPDATE;
      }
#endif
   }
}

static void
prepare_dcd(struct panvk_cmd_buffer *cmdbuf,
            const struct panvk_shader_variant *fs,
            struct pan_earlyzs_state *earlyzs)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   bool dcd2_dirty = fs_user_dirty(cmdbuf) ||
                     dyn_gfx_state_dirty(cmdbuf, INPUT_ATTACHMENT_MAP) ||
                     dyn_gfx_state_dirty(cmdbuf, COLOR_ATTACHMENT_MAP);
#if PAN_ARCH >= 13
   dcd2_dirty |= dyn_gfx_state_dirty(cmdbuf, MS_ALPHA_TO_COVERAGE_ENABLE) ||
                 dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_TEST_ENABLE) ||
                 dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_WRITE_ENABLE) ||
                 dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_COMPARE_OP) ||
                 dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_TEST_ENABLE) ||
                 dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_OP) ||
                 dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_WRITE_MASK) ||
                 dyn_gfx_state_dirty(cmdbuf, CB_BLEND_ENABLES) ||
                 dyn_gfx_state_dirty(cmdbuf, CB_BLEND_EQUATIONS) ||
                 dyn_gfx_state_dirty(cmdbuf, CB_WRITE_MASKS);
#endif
   bool dcd0_dirty =
      dyn_gfx_state_dirty(cmdbuf, RS_RASTERIZER_DISCARD_ENABLE) ||
      dyn_gfx_state_dirty(cmdbuf, RS_CULL_MODE) ||
      dyn_gfx_state_dirty(cmdbuf, RS_LINE_MODE) ||
      dyn_gfx_state_dirty(cmdbuf, RS_FRONT_FACE) ||
#if PAN_ARCH >= 11
      dyn_gfx_state_dirty(cmdbuf, RS_CONSERVATIVE_MODE) ||
#endif
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
      gfx_state_dirty(cmdbuf, IDVS) ||
      gfx_state_dirty(cmdbuf, OQ) || dcd2_dirty;
   bool dcd1_dirty = dyn_gfx_state_dirty(cmdbuf, MS_RASTERIZATION_SAMPLES) ||
                     dyn_gfx_state_dirty(cmdbuf, MS_SAMPLE_MASK) ||
                     /* line mode needs primitive topology */
                     dyn_gfx_state_dirty(cmdbuf, RS_LINE_MODE) ||
                     dyn_gfx_state_dirty(cmdbuf, IA_PRIMITIVE_TOPOLOGY) ||
                     dyn_gfx_state_dirty(cmdbuf, COLOR_ATTACHMENT_MAP) ||
                     fs_user_dirty(cmdbuf) ||
                     gfx_state_dirty(cmdbuf, IDVS) ||
                     gfx_state_dirty(cmdbuf, RENDER_STATE);

   if (!dcd0_dirty && !dcd1_dirty && !dcd2_dirty)
      return;

   struct panvk_dcd_flags dcd_flags;
   build_dcd_flags(cmdbuf, fs, cmdbuf->state.gfx.idvs.prim, &dcd_flags);
   *earlyzs = dcd_flags.earlyzs;

   if (dcd0_dirty) {
      cs_update_vt_ctx(b)
         cs_move32_to(b, cs_sr_reg32(b, IDVS, DCD0),
                      dcd_flags.flags_0.opaque[0]);
   }
   if (dcd1_dirty) {
      cs_update_vt_ctx(b)
         cs_move32_to(b, cs_sr_reg32(b, IDVS, DCD1),
                      dcd_flags.flags_1.opaque[0]);
   }
   if (dcd2_dirty) {
      cs_update_vt_ctx(b)
         cs_move32_to(b, cs_sr_reg32(b, IDVS, DCD2),
                      dcd_flags.flags_2.opaque[0]);
   }
}

static void
prepare_index_buffer(struct cs_builder *b,
                     const struct panvk_draw_info *draw)
{
   if (draw->index.index_size) {
      cs_move32_to(b, cs_sr_reg32(b, IDVS, INDEX_BUFFER_SIZE),
                   draw->index.buffer_size);
      cs_move64_to(b, cs_sr_reg64(b, IDVS, INDEX_BUFFER),
                   draw->index.buffer_dev_addr);
   }
}

static void
set_tiler_idvs_flags(struct cs_builder *b, struct panvk_cmd_buffer *cmdbuf,
                     const struct panvk_draw_info *draw)
{
   const struct panvk_shader_variant *vs = raster_vs_variant(cmdbuf, false);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_rasterization_state *rs = &dyns->rs;
   struct mali_primitive_flags_packed tiler_idvs_flags;

   /* When drawing non-point primitives, we use the no_psiz variant which has
    * point size writes patched out */
   bool writes_point_size =
      vs->info.vs.writes_point_size &&
      cmdbuf->state.gfx.idvs.prim == MESA_PRIM_POINTS;
   bool writes_layer = vs->info.outputs_written & VARYING_BIT_LAYER;
   bool extended_fifo = writes_point_size || vs->info.vs.needs_extended_fifo;
   bool writes_prim_id = vs->info.outputs_written & VARYING_BIT_PRIMITIVE_ID;
   bool fs_reads_prim_id = fs ? fs->info.fs.reads_primitive_id : false;

   bool dirty = gfx_state_dirty(cmdbuf, VS) || fs_user_dirty(cmdbuf) ||
                gfx_state_dirty(cmdbuf, IDVS) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLIP_ENABLE);

   if (dirty) {
      pan_pack(&tiler_idvs_flags, PRIMITIVE_FLAGS, cfg) {
         cfg.draw_mode = translate_prim(cmdbuf->state.gfx.idvs.prim);
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
         cfg.primitive_restart = cmdbuf->state.gfx.idvs.restart;
#if PAN_ARCH < 14
         cfg.view_mask = cmdbuf->state.gfx.render.view_mask;
#endif
      }

      cs_move32_to(b, cs_sr_reg32(b, IDVS, TILER_FLAGS), tiler_idvs_flags.opaque[0]);
#if PAN_ARCH >= 11
      struct mali_primitive_flags_2_packed tiler_flags_2;
      pan_pack(&tiler_flags_2, PRIMITIVE_FLAGS_2, cfg) {
#if PAN_ARCH >= 14
         cfg.view_mask = cmdbuf->state.gfx.render.view_mask;
#endif
      }
      cs_move32_to(b, cs_sr_reg32(b, IDVS, TILER_FLAGS2),
                   tiler_flags_2.opaque[0]);
#endif
   }
}

static struct mali_primitive_flags_packed
get_tiler_flags_override(const struct panvk_draw_info *draw)
{
   struct mali_primitive_flags_packed flags_override;
   /* Pack with nodefaults so only explicitly set override fields affect the
    * previously set register values */
   pan_pack_nodefaults(&flags_override, PRIMITIVE_FLAGS, cfg) {
      cfg.index_type = index_size_to_index_type(draw->index.index_size);
   };

   return flags_override;
}

static VkResult
prepare_draw(struct panvk_cmd_buffer *cmdbuf,
             const struct panvk_draw_info *draw)
{
   const struct panvk_shader_variant *vs =
      raster_vs_variant(cmdbuf, draw->gpu_prerast_generated);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   ASSERTED bool idvs = vs->info.vs.idvs;
   VkResult result;

   assert(vs);

    if (!draw->gpu_prerast_generated) {
       assert(panvk_gpu_prerast_select(
                 cmdbuf->state.gfx.vs.shader->gpu_prerast.requirements,
                 cmdbuf->state.gfx.vs.shader->gpu_prerast.lowered_vs_compiled) ==
              PANVK_PRERAST_HW_IDVS);
    }

   /* FIXME: support non-IDVS. */
   assert(idvs);

   if (cmdbuf->state.gfx.idvs.prim != draw->prim ||
       cmdbuf->state.gfx.idvs.restart != draw->index.restart_enable) {
      cmdbuf->state.gfx.idvs.prim = draw->prim;
      cmdbuf->state.gfx.idvs.restart = draw->index.restart_enable;
      gfx_state_set_dirty(cmdbuf, IDVS);
   }

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
      ASSERTED const struct pan_fb_layout *fb =
         &cmdbuf->state.gfx.render.fb.layout;
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
      assert(fb->sample_count == 0 ||
             fb->sample_count == cmdbuf->state.gfx.render.fb.nr_samples);
   }

   if (!inherits_render_ctx(cmdbuf)) {
      result = get_render_ctx(cmdbuf);
      if (result != VK_SUCCESS)
         return result;
   }

   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   result = prepare_blend(cmdbuf);
   if (result != VK_SUCCESS)
      return result;

   panvk_per_arch(cmd_prepare_draw_sysvals)(cmdbuf, draw, fs);

   result = prepare_push_uniforms(cmdbuf, draw, vs, fs);
   if (result != VK_SUCCESS)
      return result;

   prepare_vs(cmdbuf, vs);
   prepare_fs(cmdbuf, fs);

   cs_update_vt_ctx(b) {
      prepare_index_buffer(b, draw);

      set_tiler_idvs_flags(b, cmdbuf, draw);

      cs_move32_to(b, cs_sr_reg32(b, IDVS, VARY_SIZE),
                   vs->info.varyings.formats.generic_size_B);

      struct pan_earlyzs_state earlyzs = {0};

      prepare_dcd(cmdbuf, fs, &earlyzs);

      result = prepare_ds(cmdbuf, earlyzs);
      if (result != VK_SUCCESS)
         return result;

      result = prepare_oq(cmdbuf);
      if (result != VK_SUCCESS)
         return result;

      prepare_vp(cmdbuf);
      prepare_tiler_primitive_size(cmdbuf, draw);
   }

   clear_dirty_after_draw(cmdbuf);
   return VK_SUCCESS;
}

static void
update_prims_generated_query(struct panvk_cmd_buffer *cmdbuf,
                             const struct panvk_draw_info *draw)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);
   struct vk_input_assembly_state *ia = &cmdbuf->vk.dynamic_graphics_state.ia;
   struct panvk_prims_generated_query_state *state =
      &cmdbuf->state.gfx.prims_generated_query;

   if (!state->ptr)
      return;

   /* The GS kernel adds the primitives it emits. */
   if (draw->gpu_prerast_generated && cmdbuf->state.gfx.gs.shader)
      return;

   uint32_t view_count = cmdbuf->state.gfx.render.view_mask ?
      util_bitcount(cmdbuf->state.gfx.render.view_mask) : 1;

   assert(!draw->index.index_size || !ia->primitive_restart_enable);

   if (draw->index.index_size && ia->primitive_restart_enable) {
      struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);

      struct panlib_update_prims_generated_query_restart_args args = {
         .prims_generated = state->ptr,
         .index_buffer = cmdbuf->state.gfx.ib.dev_addr,
         .index_buffer_size_el = cmdbuf->state.gfx.ib.size /
                                 draw->index.index_size,
         .cmd_stride = draw->indirect.stride,
         .cmd = draw->indirect.buffer_dev_addr,
         .view_count = view_count,
      };

      struct panlib_precomp_grid grid;
      if (draw->indirect.count_buffer_dev_addr) {
         struct cs_index addr = cs_scratch_reg64(b, 0);
         struct cs_index max_draw_count = cs_scratch_reg32(b, 2);

         cs_update_compute_ctx(b) {
            cs_move64_to(b, addr, draw->indirect.count_buffer_dev_addr);
            cs_load32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_X), addr, 0);

            cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_Y), 1);
            cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_Z), 1);

            cs_move32_to(b, max_draw_count, draw->indirect.draw_count);
            cs_umin32(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_X),
                      cs_sr_reg32(b, COMPUTE, JOB_SIZE_X), max_draw_count);
         }

         grid = panlib_dynamic_csf();
      } else {
         grid = panlib_1d(draw->indirect.draw_count);
      }

      /* We need to WAIT in order to avoid overlapping the (non-atomic) direct
       * draw counter updates with indirect draws. TODO: we could avoid that
       * by having separate direct/indirect counters and adding them on read */
      panlib_update_prims_generated_query_restart_struct(
         &precomp_ctx, grid, PANLIB_BARRIER_CSF_WAIT, args,
         poly_compact_prim(cmdbuf->state.gfx.idvs.prim),
         util_logbase2(draw->index.index_size));
   } else if (draw->indirect.buffer_dev_addr) {
      struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);

      struct panlib_update_prims_generated_query_indirect_args args = {
         .prims_generated = state->ptr,
         .draw_count_buffer = draw->indirect.count_buffer_dev_addr,
         .max_draw_count = draw->indirect.draw_count,
         .cmd_stride = draw->indirect.stride,
         .cmd = draw->indirect.buffer_dev_addr,
         .view_count = view_count,
      };

      /* We need to WAIT in order to avoid overlapping the (non-atomic) direct
       * draw counter updates with indirect draws. TODO: we could avoid that
       * by having separate direct/indirect counters and adding them on read */
      panlib_update_prims_generated_query_indirect_struct(
         &precomp_ctx, panlib_1d(1), PANLIB_BARRIER_CSF_WAIT, args,
         poly_compact_prim(cmdbuf->state.gfx.idvs.prim));
   } else {
      uint32_t prims_per_instance = u_decomposed_prims_for_vertices(
         cmdbuf->state.gfx.idvs.prim, draw->vertex.count);
      uint32_t prims_generated =
         prims_per_instance * draw->instance.count * view_count;

      struct cs_index addr = cs_scratch_reg64(b, 0);
      struct cs_index value = cs_scratch_reg32(b, 2);

      cs_move64_to(b, addr, state->ptr);
      cs_load32_to(b, value, addr, 0);
      cs_add_imm32(b, value, value, prims_generated);
      cs_store32(b, value, addr, 0);
      cs_flush_stores(b);
   }
}

static void
launch_gfx_cs(struct panvk_cmd_buffer *cmdbuf,
              const struct panvk_shader_variant *cs,
              const struct panvk_shader_desc_state *cs_desc_state,
              uint64_t push_uniforms,
              const struct panvk_dispatch_info *info)
{
   /* For GFX compute shaders, we re-emit push_uniforms every time because it
    * massively simplifies the interface.  Also, they basically always contain
    * draw info which changes every draw anyway so dirty checks won't actually
    * save us anything.
    */
   if (!push_uniforms) {
      struct pan_ptr push_uniforms_ptr;
      VkResult result = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(
         cmdbuf, cs, &push_uniforms_ptr, 1);
      if (result != VK_SUCCESS)
         return;
      push_uniforms = push_uniforms_ptr.gpu;
   }

   /* Dirty everything */
   compute_state_set_dirty(cmdbuf, CS);
   compute_state_set_dirty(cmdbuf, DESC_STATE);
   compute_state_set_dirty(cmdbuf, PUSH_UNIFORMS);

   panvk_per_arch(cmd_dispatch_shader)(cmdbuf, cs, cs_desc_state,
                                       push_uniforms, cmdbuf->state.gfx.tsd,
                                       info);

   /* Dirty everything */
   compute_state_set_dirty(cmdbuf, CS);
   compute_state_set_dirty(cmdbuf, DESC_STATE);
   compute_state_set_dirty(cmdbuf, PUSH_UNIFORMS);
}

static void
launch_draw(struct panvk_cmd_buffer *cmdbuf,
            const struct panvk_draw_info *draw)
{
   const struct cs_tracing_ctx *tracing_ctx =
      &cmdbuf->state.cs[PANVK_SUBQUEUE_VERTEX_TILER].tracing;
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   struct cs_index tracing_scratch_regs = cs_scratch_reg_tuple(b, 0, 4);

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
   bool dynamic_idvs_count = false;

   if (idvs_count == 0 && inherits_render_ctx(cmdbuf)) {
      /* The layer count is not part of inherited render state */
#if PAN_ARCH >= 14
      /* All layers always fit in one TD on v14+ */
      idvs_count = 1;
#else
      dynamic_idvs_count = true;
#endif
   }

   panvk_cond_render(cmdbuf, b)
   {
      struct cs_index draw_id = cs_undef();

      if (draw->indirect.draw_index) {
         draw_id = cs_scratch_reg32(b, 5);
         cs_move32_to(b, draw_id, draw->indirect.draw_index);
      }

      if (idvs_count > 1 || dynamic_idvs_count) {
         struct cs_index counter_reg = cs_scratch_reg32(b, 4);
         struct cs_index tiler_ctx_addr = cs_sr_reg64(b, IDVS, TILER_CTX);
         struct cs_index tiler_ctx_addr_tmp = cs_scratch_reg64(b, 6);

         if (dynamic_idvs_count) {
#if PAN_ARCH < 14
            cs_load32_to(b, counter_reg, cs_subqueue_ctx_reg(b),
                        offsetof(struct panvk_cs_subqueue_context,
                                 render.td_count));
            cs_add_imm64(b, tiler_ctx_addr_tmp, tiler_ctx_addr, 0);
#else
            UNREACHABLE("IDVS count is always static on v14+");
#endif
         } else {
            cs_move32_to(b, counter_reg, idvs_count);
         }

         cs_while(b, MALI_CS_CONDITION_GREATER, counter_reg) {
#if PAN_ARCH >= 12
            cs_trace_run_idvs2(b, tracing_ctx, tracing_scratch_regs,
                               flags_override.opaque[0], true, draw_id,
                               MALI_IDVS_SHADING_MODE_EARLY);
#else
            cs_trace_run_idvs(b, tracing_ctx, tracing_scratch_regs,
                              flags_override.opaque[0], true,
                              cs_shader_res_sel(0, 0, 1, 0),
                              cs_shader_res_sel(2, 2, 2, 0), draw_id);
#endif

            cs_add_imm32(b, counter_reg, counter_reg, -1);
            cs_update_vt_ctx(b) {
               cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr,
                            pan_size(TILER_CONTEXT));
            }
         }

         cs_update_vt_ctx(b) {
            if (dynamic_idvs_count)
               cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr_tmp, 0);
            else
               cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr,
                            -(idvs_count * pan_size(TILER_CONTEXT)));
         }
      } else {
#if PAN_ARCH >= 12
         cs_trace_run_idvs2(b, tracing_ctx, tracing_scratch_regs,
                            flags_override.opaque[0], true, draw_id,
                            MALI_IDVS_SHADING_MODE_EARLY);
#else
         cs_trace_run_idvs(b, tracing_ctx, tracing_scratch_regs,
                           flags_override.opaque[0], true,
                           cs_shader_res_sel(0, 0, 1, 0),
                           cs_shader_res_sel(2, 2, 2, 0), draw_id);
#endif
      }
   }
}

static void
patch_vs_attribs(struct panvk_cmd_buffer *cmdbuf,
                 const struct panvk_draw_info *draw, enum panvk_subqueue_id q)
{
   uint32_t patch_attribs =
      cmdbuf->state.gfx.vi.attribs_changing_on_base_instance;

   if (!patch_attribs)
      return;

   /* We had better not have thought our descriptors were re-usable */
   assert(cmdbuf->state.gfx.vs.desc_repeat_count);

   struct panvk_shader_desc_state *vs_desc_state =
      &cmdbuf->state.gfx.vs.desc;
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_vertex_input_state *vi = dyns->vi;
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, q);

   struct cs_index draw_params = cs_scratch_reg64(b, 0);
   struct cs_index vs_drv_set = cs_scratch_reg64(b, 2);
   struct cs_index first_instance = cs_scratch_reg32(b, 4);
   struct cs_index attrib_offset = cs_scratch_reg32(b, 5);
   struct cs_index multiplicand = cs_scratch_reg32(b, 6);
   struct cs_index draw_count = cs_scratch_reg32(b, 7);
   struct cs_index max_draw_count = cs_scratch_reg32(b, 8);

   if (draw->indirect.count_buffer_dev_addr) {
      cs_move32_to(b, max_draw_count, draw->indirect.draw_count);
      cs_move64_to(b, draw_params, draw->indirect.count_buffer_dev_addr);
      cs_load32_to(b, draw_count, draw_params, 0);
      cs_umin32(b, draw_count, draw_count, max_draw_count);
   } else {
      cs_move32_to(b, draw_count, draw->indirect.draw_count);
   }

   cs_move64_to(b, vs_drv_set, vs_desc_state->driver_set.dev_addr);
   cs_move64_to(b, draw_params, draw->indirect.buffer_dev_addr);

   cs_while(b, MALI_CS_CONDITION_GREATER, draw_count) {
      cs_load32_to(b, first_instance, draw_params,
                   draw->index.index_size ? 16 : 12);

      /* If firstInstance=0, skip the offset adjustment. */
      cs_if(b, MALI_CS_CONDITION_NEQUAL, first_instance) {
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
            cs_move_reg32(b, multiplicand, first_instance);

            /* Flush the loads here so that we don't get automatic flushes
             * over and over again due to the divergent nature of the if/else
             * in the loop below. */
            cs_flush_loads(b);
            for (uint32_t i = 31; i > 0; i--) {
               uint32_t add = stride << i;

               /* bit31 is the sign bit, so we don't need to subtract to
                * check the presence of the bit. */
               if (i < 31)
                  cs_add_imm32(b, multiplicand, multiplicand, -(1 << i));

               if (add) {
                  cs_if(b, MALI_CS_CONDITION_LESS, multiplicand)
                     cs_add_imm32(b, multiplicand, multiplicand, 1 << i);
                  cs_else(b)
                     cs_add_imm32(b, attrib_offset, attrib_offset, add);
               } else {
                  cs_if(b, MALI_CS_CONDITION_LESS, multiplicand)
                     cs_add_imm32(b, multiplicand, multiplicand, 1 << i);
               }
            }

            cs_if(b, MALI_CS_CONDITION_NEQUAL, multiplicand)
               cs_add_imm32(b, attrib_offset, attrib_offset, stride);

            cs_store32(b, attrib_offset, vs_drv_set,
                       pan_size(ATTRIBUTE) * i + (2 * sizeof(uint32_t)));
            cs_flush_stores(b);
         }
      }

      cs_add_imm32(b, draw_count, draw_count, -1);
      cs_add_imm64(b, draw_params, draw_params, draw->indirect.stride);
      cs_add_imm64(b, vs_drv_set, vs_drv_set, vs_desc_state->driver_set.size);
   }
}

static void
launch_indirect_draw(struct panvk_cmd_buffer *cmdbuf,
                      const struct panvk_draw_info *draw)
{
   const struct cs_tracing_ctx *tracing_ctx =
      &cmdbuf->state.cs[PANVK_SUBQUEUE_VERTEX_TILER].tracing;
   const struct panvk_shader_variant *vs =
      raster_vs_variant(cmdbuf, draw->gpu_prerast_generated);
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   struct mali_primitive_flags_packed flags_override =
      get_tiler_flags_override(draw);

   uint32_t vs_res_table_size =
      panvk_shader_res_table_count(&cmdbuf->state.gfx.vs.desc) *
      pan_size(RESOURCE);
   bool patch_faus = shader_uses_sysval(vs, graphics, vs.first_vertex) ||
                     shader_uses_sysval(vs, graphics, vs.base_instance);
   struct cs_index draw_params_addr = cs_scratch_reg64(b, 0);
   struct cs_index draw_count = cs_scratch_reg32(b, 6);
   struct cs_index max_draw_count = cs_scratch_reg32(b, 7);
   struct cs_index draw_id = cs_scratch_reg32(b, 7);
   struct cs_index idvs_count_reg = cs_scratch_reg32(b, 8);
   struct cs_index idvs_count_reg_tmp = cs_scratch_reg32(b, 9);
   struct cs_index vs_fau_addr = cs_scratch_reg64(b, 10);
   struct cs_index tiler_ctx_addr_tmp = cs_scratch_reg64(b, 12);
   struct cs_index tracing_scratch_regs = cs_scratch_reg_tuple(b, 14, 4);
   struct cs_index tiler_ctx_addr = cs_sr_reg64(b, IDVS, TILER_CTX);
   uint32_t vs_fau_count = vs->fau.total_count;

   uint32_t idvs_count = DIV_ROUND_UP(cmdbuf->state.gfx.render.layer_count,
                                      MAX_LAYERS_PER_TILER_DESC);
   bool dynamic_idvs_count = false;

   if (idvs_count == 0 && inherits_render_ctx(cmdbuf)) {
      /* The layer count is not part of inherited render state */
#if PAN_ARCH >= 14
      /* All layers always fit in one TD on v14+ */
      idvs_count = 1;
#else
      dynamic_idvs_count = true;
      cs_add_imm64(b, tiler_ctx_addr_tmp, tiler_ctx_addr, 0);
      cs_load32_to(b, idvs_count_reg_tmp, cs_subqueue_ctx_reg(b),
                   offsetof(struct panvk_cs_subqueue_context,
                            render.td_count));
#endif
   }

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

   cs_move64_to(b, draw_params_addr, draw->indirect.buffer_dev_addr);
   cs_move32_to(b, draw_id, 0);

   panvk_cond_render(cmdbuf, b)
      cs_while(b, MALI_CS_CONDITION_GREATER, draw_count)
   {
      cs_update_vt_ctx(b) {
         cs_move32_to(b, cs_sr_reg32(b, IDVS, GLOBAL_ATTRIBUTE_OFFSET), 0);
         /* Load SR33-37 from indirect buffer. */
         unsigned reg_mask = draw->index.index_size ? 0b11111 : 0b11011;
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

         cs_flush_stores(b);
      }

      /* NIR expects zero-based instance ID, but even if it did have an
       * intrinsic to load the absolute instance ID, we'd want to keep it
       * zero-based to work around Mali's limitation on non-zero firstInstance
       * when a instance divisor is used.
       */
      cs_update_vt_ctx(b)
         cs_move32_to(b, cs_sr_reg32(b, IDVS, INSTANCE_OFFSET), 0);

      if (idvs_count > 1 || dynamic_idvs_count) {
         if (dynamic_idvs_count) {
            cs_add_imm32(b, idvs_count_reg, idvs_count_reg_tmp, 0);
         } else {
            cs_move32_to(b, idvs_count_reg, idvs_count);
         }

         cs_while(b, MALI_CS_CONDITION_GREATER, idvs_count_reg) {
#if PAN_ARCH >= 12
            cs_trace_run_idvs2(b, tracing_ctx, tracing_scratch_regs,
                               flags_override.opaque[0], true, draw_id,
                               MALI_IDVS_SHADING_MODE_EARLY);
#else
            cs_trace_run_idvs(
               b, tracing_ctx, tracing_scratch_regs, flags_override.opaque[0],
               true, cs_shader_res_sel(0, 0, 1, 0),
               cs_shader_res_sel(2, 2, 2, 0), draw_id);
#endif

            cs_add_imm32(b, idvs_count_reg, idvs_count_reg, -1);
            cs_update_vt_ctx(b) {
               cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr,
                            pan_size(TILER_CONTEXT));
            }
         }

         cs_update_vt_ctx(b) {
            if (dynamic_idvs_count)
               cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr_tmp, 0);
            else
               cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr,
                            -(idvs_count * pan_size(TILER_CONTEXT)));
         }
      } else {
#if PAN_ARCH >= 12
         cs_trace_run_idvs2(b, tracing_ctx, tracing_scratch_regs,
                            flags_override.opaque[0], true, draw_id,
                            MALI_IDVS_SHADING_MODE_EARLY);
#else
         cs_trace_run_idvs(
            b, tracing_ctx, tracing_scratch_regs, flags_override.opaque[0],
            true, cs_shader_res_sel(0, 0, 1, 0),
            cs_shader_res_sel(2, 2, 2, 0), draw_id);
#endif
      }

      cs_add_imm32(b, draw_count, draw_count, -1);
      cs_add_imm32(b, draw_id, draw_id, 1);
      cs_add_imm64(b, draw_params_addr, draw_params_addr,
                   draw->indirect.stride);

      if (patch_faus) {
         cs_add_imm64(b, vs_fau_addr, vs_fau_addr,
                      vs_fau_count * sizeof(uint64_t));
         cs_update_vt_ctx(b) {
            cs_add_imm64(b, cs_sr_reg64(b, IDVS, VERTEX_FAU),
                         cs_sr_reg64(b, IDVS, VERTEX_FAU),
                         vs_fau_count * sizeof(uint64_t));
         }

      }

      /* If we patched the VS attributes, we need to re-emit them per-draw */
      if (cmdbuf->state.gfx.vs.desc_repeat_count) {
         cs_update_vt_ctx(b) {
            cs_add_imm64(b, cs_sr_reg64(b, IDVS, VERTEX_SRT),
                         cs_sr_reg64(b, IDVS, VERTEX_SRT), vs_res_table_size);
         }
      }
   }
}

static void panvk_cmd_draw(struct panvk_cmd_buffer *cmdbuf,
                           struct panvk_draw_info draw);
static void gpu_prerast_draw_generated(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_draw_info *source,
   uint64_t arena, enum panvk_gpu_prerast_poly_mode poly_mode);

static void
gpu_prerast_wait_compute(struct panvk_cmd_buffer *cmdbuf)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   struct cs_index sync_addr = cs_scratch_reg64(b, 0);
   struct cs_index sync_point = cs_scratch_reg64(b, 2);

   cs_load64_to(b, sync_addr, cs_subqueue_ctx_reg(b),
                offsetof(struct panvk_cs_subqueue_context, syncobjs));
   cs_add_imm64(b, sync_addr, sync_addr,
                PANVK_SUBQUEUE_COMPUTE * sizeof(struct panvk_cs_sync64));
   cs_add_imm64(b, sync_point,
                cs_progress_seqno_reg(b, PANVK_SUBQUEUE_COMPUTE),
                cmdbuf->state.cs[PANVK_SUBQUEUE_COMPUTE].relative_sync_point);
   panvk_instr_sync64_wait(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER, false,
                           MALI_CS_CONDITION_GREATER, sync_point, sync_addr);
}

static void
gpu_prerast_release_arena(struct panvk_cmd_buffer *cmdbuf, uint64_t arena)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   struct cs_index addr = cs_scratch_reg64(b, 0);
   struct cs_index one = cs_scratch_reg32(b, 2);

   cs_wait_slots(b, dev->csf.sb.all_iters_mask);
   cs_move64_to(b, addr, arena + offsetof(struct panvk_gpu_prerast_arena,
                                          available));
   cs_move32_to(b, one, 1);
   cs_sync32_add(b, true, MALI_CS_SYNC_SCOPE_CSG, one, addr, cs_now());
}

static void
gpu_prerast_launch_cs_desc(struct panvk_cmd_buffer *cmdbuf,
                           const struct panvk_shader_variant *cs,
                           const struct panvk_shader_desc_state *desc,
                           uint64_t push_uniforms, uint64_t tsd,
                           const struct panvk_dispatch_info *info)
{
   /* Same TLS pointer copy as cmd_dispatch(): the global TLS descriptor is
    * only filled at EndCommandBuffer, so copy its pointer on the GPU.
    */
   if (cs->info.tls_size) {
      struct cs_builder *b =
         panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);
      cs_move64_to(b, cs_scratch_reg64(b, 0), cmdbuf->state.tls.desc.gpu);
      cs_load64_to(b, cs_scratch_reg64(b, 2), cs_scratch_reg64(b, 0), 8);
      cs_move64_to(b, cs_scratch_reg64(b, 0), tsd);
      cs_store64(b, cs_scratch_reg64(b, 2), cs_scratch_reg64(b, 0), 8);
      cs_flush_stores(b);
   }

   compute_state_set_dirty(cmdbuf, CS);
   compute_state_set_dirty(cmdbuf, DESC_STATE);
   compute_state_set_dirty(cmdbuf, PUSH_UNIFORMS);

   panvk_per_arch(cmd_dispatch_shader)(cmdbuf, cs, desc, push_uniforms, tsd,
                                       info);

   compute_state_set_dirty(cmdbuf, CS);
   compute_state_set_dirty(cmdbuf, DESC_STATE);
   compute_state_set_dirty(cmdbuf, PUSH_UNIFORMS);
}

static void
gpu_prerast_launch_cs(struct panvk_cmd_buffer *cmdbuf,
                      const struct panvk_shader_variant *cs,
                      uint64_t push_uniforms, uint64_t tsd,
                      const struct panvk_dispatch_info *info)
{
   gpu_prerast_launch_cs_desc(cmdbuf, cs, &cmdbuf->state.gfx.vs.desc,
                              push_uniforms, tsd, info);
}

static VkResult
prepare_prerast_desc(struct panvk_cmd_buffer *cmdbuf,
                     const struct panvk_shader *gs,
                     struct panvk_shader_desc_state *gs_desc)
{
   const struct panvk_shader_desc_info *info = &gs->desc_info;
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   const uint32_t desc_count = info->dyn_bufs.count + 1;
   VkResult result = panvk_per_arch(cmd_prepare_push_descs)(
      cmdbuf, desc_state, info->used_set_mask);
   if (result != VK_SUCCESS)
      return result;
   struct pan_ptr driver_set = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, desc_count * PANVK_DESCRIPTOR_SIZE, PANVK_DESCRIPTOR_SIZE);
   struct panvk_opaque_desc *descs = driver_set.cpu;

   if (!driver_set.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pan_cast_and_pack(&descs[0], SAMPLER, cfg) {
      cfg.clamp_integer_array_indices = false;
   }
   panvk_per_arch(cmd_fill_dyn_bufs)(desc_state, info,
                                     (struct mali_buffer_packed *)(&descs[1]));

   gs_desc->driver_set.dev_addr = driver_set.gpu;
   gs_desc->driver_set.size = desc_count * PANVK_DESCRIPTOR_SIZE;

   return panvk_per_arch(cmd_prepare_shader_res_table)(cmdbuf, desc_state, info,
                                                       gs_desc, 1);
}

/* prim_id_base_addr, when set, holds the GS primitive ID base on the GPU. */
static bool
gpu_prerast_gs_run(struct panvk_cmd_buffer *cmdbuf,
                   const struct panvk_draw_info *source, uint64_t arena,
                   uint64_t vs_records, uint64_t vs_indices,
                   uint64_t prim_id_base_addr, enum panvk_csf_barrier barrier)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   const struct panvk_gpu_prerast_gs_info *gsi = &gs->gpu_prerast.gs;
   const struct panvk_shader_variant *kernel =
      &gs->variants[PANVK_GS_VARIANT_KERNEL];
   struct cs_builder *compute =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);
   struct cs_index addr = cs_scratch_reg64(compute, 0);
   struct cs_index value = cs_scratch_reg32(compute, 2);
   struct cs_index base = cs_scratch_reg64(compute, 4);
   struct cs_index count_reg = cs_scratch_reg32(compute, 6);
   struct cs_index instance_reg = cs_scratch_reg32(compute, 7);
   const bool indirect = source->indirect.buffer_dev_addr != 0;
   const uint64_t gs_params = arena + PANVK_GPU_PRERAST_GS_PARAMS_OFFSET;
   const uint64_t gs_draw = arena + PANVK_GPU_PRERAST_GS_DRAW_OFFSET;
   const uint64_t gs_dispatch = arena + PANVK_GPU_PRERAST_GS_DISPATCH_OFFSET;

   if (prepare_prerast_desc(cmdbuf, gs, &cmdbuf->state.gfx.gs.desc) !=
       VK_SUCCESS)
      return false;

   struct pan_compute_dim dim = {source->vertex.count, source->instance.count,
                                 gsi->invocations};
   uint64_t tsd = panvk_per_arch(cmd_dispatch_prepare_tls)(cmdbuf, kernel,
                                                           &dim, indirect);
   if (!tsd)
      return false;

   struct pan_ptr push;
   if (panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, kernel, &push,
                                                     1) != VK_SUCCESS)
      return false;
   if (shader_uses_sysval(kernel, graphics, gpu_prerast_params)) {
      uint32_t off = shader_remapped_sysval_offset(
         kernel, sysval_offset(graphics, gpu_prerast_params));
      *(uint64_t *)((uint8_t *)push.cpu + off) = gs_params;
   }

   /* Take the counts gpu_prerast_draw() stored, they are zeroed for draws
    * past the indirect count. */
   if (indirect) {
      cs_move64_to(compute, base, arena + PANVK_GPU_PRERAST_PARAMS_OFFSET);
      cs_load32_to(compute, count_reg, base,
                   offsetof(struct panvk_gpu_prerast_params, vertex_count));
      cs_load32_to(compute, instance_reg, base,
                   offsetof(struct panvk_gpu_prerast_params, instance_count));
      cs_wait_slot(compute, SB_ID(LS));
   }

   cs_move64_to(compute, base, gs_params);
#define STORE_GS32(field, imm) do {                                            \
   cs_move32_to(compute, value, (imm));                                        \
   cs_store32(compute, value, base,                                            \
              offsetof(struct panvk_gpu_prerast_gs_params, field));            \
} while (0)
#define STORE_GS64(field, imm) do {                                            \
   cs_move64_to(compute, addr, (imm));                                         \
   cs_store64(compute, addr, base,                                             \
              offsetof(struct panvk_gpu_prerast_gs_params, field));            \
} while (0)
   STORE_GS64(vs_records, vs_records);
   STORE_GS64(vs_outputs, vs->gpu_prerast.io.outputs_written);
   STORE_GS64(gen_indices, vs_indices);
   STORE_GS64(records, arena + PANVK_GPU_PRERAST_GS_RECORD_OFFSET);
   STORE_GS64(indices, arena + PANVK_GPU_PRERAST_GS_INDEX_OFFSET);
   STORE_GS64(draw, gs_draw);
   if (indirect) {
      cs_store32(compute, count_reg, base,
                 offsetof(struct panvk_gpu_prerast_gs_params, vertex_count));
      cs_store32(compute, instance_reg, base,
                 offsetof(struct panvk_gpu_prerast_gs_params, instance_count));
   } else {
      STORE_GS32(vertex_count, source->vertex.count);
      STORE_GS32(instance_count, source->instance.count);
   }
   STORE_GS32(primitive_restart, source->index.restart_enable);
   STORE_GS32(prim, source->prim);
   STORE_GS32(cap_slots, panvk_gpu_prerast_gs_cap_slots(gsi));
   STORE_GS32(vs_record_capacity,
              panvk_gpu_prerast_record_capacity(
                 vs->gpu_prerast.io.record_stride));
   STORE_GS32(provoking_last,
              cmdbuf->vk.dynamic_graphics_state.rs.provoking_vertex ==
                 VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT);
   if (prim_id_base_addr) {
      cs_move64_to(compute, addr, prim_id_base_addr);
      cs_load32_to(compute, value, addr, 0);
      cs_wait_slot(compute, SB_ID(LS));
      cs_store32(compute, value, base,
                 offsetof(struct panvk_gpu_prerast_gs_params, prim_id_base));
   } else {
      STORE_GS32(prim_id_base, source->gpu_prerast_prim_base);
   }
   STORE_GS32(raster_stream,
              cmdbuf->vk.dynamic_graphics_state.rs.rasterization_stream);

   const struct panvk_pstats_query_state *pstats =
      &cmdbuf->state.gfx.pstats_query;
   STORE_GS64(stats[0], panvk_pstats_counter_addr(
      pstats, VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT,
      true));
   STORE_GS64(stats[1], panvk_pstats_counter_addr(
      pstats, VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT,
      true));
   STORE_GS64(stats[2], panvk_pstats_counter_addr(
      pstats, VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT, true));
   STORE_GS64(stats[3], panvk_pstats_counter_addr(
      pstats, VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT, true));
   STORE_GS64(prims_generated, cmdbuf->state.gfx.prims_generated_query.ptr);
#undef STORE_GS32
#undef STORE_GS64

   cs_move64_to(compute, base, gs_draw);
   cs_move32_to(compute, value, 0);
   for (unsigned i = 0; i < 5; i++)
      cs_store32(compute, value, base, i * 4);

   if (indirect) {
      cs_move64_to(compute, base, gs_dispatch);
      cs_store32(compute, count_reg, base, 0);
      cs_store32(compute, instance_reg, base, 4);
      cs_move32_to(compute, value, gsi->invocations);
      cs_store32(compute, value, base, 8);
   }
   cs_flush_stores(compute);

   struct panvk_dispatch_info dispatch = {
      .direct.wg_count = {source->vertex.count, source->instance.count,
                          gsi->invocations},
      .barrier = barrier,
   };
   if (indirect)
      dispatch.indirect.buffer_dev_addr = gs_dispatch;

   cs_wait_slots(compute, dev->csf.sb.all_iters_mask);
   gpu_prerast_launch_cs_desc(cmdbuf, kernel, &cmdbuf->state.gfx.gs.desc,
                              push.gpu, tsd, &dispatch);
   return true;
}

static void
gpu_prerast_acquire_arena(struct panvk_cmd_buffer *cmdbuf, uint64_t arena)
{
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);
   struct cs_index addr = cs_scratch_reg64(b, 0);
   struct cs_index value = cs_scratch_reg32(b, 2);

   cs_move64_to(b, addr,
                arena + offsetof(struct panvk_gpu_prerast_arena, available));
   cs_move32_to(b, value, 0);
   cs_sync32_wait(b, false, MALI_CS_CONDITION_GREATER, value, addr);
   cs_move32_to(b, value, -1);
   cs_sync32_add(b, true, MALI_CS_SYNC_SCOPE_CSG, value, addr, cs_now());
}

static bool
gpu_prerast_xfb_active(const struct panvk_cmd_buffer *cmdbuf)
{
   const struct panvk_shader *last = cmdbuf->state.gfx.gs.shader
                                        ? cmdbuf->state.gfx.gs.shader
                                        : cmdbuf->state.gfx.vs.shader;

   return cmdbuf->state.gfx.xfb.active && last &&
          panvk_gpu_prerast_has_xfb(&last->gpu_prerast.xfb);
}

static void
gpu_prerast_xfb(struct panvk_cmd_buffer *cmdbuf,
                const struct panvk_draw_info *source, uint64_t arena,
                enum panlib_barrier barrier)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   const struct panvk_shader *last = gs ? gs : cmdbuf->state.gfx.vs.shader;
   const struct panvk_gpu_prerast_xfb_info *xi = &last->gpu_prerast.xfb;
   struct pan_ptr ptr = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct panlib_xfb_params), 16);

   if (!ptr.gpu)
      return;

   struct panlib_xfb_params *x = ptr.cpu;
   memset(x, 0, sizeof(*x));
   x->offsets = cmdbuf->state.gfx.xfb.offsets;
   for (unsigned i = 0; i < PANLIB_XFB_MAX_BUFFERS; i++) {
      x->buffers[i] = cmdbuf->state.gfx.xfb.buffers[i].addr;
      x->sizes[i] = cmdbuf->state.gfx.xfb.buffers[i].size;
      x->strides[i] = xi->stride[i];
      x->buffer_to_stream |= xi->buffer_to_stream[i] << (2 * i);
      x->queries[i] = cmdbuf->state.gfx.xfb.queries[i];
   }
   x->output_count = xi->output_count;
   memcpy(x->outputs, xi->outputs, xi->output_count * sizeof(uint32_t));

   if (gs) {
      const struct panvk_gpu_prerast_gs_info *g = &gs->gpu_prerast.gs;

      x->records = arena + PANVK_GPU_PRERAST_GS_RECORD_OFFSET;
      x->counts = arena + PANVK_GPU_PRERAST_GS_PARAMS_OFFSET +
                  offsetof(struct panvk_gpu_prerast_gs_params, vertex_count);
      x->record_stride = g->record_stride;
      x->record_capacity = panvk_gpu_prerast_gs_cap_slots(g);
      x->prim = g->output_prim;
      x->gs_max_vertices = g->max_vertices;
      x->gs_invocations = g->invocations;
   } else {
      x->records = arena + PANVK_GPU_PRERAST_RECORD_OFFSET;
      x->indices = arena + PANVK_GPU_PRERAST_INDEX_OFFSET;
      x->counts = arena + PANVK_GPU_PRERAST_PARAMS_OFFSET +
                  offsetof(struct panvk_gpu_prerast_params, vertex_count);
      x->record_stride = last->gpu_prerast.io.record_stride;
      x->record_capacity =
         panvk_gpu_prerast_record_capacity(x->record_stride);
      x->prim = source->prim;
      x->restart = source->index.index_size && source->index.restart_enable;
   }

   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);

   x->active = 1;
   if (cmdbuf->state.cond_render.enabled || cmdbuf->state.cond_render.inherited) {
      struct cs_index active = cs_scratch_reg32(b, 0);
      struct cs_index addr = cs_scratch_reg64(b, 2);

      cs_move32_to(b, active, 0);
      panvk_cond_render(cmdbuf, b)
         cs_move32_to(b, active, 1);
      cs_move64_to(b, addr,
                   ptr.gpu + offsetof(struct panlib_xfb_params, active));
      cs_store32(b, active, addr, 0);
      cs_flush_stores(b);
   }

   cs_wait_slots(b, dev->csf.sb.all_iters_mask);
   panlib_xfb_capture(&ctx, panlib_1d(1), barrier, ptr.gpu);
}

/* Acquires the arena and runs the lowered VS, plus the polygon kernel when
 * poly_mode asks for it. Returns false if nothing was queued. chunk_addr
 * points to the {vertex, instance} start of a tessellation chunk, or is 0.
 */
static bool
gpu_prerast_vs(struct panvk_cmd_buffer *cmdbuf,
               const struct panvk_draw_info *source,
               const struct panvk_shader *shader, uint64_t arena,
               enum panvk_gpu_prerast_poly_mode poly_mode, bool acquire,
               enum panvk_csf_barrier barrier, uint64_t chunk_addr)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   panvk_per_arch(cmd_prepare_draw_sysvals)(
      cmdbuf, source, panvk_shader_only_variant(get_fs(cmdbuf)));

   /* The lowered VS runs as RUN_COMPUTE and needs its own LOCAL_STORAGE
    * descriptor. gfx.tsd is only allocated by prepare_draw(), which runs
    * after this point, so reusing it passed TSD=0 and the compute CS raised
    * DATA_INVALID_FAULT (0x58) before any store executed.
    */
   struct pan_compute_dim tls_dim = {source->vertex.count,
                                     source->instance.count, 1};
   uint64_t tsd = panvk_per_arch(cmd_dispatch_prepare_tls)(
      cmdbuf,
      panvk_shader_gpu_prerast_variant(shader, PANVK_VS_VARIANT_GPU_LOWERED),
      &tls_dim, source->indirect.buffer_dev_addr != 0);
   if (!tsd)
      return false;

   uint64_t params = arena + PANVK_GPU_PRERAST_PARAMS_OFFSET;
   uint64_t records = arena + PANVK_GPU_PRERAST_RECORD_OFFSET;
   uint64_t indices = arena + PANVK_GPU_PRERAST_INDEX_OFFSET;
   uint64_t generated_draw = arena + PANVK_GPU_PRERAST_DRAW_OFFSET;
   uint64_t poly_indices = arena + PANVK_GPU_PRERAST_POLY_INDEX_OFFSET;
   uint64_t poly_draw = arena + PANVK_GPU_PRERAST_POLY_DRAW_OFFSET;
   struct cs_builder *compute =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);
   struct cs_index addr = cs_scratch_reg64(compute, 0);
   struct cs_index value = cs_scratch_reg32(compute, 2);
   struct cs_index draw_params = cs_scratch_reg64(compute, 4);

   struct pan_ptr push, poly_push = {0};
   const struct panvk_shader_variant *lowered = panvk_shader_gpu_prerast_variant(
      shader, PANVK_VS_VARIANT_GPU_LOWERED);
   const struct panvk_shader_variant *polygon =
      panvk_shader_gpu_prerast_variant(shader, PANVK_VS_VARIANT_GPU_POLYGON);
   if (panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, lowered, &push,
                                                     1) != VK_SUCCESS ||
       (poly_mode != PANVK_GPU_PRERAST_POLY_NONE &&
        panvk_per_arch(cmd_prepare_gfx_push_uniforms)(
           cmdbuf, polygon, &poly_push, 1) != VK_SUCCESS))
      return false;

   if (acquire)
      gpu_prerast_acquire_arena(cmdbuf, arena);

   uint32_t vertex_count = source->vertex.count;
   uint32_t instance_count = source->instance.count;
   uint32_t first_instance = source->instance.base;
   struct cs_index count_reg = cs_scratch_reg32(compute, 6);
   struct cs_index instance_reg = cs_scratch_reg32(compute, 7);
   struct cs_index first_index_reg = cs_scratch_reg32(compute, 8);
   struct cs_index vertex_base_reg = cs_scratch_reg32(compute, 9);
   struct cs_index first_instance_reg = cs_scratch_reg32(compute, 10);
   if (source->indirect.buffer_dev_addr) {
      cs_move64_to(compute, draw_params, source->indirect.buffer_dev_addr);
      cs_load32_to(compute, count_reg, draw_params, 0);
      cs_load32_to(compute, instance_reg, draw_params, 4);
      if (source->index.index_size) {
         cs_load32_to(compute, first_index_reg, draw_params, 8);
         cs_load32_to(compute, vertex_base_reg, draw_params, 12);
         cs_load32_to(compute, first_instance_reg, draw_params, 16);
      } else {
         cs_load32_to(compute, vertex_base_reg, draw_params, 8);
         cs_load32_to(compute, first_instance_reg, draw_params, 12);
         cs_move32_to(compute, first_index_reg, 0);
      }
      struct cs_index draw_count = cs_scratch_reg32(compute, 11);
      if (source->indirect.count_buffer_dev_addr) {
         cs_move64_to(compute, draw_params,
                      source->indirect.count_buffer_dev_addr);
         cs_load32_to(compute, draw_count, draw_params, 0);
      }
      cs_wait_slot(compute, SB_ID(LS));
      if (source->indirect.count_buffer_dev_addr) {
         cs_add_imm32(compute, draw_count, draw_count,
                      -(int32_t)source->indirect.draw_index);
         cs_if(compute, MALI_CS_CONDITION_LEQUAL, draw_count) {
            cs_move32_to(compute, count_reg, 0);
            cs_move32_to(compute, instance_reg, 0);
         }
      }
      cs_move64_to(compute, draw_params, params);
      cs_store32(compute, count_reg, draw_params,
                 offsetof(struct panvk_gpu_prerast_params, vertex_count));
      cs_store32(compute, instance_reg, draw_params,
                 offsetof(struct panvk_gpu_prerast_params, instance_count));
      cs_store32(compute, count_reg, draw_params,
                 offsetof(struct panvk_gpu_prerast_params, vertex.verts_per_instance));
      cs_store32(compute, first_index_reg, draw_params,
                 offsetof(struct panvk_gpu_prerast_params, index_offset));
      cs_store32(compute, first_instance_reg, draw_params,
                 offsetof(struct panvk_gpu_prerast_params, first_instance));
   }

   cs_move64_to(compute, draw_params, generated_draw);
   if (source->indirect.buffer_dev_addr) {
      cs_store32(compute, count_reg, draw_params, 0);
      cs_store32(compute, instance_reg, draw_params, 4);
      cs_move32_to(compute, value, 0);
      cs_store32(compute, value, draw_params, 8);
      cs_store32(compute, first_instance_reg, draw_params, 12);
      cs_store32(compute, value, draw_params, 16);
   } else {
      cs_move32_to(compute, value, vertex_count);
      cs_store32(compute, value, draw_params, 0);
      cs_move32_to(compute, value, instance_count);
      cs_store32(compute, value, draw_params, 4);
      cs_move32_to(compute, value, 0);
      cs_store32(compute, value, draw_params, 8);
      cs_move32_to(compute, value, first_instance);
      cs_store32(compute, value, draw_params, 12);
      cs_move32_to(compute, value, 0);
      cs_store32(compute, value, draw_params, 16);
   }

   /* The polygon kernel fills this in, but it is not run for draws past the
    * indirect count. */
   if (poly_mode != PANVK_GPU_PRERAST_POLY_NONE) {
      cs_move64_to(compute, draw_params, poly_draw);
      cs_move32_to(compute, value, 0);
      for (unsigned i = 0; i < 5; i++)
         cs_store32(compute, value, draw_params, i * 4);
   }

    uint64_t *fau = push.cpu;
    if (shader_uses_sysval(lowered, graphics, gpu_prerast_params)) {
       uint32_t params_off = shader_remapped_sysval_offset(
          lowered, sysval_offset(graphics, gpu_prerast_params));
       *(uint64_t *)((uint8_t *)fau + params_off) = params;
    }
    if (shader_uses_sysval(lowered, graphics, gpu_prerast_records)) {
       uint32_t records_off = shader_remapped_sysval_offset(
          lowered, sysval_offset(graphics, gpu_prerast_records));
       *(uint64_t *)((uint8_t *)fau + records_off) = records;
    }
    cmdbuf->state.gfx.sysvals.gpu_prerast_params = params;
    cmdbuf->state.gfx.sysvals.gpu_prerast_records = records;
    if (source->indirect.buffer_dev_addr) {
       cs_move64_to(compute, addr, push.gpu);
       if (shader_uses_sysval(lowered, graphics, vs.first_vertex))
          cs_store32(compute, vertex_base_reg, addr,
                     shader_remapped_sysval_offset(
                        lowered, sysval_offset(graphics, vs.first_vertex)));
       if (shader_uses_sysval(lowered, graphics, vs.base_instance))
          cs_store32(compute, first_instance_reg, addr,
                     shader_remapped_sysval_offset(
                        lowered, sysval_offset(graphics, vs.base_instance)));
       cs_flush_stores(compute);
    }

   cs_move64_to(compute, draw_params, params);
#define STORE_PARAM32(field, imm) do {                                         \
   cs_move32_to(compute, value, (imm));                                        \
   cs_store32(compute, value, draw_params,                                     \
              offsetof(struct panvk_gpu_prerast_params, field));               \
} while (0)
#define STORE_PARAM64(field, imm) do {                                         \
   cs_move64_to(compute, addr, (imm));                                         \
   cs_store64(compute, addr, draw_params,                                      \
              offsetof(struct panvk_gpu_prerast_params, field));               \
} while (0)
   STORE_PARAM64(vertex.index_buffer, source->index.buffer_dev_addr);
   STORE_PARAM32(vertex.index_size_B, source->index.index_size);
    STORE_PARAM32(vertex.index_buffer_range_el,
                  source->index.index_size
                     ? source->index.buffer_size / source->index.index_size : 0);
    if (!source->indirect.buffer_dev_addr) {
       STORE_PARAM32(index_offset, source->index.offset);
       STORE_PARAM32(vertex.verts_per_instance, vertex_count);
       STORE_PARAM32(vertex_count, vertex_count);
       STORE_PARAM32(instance_count, instance_count);
       STORE_PARAM32(first_instance, first_instance);
    }
   STORE_PARAM64(vertex.output_buffer, records);
   STORE_PARAM64(vertex.outputs, shader->gpu_prerast.io.outputs_written);
   STORE_PARAM64(generated_indices, indices);
   STORE_PARAM64(generated_draw, generated_draw);
   STORE_PARAM32(restart_index,
                 source->index.index_size == 1 ? UINT8_MAX :
                 source->index.index_size == 2 ? UINT16_MAX : UINT32_MAX);
   STORE_PARAM32(primitive_restart, source->index.restart_enable);
   STORE_PARAM32(record_capacity,
                 panvk_gpu_prerast_record_capacity(
                    shader->gpu_prerast.io.record_stride));
   STORE_PARAM32(draw_id, source->indirect.draw_index);
   if (chunk_addr) {
      cs_move64_to(compute, addr, chunk_addr);
      cs_load32_to(compute, value, addr, 0);
      cs_load32_to(compute, first_index_reg, addr, 4);
      cs_wait_slot(compute, SB_ID(LS));
      cs_store32(compute, value, draw_params,
                 offsetof(struct panvk_gpu_prerast_params, chunk_vertex));
      cs_store32(compute, first_index_reg, draw_params,
                 offsetof(struct panvk_gpu_prerast_params, chunk_instance));
   } else {
      STORE_PARAM32(chunk_vertex, 0);
      STORE_PARAM32(chunk_instance, 0);
   }
   if (poly_mode != PANVK_GPU_PRERAST_POLY_NONE) {
      const struct vk_dynamic_graphics_state *dyns =
         &cmdbuf->vk.dynamic_graphics_state;
      STORE_PARAM64(poly.indices, poly_indices);
      STORE_PARAM64(poly.draw, poly_draw);
      STORE_PARAM32(poly.mode, poly_mode);
      STORE_PARAM32(poly.prim, source->prim);
      STORE_PARAM32(poly.cull, dyns->rs.cull_mode);
      STORE_PARAM32(poly.front_ccw,
                    dyns->rs.front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE);
      STORE_PARAM32(poly.flip_y, dyns->vp.viewports[0].height < 0.0f);
   }
   cs_flush_stores(compute);
#undef STORE_PARAM32
#undef STORE_PARAM64

   uint64_t dispatch_params = arena + PANVK_GPU_PRERAST_DISPATCH_OFFSET;
   if (source->indirect.buffer_dev_addr) {
      cs_move64_to(compute, draw_params, dispatch_params);
      cs_store32(compute, count_reg, draw_params, 0);
      cs_store32(compute, instance_reg, draw_params, 4);
      cs_move32_to(compute, value, 1);
      cs_store32(compute, value, draw_params, 8);
      cs_flush_stores(compute);
   }

   struct panvk_dispatch_info dispatch = {
      .direct.wg_count = {vertex_count, instance_count, 1},
      .barrier = barrier,
   };
   if (source->indirect.buffer_dev_addr)
      dispatch.indirect.buffer_dev_addr = dispatch_params;
   gpu_prerast_launch_cs(cmdbuf, lowered, push.gpu, tsd, &dispatch);

   if (poly_mode != PANVK_GPU_PRERAST_POLY_NONE) {
      if (shader_uses_sysval(polygon, graphics, gpu_prerast_params)) {
         uint32_t off = shader_remapped_sysval_offset(
            polygon, sysval_offset(graphics, gpu_prerast_params));
         *(uint64_t *)((uint8_t *)poly_push.cpu + off) = params;
      }
      cs_wait_slots(compute, dev->csf.sb.all_iters_mask);
      gpu_prerast_launch_cs(cmdbuf, polygon, poly_push.gpu, tsd, &dispatch);
   }

   return true;
}

static bool
gpu_prerast_draw(struct panvk_cmd_buffer *cmdbuf,
                 const struct panvk_draw_info *source)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct panvk_shader *shader = cmdbuf->state.gfx.vs.shader;
   if (!panvk_per_arch(gpu_prerast_can_lower)(cmdbuf, source,
                                               &shader->gpu_prerast))
      return false;

   uint64_t arena = panvk_priv_mem_dev_addr(dev->gpu_prerast_arena);
   uint64_t records = arena + PANVK_GPU_PRERAST_RECORD_OFFSET;
   uint64_t indices = arena + PANVK_GPU_PRERAST_INDEX_OFFSET;
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   const enum panvk_gpu_prerast_poly_mode poly_mode =
      gs ? PANVK_GPU_PRERAST_POLY_NONE
         : panvk_per_arch(gpu_prerast_poly_mode)(cmdbuf, source);

   /* The lowered VS fetches attributes on the compute queue. */
   if (source->indirect.buffer_dev_addr)
      patch_vs_attribs(cmdbuf, source, PANVK_SUBQUEUE_COMPUTE);

   if (!gpu_prerast_vs(cmdbuf, source, shader, arena, poly_mode, true,
                       PANVK_CSF_BARRIER_SYNC, 0))
      return true;

   if (gs && !gpu_prerast_gs_run(cmdbuf, source, arena, records, indices, 0,
                                 PANVK_CSF_BARRIER_SYNC)) {
      gpu_prerast_wait_compute(cmdbuf);
      gpu_prerast_release_arena(cmdbuf, arena);
      return true;
   }

   if (gpu_prerast_xfb_active(cmdbuf))
      gpu_prerast_xfb(cmdbuf, source, arena, PANLIB_BARRIER_CSF_SYNC);

   gpu_prerast_wait_compute(cmdbuf);
   gpu_prerast_draw_generated(cmdbuf, source, arena, poly_mode);
   gpu_prerast_release_arena(cmdbuf, arena);
   return true;
}

/* Draws what gpu_prerast_vs() and gpu_prerast_gs_run() left in the arena. */
static void
gpu_prerast_draw_generated(struct panvk_cmd_buffer *cmdbuf,
                           const struct panvk_draw_info *source, uint64_t arena,
                           enum panvk_gpu_prerast_poly_mode poly_mode)
{
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   uint64_t indices = arena + PANVK_GPU_PRERAST_INDEX_OFFSET;
   uint64_t generated_draw = arena + PANVK_GPU_PRERAST_DRAW_OFFSET;
   uint64_t poly_indices = arena + PANVK_GPU_PRERAST_POLY_INDEX_OFFSET;
   uint64_t poly_draw = arena + PANVK_GPU_PRERAST_POLY_DRAW_OFFSET;

   struct panvk_draw_info generated = *source;
   generated.index.buffer_dev_addr = indices;
   generated.index.buffer_size =
      PANVK_GPU_PRERAST_MAX_INVOCATIONS * sizeof(uint32_t);
   generated.index.index_size = 4;
   generated.index.offset = 0;
   generated.index.restart_enable = source->index.restart_enable;
   generated.vertex.base = 0;
   generated.vertex.count = 0;
   generated.instance.base = source->instance.base;
   generated.instance.count = source->instance.count;
   generated.indirect.buffer_dev_addr = generated_draw;
   generated.indirect.count_buffer_dev_addr = 0;
   generated.indirect.draw_count = 1;
   generated.indirect.stride = sizeof(struct poly_indirect_draw);
   if (poly_mode != PANVK_GPU_PRERAST_POLY_NONE) {
      generated.prim = poly_mode == PANVK_GPU_PRERAST_POLY_POINT
                          ? MESA_PRIM_POINTS
                          : MESA_PRIM_LINES;
      generated.index.buffer_dev_addr = poly_indices;
      generated.index.buffer_size = PANVK_GPU_PRERAST_MAX_INVOCATIONS *
                                    PANVK_GPU_PRERAST_POLY_INDICES_PER_PRIM *
                                    sizeof(uint32_t);
      generated.index.restart_enable = true;
      generated.instance.base = 0;
      generated.instance.count = 1;
      generated.indirect.buffer_dev_addr = poly_draw;
   }
   if (gs) {
      switch (gs->gpu_prerast.gs.output_prim) {
      case MESA_PRIM_POINTS:
         generated.prim = MESA_PRIM_POINTS;
         break;
      case MESA_PRIM_LINE_STRIP:
         generated.prim = MESA_PRIM_LINE_STRIP;
         break;
      default:
         generated.prim = MESA_PRIM_TRIANGLE_STRIP;
         break;
      }
      generated.index.buffer_dev_addr = arena + PANVK_GPU_PRERAST_GS_INDEX_OFFSET;
      generated.index.buffer_size =
         PANVK_GPU_PRERAST_GS_INDEX_CAPACITY * sizeof(uint32_t);
      generated.index.restart_enable = true;
      generated.instance.base = 0;
      generated.instance.count = 1;
      generated.indirect.buffer_dev_addr =
         arena + PANVK_GPU_PRERAST_GS_DRAW_OFFSET;
      cmdbuf->state.gfx.sysvals.gpu_prerast_records =
         arena + PANVK_GPU_PRERAST_GS_RECORD_OFFSET;
   }
    generated.gpu_prerast_generated = true;

    gfx_state_set_dirty(cmdbuf, VS);
    gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
    panvk_cmd_draw(cmdbuf, generated);
    gfx_state_set_dirty(cmdbuf, VS);
    gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
}

#define gpu_prerast_unhandled(why)                                             \
   do {                                                                        \
      mesa_loge_once("panvk: gpu_prerast cannot lower this draw (%s), "       \
                     "skipping it", why);                                      \
      assert(!"gpu_prerast draw not lowered");                                 \
   } while (0)

static uint32_t
gpu_prerast_tess_max_patches(const struct panvk_cmd_buffer *cmdbuf)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *tcs = cmdbuf->state.gfx.tcs.shader;
   const uint32_t ps =
      MAX2(cmdbuf->vk.dynamic_graphics_state.ts.patch_control_points, 1);

   if (!tcs || !cmdbuf->state.gfx.tes.shader)
      return 0;

   return panvk_gpu_prerast_tess_max_patches(
      tcs->gpu_prerast.tess.stride_el * 4, vs->gpu_prerast.io.record_stride,
      ps);
}

static void
gpu_prerast_sync32_take(struct panvk_cmd_buffer *cmdbuf,
                        enum panvk_subqueue_id subqueue, uint64_t sync)
{
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, subqueue);
   struct cs_index addr = cs_scratch_reg64(b, 0);
   struct cs_index value = cs_scratch_reg32(b, 2);

   cs_move64_to(b, addr, sync);
   cs_move32_to(b, value, 0);
   cs_sync32_wait(b, false, MALI_CS_CONDITION_GREATER, value, addr);
   cs_move32_to(b, value, -1);
   cs_sync32_add(b, true, MALI_CS_SYNC_SCOPE_CSG, value, addr, cs_now());
}

static void
gpu_prerast_sync32_give(struct panvk_cmd_buffer *cmdbuf,
                        enum panvk_subqueue_id subqueue, uint64_t sync)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, subqueue);
   struct cs_index addr = cs_scratch_reg64(b, 0);
   struct cs_index value = cs_scratch_reg32(b, 2);

   cs_wait_slots(b, dev->csf.sb.all_iters_mask);
   cs_move64_to(b, addr, sync);
   cs_move32_to(b, value, 1);
   cs_sync32_add(b, true, MALI_CS_SYNC_SCOPE_CSG, value, addr, cs_now());
}

/* Loads a 32-bit flag into scratch register 0 for a loop exit test. */
static struct cs_index
gpu_prerast_load_flag(struct cs_builder *b, uint64_t addr)
{
   struct cs_index ptr = cs_scratch_reg64(b, 2);
   struct cs_index flag = cs_scratch_reg32(b, 0);

   cs_move64_to(b, ptr, addr);
   cs_load32_to(b, flag, ptr, 0);
   cs_wait_slot(b, SB_ID(LS));
   return flag;
}

/* GPU tessellation, as one CS loop on each of the compute and vertex/tiler
 * queues, handshaking through the ready/free syncs of struct
 * panlib_tess_draw. Each compute iteration either runs VS -> TCS ->
 * tessellator on the next chunk of patches that fits the tessellation arena,
 * or moves to the next index range of the current chunk. When the TES output
 * goes through gpu_prerast again (GS, non-fill polygon mode), that range is
 * sized to the gpu_prerast arena and the TES and GS kernels run in the same
 * iteration. The vertex/tiler iteration then draws that range. Nothing is
 * dropped: the loop runs until every patch and index is drawn.
 */
static void
gpu_prerast_tess(struct panvk_cmd_buffer *cmdbuf,
                 const struct panvk_draw_info *draw)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *tcs = cmdbuf->state.gfx.tcs.shader;
   const struct panvk_shader *tes = cmdbuf->state.gfx.tes.shader;
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;

   if (!tcs || !tes || !tcs->gpu_prerast.tess.compiled ||
       !vs->gpu_prerast.lowered_vs_compiled ||
       !panvk_priv_mem_check_alloc(dev->gpu_prerast_tess_arena)) {
      gpu_prerast_unhandled("tessellation shaders missing");
      return;
   }

   const struct panvk_gpu_prerast_tess_info *tc = &tcs->gpu_prerast.tess;
   const struct panvk_gpu_prerast_tess_info *te = &tes->gpu_prerast.tess;
   const uint32_t domain =
      te->domain != PANVK_GPU_PRERAST_TESS_UNSET ? te->domain : tc->domain;
   const uint32_t spacing =
      te->spacing != PANVK_GPU_PRERAST_TESS_UNSET ? te->spacing : tc->spacing;
   const bool points = te->point_mode || tc->point_mode;
   const bool ccw =
      (te->ccw || tc->ccw) !=
      (dyns->ts.domain_origin == VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT);
   const uint32_t ps = MAX2(dyns->ts.patch_control_points, 1);
   const uint32_t max_patches = gpu_prerast_tess_max_patches(cmdbuf);

   if (domain == PANVK_GPU_PRERAST_TESS_UNSET ||
       spacing == PANVK_GPU_PRERAST_TESS_UNSET || !max_patches) {
      gpu_prerast_unhandled("tessellation state");
      return;
   }

   const bool indirect = draw->indirect.buffer_dev_addr != 0;
   const bool indexed = draw->index.index_size != 0;
   if (!indirect && (draw->vertex.count < ps || !draw->instance.count))
      return;

   /* The draw of the tessellator output, one index range per iteration. */
   struct panvk_draw_info out = *draw;
   out.prim = points          ? MESA_PRIM_POINTS
              : domain == 2   ? MESA_PRIM_LINES
                              : MESA_PRIM_TRIANGLES;
   out.index.index_size = 4;
   out.index.offset = 0;
   out.index.restart_enable = false;
   out.vertex.base = 0;
   out.vertex.count = 0;
   out.instance.base = 0;
   out.instance.count = 1;
   out.indirect.count_buffer_dev_addr = 0;
   out.indirect.draw_count = 1;
   out.indirect.stride = 5 * sizeof(uint32_t);
   out.pstats_counted = true;
   out.gpu_prerast_prim_base = 0;

   const uint32_t vpp = mesa_vertices_per_prim(out.prim);
   const enum panvk_gpu_prerast_poly_mode poly_mode =
      gs ? PANVK_GPU_PRERAST_POLY_NONE
         : panvk_per_arch(gpu_prerast_poly_mode)(cmdbuf, &out);
   const bool xfb = cmdbuf->state.gfx.xfb.active &&
                    panvk_gpu_prerast_has_xfb(gs ? &gs->gpu_prerast.xfb
                                                 : &tes->gpu_prerast.xfb);
   const bool nested =
      panvk_per_arch(gpu_prerast_can_lower)(cmdbuf, &out, &tes->gpu_prerast);
   uint32_t range = 0;

   if (!nested && (gs || poly_mode != PANVK_GPU_PRERAST_POLY_NONE ||
                   xfb)) {
      gpu_prerast_unhandled("tessellation gpu_prerast variants missing");
      return;
   }

   if (nested) {
      /* Same vertex budget as gpu_prerast_split(). */
      uint32_t verts =
         panvk_gpu_prerast_record_capacity(tes->gpu_prerast.io.record_stride);
      if (gs)
         verts = MIN2(verts,
                      panvk_gpu_prerast_gs_cap_slots(&gs->gpu_prerast.gs) /
                         MAX2(gs->gpu_prerast.gs.invocations, 1));
      range = verts / vpp * vpp;
      if (!range || !panvk_priv_mem_check_alloc(dev->gpu_prerast_arena)) {
         gpu_prerast_unhandled("tessellation output range");
         return;
      }
   }

   const uint64_t arena = panvk_priv_mem_dev_addr(dev->gpu_prerast_tess_arena);
   const uint64_t prerast_arena =
      nested ? panvk_priv_mem_dev_addr(dev->gpu_prerast_arena) : 0;
   struct panvk_gpu_prerast_tess_layout l;
   panvk_gpu_prerast_tess_layout(arena, max_patches, tc->stride_el * 4, &l);

   struct pan_ptr td = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct panlib_tess_draw), 16);
   if (!td.gpu)
      return;

#define TD(f) (td.gpu + offsetof(struct panlib_tess_draw, f))
   const struct panvk_pstats_query_state *pstats =
      &cmdbuf->state.gfx.pstats_query;
   struct panlib_tess_draw *t = td.cpu;

   /* ponytail: the TES invocation count is a 32-bit add from the
    * tessellator, it wraps past 4G domain points per query.
    */
   memset(t, 0, sizeof(*t));
   t->p = (struct poly_tess_params){
      .heap = TD(heap),
      .patch_coord_buffer = l.heap,
      .coord_allocs = l.coord_allocs,
      .tcs_buffer = l.tcs,
      .counts = l.counts,
      .index_buffer = l.index,
      .statistic = panvk_pstats_counter_addr(
         pstats,
         VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT,
         true),
      .tcs_per_vertex_outputs = tc->per_vertex_outputs,
      .input_patch_size = ps,
      .output_patch_size = tc->out_patch_size,
      .tcs_patch_constants = tc->patch_constants,
      .tcs_stride_el = tc->stride_el,
      .partitioning = spacing,
      .points_mode = points,
      .isolines = domain == 2,
      .ccw = ccw,
   };
   t->heap.base = l.heap;
   t->heap.size = l.heap_size;
   t->input_patch_size = ps;
   t->max_patches = max_patches;
   t->indexed = indexed;
   t->range = range;
   t->verts_per_prim = vpp;
   t->free.seqno = 1;

   uint64_t src = draw->indirect.buffer_dev_addr;
   if (!indirect) {
      t->in_draw[0] = draw->vertex.count;
      t->in_draw[1] = draw->instance.count;
      if (indexed) {
         t->in_draw[2] = draw->index.offset;
         t->in_draw[3] = draw->vertex.base;
         t->in_draw[4] = draw->instance.base;
      } else {
         t->in_draw[2] = draw->vertex.base;
         t->in_draw[3] = draw->instance.base;
      }
      src = TD(in_draw);
   }

   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);
   struct cs_builder *vt =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   cmdbuf->state.gfx.sysvals.gpu_prerast_tess = TD(p);

   /* Record-time TCS state, reused by every iteration. */
   const struct panvk_shader_variant *tcs_v = panvk_shader_only_variant(tcs);
   struct pan_compute_dim dim = {max_patches, 1, 1};
   struct pan_ptr push;
   if (prepare_prerast_desc(cmdbuf, tcs, &cmdbuf->state.gfx.tcs.desc) !=
       VK_SUCCESS)
      return;
   uint64_t tsd =
      panvk_per_arch(cmd_dispatch_prepare_tls)(cmdbuf, tcs_v, &dim, true);
   if (!tsd || panvk_per_arch(cmd_prepare_gfx_push_uniforms)(
                  cmdbuf, tcs_v, &push, 1) != VK_SUCCESS)
      return;
   if (shader_uses_sysval(tcs_v, graphics, gpu_prerast_params)) {
      *(uint64_t *)((uint8_t *)push.cpu +
                    shader_remapped_sysval_offset(
                       tcs_v, sysval_offset(graphics, gpu_prerast_params))) =
         arena + PANVK_GPU_PRERAST_PARAMS_OFFSET;
   }
   if (shader_uses_sysval(tcs_v, graphics, gpu_prerast_tess)) {
      *(uint64_t *)((uint8_t *)push.cpu +
                    shader_remapped_sysval_offset(
                       tcs_v, sysval_offset(graphics, gpu_prerast_tess))) =
         TD(p);
   }

   out.index.buffer_dev_addr = l.index;
   out.index.buffer_size = l.index_size;
   out.indirect.buffer_dev_addr = TD(draw);
   out.prims_generated_counted = true;

   struct panvk_draw_info vs_src = *draw;
   vs_src.indirect.buffer_dev_addr = TD(vs_draw);
   vs_src.indirect.count_buffer_dev_addr = 0;
   vs_src.indirect.draw_count = 1;
   vs_src.indirect.stride = sizeof(t->vs_draw);

   struct panvk_dispatch_info tcs_dispatch = {
      .direct.wg_count = {max_patches, 1, 1},
      .indirect.buffer_dev_addr = TD(tcs_grid),
      .barrier = PANVK_CSF_BARRIER_WAIT,
   };
   const struct panlib_precomp_grid tess_grid =
      panlib_1d(DIV_ROUND_UP(max_patches, 64));
   const uint64_t prims_generated =
      gs ? 0 : cmdbuf->state.gfx.prims_generated_query.ptr;
   const uint32_t view_count =
      cmdbuf->state.gfx.render.view_mask
         ? util_bitcount(cmdbuf->state.gfx.render.view_mask)
         : 1;
   const uint64_t c_invs =
      gs ? 0
         : panvk_pstats_counter_addr(
              pstats, VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT,
              true);
   const uint64_t c_prims =
      gs ? 0
         : panvk_pstats_counter_addr(
              pstats, VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT,
              true);
   const uint64_t tcs_stat = panvk_pstats_counter_addr(
      pstats, VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT,
      true);

   /* Compute queue: the draw owns both arenas until the vertex/tiler loop
    * ends. No CSF_SYNC barrier in the loop: it would bump the compute
    * progress seqno once per iteration but only once at record time.
    */
   if (draw->indirect.buffer_dev_addr)
      patch_vs_attribs(cmdbuf, draw, PANVK_SUBQUEUE_COMPUTE);
   gpu_prerast_acquire_arena(cmdbuf, arena);
   if (nested)
      gpu_prerast_acquire_arena(cmdbuf, prerast_arena);
   panlib_tess_setup(&ctx, panlib_1d(1), PANLIB_BARRIER_NONE, td.gpu, src,
                     draw->indirect.count_buffer_dev_addr,
                     draw->indirect.buffer_dev_addr ? draw->indirect.draw_index : 0);
   cs_wait_slots(b, dev->csf.sb.all_iters_mask);

   /* No C-level exit from a cs_while body, it would leave the block open.
    * Allocation failures are recorded on the command buffer instead.
    */
   bool ok = true;
   cs_while(b, MALI_CS_CONDITION_ALWAYS, cs_undef()) {
      gpu_prerast_sync32_take(cmdbuf, PANVK_SUBQUEUE_COMPUTE, TD(free));

      panlib_tess_step(&ctx, panlib_1d(1), PANLIB_BARRIER_NONE, td.gpu,
                       tcs_stat);
      cs_wait_slots(b, dev->csf.sb.all_iters_mask);

      ok &= gpu_prerast_vs(cmdbuf, &vs_src, vs, arena,
                           PANVK_GPU_PRERAST_POLY_NONE, false,
                           PANVK_CSF_BARRIER_WAIT, TD(chunk_vertex));
      cs_wait_slots(b, dev->csf.sb.all_iters_mask);

      if (shader_uses_sysval(tcs_v, graphics, gpu_prerast_params)) {
         *(uint64_t *)((uint8_t *)push.cpu +
                       shader_remapped_sysval_offset(
                          tcs_v, sysval_offset(graphics, gpu_prerast_params))) =
            arena + PANVK_GPU_PRERAST_PARAMS_OFFSET;
      }
      if (shader_uses_sysval(tcs_v, graphics, gpu_prerast_tess)) {
         *(uint64_t *)((uint8_t *)push.cpu +
                       shader_remapped_sysval_offset(
                          tcs_v, sysval_offset(graphics, gpu_prerast_tess))) =
            TD(p);
      }
      gpu_prerast_launch_cs_desc(cmdbuf, tcs_v, &cmdbuf->state.gfx.tcs.desc,
                                 push.gpu, tsd, &tcs_dispatch);
      cs_wait_slots(b, dev->csf.sb.all_iters_mask);

      panlib_tessellate(&ctx, tess_grid, PANLIB_BARRIER_NONE, TD(p), domain,
                        POLY_TESS_MODE_COUNT);
      cs_wait_slots(b, dev->csf.sb.all_iters_mask);
      panlib_tess_draw(&ctx, panlib_1d(1), PANLIB_BARRIER_NONE, td.gpu,
                       c_invs, c_prims, prims_generated);
      cs_wait_slots(b, dev->csf.sb.all_iters_mask);
      panlib_tessellate(&ctx, tess_grid, PANLIB_BARRIER_NONE, TD(p), domain,
                        POLY_TESS_MODE_WITH_COUNTS);
      cs_wait_slots(b, dev->csf.sb.all_iters_mask);

      if (nested) {
         /* TES as the gpu_prerast VS over this iteration's index range. */
         cmdbuf->state.gfx.vs.shader = tes;
         gfx_state_set_dirty(cmdbuf, VS);
         gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
         ok &= prepare_descs(cmdbuf, &out) == VK_SUCCESS &&
               gpu_prerast_vs(cmdbuf, &out, tes, prerast_arena, poly_mode,
                              false, PANVK_CSF_BARRIER_WAIT, 0);
         if (gs) {
            cs_wait_slots(b, dev->csf.sb.all_iters_mask);
            ok &= gpu_prerast_gs_run(
               cmdbuf, &out, prerast_arena,
               prerast_arena + PANVK_GPU_PRERAST_RECORD_OFFSET,
               prerast_arena + PANVK_GPU_PRERAST_INDEX_OFFSET,
               TD(gs_prim_base), PANVK_CSF_BARRIER_WAIT);
         }
         if (xfb) {
            gpu_prerast_xfb(cmdbuf, &out, prerast_arena, PANLIB_BARRIER_NONE);
            cs_wait_slots(b, dev->csf.sb.all_iters_mask);
         }
         cmdbuf->state.gfx.vs.shader = vs;
         gfx_state_set_dirty(cmdbuf, VS);
         gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
      }

      gpu_prerast_sync32_give(cmdbuf, PANVK_SUBQUEUE_COMPUTE, TD(ready));
      cs_loop_conditional_break(b, __loop, MALI_CS_CONDITION_EQUAL,
                                gpu_prerast_load_flag(b, TD(more)));
   }

   /* Vertex/tiler queue: draw each range once the compute side published
    * it, then hand the arenas back to the compute side. The render context
    * is set up here so that no one-time setup lands in the loop body.
    */
   if (!inherits_render_ctx(cmdbuf))
      ok &= get_render_ctx(cmdbuf) == VK_SUCCESS;

   struct panvk_draw_info raster = out;
   cs_while(vt, MALI_CS_CONDITION_ALWAYS, cs_undef()) {
      gpu_prerast_sync32_take(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER, TD(ready));

      cmdbuf->state.gfx.vs.shader = tes;
      gfx_state_set_dirty(cmdbuf, VS);
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
      if (nested)
         gpu_prerast_draw_generated(cmdbuf, &raster, prerast_arena, poly_mode);
      else
         panvk_cmd_draw(cmdbuf, raster);
      cmdbuf->state.gfx.vs.shader = vs;
      gfx_state_set_dirty(cmdbuf, VS);
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);

      /* Read "more" before giving the arenas back, the next compute
       * iteration rewrites it.
       */
      cs_wait_slots(vt, dev->csf.sb.all_iters_mask);
      struct cs_index more = gpu_prerast_load_flag(vt, TD(more));
      struct cs_index more_keep = cs_scratch_reg32(vt, 4);
      cs_add_imm32(vt, more_keep, more, 0);
      gpu_prerast_sync32_give(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER, TD(free));
      cs_loop_conditional_break(vt, __loop, MALI_CS_CONDITION_EQUAL,
                                more_keep);
   }
#undef TD

   if (nested)
      gpu_prerast_release_arena(cmdbuf, prerast_arena);
   gpu_prerast_release_arena(cmdbuf, arena);

   if (!ok)
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
}

static bool
gpu_prerast_needed(const struct panvk_cmd_buffer *cmdbuf,
                   const struct panvk_draw_info *draw)
{
   return !draw->gpu_prerast_generated &&
          (draw->prim == MESA_PRIM_PATCHES || cmdbuf->state.gfx.gs.shader ||
           gpu_prerast_xfb_active(cmdbuf) ||
           panvk_per_arch(gpu_prerast_poly_mode)(cmdbuf, draw) !=
              PANVK_GPU_PRERAST_POLY_NONE);
}

/* Returns true if the draw was split into draws that fit the arena. */
static bool
gpu_prerast_split(struct panvk_cmd_buffer *cmdbuf,
                  const struct panvk_draw_info *draw)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   struct panvk_draw_info chunk = *draw;

   /* ponytail: indirect draws are lowered one at a time, and the arena caps
    * in the kernels truncate an indirect draw that exceeds them. A CS loop
    * over GPU-side chunks is the upgrade path if that shows up.
    */
   if (draw->indirect.buffer_dev_addr) {
      if (draw->indirect.draw_count <= 1)
         return false;

      chunk.indirect.draw_count = 1;
      for (uint32_t i = 0; i < draw->indirect.draw_count; i++) {
         chunk.indirect.draw_index = i;
         panvk_cmd_draw(cmdbuf, chunk);
         chunk.indirect.buffer_dev_addr += draw->indirect.stride;
      }
      return true;
   }

   uint32_t max_verts =
      panvk_gpu_prerast_record_capacity(vs->gpu_prerast.io.record_stride);
   if (gs)
      max_verts = MIN2(max_verts,
                       panvk_gpu_prerast_gs_cap_slots(&gs->gpu_prerast.gs) /
                          gs->gpu_prerast.gs.invocations);

   /* Tessellation chunks on the GPU, see gpu_prerast_tess(). */
   if (draw->prim == MESA_PRIM_PATCHES)
      return false;

   uint32_t align = 1, overlap = 0, prim_div = 1;
   if ((uint64_t)draw->vertex.count * draw->instance.count <= max_verts)
      return false;

   if (draw->vertex.count <= max_verts) {
      const uint32_t step = max_verts / draw->vertex.count;
      for (uint32_t i = 0; i < draw->instance.count; i += step) {
         chunk.instance.base = draw->instance.base + i;
         chunk.instance.count = MIN2(step, draw->instance.count - i);
         panvk_cmd_draw(cmdbuf, chunk);
      }
      return true;
   }

   switch (draw->prim) {
   case MESA_PRIM_POINTS:
      break;
   case MESA_PRIM_LINES:
      align = prim_div = 2;
      break;
   case MESA_PRIM_TRIANGLES:
      align = prim_div = 3;
      break;
   case MESA_PRIM_LINES_ADJACENCY:
      align = prim_div = 4;
      break;
   case MESA_PRIM_TRIANGLES_ADJACENCY:
      align = prim_div = 6;
      break;
   case MESA_PRIM_LINE_STRIP:
      overlap = 1;
      break;
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      overlap = 3;
      break;
   case MESA_PRIM_TRIANGLE_STRIP:
      align = overlap = 2;
      break;
   default:
      /* ponytail: fans and triangle strips with adjacency are not split by
       * vertex range, the kernels need a first-vertex/strip-end input for
       * that.
       */
      gpu_prerast_unhandled("vertex count over the arena capacity");
      return true;
   }

   const uint32_t step =
      max_verts > overlap ? (max_verts - overlap) / align * align : 0;
   /* ponytail: restart segments would straddle chunks, a restart-aware split
    * needs the segment starts, which live in the index buffer.
    */
   if (!step || (overlap && draw->index.index_size &&
                 draw->index.restart_enable)) {
      gpu_prerast_unhandled("vertex count over the arena capacity");
      return true;
   }

   chunk.instance.count = 1;
   for (uint32_t inst = 0; inst < draw->instance.count; inst++) {
      chunk.instance.base = draw->instance.base + inst;
      for (uint32_t i = 0; i + overlap < draw->vertex.count; i += step) {
         chunk.vertex.count = MIN2(step + overlap, draw->vertex.count - i);
         if (draw->index.index_size)
            chunk.index.offset = draw->index.offset + i;
         else
            chunk.vertex.base = draw->vertex.base + i;
         chunk.gpu_prerast_prim_base = i / prim_div;
         panvk_cmd_draw(cmdbuf, chunk);
      }
   }
   return true;
}

static void
panvk_cmd_draw(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_info draw)
{
   const struct panvk_shader_variant *vs =
      raster_vs_variant(cmdbuf, draw.gpu_prerast_generated);
   VkResult result;

   /* If there's no vertex shader, we can skip the draw. With tessellation,
    * the TES rasterizes and the VS may be empty.
    */
   /* A GS that writes no outputs still generates primitives for the query. */
   const bool gs_counts_prims = cmdbuf->state.gfx.gs.shader &&
                                cmdbuf->state.gfx.prims_generated_query.ptr;
   if (draw.prim != MESA_PRIM_PATCHES && !panvk_priv_mem_check_alloc(vs->spd) &&
       (draw.gpu_prerast_generated ||
        (!gpu_prerast_xfb_active(cmdbuf) && !gs_counts_prims)))
      return;

   /* Count once per API draw, before gpu_prerast splits or lowers it. */
   if (!draw.gpu_prerast_generated && !draw.pstats_counted) {
      panvk_per_arch(cmd_pstats_draw)(cmdbuf, &draw);
      draw.pstats_counted = true;
   }

   if (gpu_prerast_needed(cmdbuf, &draw) && gpu_prerast_split(cmdbuf, &draw))
      return;

   if (draw.indirect.buffer_dev_addr)
      account_tiler_work(cmdbuf, (uint64_t)draw.indirect.draw_count * 256);
   else
      account_tiler_work(cmdbuf,
                         (uint64_t)draw.vertex.count * draw.instance.count);

   /* Needs to be done before get_fs() is called because it depends on
    * fs.required being initialized. */
   cmdbuf->state.gfx.fs.required =
      fs_required(&cmdbuf->state.gfx, &cmdbuf->vk.dynamic_graphics_state);

   if (cmdbuf->state.gfx.vi.base_instance != draw.instance.base) {
      cmdbuf->state.gfx.vi.base_instance = draw.instance.base;
      gfx_state_set_dirty(cmdbuf, BASE_INSTANCE);
   }

   /* Force new descriptors and push uniform blocks to be allocated because
    * we're going to patch the descriptors and copy from the indirect buffer
    * into the uniform block.
    */
   if (draw.indirect.buffer_dev_addr) {
      gfx_state_set_dirty(cmdbuf, BASE_INSTANCE);
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   }

   result = prepare_descs(cmdbuf, &draw);
   if (result != VK_SUCCESS)
      return;

   if (!draw.gpu_prerast_generated && draw.prim == MESA_PRIM_PATCHES) {
      gpu_prerast_tess(cmdbuf, &draw);
      return;
   }

   if (!draw.gpu_prerast_generated && gpu_prerast_draw(cmdbuf, &draw))
      return;

   if (gpu_prerast_needed(cmdbuf, &draw)) {
      gpu_prerast_unhandled("shader variants missing");
      return;
   }

   /* For indirect draws, we need to patch the descriptors we just emitted */
   if (draw.indirect.buffer_dev_addr)
      patch_vs_attribs(cmdbuf, &draw, PANVK_SUBQUEUE_VERTEX_TILER);

   result = prepare_draw(cmdbuf, &draw);
   if (result != VK_SUCCESS)
      return;

   if (!draw.prims_generated_counted)
      update_prims_generated_query(cmdbuf, &draw);

   if (draw.indirect.buffer_dev_addr)
      launch_indirect_draw(cmdbuf, &draw);
   else
      launch_draw(cmdbuf, &draw);
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
      .prim = panvk_get_client_prim(cmdbuf),
   };

   panvk_cmd_draw(cmdbuf, draw);
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
      .index = panvk_draw_info_index(cmdbuf, firstIndex),
      .vertex.base = vertexOffset,
      .vertex.count = indexCount,
      .instance.count = instanceCount,
      .instance.base = firstInstance,
      .prim = panvk_get_client_prim(cmdbuf),
   };

   panvk_cmd_draw(cmdbuf, draw);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawMultiEXT)(VkCommandBuffer commandBuffer,
                                uint32_t drawCount,
                                const VkMultiDrawInfoEXT *pVertexInfo,
                                uint32_t instanceCount,
                                uint32_t firstInstance,
                                uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (instanceCount == 0 || drawCount == 0)
      return;

   assert(firstInstance < INT32_MAX);

   const uint8_t *cursor = (const uint8_t *)pVertexInfo;
   for (uint32_t i = 0; i < drawCount; i++) {
      const VkMultiDrawInfoEXT *info = (const VkMultiDrawInfoEXT *)cursor;

      if (info->vertexCount != 0) {
         assert(info->firstVertex < INT32_MAX);

         struct panvk_draw_info draw = {
            .vertex.base = info->firstVertex,
            .vertex.count = info->vertexCount,
            .instance.base = firstInstance,
            .instance.count = instanceCount,
            .indirect.draw_index = i,
            .prim = panvk_get_client_prim(cmdbuf),
         };

         panvk_cmd_draw(cmdbuf, draw);
      }

      cursor += stride;
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawMultiIndexedEXT)(VkCommandBuffer commandBuffer,
                                       uint32_t drawCount,
                                       const VkMultiDrawIndexedInfoEXT *pIndexInfo,
                                       uint32_t instanceCount,
                                       uint32_t firstInstance,
                                       uint32_t stride,
                                       const int32_t *pVertexOffset)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (instanceCount == 0 || drawCount == 0)
      return;

   assert(firstInstance < INT32_MAX);

   const uint8_t *cursor = (const uint8_t *)pIndexInfo;
   for (uint32_t i = 0; i < drawCount; i++) {
      const VkMultiDrawIndexedInfoEXT *info =
         (const VkMultiDrawIndexedInfoEXT *)cursor;

      if (info->indexCount != 0) {
         int32_t vertex_offset =
            pVertexOffset != NULL ? *pVertexOffset : info->vertexOffset;

         struct panvk_draw_info draw = {
            .index = panvk_draw_info_index(cmdbuf, info->firstIndex),
            .vertex.base = vertex_offset,
            .vertex.count = info->indexCount,
            .instance.count = instanceCount,
            .instance.base = firstInstance,
            .indirect.draw_index = i,
            .prim = panvk_get_client_prim(cmdbuf),
         };

         panvk_cmd_draw(cmdbuf, draw);
      }

      cursor += stride;
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
      .prim = panvk_get_client_prim(cmdbuf),
   };

   panvk_cmd_draw(cmdbuf, draw);
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
      .index = panvk_draw_info_index(cmdbuf, 0),
      .indirect.buffer_dev_addr = panvk_buffer_gpu_ptr(buffer, offset),
      .indirect.draw_count = drawCount,
      .indirect.stride = stride,
      .prim = panvk_get_client_prim(cmdbuf),
   };

   panvk_cmd_draw(cmdbuf, draw);
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
      .prim = panvk_get_client_prim(cmdbuf),
   };

   panvk_cmd_draw(cmdbuf, draw);
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
      .index = panvk_draw_info_index(cmdbuf, 0),
      .indirect.buffer_dev_addr = panvk_buffer_gpu_ptr(buffer, offset),
      .indirect.count_buffer_dev_addr =
         panvk_buffer_gpu_ptr(count_buffer, countBufferOffset),
      .indirect.draw_count = maxDrawCount,
      .indirect.stride = stride,
      .prim = panvk_get_client_prim(cmdbuf),
   };

   panvk_cmd_draw(cmdbuf, draw);
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

   /* patch inheritance rendering info in-place for Android efr support */
   VkCommandBufferInheritanceRenderingInfo local_inheritance_info;
   VkFormat local_color_format;
   if (vk_android_is_efr_inheritance_rendering_info(
          pBeginInfo->pInheritanceInfo, inheritance_info)) {
      inheritance_info = vk_android_get_efr_inheritance_rendering_info(
         pBeginInfo->pInheritanceInfo, inheritance_info,
         &local_inheritance_info, &local_color_format);
   }

   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;

   render->first_provoking_vertex = U_TRISTATE_UNSET;
   render->maybe_set_tds_provoking_vertex = NULL;
   render->maybe_set_fbds_provoking_vertex = NULL;
   render->suspended = false;
   render->flags = inheritance_info->flags;

   gfx_state_set_dirty(cmdbuf, RENDER_STATE);
   memset(&render->color_attachments, 0, sizeof(render->color_attachments));
   memset(&render->z_attachment, 0, sizeof(render->z_attachment));
   memset(&render->s_attachment, 0, sizeof(render->s_attachment));
   cmdbuf->state.gfx.render.bound_attachments = 0;

   render->view_mask = inheritance_info->viewMask;
   render->layer_count = inheritance_info->viewMask ?
      util_last_bit(inheritance_info->viewMask) : 0;

   /* If a draw was performed, the inherited sample count should match our current sample count */
   const uint32_t sample_count = inheritance_info->rasterizationSamples;
   assert(render->fb.layout.sample_count == 0 ||
          render->fb.layout.sample_count == sample_count);
   render->fb.layout = (struct pan_fb_layout) {
      .sample_count = sample_count,
      .rt_count = MAX2(inheritance_info->colorAttachmentCount, 1),

      .tile_size_px = render->fb.layout.tile_size_px,
      .tile_rt_alloc_B = render->fb.layout.tile_rt_alloc_B,
      .tile_rt_budget_B =
         pan_query_optimal_tib_size(PAN_ARCH, phys_dev->model),
      .tile_z_budget_B =
         pan_query_optimal_z_tib_size(PAN_ARCH, phys_dev->model),
   };
   render->fb.nr_samples = sample_count;

   assert(inheritance_info->colorAttachmentCount <= PAN_MAX_RTS);

   for (uint32_t i = 0; i < inheritance_info->colorAttachmentCount; i++) {
      render->bound_attachments |= MESA_VK_RP_ATTACHMENT_COLOR_BIT(i);
      render->color_attachments.fmts[i] =
         inheritance_info->pColorAttachmentFormats[i];
      render->color_attachments.samples[i] =
         inheritance_info->rasterizationSamples;
      render->fb.layout.rt_formats[i] =
         vk_format_to_pipe_format(inheritance_info->pColorAttachmentFormats[i]);
   }

   if (inheritance_info->depthAttachmentFormat) {
      render->bound_attachments |= MESA_VK_RP_ATTACHMENT_DEPTH_BIT;
      render->z_attachment.fmt = inheritance_info->depthAttachmentFormat;
      render->fb.layout.z_format =
         vk_format_to_pipe_format(inheritance_info->depthAttachmentFormat);
   }

   if (inheritance_info->stencilAttachmentFormat) {
      render->bound_attachments |= MESA_VK_RP_ATTACHMENT_STENCIL_BIT;
      render->s_attachment.fmt = inheritance_info->stencilAttachmentFormat;
      render->fb.layout.s_format =
         vk_format_to_pipe_format(inheritance_info->stencilAttachmentFormat);
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

static void
invalidate_initial_attachment_crcs(struct panvk_cmd_buffer *cmdbuf,
                                   const VkRenderingInfo *rendering)
{
   if (rendering->flags & VK_RENDERING_RESUMING_BIT)
      return;

   uint64_t addrs[PAN_MAX_RTS];
   uint32_t addr_count = 0;

   for (uint32_t i = 0; i < rendering->colorAttachmentCount; i++) {
      const VkRenderingAttachmentInfo *att = &rendering->pColorAttachments[i];

      const VkRenderingAttachmentInitialLayoutInfoMESA *initial =
         vk_find_struct_const(att->pNext,
                              RENDERING_ATTACHMENT_INITIAL_LAYOUT_INFO_MESA);

      bool host_preinit =
         initial && initial->initialLayout == VK_IMAGE_LAYOUT_PREINITIALIZED;
      bool discard = att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ||
                     att->loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      if (!host_preinit && !discard)
         continue;

      VK_FROM_HANDLE(panvk_image_view, iview, att->imageView);
      if (!iview || iview->pview.first_level != 0)
         continue;

      struct panvk_image *image =
         container_of(iview->vk.image, struct panvk_image, vk);
      uint64_t header_addr = panvk_image_plane_crc_header_addr(image);

      if (!header_addr)
         continue;

      bool duplicate = false;
      for (uint32_t j = 0; j < addr_count; j++) {
         duplicate |= addrs[j] == header_addr;
      }

      if (!duplicate) {
         addrs[addr_count++] = header_addr;
      }
   }

   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_FRAGMENT);

   for (uint32_t i = 0; i < addr_count; i++) {
      panvk_per_arch(cmd_invalidate_crc)(b, addrs[i]);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginRendering)(VkCommandBuffer commandBuffer,
                                  const VkRenderingInfo *pRenderingInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   ASSERTED struct panvk_cmd_graphics_state *state = &cmdbuf->state.gfx;

   /* patch rendering info in-place for Android efr support */
   VkRenderingInfo local_rendering_info;
   VkRenderingAttachmentInfo local_color_att;
   if (vk_android_is_efr_rendering_info(pRenderingInfo)) {
      pRenderingInfo = vk_android_get_efr_rendering_info(
         pRenderingInfo, &local_rendering_info, &local_color_att);
   }

   bool resuming = pRenderingInfo->flags & VK_RENDERING_RESUMING_BIT;

   panvk_per_arch(cmd_init_render_state)(cmdbuf, pRenderingInfo);

   /* Renderpass lowering can fold an initial layout transition into
    * CmdBeginRendering() and report the old layout through
    * VkRenderingAttachmentInitialLayoutInfoMESA instead of an image barrier.
    *
    * If the previous image content could have been modified by the host
    * (PREINITIALIZED), we need to invalidate CRC.
    */
   invalidate_initial_attachment_crcs(cmdbuf, pRenderingInfo);

   /* If we're not resuming, the FBD should be NULL. */
   assert(!state->render.fbds.gpu || resuming);

   panvk_per_arch(panvk_instr_begin_work)(PANVK_SUBQUEUE_VERTEX_TILER, cmdbuf,
                                          PANVK_INSTR_WORK_TYPE_RENDER);
   panvk_per_arch(panvk_instr_begin_work)(PANVK_SUBQUEUE_FRAGMENT, cmdbuf,
                                          PANVK_INSTR_WORK_TYPE_RENDER);
}

static void
set_run_fullscreen_tiler_flags(struct cs_builder *b, uint32_t layer_index,
                               uint32_t view_mask)
{
   /* For RUN_FULLSCREEN, HW expects 0 for all flags. Only scissor_array_enable
    * and the layer selection fields can be set to other values. */
   struct mali_primitive_flags_packed tiler_flags = {0};
   pan_pack(&tiler_flags, PRIMITIVE_FLAGS, cfg) {
      /* These default to non-zero */
      cfg.low_depth_cull = false;
      cfg.high_depth_cull = false;
#if PAN_ARCH < 14
      cfg.view_mask = view_mask;
#else
      cfg.layer_index = layer_index;
#endif
   }

   cs_update_vt_ctx(b)
      cs_move32_to(b, cs_sr_reg32(b, IDVS, TILER_FLAGS), tiler_flags.opaque[0]);

#if PAN_ARCH >= 14
   struct mali_primitive_flags_2_packed tiler_flags_2;
   pan_pack(&tiler_flags_2, PRIMITIVE_FLAGS_2, cfg) {
      cfg.view_mask = view_mask;
   }

   cs_update_vt_ctx(b)
      cs_move32_to(b, cs_sr_reg32(b, IDVS, TILER_FLAGS2),
                   tiler_flags_2.opaque[0]);
#endif
}

/* If layer_count is 0, then reads the layer count dynamically from the
 * subqueue context and clears all layers. */
static void
cmd_run_fullscreen(struct panvk_cmd_buffer *cmdbuf, uint64_t dcd,
                   bool ignore_scissor, uint32_t base_layer,
                   uint32_t layer_count)
{
   const struct cs_tracing_ctx *tracing_ctx =
      &cmdbuf->state.cs[PANVK_SUBQUEUE_VERTEX_TILER].tracing;
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   if (!inherits_render_ctx(cmdbuf)) {
      VkResult result = get_render_ctx(cmdbuf);
      if (result != VK_SUCCESS)
         return;
   }

   struct cs_index draw_ptr = cs_scratch_reg64(b, 0);
   struct cs_index tf_tmp = cs_scratch_reg32(b, 2);
#if PAN_ARCH >= 11
   struct cs_index tf2_tmp = cs_scratch_reg32(b, 3);
#endif
#if PAN_ARCH >= 13
   struct cs_index dcd0_tmp = cs_scratch_reg32(b, 4);
   struct cs_index dcd1_tmp = cs_scratch_reg32(b, 5);
   struct cs_index dcd2_tmp = cs_scratch_reg32(b, 6);
#endif
   struct cs_index scissor_tmp = cs_scratch_reg64(b, 8);
#if PAN_ARCH >= 14
   struct cs_index layer = cs_scratch_reg32(b, 10);
#else
   struct cs_index render_ctx_values = cs_scratch_reg64(b, 10);
   struct cs_index tiler_ctx_tmp = cs_scratch_reg64(b, 12);
#endif
   struct cs_index trace_regs = cs_scratch_reg_tuple(b, 14, 4);

   cs_move64_to(b, draw_ptr, dcd);

   /* We need to set our own tiler flags so save them off */
   cs_move_reg32(b, tf_tmp, cs_sr_reg32(b, IDVS, TILER_FLAGS));

#if PAN_ARCH >= 11
   cs_move_reg32(b, tf2_tmp, cs_sr_reg32(b, IDVS, TILER_FLAGS2));
   cs_update_vt_ctx(b) {
      struct mali_primitive_flags_2_packed tiler_flags_2;
      pan_pack(&tiler_flags_2, PRIMITIVE_FLAGS_2, cfg) {
      }
      cs_move32_to(b, cs_sr_reg32(b, IDVS, TILER_FLAGS2),
                   tiler_flags_2.opaque[0]);
   }
#endif

   /* Starting with v13, the DCD flag registers have to match the flag fields
    * in the DrawCallDescriptor we provide in memory so we have to save them
    * off.
    */
#if PAN_ARCH >= 13
   cs_move_reg32(b, dcd0_tmp, cs_sr_reg32(b, IDVS, DCD0));
   cs_move_reg32(b, dcd1_tmp, cs_sr_reg32(b, IDVS, DCD1));
   cs_move_reg32(b, dcd2_tmp, cs_sr_reg32(b, IDVS, DCD2));

   cs_update_vt_ctx(b) {
      cs_load32_to(b, cs_sr_reg32(b, IDVS, DCD0), draw_ptr, 0);
      cs_load32_to(b, cs_sr_reg32(b, IDVS, DCD1), draw_ptr, 1 * 4);
      cs_load32_to(b, cs_sr_reg32(b, IDVS, DCD2), draw_ptr, 5 * 4);
   }
#endif

   /* RUN_FULLSCREEN respects scissors but for some things like barrier draws,
    * we want to ignore the scissor so we have to save off the old one and use
    * a default.
    */
   if (ignore_scissor) {
      struct mali_scissor_packed scissor;
      pan_pack(&scissor, SCISSOR, cfg) {
         cfg.scissor_minimum_x = 0;
         cfg.scissor_minimum_y = 0;
         cfg.scissor_maximum_x = UINT16_MAX;
         cfg.scissor_maximum_y = UINT16_MAX;
      }

      cs_move_reg64(b, scissor_tmp, cs_sr_reg64(b, IDVS, SCISSOR_BOX));

      cs_update_vt_ctx(b) {
         cs_move64_to(b, cs_sr_reg64(b, IDVS, SCISSOR_BOX),
                      scissor.opaque[0] | (uint64_t)scissor.opaque[1] << 32);
      }
   }

   uint32_t render_view_mask = cmdbuf->state.gfx.render.view_mask;
   uint32_t render_layer_count = cmdbuf->state.gfx.render.layer_count;

   /* If render_layer_count is known (non-zero), assert that base_layer and
    * layer_count stay in bounds so we don't select an invalid tiler descriptor
    * or view mask.
    */
   assert(base_layer <= render_layer_count || render_layer_count == 0);
   assert(layer_count <= render_layer_count - base_layer ||
          render_layer_count == 0);

   if (layer_count == 0) {
      assert(base_layer == 0);

#if PAN_ARCH >= 14
      cs_load32_to(b, layer, cs_subqueue_ctx_reg(b),
                   offsetof(struct panvk_cs_subqueue_context,
                            render.layer_count));

      /* view_mask is always 0, so we can set TILER_FLAGS2 outside of the
       * loop */
      struct mali_primitive_flags_2_packed tiler_flags_2;
      pan_pack(&tiler_flags_2, PRIMITIVE_FLAGS_2, cfg) {
         cfg.view_mask = 0;
      }
      cs_update_vt_ctx(b) {
         cs_move32_to(b, cs_sr_reg32(b, IDVS, TILER_FLAGS2),
                      tiler_flags_2.opaque[0]);
      }

      cs_while(b, MALI_CS_CONDITION_GREATER, layer) {
         cs_add_imm32(b, layer, layer, -1);

         /* Dynamic set_run_fullscreen_tiler_flags(b, layer, 0)
          *
          * The only field that needs to be set in TILER_FLAGS is layer_index,
          * bits 24 to 32 */
         cs_update_vt_ctx(b) {
            cs_lshift_imm32(b, cs_sr_reg32(b, IDVS, TILER_FLAGS), layer, 24);
         }

         cs_trace_run_fullscreen(b, tracing_ctx, trace_regs, 0, draw_ptr);
      }
#else
      struct cs_index tiler_ctx_addr = cs_sr_reg64(b, IDVS, TILER_CTX);

      /* The two fields we want to read are adjacent, so we can issue a single
       * read */
      STATIC_ASSERT(
         offsetof(struct panvk_cs_subqueue_context, render.last_td_fullscreen_tiler_flags) ==
         offsetof(struct panvk_cs_subqueue_context, render.td_count) + 4);
      struct cs_index td_count = cs_extract32(b, render_ctx_values, 0);
      struct cs_index last_td_tiler_flags =
         cs_extract32(b, render_ctx_values, 1);

      cs_load64_to(b, render_ctx_values, cs_subqueue_ctx_reg(b),
                   offsetof(struct panvk_cs_subqueue_context, render.td_count));

      cs_add_imm64(b, tiler_ctx_tmp, tiler_ctx_addr, 0);

      set_run_fullscreen_tiler_flags(b, 0,
                                     BITFIELD_MASK(MAX_LAYERS_PER_TILER_DESC));

      cs_while(b, MALI_CS_CONDITION_GREATER, td_count) {
         cs_add_imm32(b, td_count, td_count, -1);

         cs_if(b, MALI_CS_CONDITION_EQUAL, td_count) {
            /* Last TD gets alternate tiler flags which may have a partial
             * view mask */
            cs_update_vt_ctx(b) {
                cs_add_imm32(b, cs_sr_reg32(b, IDVS, TILER_FLAGS),
                            last_td_tiler_flags, 0);
            }
         }

         cs_trace_run_fullscreen(b, tracing_ctx, trace_regs, 0, draw_ptr);

         cs_update_vt_ctx(b) {
            cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr,
                         pan_size(TILER_CONTEXT));
         }
      }

      cs_update_vt_ctx(b) {
        cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_tmp, 0);
      }
#endif
   } else if (render_view_mask) {
      /* Multiview always fits inside a single tiler context */
      uint32_t view_mask =
         BITFIELD_RANGE(base_layer, layer_count) & render_view_mask;

      if (view_mask != 0) {
         set_run_fullscreen_tiler_flags(b, 0, view_mask);
         cs_trace_run_fullscreen(b, tracing_ctx, trace_regs, 0, draw_ptr);
      }
   } else {
#if PAN_ARCH >= 14
      for (uint32_t l = base_layer; l < base_layer + layer_count; l++) {
         set_run_fullscreen_tiler_flags(b, l, 0);
         cs_trace_run_fullscreen(b, tracing_ctx, trace_regs, 0, draw_ptr);
      }
#else
      /* RUN_FULLSCREEN uses IDVS.TILER_CTX and a view mask that is relative to
       * the tiler descriptor. */
      uint32_t first_td = base_layer / MAX_LAYERS_PER_TILER_DESC;
      uint32_t last_td =
         (base_layer + layer_count - 1) / MAX_LAYERS_PER_TILER_DESC;
      struct cs_index tiler_ctx_addr = cs_sr_reg64(b, IDVS, TILER_CTX);

      if (first_td) {
         cs_update_vt_ctx(b) {
            cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr,
                     first_td * pan_size(TILER_CONTEXT));
         }
      }

      for (uint32_t td = first_td; td <= last_td; td++) {
         uint32_t td_base = td * MAX_LAYERS_PER_TILER_DESC;
         uint32_t first = MAX2(base_layer, td_base);
         uint32_t last =
            MIN2(base_layer + layer_count, td_base + MAX_LAYERS_PER_TILER_DESC);
         uint32_t view_mask = BITFIELD_RANGE(first - td_base, last - first);

         set_run_fullscreen_tiler_flags(b, 0, view_mask);
         cs_trace_run_fullscreen(b, tracing_ctx, trace_regs, 0, draw_ptr);

         if (td != last_td) {
            cs_update_vt_ctx(b) {
               cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr,
                        pan_size(TILER_CONTEXT));
            }
         }
      }

      if (last_td) {
         cs_update_vt_ctx(b) {
            cs_add_imm64(b, tiler_ctx_addr, tiler_ctx_addr,
                     -(last_td * pan_size(TILER_CONTEXT)));
         }
      }
#endif
   }

   cs_update_vt_ctx(b) {
      cs_move_reg32(b, cs_sr_reg32(b, IDVS, TILER_FLAGS), tf_tmp);
#if PAN_ARCH >= 11
      cs_move_reg32(b, cs_sr_reg32(b, IDVS, TILER_FLAGS2), tf2_tmp);
#endif
#if PAN_ARCH >= 13
      cs_move_reg32(b, cs_sr_reg32(b, IDVS, DCD0), dcd0_tmp);
      cs_move_reg32(b, cs_sr_reg32(b, IDVS, DCD1), dcd1_tmp);
      cs_move_reg32(b, cs_sr_reg32(b, IDVS, DCD2), dcd2_tmp);
#endif
      if (ignore_scissor)
         cs_move_reg64(b, cs_sr_reg64(b, IDVS, SCISSOR_BOX), scissor_tmp);
   }
}

void
panvk_per_arch(cmd_fb_barrier)(struct panvk_cmd_buffer *cmdbuf)
{
   /* For inherited secondary cmdbufs, we fetch the layer count dynamically */
   if (cmdbuf->state.gfx.render.layer_count == 0 &&
       !inherits_render_ctx(cmdbuf))
      return;

   struct pan_ptr zsd = panvk_cmd_alloc_desc(cmdbuf, DEPTH_STENCIL);
   if (!zsd.gpu)
      return;

   pan_cast_and_pack(zsd.cpu, DEPTH_STENCIL, cfg) {
      cfg.stencil_test_enable = false;
      cfg.depth_write_enable = false;
      cfg.depth_function = MALI_FUNC_ALWAYS;
   };

   struct pan_ptr dcd = panvk_cmd_alloc_desc(cmdbuf, DRAW);
   if (!dcd.gpu)
      return;

   pan_cast_and_pack(dcd.cpu, DRAW, cfg) {
      cfg.flags_0.allow_forward_pixel_to_kill = false;
      cfg.flags_0.allow_forward_pixel_to_be_killed = false;
      cfg.flags_0.primitive_barrier = true;
      cfg.flags_0.occlusion_query = MALI_OCCLUSION_MODE_DISABLED;

      cfg.flags_2.read_mask = 0;
      cfg.flags_2.write_mask = 0;
#if PAN_ARCH >= 11
      cfg.flags_2.no_shader_depth_read = true;
      cfg.flags_2.no_shader_stencil_read = true;
#endif

      cfg.depth_stencil = zsd.gpu;
   };

   cmd_run_fullscreen(cmdbuf, dcd.gpu, true, 0,
                      cmdbuf->state.gfx.render.layer_count);
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
   cs_vt_end(b, cs_defer_indirect());
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
      cs_vt_end(b, cs_defer(SB_WAIT_ITER(x), SB_ID(DEFERRED_SYNC)));
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

   cs_add_imm64(b, vt_sync_point,
                cs_progress_seqno_reg(b, PANVK_SUBQUEUE_VERTEX_TILER),
                rel_vt_sync_point);

   panvk_instr_sync64_wait(cmdbuf, PANVK_SUBQUEUE_FRAGMENT, false,
                           MALI_CS_CONDITION_GREATER, vt_sync_point,
                           vt_sync_addr);
}

static uint32_t
calc_tiler_oom_handler_idx(struct panvk_cmd_buffer *cmdbuf)
{
   const struct pan_fb_layout *fb = &cmdbuf->state.gfx.render.fb.layout;
   const bool has_zs_crc_ext = render_needs_zs_crc_ext(cmdbuf);

   return get_tiler_oom_handler_idx(has_zs_crc_ext, fb->rt_count);
}

static void
setup_tiler_oom_ctx(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_FRAGMENT);

   uint32_t layer_count = render->layer_count;
   uint32_t td_count = DIV_ROUND_UP(layer_count, MAX_LAYERS_PER_TILER_DESC);

   struct cs_index counter = cs_scratch_reg32(b, 1);
   cs_move32_to(b, counter, 0);
   cs_store32(b, counter, cs_subqueue_ctx_reg(b),
              TILER_OOM_CTX_FIELD_OFFSET(counter));

   struct cs_index fbd_ptr_reg = cs_scratch_reg64(b, 6);
#if PAN_ARCH >= 14
   cs_add_imm64(b, fbd_ptr_reg, cs_sr_reg64(b, FRAGMENT, FBD_POINTER), 0);
#else
   const struct pan_fb_layout *fb = &render->fb.layout;
   const bool has_zs_crc_ext = render_needs_zs_crc_ext(cmdbuf);

   struct mali_framebuffer_pointer_packed fb_tag;
   pan_pack(&fb_tag, FRAMEBUFFER_POINTER, cfg) {
      cfg.zs_crc_extension_present = has_zs_crc_ext;
      cfg.render_target_count = fb->rt_count;
   }

   cs_add_imm64(b, fbd_ptr_reg, cs_sr_reg64(b, FRAGMENT, FBD_POINTER),
                -(int32_t)fb_tag.opaque[0]);
#endif

   /* The OOM handler may use both spill and final stores. Preserve every
    * possible CRC state address so the first IR invocation can invalidate them
    * all.
    */
   uint64_t crc_addrs[PAN_MAX_RTS * 2] = {};
   uint32_t crc_addr_count = 0;

   const struct pan_fb_store *stores[] = {
      &render->fb.spill.store,
      &render->fb.store,
   };

   for (uint32_t s = 0; s < ARRAY_SIZE(stores); s++) {
      for (uint32_t rt = 0; rt < render->fb.layout.rt_count; rt++) {
         const struct pan_fb_store_target *target = &stores[s]->rts[rt];
         uint64_t addr = target->store ? target->crc_header_addr : 0;

         if (!addr)
            continue;

         bool duplicate = false;
         for (uint32_t i = 0; i < crc_addr_count; i++) {
            if (crc_addrs[i] == addr) {
               duplicate = true;
               break;
            }
         }

         if (!duplicate)
            crc_addrs[crc_addr_count++] = addr;
      }
   }

   assert(crc_addr_count <= ARRAY_SIZE(crc_addrs));

   cs_move32_to(b, counter, crc_addr_count);
   cs_store32(b, counter, cs_subqueue_ctx_reg(b),
              TILER_OOM_CTX_FIELD_OFFSET(crc_header_addr_count));

   static_assert(ARRAY_SIZE(crc_addrs) % 4 == 0,
                 "crc_addrs must be a multiple of four");
   struct cs_index addr_regs = cs_scratch_reg_tuple(b, 8, 8);

   for (uint32_t base = 0; base < ARRAY_SIZE(crc_addrs); base += 4) {
      for (uint32_t i = 0; i < 4; i++) {
         cs_move64_to(b, cs_extract64(b, addr_regs, i * 2),
                      crc_addrs[base + i]);
      }

      cs_store(b, addr_regs, cs_subqueue_ctx_reg(b), BITFIELD_MASK(8),
               TILER_OOM_CTX_FIELD_OFFSET(crc_header_addrs) +
                  base * sizeof(crc_addrs[0]));
   }

   cs_store64(b, fbd_ptr_reg, cs_subqueue_ctx_reg(b),
              TILER_OOM_CTX_FIELD_OFFSET(layer_fbd_ptr));

   for (uint32_t ir_pass = 0; ir_pass < PANVK_IR_PASS_COUNT; ir_pass++) {
      const uint32_t ir_descs_offset =
         TILER_OOM_CTX_FIELD_OFFSET(ir_descs) + (sizeof(uint64_t) * ir_pass);
      struct cs_index ir_fbds_reg = cs_scratch_reg64(b, 2);

      cs_move64_to(b, ir_fbds_reg, render->ir.fbds[ir_pass]);
      cs_store64(b, ir_fbds_reg, cs_subqueue_ctx_reg(b), ir_descs_offset);
   }

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

static uint32_t
pack_32_2x16(uint16_t lo, uint16_t hi)
{
   return (((uint32_t)hi) << 16) | (uint32_t)lo;
}

#if PAN_ARCH >= 14
static void
cs_emit_static_fragment_state(struct cs_builder *b,
                              struct panvk_cmd_buffer *cmdbuf)
{
   /* Emit the static fragment staging registers. These don't change per-layer. */

   const struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
   const struct pan_fb_layout *fb = &render->fb.layout;

   const uint8_t sample_count = render->fb.layout.sample_count;

   const struct pan_fb_bbox fb_area_px =
      pan_fb_bbox_from_xywh(0, 0, fb->width_px, fb->height_px);
   const struct pan_fb_bbox bbox_px =
      pan_fb_bbox_clamp(fb->tiling_area_px, fb_area_px);

   assert(pan_fb_bbox_is_valid(fb->tiling_area_px));

   struct mali_fragment_bounding_box_packed bbox;
   pan_pack(&bbox, FRAGMENT_BOUNDING_BOX, cfg) {
      cfg.bound_min_x = bbox_px.min_x;
      cfg.bound_min_y = bbox_px.min_y;
      cfg.bound_max_x = bbox_px.max_x;
      cfg.bound_max_y = bbox_px.max_y;
   }

   struct mali_frame_size_packed frame_size;
   pan_pack(&frame_size, FRAME_SIZE, cfg) {
      cfg.width = fb->width_px;
      cfg.height = fb->height_px;
   }

   cs_move32_to(b, cs_sr_reg32(b, FRAGMENT, BBOX_MIN),
                bbox.opaque[0]);
   cs_move32_to(b, cs_sr_reg32(b, FRAGMENT, BBOX_MAX),
                bbox.opaque[1]);
   cs_move32_to(b, cs_sr_reg32(b, FRAGMENT, FRAME_SIZE), frame_size.opaque[0]);
   cs_move64_to(
      b, cs_sr_reg64(b, FRAGMENT, SAMPLE_POSITION_ARRAY_POINTER),
      dev->sample_positions->addr.dev +
         pan_sample_positions_offset(pan_sample_pattern(sample_count)));

   /* Flags 1 */
   struct mali_fragment_flags_1_packed flags1;
   pan_pack(&flags1, FRAGMENT_FLAGS_1, cfg) {
      cfg.sample_count = fb->sample_count;
      cfg.sample_pattern = pan_sample_pattern(fb->sample_count);
      cfg.effective_tile_size = fb->tile_size_px;
      cfg.point_sprite_coord_origin_max_y = false;
      cfg.first_provoking_vertex = get_first_provoking_vertex(cmdbuf);

      assert(fb->rt_count > 0);
      cfg.render_target_count = fb->rt_count;
      cfg.color_buffer_allocation = fb->tile_rt_alloc_B;
   }
   cs_move32_to(b, cs_sr_reg32(b, FRAGMENT, FLAGS_1), flags1.opaque[0]);

   /* If we don't know what provoking vertex mode the application wants yet,
    * leave space to patch it later */
   if (cmdbuf->state.gfx.render.first_provoking_vertex == U_TRISTATE_UNSET) {
      cs_maybe(b, &cmdbuf->state.gfx.render.maybe_set_fbds_provoking_vertex)
      {
         /* provoking_vertex flag is bit 14 of Fragment Flags 1. */
         cs_add_imm32(b, cs_sr_reg32(b, FRAGMENT, FLAGS_1),
                      cs_sr_reg32(b, FRAGMENT, FLAGS_1), -(1 << 14));
      }
   }

   /* Leave the remaining RUN_FRAGMENT2 staging registers as zero. */
}
#endif /* PAN_ARCH >= 14 */

static VkResult
issue_fragment_jobs(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct cs_tracing_ctx *tracing_ctx =
      &cmdbuf->state.cs[PANVK_SUBQUEUE_FRAGMENT].tracing;
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_FRAGMENT);
   bool has_oq_chain = cmdbuf->state.gfx.render.oq.chain != 0;

   /* Now initialize the fragment bits. */
   cs_update_frag_ctx(b) {
#if PAN_ARCH >= 14
      cs_emit_static_fragment_state(b, cmdbuf);
#else
      const struct pan_fb_layout *fb = &cmdbuf->state.gfx.render.fb.layout;
      cs_move32_to(b, cs_sr_reg32(b, FRAGMENT, BBOX_MIN),
                   pack_32_2x16(fb->tiling_area_px.min_x,
                                fb->tiling_area_px.min_y));
      cs_move32_to(b, cs_sr_reg32(b, FRAGMENT, BBOX_MAX),
                   pack_32_2x16(fb->tiling_area_px.max_x,
                                fb->tiling_area_px.max_y));
#endif
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

   struct cs_index fbd_pointer = cs_sr_reg64(b, FRAGMENT, FBD_POINTER);

   if (cmdbuf->state.gfx.render.layer_count <= 1) {
#if PAN_ARCH >= 14
      cs_update_frag_ctx(b)
         cs_emit_layer_fragment_state(b, fbd_pointer);
      cs_trace_run_fragment2(b, tracing_ctx, cs_scratch_reg_tuple(b, 0, 4),
                             false, MALI_TILE_RENDER_ORDER_Z_ORDER);
#else
      cs_trace_run_fragment(b, tracing_ctx, cs_scratch_reg_tuple(b, 0, 4),
                            false, MALI_TILE_RENDER_ORDER_Z_ORDER);
#endif
   } else {
      struct cs_index run_fragment_regs = cs_scratch_reg_tuple(b, 0, 4);
      struct cs_index remaining_layers = cs_scratch_reg32(b, 4);

      cs_move32_to(b, remaining_layers, calc_enabled_layer_count(cmdbuf));
      cs_while(b, MALI_CS_CONDITION_GREATER, remaining_layers) {
         cs_add_imm32(b, remaining_layers, remaining_layers, -1);

#if PAN_ARCH >= 14
         cs_update_frag_ctx(b)
            cs_emit_layer_fragment_state(b, fbd_pointer);
         cs_trace_run_fragment2(b, tracing_ctx, run_fragment_regs, false,
                                MALI_TILE_RENDER_ORDER_Z_ORDER);
#else
         cs_trace_run_fragment(b, tracing_ctx, run_fragment_regs, false,
                               MALI_TILE_RENDER_ORDER_Z_ORDER);
#endif

         cs_update_frag_ctx(b)
            cs_add_imm64(b, fbd_pointer, fbd_pointer, fbd_sz);
      }
   }

#if PAN_ARCH == 10
   /* CRC becomes valid only after full-frame fragment completion without IR. */
   mark_crc_valid_after_fragment(b, cmdbuf);
#endif

   struct cs_index sync_addr = cs_scratch_reg64(b, 0);
   struct cs_index sb_update_scratch_regs = cs_scratch_reg_tuple(b, 2, 2);
   struct cs_index add_val = cs_scratch_reg64(b, 4);
   struct cs_index add_val_lo = cs_scratch_reg32(b, 4);
   struct cs_index ringbuf_sync_addr = cs_scratch_reg64(b, 6);
   struct cs_index release_sz = cs_scratch_reg32(b, 8);

   struct cs_index completed = cs_scratch_reg_tuple(b, 10, 4);
   struct cs_index completed_top = cs_scratch_reg64(b, 10);
   struct cs_index completed_bottom = cs_scratch_reg64(b, 12);
   struct cs_index cur_tiler = cs_reg64(b, PANVK_CS_REG_TILER_DESC_PTR);
   struct cs_index tiler_count = cs_reg32(b, 60);
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

   cs_load64_to(b, sync_addr, cs_subqueue_ctx_reg(b),
                offsetof(struct panvk_cs_subqueue_context, syncobjs));
   cs_add_imm64(b, sync_addr, sync_addr,
                PANVK_SUBQUEUE_FRAGMENT * sizeof(struct panvk_cs_sync64));

   cs_iter_sb_update(cmdbuf, PANVK_SUBQUEUE_FRAGMENT, sb_update_scratch_regs,
                     sb_upd_ctx) {
      /* We wait on the current iter, but we signal the next one, so that
       * the next FINISH_FRAGMENT can't start until this one is done (required
       * to guarantee that used heap chunks won't be released prematurely).
       * No need to wait for sb_upd_ctx.next_sb, this is taken care of in
       * the cs_iter_sb_update() preamble.
       */
#if PAN_ARCH >= 11
      const struct cs_async_op async = cs_defer_indirect();

      cs_set_state(b, MALI_CS_SET_STATE_TYPE_SB_SEL_DEFERRED,
                   sb_upd_ctx.regs.next_sb);
#else
      struct cs_async_op async =
         cs_defer(SB_WAIT_ITER(sb_upd_ctx.cur_sb), SB_ITER(sb_upd_ctx.next_sb));
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
               cs_add_imm64(b, cur_tiler, cur_tiler, pan_size(TILER_CONTEXT));
            cs_add_imm32(b, tiler_count, tiler_count, -1);
         }
         cs_frag_end(b, async);
      }

#if PAN_ARCH >= 11
      cs_set_state_imm32(b, MALI_CS_SET_STATE_TYPE_SB_SEL_DEFERRED,
                         SB_ID(DEFERRED_SYNC));
#else
      async = cs_defer(SB_WAIT_ITER(sb_upd_ctx.cur_sb), SB_ID(DEFERRED_SYNC));
#endif

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
#else
         async = cs_defer(SB_WAIT_ITER(sb_upd_ctx.cur_sb), SB_ID(DEFERRED_FLUSH));
#endif
         cs_flush_caches(b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_CLEAN,
                         MALI_CS_OTHER_FLUSH_MODE_NONE, flush_id, async);
#if PAN_ARCH >= 11
         cs_set_state_imm32(b, MALI_CS_SET_STATE_TYPE_SB_SEL_DEFERRED,
                            SB_ID(DEFERRED_SYNC));
#else
         async = cs_defer(SB_WAIT_ITER(sb_upd_ctx.cur_sb), SB_ID(DEFERRED_SYNC));
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

         cs_add_imm64(b, current, next, 0);
      }

      cs_move64_to(b, current, 0);
      cs_store64(
         b, current, cs_subqueue_ctx_reg(b),
         offsetof(struct panvk_cs_subqueue_context, render.ts_chain.head));
      cs_flush_stores(b);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndRendering2KHR)(
   VkCommandBuffer commandBuffer,
   UNUSED const VkRenderingEndInfoKHR *pRenderingEndInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   bool suspending = cmdbuf->state.gfx.render.flags & VK_RENDERING_SUSPENDING_BIT;
   VkResult result;

   if (!suspending) {
      /* If no draw was performed, we should ensure sample count is valid and that we emit tile size */
      panvk_per_arch(cmd_select_tile_size)(cmdbuf);

      const struct pan_fb_load *fb_load = &cmdbuf->state.gfx.render.fb.load;
      bool clear = fb_load->z.in_bounds_load == PAN_FB_LOAD_CLEAR ||
                   fb_load->s.in_bounds_load == PAN_FB_LOAD_CLEAR;
      for (unsigned rt = 0; rt < PAN_MAX_RTS; rt++) {
         if (fb_load->rts[rt].in_bounds_load == PAN_FB_LOAD_CLEAR)
            clear = true;
      }

      if (clear && !inherits_render_ctx(cmdbuf)) {
         bool needs_crc_tiler =
            (cmdbuf->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) &&
            render_needs_crc_patch(cmdbuf);
         result =
            needs_crc_tiler ? get_render_ctx(cmdbuf) : get_fb_descs(cmdbuf);

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

   /* ponytail: a suspended render pass has not issued its fragment jobs yet,
    * so queries ended in it can miss those FS invocations.
    */
   panvk_per_arch(cmd_pstats_flush_pending)(cmdbuf);

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
      panvk_per_arch(cmd_meta_resolve_attachments)(cmdbuf);

   struct panvk_instr_end_args instr_info = {
      .render = {
         .flags = cmdbuf->state.gfx.render.flags,
         .fb = &cmdbuf->state.gfx.render.fb.layout,
      }};
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   panvk_per_arch(panvk_instr_end_work_async)(
      PANVK_SUBQUEUE_VERTEX_TILER, cmdbuf, PANVK_INSTR_WORK_TYPE_RENDER,
      &instr_info, cs_defer(dev->csf.sb.all_iters_mask, 0));
   panvk_per_arch(panvk_instr_end_work_async)(
      PANVK_SUBQUEUE_FRAGMENT, cmdbuf, PANVK_INSTR_WORK_TYPE_RENDER,
      &instr_info, cs_defer(dev->csf.sb.all_iters_mask, 0));
}

static void
emit_scissor_box(struct panvk_cmd_buffer *cmdbuf, struct vk_meta_rect rect)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   struct mali_scissor_packed scissor;
   pan_pack(&scissor, SCISSOR, cfg) {
      cfg.scissor_minimum_x = CLAMP(rect.x0, 0, UINT16_MAX);
      cfg.scissor_minimum_y = CLAMP(rect.y0, 0, UINT16_MAX);
      cfg.scissor_maximum_x = CLAMP(rect.x1 - 1, 0, UINT16_MAX);
      cfg.scissor_maximum_y = CLAMP(rect.y1 - 1, 0, UINT16_MAX);
   }

   cs_update_vt_ctx(b) {
      cs_move64_to(b, cs_sr_reg64(b, IDVS, SCISSOR_BOX),
                   scissor.opaque[0] | ((uint64_t)scissor.opaque[1] << 32));
   }
}

static void
panvk_per_arch(cmd_draw_fullscreen)(struct vk_command_buffer *cmd,
                                    struct vk_meta_device *meta,
                                    uint32_t rect_count,
                                    const struct vk_meta_rect *rects,
                                    uint32_t layer_count)
{
   VkResult result;
   uint64_t bds_gpu, zsd_gpu;
   struct panvk_dcd_flags dcd_flags;
   float z = rects[0].z;
   struct panvk_draw_info draw = {0};
   struct panvk_cmd_buffer *cmdbuf =
      container_of(cmd, struct panvk_cmd_buffer, vk);
   cmdbuf->state.gfx.fs.required =
      fs_required(&cmdbuf->state.gfx, &cmdbuf->vk.dynamic_graphics_state);
   struct vk_rasterization_state rs = cmdbuf->vk.dynamic_graphics_state.rs;
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   const struct panvk_shader_desc_info *vs_desc_info =
      &cmdbuf->state.gfx.vs.shader->desc_info;
   const struct panvk_shader_desc_info *fs_desc_info =
      fs ? &cmdbuf->state.gfx.fs.shader->desc_info : 0;
   const bool depth_write_enable =
      cmdbuf->vk.dynamic_graphics_state.ds.depth.write_enable;
   const bool fixed_func_depth_write =
      depth_write_enable && !(fs && fs->info.fs.writes_depth);
   struct cs_builder *b =
      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);

   set_provoking_vertex_mode(cmdbuf, U_TRISTATE_UNSET);
   result = update_tls(cmdbuf);
   if (result != VK_SUCCESS)
      return;

   if (!inherits_render_ctx(cmdbuf)) {
      result = get_render_ctx(cmdbuf);
      if (result != VK_SUCCESS)
         return;
   }

   if (gfx_state_dirty(cmdbuf, DESC_STATE) || gfx_state_dirty(cmdbuf, VS) ||
       gfx_state_dirty(cmdbuf, FS)) {
      uint32_t used_set_mask =
         vs_desc_info->used_set_mask | (fs ? fs_desc_info->used_set_mask : 0);
      struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;

      result = panvk_per_arch(cmd_prepare_push_descs)(cmdbuf, desc_state,
                                                      used_set_mask);
      if (result != VK_SUCCESS)
         return;
   }

   result = prepare_fs_desc(cmdbuf);
   if (result != VK_SUCCESS)
      return;

   result = build_blend(cmdbuf, fs, &bds_gpu);
   if (result != VK_SUCCESS)
      return;

   panvk_per_arch(cmd_prepare_draw_sysvals)(cmdbuf, &draw, fs);

   result = prepare_push_uniforms(cmdbuf, &draw, vs, fs);
   if (result != VK_SUCCESS)
      return;

   prepare_fs(cmdbuf, fs);

   build_dcd_flags(
      cmdbuf, fs,
      vk_topology_to_mesa(
         cmdbuf->vk.dynamic_graphics_state.ia.primitive_topology),
      &dcd_flags);

   if (fixed_func_depth_write) {
      rs.depth_clamp_enable = true;
      rs.depth_clip_enable = VK_MESA_DEPTH_CLIP_ENABLE_FALSE;
      rs.depth_bias.enable = rects[0].z != 0.0f;
      rs.depth_bias.constant_factor = INFINITY;
      rs.depth_bias.slope_factor = 0.0f;
      rs.depth_bias.clamp = rects[0].z;
   }

   result = build_zsd(cmdbuf, dcd_flags.earlyzs, &rs, &zsd_gpu);
   if (result != VK_SUCCESS)
      return;

   for (uint32_t i = 0; i < rect_count; i++) {
      struct vk_meta_rect rect = rects[i];
      if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0)
         continue;

      struct pan_ptr dcd = panvk_cmd_alloc_desc(cmdbuf, DRAW);
      if (!dcd.gpu)
         return;

      if (fixed_func_depth_write && z != rect.z) {
         z = rect.z;
         rs.depth_bias.enable = rect.z != 0.0f;
         rs.depth_bias.clamp = rect.z;
         result = build_zsd(cmdbuf, dcd_flags.earlyzs, &rs, &zsd_gpu);
         if (result != VK_SUCCESS)
            return;
      }

      pan_cast_and_pack(dcd.cpu, DRAW, cfg) {
         cfg.blend = bds_gpu;
         cfg.blend_count = cmdbuf->state.gfx.render.fb.layout.rt_count;
         cfg.depth_stencil = zsd_gpu;
#if PAN_ARCH >= 12
         if (fs) {
            cfg.fragment_resources = cmdbuf->state.gfx.fs.desc.res_table;
            cfg.fragment_shader = panvk_priv_mem_dev_addr(fs->spd);
            cfg.thread_storage = cmdbuf->state.gfx.tsd;
            cfg.fragment_fau.pointer = cmdbuf->state.gfx.fs.push_uniforms;
            cfg.fragment_fau.count = fs->fau.total_count;
         }
#else
         cfg.minimum_z = fixed_func_depth_write ? rect.z : 0.0f;
         cfg.maximum_z = fixed_func_depth_write ? rect.z : 1.0f;
         if (fs) {
            cfg.shader.resources = cmdbuf->state.gfx.fs.desc.res_table;
            cfg.shader.shader = panvk_priv_mem_dev_addr(fs->spd);
            cfg.shader.thread_storage = cmdbuf->state.gfx.tsd;
            cfg.shader.fau = cmdbuf->state.gfx.fs.push_uniforms;
            cfg.shader.fau_count = fs->fau.total_count;
         }
#endif
      };

      struct mali_draw_packed *packed_dcd = dcd.cpu;
      packed_dcd->opaque[0] = dcd_flags.flags_0.opaque[0];
      packed_dcd->opaque[1] = dcd_flags.flags_1.opaque[0];
      packed_dcd->opaque[5] = dcd_flags.flags_2.opaque[0];

      panvk_cond_render(cmdbuf, b) {
         emit_scissor_box(cmdbuf, rect);
         cmd_run_fullscreen(cmdbuf, dcd.gpu, false, rect.layer, layer_count);
      }
   }
}

void
panvk_per_arch(cmd_draw_rects)(struct vk_command_buffer *cmd,
                               struct vk_meta_device *meta, uint32_t rect_count,
                               const struct vk_meta_rect *rects)
{
   if (rect_count == 0)
      return;
   panvk_per_arch(cmd_draw_fullscreen)(cmd, meta, rect_count, rects, 1);
}

void
panvk_per_arch(cmd_draw_volume)(struct vk_command_buffer *cmd,
                                struct vk_meta_device *meta,
                                const struct vk_meta_rect *rect,
                                uint32_t layer_count)
{
   if (layer_count == 0)
      return;
   panvk_per_arch(cmd_draw_fullscreen)(cmd, meta, 1, rect, layer_count);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBindTransformFeedbackBuffersEXT)(
   VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
   const VkBuffer *pBuffers, const VkDeviceSize *pOffsets,
   const VkDeviceSize *pSizes)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   for (uint32_t i = 0; i < bindingCount; i++) {
      VK_FROM_HANDLE(panvk_buffer, buffer, pBuffers[i]);
      uint64_t size = vk_buffer_range(&buffer->vk, pOffsets[i],
                                      pSizes ? pSizes[i] : VK_WHOLE_SIZE);

      cmdbuf->state.gfx.xfb.buffers[firstBinding + i].addr =
         panvk_buffer_gpu_ptr(buffer, pOffsets[i]);
      cmdbuf->state.gfx.xfb.buffers[firstBinding + i].size =
         MIN2(size, UINT32_MAX);
   }
}

static uint64_t
xfb_counter_addr(uint32_t i, uint32_t first, uint32_t count,
                 const VkBuffer *buffers, const VkDeviceSize *offsets)
{
   if (!buffers || i < first || i - first >= count)
      return 0;

   VK_FROM_HANDLE(panvk_buffer, buffer, buffers[i - first]);
   return buffer ? panvk_buffer_gpu_ptr(buffer,
                                        offsets ? offsets[i - first] : 0)
                 : 0;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginTransformFeedbackEXT)(
   VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer,
   uint32_t counterBufferCount, const VkBuffer *pCounterBuffers,
   const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct pan_ptr offsets = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, PANLIB_XFB_MAX_BUFFERS * sizeof(uint32_t), 16);

   if (!offsets.gpu)
      return;

   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);
   struct cs_index addr = cs_scratch_reg64(b, 0);
   struct cs_index vals = cs_scratch_reg_tuple(b, 2, PANLIB_XFB_MAX_BUFFERS);

   for (uint32_t i = 0; i < PANLIB_XFB_MAX_BUFFERS; i++) {
      uint64_t counter =
         xfb_counter_addr(i, firstCounterBuffer, counterBufferCount,
                          pCounterBuffers, pCounterBufferOffsets);
      struct cs_index val = cs_scratch_reg32(b, 2 + i);

      if (counter) {
         cs_move64_to(b, addr, counter);
         cs_load32_to(b, val, addr, 0);
         cs_wait_slot(b, SB_ID(LS));
      } else {
         cs_move32_to(b, val, 0);
      }
   }

   cs_move64_to(b, addr, offsets.gpu);
   cs_store(b, vals, addr, BITFIELD_MASK(PANLIB_XFB_MAX_BUFFERS), 0);
   cs_flush_stores(b);

   cmdbuf->state.gfx.xfb.offsets = offsets.gpu;
   cmdbuf->state.gfx.xfb.active = true;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndTransformFeedbackEXT)(
   VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer,
   uint32_t counterBufferCount, const VkBuffer *pCounterBuffers,
   const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const uint64_t offsets = cmdbuf->state.gfx.xfb.offsets;

   cmdbuf->state.gfx.xfb.active = false;
   cmdbuf->state.gfx.xfb.offsets = 0;
   if (!offsets)
      return;

   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);
   struct cs_index addr = cs_scratch_reg64(b, 0);
   struct cs_index vals = cs_scratch_reg_tuple(b, 2, PANLIB_XFB_MAX_BUFFERS);

   cs_wait_slots(b, dev->csf.sb.all_iters_mask);
   cs_move64_to(b, addr, offsets);
   cs_load_to(b, vals, addr, BITFIELD_MASK(PANLIB_XFB_MAX_BUFFERS), 0);
   cs_wait_slot(b, SB_ID(LS));

   for (uint32_t i = 0; i < PANLIB_XFB_MAX_BUFFERS; i++) {
      uint64_t counter =
         xfb_counter_addr(i, firstCounterBuffer, counterBufferCount,
                          pCounterBuffers, pCounterBufferOffsets);

      if (!counter)
         continue;

      cs_move64_to(b, addr, counter);
      cs_store32(b, cs_scratch_reg32(b, 2 + i), addr, 0);
   }
   cs_flush_stores(b);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndirectByteCountEXT)(
   VkCommandBuffer commandBuffer, uint32_t instanceCount,
   uint32_t firstInstance, VkBuffer counterBuffer,
   VkDeviceSize counterBufferOffset, uint32_t counterOffset,
   uint32_t vertexStride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, counter, counterBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   if (!instanceCount)
      return;

   struct pan_ptr draw = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(VkDrawIndirectCommand), 16);
   if (!draw.gpu)
      return;

   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_xfb_byte_count_draw(
      &ctx, panlib_1d(1), PANLIB_BARRIER_CSF_SYNC,
      panvk_buffer_gpu_ptr(counter, counterBufferOffset), counterOffset,
      vertexStride, instanceCount, firstInstance, draw.gpu);
   cs_wait_slots(panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE),
                 dev->csf.sb.all_iters_mask);
   gpu_prerast_wait_compute(cmdbuf);

   struct panvk_draw_info info = {
      .indirect.buffer_dev_addr = draw.gpu,
      .indirect.draw_count = 1,
      .indirect.stride = sizeof(VkDrawIndirectCommand),
      .prim = panvk_get_client_prim(cmdbuf),
   };

   panvk_cmd_draw(cmdbuf, info);
}
