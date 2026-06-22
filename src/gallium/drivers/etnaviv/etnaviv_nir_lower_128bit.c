/*
 * Copyright © 2025 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "etnaviv_nir.h"
#include "nir.h"

struct lower_128bit_data
{
   const struct etna_shader_key *key;
   nir_variable *companion_var[ETNA_MAX_128BIT_RTS];
};

static const glsl_type *
vec4_of_same_base(const glsl_type *t)
{
   enum glsl_base_type base = glsl_get_base_type(t);
   return glsl_vector_type(base, 4);
}

static nir_variable *
get_or_create_companion_var(nir_shader *s, nir_variable *orig, unsigned companion_idx)
{
   unsigned location = FRAG_RESULT_DATA0 + companion_idx;
   const glsl_type *vec4_t = vec4_of_same_base(orig->type);

   nir_foreach_shader_out_variable(var, s) {
      if (var->data.location == location) {
         var->type = vec4_t;
         return var;
      }
   }

   nir_variable *companion =
      nir_variable_create(s, nir_var_shader_out, vec4_t, "etna_128bit_companion");
   companion->data.location = location;
   companion->data.driver_location = companion_idx;
   companion->data.index = 0;
   return companion;
}

static bool
lower_128bit_output(nir_builder *b, nir_intrinsic_instr *intr, void *_data)
{
   struct lower_128bit_data *data = _data;

   if (intr->intrinsic != nir_intrinsic_store_deref)
      return false;

   nir_variable *var = nir_intrinsic_get_var(intr, 0);
   if (!var || var->data.mode != nir_var_shader_out)
      return false;

   if (var->data.location < FRAG_RESULT_DATA0 ||
       var->data.location >= FRAG_RESULT_DATA0 + ETNA_MAX_128BIT_RTS)
      return false;

   nir_variable *companion =
      data->companion_var[var->data.location - FRAG_RESULT_DATA0];
   if (!companion)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   const unsigned wm = nir_intrinsic_write_mask(intr);
   nir_def *color = intr->src[1].ssa;
   nir_def *zero = nir_imm_zero(b, 1, color->bit_size);
   nir_def *ch[4];
   for (unsigned i = 0; i < 4; i++)
      ch[i] = i < color->num_components ? nir_channel(b, color, i) : zero;

   nir_def *rg_output = nir_vec4(b, ch[0], ch[1], zero, zero);
   nir_def *ba_output = nir_vec4(b, ch[2], ch[3], zero, zero);

   if (wm & 0x3)
      nir_store_var(b, var, rg_output, wm & 0x3);
   if (wm >> 2)
      nir_store_var(b, companion, ba_output, wm >> 2);

   nir_instr *orig_deref = nir_def_instr(intr->src[0].ssa);
   nir_instr_remove(&intr->instr);
   if (nir_def_is_unused(&nir_instr_as_deref(orig_deref)->def))
      nir_instr_remove(orig_deref);

   return true;
}

static bool
lower_128bit_sampler(nir_builder *b, nir_tex_instr *tex, void *_data)
{
   struct lower_128bit_data *data = _data;

   /* txf has no sampler state but still references a texture descriptor.
    * Pair it via texture_index. All other tex ops use sampler_index.
    */
   const bool is_txf = !nir_tex_instr_need_sampler(tex);
   if (is_txf && tex->op != nir_texop_txf)
      return false;

   const unsigned idx = is_txf ? tex->texture_index : tex->sampler_index;
   if (!(data->key->tex_is_128bit & (1 << idx)))
      return false;

   const unsigned companion = data->key->sampler_companion[idx];
   if (companion == ~0U)
      return false;

   /* Blob's gather lowering for paired G32R32F emulation of 128-bit formats:
    * each gather of one 32-bit channel becomes two HW tg4 calls returning
    * 16-bit halves, reassembled via (hi << 16) | lo. Works for both float
    * and int 128-bit formats - the bit pattern reconstructs the original
    * value either way.
    */
   if (tex->op == nir_texop_tg4) {
      const unsigned orig_comp = tex->component;
      const unsigned sampler_idx = (orig_comp < 2) ? tex->sampler_index : companion;
      const unsigned lo_comp = (orig_comp & 1) ? 2 : 0;
      const unsigned hi_comp = lo_comp + 1;

      b->cursor = nir_after_instr(&tex->instr);

      nir_def *halves[2];
      const unsigned comps[2] = { lo_comp, hi_comp };
      for (unsigned i = 0; i < 2; i++) {
         nir_tex_instr *g = nir_tex_instr_create(b->shader, tex->num_srcs);
         g->op = nir_texop_tg4;
         g->coord_components = tex->coord_components;
         g->sampler_dim = tex->sampler_dim;
         g->is_array = tex->is_array;
         g->dest_type = nir_type_uint32;
         g->texture_index = sampler_idx;
         g->sampler_index = sampler_idx;
         g->component = comps[i];

         for (unsigned j = 0; j < tex->num_srcs; j++) {
            g->src[j].src = nir_src_for_ssa(tex->src[j].src.ssa);
            g->src[j].src_type = tex->src[j].src_type;
         }

         nir_def_init(&g->instr, &g->def,
                      nir_tex_instr_dest_size(g), tex->def.bit_size);
         nir_builder_instr_insert(b, &g->instr);
         halves[i] = &g->def;
      }

      nir_def *combined = nir_ior(b, halves[0], nir_ishl_imm(b, halves[1], 16));
      nir_def_rewrite_uses_after(&tex->def, combined);
      nir_instr_remove(&tex->instr);
      return true;
   }

   b->cursor = nir_after_instr(&tex->instr);

   nir_tex_instr *clone =
      nir_instr_as_tex(nir_instr_clone(b->shader, &tex->instr));
   clone->sampler_index = companion;
   clone->texture_index = companion;
   nir_builder_instr_insert(b, &clone->instr);

   nir_def *combined = nir_vec4(b,
                                nir_channel(b, &tex->def, 0),
                                nir_channel(b, &tex->def, 1),
                                nir_channel(b, &clone->def, 0),
                                nir_channel(b, &clone->def, 1));

   nir_def_rewrite_uses_after(&tex->def, combined);

   return true;
}

bool
etna_nir_lower_128bit(nir_shader *s, struct etna_shader_key *key)
{
   bool progress = false;
   const bool is_fs = s->info.stage == MESA_SHADER_FRAGMENT;

   if (!is_fs && s->info.stage != MESA_SHADER_VERTEX)
      return false;

   const bool has_rt = is_fs && key->has_128bit_rt;

   if (!has_rt && !key->tex_is_128bit)
      return false;

   struct lower_128bit_data data = {
      .key = key,
   };

   if (has_rt) {
      nir_foreach_shader_out_variable(var, s) {
         if (var->data.location < FRAG_RESULT_DATA0 ||
             var->data.location >= FRAG_RESULT_DATA0 + ETNA_MAX_128BIT_RTS)
            continue;

         const unsigned rt_index = var->data.location - FRAG_RESULT_DATA0;
         if (!(key->rt_is_128bit & (1 << rt_index)))
            continue;

         data.companion_var[rt_index] =
            get_or_create_companion_var(s, var, key->rt_companion[rt_index]);
         var->type = vec4_of_same_base(var->type);
      }

      NIR_PASS(progress, s, nir_shader_intrinsics_pass, lower_128bit_output,
         nir_metadata_control_flow, &data);
   }

   if (key->tex_is_128bit)
      NIR_PASS(progress, s, nir_shader_tex_pass, lower_128bit_sampler,
         nir_metadata_control_flow, &data);

   return progress;
}
