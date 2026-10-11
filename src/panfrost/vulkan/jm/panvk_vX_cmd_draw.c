#include <stdlib.h>
#include <stdio.h>
/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2026 NXP
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "../../lib/pan_trace_gate.h"
#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_draw.h"
#include "panvk_cmd_meta.h"
#include "panvk_cmd_precomp.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_image_view.h"
#include "panvk_instance.h"
#include "panvk_meta.h"
#include "panvk_priv_bo.h"
#include "panvk_shader.h"

#include "draw_helper.h"
#include "libpan/draw_helper_vp.h"
#include "pan_desc.h"
#include "pan_earlyzs.h"
#include "pan_encoder.h"
#include "pan_format.h"
#include "pan_jc.h"
#include "pan_props.h"
#include "pan_shader.h"

#include "vk_format.h"
#include "vk_meta.h"
#include "vk_pipeline_layout.h"

/* PANVK_NO_SMC=1 disables shader_modifies_coverage (debug A/B) */
#include <stdio.h>
#include <stdlib.h>
static __attribute__((unused)) bool
v61a_no_smc(void)
{
   static int v = -1;
   if (v < 0) {
      const char *e = getenv("PANVK_NO_SMC");
      v = (e && atoi(e)) ? 1 : 0;
      (void)0;
   }
   return v;
}

#if PAN_ARCH != 9
/* TODO(v9): draw-call path (RSD, attribute buffers, indirect varying
 * bufs) not yet implemented for arch 9 -- needs a from-scratch MVS/SPD
 * rewrite, see pan_jm.c PAN_ARCH==9 sections for reference pattern. */


struct panvk_draw_data {
   struct panvk_draw_info info;
   unsigned vertex_range;
   unsigned padded_vertex_count;
   struct mali_invocation_packed invocation;
   struct {
      uint64_t varyings;
      uint64_t attributes;
      uint64_t attribute_bufs;
   } vs;
   struct {
      uint64_t rsd;
      uint64_t varyings;
   } fs;
   uint64_t varying_bufs;
   uint64_t position;
   union {
      uint64_t psiz;
      float line_width;
   };
   uint64_t tls;
   uint64_t fb;
   const struct pan_tiler_context *tiler_ctx;
   uint64_t viewport;
   struct {
      struct pan_ptr vertex_copy_desc;
      struct pan_ptr frag_copy_desc;
      union {
         struct {
            struct pan_ptr vertex;
            struct pan_ptr tiler;
         };
         struct pan_ptr idvs;
      };
   } jobs;
   struct {
      uint64_t attribs;
      uint64_t attrib_bufs;
   } indirect_info;
};

static bool
is_indirect_draw(const struct panvk_draw_data *draw)
{
   return draw->info.indirect.buffer_dev_addr != 0 ||
          draw->info.index.index_size != 0;
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

static VkResult
panvk_draw_prepare_fs_rsd(struct panvk_cmd_buffer *cmdbuf,
                          struct panvk_draw_data *draw)
{
   bool dirty = dyn_gfx_state_dirty(cmdbuf, RS_RASTERIZER_DISCARD_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLIP_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_BIAS_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_BIAS_FACTORS) ||
                dyn_gfx_state_dirty(cmdbuf, RS_LINE_MODE) ||
                /* line mode needs primitive topology */
                dyn_gfx_state_dirty(cmdbuf, IA_PRIMITIVE_TOPOLOGY) ||
                dyn_gfx_state_dirty(cmdbuf, CB_LOGIC_OP_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, CB_LOGIC_OP) ||
                dyn_gfx_state_dirty(cmdbuf, CB_ATTACHMENT_COUNT) ||
                dyn_gfx_state_dirty(cmdbuf, CB_COLOR_WRITE_ENABLES) ||
                dyn_gfx_state_dirty(cmdbuf, CB_BLEND_ENABLES) ||
                dyn_gfx_state_dirty(cmdbuf, CB_BLEND_EQUATIONS) ||
                dyn_gfx_state_dirty(cmdbuf, CB_WRITE_MASKS) ||
                dyn_gfx_state_dirty(cmdbuf, CB_BLEND_CONSTANTS) ||
                dyn_gfx_state_dirty(cmdbuf, COLOR_ATTACHMENT_MAP) ||
                dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_TEST_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_WRITE_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_COMPARE_OP) ||
                dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_COMPARE_OP) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_TEST_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_OP) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_COMPARE_MASK) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_WRITE_MASK) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_REFERENCE) ||
                dyn_gfx_state_dirty(cmdbuf, MS_RASTERIZATION_SAMPLES) ||
                dyn_gfx_state_dirty(cmdbuf, MS_SAMPLE_MASK) ||
                dyn_gfx_state_dirty(cmdbuf, MS_ALPHA_TO_COVERAGE_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, MS_ALPHA_TO_ONE_ENABLE) ||
                gfx_state_dirty(cmdbuf, FS) || gfx_state_dirty(cmdbuf, OQ) ||
                gfx_state_dirty(cmdbuf, RENDER_STATE);

   if (!dirty) {
      draw->fs.rsd = cmdbuf->state.gfx.fs.rsd;
      return VK_SUCCESS;
   }

   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_rasterization_state *rs = &dyns->rs;
   const struct vk_depth_stencil_state *ds = &dyns->ds;
   const struct vk_input_assembly_state *ia = &dyns->ia;
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   const struct pan_shader_info *fs_info = fs ? &fs->info : NULL;
   uint32_t bd_count = cmdbuf->state.gfx.render.fb.layout.rt_count;
   bool test_s = has_stencil_att(cmdbuf) && ds->stencil.test_enable;
   bool test_z = has_depth_att(cmdbuf) && ds->depth.test_enable;
   bool writes_z = writes_depth(cmdbuf);
   bool writes_s = writes_stencil(cmdbuf);

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

   struct pan_ptr ptr = panvk_cmd_alloc_desc_aggregate(
      cmdbuf, PAN_DESC(RENDERER_STATE), PAN_DESC_ARRAY(bd_count, BLEND));
   if (!ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct mali_renderer_state_packed *rsd = ptr.cpu;
   struct mali_blend_packed *bds = ptr.cpu + pan_size(RENDERER_STATE);
   struct panvk_blend_info *binfo = &cmdbuf->state.gfx.cb.info;

   uint64_t fs_code = panvk_shader_variant_get_dev_addr(fs);

   if (fs_info != NULL) {
      panvk_per_arch(blend_emit_descs)(cmdbuf, bds);
   } else {
      for (unsigned i = 0; i < bd_count; i++) {
         pan_pack(&bds[i], BLEND, cfg) {
            cfg.enable = false;
            cfg.internal.mode = MALI_BLEND_MODE_OFF;
         }
      }
   }

   pan_pack(rsd, RENDERER_STATE, cfg) {
      bool alpha_to_coverage = dyns->ms.alpha_to_coverage_enable;

      if (fs) {
         pan_shader_prepare_rsd(fs_info, fs_code, &cfg);

         uint8_t rt_mask = cmdbuf->state.gfx.render.bound_attachments &
                           MESA_VK_RP_ATTACHMENT_ANY_COLOR_BITS;
         uint8_t rt_written = color_attachment_written_mask(
            fs, &cmdbuf->vk.dynamic_graphics_state.cal);
         uint8_t rt_read = color_attachment_read_mask(fs, &dyns->ial, rt_mask);
         enum pan_earlyzs_zs_tilebuf_read zs_read =
            (z_attachment_read(fs, &dyns->ial) ||
             s_attachment_read(fs, &dyns->ial))
               ? PAN_EARLYZS_ZS_TILEBUF_READ_NO_OPT
               : PAN_EARLYZS_ZS_TILEBUF_NOT_READ;

         cfg.properties.allow_forward_pixel_to_kill =
            fs_info->fs.can_fpk && !(rt_mask & ~rt_written) &&
            !(rt_read & rt_written) && !alpha_to_coverage &&
            !binfo->any_dest_read;

         bool writes_zs = writes_z || writes_s;
         bool zs_always_passes = ds_test_always_passes(cmdbuf);
         bool oq = cmdbuf->state.gfx.occlusion_query.mode !=
                   MALI_OCCLUSION_MODE_DISABLED;

         struct pan_earlyzs_state earlyzs =
            pan_earlyzs_get(fs->fs.earlyzs_lut, writes_zs || oq,
                            alpha_to_coverage, zs_always_passes, zs_read);

         /* early ZS check for FPK is performed by HW on v7+ */
         cfg.properties.allow_forward_pixel_to_be_killed =
            !fs->info.writes_global &&
            ((PAN_ARCH > 6) || earlyzs.kill != MALI_PIXEL_KILL_FORCE_LATE);

         cfg.properties.pixel_kill_operation = earlyzs.kill;
         cfg.properties.zs_update_operation = earlyzs.update;
         cfg.multisample_misc.evaluate_per_sample =
            (fs->info.fs.sample_shading && dyns->ms.rasterization_samples > 1);
      } else {
         cfg.properties.depth_source = MALI_DEPTH_SOURCE_FIXED_FUNCTION;
         cfg.properties.allow_forward_pixel_to_kill = true;
         cfg.properties.allow_forward_pixel_to_be_killed = true;
         cfg.properties.zs_update_operation = MALI_PIXEL_KILL_FORCE_EARLY;
      }

      cfg.multisample_misc.multisample_enable = msaa;
      cfg.multisample_misc.sample_mask = dyns->ms.sample_mask;

      cfg.multisample_misc.depth_function =
         test_z ? translate_compare_func(ds->depth.compare_op)
                : MALI_FUNC_ALWAYS;

      cfg.multisample_misc.depth_write_mask = writes_z;
      cfg.multisample_misc.fixed_function_near_discard =
      cfg.multisample_misc.fixed_function_far_discard =
         vk_rasterization_state_depth_clip_enable(rs);
      cfg.multisample_misc.fixed_function_depth_range_fixed =
         !rs->depth_clamp_enable;
      cfg.multisample_misc.shader_depth_range_fixed = true;

      cfg.stencil_mask_misc.stencil_enable = test_s;
      cfg.stencil_mask_misc.alpha_to_coverage = alpha_to_coverage;
      cfg.stencil_mask_misc.alpha_test_compare_function = MALI_FUNC_ALWAYS;
      cfg.stencil_mask_misc.front_facing_depth_bias = rs->depth_bias.enable;
      cfg.stencil_mask_misc.back_facing_depth_bias = rs->depth_bias.enable;

      if (rs->line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM)
         cfg.stencil_mask_misc.aligned_line_ends = true;

      cfg.depth_units = rs->depth_bias.constant_factor;
      cfg.depth_factor = rs->depth_bias.slope_factor;
      cfg.depth_bias_clamp = rs->depth_bias.clamp;

      cfg.stencil_front.mask = ds->stencil.front.compare_mask;
      cfg.stencil_back.mask = ds->stencil.back.compare_mask;

      cfg.stencil_mask_misc.stencil_mask_front = ds->stencil.front.write_mask;
      cfg.stencil_mask_misc.stencil_mask_back = ds->stencil.back.write_mask;

      cfg.stencil_front.reference_value = ds->stencil.front.reference;
      cfg.stencil_back.reference_value = ds->stencil.back.reference;

      if (test_s) {
         cfg.stencil_front.compare_function =
            translate_compare_func(ds->stencil.front.op.compare);
         cfg.stencil_front.stencil_fail =
            translate_stencil_op(ds->stencil.front.op.fail);
         cfg.stencil_front.depth_fail =
            translate_stencil_op(ds->stencil.front.op.depth_fail);
         cfg.stencil_front.depth_pass =
            translate_stencil_op(ds->stencil.front.op.pass);
         cfg.stencil_back.compare_function =
            translate_compare_func(ds->stencil.back.op.compare);
         cfg.stencil_back.stencil_fail =
            translate_stencil_op(ds->stencil.back.op.fail);
         cfg.stencil_back.depth_fail =
            translate_stencil_op(ds->stencil.back.op.depth_fail);
         cfg.stencil_back.depth_pass =
            translate_stencil_op(ds->stencil.back.op.pass);
      }
   }

   cmdbuf->state.gfx.fs.rsd = ptr.gpu;
   draw->fs.rsd = cmdbuf->state.gfx.fs.rsd;
   return VK_SUCCESS;
}

static VkResult
panvk_draw_prepare_tiler_context(struct panvk_cmd_buffer *cmdbuf,
                                 struct panvk_draw_data *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   VkResult result =
      panvk_per_arch(cmd_prepare_tiler_context)(cmdbuf, draw->info.layer_id);
   if (result != VK_SUCCESS)
      return result;

   draw->tiler_ctx = &batch->tiler.ctx;
   return VK_SUCCESS;
}

static VkResult
panvk_draw_prepare_varyings(struct panvk_cmd_buffer *cmdbuf,
                            struct panvk_draw_data *draw)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_link *link = &cmdbuf->state.gfx.link;
   struct pan_ptr bufs = panvk_cmd_alloc_desc_array(
      cmdbuf, PANVK_VARY_BUF_MAX + 1, ATTRIBUTE_BUFFER);
   if (!bufs.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct mali_attribute_buffer_packed *buf_descs = bufs.cpu;
   const struct vk_input_assembly_state *ia =
      &cmdbuf->vk.dynamic_graphics_state.ia;
   bool writes_point_size =
      vs->info.vs.writes_point_size &&
      ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
   uint64_t psiz_buf = 0;

   for (unsigned i = 0; i < PANVK_VARY_BUF_MAX; i++) {
      uint32_t buf_size;
      uint64_t buf_addr;
      if (is_indirect_draw(draw)) {
         buf_addr = dev->poly_heap.buffer->addr.dev;
         buf_size = 0;
      } else {
         buf_size = draw->padded_vertex_count * draw->info.instance.count *
                    link->buf_strides[i];
         buf_addr =
            buf_size
               ? panvk_cmd_alloc_dev_mem(cmdbuf, varying, buf_size, 64).gpu
               : 0;

         if (buf_size && !buf_addr)
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      }

      pan_pack(&buf_descs[i], ATTRIBUTE_BUFFER, cfg) {
         cfg.stride = link->buf_strides[i];
         cfg.size = buf_size;
         cfg.pointer = buf_addr;
      }

      if (i == PANVK_VARY_BUF_POSITION)
         draw->position = buf_addr;

      if (i == PANVK_VARY_BUF_PSIZ)
         psiz_buf = buf_addr;
   }

   /* We need an empty entry to stop prefetching on Bifrost */
   memset(bufs.cpu + (pan_size(ATTRIBUTE_BUFFER) * PANVK_VARY_BUF_MAX), 0,
          pan_size(ATTRIBUTE_BUFFER));

   if (writes_point_size)
      draw->psiz = psiz_buf;
   else if (ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST ||
            ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP)
      draw->line_width = cmdbuf->vk.dynamic_graphics_state.rs.line.width;
   else
      draw->line_width = 1.0f;

   draw->varying_bufs = bufs.gpu;
   draw->vs.varyings = panvk_priv_mem_dev_addr(link->vs.attribs);
   draw->fs.varyings = panvk_priv_mem_dev_addr(link->fs.attribs);
   return VK_SUCCESS;
}

static void
panvk_draw_emit_attrib_buf(
   const struct panvk_draw_data *draw,
   const struct vk_vertex_binding_state *buf_info, uint32_t stride,
   const struct panvk_attrib_buf *buf,
   struct mali_attribute_buffer_packed *desc,
   struct libpan_draw_helper_attrib_buf_info *helper_buf_info)
{
   uint64_t addr = buf->address & ~63ULL;
   unsigned size = buf->size + (buf->address & 63);
   unsigned divisor = draw->padded_vertex_count * buf_info->divisor;
   bool per_instance = buf_info->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE;
   struct mali_attribute_buffer_packed *buf_ext = &desc[1];

   /* In case of indirect draw, the descriptor will be patched at runtime */
   if (helper_buf_info != NULL) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.pointer = addr;
         cfg.size = size;
      }

      helper_buf_info->divisor = buf_info->divisor;
      helper_buf_info->stride = stride;
      helper_buf_info->per_instance = per_instance;
   } else if (draw->info.instance.count <= 1) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.stride = per_instance ? 0 : stride;
         cfg.pointer = addr;
         cfg.size = size;
      }
   } else if (!per_instance) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_MODULUS;
         cfg.divisor = draw->padded_vertex_count;
         cfg.stride = stride;
         cfg.pointer = addr;
         cfg.size = size;
      }
   } else if (!divisor) {
      /* instance_divisor == 0 means all instances share the same value.
       * Make it a 1D array with a zero stride.
       */
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.stride = 0;
         cfg.pointer = addr;
         cfg.size = size;
      }
   } else if (util_is_power_of_two_or_zero(divisor)) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_POT_DIVISOR;
         cfg.stride = stride;
         cfg.pointer = addr;
         cfg.size = size;
         cfg.divisor_r = __builtin_ctz(divisor);
      }
   } else {
      unsigned divisor_r = 0, divisor_e = 0;
      unsigned divisor_d =
         pan_compute_npot_divisor(divisor, &divisor_r, &divisor_e);
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR;
         cfg.stride = stride;
         cfg.pointer = addr;
         cfg.size = size;
         cfg.divisor_r = divisor_r;
         cfg.divisor_e = divisor_e;
      }

      pan_cast_and_pack(buf_ext, ATTRIBUTE_BUFFER_CONTINUATION_NPOT, cfg) {
         cfg.divisor_numerator = divisor_d;
         cfg.divisor = buf_info->divisor;
      }

      buf_ext = NULL;
   }

   /* If the buffer extension wasn't used, memset(0) */
   if (buf_ext)
      memset(buf_ext, 0, pan_size(ATTRIBUTE_BUFFER));
}

static void
panvk_draw_emit_attrib(const struct panvk_draw_data *draw,
                       const struct vk_vertex_attribute_state *attrib_info,
                       const struct vk_vertex_binding_state *buf_info,
                       const struct panvk_attrib_buf *buf,
                       struct mali_attribute_packed *desc,
                       struct libpan_draw_helper_attrib_info *helper_attrib_info)
{
   bool per_instance = buf_info->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE;
   enum pipe_format f = vk_format_to_pipe_format(attrib_info->format);
   unsigned buf_idx = attrib_info->binding;

   pan_pack(desc, ATTRIBUTE, cfg) {
      cfg.buffer_index = buf_idx * 2;
      cfg.offset_enable = true;
      cfg.format = GENX(pan_format_from_pipe_format)(f)->hw;

      uint32_t offset = attrib_info->offset + (buf->address & 63);

      /* In case of indirect draw, the descriptor will be patched at runtime */
      if (helper_attrib_info != NULL) {
         helper_attrib_info->base_offset = offset;
         helper_attrib_info->stride = per_instance ? buf_info->stride : 0;
      } else {
         cfg.offset = offset;
         if (per_instance)
            cfg.offset += draw->info.instance.base * buf_info->stride;
      }
   }
}

/* Don't re-use vertex attributes, always re-emit them.  Descriptors depend on
 * per-draw parameters (e.g. vertex_count, instancing), which are likely to
 * change every draw, so don't bother trying to save an attribute[_buffer]
 * re-emission.
 */
