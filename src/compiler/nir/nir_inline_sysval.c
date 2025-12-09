/*
 * Copyright (C) 2025 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir_builder.h"

struct ctx {
   nir_intrinsic_op op;
   uint64_t imm;
};

static bool
pass(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   struct ctx *ctx = data;
   if (intr->intrinsic != ctx->op)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def_replace(&intr->def, nir_imm_intN_t(b, ctx->imm, intr->def.bit_size));
   return true;
}

bool
nir_inline_sysval(nir_shader *shader, nir_intrinsic_op op, uint64_t imm)
{
   struct ctx ctx = { .op = op, .imm = imm };
   return nir_shader_intrinsics_pass(shader, pass, nir_metadata_control_flow,
                                     &ctx);
}
