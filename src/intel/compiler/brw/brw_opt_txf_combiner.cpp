/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_eu.h"
#include "brw_shader.h"
#include "brw_builder.h"

static unsigned
dest_comps_for_txf(const brw_shader &s, const brw_inst *txf)
{
   if (!txf)
      return 0;

   const unsigned grf_size = REG_SIZE * reg_unit(s.devinfo);
   const unsigned per_component_regs =
      DIV_ROUND_UP(brw_type_size_bytes(txf->dst.type) *
                   txf->exec_size, grf_size);
   const unsigned dest_regs = txf->size_written / grf_size;
   const unsigned dest_comps = dest_regs / per_component_regs;
   return dest_comps;
}

static bool
is_def(const brw_def_analysis &defs, const brw_reg &r)
{
   return r.file == IMM || r.file == BAD_FILE || defs.get(r) != NULL;
}

static bool
is_uniform_def(const brw_def_analysis &defs, const brw_reg &r)
{
   return is_def(defs, r) && is_uniform(r);
}

/**
 * Check if two texture instructions have a matching source (either the same
 * immediate value, or both references to the same immutable SSA def and
 * with matching source modifiers and regions).
 */
static bool
sources_match(ASSERTED const brw_def_analysis &defs,
              const brw_inst *a, const brw_inst *b, enum tex_logical_srcs src)
{
   assert(is_def(defs, a->src[src]));
   assert(is_def(defs, b->src[src]));
   return brw_regs_equal(&a->src[src], &b->src[src]);
}

static void
merge_instructions(brw_shader &s, brw_tex_inst **txfs, unsigned count)
{
   const unsigned min_simd = 8 * reg_unit(s.devinfo);
   const unsigned max_simd = 16 * reg_unit(s.devinfo);
   const unsigned grf_size = REG_SIZE * reg_unit(s.devinfo);

   for (unsigned curr = 0; curr < count; curr += max_simd) {
      const unsigned lanes = CLAMP(count - curr, min_simd, max_simd);
      const unsigned width = util_next_power_of_two(lanes);
      const brw_builder ubld =
         brw_builder(&s).before(txfs[curr]).exec_all().group(width, 0);
      const brw_builder ubld1 = ubld.group(1, 0);

      enum brw_reg_type coord_type =
         txfs[curr]->src[TEX_LOGICAL_SRC_PAYLOAD0].type;
      brw_reg coord = ubld.vgrf(coord_type);
      brw_reg coord_comps[32];

      for (unsigned i = 0; i < width; i++) {
            /* Our block size might be larger than the number of convergent
             * loads we're combining.  If so, repeat the last component.
             */
         if (txfs[curr+i])
            coord_comps[i] = txfs[curr+i]->src[TEX_LOGICAL_SRC_PAYLOAD0];
         else
            coord_comps[i] = coord_comps[i-1];
         }
      ubld1.VEC(coord, coord_comps, width);

      brw_reg srcs[TEX_LOGICAL_NUM_SRCS];
      srcs[TEX_LOGICAL_SRC_SURFACE] = txfs[0]->src[TEX_LOGICAL_SRC_SURFACE];
      srcs[TEX_LOGICAL_SRC_SAMPLER] = brw_imm_ud(0);
      srcs[TEX_LOGICAL_SRC_PAYLOAD0] = coord;
      for (unsigned i = TEX_LOGICAL_SRC_PAYLOAD1; i < txfs[0]->sources; i++)
         srcs[i] = txfs[0]->src[i];

      /* Each of our txf may have a reduced response length if some
       * components are never read.  Use the maximum of the sizes.
       */
      unsigned new_dest_comps = 0;
      for (unsigned i = 0; i < width; i++) {
         const unsigned this_comps = dest_comps_for_txf(s, txfs[curr+i]);
         new_dest_comps = MAX2(new_dest_comps, this_comps);
      }

      /* Emit the new divergent TXF */
      brw_reg div = ubld.vgrf(BRW_TYPE_UD, new_dest_comps);
      brw_tex_inst *div_txf =
         ubld.emit(SHADER_OPCODE_SAMPLER, div, srcs, txfs[0]->sources)->as_tex();
      div_txf->surface_bindless = txfs[0]->surface_bindless;
      div_txf->sampler_opcode = txfs[0]->sampler_opcode;
      div_txf->residency = false;

      /* Update it to also use response length reduction */
      const unsigned per_component_regs =
         DIV_ROUND_UP(brw_type_size_bytes(div.type) * div_txf->exec_size,
                      grf_size);
      div_txf->size_written = new_dest_comps * per_component_regs * grf_size;

      for (unsigned i = 0; i < width; i++) {
         brw_inst *txf = txfs[curr+i];
         if (!txf)
            break;

         const brw_builder ibld = brw_builder(txf);

         /* Replace each of the original TXFs with MOVs from our new one */
         const unsigned dest_comps = dest_comps_for_txf(s, txf);
         assert(dest_comps <= 4);

         brw_reg v[4];
         for (unsigned c = 0; c < dest_comps; c++)
            v[c] = component(offset(div, ubld, c), i);
         ibld.VEC(retype(txf->dst, BRW_TYPE_UD), v, dest_comps);

         txf->remove();
      }
   }
}