static VkResult
panvk_draw_prepare_vs_attribs(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_data *draw)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader_variant *vs_variant =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_vertex_input_state *vi = dyns->vi;
   unsigned num_imgs = vs->desc_info.others.count[PANVK_BIFROST_DESC_TABLE_IMG];
   unsigned num_vbs = util_last_bit(vi->bindings_valid);
   unsigned num_vs_attribs =
      num_imgs ? MAX_VS_ATTRIBS
               : MAX2(util_last_bit(vi->attributes_valid),
                      vs_variant->info.attribute_count);
   unsigned attrib_count = num_vs_attribs + num_imgs;

   unsigned attrib_buf_count = (num_vbs + num_imgs) * 2;
   struct pan_ptr bufs = panvk_cmd_alloc_desc_array(
      cmdbuf, attrib_buf_count + 1, ATTRIBUTE_BUFFER);
   struct mali_attribute_buffer_packed *attrib_buf_descs = bufs.cpu;
   struct pan_ptr attribs =
      panvk_cmd_alloc_desc_array(cmdbuf, attrib_count, ATTRIBUTE);
   struct mali_attribute_packed *attrib_descs = attribs.cpu;

   if (!bufs.gpu || (attrib_count && !attribs.gpu))
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct libpan_draw_helper_attrib_buf_info *bufs_infos = NULL;
   struct libpan_draw_helper_attrib_info *attribs_infos = NULL;

   if (is_indirect_draw(draw)) {
      struct pan_ptr bufs_infos_storage = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc,
         num_vbs * sizeof(struct libpan_draw_helper_attrib_buf_info), 8);
      struct pan_ptr attribs_infos_storage = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc,
         num_vs_attribs * sizeof(struct libpan_draw_helper_attrib_info), 8);

      if (!bufs_infos_storage.gpu ||
          (num_vs_attribs && !attribs_infos_storage.gpu))
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      cmdbuf->state.gfx.vs.indirect_attrib_bufs_infos = bufs_infos_storage.gpu;
      cmdbuf->state.gfx.vs.indirect_attribs_infos = attribs_infos_storage.gpu;
      bufs_infos = bufs_infos_storage.cpu;
      attribs_infos = attribs_infos_storage.cpu;
   }

   for (unsigned i = 0; i < num_vbs; i++) {
      if (vi->bindings_valid & BITFIELD_BIT(i)) {
         struct libpan_draw_helper_attrib_buf_info *helper_buf_info =
            bufs_infos ? &bufs_infos[i] : NULL;
         panvk_draw_emit_attrib_buf(draw, &vi->bindings[i],
                                    dyns->vi_binding_strides[i],
                                    &cmdbuf->state.gfx.vb.bufs[i],
                                    &attrib_buf_descs[i * 2], helper_buf_info);
      } else {
         memset(&attrib_buf_descs[i * 2], 0, sizeof(*attrib_buf_descs) * 2);
      }
   }

   for (unsigned i = 0; i < num_vs_attribs; i++) {
      if (vi->attributes_valid & BITFIELD_BIT(i)) {
         unsigned buf_idx = vi->attributes[i].binding;
         struct libpan_draw_helper_attrib_info *helper_attrib_info =
            attribs_infos ? &attribs_infos[i] : NULL;
         panvk_draw_emit_attrib(draw, &vi->attributes[i],
                                &vi->bindings[buf_idx],
                                &cmdbuf->state.gfx.vb.bufs[buf_idx],
                                &attrib_descs[i], helper_attrib_info);
      } else {
         /* Unbound attributes read the zero default */
         pan_pack(&attrib_descs[i], ATTRIBUTE, cfg)
            cfg.format = MALI_PACK_FMT(CONSTANT, 0000, L);
      }
   }

   /* A NULL entry is needed to stop prefecting on Bifrost */
   memset(bufs.cpu + (pan_size(ATTRIBUTE_BUFFER) * attrib_buf_count), 0,
          pan_size(ATTRIBUTE_BUFFER));

   cmdbuf->state.gfx.vs.attrib_bufs = bufs.gpu;
   cmdbuf->state.gfx.vs.attribs = attribs.gpu;

   if (num_imgs) {
      cmdbuf->state.gfx.vs.desc.img_attrib_table =
         attribs.gpu + (MAX_VS_ATTRIBS * pan_size(ATTRIBUTE));
      cmdbuf->state.gfx.vs.desc.tables[PANVK_BIFROST_DESC_TABLE_IMG] =
         bufs.gpu + (num_vbs * pan_size(ATTRIBUTE_BUFFER) * 2);
   }

   return VK_SUCCESS;
}

static void
panvk_draw_prepare_attributes(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_data *draw)
{
   panvk_draw_prepare_vs_attribs(cmdbuf, draw);
   draw->vs.attributes = cmdbuf->state.gfx.vs.attribs;
   draw->vs.attribute_bufs = cmdbuf->state.gfx.vs.attrib_bufs;
   draw->indirect_info.attribs = cmdbuf->state.gfx.vs.indirect_attribs_infos;
   draw->indirect_info.attrib_bufs =
      cmdbuf->state.gfx.vs.indirect_attrib_bufs_infos;
}

static void
panvk_emit_viewport(struct panvk_cmd_buffer *cmdbuf,
                    struct mali_viewport_packed *vpd)
{
   const struct vk_viewport_state *vp = &cmdbuf->vk.dynamic_graphics_state.vp;

   if (vp->viewport_count < 1)
      return;

   const VkViewport *viewport = &vp->viewports[0];
   const VkRect2D *scissor = &vp->scissors[0];
   float minz, maxz;
   panvk_depth_range(&cmdbuf->state.gfx, &cmdbuf->vk.dynamic_graphics_state.vp,
                     &minz, &maxz);

   /* The spec says "width must be greater than 0.0" */
   assert(viewport->width >= 0);
   int minx = (int)viewport->x;
   int maxx = (int)(viewport->x + viewport->width);

   /* Viewport height can be negative */
   int miny = MIN2((int)viewport->y, (int)(viewport->y + viewport->height));
   int maxy = MAX2((int)viewport->y, (int)(viewport->y + viewport->height));

   assert(scissor->offset.x >= 0 && scissor->offset.y >= 0);
   minx = MAX2(scissor->offset.x, minx);
   miny = MAX2(scissor->offset.y, miny);
   maxx = MIN2(scissor->offset.x + scissor->extent.width, maxx);
   maxy = MIN2(scissor->offset.y + scissor->extent.height, maxy);

   /* Make sure we don't end up with a max < min when width/height is 0 */
   maxx = maxx > minx ? maxx - 1 : maxx;
   maxy = maxy > miny ? maxy - 1 : maxy;

   /* Clamp viewport scissor to valid range */
   minx = CLAMP(minx, 0, UINT16_MAX);
   maxx = CLAMP(maxx, 0, UINT16_MAX);
   miny = CLAMP(miny, 0, UINT16_MAX);
   maxy = CLAMP(maxy, 0, UINT16_MAX);

   pan_pack(vpd, VIEWPORT, cfg) {
      cfg.scissor_minimum_x = minx;
      cfg.scissor_minimum_y = miny;
      cfg.scissor_maximum_x = maxx;
      cfg.scissor_maximum_y = maxy;
      cfg.minimum_z = minz;
      cfg.maximum_z = maxz;
   }
}

static VkResult
panvk_draw_prepare_viewport(struct panvk_cmd_buffer *cmdbuf,
                            struct panvk_draw_data *draw)
{
   /* When rasterizerDiscardEnable is active, it is allowed to have viewport and
    * scissor disabled.
    * As a result, we define an empty one.
    */
   if (!cmdbuf->state.gfx.vpd || dyn_gfx_state_dirty(cmdbuf, VP_VIEWPORTS) ||
       dyn_gfx_state_dirty(cmdbuf, VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE) ||
       dyn_gfx_state_dirty(cmdbuf, VP_SCISSORS) ||
       dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLIP_ENABLE) ||
       dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE) ||
       dyn_gfx_state_dirty(cmdbuf, VP_DEPTH_CLAMP_RANGE)) {
      struct pan_ptr vp = panvk_cmd_alloc_desc(cmdbuf, VIEWPORT);
      if (!vp.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      panvk_emit_viewport(cmdbuf, vp.cpu);
      cmdbuf->state.gfx.vpd = vp.gpu;
   }

   draw->viewport = cmdbuf->state.gfx.vpd;
   return VK_SUCCESS;
}

static void
panvk_emit_vertex_dcd(struct panvk_cmd_buffer *cmdbuf,
                      const struct panvk_draw_data *draw,
                      struct mali_draw_packed *dcd)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_desc_state *vs_desc_state =
      &cmdbuf->state.gfx.vs.desc;

   pan_pack(dcd, DRAW, cfg) {
      cfg.state = panvk_priv_mem_dev_addr(vs->rsd);
      cfg.attributes = draw->vs.attributes;
      cfg.attribute_buffers = draw->vs.attribute_bufs;
      cfg.varyings = draw->vs.varyings;
      cfg.varying_buffers = draw->varying_bufs;
      cfg.thread_storage = draw->tls;

      /* In case of indirect draw, the descriptor will be patched at runtime */
      if (!is_indirect_draw(draw)) {
         cfg.offset_start = draw->info.vertex.raw_offset;
         cfg.instance_size =
            draw->info.instance.count > 1 ? draw->padded_vertex_count : 1;
      }

      cfg.uniform_buffers = vs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_UBO];
      cfg.push_uniforms = cmdbuf->state.gfx.vs.push_uniforms;
      cfg.textures = vs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_TEXTURE];
      cfg.samplers = vs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_SAMPLER];
   }
}

static VkResult
panvk_draw_prepare_vertex_job(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_data *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct pan_ptr ptr = panvk_cmd_alloc_desc(cmdbuf, COMPUTE_JOB);
   if (!ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   util_dynarray_append(&batch->jobs, ptr.cpu);
   draw->jobs.vertex = ptr;

   memcpy(pan_section_ptr(ptr.cpu, COMPUTE_JOB, INVOCATION), &draw->invocation,
          pan_size(INVOCATION));

   pan_section_pack(ptr.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
      cfg.job_task_split = 5;
   }

   panvk_emit_vertex_dcd(cmdbuf, draw,
                         pan_section_ptr(ptr.cpu, COMPUTE_JOB, DRAW));
   return VK_SUCCESS;
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
#if PAN_ARCH >= 9
   case MESA_PRIM_LINES_ADJACENCY:
      return MALI_DRAW_MODE_LINES_ADJACENCY;
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      return MALI_DRAW_MODE_LINE_STRIP_ADJACENCY;
   case MESA_PRIM_TRIANGLES_ADJACENCY:
      return MALI_DRAW_MODE_TRIANGLES_ADJACENCY;
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return MALI_DRAW_MODE_TRIANGLE_STRIP_ADJACENCY;
#endif
   default:
      UNREACHABLE("Invalid primitive type");
   }
}

static void
panvk_emit_tiler_primitive(struct panvk_cmd_buffer *cmdbuf,
                           const struct panvk_draw_data *draw,
                           struct mali_primitive_packed *prim)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_input_assembly_state *ia = &dyns->ia;
   const struct vk_rasterization_state *rs = &dyns->rs;
   bool writes_point_size =
      vs->info.vs.writes_point_size &&
      ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
   bool secondary_shader = vs->info.vs.secondary_enable && fs != NULL;
   assert(!(vs->info.outputs_written & VARYING_BIT_PRIMITIVE_ID));
   bool fs_reads_primitive_id = fs ? fs->info.fs.reads_primitive_id : false;

   pan_pack(prim, PRIMITIVE, cfg) {
      cfg.draw_mode = translate_prim(draw->info.prim);
      if (writes_point_size)
         cfg.point_size_array_format = MALI_POINT_SIZE_ARRAY_FORMAT_FP16;
      cfg.primitive_index_enable = fs_reads_primitive_id;
      cfg.primitive_index_writeback = fs_reads_primitive_id;

      cfg.first_provoking_vertex =
         cmdbuf->state.gfx.render.first_provoking_vertex != U_TRISTATE_NO;

      if (draw->info.index.restart_enable)
         cfg.primitive_restart = MALI_PRIMITIVE_RESTART_IMPLICIT;
      cfg.job_task_split = 6;

      if (draw->info.index.index_size) {
         switch (draw->info.index.index_size) {
         case 4:
            cfg.index_type = MALI_INDEX_TYPE_UINT32;
            break;
         case 2:
            cfg.index_type = MALI_INDEX_TYPE_UINT16;
            break;
         case 1:
            cfg.index_type = MALI_INDEX_TYPE_UINT8;
            break;
         default:
            UNREACHABLE("Invalid index size");
         }
      }

      /* In case of indirect draw, the descriptor will be patched at runtime */
      cfg.index_count = is_indirect_draw(draw) ? 1 : draw->info.vertex.count;

      cfg.low_depth_cull = cfg.high_depth_cull =
         vk_rasterization_state_depth_clip_enable(rs);

      cfg.secondary_shader = secondary_shader;
   }
}

static void
panvk_emit_tiler_primitive_size(struct panvk_cmd_buffer *cmdbuf,
                                const struct panvk_draw_data *draw,
                                struct mali_primitive_size_packed *primsz)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const bool writes_point_size =
      vs->info.vs.writes_point_size && draw->info.prim == MESA_PRIM_POINTS;

   pan_pack(primsz, PRIMITIVE_SIZE, cfg) {
      if (writes_point_size) {
         cfg.size_array = draw->psiz;
      } else {
         cfg.fixed_sized = draw->line_width;
      }
   }
}

static void
panvk_emit_tiler_dcd(struct panvk_cmd_buffer *cmdbuf,
                     const struct panvk_draw_data *draw,
                     struct mali_draw_packed *dcd)
{
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   const struct vk_rasterization_state *rs =
      &cmdbuf->vk.dynamic_graphics_state.rs;

   enum mesa_prim reduced_prim = u_reduced_prim(draw->info.prim);
   const bool non_polygon = reduced_prim != MESA_PRIM_TRIANGLES;

   pan_pack(dcd, DRAW, cfg) {
      cfg.front_face_ccw = rs->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE;
      cfg.cull_front_face =
         !non_polygon && (rs->cull_mode & VK_CULL_MODE_FRONT_BIT) != 0;
      cfg.cull_back_face =
         !non_polygon && (rs->cull_mode & VK_CULL_MODE_BACK_BIT) != 0;

      cfg.position = draw->position;
      cfg.state = draw->fs.rsd;
      cfg.attributes = fs_desc_state->img_attrib_table;
      cfg.attribute_buffers =
         fs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_IMG];
      cfg.viewport = draw->viewport;
      cfg.varyings = draw->fs.varyings;
      cfg.varying_buffers = cfg.varyings ? draw->varying_bufs : 0;
      cfg.thread_storage = draw->tls;

      /* For all primitives but lines DRAW.flat_shading_vertex must
       * be set to 0 and the provoking vertex is selected with the
       * PRIMITIVE.first_provoking_vertex field.
       */
      if (reduced_prim == MESA_PRIM_LINES)
         cfg.flat_shading_vertex = true;

      /* In case of indirect draw, the descriptor will be patched at runtime */
      if (!is_indirect_draw(draw)) {
         cfg.offset_start = draw->info.vertex.raw_offset;
         cfg.instance_size =
            draw->info.instance.count > 1 ? draw->padded_vertex_count : 1;
         uint32_t primitives_per_instance =
            DIV_ROUND_UP(draw->padded_vertex_count,
                         mesa_vertices_per_prim(draw->info.prim));
         /* instance_primitive_size has the same restrictions as
          * padded_vertex_count, so we can use pan_padded_vertex_count here. */
         cfg.instance_primitive_size =
            pan_padded_vertex_count(primitives_per_instance);
      }

      cfg.uniform_buffers = fs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_UBO];
      cfg.push_uniforms = cmdbuf->state.gfx.fs.push_uniforms;
      cfg.textures = fs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_TEXTURE];
      cfg.samplers = fs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_SAMPLER];

      cfg.occlusion_query = cmdbuf->state.gfx.occlusion_query.mode;
      cfg.occlusion = cmdbuf->state.gfx.occlusion_query.ptr;
#if PAN_ARCH == 9
      cfg.scissor_to_bounding_box = true;
#endif
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

   /* Once we emit the first FBDs/TDs, we need to commit to a state. If we
    * choose the wrong one, we will fail the assert when the next application
    * draw happens (with a different state). Use PROVOKING_VERTEX_MODE_FIRST
    * because it's the vulkan default, and so likely to be right more often.
    *
    * TODO: handle this case better */
   if (state->render.first_provoking_vertex == U_TRISTATE_UNSET)
      state->render.first_provoking_vertex = U_TRISTATE_YES;
}

static VkResult
panvk_draw_prepare_tiler_job(struct panvk_cmd_buffer *cmdbuf,
                             struct panvk_draw_data *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct pan_ptr ptr = panvk_cmd_alloc_desc(cmdbuf, TILER_JOB);
   util_dynarray_append(&batch->jobs, ptr.cpu);
   draw->jobs.tiler = ptr;

   memcpy(pan_section_ptr(ptr.cpu, TILER_JOB, INVOCATION), &draw->invocation,
          pan_size(INVOCATION));

   panvk_emit_tiler_primitive(cmdbuf, draw,
                              pan_section_ptr(ptr.cpu, TILER_JOB, PRIMITIVE));

   panvk_emit_tiler_primitive_size(
      cmdbuf, draw, pan_section_ptr(ptr.cpu, TILER_JOB, PRIMITIVE_SIZE));

   panvk_emit_tiler_dcd(cmdbuf, draw,
                        pan_section_ptr(ptr.cpu, TILER_JOB, DRAW));

   pan_section_pack(ptr.cpu, TILER_JOB, TILER, cfg) {
      cfg.address = PAN_ARCH >= 9 ? draw->tiler_ctx->valhall.desc
                                  : draw->tiler_ctx->bifrost.desc;
   }

   pan_section_pack(ptr.cpu, TILER_JOB, PADDING, padding)
      ;

   return VK_SUCCESS;
}

static VkResult
panvk_draw_prepare_idvs_job(struct panvk_cmd_buffer *cmdbuf,
                            struct panvk_draw_data *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct pan_ptr ptr = panvk_cmd_alloc_desc(cmdbuf, INDEXED_VERTEX_JOB);
   if (!ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   util_dynarray_append(&batch->jobs, ptr.cpu);
   draw->jobs.idvs = ptr;

   memcpy(pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, INVOCATION),
          &draw->invocation, pan_size(INVOCATION));

   panvk_emit_tiler_primitive(
      cmdbuf, draw, pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, PRIMITIVE));

   panvk_emit_tiler_primitive_size(
      cmdbuf, draw,
      pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, PRIMITIVE_SIZE));

   pan_section_pack(ptr.cpu, INDEXED_VERTEX_JOB, TILER, cfg) {
      cfg.address = PAN_ARCH >= 9 ? draw->tiler_ctx->valhall.desc
                                  : draw->tiler_ctx->bifrost.desc;
   }

   pan_section_pack(ptr.cpu, INDEXED_VERTEX_JOB, PADDING, _) {
   }

   panvk_emit_tiler_dcd(
      cmdbuf, draw,
      pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, FRAGMENT_DRAW));

   panvk_emit_vertex_dcd(
      cmdbuf, draw, pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, VERTEX_DRAW));
   return VK_SUCCESS;
}

static VkResult
panvk_draw_prepare_vs_copy_desc_job(struct panvk_cmd_buffer *cmdbuf,
                                    struct panvk_draw_data *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   const struct panvk_shader_desc_info *vs_desc_info =
      &cmdbuf->state.gfx.vs.shader->desc_info;
   const struct panvk_shader_desc_state *vs_desc_state =
      &cmdbuf->state.gfx.vs.desc;
   const struct vk_vertex_input_state *vi =
      cmdbuf->vk.dynamic_graphics_state.vi;
   unsigned num_vbs = util_last_bit(vi->bindings_valid);
   struct pan_ptr ptr;

   VkResult result = panvk_per_arch(meta_get_copy_desc_job)(
      cmdbuf, vs_desc_info, &cmdbuf->state.gfx.desc_state, vs_desc_state,
      num_vbs * pan_size(ATTRIBUTE_BUFFER) * 2, &ptr);
   if (result != VK_SUCCESS)
      return result;

   if (ptr.cpu) {
      util_dynarray_append(&batch->jobs, ptr.cpu);
   }

   draw->jobs.vertex_copy_desc = ptr;
   return VK_SUCCESS;
}

static VkResult
panvk_draw_prepare_fs_copy_desc_job(struct panvk_cmd_buffer *cmdbuf,
                                    struct panvk_draw_data *draw)
{
   const struct panvk_shader_desc_info *fs_desc_info =
      &cmdbuf->state.gfx.fs.shader->desc_info;
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct pan_ptr ptr;

   VkResult result = panvk_per_arch(meta_get_copy_desc_job)(
      cmdbuf, fs_desc_info, &cmdbuf->state.gfx.desc_state,
      fs_desc_state, 0, &ptr);
   if (result != VK_SUCCESS)
      return result;

   if (ptr.cpu) {
      util_dynarray_append(&batch->jobs, ptr.cpu);
   }

   draw->jobs.frag_copy_desc = ptr;
   return VK_SUCCESS;
}

static VkResult
panvk_cmd_prepare_draw_link_shaders(struct panvk_cmd_buffer *cmd)
{
   struct panvk_cmd_graphics_state *gfx = &cmd->state.gfx;

   if (!gfx_state_dirty(cmd, VS) && !gfx_state_dirty(cmd, FS))
      return VK_SUCCESS;

   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmd->state.gfx.vs.shader);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmd));

   VkResult result =
      panvk_per_arch(link_shaders)(&cmd->desc_pool, vs, fs, &gfx->link);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return result;
   }

   return VK_SUCCESS;
}

