/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/u_dynarray.h"

#include "nir_builder.h"

#include "vk_blend.h"
#include "vk_format.h"
#include "vk_graphics_state.h"
#include "vk_log.h"

#include "pan_shader.h"

#include "panvk_blend.h"
#include "panvk_cmd_buffer.h"
#include "panvk_device.h"
#include "panvk_meta.h"
#include "panvk_shader.h"

struct panvk_blend_shader_key {
   enum panvk_meta_object_key_type type;
   struct pan_blend_shader_key info;
};

static bool
lower_load_blend_const(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   if (intr->intrinsic != nir_intrinsic_load_blend_const_color_rgba)
      return false;

   b->cursor = nir_before_instr(instr);

   /* Blend constants are always passed through FAU words 0:3. */
   nir_def *blend_consts = nir_load_push_constant(
      b, intr->def.num_components, intr->def.bit_size, nir_imm_int(b, 0));

   nir_def_rewrite_uses(&intr->def, blend_consts);
   return true;
}

static VkResult
get_blend_shader(struct panvk_device *dev,
                 const struct pan_blend_state *state,
                 nir_alu_type src0_type, nir_alu_type src1_type,
                 unsigned rt, uint64_t *shader_addr)
{
   struct panvk_physical_device *pdev =
      to_panvk_physical_device(dev->vk.physical);
   struct panvk_blend_shader_key key = {
      .type = PANVK_META_OBJECT_KEY_BLEND_SHADER,
      .info = {
         .format = state->rts[rt].format,
         .src0_type = src0_type,
         .src1_type = src1_type,
         .rt = rt,
         .logicop_enable = state->logicop_enable,
         .logicop_func = state->logicop_func,
         .nr_samples = state->rts[rt].nr_samples,
         .equation = state->rts[rt].equation,
         .alpha_to_one = state->alpha_to_one,
      },
   };
   struct panvk_internal_shader *shader;

   assert(state->logicop_enable || state->alpha_to_one ||
          !pan_blend_is_opaque(state->rts[rt].equation));
   assert(state->rts[rt].equation.color_mask != 0);

   VkShaderEXT shader_handle = (VkShaderEXT)vk_meta_lookup_object(
      &dev->meta, VK_OBJECT_TYPE_SHADER_EXT, &key, sizeof(key));
   if (shader_handle != VK_NULL_HANDLE)
      goto out;

   nir_shader *nir =
      GENX(pan_blend_create_shader)(state, src0_type, src1_type, rt);

   NIR_PASS(_, nir, nir_shader_instructions_pass, lower_load_blend_const,
            nir_metadata_control_flow, NULL);

   /* Compile the NIR shader */
   struct pan_compile_inputs inputs = {
      .gpu_id = pdev->kmod.dev->props.gpu_id,
      .gpu_variant = pdev->kmod.dev->props.gpu_variant,
      .is_blend = true,
   };

   pan_preprocess_nir(nir, inputs.gpu_id);

   VkResult result =
      panvk_per_arch(create_internal_shader)(dev, nir, &inputs, &shader);

   ralloc_free(nir);

   if (result != VK_SUCCESS)
      return result;

   shader_handle = (VkShaderEXT)vk_meta_cache_object(
      &dev->vk, &dev->meta, &key, sizeof(key), VK_OBJECT_TYPE_SHADER_EXT,
      (uint64_t)panvk_internal_shader_to_handle(shader));

out:
   shader = panvk_internal_shader_from_handle(shader_handle);
   *shader_addr = panvk_priv_mem_dev_addr(shader->code_mem);
   return VK_SUCCESS;
}

