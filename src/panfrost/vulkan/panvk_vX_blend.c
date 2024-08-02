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
#include "panvk_device.h"
#include "panvk_shader.h"

DERIVE_HASH_TABLE(pan_blend_shader_key);

VkResult
panvk_per_arch(blend_shader_cache_init)(struct panvk_device *dev)
{
   struct panvk_blend_shader_cache *cache = &dev->blend_shader_cache;

   simple_mtx_init(&cache->lock, mtx_plain);

   struct panvk_pool_properties bin_pool_props = {
      .create_flags = PAN_KMOD_BO_FLAG_EXECUTABLE,
      .slab_size = 16 * 1024,
      .label = "blend shaders",
      .owns_bos = true,
      .prealloc = false,
      .needs_locking = false,
   };
   panvk_pool_init(&cache->bin_pool, dev, NULL, &bin_pool_props);

   cache->ht = pan_blend_shader_key_table_create(NULL);
   if (!cache->ht) {
      panvk_pool_cleanup(&cache->bin_pool);
      simple_mtx_destroy(&cache->lock);
      return vk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "couldn't create blend shader hash table");
   }

   return VK_SUCCESS;
}

void
panvk_per_arch(blend_shader_cache_cleanup)(struct panvk_device *dev)
{
   struct panvk_blend_shader_cache *cache = &dev->blend_shader_cache;

   hash_table_foreach_remove(cache->ht, he)
      vk_free(&dev->vk.alloc, he->data);

   _mesa_hash_table_destroy(cache->ht, NULL);
   panvk_pool_cleanup(&cache->bin_pool);
   simple_mtx_destroy(&cache->lock);
}

static bool
lower_load_blend_const(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   if (intr->intrinsic != nir_intrinsic_load_blend_const_color_rgba)
      return false;

   b->cursor = nir_before_instr(instr);

   unsigned offset = offsetof(struct panvk_graphics_sysvals, blend.constants);
   nir_def *blend_consts = nir_load_push_constant(
      b, intr->def.num_components, intr->def.bit_size, nir_imm_int(b, 0),
      /* Push constants are placed first, and then come the sysvals. */
      .base = offset + 256,
      .range = intr->def.num_components * intr->def.bit_size / 8);

   nir_def_rewrite_uses(&intr->def, blend_consts);
   return true;
}

static VkResult
get_blend_shader_locked(struct panvk_device *dev,
                        const struct pan_blend_state *state,
                        nir_alu_type src0_type, nir_alu_type src1_type,
                        unsigned rt, mali_ptr *shader_addr)
{
   struct panvk_physical_device *pdev =
      to_panvk_physical_device(dev->vk.physical);
   struct panvk_blend_shader_cache *cache = &dev->blend_shader_cache;
   struct pan_blend_shader_key key = {
      .format = state->rts[rt].format,
      .src0_type = src0_type,
      .src1_type = src1_type,
      .rt = rt,
      .has_constants = pan_blend_constant_mask(state->rts[rt].equation) != 0,
      .logicop_enable = state->logicop_enable,
      .logicop_func = state->logicop_func,
      .nr_samples = state->rts[rt].nr_samples,
      .equation = state->rts[rt].equation,
   };

   assert(state->logicop_enable ||
          !pan_blend_is_opaque(state->rts[rt].equation));
   assert(state->rts[rt].equation.color_mask != 0);
   simple_mtx_assert_locked(&dev->blend_shader_cache.lock);

   struct hash_entry *he = _mesa_hash_table_search(cache->ht, &key);
   struct panvk_blend_shader *shader = he ? he->data : NULL;

   if (shader)
      goto out;

   shader = vk_zalloc(&dev->vk.alloc, sizeof(*shader), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!shader)
      return vk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "couldn't allocate blend shader object");

   nir_shader *nir =
      GENX(pan_blend_create_shader)(state, src0_type, src1_type, rt);

   NIR_PASS_V(nir, nir_shader_instructions_pass, lower_load_blend_const,
              nir_metadata_control_flow, NULL);

   /* Compile the NIR shader */
   struct panfrost_compile_inputs inputs = {
      .gpu_id = pdev->kmod.props.gpu_prod_id,
      .no_ubo_to_push = true,
      .is_blend = true,
      .blend =
         {
            .nr_samples = key.nr_samples,
            .bifrost_blend_desc =
               GENX(pan_blend_get_internal_desc)(key.format, key.rt, 0, false),
         },
   };

   pan_shader_preprocess(nir, inputs.gpu_id);

   enum pipe_format rt_formats[8] = {0};
   rt_formats[rt] = key.format;
   NIR_PASS_V(nir, GENX(pan_inline_rt_conversion), rt_formats);

   struct pan_shader_info info;
   struct util_dynarray binary;

   util_dynarray_init(&binary, nir);
   GENX(pan_shader_compile)(nir, &inputs, &binary, &info);

   shader->key = key;
   shader->binary = pan_pool_upload_aligned(&cache->bin_pool.base, binary.data,
                                            binary.size, 128);

   ralloc_free(nir);

   _mesa_hash_table_insert(cache->ht, &shader->key, shader);