static VkResult
prepare_draw(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_data *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   struct panvk_shader_desc_state *vs_desc_state = &cmdbuf->state.gfx.vs.desc;
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   const struct vk_rasterization_state *rs =
      &cmdbuf->vk.dynamic_graphics_state.rs;
   VkResult result;
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));

   /* There are only 16 bits in the descriptor for the job ID. Each job has a
    * pilot shader dealing with descriptor copies, and we need one
    * <vertex,tiler> pair per draw.
    */
   if (batch->vtc_jc.job_index + (4 * cmdbuf->state.gfx.render.layer_count) >=
       UINT16_MAX) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      batch = panvk_per_arch(cmd_open_batch)(cmdbuf);
   }

   if (fs_user_dirty(cmdbuf)) {
      result = panvk_cmd_prepare_draw_link_shaders(cmdbuf);
      if (result != VK_SUCCESS)
         return result;
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

   if (!rs->rasterizer_discard_enable) {
      ASSERTED const struct pan_fb_layout *fb =
         &cmdbuf->state.gfx.render.fb.layout;
      uint32_t *nr_samples = &cmdbuf->state.gfx.render.fb.nr_samples;
      uint32_t rasterization_samples =
         cmdbuf->vk.dynamic_graphics_state.ms.rasterization_samples;

      /* If there's no attachment, and the FB descriptor hasn't been allocated
       * yet, we patch nr_samples to match rasterization_samples, otherwise, we
       * make sure those two numbers match. */
      if (!batch->fb.desc.gpu && !cmdbuf->state.gfx.render.bound_attachments) {
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

      result = panvk_per_arch(cmd_alloc_fb_desc)(cmdbuf);
      if (result != VK_SUCCESS)
         return result;
   }

   panvk_per_arch(cmd_select_tile_size)(cmdbuf);

   result = panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf, true);
   if (result != VK_SUCCESS)
      return result;

   const struct panvk_shader_desc_info *vs_desc_info =
      &cmdbuf->state.gfx.vs.shader->desc_info;
   const struct panvk_shader_desc_info *fs_desc_info =
      fs ? &cmdbuf->state.gfx.fs.shader->desc_info : NULL;

   uint32_t used_set_mask =
      vs_desc_info->used_set_mask | (fs ? fs_desc_info->used_set_mask : 0);

   if (gfx_state_dirty(cmdbuf, DESC_STATE) || gfx_state_dirty(cmdbuf, VS) ||
       gfx_state_dirty(cmdbuf, FS)) {
      result = panvk_per_arch(cmd_prepare_push_descs)(cmdbuf, desc_state,
                                                      used_set_mask);
      if (result != VK_SUCCESS)
         return result;
   }

   if (gfx_state_dirty(cmdbuf, DESC_STATE) || gfx_state_dirty(cmdbuf, VS)) {
      result = panvk_per_arch(cmd_prepare_shader_desc_tables)(
         cmdbuf, desc_state, vs_desc_info, false, vs_desc_state);
      if (result != VK_SUCCESS)
         return result;

      result = panvk_per_arch(cmd_prepare_dyn_ssbos)(
         cmdbuf, desc_state, vs_desc_info, vs_desc_state);
      if (result != VK_SUCCESS)
         return result;
   }

   /* This allocates and initializes the image table, which we need before we
    * can copy descriptors.
    */
   panvk_draw_prepare_attributes(cmdbuf, draw);

   if (gfx_state_dirty(cmdbuf, DESC_STATE) || gfx_state_dirty(cmdbuf, VS)) {
      result = panvk_draw_prepare_vs_copy_desc_job(cmdbuf, draw);
      if (result != VK_SUCCESS)
         return result;
   }

   if (gfx_state_dirty(cmdbuf, DESC_STATE) || gfx_state_dirty(cmdbuf, FS)) {
      if (fs == NULL) {
         /* No need to setup the FS desc tables if the FS is not executed. */
         memset(fs_desc_state, 0, sizeof(*fs_desc_state));
      } else {
         result = panvk_per_arch(cmd_prepare_shader_desc_tables)(
            cmdbuf, desc_state, fs_desc_info, true, fs_desc_state);
         if (result != VK_SUCCESS)
            return result;

         result = panvk_per_arch(cmd_prepare_dyn_ssbos)(
            cmdbuf, desc_state, fs_desc_info, fs_desc_state);
         if (result != VK_SUCCESS)
            return result;

         result = panvk_draw_prepare_fs_copy_desc_job(cmdbuf, draw);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   draw->tls = batch->tls.gpu;
   draw->fb = batch->fb.desc.gpu;

   result = panvk_draw_prepare_fs_rsd(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return result;

   batch->tlsinfo.tls.size = MAX3(vs->info.tls_size, fs ? fs->info.tls_size : 0,
                                  batch->tlsinfo.tls.size);

   panvk_per_arch(cmd_prepare_draw_sysvals)(cmdbuf, &draw->info, fs);

   /* Viewport emission requires up-to-date {scale,offset}.z for min/max Z,
    * so we need to call it after calling cmd_prepare_draw_sysvals(), but
    * viewports are the same for all layers, so we only emit when layer_id=0.
    */
   result = panvk_draw_prepare_viewport(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

static VkResult
prepare_draw_layer(struct panvk_cmd_buffer *cmdbuf,
                   struct panvk_draw_data *draw, uint32_t layer)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   VkResult result;

   result = panvk_draw_prepare_varyings(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return result;

   draw->info.layer_id = layer;
   if (draw->info.layer_id > 0) {
      cmdbuf->state.gfx.sysvals.layer_id = draw->info.layer_id;
      gfx_state_set_dirty(cmdbuf, FS_PUSH_UNIFORMS);
   }

   struct pan_ptr vs_push_uniforms;
   result = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(
      cmdbuf, vs, &vs_push_uniforms, 1);
   if (result != VK_SUCCESS)
      return result;
   cmdbuf->state.gfx.vs.push_uniforms = vs_push_uniforms.gpu;

   if (fs) {
      struct pan_ptr fs_push_uniforms;
      result = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(
         cmdbuf, fs, &fs_push_uniforms, 1);
      if (result != VK_SUCCESS)
         return result;
      cmdbuf->state.gfx.fs.push_uniforms = fs_push_uniforms.gpu;
   }

   result = panvk_draw_prepare_tiler_context(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return result;

   if (vs->info.vs.idvs) {
      result = panvk_draw_prepare_idvs_job(cmdbuf, draw);
      if (result != VK_SUCCESS)
         return result;
   } else {
      result = panvk_draw_prepare_vertex_job(cmdbuf, draw);
      if (result != VK_SUCCESS)
         return result;

      bool needs_tiling =
         !cmdbuf->vk.dynamic_graphics_state.rs.rasterizer_discard_enable ||
         cmdbuf->state.gfx.occlusion_query.mode !=
            MALI_OCCLUSION_MODE_DISABLED;

      if (needs_tiling) {
         result = panvk_draw_prepare_tiler_job(cmdbuf, draw);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   return VK_SUCCESS;
}

static void
panvk_cmd_draw(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_data *draw)
{
   const struct panvk_shader_variant *vs = panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   VkResult result;

   /* If there's no vertex shader, we can skip the draw. */
   if (!panvk_priv_mem_check_alloc(vs->rsd))
      return;

   /* Needs to be done before get_fs() is called because it depends on
    * fs.required being initialized. */
   cmdbuf->state.gfx.fs.required =
      fs_required(&cmdbuf->state.gfx, &cmdbuf->vk.dynamic_graphics_state);

   result = prepare_draw(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return;

   pan_pack_work_groups_compute(&draw->invocation, 1, draw->vertex_range,
                                draw->info.instance.count, 1, 1, 1, true,
                                false);

   struct panvk_batch *batch = cmdbuf->cur_batch;

   unsigned copy_desc_job_id =
      draw->jobs.vertex_copy_desc.gpu
         ? pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false, false,
                          0, 0, &draw->jobs.vertex_copy_desc, false)
         : 0;

   if (draw->jobs.frag_copy_desc.gpu) {
      /* We don't need to add frag_copy_desc as a dependency because the
       * tiler job doesn't execute the fragment shader, the fragment job
       * will, and the tiler/fragment synchronization happens at the batch
       * level. */
      pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false, false, 0, 0,
                     &draw->jobs.frag_copy_desc, false);
   }

   uint32_t view_mask = cmdbuf->state.gfx.render.view_mask;
   assert(view_mask == 0 || util_bitcount(view_mask) <= batch->fb.layer_count);
   uint32_t enabled_layer_count = view_mask
                                     ? util_bitcount(view_mask)
                                     : cmdbuf->state.gfx.render.layer_count;

   for (uint32_t i = 0; i < enabled_layer_count; i++) {
      const uint32_t layer = (view_mask != 0) ? u_bit_scan(&view_mask) : i;
      result = prepare_draw_layer(cmdbuf, draw, layer);
      if (result != VK_SUCCESS)
         return;

      if (vs->info.vs.idvs) {
         pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_INDEXED_VERTEX, false,
                        false, 0, copy_desc_job_id, &draw->jobs.idvs, false);
      } else {
         unsigned vjob_id =
            pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_VERTEX, false, false,
                           0, copy_desc_job_id, &draw->jobs.vertex, false);

         if (draw->jobs.tiler.gpu != 0) {
            pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_TILER, false, false,
                           vjob_id, 0, &draw->jobs.tiler, false);
         }
      }
   }

   clear_dirty_after_draw(cmdbuf);
}

static void
panvk_cmd_draw_indirect(struct panvk_cmd_buffer *cmdbuf,
                        struct panvk_draw_data *draw)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct panvk_shader_variant *vs = panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   VkResult result;

   /* If there's no vertex shader, we can skip the draw. */
   if (!panvk_priv_mem_check_alloc(vs->rsd))
      return;

   /* Needs to be done before get_fs() is called because it depends on
    * fs.required being initialized. */
   cmdbuf->state.gfx.fs.required =
      fs_required(&cmdbuf->state.gfx, &cmdbuf->vk.dynamic_graphics_state);

   result = prepare_draw(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return;

   struct panvk_batch *batch = cmdbuf->cur_batch;
   const struct vk_vertex_input_state *vi =
      cmdbuf->vk.dynamic_graphics_state.vi;

   unsigned init_heap_job_id;
   result = panvk_per_arch(cmd_init_poly_heap)(cmdbuf, &init_heap_job_id);
   if (result != VK_SUCCESS)
      return;

   unsigned copy_desc_job_id =
      draw->jobs.vertex_copy_desc.gpu
         ? pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false, false,
                          0, 0, &draw->jobs.vertex_copy_desc, false)
         : 0;

   if (draw->jobs.frag_copy_desc.gpu) {
      /* We don't need to add frag_copy_desc as a dependency because the
       * tiler job doesn't execute the fragment shader, the fragment job
       * will, and the tiler/fragment synchronization happens at the batch
       * level. */
      pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false, false, 0, 0,
                     &draw->jobs.frag_copy_desc, false);
   }

   uint32_t view_mask = cmdbuf->state.gfx.render.view_mask;
   assert(view_mask == 0 || util_bitcount(view_mask) <= batch->fb.layer_count);
   uint32_t enabled_layer_count = view_mask
                                     ? util_bitcount(view_mask)
                                     : cmdbuf->state.gfx.render.layer_count;

   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   uint64_t index_min_max_res_ptr = 0;
   uint32_t job_before_indirect_helper = copy_desc_job_id;
   if (draw->info.index.index_size) {
      index_min_max_res_ptr =
         panvk_cmd_alloc_dev_mem(
            cmdbuf, desc,
            sizeof(struct libpan_draw_helper_index_min_max_result), 8)
            .gpu;
      const struct panlib_draw_index_minmax_search_helper_args args = {
         .index_buffer_ptr = draw->info.index.buffer_dev_addr,
         .cmd = draw->info.indirect.buffer_dev_addr,
         .min_ptr =
            index_min_max_res_ptr +
            offsetof(struct libpan_draw_helper_index_min_max_result, min),
         .max_ptr =
            index_min_max_res_ptr +
            offsetof(struct libpan_draw_helper_index_min_max_result, max),
      };

      struct libpan_draw_helper_index_min_max_result val = {
         .min = ((uint64_t)1 << (draw->info.index.index_size * 8)) - 1,
         .max = 0,
      };
      uint64_t *raw_val = (uint64_t *)&val;

      struct pan_ptr write_job =
         pan_pool_alloc_desc(&cmdbuf->desc_pool.base, WRITE_VALUE_JOB);

      pan_section_pack(write_job.cpu, WRITE_VALUE_JOB, PAYLOAD, payload) {
         payload.type = MALI_WRITE_VALUE_TYPE_IMMEDIATE_64;
         payload.address = index_min_max_res_ptr;
         payload.immediate_value = *raw_val;
      };

      unsigned write_job_id =
         pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_WRITE_VALUE, false, false,
                        0, copy_desc_job_id, &write_job, false);
      util_dynarray_append(&batch->jobs, write_job.cpu);

      const uint32_t index_count =
         draw->info.index.buffer_size / draw->info.index.index_size;
      uint32_t wg_count = DIV_ROUND_UP(index_count, 65536);
      assert(wg_count <= 65536);

      panlib_draw_index_minmax_search_helper_struct(
         &precomp_ctx, panlib_1d_with_jm_deps(wg_count, 0, write_job_id),
         PANLIB_BARRIER_NONE, args, util_logbase2(draw->info.index.index_size),
         draw->info.index.restart_enable);
      job_before_indirect_helper = batch->vtc_jc.job_index;
   }

   for (uint32_t i = 0; i < enabled_layer_count; i++) {
      const uint32_t layer = (view_mask != 0) ? u_bit_scan(&view_mask) : i;

      /* Force a new push uniform block to be allocated */
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);

      result = prepare_draw_layer(cmdbuf, draw, layer);
      if (result != VK_SUCCESS)
         return;

      assert(draw->info.indirect.buffer_dev_addr != 0 ||
             draw->info.index.index_size);

      uint32_t attrib_bufs_valid = vi->bindings_valid;
      uint32_t attribs_valid = vi->attributes_valid;
      uint64_t first_vertex_sysval = 0x8ull << 60;
      uint64_t first_instance_sysval = 0x8ull << 60;
      uint64_t raw_vertex_offset_sysval = 0x8ull << 60;
      if (shader_uses_sysval(vs, graphics, vs.first_vertex)) {
         first_vertex_sysval = cmdbuf->state.gfx.vs.push_uniforms +
                               shader_remapped_sysval_offset(
                                  vs, sysval_offset(graphics, vs.first_vertex));
      }

      if (shader_uses_sysval(vs, graphics, vs.base_instance)) {
         first_instance_sysval =
            cmdbuf->state.gfx.vs.push_uniforms +
            shader_remapped_sysval_offset(
               vs, sysval_offset(graphics, vs.base_instance));
      }

      if (shader_uses_sysval(vs, graphics, vs.raw_vertex_offset)) {
         raw_vertex_offset_sysval =
            cmdbuf->state.gfx.vs.push_uniforms +
            shader_remapped_sysval_offset(
               vs, sysval_offset(graphics, vs.raw_vertex_offset));
      }

      enum panlib_barrier indirect_barrier =
         PANLIB_BARRIER_JM_SUPPRESS_PREFETCH;
      struct panlib_precomp_grid indirect_grid = panlib_1d_with_jm_deps(
         1, init_heap_job_id, job_before_indirect_helper);

      if (draw->info.indirect.buffer_dev_addr != 0 &&
          draw->info.index.index_size) {
         const struct panlib_draw_indexed_indirect_helper_args args = {
            .cmd = draw->info.indirect.buffer_dev_addr,
            .heap = panvk_priv_mem_dev_addr(dev->poly_heap.state),
            .index_buffer_ptr = draw->info.index.buffer_dev_addr,
            .index_min_max_res = index_min_max_res_ptr,
            .index_size = draw->info.index.index_size,
            .primitive_vertex_count =
               mesa_vertices_per_prim(draw->info.prim),
            .varying_bufs_descs = draw->varying_bufs,
            .attrib_bufs_descs = draw->vs.attribute_bufs,
            .attrib_bufs_infos = draw->indirect_info.attrib_bufs,
            .attrib_bufs_valid = attrib_bufs_valid,
            .attribs_valid = attribs_valid,
            .attribs_descs = draw->vs.attributes,
            .attribs_infos = draw->indirect_info.attribs,
            .first_vertex_sysval = first_vertex_sysval,
            .first_instance_sysval = first_instance_sysval,
            .raw_vertex_offset_sysval = raw_vertex_offset_sysval,
            .idvs_job = vs->info.vs.idvs ? draw->jobs.idvs.gpu : 0,
            .vertex_job = draw->jobs.vertex.gpu,
            .tiler_job = draw->jobs.tiler.gpu,
         };
         panlib_draw_indexed_indirect_helper_struct(&precomp_ctx, indirect_grid,
                                                    indirect_barrier, args);
      } else if (draw->info.indirect.buffer_dev_addr != 0) {
         const struct panlib_draw_indirect_helper_args args = {
            .cmd = draw->info.indirect.buffer_dev_addr,
            .primitive_vertex_count =
               mesa_vertices_per_prim(draw->info.prim),
            .heap = panvk_priv_mem_dev_addr(dev->poly_heap.state),
            .varying_bufs_descs = draw->varying_bufs,
            .attrib_bufs_descs = draw->vs.attribute_bufs,
            .attrib_bufs_infos = draw->indirect_info.attrib_bufs,
            .attrib_bufs_valid = attrib_bufs_valid,
            .attribs_valid = attribs_valid,
            .attribs_descs = draw->vs.attributes,
            .attribs_infos = draw->indirect_info.attribs,
            .first_vertex_sysval = first_vertex_sysval,
            .first_instance_sysval = first_instance_sysval,
            .raw_vertex_offset_sysval = raw_vertex_offset_sysval,
            .idvs_job = vs->info.vs.idvs ? draw->jobs.idvs.gpu : 0,
            .vertex_job = draw->jobs.vertex.gpu,
            .tiler_job = draw->jobs.tiler.gpu,
         };
         panlib_draw_indirect_helper_struct(&precomp_ctx, indirect_grid,
                                            indirect_barrier, args);
      } else {
         assert(false && "Invalid indirect draw");
      }

      /* Grab the index of the indirect helper job */
      uint32_t prev_job = batch->vtc_jc.job_index;

      if (vs->info.vs.idvs) {
         pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_INDEXED_VERTEX, false,
                        false, 0, prev_job, &draw->jobs.idvs, false);
      } else {
         unsigned vjob_id =
            pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_VERTEX, false, true, 0,
                           prev_job, &draw->jobs.vertex, false);

         if (draw->jobs.tiler.gpu != 0) {
            pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_TILER, false, false,
                           vjob_id, 0, &draw->jobs.tiler, false);
         }
      }
   }

   /*
    * We split every ~1024 indirect draw.
    * This is here for multiple reasons:
    * - The heap offset need to be reset at some point to avoid going outside of
    * bounds.
    * - It is possible to always end up with timeouts for batches with 4k draws
    * (see "dEQP-VK.api.command_buffers.many_indirect_draws_on_secondary") At
    * the same time, because of how TLS works on Mali, we should not split too
    * much as this will cause the TLS budget to go crazy.
    */
   if (batch->vtc_jc.job_index > (5 * 1024)) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      batch = panvk_per_arch(cmd_open_batch)(cmdbuf);
      cmdbuf->state.uses_poly_heap = false;
   }

   clear_dirty_after_draw(cmdbuf);
}