static void
emit_blend_desc(const struct pan_blend_state *state, uint8_t rt_idx,
                const struct pan_shader_info *fs_info, uint8_t loc,
                uint64_t fs_code, uint64_t blend_shader, uint16_t constant,
                bool dithered, struct mali_blend_packed *bd)
{
   const struct pan_blend_rt_state *rt = &state->rts[rt_idx];

   pan_pack(bd, BLEND, cfg) {
      if (loc == MESA_VK_ATTACHMENT_UNUSED || !rt->equation.color_mask) {
         cfg.enable = false;
         cfg.internal.mode = MALI_BLEND_MODE_OFF;
         continue;
      }

      cfg.srgb = util_format_is_srgb(rt->format);
      cfg.load_destination = pan_blend_reads_dest(rt->equation);
      cfg.round_to_fb_precision = true;
      cfg.blend_constant = constant;

      if (blend_shader) {
         /* Blend and fragment shaders must be in the same 4G region. */
         assert((blend_shader >> 32) == (fs_code >> 32));
         /* Blend shader must be 16-byte aligned. */
         assert((blend_shader & 15) == 0);
         /* Fragment shader return address must be 8-byte aligned. */
         assert((fs_code & 7) == 0);

         cfg.internal.mode = MALI_BLEND_MODE_SHADER;
         cfg.internal.shader.pc = (uint32_t)blend_shader;

#if PAN_ARCH < 9
         uint32_t ret_offset = fs_info->bifrost.blend[loc].return_offset;

         /* If ret_offset is zero, we assume the BLEND is a terminal
          * instruction and set return_value to zero, to let the
          * blend shader jump to address zero, which terminates the
          * thread.
          */
         cfg.internal.shader.return_value =
            ret_offset ? fs_code + ret_offset : 0;
#endif
      } else {
         bool opaque = pan_blend_is_opaque(rt->equation);

         cfg.internal.mode =
            opaque ? MALI_BLEND_MODE_OPAQUE : MALI_BLEND_MODE_FIXED_FUNCTION;

         pan_blend_to_fixed_function_equation(rt->equation, &cfg.equation);

         /* If we want the conversion to work properly, num_comps must be set to
          * 4.
          */
         cfg.internal.fixed_function.num_comps = 4;
         cfg.internal.fixed_function.conversion.memory_format =
            GENX(pan_dithered_format_from_pipe_format)(rt->format, dithered);

#if PAN_ARCH >= 7
         if (cfg.internal.mode == MALI_BLEND_MODE_FIXED_FUNCTION &&
             (cfg.internal.fixed_function.conversion.memory_format & 0xff) ==
                MALI_RGB_COMPONENT_ORDER_RGB1) {
            /* fixed function does not like RGB1 as the component order */
            /* force this field to be the RGBA. */
            cfg.internal.fixed_function.conversion.memory_format &= ~0xff;
            cfg.internal.fixed_function.conversion.memory_format |=
               MALI_RGB_COMPONENT_ORDER_RGBA;
         }
#endif

         cfg.internal.fixed_function.rt = rt_idx;

#if PAN_ARCH < 9
         nir_alu_type type = fs_info->bifrost.blend[loc].type;
         if (fs_info->fs.untyped_color_outputs) {
            cfg.internal.fixed_function.conversion.register_format =
               GENX(pan_fixup_blend_type)(type, rt->format);
         } else {
            cfg.internal.fixed_function.conversion.register_format =
               pan_blend_type_from_nir(type);
         }

         if (!opaque) {
            cfg.internal.fixed_function.alpha_zero_nop =
               pan_blend_alpha_zero_nop(rt->equation);
            cfg.internal.fixed_function.alpha_one_store =
               pan_blend_alpha_one_store(rt->equation);
         }
#endif
      }
   }
}

static bool
blend_needs_shader(const struct pan_blend_state *state, unsigned rt_idx,
                   unsigned *ff_blend_constant)
{
   const struct pan_blend_rt_state *rt = &state->rts[rt_idx];

   /* LogicOp requires a blend shader */
   if (state->logicop_enable)
      return true;

   /* alpha-to-one always requires a blend shader */
   if (state->alpha_to_one)
      return true;

   /* If the output is opaque, we don't need a blend shader, no matter the
    * format.
    */
   if (pan_blend_is_opaque(rt->equation))
      return false;

   /* Not all formats can be blended by fixed-function hardware */
   if (!GENX(pan_format_supports_hw_blend)(rt->format))
      return true;

   bool supports_2src = pan_blend_supports_2src(PAN_ARCH);
   if (!pan_blend_can_fixed_function(PAN_ARCH, rt->equation, supports_2src))
      return true;

   unsigned constant_mask = pan_blend_constant_mask(rt->equation);

   /* v6 doesn't support blend constants in FF blend equations. */
   if (constant_mask && PAN_ARCH == 6)
      return true;

   if (!pan_blend_is_homogenous_constant(constant_mask, state->constants))
      return true;

   /* v7+ only uses the constant from RT 0. If we're not RT0, all previous
    * RTs using FF with a blend constant need to have the same constant,
    * otherwise we need a blend shader.
    */
   unsigned blend_const = ~0;
   if (constant_mask) {
      const float blend_const_f =
         pan_blend_get_constant(constant_mask, state->constants);
      blend_const = pan_pack_blend_constant(rt->format, blend_const_f);

      if (*ff_blend_constant != ~0 && blend_const != *ff_blend_constant)
         return true;
   }

   /* Update the fixed function blend constant, if we use it. */
   if (blend_const != ~0)
      *ff_blend_constant = blend_const;

   return false;
}