out:
   *shader_addr = shader->binary;
   return VK_SUCCESS;
}

static VkResult
get_blend_shader(struct panvk_device *dev, const struct pan_blend_state *state,
                 nir_alu_type src0_type, nir_alu_type src1_type, unsigned rt,
                 mali_ptr *shader_addr)
{
   struct panvk_blend_shader_cache *cache = &dev->blend_shader_cache;
   VkResult result;

   simple_mtx_lock(&cache->lock);
   result = get_blend_shader_locked(dev, state, src0_type, src1_type, rt,
                                    shader_addr);
   simple_mtx_unlock(&cache->lock);

   return result;
}

static void
emit_blend_desc(const struct pan_shader_info *fs_info, mali_ptr fs_code,
                const struct pan_blend_state *state, unsigned rt_idx,
                mali_ptr blend_shader, uint16_t constant,
                struct mali_blend_packed *bd)
{
   const struct pan_blend_rt_state *rt = &state->rts[rt_idx];

   pan_pack(bd, BLEND, cfg) {
      if (!state->rt_count || !rt->equation.color_mask) {
         cfg.enable = false;
         cfg.internal.mode = MALI_BLEND_MODE_OFF;
         continue;
      }

      cfg.srgb = util_format_is_srgb(rt->format);
      cfg.load_destination = pan_blend_reads_dest(rt->equation);
      cfg.round_to_fb_precision = true;
      cfg.constant = constant;

      if (blend_shader) {
         uint32_t ret_offset = fs_info->bifrost.blend[rt_idx].return_offset;

         /* Blend and fragment shaders must be in the same 4G region. */
         assert((blend_shader >> 32) == (fs_code >> 32));
         /* Blend shader must be 16-byte aligned. */
         assert((blend_shader & 15) == 0);
         /* Fragment shader return address must be 8-byte aligned. */
         assert((fs_code & 7) == 0);

         cfg.internal.mode = MALI_BLEND_MODE_SHADER;
         cfg.internal.shader.pc = (uint32_t)blend_shader;

         /* If ret_offset is zero, we assume the BLEND is a terminal
          * instruction and set return_value to zero, to let the
          * blend shader jump to address zero, which terminates the
          * thread.
          */
         cfg.internal.shader.return_value =
            ret_offset ? fs_code + ret_offset : 0;
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
            GENX(panfrost_dithered_format_from_pipe_format)(rt->format, false);
         cfg.internal.fixed_function.rt = rt_idx;

         if (fs_info->fs.untyped_color_outputs) {
            nir_alu_type type = fs_info->bifrost.blend[rt_idx].type;

            cfg.internal.fixed_function.conversion.register_format =
               GENX(pan_fixup_blend_type)(type, rt->format);
         } else {
            cfg.internal.fixed_function.conversion.register_format =
               fs_info->bifrost.blend[rt_idx].format;
         }

         if (!opaque) {
            cfg.internal.fixed_function.alpha_zero_nop =
               pan_blend_alpha_zero_nop(rt->equation);
            cfg.internal.fixed_function.alpha_one_store =
               pan_blend_alpha_one_store(rt->equation);
         }
      }
   }
}

static uint16_t
get_ff_blend_constant(const struct pan_blend_state *state, unsigned rt_idx,
                      unsigned const_idx)
{
   const struct pan_blend_rt_state *rt = &state->rts[rt_idx];

   /* On Bifrost, the blend constant is expressed with a UNORM of the
    * size of the target format. The value is then shifted such that
    * used bits are in the MSB.
    */
   const struct util_format_description *format_desc =
      util_format_description(rt->format);
   unsigned chan_size = 0;
   for (unsigned c = 0; c < format_desc->nr_channels; c++)
      chan_size = MAX2(format_desc->channel[c].size, chan_size);
   float factor = ((1 << chan_size) - 1) << (16 - chan_size);

   return (uint16_t)(state->constants[const_idx] * factor);
}