static unsigned
padded_vertex_count(struct panvk_cmd_buffer *cmdbuf, uint32_t vertex_count,
                    uint32_t instance_count)
{
   if (instance_count == 1)
      return vertex_count;

   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   bool idvs = vs->info.vs.idvs;

   /* Index-Driven Vertex Shading requires different instances to
    * have different cache lines for position results. Each vertex
    * position is 16 bytes and the Mali cache line is 64 bytes, so
    * the instance count must be aligned to 4 vertices.
    */
   if (idvs)
      vertex_count = ALIGN_POT(vertex_count, 4);

   return pan_padded_vertex_count(vertex_count);
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

   struct panvk_draw_data draw = {
      .info = {
         .vertex.base = firstVertex,
         .vertex.raw_offset = firstVertex,
         .vertex.count = vertexCount,
         .instance.base = firstInstance,
         .instance.count = instanceCount,
         .prim = panvk_get_client_prim(cmdbuf),
      },
      .vertex_range = vertexCount,
      .padded_vertex_count =
         padded_vertex_count(cmdbuf, vertexCount, instanceCount),
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

   struct pan_ptr indirect_index_alloc = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct VkDrawIndexedIndirectCommand), 8);

   struct VkDrawIndexedIndirectCommand *indirect_index_alloc_ptr =
      indirect_index_alloc.cpu;

   *indirect_index_alloc_ptr = (struct VkDrawIndexedIndirectCommand){
      .indexCount = indexCount,
      .instanceCount = instanceCount,
      .firstIndex = firstIndex,
      .vertexOffset = vertexOffset,
      .firstInstance = firstInstance,
   };

   struct panvk_draw_data draw = {
      .info = {
         .index = panvk_draw_info_index(cmdbuf, 0),
         .indirect.buffer_dev_addr = indirect_index_alloc.gpu,
         .indirect.draw_count = 1,
         .indirect.stride = 0,
         .prim = panvk_get_client_prim(cmdbuf),
      },
   };

   panvk_cmd_draw_indirect(cmdbuf, &draw);
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

   /* We cannot support arbitrary draw count on JM */
   assert(drawCount == 1);

   struct panvk_draw_data draw = {
      .info = {
         .indirect.buffer_dev_addr = panvk_buffer_gpu_ptr(buffer, offset),
         .indirect.draw_count = drawCount,
         .indirect.stride = stride,
         .prim = panvk_get_client_prim(cmdbuf),
      },
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

   /* Because we don't currently advertise nullDescriptor for JM, it is only
    * valid to draw with a null index buffer if the draw accesses 0 indices.
    * For direct draws, this is covered by checks on instancedCount and
    * indexCount. For indirect draws we need to add an additional check, under
    * the assumption that if the index buffer is null, the draw must be empty.
    */
   if (drawCount == 0 || cmdbuf->state.gfx.ib.size == 0)
      return;

   /* We cannot support arbitrary draw count on JM */
   assert(drawCount == 1);

   struct panvk_draw_data draw = {
      .info = {
         .index = panvk_draw_info_index(cmdbuf, 0),
         .indirect.buffer_dev_addr = panvk_buffer_gpu_ptr(buffer, offset),
         .indirect.draw_count = drawCount,
         .indirect.stride = stride,
         .prim = panvk_get_client_prim(cmdbuf),
      },
   };

   panvk_cmd_draw_indirect(cmdbuf, &draw);
}
#else /* PAN_ARCH == 9 */

static enum mali_draw_mode
v9_translate_prim(enum mesa_prim prim)
{
   switch (prim) {
   case MESA_PRIM_POINTS:                   return MALI_DRAW_MODE_POINTS;
   case MESA_PRIM_LINES:                    return MALI_DRAW_MODE_LINES;
   case MESA_PRIM_LINE_STRIP:               return MALI_DRAW_MODE_LINE_STRIP;
   case MESA_PRIM_TRIANGLES:                return MALI_DRAW_MODE_TRIANGLES;
   case MESA_PRIM_TRIANGLE_STRIP:           return MALI_DRAW_MODE_TRIANGLE_STRIP;
   case MESA_PRIM_TRIANGLE_FAN:             return MALI_DRAW_MODE_TRIANGLE_FAN;
   case MESA_PRIM_LINES_ADJACENCY:          return MALI_DRAW_MODE_LINES_ADJACENCY;
   case MESA_PRIM_LINE_STRIP_ADJACENCY:     return MALI_DRAW_MODE_LINE_STRIP_ADJACENCY;
   case MESA_PRIM_TRIANGLES_ADJACENCY:      return MALI_DRAW_MODE_TRIANGLES_ADJACENCY;
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY: return MALI_DRAW_MODE_TRIANGLE_STRIP_ADJACENCY;
   default:
      UNREACHABLE("Invalid primitive type");
   }
}

static enum mali_func
v9_translate_compare_func(VkCompareOp comp)
{
   return (enum mali_func)comp;
}

static enum mali_stencil_op
v9_translate_stencil_op(VkStencilOp in)
{
   switch (in) {
   case VK_STENCIL_OP_KEEP:                return MALI_STENCIL_OP_KEEP;
   case VK_STENCIL_OP_ZERO:                return MALI_STENCIL_OP_ZERO;
   case VK_STENCIL_OP_REPLACE:             return MALI_STENCIL_OP_REPLACE;
   case VK_STENCIL_OP_INCREMENT_AND_CLAMP: return MALI_STENCIL_OP_INCR_SAT;
   case VK_STENCIL_OP_DECREMENT_AND_CLAMP: return MALI_STENCIL_OP_DECR_SAT;
   case VK_STENCIL_OP_INCREMENT_AND_WRAP:  return MALI_STENCIL_OP_INCR_WRAP;
   case VK_STENCIL_OP_DECREMENT_AND_WRAP:  return MALI_STENCIL_OP_DECR_WRAP;
   case VK_STENCIL_OP_INVERT:              return MALI_STENCIL_OP_INVERT;
   default:
      UNREACHABLE("Invalid stencil op");
   }
}

static void
v9_emit_vs_attrib(struct panvk_cmd_buffer *cmdbuf, uint32_t attrib_idx,
                  uint32_t vb_desc_offset, struct mali_attribute_packed *desc,
                  uint32_t base_instance)
{
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_vertex_input_state *vi = dyns->vi;
   const struct vk_vertex_attribute_state *attrib_info = &vi->attributes[attrib_idx];
   const struct vk_vertex_binding_state *buf_info = &vi->bindings[attrib_info->binding];
   const uint32_t stride = dyns->vi_binding_strides[attrib_info->binding];
   bool per_instance = buf_info->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE;
   enum pipe_format f = vk_format_to_pipe_format(attrib_info->format);

   pan_pack(desc, ATTRIBUTE, cfg) {
      /* Valhall doesn't add firstInstance when fetching per-instance
       * attributes, so fold it into the attribute offset. */
      cfg.offset = attrib_info->offset +
                   ((per_instance && buf_info->divisor)
                       ? (base_instance / buf_info->divisor) * stride : 0);
      cfg.format = GENX(pan_format_from_pipe_format)(f)->hw;
      cfg.table = 0;
      cfg.buffer_index = vb_desc_offset + attrib_info->binding;
      cfg.stride = stride;
      if (!per_instance) {
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_VERTEX;
         cfg.offset_enable = true;
      } else if (buf_info->divisor <= 1) {
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_INSTANCE;
         if (buf_info->divisor == 0)
            cfg.stride = 0;
      } else if (util_is_power_of_two_or_zero(buf_info->divisor)) {
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D_POT_DIVISOR;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_INSTANCE;
         cfg.divisor_r = __builtin_ctz(buf_info->divisor);
      } else {
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_INSTANCE;
         cfg.divisor_d = pan_compute_npot_divisor(buf_info->divisor,
                                                  &cfg.divisor_r, &cfg.divisor_e);
      }
   }
}

static VkResult
v9_prepare_vs_driver_set_ex(struct panvk_cmd_buffer *cmdbuf,
                            const struct panvk_shader *vs_shader,
                            struct panvk_shader_desc_state *st,
                            uint32_t base_instance)
{
   const struct panvk_shader_desc_info *vs_desc_info = &vs_shader->desc_info;
   const struct vk_vertex_input_state *vi =
      cmdbuf->vk.dynamic_graphics_state.vi;
   const struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;

   uint32_t vb_count = 0;
   u_foreach_bit(i, vi->attributes_valid)
      vb_count = MAX2(vi->attributes[i].binding + 1, vb_count);

   uint32_t vb_offset = vs_desc_info->dyn_bufs.count + MAX_VS_ATTRIBS + 1;
   uint32_t desc_count = vb_offset + vb_count;
   struct pan_ptr driver_set = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, desc_count * PANVK_DESCRIPTOR_SIZE, PANVK_DESCRIPTOR_SIZE);
   if (!driver_set.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct panvk_opaque_desc *descs = driver_set.cpu;
   for (uint32_t i = 0; i < MAX_VS_ATTRIBS; i++) {
      if (vi->attributes_valid & BITFIELD_BIT(i))
         v9_emit_vs_attrib(cmdbuf, i, vb_offset, (void *)&descs[i], base_instance);
      else
         pan_cast_and_pack(&descs[i], NULL_DESCRIPTOR, cfg);
   }

   pan_cast_and_pack(&descs[MAX_VS_ATTRIBS], SAMPLER, cfg) {
      cfg.clamp_integer_array_indices = false;
   }
   panvk_per_arch(cmd_fill_dyn_bufs)(desc_state, vs_desc_info,
      (struct mali_buffer_packed *)(&descs[MAX_VS_ATTRIBS + 1]));

   for (uint32_t i = 0; i < vb_count; i++) {
      const struct panvk_attrib_buf *vb = &cmdbuf->state.gfx.vb.bufs[i];
      if ((vi->bindings_valid & BITFIELD_BIT(i)) && vb->size) {
         pan_cast_and_pack(&descs[vb_offset + i], BUFFER, cfg) {
            cfg.address = vb->address;
            cfg.size = vb->size;
         }
      } else {
         pan_cast_and_pack(&descs[vb_offset + i], NULL_DESCRIPTOR, cfg);
      }
   }

   st->driver_set.dev_addr = driver_set.gpu;
   st->driver_set.size = desc_count * PANVK_DESCRIPTOR_SIZE;
   return VK_SUCCESS;
}

static VkResult
v9_prepare_vs_driver_set(struct panvk_cmd_buffer *cmdbuf, uint32_t base_instance)
{
   return v9_prepare_vs_driver_set_ex(cmdbuf, cmdbuf->state.gfx.vs.shader,
                                      &cmdbuf->state.gfx.vs.desc, base_instance);
}

/* Driver set of a stage without vertex attributes (FS, GS kernel): the
 * dummy sampler and the dynamic buffers. */
static VkResult
v9_prepare_stage_driver_set(struct panvk_cmd_buffer *cmdbuf,
                            const struct panvk_shader *shader,
                            struct panvk_shader_desc_state *st)
{
   const struct panvk_shader_desc_info *info = &shader->desc_info;
   uint32_t desc_count = info->dyn_bufs.count + 1;
   struct pan_ptr driver_set = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, desc_count * PANVK_DESCRIPTOR_SIZE, PANVK_DESCRIPTOR_SIZE);
   if (desc_count && !driver_set.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct panvk_opaque_desc *descs = driver_set.cpu;
   pan_cast_and_pack(&descs[0], SAMPLER, cfg) {
      cfg.clamp_integer_array_indices = false;
   }
   panvk_per_arch(cmd_fill_dyn_bufs)(&cmdbuf->state.gfx.desc_state, info,
      (struct mali_buffer_packed *)(&descs[1]));
   st->driver_set.dev_addr = driver_set.gpu;
   st->driver_set.size = desc_count * PANVK_DESCRIPTOR_SIZE;
   return VK_SUCCESS;
}

static VkResult
v9_prepare_fs_driver_set(struct panvk_cmd_buffer *cmdbuf)
{
   if (!cmdbuf->state.gfx.fs.shader)
      return VK_SUCCESS;

   return v9_prepare_stage_driver_set(cmdbuf, cmdbuf->state.gfx.fs.shader,
                                      &cmdbuf->state.gfx.fs.desc);
}

