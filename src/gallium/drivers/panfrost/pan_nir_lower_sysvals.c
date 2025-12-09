/*
 * Copyright (C) 2020-2023 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "compiler/nir/nir_builder.h"
#include "pan_context.h"

struct ctx {
   unsigned arch;
   struct panfrost_sysvals *sysvals;
   struct hash_table_u64 *sysval_to_id;
};

static unsigned
lookup_sysval(struct hash_table_u64 *sysval_to_id,
              struct panfrost_sysvals *sysvals, int sysval)
{
   /* Try to lookup */
   void *cached = _mesa_hash_table_u64_search(sysval_to_id, sysval);

   if (cached) {
      unsigned id = ((uintptr_t)cached) - 1;
      assert(id < MAX_SYSVAL_COUNT);
      assert(sysvals->sysvals[id] == sysval);
      return id;
   }

   /* Else assign */
   unsigned id = sysvals->sysval_count++;
   assert(id < MAX_SYSVAL_COUNT);
   _mesa_hash_table_u64_insert(sysval_to_id, sysval,
                               (void *)((uintptr_t)id + 1));
   sysvals->sysvals[id] = sysval;
   return id;
}

static unsigned
sysval_for_intrinsic(unsigned arch, nir_intrinsic_instr *intr, unsigned *offset)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_ssbo_address:
      if (arch >= 9)
         return ~0;

      assert(nir_src_as_uint(intr->src[1]) == 0);
      return PAN_SYSVAL(SSBO, nir_src_as_uint(intr->src[0]));
   case nir_intrinsic_get_ssbo_size:
      *offset = 8;
      return PAN_SYSVAL(SSBO, nir_src_as_uint(intr->src[0]));

   case nir_intrinsic_load_sampler_lod_parameters:
      /* This is only used for a workaround on Mali-T720, where we don't
       * support dynamic samplers.
       */
      return PAN_SYSVAL(SAMPLER, nir_src_as_uint(intr->src[0]));

   case nir_intrinsic_load_xfb_address:
      return PAN_SYSVAL(XFB, nir_intrinsic_base(intr));

   case nir_intrinsic_load_work_dim:
      return PAN_SYSVAL_WORK_DIM;

   case nir_intrinsic_load_sample_positions_pan:
      return PAN_SYSVAL_SAMPLE_POSITIONS;

   case nir_intrinsic_load_num_vertices:
      return PAN_SYSVAL_NUM_VERTICES;

   case nir_intrinsic_load_raw_vertex_offset_pan:
      return PAN_SYSVAL_VERTEX_INSTANCE_OFFSETS;
   case nir_intrinsic_load_base_vertex:
      *offset = 4;
      return PAN_SYSVAL_VERTEX_INSTANCE_OFFSETS;
   case nir_intrinsic_load_base_instance:
      *offset = 8;
      return PAN_SYSVAL_VERTEX_INSTANCE_OFFSETS;

   case nir_intrinsic_load_draw_id:
      if (arch >= 10)
         return ~0;

      return PAN_SYSVAL_DRAWID;

   case nir_intrinsic_load_multisampled_pan:
      return PAN_SYSVAL_MULTISAMPLED;

   case nir_intrinsic_load_viewport_scale:
      return PAN_SYSVAL_VIEWPORT_SCALE;

   case nir_intrinsic_load_viewport_offset:
      return PAN_SYSVAL_VIEWPORT_OFFSET;

   case nir_intrinsic_load_num_workgroups:
      return PAN_SYSVAL_NUM_WORK_GROUPS;

   case nir_intrinsic_load_workgroup_size:
      return PAN_SYSVAL_LOCAL_GROUP_SIZE;

   case nir_intrinsic_load_printf_buffer_address:
      return PAN_SYSVAL_PRINTF_BUFFER;

   case nir_intrinsic_load_blend_const_color_rgba:
      return PAN_SYSVAL_BLEND_CONSTANTS;

   case nir_intrinsic_load_rt_conversion_pan: {
      unsigned size = nir_alu_type_get_type_size(nir_intrinsic_src_type(intr));
      unsigned rt = nir_intrinsic_base(intr);

      return PAN_SYSVAL(RT_CONVERSION, rt | (size << 4));
   }

   case nir_intrinsic_image_size: {
      uint32_t uindex = nir_src_as_uint(intr->src[0]);
      bool is_array = nir_intrinsic_image_array(intr);
      unsigned dim = nir_intrinsic_dest_components(intr) - is_array;

      return PAN_SYSVAL(IMAGE_SIZE, PAN_TXS_SYSVAL_ID(uindex, dim, is_array));
   }

   default:
      return ~0;
   }
}

