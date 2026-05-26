/*
 * Copyright (C) 2026 Arm Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "va_compiler.h"
#include "valhall_enums.h"

static bool
reads_rasterizer_coverage(nir_builder *b, nir_intrinsic_instr *intrin,
                          void *data)
{
   bool *rasterizer_coverage_read = data;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_sample_mask_in:
      *rasterizer_coverage_read = true;
      break;
   default:
      break;
   }

   return false;
}

static void
walk_nir_intrinsics(bi_context *ctx, struct pan_shader_info *info)
{
   bool rasterizer_coverage_read = false;

   nir_shader_intrinsics_pass(ctx->nir, reads_rasterizer_coverage,
                              nir_metadata_all, &rasterizer_coverage_read);

   info->fs.hsr.rasterizer_coverage_read = rasterizer_coverage_read;
}

static void
walk_bir_shader(bi_context *ctx, struct pan_shader_info *info)
{
   bool found_atest = false;
   bool found_zsemit = false;

   bool wait_or_tile_acces_before_atest = false;
   bool wait_or_tile_acces_before_zsemit = false;
   bool varying_before_atest = false;
   bool varying_before_zsemit = false;

   /* Walk the shader, gathering required info. */
   bi_foreach_block(ctx, block) {
      bi_foreach_instr_in_block(block, instr) {
         switch (instr->op) {
         case BI_OPCODE_LD_TILE:
            info->fs.hsr.ld_tile = true;
            FALLTHROUGH;
         case BI_OPCODE_ST_TILE:
         case BI_OPCODE_BLEND:
            if (!found_atest)
               wait_or_tile_acces_before_atest = true;
            if (!found_zsemit)
               wait_or_tile_acces_before_zsemit = true;
            break;
         case BI_OPCODE_LD_VAR_SPECIAL:
            if (instr->sample == BI_SAMPLE_CENTROID)
               info->fs.hsr.centroid_interpolation = true;
            /* LD_VAR_SPECIAL should not fallthrough */
            break;
         case BI_OPCODE_LD_VAR:
         case BI_OPCODE_LD_VAR_IMM:
         case BI_OPCODE_LD_VAR_BUF_F16:
         case BI_OPCODE_LD_VAR_BUF_F32:
         case BI_OPCODE_LD_VAR_BUF_IMM_F16:
         case BI_OPCODE_LD_VAR_BUF_IMM_F32:
         case BI_OPCODE_VAR_TEX_F16:
         case BI_OPCODE_VAR_TEX_F32:
            if (instr->sample == BI_SAMPLE_CENTROID)
               info->fs.hsr.centroid_interpolation = true;
            FALLTHROUGH;
         case BI_OPCODE_LD_VAR_BUF_FLAT:
         case BI_OPCODE_LD_VAR_BUF_FLAT_IMM:
         case BI_OPCODE_LD_VAR_FLAT:
         case BI_OPCODE_LD_VAR_FLAT_IMM:
            if (!found_atest)
               varying_before_atest = true;
            if (!found_zsemit)
               varying_before_zsemit = true;
            break;
         case BI_OPCODE_ATEST:
            found_atest = true;
            break;
         case BI_OPCODE_ZS_EMIT:
            found_zsemit = true;
            break;
         default:
            break;
         }

         if ((instr->flow == VA_FLOW_WAIT ||
              instr->flow == VA_FLOW_WAIT_RESOURCE)) {
            if (!found_atest)
               wait_or_tile_acces_before_atest = true;
            if (!found_zsemit)
               wait_or_tile_acces_before_zsemit = true;
         }
      }
   }

   info->fs.hsr.varying_before_atest_zsemit =
      (varying_before_atest && found_atest) ||
      (varying_before_zsemit && found_zsemit);

   info->fs.hsr.wait_or_tile_access_before_atest_zsemit =
      (wait_or_tile_acces_before_atest && found_atest) ||
      (wait_or_tile_acces_before_zsemit && found_zsemit);
}

void
va_gather_hsr_info(bi_context *ctx, struct pan_shader_info *info)
{
   if (ctx->stage != MESA_SHADER_FRAGMENT)
      return;

   walk_nir_intrinsics(ctx, info);
   walk_bir_shader(ctx, info);
}