static bool
blend_needs_shader(const struct pan_blend_state *state, unsigned rt_idx,
                   unsigned *ff_blend_constant)
{
   const struct pan_blend_rt_state *rt = &state->rts[rt_idx];

   /* LogicOp requires a blend shader, unless it's a NOOP, in which case we just
    * disable blending.
    */
   if (state->logicop_enable)
      return state->logicop_func != PIPE_LOGICOP_NOOP;

   /* If the output is opaque, we don't need a blend shader, no matter the
    * format.
    */
   if (pan_blend_is_opaque(rt->equation))
      return false;

   /* Not all formats can be blended by fixed-function hardware */
   if (!GENX(panfrost_blendable_format_from_pipe_format)(rt->format)->internal)
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
      blend_const =
         get_ff_blend_constant(state, rt_idx, ffs(constant_mask) - 1);

      if (*ff_blend_constant != ~0 && blend_const != *ff_blend_constant)
         return true;
   }

   bool supports_2src = pan_blend_supports_2src(PAN_ARCH);
   if (!pan_blend_can_fixed_function(rt->equation, supports_2src))
      return true;

   /* Update the fixed function blend constant, if we use it. */
   if (blend_const != ~0)
      *ff_blend_constant = blend_const;

   return false;
}

VkResult
panvk_per_arch(blend_emit_descs)(
   struct panvk_device *dev, const struct vk_color_blend_state *cb,
   const VkFormat *color_attachment_formats, uint8_t *color_attachment_samples,
   const struct pan_shader_info *fs_info, mali_ptr fs_code,
   struct mali_blend_packed *bds, bool *any_dest_read,
   bool *any_blend_const_load)
{
   struct pan_blend_state bs = {
      .logicop_enable = cb->logic_op_enable,
      .logicop_func = vk_logic_op_to_pipe(cb->logic_op),
      .rt_count = cb->attachment_count,
      .constants =
         {
            cb->blend_constants[0],
            cb->blend_constants[1],
            cb->blend_constants[2],
            cb->blend_constants[3],
         },
   };
   mali_ptr blend_shaders[8] = {};
   /* All bits set to one encodes unused fixed-function blend constant. */
   unsigned ff_blend_constant = ~0;

   *any_dest_read = false;
   for (uint8_t i = 0; i < cb->attachment_count; i++) {
      struct pan_blend_rt_state *rt = &bs.rts[i];

      if (!(cb->color_write_enables & BITFIELD_BIT(i))) {
         rt->equation.color_mask = 0;
         continue;
      }

      if (bs.logicop_enable && bs.logicop_func == PIPE_LOGICOP_NOOP) {
         rt->equation.color_mask = 0;
         continue;
      }

      if (color_attachment_formats[i] == VK_FORMAT_UNDEFINED) {
         rt->equation.color_mask = 0;
         continue;
      }

      if (!cb->attachments[i].write_mask) {
         rt->equation.color_mask = 0;
         continue;
      }

      rt->format = vk_format_to_pipe_format(color_attachment_formats[i]);

      rt->nr_samples = color_attachment_samples[i];
      rt->equation.blend_enable = cb->attachments[i].blend_enable;
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

      bool dest_has_alpha = util_format_has_alpha(rt->format);
      if (!dest_has_alpha) {
         rt->equation.rgb_src_factor =
            util_blend_dst_alpha_to_one(rt->equation.rgb_src_factor);
         rt->equation.rgb_dst_factor =
            util_blend_dst_alpha_to_one(rt->equation.rgb_dst_factor);

         rt->equation.alpha_src_factor =
            util_blend_dst_alpha_to_one(rt->equation.alpha_src_factor);
         rt->equation.alpha_dst_factor =
            util_blend_dst_alpha_to_one(rt->equation.alpha_dst_factor);
      }

      *any_dest_read |= pan_blend_reads_dest(rt->equation);

      if (blend_needs_shader(&bs, i, &ff_blend_constant)) {
         nir_alu_type src0_type = fs_info->bifrost.blend[i].type;
         nir_alu_type src1_type = fs_info->bifrost.blend_src1_type;

         VkResult result = get_blend_shader(dev, &bs, src0_type, src1_type, i,
                                            &blend_shaders[i]);
         if (result != VK_SUCCESS)
            return result;

         *any_blend_const_load |= pan_blend_constant_mask(rt->equation) != 0;
      }
   }

   /* Set the blend constant to zero if it's not used by any of the blend ops. */
   if (ff_blend_constant == ~0)
      ff_blend_constant = 0;

   /* Now that we've collected all the information, we can emit. */
   for (uint8_t i = 0; i < MAX2(cb->attachment_count, 1); i++) {
      emit_blend_desc(fs_info, fs_code, &bs, i, blend_shaders[i],
                      ff_blend_constant, &bds[i]);
   }

   return VK_SUCCESS;
}