static VkResult
v9_prepare_blend_zsd(struct panvk_cmd_buffer *cmdbuf,
                     const struct panvk_shader_variant *fs,
                     uint64_t *blend_gpu, uint64_t *zsd_gpu)
{
   uint32_t bd_count = cmdbuf->state.gfx.render.fb.layout.rt_count;
   struct pan_ptr bptr = panvk_cmd_alloc_desc_array(cmdbuf, bd_count, BLEND);
   if (bd_count && !bptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   if (fs) {
      VkResult r = panvk_per_arch(blend_emit_descs)(cmdbuf, bptr.cpu);
      if (r != VK_SUCCESS)
         return r;
   } else {
      struct mali_blend_packed *bds = bptr.cpu;
      for (unsigned i = 0; i < bd_count; i++) {
         pan_pack(&bds[i], BLEND, cfg) {
            cfg.enable = false;
            cfg.internal.mode = MALI_BLEND_MODE_OFF;
         }
      }
   }
   *blend_gpu = bptr.gpu;

   const struct vk_dynamic_graphics_state *dyns = &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_depth_stencil_state *ds = &dyns->ds;
   const struct vk_rasterization_state *rs = &dyns->rs;
   bool test_z = (cmdbuf->state.gfx.render.bound_attachments &
                  MESA_VK_RP_ATTACHMENT_DEPTH_BIT) && ds->depth.test_enable;
   bool test_s = (cmdbuf->state.gfx.render.bound_attachments &
                  MESA_VK_RP_ATTACHMENT_STENCIL_BIT) && ds->stencil.test_enable;

   struct pan_ptr zsd = panvk_cmd_alloc_desc(cmdbuf, DEPTH_STENCIL);
   if (!zsd.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   pan_cast_and_pack(zsd.cpu, DEPTH_STENCIL, cfg) {
      cfg.stencil_test_enable = test_s;
      if (test_s) {
         cfg.front_compare_function = v9_translate_compare_func(ds->stencil.front.op.compare);
         cfg.front_stencil_fail = v9_translate_stencil_op(ds->stencil.front.op.fail);
         cfg.front_depth_fail = v9_translate_stencil_op(ds->stencil.front.op.depth_fail);
         cfg.front_depth_pass = v9_translate_stencil_op(ds->stencil.front.op.pass);
         cfg.back_compare_function = v9_translate_compare_func(ds->stencil.back.op.compare);
         cfg.back_stencil_fail = v9_translate_stencil_op(ds->stencil.back.op.fail);
         cfg.back_depth_fail = v9_translate_stencil_op(ds->stencil.back.op.depth_fail);
         cfg.back_depth_pass = v9_translate_stencil_op(ds->stencil.back.op.pass);
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
      if (fs)
         cfg.depth_source = pan_depth_source(&fs->info);
      cfg.depth_write_enable = test_z && ds->depth.write_enable;
      cfg.depth_bias_enable = rs->depth_bias.enable;
      cfg.depth_function = test_z ? v9_translate_compare_func(ds->depth.compare_op)
                                  : MALI_FUNC_ALWAYS;
      cfg.depth_units = rs->depth_bias.constant_factor;
      cfg.depth_factor = rs->depth_bias.slope_factor;
      cfg.depth_bias_clamp = rs->depth_bias.clamp;
   }
   *zsd_gpu = zsd.gpu;
   return VK_SUCCESS;
}

/* A viewport-selecting shader with more than one viewport: its passthrough
 * does each primitive's viewport transform and scissor itself (clip planes),
 * the fixed function only clamps depth. */
static bool
v9_multi_viewport(const struct panvk_cmd_buffer *cmdbuf)
{
   return panvk_prerast_viewport_shader(&cmdbuf->state.gfx) &&
          cmdbuf->vk.dynamic_graphics_state.vp.viewport_count > 1;
}

/* Depth clamp range of one viewport, CSF panvk_viewport_depth_range(). */
static void
v9_viewport_depth_range(const struct panvk_cmd_buffer *cmdbuf, unsigned index,
                        float *z_min, float *z_max)
{
   const struct vk_viewport_state *vp = &cmdbuf->vk.dynamic_graphics_state.vp;

   if (vp->depth_clamp_mode == VK_DEPTH_CLAMP_MODE_USER_DEFINED_RANGE_EXT) {
      *z_min = vp->depth_clamp_range.minDepthClamp;
      *z_max = vp->depth_clamp_range.maxDepthClamp;
      return;
   }

   float sz, oz;
   panvk_viewport_z_coeffs(vp, &cmdbuf->vk.dynamic_graphics_state.rs, index,
                           &sz, &oz);
   float a = vp->depth_clip_negative_one_to_one ? oz - sz : oz;
   float b = oz + sz;
   *z_min = MIN2(a, b);
   *z_max = MAX2(a, b);
}

static void
v9_emit_scissor(struct panvk_cmd_buffer *cmdbuf, struct mali_scissor_packed *out)
{
   if (v9_multi_viewport(cmdbuf)) {
      /* The framebuffer: it cannot cut a primitive of another viewport. */
      uint32_t w = cmdbuf->state.gfx.render.fb.layout.width_px;
      uint32_t h = cmdbuf->state.gfx.render.fb.layout.height_px;

      w = w ? w : MAX_FRAMEBUFFER_DIMENSION;
      h = h ? h : MAX_FRAMEBUFFER_DIMENSION;
      pan_pack(out, SCISSOR, cfg) {
         cfg.scissor_minimum_x = 0;
         cfg.scissor_minimum_y = 0;
         cfg.scissor_maximum_x = MIN2(w - 1, (uint32_t)UINT16_MAX);
         cfg.scissor_maximum_y = MIN2(h - 1, (uint32_t)UINT16_MAX);
      }
      return;
   }

   const VkViewport *vp = &cmdbuf->vk.dynamic_graphics_state.vp.viewports[0];
   const VkRect2D *sc = &cmdbuf->vk.dynamic_graphics_state.vp.scissors[0];
   int minx = MAX2(sc->offset.x, (int)vp->x);
   int maxx = MIN2(sc->offset.x + (int)sc->extent.width, (int)(vp->x + vp->width));
   int miny = MAX2(sc->offset.y, (int)MIN2(vp->y, vp->y + vp->height));
   int maxy = MIN2(sc->offset.y + (int)sc->extent.height,
                   (int)MAX2(vp->y, vp->y + vp->height));
   maxx = maxx > minx ? maxx - 1 : maxx;
   maxy = maxy > miny ? maxy - 1 : maxy;
   pan_pack(out, SCISSOR, cfg) {
      cfg.scissor_minimum_x = CLAMP(minx, 0, UINT16_MAX);
      cfg.scissor_minimum_y = CLAMP(miny, 0, UINT16_MAX);
      cfg.scissor_maximum_x = CLAMP(maxx, 0, UINT16_MAX);
      cfg.scissor_maximum_y = CLAMP(maxy, 0, UINT16_MAX);
   }
}

/* PANVK_SKIP_FS workaround (default off): some fragment shaders that spill
 * registers run past the kernel job timeout (Far Cry 3). 1 = skip draws
 * whose fragment shader spills (tls > 0), 2 = also skip shaders using 64
 * registers. Skipped draws are culled, so those objects are missing. */
static bool
panvk_skip_fs(unsigned regs, unsigned tls)
{
   static int mode = -1;

   if (mode < 0) {
      const char *e = getenv("PANVK_SKIP_FS");
      mode = e ? atoi(e) : 0;
      if (mode)
         fprintf(stderr, "panvk: PANVK_SKIP_FS=%d\n", mode);
   }
   return (mode == 1 && tls > 0) || (mode == 2 && (tls > 0 || regs > 32));
}

/* Instance-rate attributes with a divisor of an indirect draw: the patch
 * kernels fold the GPU-side firstInstance into their Offset
 * (v9_emit_vs_attrib only saw the placeholder 0). Returns false when out of
 * memory. */
static bool
v9_inst_attr_info(struct panvk_cmd_buffer *cmdbuf, uint32_t *mask,
                  uint64_t *info_gpu, uint64_t *vs_attribs)
{
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_vertex_input_state *vi = dyns->vi;
   uint32_t info[MAX_VS_ATTRIBS][3] = {0};

   *mask = 0;
   *info_gpu = 0;
   *vs_attribs = 0;
   u_foreach_bit(a, vi->attributes_valid) {
      const uint32_t b = vi->attributes[a].binding;
      if (vi->bindings[b].input_rate != VK_VERTEX_INPUT_RATE_INSTANCE ||
          !vi->bindings[b].divisor)
         continue;
      *mask |= BITFIELD_BIT(a);
      info[a][0] = vi->attributes[a].offset;
      info[a][1] = dyns->vi_binding_strides[b];
      info[a][2] = vi->bindings[b].divisor;
   }
   if (!*mask)
      return true;

   struct pan_ptr ii = panvk_cmd_alloc_dev_mem(cmdbuf, desc, sizeof(info), 4);
   if (!ii.gpu)
      return false;
   memcpy(ii.cpu, info, sizeof(info));
   *info_gpu = ii.gpu;
   *vs_attribs = cmdbuf->state.gfx.vs.desc.driver_set.dev_addr;
   return true;
}

static VkResult
v9_emit_malloc_vertex_job(struct panvk_cmd_buffer *cmdbuf,
                          const struct panvk_draw_info *info,
                          const struct panvk_shader_variant *vs,
                          const struct panvk_shader_variant *fs,
                          uint64_t blend_gpu, uint64_t zsd_gpu)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct pan_ptr job = panvk_cmd_alloc_desc(cmdbuf, MALLOC_VERTEX_JOB);
   /* Shaders that spill or use scratch (e.g. DXVK indexable temps) need
    * thread-local storage; the v9 path never sized it. */
   batch->tlsinfo.tls.size = MAX3(vs->info.tls_size,
                                  fs ? fs->info.tls_size : 0,
                                  batch->tlsinfo.tls.size);
   if (!job.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   util_dynarray_append(&batch->jobs, job.cpu);
   util_dynarray_append(&batch->malloc_vertex_jobs, job.cpu);

   const struct vk_dynamic_graphics_state *dyns = &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_rasterization_state *rs = &dyns->rs;
   enum mesa_prim reduced = u_reduced_prim(info->prim);
   bool polygon = reduced == MESA_PRIM_TRIANGLES;
   float z_min, z_max;
   if (info->viewport_clamp)
      v9_viewport_depth_range(cmdbuf, info->viewport_clamp - 1, &z_min, &z_max);
   else
      panvk_depth_range(&cmdbuf->state.gfx, &dyns->vp, &z_min, &z_max);

   const bool secondary_shader = vs->info.vs.secondary_enable && fs != NULL;
   /* Use the real varying count from the linked vs/fs pair instead of a
    * vkcube-only hardcoded guess. Position is IDVS-side and not counted here,
    * so vs varying count (fs consumes a subset/same set) drives the stride. */
   const unsigned vary_slots = MAX2(vs->info.varyings.formats.count, 1);
   /* The compiler places varyings by layout offset, not densely by count:
    * use the layout's real byte size (as CSF does) so a vertex's varyings
    * never spill into the next vertex packet. Never smaller than before. */
   const unsigned vary_bytes =
      MAX2(vary_slots * 16, ALIGN_POT(vs->info.varyings.formats.generic_size_B, 16));
   const unsigned vary_stride = secondary_shader ? vary_bytes : 0;
   PANVK_TRACE_PRINTF(
           "[V9-VARY] secondary=%d slots=%u packet=%u attr=%u var_spd=%d\n",
           (int)secondary_shader, vary_slots,
           secondary_shader ? vary_stride + 16 : 16, vary_stride,
           (int)panvk_priv_mem_check_alloc(vs->spds.var));

   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, PRIMITIVE, cfg) {
      cfg.draw_mode = v9_translate_prim(info->prim);
      cfg.allow_rotating_primitives = polygon;
      cfg.primitive_restart = false;
      cfg.low_depth_cull = cfg.high_depth_cull =
         vk_rasterization_state_depth_clip_enable(rs);
      cfg.index_count = info->vertex.count;
      cfg.base_vertex_offset = info->vertex.base;
      if (info->index.index_size == 4)
         cfg.index_type = MALI_INDEX_TYPE_UINT32;
      else if (info->index.index_size == 2)
         cfg.index_type = MALI_INDEX_TYPE_UINT16;
      else if (info->index.index_size == 1)
         cfg.index_type = MALI_INDEX_TYPE_UINT8;
      else
         cfg.index_type = MALI_INDEX_TYPE_NONE;
      cfg.primitive_restart = info->index.restart_enable;
      cfg.secondary_shader = secondary_shader;
   }
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, INSTANCE_COUNT, cfg) {
      cfg.count = info->instance.count;
   }
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, ALLOCATION, cfg) {
      cfg.vertex_packet_stride = secondary_shader ? (vary_stride + 16) : 16;
      cfg.vertex_attribute_stride = vary_stride;
   }
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, TILER, cfg) {
      cfg.address = batch->tiler.ctx.valhall.desc;
   }
   v9_emit_scissor(cmdbuf, pan_section_ptr(job.cpu, MALLOC_VERTEX_JOB, SCISSOR));
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, PRIMITIVE_SIZE, cfg) {
      cfg.fixed_sized = rs->line.width;
   }
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, INDICES, cfg) {
      cfg.address = info->index.buffer_dev_addr
                    ? (info->index.buffer_dev_addr + info->index.offset)
                    : 0;
   }
   (void)0;

   static int tess_nocull = -1;
   if (tess_nocull < 0) {
      const char *e = getenv("PANVK_TESS_NOCULL");
      tess_nocull = e && e[0] == '1';
      if (tess_nocull)
         (void)0;
   }
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, DRAW, cfg) {
      cfg.flags_0.cull_front_face = !tess_nocull && polygon && (rs->cull_mode & VK_CULL_MODE_FRONT_BIT);
      cfg.flags_0.cull_back_face = !tess_nocull && polygon && (rs->cull_mode & VK_CULL_MODE_BACK_BIT);
      cfg.flags_0.front_face_ccw =
         rs->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE;
      cfg.flags_0.multisample_enable = dyns->ms.rasterization_samples > 1;
      cfg.flags_1.sample_mask = cfg.flags_0.multisample_enable ? dyns->ms.sample_mask : 0xFFFF;
      cfg.flags_0.aligned_line_ends = rs->line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM;
      cfg.vertex_array.packet = true;
      /* v58e: occlusion queries on the v9 MALLOC_VERTEX path */
      cfg.flags_0.occlusion_query = cmdbuf->state.gfx.occlusion_query.mode;
      cfg.occlusion = cmdbuf->state.gfx.occlusion_query.ptr;
      cfg.minimum_z = z_min;
      cfg.maximum_z = z_max;
      cfg.depth_stencil = zsd_gpu;
      cfg.blend = blend_gpu;
      cfg.blend_count = MAX2(cmdbuf->state.gfx.render.fb.layout.rt_count, 1);
      if (fs) {
         static int force_early = -1;
         if (force_early < 0) {
            const char *e = getenv("PANVK_FORCE_EARLYZS");
            force_early = e && e[0] == '1';
         }
         const bool late_zs = !force_early &&
            !fs->info.fs.early_fragment_tests &&
            (fs->info.fs.can_discard || fs->info.fs.writes_depth ||
             fs->info.fs.writes_stencil || fs->info.fs.writes_coverage ||
             dyns->ms.alpha_to_coverage_enable);
         if (panvk_skip_fs(fs->info.work_reg_count, fs->info.tls_size)) {
            cfg.flags_0.cull_front_face = true;
            cfg.flags_0.cull_back_face = true;
         }
         cfg.flags_0.pixel_kill_operation =
            late_zs ? MALI_PIXEL_KILL_FORCE_LATE : MALI_PIXEL_KILL_FORCE_EARLY;
         cfg.flags_0.zs_update_operation =
            late_zs ? MALI_PIXEL_KILL_FORCE_LATE : MALI_PIXEL_KILL_FORCE_EARLY;
         /* the GPU must wait for the shader's coverage (discard,
          * sample mask, alpha-to-coverage) before updating depth/stencil. */
         cfg.flags_0.shader_modifies_coverage = !v61a_no_smc() &&
            (fs->info.fs.can_discard || fs->info.fs.writes_coverage ||
             dyns->ms.alpha_to_coverage_enable);
         /* FPK would discard what lies underneath; only allowed when blending
          * doesn't read the destination (and no alpha-to-coverage). */
         cfg.flags_0.allow_forward_pixel_to_kill =
            fs->info.fs.can_fpk &&
            !cmdbuf->state.gfx.cb.info.any_dest_read &&
            !dyns->ms.alpha_to_coverage_enable;
         cfg.flags_0.allow_forward_pixel_to_be_killed = !fs->info.writes_global;
         cfg.flags_1.render_target_mask =
            color_attachment_written_mask(fs, &cmdbuf->vk.dynamic_graphics_state.cal);
         cfg.shader.resources = cmdbuf->state.gfx.fs.desc.res_table;
         cfg.shader.shader = panvk_priv_mem_dev_addr(fs->spd);
         cfg.shader.thread_storage = batch->tls.gpu;
         cfg.shader.fau = cmdbuf->state.gfx.fs.push_uniforms;
         cfg.shader.fau_count = fs->fau.total_count;
      } else {
         cfg.flags_0.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_EARLY;
         cfg.flags_0.zs_update_operation = MALI_PIXEL_KILL_FORCE_EARLY;
         cfg.flags_0.allow_forward_pixel_to_kill = true;
         cfg.flags_0.allow_forward_pixel_to_be_killed = true;
      }
   }

   const bool points = info->prim == MESA_PRIM_POINTS;
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, POSITION, cfg) {
      cfg.resources = cmdbuf->state.gfx.vs.desc.res_table;
      cfg.thread_storage = batch->tls.gpu;
      cfg.shader = points ? panvk_priv_mem_dev_addr(vs->spds.pos_points)
                          : panvk_priv_mem_dev_addr(vs->spds.pos_triangles);
      cfg.fau = cmdbuf->state.gfx.vs.push_uniforms;
      cfg.fau_count = vs->fau.total_count;
   }

   (void)0;
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, VARYING, cfg) {
      if (panvk_priv_mem_check_alloc(vs->spds.var)) {
         cfg.resources = cmdbuf->state.gfx.vs.desc.res_table;
         cfg.thread_storage = batch->tls.gpu;
         cfg.shader = panvk_priv_mem_dev_addr(vs->spds.var);
         cfg.fau = cmdbuf->state.gfx.vs.push_uniforms;
         cfg.fau_count = vs->fau.total_count;
      } else {
         cfg.resources = 0;
         cfg.thread_storage = 0;
         cfg.shader = 0;
         cfg.fau = 0;
         cfg.fau_count = 0;
      }
   }

   unsigned indirect_dep = 0;
   if (info->indirect.buffer_dev_addr) {
      uint64_t fv_ptr = 0, bi_ptr = 0;
      if (shader_uses_sysval(vs, graphics, vs.first_vertex))
         fv_ptr = cmdbuf->state.gfx.vs.push_uniforms +
                  shader_remapped_sysval_offset(
                     vs, sysval_offset(graphics, vs.first_vertex));
      if (shader_uses_sysval(vs, graphics, vs.base_instance))
         bi_ptr = cmdbuf->state.gfx.vs.push_uniforms +
                  shader_remapped_sysval_offset(
                     vs, sysval_offset(graphics, vs.base_instance));

      uint32_t inst_mask;
      uint64_t inst_info_gpu, vs_attribs;
      if (!v9_inst_attr_info(cmdbuf, &inst_mask, &inst_info_gpu, &vs_attribs))
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      struct panvk_precomp_ctx pctx = panvk_per_arch(precomp_cs)(cmdbuf);
      panlib_v9_draw_indirect_patch(&pctx, panlib_1d(1),
                                    (enum panlib_barrier)(PANLIB_BARRIER_JM_BARRIER |
                                    PANLIB_BARRIER_JM_SUPPRESS_PREFETCH),
                                    info->indirect.buffer_dev_addr, job.gpu,
                                    info->index.buffer_dev_addr, fv_ptr, bi_ptr,
                                    info->index.index_size,
                                    info->indirect.count_buffer_dev_addr,
                                    info->indirect.draw_index, vs_attribs,
                                    inst_mask, inst_info_gpu);
      indirect_dep = batch->vtc_jc.job_index;
   }
   static int draw_barrier = -1;
   if (draw_barrier < 0) {
      const char *e = getenv("PANVK_TESS_DRAWBARRIER");
      draw_barrier = e && e[0] == '1';
      if (draw_barrier)
         (void)0;
   }
   /* A gpu_prerast generated draw reads the records and indices the
    * producer jobs just wrote: wait for them. */
   const bool barrier = draw_barrier || info->gpu_prerast_generated;
   pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_MALLOC_VERTEX, barrier,
                  draw_barrier != 0, 0, indirect_dep, &job, false);
   return VK_SUCCESS;
}

/* gpu_prerast on v9 JM. The pre-raster stages the hardware cannot run (the
 * transform feedback capture, later GS and tessellation) run on the shared
 * gpu_prerast shader variants, as COMPUTE jobs in the batch's vertex/tiler
 * chain, and the records they leave in the device arena are drawn with the
 * passthrough VS through MALLOC_VERTEX. Same variants and arena layout as
 * csf/panvk_vX_cmd_draw.c (gpu_prerast_vs/_xfb/_draw_generated). JM has no
 * command stream: what CSF stores with cs_store32 the CPU writes into
 * per-draw memory, and job barriers order the jobs. Every prerast job is a
 * barrier, so the next draw's producer only starts once the previous
 * generated draw stopped reading the arena; vertex/tiler atoms are ordered
 * by the queue. After Noysz/panvk-g99-jm 0060 (JM gfx-compute helper).
 */
static void v9_cmd_draw_layer(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_info *info);

#define v9_prerast_unhandled(why)                                              \
   mesa_loge_once("panvk: v9 gpu_prerast cannot lower this draw (%s), "       \
                  "skipping it", why)

static bool
v9_prerast_xfb_active(const struct panvk_cmd_buffer *cmdbuf)
{
   const struct panvk_shader *last = cmdbuf->state.gfx.gs.shader
                                        ? cmdbuf->state.gfx.gs.shader
                                        : cmdbuf->state.gfx.vs.shader;

   return cmdbuf->state.gfx.xfb.active && last &&
          panvk_gpu_prerast_has_xfb(&last->gpu_prerast.xfb);
}

static bool
v9_prerast_needed(const struct panvk_cmd_buffer *cmdbuf,
                  const struct panvk_draw_info *info)
{
   return !info->gpu_prerast_generated &&
          (cmdbuf->state.gfx.gs.shader ||
           panvk_prerast_viewport_shader(&cmdbuf->state.gfx) ||
           v9_prerast_xfb_active(cmdbuf));
}

/* VS stores and atomics run once per vertex through gpu_prerast, like CSF
 * gpu_prerast_wanted(). Draws it cannot lower fall back to IDVS, which runs
 * them per position and varying shader invocation (allowed: the number of
 * VS invocations is implementation-defined). */
static bool
v9_prerast_side_effects(struct panvk_cmd_buffer *cmdbuf,
                        const struct panvk_draw_info *info)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;

   if (info->gpu_prerast_generated || !vs ||
       !vs->gpu_prerast.has_side_effects)
      return false;

   /* v9_prerast_draw() splits direct draws over the arena. */
   struct panvk_draw_info probe = *info;
   if (!probe.indirect.buffer_dev_addr)
      probe.vertex.count = probe.instance.count = 1;
   return panvk_per_arch(gpu_prerast_can_lower)(cmdbuf, &probe,
                                                &vs->gpu_prerast);
}

/* Raster VS of a draw: the GS passthrough after a GS, the VS passthrough for
 * generated draws and viewport-selecting VSs, else the HW variant. */
static const struct panvk_shader_variant *
v9_raster_vs_variant(const struct panvk_cmd_buffer *cmdbuf, bool generated)
{
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   if (gs)
      return &gs->variants[PANVK_GS_VARIANT_PASSTHROUGH];

   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   return generated || (vs && vs->gpu_prerast.vp_scissor_loc)
             ? panvk_shader_gpu_prerast_variant(
                  vs, PANVK_VS_VARIANT_GPU_PASSTHROUGH)
             : panvk_shader_hw_variant(vs);
}

/* One COMPUTE job of a gpu_prerast variant in the vertex/tiler chain, a
 * barrier: it waits for every earlier job of the batch. */
/* COMPUTE job of a gpu_prerast variant. It is added to the vertex/tiler
 * chain by v9_prerast_add_job() as a barrier: it waits for every earlier job
 * of the batch. Indirect jobs get their workgroup counts from
 * panlib_v9_prerast_indirect(), which must be added before them. */
static struct pan_ptr
v9_prerast_job(struct panvk_cmd_buffer *cmdbuf,
               const struct panvk_shader_variant *cs, uint64_t res_table,
               uint64_t push, uint32_t x, uint32_t y, uint32_t z,
               bool indirect)
{
   struct pan_compute_dim dim = {x, y, z};
   uint64_t tsd =
      panvk_per_arch(cmd_dispatch_prepare_tls)(cmdbuf, cs, &dim, indirect);
   struct pan_ptr job = panvk_cmd_alloc_desc(cmdbuf, COMPUTE_JOB);

   if (!tsd || !job.gpu)
      return (struct pan_ptr){0};

   pan_section_pack(job.cpu, COMPUTE_JOB, PAYLOAD, cfg) {
      cfg.workgroup_size_x = MAX2(cs->cs.local_size.x, 1);
      cfg.workgroup_size_y = MAX2(cs->cs.local_size.y, 1);
      cfg.workgroup_size_z = MAX2(cs->cs.local_size.z, 1);
      cfg.workgroup_count_x = x;
      cfg.workgroup_count_y = y;
      cfg.workgroup_count_z = z;
      cfg.task_increment = 1;
      cfg.task_axis = MALI_TASK_AXIS_Z;
      cfg.compute.resources = res_table;
      cfg.compute.shader = panvk_priv_mem_dev_addr(cs->spd);
      cfg.compute.thread_storage = tsd;
      cfg.compute.fau = push;
      cfg.compute.fau_count = cs->fau.total_count;
   }
   return job;
}

static void
v9_prerast_add_job(struct panvk_cmd_buffer *cmdbuf, struct pan_ptr *job)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;

   util_dynarray_append(&batch->jobs, job->cpu);
   pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, true, false, 0, 0,
                  job, false);
}

/* Fills the panvk_gpu_prerast_params of a direct draw, like the STORE_PARAM
 * list of CSF gpu_prerast_vs(). */
static void
v9_prerast_fill_params(const struct panvk_draw_info *src,
                       const struct panvk_shader *shader, uint64_t records,
                       uint64_t indices, struct panvk_gpu_prerast_params *p)
{
   memset(p, 0, sizeof(*p));
   p->vertex.index_buffer = src->index.buffer_dev_addr;
   p->vertex.index_size_B = src->index.index_size;
   p->vertex.index_buffer_range_el =
      src->index.index_size ? src->index.buffer_size / src->index.index_size
                            : 0;
   p->vertex.verts_per_instance = src->vertex.count;
   p->vertex.output_buffer = records;
   p->vertex.outputs = shader->gpu_prerast.io.outputs_written;
   /* JM draw infos carry the index offset in bytes, the params (like CSF
    * draw infos) in indices. */
   p->index_offset =
      src->index.index_size ? src->index.offset / src->index.index_size : 0;
   p->vertex_count = src->vertex.count;
   p->instance_count = src->instance.count;
   p->first_instance = src->instance.base;
   p->generated_indices = indices;
   p->restart_index = src->index.index_size == 1   ? UINT8_MAX
                      : src->index.index_size == 2 ? UINT16_MAX
                                                   : UINT32_MAX;
   p->primitive_restart = src->index.restart_enable;
   p->record_capacity =
      panvk_gpu_prerast_record_capacity(shader->gpu_prerast.io.record_stride);
   p->draw_id = src->indirect.draw_index;
}

/* VS (or TES) descriptors and resource table for the lowered variant. */
static VkResult
v9_prerast_prepare_vs_descs(struct panvk_cmd_buffer *cmdbuf,
                            const struct panvk_draw_info *info)
{
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   VkResult result = panvk_per_arch(cmd_prepare_push_descs)(
      cmdbuf, desc_state, vs->desc_info.used_set_mask);
   if (result != VK_SUCCESS)
      return result;

   result = v9_prepare_vs_driver_set(cmdbuf, info->instance.base);
   if (result != VK_SUCCESS)
      return result;

   return panvk_per_arch(cmd_prepare_shader_res_table)(
      cmdbuf, desc_state, &vs->desc_info, &cmdbuf->state.gfx.vs.desc, 1);
}

/* Lowered VS of a draw: one invocation per vertex and instance, records at
 * arena + PANVK_GPU_PRERAST_RECORD_OFFSET. Fills the per-draw params (the
 * counts of an indirect draw come from panlib_v9_prerast_indirect()) and
 * prepares the job, the caller adds it. */
struct v9_prerast_vs_job {
   uint64_t params;
   uint64_t push;
   struct pan_ptr job;
};

static bool
v9_prerast_vs(struct panvk_cmd_buffer *cmdbuf,
              const struct panvk_draw_info *src, uint64_t arena,
              struct v9_prerast_vs_job *out)
{
   const struct panvk_shader *shader = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader_variant *lowered =
      panvk_shader_gpu_prerast_variant(shader, PANVK_VS_VARIANT_GPU_LOWERED);
   const uint64_t records = arena + PANVK_GPU_PRERAST_RECORD_OFFSET;
   const uint64_t indices = arena + PANVK_GPU_PRERAST_INDEX_OFFSET;
   const bool indirect = src->indirect.buffer_dev_addr != 0;

   struct pan_ptr params = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct panvk_gpu_prerast_params), 64);
   if (!params.gpu)
      return false;
   v9_prerast_fill_params(src, shader, records, indices, params.cpu);

   if (v9_prerast_prepare_vs_descs(cmdbuf, src) != VK_SUCCESS)
      return false;

   panvk_per_arch(cmd_prepare_draw_sysvals)(
      cmdbuf, src, panvk_shader_only_variant(get_fs(cmdbuf)));
   cmdbuf->state.gfx.sysvals.gpu_prerast_params = params.gpu;
   cmdbuf->state.gfx.sysvals.gpu_prerast_records = records;

   struct pan_ptr push;
   if (panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, lowered, &push,
                                                     1) != VK_SUCCESS)
      return false;

   out->params = params.gpu;
   out->push = push.gpu;
   out->job = v9_prerast_job(cmdbuf, lowered,
                             cmdbuf->state.gfx.vs.desc.res_table, push.gpu,
                             indirect ? 1 : src->vertex.count,
                             indirect ? 1 : src->instance.count, 1, indirect);
   return out->job.gpu != 0;
}