static bool
uses_sysvals(unsigned arch, nir_shader *shader)
{
   /* Fragment shaders always use the blend constant sysval */
   if (shader->info.stage == MESA_SHADER_FRAGMENT)
      return true;

   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type == nir_instr_type_intrinsic) {
               nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
               unsigned offset;
               if (sysval_for_intrinsic(arch, intr, &offset) != ~0)
                  return true;
            } else if (instr->type == nir_instr_type_tex) {
               nir_tex_instr *tex = nir_instr_as_tex(instr);
               if (tex->op == nir_texop_txs)
                  return true;
            }
         }
      }
   }
   return false;
}

/* Move all UBO indexes after PAN_UBO_SYSVALS up by one to make space for the
 * sysval UBO */
static bool
remap_load_ubo(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_src *old_idx = &intr->src[0];

   if (nir_src_is_const(*old_idx)) {
      if (nir_src_as_uint(*old_idx) < PAN_UBO_SYSVALS)
         return false;
   } else {
      /* Assume that all dynamic indices are into normal UBOs, not the default
       * UBO0, and so should be adjusted */
      assert(b->shader->info.first_ubo_is_default_ubo);
   }

   nir_src_rewrite(old_idx, nir_iadd_imm(b, old_idx->ssa, 1));
   return true;
}

static bool
lower(nir_builder *b, nir_instr *instr, void *data)
{
   struct ctx *ctx = data;
   nir_def *old = NULL;
   unsigned sysval = ~0, offset = 0;
   b->cursor = nir_before_instr(instr);

   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

      if (intr->intrinsic == nir_intrinsic_load_ubo) {
         return remap_load_ubo(b, intr);
      }

      old = &intr->def;
      sysval = sysval_for_intrinsic(ctx->arch, intr, &offset);

      if (sysval == ~0)
         return false;
   } else if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      old = &tex->def;

      if (tex->op != nir_texop_txs)
         return false;

      /* XXX: This is broken for dynamic indexing */
      sysval = PAN_SYSVAL(TEXTURE_SIZE,
                          PAN_TXS_SYSVAL_ID(tex->texture_index,
                                            nir_tex_instr_dest_size(tex) -
                                               (tex->is_array ? 1 : 0),
                                            tex->is_array));
   } else {
      return false;
   }

   unsigned vec4_index = lookup_sysval(ctx->sysval_to_id, ctx->sysvals, sysval);
   unsigned ubo_offset = (vec4_index * 16) + offset;

   b->cursor = nir_after_instr(instr);
   nir_def *val = nir_load_ubo(
      b, old->num_components, old->bit_size, nir_imm_int(b, PAN_UBO_SYSVALS),
      nir_imm_int(b, ubo_offset), .align_mul = old->bit_size / 8,
      .align_offset = 0, .range_base = offset, .range = old->bit_size / 8);
   nir_def_rewrite_uses(old, val);
   return true;
}

bool
panfrost_nir_lower_sysvals(nir_shader *shader, unsigned arch,
                           struct panfrost_sysvals *sysvals)
{
   bool progress = false;

   /* The lowerings for SSBOs, etc require constants, so fold now */
   do {
      progress = false;

      NIR_PASS(progress, shader, nir_opt_copy_prop);
      NIR_PASS(progress, shader, nir_opt_constant_folding);
      NIR_PASS(progress, shader, nir_opt_dce);
   } while (progress);

   if (!uses_sysvals(arch, shader))
      return progress;

   struct ctx ctx = {
      .arch = arch,
      .sysvals = sysvals,
      .sysval_to_id = _mesa_hash_table_u64_create(NULL),
   };

   memset(sysvals, 0, sizeof(*sysvals));

   /* If we don't have any UBOs, we need to insert an empty UBO0 to put the
    * sysval at UBO1 */
   shader->info.num_ubos = MAX2(PAN_UBO_SYSVALS, shader->info.num_ubos) + 1;

   /* Reserve the first slot for blend constants, so that they can be accessed
    * from a fixed offset in the blend shader */
   if (shader->info.stage == MESA_SHADER_FRAGMENT)
      lookup_sysval(ctx.sysval_to_id, ctx.sysvals, PAN_SYSVAL_BLEND_CONSTANTS);

   nir_shader_instructions_pass(
      shader, lower, nir_metadata_control_flow, &ctx);

   _mesa_hash_table_u64_destroy(ctx.sysval_to_id);
   return true;
}