/**
 * Look for a series of convergent texture buffer fetches within a basic
 * block and combine them into a single divergent load with one lane for
 * each original fetch.  For example, this series of convergent fetches:
 *
 *   txf(16) %12:UD, coord = 12d, lod = 0u, handle = %1<0>:D
 *   txf(16) %13:UD, coord = 13d, lod = 0u, handle = %1<0>:D
 *   txf(16) %14:UD, coord = 14d, lod = 0u, handle = %1<0>:D
 *   txf(16) %15:UD, coord = 15d, lod = 0u, handle = %1<0>:D
 *   txf(16) %16:UD, coord = 16d, lod = 0u, handle = %1<0>:D
 *   txf(16) %17:UD, coord = 17d, lod = 0u, handle = %1<0>:D
 *   txf(16) %18:UD, coord = 18d, lod = 0u, handle = %1<0>:D
 *   txf(16) %19:UD, coord = 19d, lod = 0u, handle = %1<0>:D
 *
 * can be combined into a single divergent load and scalar-expansion moves
 * (which can easily be copy propagated away):
 *
 *   load_payload(1) %2:D 12d, 13d, 14d, 15d, 16d, 17d, 18d, 19d
 *   txf(8) %3:UD, coord = %2, lod = 0u, handle = %1<0>:D
 *   mov(16) %12:UD, %3+0.0<0>:UD
 *   ...
 *   mov(16) %19:UD, %3+0.28<0>:UD
 *
 * Our sampler hardware doesn't have any special support for convergent
 * loads (like LSC transpose/block loads), and always performs SIMD8/16/32
 * per-channel loads.  But with this trick, we can still combine multiple
 * convergent loads into a single message with fewer round-trips, and much
 * lower register pressure.
 */
bool
brw_opt_combine_convergent_txf(brw_shader &s)
{
   const brw_def_analysis &defs = s.def_analysis.require();

   bool progress = false;

   foreach_block(block, s.cfg) {
      /* Gather a list of convergent TXFs to the same surface in this block */
      brw_tex_inst *txfs_ld[32] = {};
      brw_tex_inst *txfs_ld_lz[32] = {};
      unsigned ld_count = 0;
      unsigned ld_lz_count = 0;

      foreach_inst_in_block(brw_inst, inst, block) {
         brw_tex_inst *tex = inst->as_tex();
         if (tex == NULL)
            continue;

         if (tex->sampler_opcode != BRW_SAMPLER_OPCODE_LD &&
             tex->sampler_opcode != BRW_SAMPLER_OPCODE_LD_LZ)
            continue;

         /* Only handle buffers or single miplevel 1D images for now */
         if (tex->coord_components > 1)
            continue;

         if (tex->residency)
            continue;

         if (tex->predicate || tex->force_writemask_all)
            continue;

         if (!is_uniform_def(defs, tex->src[TEX_LOGICAL_SRC_SURFACE]))
            continue;

         /* Only handle immediates for now: we could check is_uniform(),
          * but we'd need to ensure the coordinate's definition reaches
          * txfs[0] which is where we'll insert the combined coordinate.
          */
         if (tex->src[TEX_LOGICAL_SRC_PAYLOAD0].file != IMM)
            continue;

         brw_tex_inst *tex0 = tex->sampler_opcode == BRW_SAMPLER_OPCODE_LD ?
            txfs_ld[0] : txfs_ld_lz[0];

         if (tex0 != NULL) {
            if (!sources_match(defs, tex, tex0, TEX_LOGICAL_SRC_SURFACE) ||
                tex->surface_bindless != tex0->surface_bindless)
               continue;

            if (tex->sampler_opcode == BRW_SAMPLER_OPCODE_LD) {
               if (ld_count > 0 &&
                   !sources_match(defs, tex, tex0, TEX_LOGICAL_SRC_PAYLOAD2))
                  continue;
            }
         }

         if (tex->sampler_opcode == BRW_SAMPLER_OPCODE_LD)
            txfs_ld[ld_count++] = tex;
         if (tex->sampler_opcode == BRW_SAMPLER_OPCODE_LD_LZ)
            txfs_ld_lz[ld_lz_count++] = tex;

         if (ld_count == ARRAY_SIZE(txfs_ld) ||
             ld_lz_count == ARRAY_SIZE(txfs_ld_lz))
            break;
      }

      /* Emit divergent TXFs and replace the original ones with MOVs */
      if (ld_count >= 2) {
         merge_instructions(s, txfs_ld, ld_count);
         progress = true;
      }
      if (ld_lz_count >= 2) {
         merge_instructions(s, txfs_ld_lz, ld_lz_count);
         progress = true;
      }
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS);

   return progress;
}