/* GS kernel of a draw, CSF gpu_prerast_gs_run(): one invocation per input
 * vertex slot, instance and GS invocation, reading the lowered VS records and
 * appending strips to arena + PANVK_GPU_PRERAST_GS_RECORD_OFFSET/_INDEX_OFFSET.
 * The GS params and the generated draw command live in per-draw memory, not
 * the arena: the CPU fills the params, a fill job zeroes the draw command
 * the kernel appends to (so a resubmitted command buffer starts from 0). The
 * job is prepared here and added by the caller. After Noysz/panvk-g99-jm
 * 0063/0065. */
struct v9_prerast_gs_job {
   uint64_t params;
   uint64_t draw;
   struct pan_ptr job;
};

static bool
v9_prerast_gs(struct panvk_cmd_buffer *cmdbuf,
              const struct panvk_draw_info *src, uint64_t arena,
              struct v9_prerast_gs_job *out)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   const struct panvk_gpu_prerast_gs_info *gsi = &gs->gpu_prerast.gs;
   const struct panvk_shader_variant *kernel =
      &gs->variants[PANVK_GS_VARIANT_KERNEL];
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   struct panvk_shader_desc_state *gs_desc = &cmdbuf->state.gfx.gs.desc;
   const bool indirect = src->indirect.buffer_dev_addr != 0;

   if (panvk_per_arch(cmd_prepare_push_descs)(
          cmdbuf, desc_state, gs->desc_info.used_set_mask) != VK_SUCCESS ||
       v9_prepare_stage_driver_set(cmdbuf, gs, gs_desc) != VK_SUCCESS ||
       panvk_per_arch(cmd_prepare_shader_res_table)(
          cmdbuf, desc_state, &gs->desc_info, gs_desc, 1) != VK_SUCCESS)
      return false;

   struct pan_ptr params = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct panvk_gpu_prerast_gs_params), 64);
   struct pan_ptr draw = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(VkDrawIndexedIndirectCommand), 16);
   if (!params.gpu || !draw.gpu)
      return false;

   const struct panvk_pstats_query_state *pstats =
      &cmdbuf->state.gfx.pstats_query;
   struct panvk_gpu_prerast_gs_params *p = params.cpu;
   *p = (struct panvk_gpu_prerast_gs_params){
      .vs_records = arena + PANVK_GPU_PRERAST_RECORD_OFFSET,
      .vs_outputs = vs->gpu_prerast.io.outputs_written,
      .gen_indices = arena + PANVK_GPU_PRERAST_INDEX_OFFSET,
      .records = arena + PANVK_GPU_PRERAST_GS_RECORD_OFFSET,
      .indices = arena + PANVK_GPU_PRERAST_GS_INDEX_OFFSET,
      .draw = draw.gpu,
      /* panlib_v9_prerast_indirect() writes the counts of indirect draws. */
      .vertex_count = indirect ? 0 : src->vertex.count,
      .instance_count = indirect ? 0 : src->instance.count,
      .primitive_restart = src->index.restart_enable,
      .prim = src->prim,
      .cap_slots = panvk_gpu_prerast_gs_cap_slots(gsi),
      .vs_record_capacity =
         panvk_gpu_prerast_record_capacity(vs->gpu_prerast.io.record_stride),
      .provoking_last = cmdbuf->vk.dynamic_graphics_state.rs.provoking_vertex ==
                        VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT,
      .prim_id_base = src->gpu_prerast_prim_base,
      .raster_stream =
         cmdbuf->vk.dynamic_graphics_state.rs.rasterization_stream,
      .stats = {
         panvk_pstats_counter_addr(
            pstats, VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT,
            true),
         panvk_pstats_counter_addr(
            pstats, VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT,
            true),
         panvk_pstats_counter_addr(
            pstats, VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT, true),
         panvk_pstats_counter_addr(
            pstats, VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT, true),
      },
      .prims_generated = cmdbuf->state.gfx.prims_generated_query.ptr,
      .strips = arena + PANVK_GPU_PRERAST_GS_STRIP_OFFSET,
   };

   struct pan_ptr push;
   if (panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, kernel, &push,
                                                     1) != VK_SUCCESS)
      return false;
   if (shader_uses_sysval(kernel, graphics, gpu_prerast_params))
      *(uint64_t *)((uint8_t *)push.cpu +
                    shader_remapped_sysval_offset(
                       kernel, sysval_offset(graphics, gpu_prerast_params))) =
         params.gpu;

   out->params = params.gpu;
   out->draw = draw.gpu;
   out->job = v9_prerast_job(cmdbuf, kernel, gs_desc->res_table, push.gpu,
                             indirect ? 1 : src->vertex.count,
                             indirect ? 1 : src->instance.count,
                             gsi->invocations, indirect);
   return out->job.gpu != 0;
}

/* Jobs before the GS kernel: zero its draw command, and with primitive
 * restart build the strip table (panlib_gs_strip_table()) from the lowered
 * VS indices. */
static void
v9_prerast_gs_prologue(struct panvk_cmd_buffer *cmdbuf,
                       const struct panvk_draw_info *src, uint64_t arena,
                       const struct v9_prerast_gs_job *gs)
{
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);

   panlib_fill_scalar(&ctx, panlib_1d(sizeof(VkDrawIndexedIndirectCommand) / 4),
                      PANLIB_BARRIER_JM_BARRIER, gs->draw, 0);

   if (src->gpu_prerast_prim_base_addr)
      panlib_v9_copy32(
         &ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
         gs->params + offsetof(struct panvk_gpu_prerast_gs_params, prim_id_base),
         src->gpu_prerast_prim_base_addr);

   if (src->index.restart_enable)
      panlib_gs_strip_table(
         &ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
         arena + PANVK_GPU_PRERAST_INDEX_OFFSET,
         gs->params + offsetof(struct panvk_gpu_prerast_gs_params, vertex_count),
         PANVK_GPU_PRERAST_GS_STRIP_CAP, src->prim,
         arena + PANVK_GPU_PRERAST_GS_STRIP_OFFSET);
}

/* panlib_xfb_capture() over the records the lowered VS or GS left, CSF
 * gpu_prerast_xfb() without the conditional rendering part (v9 has no
 * VK_EXT_conditional_rendering). */
static void
v9_prerast_xfb(struct panvk_cmd_buffer *cmdbuf,
               const struct panvk_draw_info *src, uint64_t arena,
               uint64_t params)
{
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
      x->counts = params + offsetof(struct panvk_gpu_prerast_gs_params,
                                    vertex_count);
      x->record_stride = g->record_stride;
      x->record_capacity = panvk_gpu_prerast_gs_cap_slots(g);
      x->prim = g->output_prim;
      x->gs_max_vertices = g->max_vertices;
      x->gs_invocations = g->invocations;
   } else {
      x->records = arena + PANVK_GPU_PRERAST_RECORD_OFFSET;
      x->indices = arena + PANVK_GPU_PRERAST_INDEX_OFFSET;
      x->counts =
         params + offsetof(struct panvk_gpu_prerast_params, vertex_count);
      x->record_stride = last->gpu_prerast.io.record_stride;
      x->record_capacity = panvk_gpu_prerast_record_capacity(x->record_stride);
      x->prim = src->prim;
      x->restart = src->index.index_size && src->index.restart_enable;
   }
   x->active = 1;

   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_xfb_capture(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, ptr.gpu);
}

/* Draws the records of a lowering. VS only: the generated indices (vertex
 * IDs of instance 0, ~0 at restarts) with the passthrough VS, gen_draw is
 * the VkDrawIndexedIndirectCommand panlib_v9_prerast_indirect() wrote for an
 * indirect draw, 0 for a direct one. GS: the strips with the GS passthrough,
 * gen_draw is the GS kernel's draw command. */
static void
v9_prerast_draw_generated(struct panvk_cmd_buffer *cmdbuf,
                          const struct panvk_draw_info *src, uint64_t arena,
                          uint64_t gen_draw)
{
   struct panvk_draw_info generated = *src;

   generated.index.buffer_dev_addr = arena + PANVK_GPU_PRERAST_INDEX_OFFSET;
   generated.index.buffer_size =
      PANVK_GPU_PRERAST_MAX_INVOCATIONS * sizeof(uint32_t);
   generated.index.index_size = 4;
   generated.index.offset = 0;
   generated.vertex.base = 0;
   generated.indirect.buffer_dev_addr = gen_draw;
   generated.indirect.count_buffer_dev_addr = 0;
   generated.indirect.draw_index = 0;
   generated.gpu_prerast_generated = true;
   cmdbuf->state.gfx.sysvals.gpu_prerast_records =
      arena + PANVK_GPU_PRERAST_RECORD_OFFSET;

   /* The GS strips: one instance, restart-separated, the count the kernel
    * appended in gen_draw. */
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
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
      generated.vertex.count = 1;
      generated.instance.base = 0;
      generated.instance.count = 1;
      cmdbuf->state.gfx.sysvals.gpu_prerast_records =
         arena + PANVK_GPU_PRERAST_GS_RECORD_OFFSET;
   }
   gfx_state_set_dirty(cmdbuf, VS);
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   v9_cmd_draw_layer(cmdbuf, &generated);
   gfx_state_set_dirty(cmdbuf, VS);
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
}

/* panlib_v9_prerast_indirect() for an indirect draw: patches the params and
 * the lowered VS job, returns the generated draw command, 0 on failure. */
static uint64_t
v9_prerast_indirect(struct panvk_cmd_buffer *cmdbuf,
                    const struct panvk_draw_info *src,
                    const struct v9_prerast_vs_job *vs,
                    const struct v9_prerast_gs_job *gs, uint64_t chunk)
{
   const struct panvk_shader *shader = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader_variant *lowered =
      panvk_shader_gpu_prerast_variant(shader, PANVK_VS_VARIANT_GPU_LOWERED);
   struct pan_ptr args = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct panlib_v9_prerast_patch), 16);
   struct pan_ptr gen = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(VkDrawIndexedIndirectCommand), 16);
   uint32_t inst_mask;
   uint64_t inst_info, vs_attribs;

   if (!args.gpu || !gen.gpu ||
       !v9_inst_attr_info(cmdbuf, &inst_mask, &inst_info, &vs_attribs))
      return 0;

   static_assert(offsetof(struct panvk_gpu_prerast_params, instance_count) ==
                    offsetof(struct panvk_gpu_prerast_params, vertex_count) + 4,
                 "panlib_v9_prerast_indirect counts layout");
   static_assert(offsetof(struct panvk_gpu_prerast_params, first_instance) ==
                    offsetof(struct panvk_gpu_prerast_params, vertex_count) + 8,
                 "panlib_v9_prerast_indirect counts layout");
   static_assert(offsetof(struct panvk_gpu_prerast_params, index_offset) ==
                    offsetof(struct panvk_gpu_prerast_params, vertex_count) + 20,
                 "panlib_v9_prerast_indirect counts layout");
   static_assert(offsetof(struct panvk_gpu_prerast_params, chunk_vertex) ==
                    offsetof(struct panvk_gpu_prerast_params, vertex_count) + 32,
                 "panlib_v9_prerast_indirect counts layout");
   static_assert(offsetof(struct panvk_gpu_prerast_params, chunk_instance) ==
                    offsetof(struct panvk_gpu_prerast_params, vertex_count) + 36,
                 "panlib_v9_prerast_indirect counts layout");

   struct panlib_v9_prerast_patch *a = args.cpu;
   *a = (struct panlib_v9_prerast_patch){
      .cmd = src->indirect.buffer_dev_addr,
      .draw_count = src->indirect.count_buffer_dev_addr,
      .counts =
         vs->params + offsetof(struct panvk_gpu_prerast_params, vertex_count),
      .verts_per_instance =
         vs->params + offsetof(struct panvk_gpu_prerast_params,
                               vertex.verts_per_instance),
      .vs_job = vs->job.gpu,
      .vs_attribs = vs_attribs,
      .inst_attr_info = inst_info,
      .gen_draw = gen.gpu,
      .chunk = chunk,
      .indexed = src->index.index_size != 0,
      .draw_index = src->indirect.draw_index,
      .capacity = panvk_gpu_prerast_record_capacity(
         shader->gpu_prerast.io.record_stride),
      .inst_attr_mask = inst_mask,
   };
   if (gs) {
      const struct panvk_gpu_prerast_gs_info *gsi =
         &cmdbuf->state.gfx.gs.shader->gpu_prerast.gs;

      a->gs_job = gs->job.gpu;
      a->gs_counts =
         gs->params + offsetof(struct panvk_gpu_prerast_gs_params, vertex_count);
      a->gs_invocations = gsi->invocations;
      /* The GS slots bound the draw too, see gpu_prerast_can_lower(). */
      a->capacity = MIN2(a->capacity,
                         panvk_gpu_prerast_gs_cap_slots(gsi) / gsi->invocations);
   }
   if (shader_uses_sysval(lowered, graphics, vs.first_vertex))
      a->first_vertex_sysval =
         vs->push + shader_remapped_sysval_offset(
                       lowered, sysval_offset(graphics, vs.first_vertex));
   if (shader_uses_sysval(lowered, graphics, vs.base_instance))
      a->base_instance_sysval =
         vs->push + shader_remapped_sysval_offset(
                       lowered, sysval_offset(graphics, vs.base_instance));

   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_v9_prerast_indirect(&ctx, panlib_1d(1),
                              (enum panlib_barrier)(PANLIB_BARRIER_JM_BARRIER |
                              PANLIB_BARRIER_JM_SUPPRESS_PREFETCH),
                              args.gpu);
   return gen.gpu;
}

/* Multi-viewport form of v9_prerast_draw_generated(): panlib_vp_runs() sorts
 * the primitives of the records into viewport runs, as on CSF
 * (gpu_prerast_vp_prepare()), panlib_v9_vp_partition() packs them per
 * viewport, and each viewport gets one draw clamped to its depth range. One
 * draw when every viewport has the same range. counts is the VS params
 * {vertex count, instance count} or the GS draw command. */
static void
v9_prerast_draw_viewports(struct panvk_cmd_buffer *cmdbuf,
                          const struct panvk_draw_info *src, uint64_t arena,
                          uint64_t counts)
{
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   const struct panvk_shader *vps =
      panvk_prerast_viewport_shader(&cmdbuf->state.gfx);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   const uint32_t vp_count =
      MIN2(cmdbuf->vk.dynamic_graphics_state.vp.viewport_count,
           PANLIB_VP_MAX_VIEWPORTS);
   uint64_t in_indices, records, outputs;
   uint32_t record_stride, prim, index_count = 0, out_count = 0;
   uint32_t run_capacity = 0;
   bool count_ok, per_instance;

   if (gs) {
      const struct panvk_gpu_prerast_gs_info *gsi = &gs->gpu_prerast.gs;
      const uint32_t slots = panvk_gpu_prerast_gs_cap_slots(gsi);

      count_ok = slots && gsi->index_stride &&
                 !__builtin_mul_overflow(slots, gsi->index_stride,
                                         &index_count);
      /* A strip expands to 3 indices per triangle. */
      out_count = gsi->output_prim == MESA_PRIM_TRIANGLE_STRIP &&
                        index_count > 2
                     ? (index_count - 2) * 3
                     : index_count;
      run_capacity = count_ok ? MIN2(out_count, PANLIB_VP_MAX_RUNS) : 0;
      in_indices = arena + PANVK_GPU_PRERAST_GS_INDEX_OFFSET;
      records = arena + PANVK_GPU_PRERAST_GS_RECORD_OFFSET;
      outputs = gsi->outputs_written;
      record_stride = gsi->record_stride;
      prim = gsi->output_prim;
      per_instance = false;
   } else {
      const struct panvk_gpu_prerast_io *io = &vps->gpu_prerast.io;

      index_count = panvk_gpu_prerast_record_capacity(io->record_stride);
      uint64_t total = index_count;
      if (!src->indirect.buffer_dev_addr)
         total = MIN2(total,
                      (uint64_t)src->vertex.count * src->instance.count);
      const uint32_t mult =
         src->prim == MESA_PRIM_LINE_STRIP                 ? 2
         : (src->prim == MESA_PRIM_TRIANGLE_STRIP ||
            src->prim == MESA_PRIM_TRIANGLE_FAN)           ? 3
                                                           : 1;
      const uint32_t vpp = src->prim == MESA_PRIM_POINTS ? 1
                           : (src->prim == MESA_PRIM_LINES ||
                              src->prim == MESA_PRIM_LINE_STRIP)
                              ? 2
                              : 3;
      count_ok = index_count && total && total * mult <= UINT32_MAX;
      out_count = count_ok ? total * mult : 0;
      run_capacity =
         count_ok ? MIN2(DIV_ROUND_UP(out_count, vpp), PANLIB_VP_MAX_RUNS) : 0;
      in_indices = arena + PANVK_GPU_PRERAST_INDEX_OFFSET;
      records = arena + PANVK_GPU_PRERAST_RECORD_OFFSET;
      outputs = io->outputs_written;
      record_stride = io->record_stride;
      prim = src->prim;
      per_instance = true;
   }

   uint64_t run_bytes = 0, index_bytes = 0;
   if (!count_ok || index_count > PANLIB_VP_MAX_INPUT_INDICES ||
       out_count > PANLIB_VP_MAX_OUT_INDICES ||
       !panlib_vp_run_bytes(run_capacity, &run_bytes) ||
       !panvk_gpu_prerast_checked_size(out_count, sizeof(uint32_t), 4,
                                       &index_bytes)) {
      mesa_loge_once("panvk: viewport run list does not fit, draw discarded");
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

   /* One draw when no viewport's depth clamp differs from viewport 0's. */
   float z0_min, z0_max;
   bool merged = true;
   v9_viewport_depth_range(cmdbuf, 0, &z0_min, &z0_max);
   for (uint32_t v = 1; v < vp_count; v++) {
      float lo, hi;
      v9_viewport_depth_range(cmdbuf, v, &lo, &hi);
      merged &= lo == z0_min && hi == z0_max;
   }

   struct pan_ptr runs = panvk_cmd_alloc_dev_mem(cmdbuf, desc, run_bytes, 16);
   struct pan_ptr out = panvk_cmd_alloc_dev_mem(cmdbuf, desc, index_bytes, 16);
   struct pan_ptr part = {0};
   if (!merged)
      part = panvk_cmd_alloc_dev_mem(cmdbuf, desc, index_bytes, 16);
   struct pan_ptr draws = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, PANLIB_VP_MAX_VIEWPORTS * 5 * sizeof(uint32_t), 16);
   if (!runs.gpu || !out.gpu || (!merged && !part.gpu) || !draws.gpu) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

   const struct panlib_vp_runs_launch_args args = {
      .in_indices = in_indices,
      .records = records,
      .out_indices = out.gpu,
      .header = runs.gpu,
      .outputs_written = outputs,
      .counts = counts,
      .record_stride = record_stride,
      .viewport_count = vp_count,
      .prim = prim,
      .provoking_last = cmdbuf->vk.dynamic_graphics_state.rs.provoking_vertex ==
                        VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT,
      .index_bound = index_count,
      .index_capacity = out_count,
      .run_capacity = run_capacity,
      .per_instance = per_instance,
      /* A VS has no PrimitiveID output (a TES has one): give the FS one that
       * does not restart with each draw. */
      .pack_prim_id = per_instance && fs && fs->info.fs.reads_primitive_id &&
                      !(outputs & VARYING_BIT_PRIMITIVE_ID),
      .prim_base = src->gpu_prerast_prim_base,
   };
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_vp_runs_launch_struct(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
                                args);
   panlib_v9_vp_partition(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
                          runs.gpu, out.gpu, merged ? 0 : part.gpu, draws.gpu,
                          vp_count, merged);

   struct panvk_draw_info generated = *src;
   generated.prim = prim == MESA_PRIM_POINTS ? MESA_PRIM_POINTS
                    : (prim == MESA_PRIM_LINE_STRIP || prim == MESA_PRIM_LINES)
                       ? MESA_PRIM_LINES
                       : MESA_PRIM_TRIANGLES;
   generated.index.buffer_dev_addr = merged ? out.gpu : part.gpu;
   generated.index.buffer_size = index_bytes;
   generated.index.index_size = 4;
   generated.index.offset = 0;
   generated.index.restart_enable = false;
   generated.vertex.base = 0;
   generated.vertex.count = 1;
   generated.instance.base = 0;
   generated.instance.count = 1;
   generated.indirect.count_buffer_dev_addr = 0;
   generated.indirect.draw_index = 0;
   generated.gpu_prerast_generated = true;

   for (uint32_t v = 0; v < (merged ? 1 : vp_count); v++) {
      cmdbuf->state.gfx.sysvals.gpu_prerast_records = records;
      generated.indirect.buffer_dev_addr =
         draws.gpu + v * 5 * sizeof(uint32_t);
      generated.viewport_clamp = merged ? 0 : v + 1;
      gfx_state_set_dirty(cmdbuf, VS);
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
      v9_cmd_draw_layer(cmdbuf, &generated);
   }
   gfx_state_set_dirty(cmdbuf, VS);
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
}