VkResult
panvk_per_arch(blend_emit_descs)(struct panvk_cmd_buffer *cmdbuf,
                                 struct mali_blend_packed *bds)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_color_blend_state *cb = &dyns->cb;
   const struct vk_color_attachment_location_state *cal = &dyns->cal;
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(cmdbuf->state.gfx.fs.shader);
   const struct pan_shader_info *fs_info = &fs->info;
   uint64_t fs_code = panvk_shader_variant_get_dev_addr(fs);
   const struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
   const VkFormat *color_attachment_formats = render->color_attachments.fmts;
   const uint8_t *color_attachment_samples = render->color_attachments.samples;
   struct panvk_blend_info *blend_info = &cmdbuf->state.gfx.cb.info;
   struct pan_blend_state bs = {
      .alpha_to_one = dyns->ms.alpha_to_one_enable,
      .logicop_enable = cb->logic_op_enable,
      .logicop_func = vk_logic_op_to_pipe(cb->logic_op),
      .rt_count = cmdbuf->state.gfx.render.fb.layout.rt_count,
      .constants =
         {
            cb->blend_constants[0],
            cb->blend_constants[1],
            cb->blend_constants[2],
            cb->blend_constants[3],
         },
   };
   uint64_t blend_shaders[8] = {};
   /* All bits set to one encodes unused fixed-function blend constant. */
   unsigned ff_blend_constant = ~0;

   uint8_t loc_rt[MAX_RTS], rt_loc[MAX_RTS];
   memset(loc_rt, MESA_VK_ATTACHMENT_UNUSED, sizeof(loc_rt));
   memset(rt_loc, MESA_VK_ATTACHMENT_UNUSED, sizeof(rt_loc));

   memset(blend_info, 0, sizeof(*blend_info));
   for (uint8_t i = 0; i < bs.rt_count; i++) {
      struct pan_blend_rt_state *rt = &bs.rts[i];

      uint8_t loc = cal->color_map[i];
      if (loc == MESA_VK_ATTACHMENT_UNUSED)
         continue;

      if (!(fs_info->outputs_written & BITFIELD_BIT(FRAG_RESULT_DATA0 + loc)))
         continue;

      /* At this point, we know it's mapped to a shader location. */
      assert(loc < MAX_RTS && loc_rt[loc] == MESA_VK_ATTACHMENT_UNUSED);
      rt_loc[i] = loc;
      loc_rt[loc] = i;

      if (!(cb->color_write_enables & BITFIELD_BIT(i)))
         continue;

      if (color_attachment_formats[i] == VK_FORMAT_UNDEFINED)
         continue;

      if (!cb->attachments[i].write_mask)
         continue;

      rt->format = vk_format_to_pipe_format(color_attachment_formats[i]);

      /* Disable blending for LOGICOP_NOOP unless the format is float/srgb */
      bool is_float = util_format_is_float(rt->format);
      if (bs.logicop_enable && bs.logicop_func == PIPE_LOGICOP_NOOP &&
          !(is_float || util_format_is_srgb(rt->format)))
         continue;

      rt->nr_samples = color_attachment_samples[i];
      rt->equation.blend_enable = cb->attachments[i].blend_enable;
      rt->equation.is_float = is_float;
      rt->equation.color_mask = cb->attachments[i].write_mask;

      rt->equation.rgb_func =
         vk_blend_op_to_pipe(cb->attachments[i].color_blend_op);
      rt->equation.rgb_src_factor =
         vk_blend_factor_to_pipe(cb->attachments[i].src_color_blend_factor);
      rt->equation.rgb_dst_factor =
         vk_blend_factor_to_pipe(cb->attachments[i].dst_color_blend_factor);
      rt->equation.alpha_func =
         vk_blend_op_to_pipe(cb->attachments[i].alpha_blend_op);
      rt->equation.alpha_src_factor =
         vk_blend_factor_to_pipe(cb->attachments[i].src_alpha_blend_factor);
      rt->equation.alpha_dst_factor =
         vk_blend_factor_to_pipe(cb->attachments[i].dst_alpha_blend_factor);

      /* We have the format and the constants so we can optimize the blend
       * equation before we decide if we actually need a blend shader.
       */
      pan_blend_optimize_equation(&rt->equation, rt->format, bs.constants);

      blend_info->any_dest_read |= pan_blend_reads_dest(rt->equation);

      if (blend_needs_shader(&bs, i, &ff_blend_constant)) {
         nir_alu_type src0_type = fs_info->bifrost.blend[loc].type;
         nir_alu_type src1_type = fs_info->bifrost.blend_src1_type;

         VkResult result = get_blend_shader(dev, &bs, src0_type, src1_type,
                                            i, &blend_shaders[i]);
         if (result != VK_SUCCESS)
            return result;

         blend_info->shader_loads_blend_const |=
            pan_blend_constant_mask(rt->equation) != 0;
         blend_info->needs_shader = true;
      }
   }

   /* Set the blend constant to zero if it's not used by any of the blend ops. */
   if (ff_blend_constant == ~0)
      ff_blend_constant = 0;

   struct mali_blend_packed packed[MAX_RTS];
   for (uint8_t rt = 0; rt < bs.rt_count; rt++) {
      bool dithered = render->flags &
                      VK_RENDERING_ENABLE_LEGACY_DITHERING_BIT_EXT;

      emit_blend_desc(&bs, rt, fs_info, rt_loc[rt], fs_code,
                      blend_shaders[rt], ff_blend_constant, dithered,
                      &packed[rt]);
   }

   /* Copy into the GPU descriptor array */
   typed_memcpy(bds, packed, bs.rt_count);

   /* Re-order blend descriptors for the shader
    *
    * Blending on Bifrost+ is really annoying.  In theory, we can order the
    * blend descriptors any way we want and, in theory, they all have an RT
    * index.  However, that's not really the way blending works.  If every
    * blend descriptor is either disabled or has the RT index that's the same
    * as the blend descriptor index, everything is fine.  If not, we're in for
    * a bit of trouble.
    *
    * The RT index is only really consumed by the BLEND instruction to tell it
    * what RT to target.  The FF blend hardware, however, assumes that blend
    * RTs map 1:1 to framebuffer RTs.  If the BLEND instruction kicks off a
    * blend shader, everything is good because the blend shader has the
    * correct RT index baked into it.  But if the BLEND fires off a message to
    * the blend unit, it then looks up the blend descriptor by RT index,
    * ignoring which blend descriptor FAU we read from the shader.  Say, for
    * instance bds[3].rt == 5.  If we BLEND using blend_descriptor_3, the
    * BLEND instruction will see an RT of 5 and kick off a blend message.  The
    * blend unit will then fetch bds[5] and use that to do the actual blend.
    *
    * We could try to do something crazy where we divide blend descriptors
    * into a shader half and a HW half and only re-arrange the shader half so
    * that the hardware's little daisy-chain game works.  However, there are a
    * few fields that are used by both, notably the conversion, so this is
    * dead in the water.
    *
    * The solution, then, is to push our own blend descriptors.  Or at least
    * the shader half (everything in .internal).  This lets us leave the HW
    * blend descriptors with a 1:1 mapping to framebuffer RTs and re-order
    * things from the shader PoV.  The BLEND instructions in the shader will
    * use the ones we pushed manually and then, when a blend message gets sent
    * to the hardware, it'll pick up an idential blend descriptor in the RT
    * index location in the bds[] array.
    */
   for (uint8_t loc = 0; loc < MAX_RTS; loc++) {
      uint8_t rt = loc_rt[loc];
      if (rt == MESA_VK_ATTACHMENT_UNUSED) {
         struct mali_blend_packed disabled;
         pan_pack(&disabled, BLEND, cfg) {
            cfg.enable = false;
            cfg.internal.mode = MALI_BLEND_MODE_OFF;
         }
         cmdbuf->state.gfx.fs.blend_descs[loc] =
            disabled.opaque[2] | (uint64_t)disabled.opaque[3] << 32;
      } else {
         cmdbuf->state.gfx.fs.blend_descs[loc] =
            packed[rt].opaque[2] | (uint64_t)packed[rt].opaque[3] << 32;
      }
   }

   gfx_state_set_dirty(cmdbuf, FS_PUSH_UNIFORMS);

   return VK_SUCCESS;
}
