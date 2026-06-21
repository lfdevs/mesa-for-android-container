/*
 * Copyright © 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "intel_decoder.h"
#include "intel_decoder_private.h"

#include "compiler/gen/gen.h"

#include "util/ralloc.h"

static void
ctx_disassemble_program_gen(struct intel_batch_decode_ctx *ctx,
                            uint32_t ksp,
                            const char *short_name,
                            const char *name)
{
   uint64_t addr = ctx->instruction_base + ksp;
   struct intel_batch_decode_bo bo = ctx_get_bo(ctx, true, addr);
   if (!bo.map)
      return;

   fprintf(ctx->fp, "\nReferenced %s (ksp: 0x%" PRIx32, name, ksp);
   if (ctx->shader_hash.last_inst &&
       !strcmp(ctx->shader_hash.last_inst->name, "MI_STORE_DATA_IMM")) {
      /* We only consider a recorded hash valid when the previously parsed
       * instruction is the token.
       */
      fprintf(ctx->fp, " hash: 0x%" PRIx64 "): \n", ctx->shader_hash.hash);
      /* FIXME: Only the hash of the first shader is available now.
       *
       * For a targeted shader submission command which can have more than
       * one shader, the dummy MI_STORE_DATA_IMM token instruction only
       * contains the hash of the first shader. In this case, we invalidate
       * the last instruction here so that the following shaders won't be
       * printed out with the hash of the first.
       */
      ctx->shader_hash.last_inst = NULL;
   } else {
      fprintf(ctx->fp, "):\n");
   }

   const int size = gen_find_shader_size(&ctx->devinfo, bo.map, 0, bo.size);
   if (size > 0) {
      void *tmp_ctx = ralloc_context(NULL);

      gen_decode_params decode = {
         .devinfo = &ctx->devinfo,
         .raw_bytes = bo.map,
         .raw_bytes_size = size,
         .mem_ctx = tmp_ctx,
      };
      gen_decode(&decode);

      gen_print_params print = {
         .devinfo = &ctx->devinfo,
         .fp = ctx->fp,
         .insts = decode.insts,
         .num_insts = decode.num_insts,
         .errors = decode.errors,
         .num_errors = decode.num_errors,
         .raw_bytes = bo.map,
         .raw_bytes_size = size,
      };
      gen_print(&print);

      ralloc_free(tmp_ctx);
   }

   if (ctx->shader_binary) {
      ctx->shader_binary(ctx->user_data, short_name, addr,
                         bo.map, size);
   }
}

void
intel_batch_decode_ctx_init_gen(struct intel_batch_decode_ctx *ctx,
                                const struct intel_device_info *devinfo,
                                FILE *fp, enum intel_batch_decode_flags flags,
                                const char *xml_path,
                                struct intel_batch_decode_bo (*get_bo)(void *,
                                                                       bool,
                                                                       uint64_t),
                                unsigned (*get_state_size)(void *, uint64_t,
                                                           uint64_t),
                                void *user_data)
{
   intel_batch_decode_ctx_init(ctx, devinfo, fp, flags, xml_path,
                               get_bo, get_state_size, user_data);
   ctx->disassemble_program = ctx_disassemble_program_gen;
}