/* Lowers one draw that fits the arena (direct) or is dropped when it does
 * not (indirect, see panlib_v9_prerast_indirect()). */
static void
v9_prerast_draw_one(struct panvk_cmd_buffer *cmdbuf,
                    const struct panvk_draw_info *src)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct panvk_shader *shader = cmdbuf->state.gfx.vs.shader;
   const uint64_t arena = panvk_priv_mem_dev_addr(dev->gpu_prerast_arena);

   if (!arena ||
       !panvk_per_arch(gpu_prerast_can_lower)(cmdbuf, src,
                                              &shader->gpu_prerast)) {
      v9_prerast_unhandled("shader variants or arena missing");
      return;
   }

   if (!cmdbuf->cur_batch)
      panvk_per_arch(cmd_open_batch)(cmdbuf);

   /* Patch, producer, capture, draw patch and draw jobs. */
   if (cmdbuf->cur_batch->vtc_jc.job_index + 16 >= UINT16_MAX) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }

   if (panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf, true) != VK_SUCCESS)
      return;

   const bool has_gs = cmdbuf->state.gfx.gs.shader != NULL;
   struct v9_prerast_vs_job vs;
   struct v9_prerast_gs_job gs;
   uint64_t gen_draw = 0;
   if (!v9_prerast_vs(cmdbuf, src, arena, &vs) ||
       (has_gs && !v9_prerast_gs(cmdbuf, src, arena, &gs)) ||
       (src->indirect.buffer_dev_addr &&
        !(gen_draw = v9_prerast_indirect(cmdbuf, src, &vs,
                                         has_gs ? &gs : NULL, 0)))) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }
   v9_prerast_add_job(cmdbuf, &vs.job);
   if (has_gs) {
      v9_prerast_gs_prologue(cmdbuf, src, arena, &gs);
      v9_prerast_add_job(cmdbuf, &gs.job);
      gen_draw = gs.draw;
   }

   /* Captured once per draw: multiview and transform feedback are
    * exclusive, a layered draw captures layer 0. */
   if (src->layer_id == 0 && v9_prerast_xfb_active(cmdbuf))
      v9_prerast_xfb(cmdbuf, src, arena, has_gs ? gs.params : vs.params);

   /* Capture-only draws (rasterizer discard) have nothing to tile. */
   if (cmdbuf->vk.dynamic_graphics_state.rs.rasterizer_discard_enable)
      return;

   if (v9_multi_viewport(cmdbuf))
      v9_prerast_draw_viewports(
         cmdbuf, src, arena,
         has_gs ? gs.draw
                : vs.params + offsetof(struct panvk_gpu_prerast_params,
                                       vertex_count));
   else
      v9_prerast_draw_generated(cmdbuf, src, arena, gen_draw);
}

/* Fans over the arena: chunks of hub + window vertices, like CSF
 * gpu_prerast_split_fan(). */
static void
v9_prerast_split_fan(struct panvk_cmd_buffer *cmdbuf,
                     const struct panvk_draw_info *draw, uint32_t max_verts)
{
   const uint32_t isz = draw->index.index_size;
   /* First index in elements, the JM offset is in bytes. */
   const uint32_t first = isz ? draw->index.offset / isz : 0;
   const uint32_t win = max_verts - 1;
   struct panvk_draw_info chunk = *draw;

   chunk.index.index_size = 4;
   chunk.index.offset = 0;
   chunk.index.restart_enable = false;
   chunk.instance.count = 1;

   for (uint32_t inst = 0; inst < draw->instance.count; inst++) {
      chunk.instance.base = draw->instance.base + inst;
      for (uint32_t s = 1; s + 1 < draw->vertex.count; s += win - 1) {
         const uint32_t n = MIN2(win, draw->vertex.count - s);
         struct pan_ptr ib = panvk_cmd_alloc_dev_mem(
            cmdbuf, desc, (n + 1) * sizeof(uint32_t), 16);
         if (!ib.gpu) {
            vk_command_buffer_set_error(&cmdbuf->vk,
                                        VK_ERROR_OUT_OF_DEVICE_MEMORY);
            return;
         }

         /* The lowered VS job of the chunk is a barrier, it waits for this
          * one. */
         struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
         panlib_fan_chunk_indices(
            &ctx, panlib_1d(DIV_ROUND_UP(n + 1, 64)), PANLIB_BARRIER_NONE,
            ib.gpu, draw->index.buffer_dev_addr, isz,
            isz ? draw->index.buffer_size / isz : 0,
            first, first + s, n + 1);

         chunk.index.buffer_dev_addr = ib.gpu;
         chunk.index.buffer_size = (n + 1) * sizeof(uint32_t);
         chunk.vertex.count = n + 1;
         chunk.gpu_prerast_prim_base = s - 1;
         v9_prerast_draw_one(cmdbuf, &chunk);
      }
   }
}

/* Direct draws the arena does not hold are split on the CPU, the CPU half of
 * CSF gpu_prerast_split(). The GPU chunk loop has no JM equivalent: strips
 * with primitive restart over the arena are not lowered, indirect draws over
 * it are dropped (panlib_v9_prerast_indirect()).
 */
static void
v9_prerast_draw(struct panvk_cmd_buffer *cmdbuf,
                const struct panvk_draw_info *draw)
{
   /* v9_draw_indirect_unrolled() hands over one draw at a time. */
   if (draw->indirect.buffer_dev_addr) {
      v9_prerast_draw_one(cmdbuf, draw);
      return;
   }


   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   uint32_t max_verts =
      panvk_gpu_prerast_record_capacity(vs->gpu_prerast.io.record_stride);
   if (gs)
      max_verts = MIN2(max_verts,
                       panvk_gpu_prerast_gs_cap_slots(&gs->gpu_prerast.gs) /
                          gs->gpu_prerast.gs.invocations);

   if ((uint64_t)draw->vertex.count * draw->instance.count <= max_verts) {
      v9_prerast_draw_one(cmdbuf, draw);
      return;
   }

   struct panvk_draw_info chunk = *draw;
   if (draw->vertex.count <= max_verts) {
      const uint32_t step = max_verts / draw->vertex.count;
      for (uint32_t i = 0; i < draw->instance.count; i += step) {
         chunk.instance.base = draw->instance.base + i;
         chunk.instance.count = MIN2(step, draw->instance.count - i);
         v9_prerast_draw_one(cmdbuf, &chunk);
      }
      return;
   }

   if (draw->prim == MESA_PRIM_TRIANGLE_FAN &&
       !(draw->index.index_size && draw->index.restart_enable) &&
       max_verts >= 3) {
      v9_prerast_split_fan(cmdbuf, draw, max_verts);
      return;
   }

   uint32_t align, overlap, prim_div;
   panlib_chunk_topology(draw->prim, &align, &overlap, &prim_div);
   const uint32_t step =
      max_verts > overlap ? (max_verts - overlap) / align * align : 0;
   if (!step || draw->prim == MESA_PRIM_TRIANGLE_FAN ||
       draw->prim == MESA_PRIM_PATCHES ||
       (overlap && draw->index.index_size && draw->index.restart_enable)) {
      v9_prerast_unhandled("vertex count over the arena capacity");
      return;
   }

   chunk.instance.count = 1;
   for (uint32_t inst = 0; inst < draw->instance.count; inst++) {
      chunk.instance.base = draw->instance.base + inst;
      for (uint32_t i = 0; i + overlap < draw->vertex.count; i += step) {
         chunk.vertex.count = MIN2(step + overlap, draw->vertex.count - i);
         if (draw->index.index_size)
            chunk.index.offset = draw->index.offset + i * draw->index.index_size;
         else
            chunk.vertex.base = draw->vertex.base + i;
         chunk.gpu_prerast_prim_base = i / prim_div;
         v9_prerast_draw_one(cmdbuf, &chunk);
      }
   }
}

/* Room for the next jobs in the 16-bit job index space of the batch, and a
 * TLS descriptor for them. */
static bool
v9_prerast_reserve_jobs(struct panvk_cmd_buffer *cmdbuf, uint32_t jobs)
{
   if (!cmdbuf->cur_batch)
      panvk_per_arch(cmd_open_batch)(cmdbuf);

   if (cmdbuf->cur_batch->vtc_jc.job_index + jobs >= UINT16_MAX) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }

   return panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf, true) == VK_SUCCESS;
}

/* Tessellation on v9 JM. CSF gpu_prerast_tess() runs the chunks of a draw
 * as a command stream loop until the kernels clear "more". JM has no loop:
 * the iterations are unrolled into the job chain, each one the jobs of a CSF
 * iteration, every job a barrier. An iteration past the end of the draw finds
 * nothing left: panlib_tess_step() zeroes the VS and TCS grids, whose patch
 * jobs then make them NULL jobs, the tessellator kernels return at once and
 * panlib_tess_draw() writes an empty draw that the draw patch makes a NULL
 * job. No CPU reads anything back.
 *
 * Direct draws get exactly the iterations the chunk plan of
 * panlib_tess_step() needs. Indirect draws get V9_TESS_INDIRECT_CHUNKS chunks
 * (each up to max_patches patches); patches past them are not drawn.
 * ponytail: fixed bound, a GPU-sized chain of job slots would lift it.
 *
 * When the TES output goes through gpu_prerast again (GS, transform
 * feedback, viewport select, TES stores), an iteration lowers at most one
 * arena of tessellator indices. Chunks are cut so that one at the maximum
 * tessellation level still fits, with more iterations per chunk when a single
 * patch does not. After Noysz/panvk-g99-jm 0062/0065 (libpoly kernels in the
 * vertex/tiler chain), on the shared chunked kernels of csf-v11.
 */
#define V9_TESS_INDIRECT_CHUNKS 8
#define V9_TESS_MAX_ITERATIONS 4096
/* Jobs of one iteration, including a nested GS lowering and its draw. */
#define V9_TESS_ITERATION_JOBS 32

static void
v9_tess_draw(struct panvk_cmd_buffer *cmdbuf,
             const struct panvk_draw_info *draw)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *tcs = cmdbuf->state.gfx.tcs.shader;
   const struct panvk_shader *tes = cmdbuf->state.gfx.tes.shader;
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   const uint64_t arena = panvk_priv_mem_dev_addr(dev->gpu_prerast_tess_arena);

   if (!vs || !tcs || !tes || !tcs->gpu_prerast.tess.compiled ||
       !vs->gpu_prerast.lowered_vs_compiled || !arena) {
      v9_prerast_unhandled("tessellation shaders missing");
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
   uint32_t max_patches = panvk_gpu_prerast_tess_max_patches(
      tc->stride_el * 4, vs->gpu_prerast.io.record_stride, ps);

   if (domain == PANVK_GPU_PRERAST_TESS_UNSET ||
       spacing == PANVK_GPU_PRERAST_TESS_UNSET || !max_patches) {
      v9_prerast_unhandled("tessellation state");
      return;
   }

   const bool indirect = draw->indirect.buffer_dev_addr != 0;
   const bool indexed = draw->index.index_size != 0;
   if (!indirect && (draw->vertex.count < ps || !draw->instance.count))
      return;

   /* The draw of the tessellator output, one index range per iteration. */
   struct panvk_draw_info out = *draw;
   out.prim = points        ? MESA_PRIM_POINTS
              : domain == 2 ? MESA_PRIM_LINES
                            : MESA_PRIM_TRIANGLES;
   out.index.index_size = 4;
   out.index.offset = 0;
   out.index.restart_enable = false;
   out.vertex.base = 0;
   out.vertex.count = 1;
   out.instance.base = 0;
   out.instance.count = 1;
   out.indirect.count_buffer_dev_addr = 0;
   out.indirect.draw_count = 1;
   out.indirect.draw_index = 0;
   out.pstats_counted = true;
   out.prims_generated_counted = true;
   out.gpu_prerast_prim_base = 0;
   out.gpu_prerast_prim_base_addr = 0;

   const uint32_t vpp = mesa_vertices_per_prim(out.prim);

   /* What v9_cmd_draw_layer() does with the TES output. */
   cmdbuf->state.gfx.vs.shader = tes;
   const bool nested = v9_prerast_needed(cmdbuf, &out) ||
                       v9_prerast_side_effects(cmdbuf, &out);
   cmdbuf->state.gfx.vs.shader = vs;

   uint32_t range = 0, iters_per_chunk = 1;
   if (nested) {
      /* Same vertex budget as v9_prerast_draw(). */
      uint32_t verts =
         panvk_gpu_prerast_record_capacity(tes->gpu_prerast.io.record_stride);
      if (gs)
         verts = MIN2(verts, panvk_gpu_prerast_gs_cap_slots(&gs->gpu_prerast.gs) /
                                MAX2(gs->gpu_prerast.gs.invocations, 1));
      range = verts / vpp * vpp;
      if (!range) {
         v9_prerast_unhandled("tessellation output range");
         return;
      }
      max_patches = MIN2(max_patches,
                         MAX2(range / PANVK_GPU_PRERAST_TESS_MAX_INDICES, 1));
      iters_per_chunk = DIV_ROUND_UP(
         (uint64_t)max_patches * PANVK_GPU_PRERAST_TESS_MAX_INDICES, range);
   }

   /* The chunk plan of panlib_tess_step(). */
   uint64_t chunks = V9_TESS_INDIRECT_CHUNKS;
   if (!indirect) {
      const uint32_t ppi = draw->vertex.count / ps;

      chunks = ppi <= max_patches
                  ? DIV_ROUND_UP((uint64_t)draw->instance.count,
                                 MIN2(max_patches / ppi,
                                      PANLIB_CHUNK_MAX_INSTANCES))
                  : (uint64_t)draw->instance.count *
                       DIV_ROUND_UP((uint64_t)ppi, max_patches);
   }
   uint64_t iters = chunks * iters_per_chunk;
   if (iters > V9_TESS_MAX_ITERATIONS) {
      mesa_loge_once("panvk: v9 tessellated draw needs %" PRIu64
                     " iterations, only %u are drawn",
                     iters, V9_TESS_MAX_ITERATIONS);
      iters = V9_TESS_MAX_ITERATIONS;
   }

   struct panvk_gpu_prerast_tess_layout l;
   panvk_gpu_prerast_tess_layout(arena, max_patches, tc->stride_el * 4, &l);

   struct pan_ptr td = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct panlib_tess_draw), 16);
   /* Direct draws have no app buffer to read the counts from. */
   struct pan_ptr direct_draw = {0};
   if (!indirect)
      direct_draw =
         panvk_cmd_alloc_dev_mem(cmdbuf, desc, 5 * sizeof(uint32_t), 4);
   if (!td.gpu || (!indirect && !direct_draw.gpu)) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

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

   uint64_t src = draw->indirect.buffer_dev_addr;
   if (!indirect) {
      uint32_t *rec = direct_draw.cpu;

      rec[0] = draw->vertex.count;
      rec[1] = draw->instance.count;
      if (indexed) {
         /* JM draw infos carry the index offset in bytes. */
         rec[2] = draw->index.offset / draw->index.index_size;
         rec[3] = draw->vertex.base;
         rec[4] = draw->instance.base;
      } else {
         rec[2] = draw->vertex.base;
         rec[3] = draw->instance.base;
         rec[4] = 0;
      }
      src = direct_draw.gpu;
   }

   if (!v9_prerast_reserve_jobs(cmdbuf, V9_TESS_ITERATION_JOBS))
      return;

   /* TCS descriptors, shared by every iteration. */
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   struct panvk_shader_desc_state *tcs_desc = &cmdbuf->state.gfx.tcs.desc;
   const struct panvk_shader_variant *tcs_v = panvk_shader_only_variant(tcs);
   if (panvk_per_arch(cmd_prepare_push_descs)(
          cmdbuf, desc_state, tcs->desc_info.used_set_mask) != VK_SUCCESS ||
       v9_prepare_stage_driver_set(cmdbuf, tcs, tcs_desc) != VK_SUCCESS ||
       panvk_per_arch(cmd_prepare_shader_res_table)(
          cmdbuf, desc_state, &tcs->desc_info, tcs_desc, 1) != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

   cmdbuf->state.gfx.sysvals.gpu_prerast_tess = TD(p);
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_tess_setup(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, td.gpu,
                     src, draw->indirect.count_buffer_dev_addr,
                     indirect ? draw->indirect.draw_index : 0);

   /* The part of the API draw the VS runs over, see panlib_tess_step(). */
   struct panvk_draw_info vs_src = *draw;
   vs_src.indirect.buffer_dev_addr = TD(vs_draw);
   vs_src.indirect.count_buffer_dev_addr = 0;
   vs_src.indirect.draw_count = 1;
   vs_src.instance.base = 0;

   out.index.buffer_dev_addr = l.index;
   out.index.buffer_size = l.index_size;
   out.indirect.buffer_dev_addr = TD(draw);
   /* The GS primitive ID continues over the iterations of a chunk. */
   if (gs)
      out.gpu_prerast_prim_base_addr = TD(gs_prim_base);

   const enum panlib_barrier patch_barrier =
      (enum panlib_barrier)(PANLIB_BARRIER_JM_BARRIER |
                            PANLIB_BARRIER_JM_SUPPRESS_PREFETCH);
   const struct panlib_precomp_grid tess_grid =
      panlib_1d(DIV_ROUND_UP(max_patches, 64));
   const uint64_t prims_generated =
      gs ? 0 : cmdbuf->state.gfx.prims_generated_query.ptr;
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

   bool ok = true;
   for (uint32_t it = 0; ok && it < iters; it++) {
      ok = v9_prerast_reserve_jobs(cmdbuf, V9_TESS_ITERATION_JOBS);
      if (!ok)
         break;

      panlib_tess_step(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, td.gpu,
                       tcs_stat);

      /* VS over the chunk, sized by its patch job. */
      struct v9_prerast_vs_job vsj;
      ok = v9_prerast_vs(cmdbuf, &vs_src, arena, &vsj) &&
           v9_prerast_indirect(cmdbuf, &vs_src, &vsj, NULL, TD(chunk_vertex));
      if (!ok)
         break;
      v9_prerast_add_job(cmdbuf, &vsj.job);

      /* TCS, one workgroup per patch of the chunk. */
      struct pan_ptr push;
      ok = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, tcs_v, &push,
                                                         1) == VK_SUCCESS;
      if (!ok)
         break;
      if (shader_uses_sysval(tcs_v, graphics, gpu_prerast_params))
         *(uint64_t *)((uint8_t *)push.cpu +
                       shader_remapped_sysval_offset(
                          tcs_v, sysval_offset(graphics, gpu_prerast_params))) =
            vsj.params;
      if (shader_uses_sysval(tcs_v, graphics, gpu_prerast_tess))
         *(uint64_t *)((uint8_t *)push.cpu +
                       shader_remapped_sysval_offset(
                          tcs_v, sysval_offset(graphics, gpu_prerast_tess))) =
            TD(p);
      struct pan_ptr tcs_job = v9_prerast_job(
         cmdbuf, tcs_v, tcs_desc->res_table, push.gpu, max_patches, 1, 1, true);
      ok = tcs_job.gpu != 0;
      if (!ok)
         break;
      panlib_v9_compute_job_grid(&ctx, panlib_1d(1), patch_barrier,
                                 tcs_job.gpu, TD(tcs_grid));
      v9_prerast_add_job(cmdbuf, &tcs_job);

      panlib_tessellate(&ctx, tess_grid, PANLIB_BARRIER_JM_BARRIER, TD(p),
                        domain, POLY_TESS_MODE_COUNT);
      panlib_tess_draw(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, td.gpu,
                       c_invs, c_prims, prims_generated);
      panlib_tessellate(&ctx, tess_grid, PANLIB_BARRIER_JM_BARRIER, TD(p),
                        domain, POLY_TESS_MODE_WITH_COUNTS);

      /* This iteration's index range with the TES as the VS: a MALLOC_VERTEX
       * draw, or another gpu_prerast lowering when nested. */
      struct panvk_draw_info raster = out;
      cmdbuf->state.gfx.vs.shader = tes;
      gfx_state_set_dirty(cmdbuf, VS);
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
      v9_cmd_draw_layer(cmdbuf, &raster);
      cmdbuf->state.gfx.vs.shader = vs;
      gfx_state_set_dirty(cmdbuf, VS);
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   }
#undef TD

   if (!ok)
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
}

