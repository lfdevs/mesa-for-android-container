/*
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2019-2025 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_blend_cso.h"

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "pan_shader.h"
#include "panfrost/compiler/pan_compiler.h"
#include "panfrost/compiler/pan_nir.h"
#include "pan_context.h"

#ifndef PAN_ARCH

DERIVE_HASH_TABLE(pan_blend_shader_key);

void
pan_blend_shader_cache_init(struct pan_blend_shader_cache *cache,
                            unsigned gpu_id, uint32_t gpu_variant,
                            struct pan_pool *bin_pool)
{
   cache->gpu_id = gpu_id;
   cache->gpu_variant = gpu_variant;
   cache->bin_pool = bin_pool;
   cache->shaders = pan_blend_shader_key_table_create(NULL);
   pthread_mutex_init(&cache->lock, NULL);
}

void
pan_blend_shader_cache_cleanup(struct pan_blend_shader_cache *cache)
{
   _mesa_hash_table_destroy(cache->shaders, NULL);
   pthread_mutex_destroy(&cache->lock);
}

#else /* PAN_ARCH */

static bool
pan_lower_blend_constants(nir_builder *b, nir_intrinsic_instr *intr,
                           void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_blend_const_color_rgba)
      return false;

   /* panfrost_nir_lower_sysvals always maps blend constants to slot 0 */
   unsigned offset = 0;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *constant = nir_load_ubo(
      b, 4, 32, nir_imm_int(b, PAN_UBO_SYSVALS), nir_imm_int(b, offset),
      .align_mul = 4, .align_offset = 0, .range_base = 0, .range = 4);
   nir_def_replace(&intr->def, constant);
   return true;
}

struct pan_blend_shader *
GENX(pan_blend_get_shader_locked)(struct pan_blend_shader_cache *cache,
                                  const struct pan_blend_state *state,
                                  nir_alu_type src0_type,
                                  nir_alu_type src1_type, unsigned rt)
{
   struct pan_blend_shader_key key = {
      .format = state->rts[rt].format,
      .src0_type = src0_type,
      .src1_type = src1_type,
      .rt = rt,
      .logicop_enable = state->logicop_enable,
      .logicop_func = state->logicop_func,
      .nr_samples = state->rts[rt].nr_samples,
      .equation = state->rts[rt].equation,
      .alpha_to_one = state->alpha_to_one,
   };
   /* Blend shaders should only be used for blending on Bifrost onwards */
   assert(PAN_ARCH <= 5 || state->logicop_enable || state->alpha_to_one ||
          !pan_blend_is_opaque(state->rts[rt].equation));
   assert(state->rts[rt].equation.color_mask != 0);

   struct hash_entry *he =
      _mesa_hash_table_search(cache->shaders, &key);
   struct pan_blend_shader *shader = he ? he->data : NULL;
   if (shader)
      return shader;

   shader = rzalloc(cache->shaders, struct pan_blend_shader);
   shader->key = key;
   _mesa_hash_table_insert(cache->shaders, &shader->key, shader);

   nir_shader *nir =
      GENX(pan_blend_create_shader)(state, src0_type, src1_type, rt);

   nir->info.num_ubos = PAN_UBO_SYSVALS + 1;
   nir_shader_intrinsics_pass(nir, pan_lower_blend_constants,
                              nir_metadata_control_flow, NULL);

   /* Compile the NIR shader */
   struct pan_compile_inputs inputs = {
      .gpu_id = cache->gpu_id,
      .gpu_variant = cache->gpu_variant,
      .is_blend = true,
      .blend.nr_samples = key.nr_samples,
      .pushable_ubos = BITFIELD_BIT(PAN_UBO_SYSVALS),
   };

   enum pipe_format rt_formats[8] = {0};
   rt_formats[rt] = key.format;

#if PAN_ARCH >= 6
   inputs.blend.bifrost_blend_desc =
      GENX(pan_blend_get_internal_desc)(key.format, key.rt, 0, false);
#endif

   struct pan_shader_info info;
   pan_preprocess_nir(nir, inputs.gpu_id);
   pan_postprocess_nir(nir, inputs.gpu_id);

#if PAN_ARCH >= 6
   NIR_PASS(_, nir, GENX(pan_inline_rt_conversion), rt_formats);
#else
   NIR_PASS(_, nir, pan_nir_lower_framebuffer, rt_formats,
            pan_raw_format_mask_midgard(rt_formats), MAX2(key.nr_samples, 1),
            (cache->gpu_id >> 16) < 0x700);
#endif

   struct util_dynarray binary;
   binary = UTIL_DYNARRAY_INIT;
   pan_shader_compile(nir, &inputs, &binary, &info);

   struct pan_ptr bin =
      pan_pool_alloc_aligned(cache->bin_pool, binary.size, 64);
   memcpy(bin.cpu, binary.data, binary.size);
   util_dynarray_fini(&binary);

   shader->work_reg_count = info.work_reg_count;

   shader->address = bin.gpu;
#if PAN_ARCH <= 5
   shader->address |= info.midgard.first_tag;
#endif

   ralloc_free(nir);

   return shader;
}

#endif /* PAN_ARCH */