static void
v9_cmd_draw_layer(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_info *info)
{
   if (info->prim == MESA_PRIM_PATCHES) {
      v9_tess_draw(cmdbuf, info);
      return;
   }

   if (v9_prerast_needed(cmdbuf, info) ||
       v9_prerast_side_effects(cmdbuf, info)) {
      v9_prerast_draw(cmdbuf, info);
      return;
   }

   const struct panvk_shader_variant *vs =
      v9_raster_vs_variant(cmdbuf, info->gpu_prerast_generated);
   if (!vs || !panvk_priv_mem_check_alloc(vs->spds.pos_triangles))
      return;


   cmdbuf->state.gfx.fs.required =
      fs_required(&cmdbuf->state.gfx, &cmdbuf->vk.dynamic_graphics_state);
   const struct panvk_shader_variant *fs = panvk_shader_only_variant(get_fs(cmdbuf));

   if (!cmdbuf->cur_batch)
      panvk_per_arch(cmd_open_batch)(cmdbuf);

   /* There are only 16 bits in the job header for the job index, and a draw
    * takes up to two jobs (indirect patch + MALLOC_VERTEX). */
   if (cmdbuf->cur_batch->vtc_jc.job_index + 4 >= UINT16_MAX) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }

   if (cmdbuf->state.gfx.render.first_provoking_vertex == U_TRISTATE_UNSET) {
      cmdbuf->state.gfx.render.first_provoking_vertex = u_tristate_make(
         cmdbuf->vk.dynamic_graphics_state.rs.provoking_vertex ==
         VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT);
      if (cmdbuf->state.gfx.render.first_provoking_vertex == U_TRISTATE_UNSET)
         cmdbuf->state.gfx.render.first_provoking_vertex = U_TRISTATE_YES;
   }

   const struct vk_rasterization_state *rs = &cmdbuf->vk.dynamic_graphics_state.rs;
   VkResult result;
   if (!rs->rasterizer_discard_enable) {
      result = panvk_per_arch(cmd_alloc_fb_desc)(cmdbuf);
      if (result != VK_SUCCESS)
         return;
   }
   panvk_per_arch(cmd_select_tile_size)(cmdbuf);
   result = panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf, true);
   if (result != VK_SUCCESS)
      return;
   result = panvk_per_arch(cmd_prepare_tiler_context)(cmdbuf, info->layer_id);
   if (result != VK_SUCCESS)
      return;

   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   const struct panvk_shader_desc_info *vs_info = &cmdbuf->state.gfx.vs.shader->desc_info;
   uint32_t used = vs_info->used_set_mask;
   if (fs)
      used |= cmdbuf->state.gfx.fs.shader->desc_info.used_set_mask;
   result = panvk_per_arch(cmd_prepare_push_descs)(cmdbuf, desc_state, used);
   if (result != VK_SUCCESS)
      return;
   result = v9_prepare_vs_driver_set(cmdbuf, info->instance.base);
   if (result != VK_SUCCESS)
      return;
   result = panvk_per_arch(cmd_prepare_shader_res_table)(
      cmdbuf, desc_state, vs_info, &cmdbuf->state.gfx.vs.desc, 1);
   if (result != VK_SUCCESS)
      return;
   if (fs) {
      result = v9_prepare_fs_driver_set(cmdbuf);
      if (result != VK_SUCCESS)
         return;
      result = panvk_per_arch(cmd_prepare_shader_res_table)(
         cmdbuf, desc_state, &cmdbuf->state.gfx.fs.shader->desc_info,
         &cmdbuf->state.gfx.fs.desc, 1);
      if (result != VK_SUCCESS)
         return;
   }

   /*
    * blend_emit_descs() updates fs.blend_descs[], which are copied into
    * fragment push uniforms by cmd_prepare_draw_sysvals().  Prepare blend
    * descriptors before serializing push uniforms.
    */
   uint64_t blend_gpu = 0, zsd_gpu = 0;
   result = v9_prepare_blend_zsd(cmdbuf, fs, &blend_gpu, &zsd_gpu);
   if (result != VK_SUCCESS)
      return;

   panvk_per_arch(cmd_prepare_draw_sysvals)(cmdbuf, info, fs);

   struct pan_ptr vs_push;
   result = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, vs, &vs_push, 1);
   if (result != VK_SUCCESS)
      return;
   cmdbuf->state.gfx.vs.push_uniforms = vs_push.gpu;

   if (fs) {
      struct pan_ptr fs_push;
      result = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, fs, &fs_push, 1);
      if (result != VK_SUCCESS)
         return;
      cmdbuf->state.gfx.fs.push_uniforms = fs_push.gpu;
   }
   (void)0;
   result = v9_emit_malloc_vertex_job(cmdbuf, info, vs, fs, blend_gpu, zsd_gpu);
   if (result != VK_SUCCESS)
      return;
   clear_dirty_after_draw(cmdbuf);
   if (info->indirect.buffer_dev_addr)
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
}

/* Pipeline statistics and primitives generated queries of one API draw. JM
 * has no command stream to add with, so a panlib_update_pstats_draw()
 * workgroup counts every draw on the GPU, direct ones from a
 * VkDrawIndexedIndirectCommand made here. Same counters as CSF
 * cmd_pstats_draw(): the GS and tessellation kernels count their own output
 * (clipping and primitives generated).
 */
static void
v9_count_draw_queries(struct panvk_cmd_buffer *cmdbuf,
                      const struct panvk_draw_info *draw)
{
#define PSTAT(name) VK_QUERY_PIPELINE_STATISTIC_##name##_BIT
   const struct panvk_pstats_query_state *q = &cmdbuf->state.gfx.pstats_query;
   const bool kernels_count = cmdbuf->state.gfx.gs.shader ||
                              cmdbuf->state.gfx.tes.shader;
   const uint64_t prims_generated =
      kernels_count ? 0 : cmdbuf->state.gfx.prims_generated_query.ptr;
   uint32_t counted = 0;

   if (q->report) {
      counted = q->mask & (PSTAT(INPUT_ASSEMBLY_VERTICES) |
                           PSTAT(INPUT_ASSEMBLY_PRIMITIVES) |
                           PSTAT(VERTEX_SHADER_INVOCATIONS));
      if (!kernels_count)
         counted |= q->mask & (PSTAT(CLIPPING_INVOCATIONS) |
                               PSTAT(CLIPPING_PRIMITIVES));
   }

   if (!counted && !prims_generated)
      return;

   if (!v9_prerast_reserve_jobs(cmdbuf, 4))
      return;

   const uint32_t index_size = draw->index.index_size;
   uint64_t cmd = draw->indirect.buffer_dev_addr;
   uint32_t cmd_stride = draw->indirect.stride;
   uint32_t draw_count = draw->indirect.draw_count;

   if (!cmd) {
      struct pan_ptr ptr = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, sizeof(VkDrawIndexedIndirectCommand), 16);
      if (!ptr.gpu)
         return;

      /* JM index offsets are in bytes. */
      *(VkDrawIndexedIndirectCommand *)ptr.cpu = (VkDrawIndexedIndirectCommand){
         .indexCount = draw->vertex.count,
         .instanceCount = draw->instance.count,
         .firstIndex = index_size ? draw->index.offset / index_size : 0,
         .vertexOffset = draw->vertex.base,
         .firstInstance = draw->instance.base,
      };
      cmd = ptr.gpu;
      cmd_stride = 0;
      draw_count = 1;
   }

   const uint32_t view_mask = cmdbuf->state.gfx.render.view_mask;
   struct panlib_update_pstats_draw_args args = {
      .cmd = cmd,
      .cmd_stride = cmd_stride,
      .count_buffer = draw->indirect.count_buffer_dev_addr,
      .draw_index = draw->indirect.draw_index,
      .index_buffer = cmdbuf->state.gfx.ib.dev_addr,
      .index_buffer_size_el =
         index_size ? cmdbuf->state.gfx.ib.size / index_size : 0,
      .index_size_B = index_size,
      .restart = index_size && draw->index.restart_enable,
      .prim = draw->prim,
      .view_count = view_mask ? util_bitcount(view_mask) : 1,
      .patch_size = cmdbuf->vk.dynamic_graphics_state.ts.patch_control_points,
   };
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);

   if (counted) {
      static const uint32_t order[] = {
         PSTAT(INPUT_ASSEMBLY_VERTICES), PSTAT(INPUT_ASSEMBLY_PRIMITIVES),
         PSTAT(VERTEX_SHADER_INVOCATIONS), PSTAT(CLIPPING_INVOCATIONS),
         PSTAT(CLIPPING_PRIMITIVES),
      };

      args.stats = q->report + util_bitcount(q->mask) * sizeof(uint64_t);
      args.slots = 0;
      for (uint32_t i = 0; i < ARRAY_SIZE(order); i++) {
         uint32_t slot = (counted & order[i])
                            ? util_bitcount(q->mask & (order[i] - 1))
                            : 0xf;
         args.slots |= slot << (i * 4);
      }
      panlib_update_pstats_draw_struct(&ctx, panlib_1d(draw_count),
                                       PANLIB_BARRIER_JM_BARRIER, args);
   }

   /* The primitives generated query is the IA primitives counter alone. */
   if (prims_generated) {
      args.stats = prims_generated;
      args.slots = 0xfff0f;
      panlib_update_pstats_draw_struct(&ctx, panlib_1d(draw_count),
                                       PANLIB_BARRIER_JM_BARRIER, args);
   }
#undef PSTAT
}

/* Multiview and layered rendering, same scheme as the Bifrost path: the draw
 * is recorded once per view (or per framebuffer layer when the VS writes
 * gl_Layer), each copy with its own tiler context and the view or layer in
 * the layer_id sysval. The fragment jobs are emitted per layer when the
 * batch is closed. */
static void
v9_cmd_draw(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_info *info)
{
   uint32_t view_mask = cmdbuf->state.gfx.render.view_mask;
   uint32_t layer_count = view_mask ? util_bitcount(view_mask)
                                    : cmdbuf->state.gfx.render.layer_count;
   /* The last pre-rasterization stage writes gl_Layer. */
   const struct panvk_shader *vs = cmdbuf->state.gfx.gs.shader
                                      ? cmdbuf->state.gfx.gs.shader
                                   : info->prim == MESA_PRIM_PATCHES
                                      ? cmdbuf->state.gfx.tes.shader
                                      : cmdbuf->state.gfx.vs.shader;

   /* Without a gl_Layer write, primitives go to layer 0 only. */
   if (!view_mask && (!vs || !vs->writes_layer))
      layer_count = 1;

   /* Counted once per API draw, not per layer. */
   if (!info->pstats_counted)
      v9_count_draw_queries(cmdbuf, info);

   /* A layered draw re-runs the GS and tessellation kernels per layer: only
    * the first run counts into the queries. Views count, as on CSF. */
   const bool layered = !view_mask && layer_count > 1;
   const struct panvk_pstats_query_state pstats =
      cmdbuf->state.gfx.pstats_query;
   const uint64_t prims_generated =
      cmdbuf->state.gfx.prims_generated_query.ptr;

   for (uint32_t i = 0; i < MAX2(layer_count, 1); i++) {
      info->layer_id = view_mask ? u_bit_scan(&view_mask) : i;
      if (layered && i == 1) {
         cmdbuf->state.gfx.pstats_query.report = 0;
         cmdbuf->state.gfx.prims_generated_query.ptr = 0;
      }
      v9_cmd_draw_layer(cmdbuf, info);
   }

   cmdbuf->state.gfx.pstats_query = pstats;
   cmdbuf->state.gfx.prims_generated_query.ptr = prims_generated;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDraw)(VkCommandBuffer commandBuffer, uint32_t vertexCount,
                        uint32_t instanceCount, uint32_t firstVertex,
                        uint32_t firstInstance)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   if (instanceCount == 0 || vertexCount == 0)
      return;
   struct panvk_draw_info info = {
      .vertex.base = firstVertex,
      .vertex.count = vertexCount,
      .instance.base = firstInstance,
      .instance.count = instanceCount,
      .prim = panvk_get_client_prim(cmdbuf),
   };
   v9_cmd_draw(cmdbuf, &info);
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

   uint32_t index_size = cmdbuf->state.gfx.ib.index_size;
   if (index_size == 0 || cmdbuf->state.gfx.ib.size == 0) {
      (void)0;
      return;
   }

   struct panvk_draw_info info = {
      .index = panvk_draw_info_index(cmdbuf, firstIndex * index_size),
      .vertex.base = vertexOffset,
      .vertex.count = indexCount,
      .instance.base = firstInstance,
      .instance.count = instanceCount,
      .prim = panvk_get_client_prim(cmdbuf),
   };
   v9_cmd_draw(cmdbuf, &info);
}

/* JM has no multi-draw descriptor: drawCount (or maxDrawCount) is unrolled
 * into one indirect patch + MALLOC_VERTEX pair per draw, each with its own
 * push-uniform block (the patch writes sysvals in it) and gl_DrawID. For the
 * *Count variants the patch turns draws at or past the GPU-side count into
 * NULL jobs. */
static void
v9_draw_indirect_unrolled(struct panvk_cmd_buffer *cmdbuf,
                          struct panvk_buffer *buffer, VkDeviceSize offset,
                          uint32_t draw_count, uint32_t stride, bool indexed,
                          uint64_t count_buffer_dev_addr)
{
   if (indexed &&
       (cmdbuf->state.gfx.ib.index_size == 0 || cmdbuf->state.gfx.ib.size == 0))
      return;

   for (uint32_t i = 0; i < draw_count; i++) {
      struct panvk_draw_info info = {
         .vertex.count = 1,
         .instance.count = 1,
         .indirect.buffer_dev_addr =
            panvk_buffer_gpu_ptr(buffer, offset + (uint64_t)i * stride),
         .indirect.count_buffer_dev_addr = count_buffer_dev_addr,
         .indirect.draw_count = 1,
         .indirect.draw_index = i,
         .prim = panvk_get_client_prim(cmdbuf),
      };
      if (indexed)
         info.index = (__typeof__(info.index))panvk_draw_info_index(cmdbuf, 0);

      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
      v9_cmd_draw(cmdbuf, &info);
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndirect)(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                                VkDeviceSize offset, uint32_t drawCount,
                                uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);

   v9_draw_indirect_unrolled(cmdbuf, buffer, offset, drawCount, stride, false,
                             0);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndexedIndirect)(VkCommandBuffer commandBuffer,
                                       VkBuffer _buffer, VkDeviceSize offset,
                                       uint32_t drawCount, uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);

   v9_draw_indirect_unrolled(cmdbuf, buffer, offset, drawCount, stride, true,
                             0);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndirectCount)(VkCommandBuffer commandBuffer,
                                     VkBuffer _buffer, VkDeviceSize offset,
                                     VkBuffer countBuffer,
                                     VkDeviceSize countBufferOffset,
                                     uint32_t maxDrawCount, uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);
   VK_FROM_HANDLE(panvk_buffer, count_buffer, countBuffer);

   v9_draw_indirect_unrolled(
      cmdbuf, buffer, offset, maxDrawCount, stride, false,
      panvk_buffer_gpu_ptr(count_buffer, countBufferOffset));
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

   v9_draw_indirect_unrolled(
      cmdbuf, buffer, offset, maxDrawCount, stride, true,
      panvk_buffer_gpu_ptr(count_buffer, countBufferOffset));
}

/* VK_EXT_transform_feedback on v9 JM: CSF's entrypoints with the counter
 * loads and stores done by one-thread jobs instead of cs loads/stores.
 * After Noysz/panvk-g99-jm 0060/0064. */
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

/* Counter addresses of buffers 0..3, 0 when not given; returns the mask of
 * the given ones. */
static uint32_t
v9_xfb_counters(uint32_t first, uint32_t count, const VkBuffer *buffers,
                const VkDeviceSize *offsets,
                uint64_t addr[PANLIB_XFB_MAX_BUFFERS])
{
   uint32_t mask = 0;

   for (uint32_t i = 0; i < PANLIB_XFB_MAX_BUFFERS; i++) {
      addr[i] = 0;
      if (!buffers || i < first || i - first >= count)
         continue;

      VK_FROM_HANDLE(panvk_buffer, buffer, buffers[i - first]);
      if (!buffer)
         continue;

      addr[i] = panvk_buffer_gpu_ptr(buffer, offsets ? offsets[i - first] : 0);
      mask |= BITFIELD_BIT(i);
   }
   return mask;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginTransformFeedbackEXT)(
   VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer,
   uint32_t counterBufferCount, const VkBuffer *pCounterBuffers,
   const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   uint64_t c[PANLIB_XFB_MAX_BUFFERS];
   uint32_t mask = v9_xfb_counters(firstCounterBuffer, counterBufferCount,
                                   pCounterBuffers, pCounterBufferOffsets, c);
   struct pan_ptr offsets = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, PANLIB_XFB_MAX_BUFFERS * sizeof(uint32_t), 16);

   if (!offsets.gpu)
      return;

   /* Always a job, also with no counter buffer: the captures advance the
    * offsets, a resubmitted command buffer must start from 0 again. */
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_v9_xfb_begin(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
                       offsets.gpu, mask, c[0], c[1], c[2], c[3]);

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
   const uint64_t offsets = cmdbuf->state.gfx.xfb.offsets;
   uint64_t c[PANLIB_XFB_MAX_BUFFERS];
   uint32_t mask = v9_xfb_counters(firstCounterBuffer, counterBufferCount,
                                   pCounterBuffers, pCounterBufferOffsets, c);

   cmdbuf->state.gfx.xfb.active = false;
   cmdbuf->state.gfx.xfb.offsets = 0;
   if (!offsets || !mask)
      return;

   /* A barrier job: it runs after every capture of the batch. */
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_v9_xfb_end(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, offsets,
                     mask, c[0], c[1], c[2], c[3]);
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

   if (!instanceCount)
      return;

   struct pan_ptr draw = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(VkDrawIndirectCommand), 16);
   if (!draw.gpu)
      return;

   /* The indirect patch job of the draw is a barrier, it waits for this. */
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_xfb_byte_count_draw(
      &ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
      panvk_buffer_gpu_ptr(counter, counterBufferOffset), counterOffset,
      vertexStride, instanceCount, firstInstance, draw.gpu);

   struct panvk_draw_info info = {
      .vertex.count = 1,
      .instance.count = 1,
      .indirect.buffer_dev_addr = draw.gpu,
      .indirect.draw_count = 1,
      .prim = panvk_get_client_prim(cmdbuf),
   };

   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   v9_cmd_draw(cmdbuf, &info);
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
}

#endif /* PAN_ARCH != 9 */

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginRendering)(VkCommandBuffer commandBuffer,
                                  const VkRenderingInfo *pRenderingInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_cmd_graphics_state *state = &cmdbuf->state.gfx;
   bool resuming = pRenderingInfo->flags & VK_RENDERING_RESUMING_BIT;

   /* When resuming from a suspended pass, the state should be unchanged. */
   if (resuming && cmdbuf->cur_batch) {
      state->render.flags = pRenderingInfo->flags;
   } else {
      /* If we're not resuming, cur_batch should be NULL.  However, this
       * currently isn't true because of how events are implemented.
       *
       * XXX: Rewrite events to not close and open batch and add an assert here.
       */
      if (cmdbuf->cur_batch)
         panvk_per_arch(cmd_close_batch)(cmdbuf);

      panvk_per_arch(cmd_init_render_state)(cmdbuf, pRenderingInfo);
      cmdbuf->state.gfx.render.fb.needs_load = !resuming;
   }

   if (!cmdbuf->cur_batch)
      panvk_per_arch(cmd_open_batch)(cmdbuf);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndRendering2KHR)(
   VkCommandBuffer commandBuffer,
   UNUSED const VkRenderingEndInfoKHR *pRenderingEndInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (!(cmdbuf->state.gfx.render.flags & VK_RENDERING_SUSPENDING_BIT)) {
      const struct pan_fb_load *fb_load = &cmdbuf->state.gfx.render.fb.load;
      bool always_load = fb_load->z.always || fb_load->s.always;
      for (unsigned rt = 0; rt < PAN_MAX_RTS; rt++) {
         if (fb_load->rts[rt].always)
            always_load = true;
      }

      if (always_load)
         panvk_per_arch(cmd_alloc_fb_desc)(cmdbuf);

      cmdbuf->state.gfx.render.fb.needs_store = true;

      panvk_per_arch(cmd_close_batch)(cmdbuf);
      cmdbuf->cur_batch = NULL;
      panvk_per_arch(cmd_meta_resolve_attachments)(cmdbuf);
   }
}
