/*
 * Copyright (C) 2022-2023 Collabora, Ltd.
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

#include "util/bitset.h"
#include "util/hash_table.h"
#include "util/list.h"
#include "util/ralloc.h"

#include "genxml/gen_macros.h"
#include "decode.h"

#if PAN_ARCH >= 10

#include "genxml/cs_builder.h"

/* Limit for Mali-G610. -1 because we're not including the active frame */
#define MAX_CALL_STACK_DEPTH (8 - 1)

#define cs_unpack(packed, T, unpacked) pan_cast_and_unpack(packed, T, unpacked)

struct queue_ctx {
   /* Size of CSHWIF register file in 32-bit registers */
   unsigned nr_regs;

   /* CSHWIF register file */
   uint32_t *regs;

   /* Current instruction pointer (CPU pointer for convenience) */
   uint64_t *ip;

   /* Current instruction end pointer */
   uint64_t *end;

   /* Whether currently inside an exception handler */
   bool in_exception_handler;

   /* Call stack. Depth=0 means root */
   struct {
      /* Link register to return to */
      uint64_t *lr;

      /* End pointer, there is a return (or exit) after */
      uint64_t *end;
   } call_stack[MAX_CALL_STACK_DEPTH + 1]; /* +1 for exception handler */
   uint8_t call_stack_depth;

   unsigned gpu_id;
};

static void
print_indirect(unsigned address, int16_t offset, FILE *fp)
{
   if (offset)
      fprintf(fp, "[d%u + %d]", address, offset);
   else
      fprintf(fp, "[d%u]", address);
}

static void
print_reg_tuple(unsigned base, uint16_t mask, FILE *fp)
{
   bool first_reg = true;

   u_foreach_bit(i, mask) {
      fprintf(fp, "%sr%u", first_reg ? "" : ":", base + i);
      first_reg = false;
   }

   if (mask == 0)
      fprintf(fp, "_");
}

static const char *conditions_str[] = {
   "le", "gt", "eq", "ne", "lt", "ge", "always",
};

static void
print_cs_instr(FILE *fp, const uint64_t *instr)
{
   cs_unpack(instr, CS_BASE, base);
   switch (base.opcode) {
   case MALI_CS_OPCODE_NOP: {
      cs_unpack(instr, CS_NOP, I);
      if (I.ignored)
         fprintf(fp, "NOP // 0x%" PRIX64, I.ignored);
      else
         fprintf(fp, "NOP");
      break;
   }

   case MALI_CS_OPCODE_MOVE48: {
      cs_unpack(instr, CS_MOVE48, I);
      fprintf(fp, "MOVE48 d%u, #0x%" PRIX64, I.destination, I.immediate);
      break;
   }

   case MALI_CS_OPCODE_MOVE32: {
      cs_unpack(instr, CS_MOVE32, I);
      fprintf(fp, "MOVE32 r%u, #0x%" PRIX64, I.destination, I.immediate);
      break;
   }

   case MALI_CS_OPCODE_WAIT: {
      cs_unpack(instr, CS_WAIT, I);
      fprintf(fp, "WAIT%s #%x", I.progress_increment ? ".progress_inc" : "",
              I.wait_mask);
      break;
   }

   case MALI_CS_OPCODE_RUN_COMPUTE: {
      const char *axes[4] = {"x_axis", "y_axis", "z_axis"};
      cs_unpack(instr, CS_RUN_COMPUTE, I);

      /* Print the instruction. Ignore the selects and the flags override
       * since we'll print them implicitly later.
       */
      fprintf(fp, "RUN_COMPUTE%s.%s.srt%d.spd%d.tsd%d.fau%d #%u",
              I.progress_increment ? ".progress_inc" : "", axes[I.task_axis],
              I.srt_select, I.spd_select, I.tsd_select, I.fau_select,
              I.task_increment);
      break;
   }

#if PAN_ARCH == 10
   case MALI_CS_OPCODE_RUN_TILING: {
      cs_unpack(instr, CS_RUN_TILING, I);
      fprintf(fp, "RUN_TILING%s.srt%d.spd%d.tsd%d.fau%d",
              I.progress_increment ? ".progress_inc" : "", I.srt_select,
              I.spd_select, I.tsd_select, I.fau_select);
      break;
   }
#endif

#if PAN_ARCH < 12
   case MALI_CS_OPCODE_RUN_IDVS: {
      cs_unpack(instr, CS_RUN_IDVS, I);
      fprintf(
         fp,
         "RUN_IDVS%s%s%s.varying_srt%d.varying_fau%d.varying_tsd%d.frag_srt%d.frag_tsd%d r%u, #%" PRIx64,
         I.progress_increment ? ".progress_inc" : "",
         I.malloc_enable ? "" : ".no_malloc",
         I.draw_id_register_enable ? ".draw_id_enable" : "",
         I.varying_srt_select, I.varying_fau_select, I.varying_tsd_select,
         I.fragment_srt_select, I.fragment_tsd_select, I.draw_id,
         I.flags_override);
      break;
   }
#else
   case MALI_CS_OPCODE_RUN_IDVS2: {
      cs_unpack(instr, CS_RUN_IDVS2, I);

      const char *vertex_shading_str[] = {
         ".early",
         ".deferred",
         ".INVALID",
         ".INVALID",
      };

      fprintf(fp, "RUN_IDVS2%s%s%s%s r%u, #%" PRIx64,
              I.progress_increment ? ".progress_inc" : "",
              I.malloc_enable ? "" : ".no_malloc",
              I.draw_id_register_enable ? ".draw_id_enable" : "",
              vertex_shading_str[I.vertex_shading_mode], I.draw_id,
              I.flags_override);
      break;
   }
#endif

#if PAN_ARCH >= 13
   case MALI_CS_OPCODE_ARITH_IMM32: {
      cs_unpack(instr, CS_ARITH_IMM32_BASE, I);

      const char *instr_name[] = {
         "ADD_IMM32", "LSHIFT_IMM32", "RSHIFT_IMM_U32", "RSHIFT_IMM_S32",
         "BFEXT_U32", "BFEXT_S32",    "BFINS_IMM32",    "UMIN_IMM32",
      };

      fprintf(fp, "%s r%u, r%u, #%" PRId64, instr_name[I.sub_opcode],
              I.destination, I.source, I.immediate);
      break;
   }

   case MALI_CS_OPCODE_ARITH_IMM64: {
      cs_unpack(instr, CS_ARITH_IMM64_BASE, I);

      const char *instr_name[] = {
         "ADD_IMM64", "LSHIFT_IMM64", "RSHIFT_IMM_U64", "RSHIFT_IMM_S64",
         "BFEXT_U64", "BFEXT_S64",    "BFINS_IMM64",    "UMIN_IMM64",
      };

      fprintf(fp, "%s d%u, d%u, #%" PRId64, instr_name[I.sub_opcode],
              I.destination, I.source, I.immediate);
      break;
   }

   case MALI_CS_OPCODE_ARITH_REG32: {
      cs_unpack(instr, CS_ARITH_REG32_BASE, I);

      const char *instr_name[] = {
         "UMIN32",     "ADD32",      "SUB32",   "LSHIFT32",
         "RSHIFT_U32", "RSHIFT_S32", "BFINS32",
      };

      fprintf(fp, "%s r%u, r%u, r%u", instr_name[I.sub_opcode], I.destination,
              I.source_1, I.source_0);

      break;
   }

   case MALI_CS_OPCODE_ARITH_REG64: {
      cs_unpack(instr, CS_ARITH_REG64_BASE, I);

      const char *instr_name[] = {
         "UMIN64",     "ADD64",      "SUB64",   "LSHIFT64",
         "RSHIFT_U64", "RSHIFT_S64", "BFINS64",
      };

      fprintf(fp, "%s d%u, d%u, d%u", instr_name[I.sub_opcode], I.destination,
              I.source_1, I.source_0);

      break;
   }
#endif

#if PAN_ARCH >= 11
   case MALI_CS_OPCODE_LOGIC_OP32: {
      cs_unpack(instr, CS_LOGIC_OP32, I);

      const char *mode_name[] = {
         ".CLEAR", ".AND",     ".AND_A_NB", ".MOV_A", ".AND_NA_B", ".MOV_B",
         ".XOR",   ".OR",      ".NOR",      ".XNOR",  ".NOT_B",    ".OR_A_NB",
         ".NOT_A", ".OR_NA_B", ".NAND",     ".SET",
      };

      const char *index_name[] = {
         ".direct",
         ".index",
      };

      fprintf(fp, "LOGIC_OP32%s r%u, r%u, r%u%s", mode_name[I.mode],
              I.destination, I.source_0, I.source_1, index_name[I.index]);

      break;
   }

   case MALI_CS_OPCODE_NEXT_SB_ENTRY: {
      cs_unpack(instr, CS_NEXT_SB_ENTRY, I);

      const char *sb_type_name[] = {
         ".no_change", ".endpoint", ".other",   ".deferred",
         ".INVALID",   ".INVALID",  ".INVALID", ".INVALID",
         ".INVALID",   ".INVALID",  ".INVALID", ".INVALID",
         ".INVALID",   ".INVALID",  ".INVALID", ".INVALID",
      };

      const char *format_name[] = {".index", ".mask"};

      fprintf(fp, "NEXT_SB_ENTRY%s%s r%u", sb_type_name[I.sb_type],
              format_name[I.format], I.destination);

      break;
   }

   case MALI_CS_OPCODE_SET_STATE: {
      cs_unpack(instr, CS_SET_STATE, I);

      const char *state_name[] = {
         ".sb_sel_endpoint", ".sb_sel_other", ".sb_sel_deferred", ".INVALID",
         ".INVALID",         ".INVALID",      ".INVALID",         ".INVALID",
         ".sb_mask_stream",  ".sb_mask_wait",
      };

      const char *state =
         I.state <= sizeof(state_name) ? state_name[I.state] : ".INVALID";

      fprintf(fp, "SET_STATE%s r%u", state, I.source);
      break;
   }

   case MALI_CS_OPCODE_SET_STATE_IMM32: {
      cs_unpack(instr, CS_SET_STATE_IMM32, I);

      const char *state_name[] = {
         ".sb_sel_endpoint", ".sb_sel_other", ".sb_sel_deferred", ".INVALID",
         ".INVALID",         ".INVALID",      ".INVALID",         ".INVALID",
         ".sb_mask_stream",  ".sb_mask_wait",
      };

      const char *state =
         I.state <= sizeof(state_name) ? state_name[I.state] : ".INVALID";

      fprintf(fp, "SET_STATE_IMM32%s #%" PRIu64, state, I.value);
      break;
   }

   case MALI_CS_OPCODE_SHARED_SB_INC: {
      cs_unpack(instr, CS_SHARED_SB_INC, I);

      const char *progress_increment_name[] = {
         ".no_increment",
         ".increment",
      };

      const char *defer_mode_name[] = {
         ".defer_immediate",
         ".defer_indirect",
      };

      fprintf(fp, "SHARED_SB_INC%s%s #%u, #%u",
              progress_increment_name[I.progress_increment],
              defer_mode_name[I.defer_mode], I.sb_mask, I.shared_entry);
      break;
   }

   case MALI_CS_OPCODE_SHARED_SB_DEC: {
      cs_unpack(instr, CS_SHARED_SB_DEC, I);

      const char *progress_increment_name[] = {
         ".no_increment",
         ".increment",
      };

      fprintf(fp, "SHARED_SB_DEC%s #%u",
              progress_increment_name[I.progress_increment], I.shared_entry);
      break;
   }
#endif

   case MALI_CS_OPCODE_RUN_FRAGMENT: {
      static const char *tile_order[] = {
         "zorder",  "horizontal",     "vertical",     "unknown",
         "unknown", "rev_horizontal", "rev_vertical", "unknown",
         "unknown", "unknown",        "unknown",      "unknown",
         "unknown", "unknown",        "unknown",      "unknown",
      };
      cs_unpack(instr, CS_RUN_FRAGMENT, I);

      fprintf(fp, "RUN_FRAGMENT%s%s.tile_order=%s",
              I.progress_increment ? ".progress_inc" : "",
              I.enable_tem ? ".tile_enable_map_enable" : "",
              tile_order[I.tile_order]);
      break;
   }

   case MALI_CS_OPCODE_RUN_FULLSCREEN: {
      cs_unpack(instr, CS_RUN_FULLSCREEN, I);
      fprintf(fp, "RUN_FULLSCREEN%s r%u, #%" PRIx64,
              I.progress_increment ? ".progress_inc" : "", I.dcd,
              I.flags_override);
      break;
   }

   case MALI_CS_OPCODE_FINISH_TILING: {
      cs_unpack(instr, CS_FINISH_TILING, I);
      fprintf(fp, "FINISH_TILING%s",
              I.progress_increment ? ".progress_inc" : "");
      break;
   }

   case MALI_CS_OPCODE_FINISH_FRAGMENT: {
      cs_unpack(instr, CS_FINISH_FRAGMENT, I);
      fprintf(fp, "FINISH_FRAGMENT%s d%u, d%u, #%x, #%u",
              I.increment_fragment_completed ? ".frag_end" : "",
              I.last_heap_chunk, I.first_heap_chunk, I.wait_mask,
              I.signal_slot);
      break;
   }

#if PAN_ARCH < 13
   case MALI_CS_OPCODE_ADD_IMMEDIATE32: {
      cs_unpack(instr, CS_ADD_IMM32, I);

      fprintf(fp, "ADD_IMMEDIATE32 r%u, r%u, #%" PRId64, I.destination,
              I.source, I.immediate);
      break;
   }

   case MALI_CS_OPCODE_ADD_IMMEDIATE64: {
      cs_unpack(instr, CS_ADD_IMM64, I);

      fprintf(fp, "ADD_IMMEDIATE64 d%u, d%u, #%" PRId64, I.destination,
              I.source, I.immediate);
      break;
   }

   case MALI_CS_OPCODE_COMPARE_SELECT32: {
      cs_unpack(instr, CS_UMIN32, I);

      fprintf(fp, "UMIN32 r%u, r%u, r%u", I.destination, I.source_1,
              I.source_0);
      break;
   }
#endif

   case MALI_CS_OPCODE_LOAD_MULTIPLE: {
      cs_unpack(instr, CS_LOAD_MULTIPLE, I);

      fprintf(fp, "LOAD_MULTIPLE ");
      print_reg_tuple(I.base_register, I.mask, fp);
      fprintf(fp, ", ");
      print_indirect(I.address, I.offset, fp);
      break;
   }

   case MALI_CS_OPCODE_STORE_MULTIPLE: {
      cs_unpack(instr, CS_STORE_MULTIPLE, I);

      fprintf(fp, "STORE_MULTIPLE ");
      print_indirect(I.address, I.offset, fp);
      fprintf(fp, ", ");
      print_reg_tuple(I.base_register, I.mask, fp);
      break;
   }

   case MALI_CS_OPCODE_BRANCH: {
      cs_unpack(instr, CS_BRANCH, I);
      fprintf(fp, "BRANCH.%s r%u, #%d", conditions_str[I.condition], I.value,
              I.offset);
      break;
   }

   case MALI_CS_OPCODE_SET_SB_ENTRY: {
      cs_unpack(instr, CS_SET_SB_ENTRY, I);
      fprintf(fp, "SET_SB_ENTRY #%u, #%u", I.endpoint_entry, I.other_entry);
      break;
   }

   case MALI_CS_OPCODE_PROGRESS_WAIT: {
      cs_unpack(instr, CS_PROGRESS_WAIT, I);
      fprintf(fp, "PROGRESS_WAIT d%u, #%u", I.source, I.queue);
      break;
   }

   case MALI_CS_OPCODE_SET_EXCEPTION_HANDLER: {
      cs_unpack(instr, CS_SET_EXCEPTION_HANDLER, I);
      fprintf(fp, "SET_EXCEPTION_HANDLER d%u, r%u", I.address, I.length);
      break;
   }

   case MALI_CS_OPCODE_CALL: {
      cs_unpack(instr, CS_CALL, I);
      fprintf(fp, "CALL d%u, r%u", I.address, I.length);
      break;
   }

   case MALI_CS_OPCODE_JUMP: {
      cs_unpack(instr, CS_JUMP, I);
      fprintf(fp, "JUMP d%u, r%u", I.address, I.length);
      break;
   }

   case MALI_CS_OPCODE_REQ_RESOURCE: {
      cs_unpack(instr, CS_REQ_RESOURCE, I);
      fprintf(fp, "REQ_RESOURCE%s%s%s%s", I.compute ? ".compute" : "",
              I.fragment ? ".fragment" : "", I.tiler ? ".tiler" : "",
              I.idvs ? ".idvs" : "");
      break;
   }

   case MALI_CS_OPCODE_FLUSH_CACHE2: {
      cs_unpack(instr, CS_FLUSH_CACHE2, I);
      static const char *mode[] = {
         "nop",
         "clean",
         "INVALID",
         "clean_invalidate",
      };

      static const char *other_mode[] = {
         "nop_other",
         "INVALID",
         "invalidate_other",
         "INVALID",
      };

      fprintf(fp, "FLUSH_CACHE2.%s_l2.%s_lsc.%s r%u, #%x, #%u",
              mode[I.l2_flush_mode], mode[I.lsc_flush_mode],
              other_mode[I.other_flush_mode], I.latest_flush_id, I.wait_mask,
              I.signal_slot);
      break;
   }

   case MALI_CS_OPCODE_SYNC_ADD32: {
      cs_unpack(instr, CS_SYNC_ADD32, I);
      fprintf(fp, "SYNC_ADD32%s%s [d%u], r%u, #%x, #%u",
              I.error_propagate ? ".error_propagate" : "",
              I.scope == MALI_CS_SYNC_SCOPE_CSG ? ".csg" : ".system", I.address,
              I.data, I.wait_mask, I.signal_slot);
      break;
   }

   case MALI_CS_OPCODE_SYNC_SET32: {
      cs_unpack(instr, CS_SYNC_SET32, I);
      fprintf(fp, "SYNC_SET32%s%s [d%u], r%u, #%x, #%u",
              I.error_propagate ? ".error_propagate" : "",
              I.scope == MALI_CS_SYNC_SCOPE_CSG ? ".csg" : ".system", I.address,
              I.data, I.wait_mask, I.signal_slot);
      break;
   }

   case MALI_CS_OPCODE_SYNC_WAIT32: {
      cs_unpack(instr, CS_SYNC_WAIT32, I);
      fprintf(fp, "SYNC_WAIT32%s%s d%u, r%u", conditions_str[I.condition],
              I.error_reject ? ".reject" : ".inherit", I.address, I.data);
      break;
   }

   case MALI_CS_OPCODE_STORE_STATE: {
      static const char *states_str[] = {
         "SYSTEM_TIMESTAMP",
         "CYCLE_COUNT",
         "DISJOINT_COUNT",
         "ERROR_STATE",
      };

      cs_unpack(instr, CS_STORE_STATE, I);
      fprintf(fp, "STORE_STATE.%s d%u, #%i, #%x, #%u",
              I.state >= ARRAY_SIZE(states_str) ? "UNKNOWN_STATE"
                                                : states_str[I.state],
              I.address, I.offset, I.wait_mask, I.signal_slot);
      break;
   }

   case MALI_CS_OPCODE_PROT_REGION: {
      cs_unpack(instr, CS_PROT_REGION, I);
      fprintf(fp, "PROT_REGION #%u", I.size);
      break;
   }

   case MALI_CS_OPCODE_PROGRESS_STORE: {
      cs_unpack(instr, CS_PROGRESS_STORE, I);
      fprintf(fp, "PROGRESS_STORE d%u", I.source);
      break;
   }

   case MALI_CS_OPCODE_PROGRESS_LOAD: {
      cs_unpack(instr, CS_PROGRESS_LOAD, I);
      fprintf(fp, "PROGRESS_LOAD d%u", I.destination);
      break;
   }

   case MALI_CS_OPCODE_RUN_COMPUTE_INDIRECT: {
      cs_unpack(instr, CS_RUN_COMPUTE_INDIRECT, I);
      fprintf(fp, "RUN_COMPUTE_INDIRECT%s.srt%d.spd%d.tsd%d.fau%d #%u",
              I.progress_increment ? ".progress_inc" : "", I.srt_select,
              I.spd_select, I.tsd_select, I.fau_select, I.workgroups_per_task);

      break;
   }

   case MALI_CS_OPCODE_ERROR_BARRIER: {
      cs_unpack(instr, CS_ERROR_BARRIER, I);
      fprintf(fp, "ERROR_BARRIER");
      break;
   }

   case MALI_CS_OPCODE_HEAP_SET: {
      cs_unpack(instr, CS_HEAP_SET, I);
      fprintf(fp, "HEAP_SET d%u", I.address);
      break;
   }

   case MALI_CS_OPCODE_HEAP_OPERATION: {
      cs_unpack(instr, CS_HEAP_OPERATION, I);
      const char *counter_names[] = {"vt_start", "vt_end", NULL, "frag_end"};
      fprintf(fp, "HEAP_OPERATION.%s #%x, #%d", counter_names[I.operation],
              I.wait_mask, I.signal_slot);
      break;
   }

   case MALI_CS_OPCODE_TRACE_POINT: {
      cs_unpack(instr, CS_TRACE_POINT, I);
      fprintf(fp, "TRACE_POINT r%d:r%d, #%x, #%u", I.base_register,
              I.base_register + I.register_count - 1, I.wait_mask,
              I.signal_slot);
      break;
   }

   case MALI_CS_OPCODE_SYNC_ADD64: {
      cs_unpack(instr, CS_SYNC_ADD64, I);
      fprintf(fp, "SYNC_ADD64%s%s [d%u], d%u, #%x, #%u",
              I.error_propagate ? ".error_propagate" : "",
              I.scope == MALI_CS_SYNC_SCOPE_CSG ? ".csg" : ".system", I.address,
              I.data, I.wait_mask, I.signal_slot);
      break;
   }

   case MALI_CS_OPCODE_SYNC_SET64: {
      cs_unpack(instr, CS_SYNC_SET64, I);
      fprintf(fp, "SYNC_SET64.%s%s [d%u], d%u, #%x, #%u",
              I.error_propagate ? ".error_propagate" : "",
              I.scope == MALI_CS_SYNC_SCOPE_CSG ? ".csg" : ".system", I.address,
              I.data, I.wait_mask, I.signal_slot);
      break;
   }

   case MALI_CS_OPCODE_SYNC_WAIT64: {
      cs_unpack(instr, CS_SYNC_WAIT64, I);

      fprintf(fp, "SYNC_WAIT64%s%s d%u, d%u", conditions_str[I.condition],
              I.error_reject ? ".reject" : ".inherit", I.address, I.data);
      break;
   }

   default: {
      fprintf(fp, "UNKNOWN_%u 0x%" PRIX64 "\n", base.opcode, base.data);
      break;
   }
   }
}

static uint32_t
cs_get_u32(struct queue_ctx *qctx, uint8_t reg)
{
   assert(reg < qctx->nr_regs);
   return qctx->regs[reg];
}

static uint64_t
cs_get_u64(struct queue_ctx *qctx, uint8_t reg)
{
   return (((uint64_t)cs_get_u32(qctx, reg + 1)) << 32) | cs_get_u32(qctx, reg);
}

static void
pandecode_run_compute(struct pandecode_context *ctx, FILE *fp,
                      struct queue_ctx *qctx, struct MALI_CS_RUN_COMPUTE *I)
{
   if (qctx->in_exception_handler)
      return;

   ctx->indent++;

   unsigned reg_srt = 0 + (I->srt_select * 2);
   unsigned reg_fau = 8 + (I->fau_select * 2);
   unsigned reg_spd = 16 + (I->spd_select * 2);
   unsigned reg_tsd = 24 + (I->tsd_select * 2);

   uint64_t compute_srt = cs_get_u64(qctx, reg_srt);

   GENX(pandecode_resource_tables)(ctx, compute_srt, "Resources");

   uint64_t fau = cs_get_u64(qctx, reg_fau);

   if (fau)
      GENX(pandecode_fau)(ctx, fau & BITFIELD64_MASK(48), fau >> 56, "FAU");

   GENX(pandecode_shader)
   (ctx, cs_get_u64(qctx, reg_spd), "Shader", qctx->gpu_id);

   DUMP_ADDR(ctx, LOCAL_STORAGE, cs_get_u64(qctx, reg_tsd),
             "Local Storage @%" PRIx64 ":\n", cs_get_u64(qctx, reg_tsd));

   pandecode_log(ctx, "Global attribute offset: %u\n", cs_get_u32(qctx, 32));
   DUMP_CL(ctx, COMPUTE_SIZE_WORKGROUP, &qctx->regs[33], "Workgroup size\n");
   pandecode_log(ctx, "Job offset X: %u\n", cs_get_u32(qctx, 34));
   pandecode_log(ctx, "Job offset Y: %u\n", cs_get_u32(qctx, 35));
   pandecode_log(ctx, "Job offset Z: %u\n", cs_get_u32(qctx, 36));
   pandecode_log(ctx, "Job size X: %u\n", cs_get_u32(qctx, 37));
   pandecode_log(ctx, "Job size Y: %u\n", cs_get_u32(qctx, 38));
   pandecode_log(ctx, "Job size Z: %u\n", cs_get_u32(qctx, 39));

   ctx->indent--;
}

static void
pandecode_run_compute_indirect(struct pandecode_context *ctx, FILE *fp,
                               struct queue_ctx *qctx,
                               struct MALI_CS_RUN_COMPUTE_INDIRECT *I)
{
   if (qctx->in_exception_handler)
      return;

   ctx->indent++;

   unsigned reg_srt = 0 + (I->srt_select * 2);
   unsigned reg_fau = 8 + (I->fau_select * 2);
   unsigned reg_spd = 16 + (I->spd_select * 2);
   unsigned reg_tsd = 24 + (I->tsd_select * 2);

   uint64_t compute_srt = cs_get_u64(qctx, reg_srt);

   GENX(pandecode_resource_tables)(ctx, compute_srt, "Resources");

   uint64_t fau = cs_get_u64(qctx, reg_fau);

   if (fau)
      GENX(pandecode_fau)(ctx, fau & BITFIELD64_MASK(48), fau >> 56, "FAU");

   GENX(pandecode_shader)
   (ctx, cs_get_u64(qctx, reg_spd), "Shader", qctx->gpu_id);

   DUMP_ADDR(ctx, LOCAL_STORAGE, cs_get_u64(qctx, reg_tsd),
             "Local Storage @%" PRIx64 ":\n", cs_get_u64(qctx, reg_tsd));

   pandecode_log(ctx, "Global attribute offset: %u\n", cs_get_u32(qctx, 32));
   DUMP_CL(ctx, COMPUTE_SIZE_WORKGROUP, &qctx->regs[33], "Workgroup size\n");
   pandecode_log(ctx, "Job offset X: %u\n", cs_get_u32(qctx, 34));
   pandecode_log(ctx, "Job offset Y: %u\n", cs_get_u32(qctx, 35));
   pandecode_log(ctx, "Job offset Z: %u\n", cs_get_u32(qctx, 36));
   pandecode_log(ctx, "Job size X: %u\n", cs_get_u32(qctx, 37));
   pandecode_log(ctx, "Job size Y: %u\n", cs_get_u32(qctx, 38));
   pandecode_log(ctx, "Job size Z: %u\n", cs_get_u32(qctx, 39));

   ctx->indent--;
}

#if PAN_ARCH == 10
static void
pandecode_run_tiling(struct pandecode_context *ctx, FILE *fp,
                     struct queue_ctx *qctx, struct MALI_CS_RUN_TILING *I)
{
   if (qctx->in_exception_handler)
      return;

   ctx->indent++;

   /* Merge flag overrides with the register flags */
   struct mali_primitive_flags_packed tiler_flags_packed = {
      .opaque[0] = cs_get_u32(qctx, 56) | I->flags_override,
   };
   pan_unpack(&tiler_flags_packed, PRIMITIVE_FLAGS, tiler_flags);

   unsigned reg_srt = I->srt_select * 2;
   unsigned reg_fau = 8 + I->fau_select * 2;
   unsigned reg_spd = 16 + I->spd_select * 2;
   unsigned reg_tsd = 24 + I->tsd_select;

   uint64_t srt = cs_get_u64(qctx, reg_srt);
   uint64_t fau = cs_get_u64(qctx, reg_fau);
   uint64_t spd = cs_get_u64(qctx, reg_spd);
   uint64_t tsd = cs_get_u64(qctx, reg_tsd);

   GENX(pandecode_resource_tables)(ctx, srt, "Fragment resources");

   if (fau) {
      uint64_t lo = fau & BITFIELD64_MASK(48);
      uint64_t hi = fau >> 56;

      GENX(pandecode_fau)(ctx, lo, hi, "Fragment FAU");
   }

   uint64_t fs_bin_addr = 0;
   if (spd) {
      fs_bin_addr = GENX(pandecode_shader)
      (ctx, spd, "Fragment shader", qctx->gpu_id);
   }

   DUMP_ADDR(ctx, LOCAL_STORAGE, tsd, "Fragment Local Storage @%" PRIx64 ":\n",
             tsd);

   pandecode_log(ctx, "Global attribute offset: %u\n", cs_get_u32(qctx, 32));
   pandecode_log(ctx, "Index count: %u\n", cs_get_u32(qctx, 33));
   pandecode_log(ctx, "Instance count: %u\n", cs_get_u32(qctx, 34));

   if (tiler_flags.index_type)
      pandecode_log(ctx, "Index offset: %u\n", cs_get_u32(qctx, 35));

   pandecode_log(ctx, "Vertex offset: %d\n", cs_get_u32(qctx, 36));
   pandecode_log(ctx, "Tiler DCD flags2: %X\n", cs_get_u32(qctx, 38));

   if (tiler_flags.index_type)
      pandecode_log(ctx, "Index array size: %u\n", cs_get_u32(qctx, 39));

   GENX(pandecode_tiler)(ctx, cs_get_u64(qctx, 40), qctx->gpu_id);

   DUMP_CL(ctx, SCISSOR, &qctx->regs[42], "Scissor\n");
   pandecode_log(ctx, "Low depth clamp: %f\n", uif(cs_get_u32(qctx, 44)));
   pandecode_log(ctx, "High depth clamp: %f\n", uif(cs_get_u32(qctx, 45)));
   pandecode_log(ctx, "Occlusion: %" PRIx64 "\n", cs_get_u64(qctx, 46));
   pandecode_log(ctx, "Vertex position array: %" PRIx64 "\n",
                 cs_get_u64(qctx, 48));

   uint64_t blend = cs_get_u64(qctx, 50);
   GENX(pandecode_blend_descs)(ctx, blend & ~15, blend & 15,
                               fs_bin_addr, qctx->gpu_id);

   DUMP_ADDR(ctx, DEPTH_STENCIL, cs_get_u64(qctx, 52), "Depth/stencil");

   if (tiler_flags.index_type)
      pandecode_log(ctx, "Indices: %" PRIx64 "\n", cs_get_u64(qctx, 54));

   DUMP_UNPACKED(ctx, PRIMITIVE_FLAGS, tiler_flags, "Primitive flags\n");
   DUMP_CL(ctx, DCD_FLAGS_0, &qctx->regs[57], "DCD Flags 0\n");
   DUMP_CL(ctx, DCD_FLAGS_1, &qctx->regs[58], "DCD Flags 1\n");
   pandecode_log(ctx, "Vertex bounds: %u\n", cs_get_u32(qctx, 59));
   DUMP_CL(ctx, PRIMITIVE_SIZE, &qctx->regs[60], "Primitive size\n");

   ctx->indent--;
}
#endif

#if PAN_ARCH >= 12
static void
pandecode_run_idvs2(struct pandecode_context *ctx, FILE *fp,
                    struct queue_ctx *qctx, struct MALI_CS_RUN_IDVS2 *I)
{
   if (qctx->in_exception_handler)
      return;

   ctx->indent++;

   uint64_t vert_srt = cs_get_u64(qctx, MALI_IDVS_SR_VERTEX_SRT);
   uint64_t frag_srt = cs_get_u64(qctx, MALI_IDVS_SR_FRAGMENT_SRT);
   uint64_t vert_fau = cs_get_u64(qctx, MALI_IDVS_SR_VERTEX_FAU);
   uint64_t fragment_fau = cs_get_u64(qctx, MALI_IDVS_SR_FRAGMENT_FAU);
   uint64_t vertex_spd = cs_get_u64(qctx, MALI_IDVS_SR_VERTEX_SPD);
   uint64_t fragment_spd = cs_get_u64(qctx, MALI_IDVS_SR_FRAGMENT_SPD);
   uint64_t vertex_tsd = cs_get_u64(qctx, MALI_IDVS_SR_VERTEX_TSD);
   uint64_t fragment_tsd = cs_get_u64(qctx, MALI_IDVS_SR_FRAGMENT_TSD);
   uint32_t global_attribute_offset =
      cs_get_u32(qctx, MALI_IDVS_SR_GLOBAL_ATTRIBUTE_OFFSET);
   uint32_t index_count = cs_get_u32(qctx, MALI_IDVS_SR_INDEX_COUNT);
   uint32_t instance_count = cs_get_u32(qctx, MALI_IDVS_SR_INSTANCE_COUNT);
   uint32_t index_offset = cs_get_u32(qctx, MALI_IDVS_SR_INDEX_OFFSET);
   uint32_t vertex_offset = cs_get_u32(qctx, MALI_IDVS_SR_VERTEX_OFFSET);
   uint32_t instance_offset = cs_get_u32(qctx, MALI_IDVS_SR_INSTANCE_OFFSET);
   uint64_t tilder_descriptor_pointer =
      cs_get_u64(qctx, MALI_IDVS_SR_TILER_CTX);
   uint64_t vertex_index_array_pointer =
      cs_get_u64(qctx, MALI_IDVS_SR_INDEX_BUFFER);
   uint32_t index_array_size = cs_get_u32(qctx, MALI_IDVS_SR_INDEX_BUFFER_SIZE);
   uint32_t varying_size = cs_get_u32(qctx, MALI_IDVS_SR_VARY_SIZE) & 0xffff;
   uint64_t zsd_pointer = cs_get_u64(qctx, MALI_IDVS_SR_ZSD);
   uint64_t blend = cs_get_u64(qctx, MALI_IDVS_SR_BLEND_DESC);
   uint32_t raw_tiler_flags = cs_get_u32(qctx, MALI_IDVS_SR_TILER_FLAGS);
   uint64_t occlusion_pointer = cs_get_u32(qctx, MALI_IDVS_SR_OQ);

   /* Merge flag overrides with the register flags */
   struct mali_primitive_flags_packed tiler_flags_packed = {
      .opaque[0] = raw_tiler_flags | I->flags_override,
   };
   pan_unpack(&tiler_flags_packed, PRIMITIVE_FLAGS, tiler_flags);

   GENX(pandecode_resource_tables)(ctx, vert_srt, "Vertex resources");
   GENX(pandecode_resource_tables)(ctx, frag_srt, "Fragment resources");

   if (vert_fau) {
      uint64_t lo = vert_fau & BITFIELD64_MASK(48);
      uint64_t hi = vert_fau >> 56;

      GENX(pandecode_fau)(ctx, lo, hi, "Vertex FAU");
   }

   if (fragment_fau) {
      uint64_t lo = fragment_fau & BITFIELD64_MASK(48);
      uint64_t hi = fragment_fau >> 56;

      GENX(pandecode_fau)(ctx, lo, hi, "Fragment FAU");
   }

   if (vertex_spd) {
      GENX(pandecode_shader)
      (ctx, vertex_spd, "Vertex shader", qctx->gpu_id);
   }

   uint64_t fs_bin_addr = 0;
   if (fragment_spd) {
      fs_bin_addr = GENX(pandecode_shader)
      (ctx, fragment_spd, "Fragment shader", qctx->gpu_id);
   }

   DUMP_ADDR(ctx, LOCAL_STORAGE, vertex_tsd,
             "Vertex Local Storage @%" PRIx64 ":\n", vertex_tsd);
   DUMP_ADDR(ctx, LOCAL_STORAGE, fragment_tsd,
             "Fragment Local Storage @%" PRIx64 ":\n", fragment_tsd);

   pandecode_log(ctx, "Global attribute offset: %u\n", global_attribute_offset);
   pandecode_log(ctx, "Index count: %u\n", index_count);
   pandecode_log(ctx, "Instance count: %u\n", instance_count);

   if (tiler_flags.index_type)
      pandecode_log(ctx, "Index offset: %u\n", index_offset);

   pandecode_log(ctx, "Vertex offset: %u\n", vertex_offset);
   pandecode_log(ctx, "Instance offset: %u\n", instance_offset);

   GENX(pandecode_tiler)(ctx, tilder_descriptor_pointer, qctx->gpu_id);

   /* If this is true, then the scissor is actually a pointer to an
    * array of boxes; bottom 56 bits are the pointer and top 8 are
    * the length */
   assert(!tiler_flags.scissor_array_enable);

   struct mali_viewport_packed viewport_packed = {
      .opaque[0] = cs_get_u32(qctx, MALI_IDVS_SR_VIEWPORT_HIGH),
      .opaque[1] = cs_get_u32(qctx, MALI_IDVS_SR_VIEWPORT_HIGH + 1),
      .opaque[2] = cs_get_u32(qctx, MALI_IDVS_SR_VIEWPORT_LOW),
      .opaque[3] = cs_get_u32(qctx, MALI_IDVS_SR_VIEWPORT_LOW + 1),
   };
   DUMP_CL(ctx, VIEWPORT, &viewport_packed, "Viewport\n");
   DUMP_CL(ctx, SCISSOR, &qctx->regs[MALI_IDVS_SR_SCISSOR_BOX], "Scissor\n");

   pandecode_log(ctx, "Per-vertex varying size: %u\n", varying_size);

   DUMP_ADDR(ctx, DEPTH_STENCIL, zsd_pointer, "Depth/stencil");

   GENX(pandecode_blend_descs)(ctx, blend & ~15, blend & 15,
                               fs_bin_addr, qctx->gpu_id);

   if (tiler_flags.index_type) {
      pandecode_log(ctx, "Indices: %" PRIx64 "\n", vertex_index_array_pointer);
      pandecode_log(ctx, "Index array size: %u\n", index_array_size);
   }

   DUMP_UNPACKED(ctx, PRIMITIVE_FLAGS, tiler_flags, "Primitive flags\n");
   DUMP_CL(ctx, DCD_FLAGS_0, &qctx->regs[MALI_IDVS_SR_DCD0], "DCD Flags 0\n");
   DUMP_CL(ctx, DCD_FLAGS_1, &qctx->regs[MALI_IDVS_SR_DCD1], "DCD Flags 1\n");
   DUMP_CL(ctx, DCD_FLAGS_2, &qctx->regs[MALI_IDVS_SR_DCD2], "DCD Flags 2\n");

#if PAN_ARCH >= 13
   float line_width = cs_get_u32(qctx, MALI_IDVS_SR_LINE_WIDTH);
   pandecode_log(ctx, "Line width: %f\n", line_width);
#else
   DUMP_CL(ctx, PRIMITIVE_SIZE, &qctx->regs[MALI_IDVS_SR_PRIMITIVE_SIZE],
           "Primitive size\n");
#endif

   DUMP_CL(ctx, PRIMITIVE_FLAGS_2, &qctx->regs[MALI_IDVS_SR_TILER_FLAGS2],
           "Tiler flags 2\n");
   pandecode_log(ctx, "Occlusion: %" PRIx64 "\n", occlusion_pointer);

   ctx->indent--;
}
#else
static void
pandecode_run_idvs(struct pandecode_context *ctx, FILE *fp,
                   struct queue_ctx *qctx, struct MALI_CS_RUN_IDVS *I)
{
   if (qctx->in_exception_handler)
      return;

   ctx->indent++;

   /* Merge flag overrides with the register flags */
   struct mali_primitive_flags_packed tiler_flags_packed = {
      .opaque[0] =
         cs_get_u32(qctx, MALI_IDVS_SR_TILER_FLAGS) | I->flags_override,
   };
   pan_unpack(&tiler_flags_packed, PRIMITIVE_FLAGS, tiler_flags);

   unsigned reg_position_srt = 0;
   unsigned reg_position_fau = 8;
   unsigned reg_position_tsd = 24;

   unsigned reg_vary_srt = I->varying_srt_select ? 2 : 0;
   unsigned reg_vary_fau = I->varying_fau_select ? 10 : 8;
   unsigned reg_vary_tsd = I->varying_tsd_select ? 26 : 24;

   unsigned reg_frag_srt = I->fragment_srt_select ? 4 : 0;
   unsigned reg_frag_fau = 12;
   unsigned reg_frag_tsd = I->fragment_tsd_select ? 28 : 24;

   uint64_t position_srt = cs_get_u64(qctx, reg_position_srt);
   uint64_t vary_srt = cs_get_u64(qctx, reg_vary_srt);
   uint64_t frag_srt = cs_get_u64(qctx, reg_frag_srt);

   GENX(pandecode_resource_tables)(ctx, position_srt, "Position resources");
   GENX(pandecode_resource_tables)(ctx, vary_srt, "Varying resources");
   GENX(pandecode_resource_tables)(ctx, frag_srt, "Fragment resources");

   uint64_t position_fau = cs_get_u64(qctx, reg_position_fau);
   uint64_t vary_fau = cs_get_u64(qctx, reg_vary_fau);
   uint64_t fragment_fau = cs_get_u64(qctx, reg_frag_fau);

   if (position_fau) {
      uint64_t lo = position_fau & BITFIELD64_MASK(48);
      uint64_t hi = position_fau >> 56;

      GENX(pandecode_fau)(ctx, lo, hi, "Position FAU");
   }

   if (vary_fau) {
      uint64_t lo = vary_fau & BITFIELD64_MASK(48);
      uint64_t hi = vary_fau >> 56;

      GENX(pandecode_fau)(ctx, lo, hi, "Varying FAU");
   }

   if (fragment_fau) {
      uint64_t lo = fragment_fau & BITFIELD64_MASK(48);
      uint64_t hi = fragment_fau >> 56;

      GENX(pandecode_fau)(ctx, lo, hi, "Fragment FAU");
   }

   if (cs_get_u64(qctx, MALI_IDVS_SR_VERTEX_POS_SPD)) {
      GENX(pandecode_shader)
      (ctx, cs_get_u64(qctx, MALI_IDVS_SR_VERTEX_POS_SPD), "Position shader",
       qctx->gpu_id);
   }

   if (tiler_flags.secondary_shader) {
      uint64_t ptr = cs_get_u64(qctx, MALI_IDVS_SR_VERTEX_VARY_SPD);

      GENX(pandecode_shader)(ctx, ptr, "Varying shader", qctx->gpu_id);
   }

   uint64_t fs_bin_addr = 0;
   if (cs_get_u64(qctx, MALI_IDVS_SR_FRAGMENT_SPD)) {
      fs_bin_addr = GENX(pandecode_shader)
      (ctx, cs_get_u64(qctx, MALI_IDVS_SR_FRAGMENT_SPD), "Fragment shader",
       qctx->gpu_id);
   }

   DUMP_ADDR(ctx, LOCAL_STORAGE, cs_get_u64(qctx, reg_position_tsd),
             "Position Local Storage @%" PRIx64 ":\n",
             cs_get_u64(qctx, reg_position_tsd));
   DUMP_ADDR(ctx, LOCAL_STORAGE, cs_get_u64(qctx, reg_vary_tsd),
             "Varying Local Storage @%" PRIx64 ":\n",
             cs_get_u64(qctx, reg_vary_tsd));
   DUMP_ADDR(ctx, LOCAL_STORAGE, cs_get_u64(qctx, reg_frag_tsd),
             "Fragment Local Storage @%" PRIx64 ":\n",
             cs_get_u64(qctx, reg_frag_tsd));

   pandecode_log(ctx, "Global attribute offset: %u\n",
                 cs_get_u32(qctx, MALI_IDVS_SR_GLOBAL_ATTRIBUTE_OFFSET));
   pandecode_log(ctx, "Index count: %u\n",
                 cs_get_u32(qctx, MALI_IDVS_SR_INDEX_COUNT));
   pandecode_log(ctx, "Instance count: %u\n",
                 cs_get_u32(qctx, MALI_IDVS_SR_INSTANCE_COUNT));

   if (tiler_flags.index_type)
      pandecode_log(ctx, "Index offset: %u\n",
                    cs_get_u32(qctx, MALI_IDVS_SR_INDEX_OFFSET));

   pandecode_log(ctx, "Vertex offset: %d\n",
                 cs_get_u32(qctx, MALI_IDVS_SR_VERTEX_OFFSET));
   pandecode_log(ctx, "Instance offset: %u\n",
                 cs_get_u32(qctx, MALI_IDVS_SR_INSTANCE_OFFSET));
   pandecode_log(ctx, "Tiler DCD flags2: %X\n",
                 cs_get_u32(qctx, MALI_IDVS_SR_DCD2));

   if (tiler_flags.index_type)
      pandecode_log(ctx, "Index array size: %u\n",
                    cs_get_u32(qctx, MALI_IDVS_SR_INDEX_BUFFER_SIZE));

   GENX(pandecode_tiler)(ctx, cs_get_u64(qctx, MALI_IDVS_SR_TILER_CTX),
                         qctx->gpu_id);

   DUMP_CL(ctx, SCISSOR, &qctx->regs[MALI_IDVS_SR_SCISSOR_BOX], "Scissor\n");
   pandecode_log(ctx, "Low depth clamp: %f\n",
                 uif(cs_get_u32(qctx, MALI_IDVS_SR_LOW_DEPTH_CLAMP)));
   pandecode_log(ctx, "High depth clamp: %f\n",
                 uif(cs_get_u32(qctx, MALI_IDVS_SR_HIGH_DEPTH_CLAMP)));
   pandecode_log(ctx, "Occlusion: %" PRIx64 "\n",
                 cs_get_u64(qctx, MALI_IDVS_SR_OQ));

   if (tiler_flags.secondary_shader)
      pandecode_log(ctx, "Varying allocation: %u\n",
                    cs_get_u32(qctx, MALI_IDVS_SR_VARY_SIZE));

   uint64_t blend = cs_get_u64(qctx, MALI_IDVS_SR_BLEND_DESC);
   GENX(pandecode_blend_descs)(ctx, blend & ~15, blend & 15,
                               fs_bin_addr, qctx->gpu_id);

   DUMP_ADDR(ctx, DEPTH_STENCIL, cs_get_u64(qctx, MALI_IDVS_SR_ZSD),
             "Depth/stencil");

   if (tiler_flags.index_type)
      pandecode_log(ctx, "Indices: %" PRIx64 "\n",
                    cs_get_u64(qctx, MALI_IDVS_SR_INDEX_BUFFER));

   DUMP_UNPACKED(ctx, PRIMITIVE_FLAGS, tiler_flags, "Primitive flags\n");
   DUMP_CL(ctx, DCD_FLAGS_0, &qctx->regs[MALI_IDVS_SR_DCD0], "DCD Flags 0\n");
   DUMP_CL(ctx, DCD_FLAGS_1, &qctx->regs[MALI_IDVS_SR_DCD1], "DCD Flags 1\n");
   DUMP_CL(ctx, PRIMITIVE_SIZE, &qctx->regs[MALI_IDVS_SR_PRIMITIVE_SIZE],
           "Primitive size\n");

   ctx->indent--;
}
#endif

static void
pandecode_run_fragment(struct pandecode_context *ctx, FILE *fp,
                       struct queue_ctx *qctx, struct MALI_CS_RUN_FRAGMENT *I)
{
   if (qctx->in_exception_handler)
      return;

   ctx->indent++;

   DUMP_CL(ctx, SCISSOR, &qctx->regs[MALI_FRAGMENT_SR_BBOX_MIN], "Scissor\n");

   /* TODO: Tile enable map */
   GENX(pandecode_fbd)
   (ctx, cs_get_u64(qctx, MALI_FRAGMENT_SR_FBD_POINTER) & ~0x3full, true,
    qctx->gpu_id);

   ctx->indent--;
}

static void
pandecode_run_fullscreen(struct pandecode_context *ctx, FILE *fp,
                         struct queue_ctx *qctx,
                         struct MALI_CS_RUN_FULLSCREEN *I)
{
   if (qctx->in_exception_handler)
      return;

   ctx->indent++;

   /* Merge flag overrides with the register flags */
   struct mali_primitive_flags_packed tiler_flags_packed = {
      .opaque[0] = cs_get_u32(qctx, 56) | I->flags_override,
   };
   pan_unpack(&tiler_flags_packed, PRIMITIVE_FLAGS, tiler_flags);
   DUMP_UNPACKED(ctx, PRIMITIVE_FLAGS, tiler_flags, "Primitive flags\n");

   GENX(pandecode_tiler)(ctx, cs_get_u64(qctx, 40), qctx->gpu_id);

   DUMP_CL(ctx, SCISSOR, &qctx->regs[42], "Scissor\n");

   pan_unpack(
      PANDECODE_PTR(ctx, cs_get_u64(qctx, I->dcd), struct mali_draw_packed),
      DRAW, dcd);
   GENX(pandecode_dcd)(ctx, &dcd, 0, qctx->gpu_id);

   ctx->indent--;
}

static bool
interpret_cs_jump(struct pandecode_context *ctx, struct queue_ctx *qctx,
                  uint64_t reg_address, uint32_t reg_length)
{
   uint32_t address_lo = qctx->regs[reg_address];
   uint32_t address_hi = qctx->regs[reg_address + 1];
   uint32_t length = qctx->regs[reg_length];

   if (length % 8) {
      fprintf(stderr, "CS call alignment error\n");
      return false;
   }

   /* Map the entire subqueue now */
   uint64_t address = ((uint64_t)address_hi << 32) | address_lo;
   /* Return if the jump is for an exception handler that's set to zero */
   if (qctx->in_exception_handler && (!address || !length)) {
      qctx->in_exception_handler = false;
      qctx->call_stack_depth--;
      return true;
   }
   uint64_t *cs = pandecode_fetch_gpu_mem(ctx, address, length);

   qctx->ip = cs;
   qctx->end = cs + (length / 8);

   /* Skip the usual IP update */
   return true;
}

static bool
eval_cond(struct queue_ctx *qctx, enum mali_cs_condition cond, uint32_t reg)
{
   int32_t val = qctx->regs[reg];

   switch (cond) {
   case MALI_CS_CONDITION_LEQUAL:
      return val <= 0;
   case MALI_CS_CONDITION_EQUAL:
      return val == 0;
   case MALI_CS_CONDITION_LESS:
      return val < 0;
   case MALI_CS_CONDITION_GREATER:
      return val > 0;
   case MALI_CS_CONDITION_NEQUAL:
      return val != 0;
   case MALI_CS_CONDITION_GEQUAL:
      return val >= 0;
   case MALI_CS_CONDITION_ALWAYS:
      return true;
   default:
      assert(!"Invalid condition");
      return false;
   }
}

static void
interpret_cs_branch(struct pandecode_context *ctx, struct queue_ctx *qctx,
                    int16_t offset, enum mali_cs_condition cond, uint32_t reg)
{
   if (eval_cond(qctx, cond, reg))
      qctx->ip += offset;
}

/*
 * Interpret a single instruction of the CS, updating the register file,
 * instruction pointer, and call stack. Memory access and GPU controls are
 * ignored for now.
 *
 * Returns true if execution should continue.
 */
static bool
interpret_cs_instr(struct pandecode_context *ctx, struct queue_ctx *qctx)
{
   FILE *fp = ctx->dump_stream;
   /* Unpack the base so we get the opcode */
   uint8_t *bytes = (uint8_t *)qctx->ip;
   cs_unpack(bytes, CS_BASE, base);

   assert(qctx->ip < qctx->end);

   /* Don't try to keep track of registers/operations inside exception handler */
   if (qctx->in_exception_handler) {
      assert(base.opcode != MALI_CS_OPCODE_SET_EXCEPTION_HANDLER);
      goto no_interpret;
   }

   switch (base.opcode) {
   case MALI_CS_OPCODE_RUN_COMPUTE: {
      cs_unpack(bytes, CS_RUN_COMPUTE, I);
      pandecode_run_compute(ctx, fp, qctx, &I);
      break;
   }

#if PAN_ARCH == 10
   case MALI_CS_OPCODE_RUN_TILING: {
      cs_unpack(bytes, CS_RUN_TILING, I);
      pandecode_run_tiling(ctx, fp, qctx, &I);
      break;
   }
#endif

#if PAN_ARCH >= 12
   case MALI_CS_OPCODE_RUN_IDVS2: {
      cs_unpack(bytes, CS_RUN_IDVS2, I);
      pandecode_run_idvs2(ctx, fp, qctx, &I);
      break;
   }
#else
   case MALI_CS_OPCODE_RUN_IDVS: {
      cs_unpack(bytes, CS_RUN_IDVS, I);
      pandecode_run_idvs(ctx, fp, qctx, &I);
      break;
   }
#endif

   case MALI_CS_OPCODE_RUN_FRAGMENT: {
      cs_unpack(bytes, CS_RUN_FRAGMENT, I);
      pandecode_run_fragment(ctx, fp, qctx, &I);
      break;
   }

   case MALI_CS_OPCODE_RUN_FULLSCREEN: {
      cs_unpack(bytes, CS_RUN_FULLSCREEN, I);
      pandecode_run_fullscreen(ctx, fp, qctx, &I);
      break;
   }

   case MALI_CS_OPCODE_RUN_COMPUTE_INDIRECT: {
      cs_unpack(bytes, CS_RUN_COMPUTE_INDIRECT, I);
      pandecode_run_compute_indirect(ctx, fp, qctx, &I);
      break;
   }

   case MALI_CS_OPCODE_MOVE48: {
      cs_unpack(bytes, CS_MOVE48, I);

      qctx->regs[I.destination + 0] = (uint32_t)I.immediate;
      qctx->regs[I.destination + 1] = (uint32_t)(I.immediate >> 32);
      break;
   }

   case MALI_CS_OPCODE_MOVE32: {
      cs_unpack(bytes, CS_MOVE32, I);

      qctx->regs[I.destination] = I.immediate;
      break;
   }

   case MALI_CS_OPCODE_LOAD_MULTIPLE: {
      cs_unpack(bytes, CS_LOAD_MULTIPLE, I);
      uint64_t addr =
         ((uint64_t)qctx->regs[I.address + 1] << 32) | qctx->regs[I.address];
      addr += I.offset;

      uint32_t *src =
         pandecode_fetch_gpu_mem(ctx, addr, util_last_bit(I.mask) * 4);

      for (uint32_t i = 0; i < 16; i++) {
         if (I.mask & BITFIELD_BIT(i))
            qctx->regs[I.base_register + i] = src[i];
      }
      break;
   }

#if PAN_ARCH >= 11
   case MALI_CS_OPCODE_LOGIC_OP32: {
      cs_unpack(bytes, CS_LOGIC_OP32, I);

      uint32_t *dest = &qctx->regs[I.destination];
      uint32_t source_0 = qctx->regs[I.source_0];
      uint32_t source_1 = qctx->regs[I.source_1];
      uint32_t mode_0 = I.mode & 1;
      uint32_t mode_1 = (I.mode >> 1) & 1;
      uint32_t mode_2 = (I.mode >> 2) & 1;
      uint32_t mode_3 = (I.mode >> 3) & 1;

      if (I.index == MALI_CS_LOGIC_OP_INDEX_INDEX)
         source_1 = (1 << source_1);

      uint32_t result = 0;
      for (int i = 0; i < 32; i++) {
         uint32_t a_n = (source_0 >> i) & 1;
         uint32_t b_n = (source_1 >> i) & 1;

         uint32_t tmp = 0;
         tmp |= mode_0 & a_n & b_n;
         tmp |= mode_1 & a_n & ~b_n;
         tmp |= mode_2 & ~a_n & b_n;
         tmp |= mode_3 & ~a_n & ~b_n;
         result |= tmp << i;
      }

      *dest = result;
      break;
   }
#endif

#if PAN_ARCH >= 13
   case MALI_CS_OPCODE_ARITH_IMM32: {
      cs_unpack(bytes, CS_ARITH_IMM32_BASE, I);

      uint32_t *dest = &qctx->regs[I.destination];
      uint32_t source = qctx->regs[I.source];
      uint32_t imm = I.immediate;
      uint8_t bf_position = imm & 0xff;
      uint8_t bf_width = (imm >> 8) & 0xff;
      uint16_t bf_imm = (imm >> 16) & 0xffff;

      switch (I.sub_opcode) {
      case MALI_CS_ARITH_IMM32_SUB_OPCODE_ADD_IMM32: {
         *dest = source + imm;
         break;
      }
      case MALI_CS_ARITH_IMM32_SUB_OPCODE_LSHIFT_IMM32: {
         *dest = source << imm;
         break;
      }
      case MALI_CS_ARITH_IMM32_SUB_OPCODE_RSHIFT_IMM_U32: {
         *dest = source >> imm;
         break;
      }
      case MALI_CS_ARITH_IMM32_SUB_OPCODE_RSHIFT_IMM_S32: {
         *dest = (int32_t)source >> imm;
         break;
      }
      case MALI_CS_ARITH_IMM32_SUB_OPCODE_BFEXT_U32: {
         uint32_t mask = (1 << bf_width) - 1;
         *dest = (source >> bf_position) & mask;
         break;
      }
      case MALI_CS_ARITH_IMM32_SUB_OPCODE_BFEXT_S32: {
         uint32_t mask = (1 << bf_width) - 1;
         uint32_t tmp = (source >> bf_position) & mask;
         *dest = util_sign_extend(tmp, bf_width);
         break;
      }
      case MALI_CS_ARITH_IMM32_SUB_OPCODE_BFINS_IMM32: {
         uint32_t mask0 = (1 << bf_width) - 1;
         uint32_t mask1 = mask0 << bf_position;
         uint32_t tmp = bf_imm << bf_position;
         *dest = (tmp & mask1) | (source & ~mask1);
         break;
      }
      case MALI_CS_ARITH_IMM32_SUB_OPCODE_UMIN_IMM32: {
         *dest = MIN2(source, imm);
         break;
      }
      default:
         assert(0 && "unhandled ARITH_IMM32 subopcode");
      }

      break;
   }

   case MALI_CS_OPCODE_ARITH_REG32: {
      cs_unpack(bytes, CS_ARITH_REG32_BASE, I);

      uint32_t *dest = &qctx->regs[I.destination];
      uint32_t source_0 = qctx->regs[I.source_0];
      uint32_t source_1 = qctx->regs[I.source_1];

      switch (I.sub_opcode) {
      case MALI_CS_ARITH_REG32_SUB_OPCODE_UMIN32: {
         *dest = MIN2(source_0, source_1);
         break;
      }
      case MALI_CS_ARITH_REG32_SUB_OPCODE_ADD32: {
         *dest = source_0 + source_1;
         break;
      }
      case MALI_CS_ARITH_REG32_SUB_OPCODE_SUB32: {
         *dest = source_0 - source_1;
         break;
      }
      case MALI_CS_ARITH_REG32_SUB_OPCODE_LSHIFT32: {
         *dest = source_0 << source_1;
         break;
      }
      case MALI_CS_ARITH_REG32_SUB_OPCODE_RSHIFT_U32: {
         *dest = source_0 >> source_1;
         break;
      }
      case MALI_CS_ARITH_REG32_SUB_OPCODE_RSHIFT_S32: {
         *dest = (int32_t)source_0 >> source_1;
         break;
      }
      case MALI_CS_ARITH_REG32_SUB_OPCODE_BFINS32: {
         uint8_t bf_position = I.immediate & 0xff;
         uint8_t bf_width = (I.immediate >> 8) & 0xff;
         uint32_t mask0 = (1 << bf_width) - 1;
         uint32_t mask1 = mask0 << bf_position;
         uint32_t tmp = source_1 << bf_position;
         *dest = (tmp & mask1) | (source_0 & ~mask1);
         break;
      }
      default:
         assert(0 && "unhandled ARITH_REG32 subopcode");
      }

      break;
   }

   case MALI_CS_OPCODE_ARITH_IMM64: {
      cs_unpack(bytes, CS_ARITH_IMM64_BASE, I);

      uint64_t *dest = (uint64_t *)&qctx->regs[I.destination];
      uint64_t source =
         ((uint64_t)qctx->regs[I.source + 1] << 32) | qctx->regs[I.source];
      uint64_t imm = I.immediate;
      uint8_t bf_position = imm & 0xff;
      uint8_t bf_width = (imm >> 8) & 0xff;
      uint16_t bf_imm = (imm >> 16) & 0xffff;

      switch (I.sub_opcode) {
      case MALI_CS_ARITH_IMM64_SUB_OPCODE_ADD_IMM64: {
         *dest = source + imm;
         break;
      }
      case MALI_CS_ARITH_IMM64_SUB_OPCODE_LSHIFT_IMM64: {
         *dest = source << imm;
         break;
      }
      case MALI_CS_ARITH_IMM64_SUB_OPCODE_RSHIFT_IMM_U64: {
         *dest = source >> imm;
         break;
      }
      case MALI_CS_ARITH_IMM64_SUB_OPCODE_RSHIFT_IMM_S64: {
         *dest = (int64_t)source >> imm;
         break;
      }
      case MALI_CS_ARITH_IMM64_SUB_OPCODE_BFEXT_U64: {
         uint64_t mask = (1 << bf_width) - 1;
         *dest = (source >> bf_position) & mask;
         break;
      }
      case MALI_CS_ARITH_IMM64_SUB_OPCODE_BFEXT_S64: {
         uint64_t mask = (1 << bf_width) - 1;
         uint64_t tmp = (source >> bf_position) & mask;
         *dest = util_sign_extend(tmp, bf_width);
         break;
      }
      case MALI_CS_ARITH_IMM64_SUB_OPCODE_BFINS_IMM64: {
         uint64_t mask0 = (1 << bf_width) - 1;
         uint64_t mask1 = mask0 << bf_position;
         uint64_t tmp = bf_imm << bf_position;
         *dest = (tmp & mask1) | (source & ~mask1);
         break;
      }
      case MALI_CS_ARITH_IMM64_SUB_OPCODE_UMIN_IMM64: {
         *dest = MIN2(source, imm);
         break;
      }
      default:
         assert(0 && "unhandled ARITH_IMM64 subopcode");
      }

      break;
   }

   case MALI_CS_OPCODE_ARITH_REG64: {
      cs_unpack(bytes, CS_ARITH_REG64_BASE, I);

      uint64_t *dest = (uint64_t *)&qctx->regs[I.destination];
      uint64_t source_0 =
         ((uint64_t)qctx->regs[I.source_0 + 1] << 32) | qctx->regs[I.source_0];
      uint64_t source_1 =
         ((uint64_t)qctx->regs[I.source_1 + 1] << 32) | qctx->regs[I.source_1];

      switch (I.sub_opcode) {
      case MALI_CS_ARITH_REG64_SUB_OPCODE_UMIN64: {
         *dest = MIN2(source_0, source_1);
         break;
      }
      case MALI_CS_ARITH_REG64_SUB_OPCODE_ADD64: {
         *dest = source_0 + source_1;
         break;
      }
      case MALI_CS_ARITH_REG64_SUB_OPCODE_SUB64: {
         *dest = source_0 - source_1;
         break;
      }
      case MALI_CS_ARITH_REG64_SUB_OPCODE_LSHIFT64: {
         *dest = source_0 << source_1;
         break;
      }
      case MALI_CS_ARITH_REG64_SUB_OPCODE_RSHIFT_U64: {
         *dest = source_0 >> source_1;
         break;
      }
      case MALI_CS_ARITH_REG64_SUB_OPCODE_RSHIFT_S64: {
         *dest = (int64_t)source_0 >> source_1;
         break;
      }
      case MALI_CS_ARITH_REG64_SUB_OPCODE_BFINS64: {
         uint8_t bf_position = I.immediate & 0xff;
         uint8_t bf_width = (I.immediate >> 8) & 0xff;
         uint64_t mask0 = (1 << bf_width) - 1;
         uint64_t mask1 = mask0 << bf_position;
         uint64_t tmp = source_1 << bf_position;
         *dest = (tmp & mask1) | (source_0 & ~mask1);
         break;
      }
      default:
         assert(0 && "unhandled ARITH_REG64 subopcode");
      }

      break;
   }
#else
   case MALI_CS_OPCODE_ADD_IMMEDIATE32: {
      cs_unpack(bytes, CS_ADD_IMM32, I);

      qctx->regs[I.destination] = qctx->regs[I.source] + I.immediate;
      break;
   }

   case MALI_CS_OPCODE_ADD_IMMEDIATE64: {
      cs_unpack(bytes, CS_ADD_IMM64, I);

      int64_t value =
         (qctx->regs[I.source] | ((int64_t)qctx->regs[I.source + 1] << 32)) +
         I.immediate;

      qctx->regs[I.destination] = value;
      qctx->regs[I.destination + 1] = value >> 32;
      break;
   }
#endif

   case MALI_CS_OPCODE_CALL: {
      cs_unpack(bytes, CS_CALL, I);

      if (qctx->call_stack_depth == MAX_CALL_STACK_DEPTH) {
         fprintf(stderr, "CS call stack overflow\n");
         return false;
      }

      assert(qctx->call_stack_depth < MAX_CALL_STACK_DEPTH);

      qctx->ip++;

      /* Note: tail calls are not optimized in the hardware. */
      assert(qctx->ip <= qctx->end);

      unsigned depth = qctx->call_stack_depth++;

      qctx->call_stack[depth].lr = qctx->ip;
      qctx->call_stack[depth].end = qctx->end;

      return interpret_cs_jump(ctx, qctx, I.address, I.length);
   }

   case MALI_CS_OPCODE_SET_EXCEPTION_HANDLER: {
      cs_unpack(bytes, CS_SET_EXCEPTION_HANDLER, I);

      assert(qctx->call_stack_depth < MAX_CALL_STACK_DEPTH);

      qctx->ip++;

      /* Note: tail calls are not optimized in the hardware. */
      assert(qctx->ip <= qctx->end);

      unsigned depth = qctx->call_stack_depth++;

      qctx->call_stack[depth].lr = qctx->ip;
      qctx->call_stack[depth].end = qctx->end;

      /* Exception handler can use the full frame stack depth but we don't try
       * to keep track of the nested JUMP/CALL as we don't know what will be
       * the registers/memory content when the handler is triggered. */
      qctx->in_exception_handler = true;

      return interpret_cs_jump(ctx, qctx, I.address, I.length);
   }

   case MALI_CS_OPCODE_JUMP: {
      cs_unpack(bytes, CS_JUMP, I);

      if (qctx->call_stack_depth == 0) {
         fprintf(stderr, "Cannot jump from the entrypoint\n");
         return false;
      }

      return interpret_cs_jump(ctx, qctx, I.address, I.length);
   }

   case MALI_CS_OPCODE_BRANCH: {
      cs_unpack(bytes, CS_BRANCH, I);

      interpret_cs_branch(ctx, qctx, I.offset, I.condition, I.value);
      break;
   }

   default:
      break;
   }

no_interpret:

   /* Update IP first to point to the next instruction, so call doesn't
    * require special handling (even for tail calls).
    */
   qctx->ip++;

   while (qctx->ip == qctx->end) {
      /* Graceful termination */
      if (qctx->call_stack_depth == 0)
         return false;

      /* Pop off the call stack */
      unsigned old_depth = --qctx->call_stack_depth;

      qctx->ip = qctx->call_stack[old_depth].lr;
      qctx->end = qctx->call_stack[old_depth].end;
      qctx->in_exception_handler = false;
   }

   return true;
}

void
GENX(pandecode_interpret_cs)(struct pandecode_context *ctx, uint64_t queue,
                             uint32_t size, unsigned gpu_id, uint32_t *regs)
{
   pandecode_dump_file_open(ctx);

   uint64_t *cs = pandecode_fetch_gpu_mem(ctx, queue, size);

   /* v10 has 96 registers. v12+ have 128. */
   struct queue_ctx qctx = {
      .nr_regs = PAN_ARCH >= 12 ? 128 : 96,
      .regs = regs,
      .ip = cs,
      .end = cs + (size / 8),
      .gpu_id = gpu_id,

      /* If this is a kernel mode queue, we don't see the root ring buffer and
       * we must adjust the initial call stack depth accordingly.
       */
      .call_stack_depth = ctx->usermode_queue ? 0 : 1,
   };
   FILE *fp = ctx->dump_stream;

   if (size) {
      do {
         uint64_t instr = *qctx.ip;

         fprintf(fp, " ");
         for (unsigned b = 0; b < 8; ++b)
            fprintf(fp, " %02x", (uint8_t)(instr >> (8 * b)));

         for (int i = 0; i < 1 + qctx.call_stack_depth; ++i)
            fprintf(fp, "  ");

         print_cs_instr(fp, qctx.ip);
         fprintf(fp, "\n");
      } while (interpret_cs_instr(ctx, &qctx));
   }

   fflush(ctx->dump_stream);
   pandecode_map_read_write(ctx);
}

struct cs_code_block {
   struct list_head node;
   unsigned start;
   unsigned size;
   struct util_dynarray predecessors;
   unsigned successors[2];
};

struct cs_indirect_branch_target {
   uint64_t address;
   uint32_t length;
};

struct cs_indirect_branch {
   unsigned instr_idx;
   bool has_unknown_targets;
   struct util_dynarray targets;
};

struct cs_code_cfg {
   uint64_t *instrs;
   unsigned instr_count;
   struct cs_code_block **blk_map;
   struct util_dynarray indirect_branches;
};

static struct cs_code_block *
cs_code_block_alloc(void *alloc_ctx, unsigned start, unsigned size)
{
   struct cs_code_block *block = rzalloc(alloc_ctx, struct cs_code_block);

   block->start = start;
   block->size = size;
   memset(block->successors, ~0, sizeof(block->successors));
   list_inithead(&block->node);
   util_dynarray_init(&block->predecessors, alloc_ctx);
   return block;
}

static void
record_indirect_branch_target(struct cs_code_cfg *cfg,
                              struct list_head *blk_stack,
                              struct cs_code_block *cur_blk, unsigned blk_offs,
                              struct cs_indirect_branch *ibranch)
{
   union {
      uint32_t u32[256];
      uint64_t u64[128];
   } reg_file = {0};

   list_add(&cur_blk->node, blk_stack);
   list_for_each_entry(struct cs_code_block, blk, blk_stack, node) {
      for (; blk_offs < blk->size &&
             blk->start + blk_offs != ibranch->instr_idx;
           blk_offs++) {
         const uint64_t *instr = &cfg->instrs[blk->start + blk_offs];
         cs_unpack(instr, CS_BASE, base);
         switch (base.opcode) {
         case MALI_CS_OPCODE_MOVE48: {
            cs_unpack(instr, CS_MOVE48, I);

            assert(I.destination % 2 == 0 &&
                   "Destination register should be aligned to 2");

            reg_file.u64[I.destination / 2] = I.immediate;
            break;
         }

         case MALI_CS_OPCODE_MOVE32: {
            cs_unpack(instr, CS_MOVE32, I);
            reg_file.u32[I.destination] = I.immediate;
            break;
         }

#if PAN_ARCH >= 11
         case MALI_CS_OPCODE_LOGIC_OP32: {
            cs_unpack(instr, CS_LOGIC_OP32, I);

            uint32_t *dest = &reg_file.u32[I.destination];
            uint32_t source_0 = reg_file.u32[I.source_0];
            uint32_t source_1 = reg_file.u32[I.source_1];
            uint32_t mode_0 = I.mode & 1;
            uint32_t mode_1 = (I.mode >> 1) & 1;
            uint32_t mode_2 = (I.mode >> 2) & 1;
            uint32_t mode_3 = (I.mode >> 3) & 1;

            if (I.index == MALI_CS_LOGIC_OP_INDEX_INDEX)
               source_1 = (1 << source_1);

            uint32_t result = 0;
            for (int i = 0; i < 32; i++) {
               uint32_t a_n = (source_0 >> i) & 1;
               uint32_t b_n = (source_1 >> i) & 1;

               uint32_t tmp = 0;
               tmp |= mode_0 & a_n & b_n;
               tmp |= mode_1 & a_n & ~b_n;
               tmp |= mode_2 & ~a_n & b_n;
               tmp |= mode_3 & ~a_n & ~b_n;
               result |= tmp << i;
            }

            *dest = result;
            break;
         }
#endif

#if PAN_ARCH >= 13
         case MALI_CS_OPCODE_ARITH_IMM32: {
            cs_unpack(instr, CS_ARITH_IMM32_BASE, I);

            uint32_t *dest = &reg_file.u32[I.destination];
            uint32_t source = reg_file.u32[I.source];
            uint32_t imm = I.immediate;
            uint8_t bf_position = imm & 0xff;
            uint8_t bf_width = (imm >> 8) & 0xff;
            uint16_t bf_imm = (imm >> 16) & 0xffff;

            switch (I.sub_opcode) {
            case MALI_CS_ARITH_IMM32_SUB_OPCODE_ADD_IMM32: {
               *dest = source + imm;
               break;
            }
            case MALI_CS_ARITH_IMM32_SUB_OPCODE_LSHIFT_IMM32: {
               *dest = source << imm;
               break;
            }
            case MALI_CS_ARITH_IMM32_SUB_OPCODE_RSHIFT_IMM_U32: {
               *dest = source >> imm;
               break;
            }
            case MALI_CS_ARITH_IMM32_SUB_OPCODE_RSHIFT_IMM_S32: {
               *dest = (int32_t)source >> imm;
               break;
            }
            case MALI_CS_ARITH_IMM32_SUB_OPCODE_BFEXT_U32: {
               uint32_t mask = (1 << bf_width) - 1;
               *dest = (source >> bf_position) & mask;
               break;
            }
            case MALI_CS_ARITH_IMM32_SUB_OPCODE_BFEXT_S32: {
               uint32_t mask = (1 << bf_width) - 1;
               uint32_t tmp = (source >> bf_position) & mask;
               *dest = util_sign_extend(tmp, bf_width);
               break;
            }
            case MALI_CS_ARITH_IMM32_SUB_OPCODE_BFINS_IMM32: {
               uint32_t mask0 = (1 << bf_width) - 1;
               uint32_t mask1 = mask0 << bf_position;
               uint32_t tmp = bf_imm << bf_position;
               *dest = (tmp & mask1) | (source & ~mask1);
               break;
            }
            case MALI_CS_ARITH_IMM32_SUB_OPCODE_UMIN_IMM32: {
               *dest = MIN2(source, imm);
               break;
            }
            default:
               assert(0 && "unhandled ARITH_IMM32 subopcode");
            }

            break;
         }

         case MALI_CS_OPCODE_ARITH_REG32: {
            cs_unpack(instr, CS_ARITH_REG32_BASE, I);

            uint32_t *dest = &reg_file.u32[I.destination];
            uint32_t source_0 = reg_file.u32[I.source_0];
            uint32_t source_1 = reg_file.u32[I.source_1];

            switch (I.sub_opcode) {
            case MALI_CS_ARITH_REG32_SUB_OPCODE_UMIN32: {
               *dest = MIN2(source_0, source_1);
               break;
            }
            case MALI_CS_ARITH_REG32_SUB_OPCODE_ADD32: {
               *dest = source_0 + source_1;
               break;
            }
            case MALI_CS_ARITH_REG32_SUB_OPCODE_SUB32: {
               *dest = source_0 - source_1;
               break;
            }
            case MALI_CS_ARITH_REG32_SUB_OPCODE_LSHIFT32: {
               *dest = source_0 << source_1;
               break;
            }
            case MALI_CS_ARITH_REG32_SUB_OPCODE_RSHIFT_U32: {
               *dest = source_0 >> source_1;
               break;
            }
            case MALI_CS_ARITH_REG32_SUB_OPCODE_RSHIFT_S32: {
               *dest = (int32_t)source_0 >> source_1;
               break;
            }
            case MALI_CS_ARITH_REG32_SUB_OPCODE_BFINS32: {
               uint8_t bf_position = I.immediate & 0xff;
               uint8_t bf_width = (I.immediate >> 8) & 0xff;
               uint32_t mask0 = (1 << bf_width) - 1;
               uint32_t mask1 = mask0 << bf_position;
               uint32_t tmp = source_1 << bf_position;
               *dest = (tmp & mask1) | (source_0 & ~mask1);
               break;
            }
            default:
               assert(0 && "unhandled ARITH_REG32 subopcode");
            }

            break;
         }

         case MALI_CS_OPCODE_ARITH_IMM64: {
            cs_unpack(instr, CS_ARITH_IMM64_BASE, I);

            uint64_t *dest = &reg_file.u64[I.destination / 2];
            uint64_t source = reg_file.u64[I.source / 2];
            uint64_t imm = I.immediate;
            uint8_t bf_position = imm & 0xff;
            uint8_t bf_width = (imm >> 8) & 0xff;
            uint16_t bf_imm = (imm >> 16) & 0xffff;

            switch (I.sub_opcode) {
            case MALI_CS_ARITH_IMM64_SUB_OPCODE_ADD_IMM64: {
               *dest = source + imm;
               break;
            }
            case MALI_CS_ARITH_IMM64_SUB_OPCODE_LSHIFT_IMM64: {
               *dest = source << imm;
               break;
            }
            case MALI_CS_ARITH_IMM64_SUB_OPCODE_RSHIFT_IMM_U64: {
               *dest = source >> imm;
               break;
            }
            case MALI_CS_ARITH_IMM64_SUB_OPCODE_RSHIFT_IMM_S64: {
               *dest = (int64_t)source >> imm;
               break;
            }
            case MALI_CS_ARITH_IMM64_SUB_OPCODE_BFEXT_U64: {
               uint64_t mask = (1 << bf_width) - 1;
               *dest = (source >> bf_position) & mask;
               break;
            }
            case MALI_CS_ARITH_IMM64_SUB_OPCODE_BFEXT_S64: {
               uint64_t mask = (1 << bf_width) - 1;
               uint64_t tmp = (source >> bf_position) & mask;
               *dest = util_sign_extend(tmp, bf_width);
               break;
            }
            case MALI_CS_ARITH_IMM64_SUB_OPCODE_BFINS_IMM64: {
               uint64_t mask0 = (1 << bf_width) - 1;
               uint64_t mask1 = mask0 << bf_position;
               uint64_t tmp = bf_imm << bf_position;
               *dest = (tmp & mask1) | (source & ~mask1);
               break;
            }
            case MALI_CS_ARITH_IMM64_SUB_OPCODE_UMIN_IMM64: {
               *dest = MIN2(source, imm);
               break;
            }
            default:
               assert(0 && "unhandled ARITH_IMM64 subopcode");
            }

            break;
         }

         case MALI_CS_OPCODE_ARITH_REG64: {
            cs_unpack(instr, CS_ARITH_REG64_BASE, I);

            uint64_t *dest = &reg_file.u64[I.destination];
            uint64_t source_0 = reg_file.u64[I.source_0 / 2];
            uint64_t source_1 = reg_file.u64[I.source_1 / 2];

            switch (I.sub_opcode) {
            case MALI_CS_ARITH_REG64_SUB_OPCODE_UMIN64: {
               *dest = MIN2(source_0, source_1);
               break;
            }
            case MALI_CS_ARITH_REG64_SUB_OPCODE_ADD64: {
               *dest = source_0 + source_1;
               break;
            }
            case MALI_CS_ARITH_REG64_SUB_OPCODE_SUB64: {
               *dest = source_0 - source_1;
               break;
            }
            case MALI_CS_ARITH_REG64_SUB_OPCODE_LSHIFT64: {
               *dest = source_0 << source_1;
               break;
            }
            case MALI_CS_ARITH_REG64_SUB_OPCODE_RSHIFT_U64: {
               *dest = source_0 >> source_1;
               break;
            }
            case MALI_CS_ARITH_REG64_SUB_OPCODE_RSHIFT_S64: {
               *dest = (int64_t)source_0 >> source_1;
               break;
            }
            case MALI_CS_ARITH_REG64_SUB_OPCODE_BFINS64: {
               uint8_t bf_position = I.immediate & 0xff;
               uint8_t bf_width = (I.immediate >> 8) & 0xff;
               uint64_t mask0 = (1 << bf_width) - 1;
               uint64_t mask1 = mask0 << bf_position;
               uint64_t tmp = source_1 << bf_position;
               *dest = (tmp & mask1) | (source_0 & ~mask1);
               break;
            }
            default:
               assert(0 && "unhandled ARITH_REG64 subopcode");
            }

            break;
         }
#else
         case MALI_CS_OPCODE_ADD_IMMEDIATE32: {
            cs_unpack(instr, CS_ADD_IMM32, I);
            reg_file.u32[I.destination] = reg_file.u32[I.source] + I.immediate;
            break;
         }

         case MALI_CS_OPCODE_ADD_IMMEDIATE64: {
            cs_unpack(instr, CS_ADD_IMM64, I);

            assert(I.destination % 2 == 0 &&
                   "Destination register should be aligned to 2");
            assert(I.source % 2 == 0 &&
                   "Source register should be aligned to 2");

            reg_file.u64[I.destination / 2] =
               reg_file.u64[I.source / 2] + I.immediate;
            break;
         }

         case MALI_CS_OPCODE_COMPARE_SELECT32: {
            cs_unpack(instr, CS_UMIN32, I);
            reg_file.u32[I.destination] =
               MIN2(reg_file.u32[I.source_1], reg_file.u32[I.source_0]);
            break;
         }
#endif

         default:
            break;
         }
      }
      blk_offs = 0;
   }
   list_delinit(&cur_blk->node);

   uint64_t *instr = &cfg->instrs[ibranch->instr_idx];
   cs_unpack(instr, CS_JUMP, I);

   assert(I.address % 2 == 0 && "Address register should be aligned to 2");

   struct cs_indirect_branch_target target = {
      .address = reg_file.u64[I.address / 2],
      .length = reg_file.u32[I.length],
   };

   util_dynarray_append(&ibranch->targets, target);
}

static void
collect_indirect_branch_targets_recurse(struct cs_code_cfg *cfg,
                                        struct list_head *blk_stack,
                                        BITSET_WORD *track_map,
                                        struct cs_code_block *cur_blk,
                                        int instr_ptr,
                                        struct cs_indirect_branch *ibranch)
{
   for (; instr_ptr >= (int)cur_blk->start; instr_ptr--) {
      assert(instr_ptr >= 0);
      const uint64_t *instr = &cfg->instrs[instr_ptr];
      cs_unpack(instr, CS_BASE, base);
      switch (base.opcode) {
      case MALI_CS_OPCODE_MOVE48: {
         cs_unpack(instr, CS_MOVE48, I);
         BITSET_CLEAR(track_map, I.destination);
         BITSET_CLEAR(track_map, I.destination + 1);
         break;
      }

      case MALI_CS_OPCODE_MOVE32: {
         cs_unpack(instr, CS_MOVE32, I);
         BITSET_CLEAR(track_map, I.destination);
         break;
      }

#if PAN_ARCH >= 13
      case MALI_CS_OPCODE_ARITH_IMM32: {
         cs_unpack(instr, CS_ARITH_IMM32_BASE, I);
         if (BITSET_TEST(track_map, I.destination)) {
            BITSET_SET(track_map, I.source);
            BITSET_CLEAR(track_map, I.destination);
         }
         break;
      }

      case MALI_CS_OPCODE_ARITH_IMM64: {
         cs_unpack(instr, CS_ARITH_IMM64_BASE, I);
         if (BITSET_TEST(track_map, I.destination)) {
            BITSET_SET(track_map, I.source);
            BITSET_CLEAR(track_map, I.destination);
         }
         if (BITSET_TEST(track_map, I.destination + 1)) {
            BITSET_SET(track_map, I.source + 1);
            BITSET_CLEAR(track_map, I.destination + 1);
         }
         break;
      }

      case MALI_CS_OPCODE_ARITH_REG32: {
         cs_unpack(instr, CS_ARITH_REG32_BASE, I);
         if (BITSET_TEST(track_map, I.destination)) {
            BITSET_SET(track_map, I.source_1);
            BITSET_SET(track_map, I.source_0);
            BITSET_CLEAR(track_map, I.destination);
         }
         break;
      }

      case MALI_CS_OPCODE_ARITH_REG64: {
         cs_unpack(instr, CS_ARITH_REG64_BASE, I);
         if (BITSET_TEST(track_map, I.destination)) {
            BITSET_SET(track_map, I.source_1);
            BITSET_SET(track_map, I.source_0);
            BITSET_CLEAR(track_map, I.destination);
         }
         if (BITSET_TEST(track_map, I.destination + 1)) {
            BITSET_SET(track_map, I.source_1 + 1);
            BITSET_SET(track_map, I.source_0 + 1);
            BITSET_CLEAR(track_map, I.destination + 1);
         }
         break;
      }
#else
      case MALI_CS_OPCODE_ADD_IMMEDIATE32: {
         cs_unpack(instr, CS_ADD_IMM32, I);
         if (BITSET_TEST(track_map, I.destination)) {
            BITSET_SET(track_map, I.source);
            BITSET_CLEAR(track_map, I.destination);
         }
         break;
      }

      case MALI_CS_OPCODE_ADD_IMMEDIATE64: {
         cs_unpack(instr, CS_ADD_IMM64, I);
         if (BITSET_TEST(track_map, I.destination)) {
            BITSET_SET(track_map, I.source);
            BITSET_CLEAR(track_map, I.destination);
         }
         if (BITSET_TEST(track_map, I.destination + 1)) {
            BITSET_SET(track_map, I.source + 1);
            BITSET_CLEAR(track_map, I.destination + 1);
         }
         break;
      }

      case MALI_CS_OPCODE_COMPARE_SELECT32: {
         cs_unpack(instr, CS_UMIN32, I);
         if (BITSET_TEST(track_map, I.destination)) {
            BITSET_SET(track_map, I.source_1);
            BITSET_SET(track_map, I.source_0);
            BITSET_CLEAR(track_map, I.destination);
         }
         break;
      }
#endif

      case MALI_CS_OPCODE_LOAD_MULTIPLE: {
         cs_unpack(instr, CS_LOAD_MULTIPLE, I);
         for (unsigned i = 0; i < 16; i++) {
            if ((I.mask & BITFIELD_BIT(i)) &&
                BITSET_TEST(track_map, I.base_register + i)) {
               ibranch->has_unknown_targets = true;
               return;
            }
         }
         break;
      }

      case MALI_CS_OPCODE_PROGRESS_LOAD: {
         cs_unpack(instr, CS_PROGRESS_LOAD, I);
         for (unsigned i = 0; i < 16; i++) {
            if (BITSET_TEST(track_map, I.destination) ||
                BITSET_TEST(track_map, I.destination + 1)) {
               ibranch->has_unknown_targets = true;
               return;
            }
         }
         break;
      }

      default:
         break;
      }

      if (__bitset_is_empty(track_map, BITSET_WORDS(256))) {
         record_indirect_branch_target(cfg, blk_stack, cur_blk,
                                       instr_ptr - cur_blk->start, ibranch);
         return;
      }
   }

   assert(!__bitset_is_empty(track_map, BITSET_WORDS(256)));

   if (util_dynarray_num_elements(&cur_blk->predecessors, unsigned) == 0) {
      ibranch->has_unknown_targets = true;
      return;
   }

   list_add(&cur_blk->node, blk_stack);
   util_dynarray_foreach(&cur_blk->predecessors, unsigned, pred) {
      struct cs_code_block *prev_blk = cfg->blk_map[*pred];

      /* If the node is already in the block stack, we skip it
       * and consider this path leading to an unknown target. */
      if (!list_is_empty(&cur_blk->node)) {
         ibranch->has_unknown_targets = true;
         continue;
      }

      collect_indirect_branch_targets_recurse(
         cfg, blk_stack, track_map, prev_blk,
         prev_blk->start + prev_blk->size - 1, ibranch);
   }
   list_delinit(&cur_blk->node);

   return;
}

static void
collect_indirect_branch_targets(struct cs_code_cfg *cfg,
                                struct cs_indirect_branch *ibranch)
{
   uint64_t *instr = &cfg->instrs[ibranch->instr_idx];
   struct cs_code_block *cur_blk = cfg->blk_map[ibranch->instr_idx];
   struct list_head blk_stack;
   BITSET_DECLARE(track_map, 256) = {0};

   list_inithead(&blk_stack);

   cs_unpack(instr, CS_JUMP, I);
   BITSET_SET(track_map, I.address);
   BITSET_SET(track_map, I.address + 1);
   BITSET_SET(track_map, I.length);

   collect_indirect_branch_targets_recurse(cfg, &blk_stack, track_map, cur_blk,
                                           ibranch->instr_idx - 1, ibranch);
}

static struct cs_code_cfg *
get_cs_cfg(struct pandecode_context *ctx, struct hash_table_u64 *symbols,
           uint64_t bin, uint32_t bin_size)
{
   uint32_t instr_count = bin_size / sizeof(uint64_t);
   struct cs_code_cfg *cfg = _mesa_hash_table_u64_search(symbols, bin);

   if (cfg) {
      assert(cfg->instr_count == instr_count);
      return cfg;
   }

   uint64_t *instrs = pandecode_fetch_gpu_mem(ctx, bin, bin_size);

   cfg = rzalloc(symbols, struct cs_code_cfg);
   _mesa_hash_table_u64_insert(symbols, bin, cfg);

   util_dynarray_init(&cfg->indirect_branches, cfg);

   cfg->blk_map = rzalloc_array(cfg, struct cs_code_block *, instr_count);
   cfg->instrs = instrs;
   cfg->instr_count = instr_count;

   struct cs_code_block *block = cs_code_block_alloc(cfg, 0, 0);

   for (unsigned i = 0; i < instr_count; i++) {
      const uint64_t *instr = &instrs[i];

      if (!cfg->blk_map[i]) {
         cfg->blk_map[i] = block;
         block->size++;
      } else {
         if (block->successors[0] == ~0)
            block->successors[0] = i;

         block = cfg->blk_map[i];
         util_dynarray_append(&block->predecessors, i - 1);
      }

      cs_unpack(instr, CS_BASE, base);

      if (base.opcode == MALI_CS_OPCODE_JUMP ||
          base.opcode == MALI_CS_OPCODE_CALL) {
         struct cs_indirect_branch ibranch = {
            .instr_idx = i,
         };

         util_dynarray_append(&cfg->indirect_branches, ibranch);
      }

      if (base.opcode != MALI_CS_OPCODE_BRANCH)
         continue;

      cs_unpack(instr, CS_BRANCH, I);

      unsigned target = MIN2(i + 1 + I.offset, instr_count);

      /* If the target of the branch is the next instruction, it's just a NOP,
       * and we consider it the same block. */
      if (target == i + 1)
         continue;

      if (I.offset < 0 && cfg->blk_map[target]->start != target) {
         struct cs_code_block *old = cfg->blk_map[target];
         struct cs_code_block *new =
            cs_code_block_alloc(cfg, target, old->start + old->size - target);

         util_dynarray_append(&new->predecessors, target - 1);
         memcpy(&new->successors, &old->successors, sizeof(new->successors));

         old->successors[0] = target;
         old->successors[1] = ~0;
         old->size = new->start - old->start;

         for (unsigned j = 0; j <= new->size; j++)
            cfg->blk_map[new->start + j] = new;
      }

      if (I.offset > 0 && target < instr_count && !cfg->blk_map[target]) {
         struct cs_code_block *new = cs_code_block_alloc(cfg, target, 1);

         cfg->blk_map[target] = new;
         util_dynarray_append(&new->predecessors, i);
      }

      block->successors[0] = target;
      if (I.condition != MALI_CS_CONDITION_ALWAYS)
         block->successors[1] = i + 1;

      block = cs_code_block_alloc(cfg, i + 1, 0);

      if (target == i + 1 || I.condition != MALI_CS_CONDITION_ALWAYS) {
         util_dynarray_append(&block->predecessors, i);
      }
   }

   util_dynarray_foreach(&cfg->indirect_branches, struct cs_indirect_branch,
                         ibranch)
   {
      collect_indirect_branch_targets(cfg, ibranch);
      util_dynarray_foreach(&ibranch->targets, struct cs_indirect_branch_target,
                            target)
      {
         get_cs_cfg(ctx, symbols, target->address, target->length);
      }
   }

   return cfg;
}

static void
print_cs_binary(struct pandecode_context *ctx, uint64_t bin,
                struct cs_code_cfg *cfg, const char *name)
{
   pandecode_log(ctx, "%s@%" PRIx64 "{\n", name, bin);
   unsigned ibranch_idx = 0;

   ctx->indent++;
   for (unsigned i = 0; i < cfg->instr_count; i++) {
      if (i && cfg->blk_map[i - 1] != cfg->blk_map[i]) {
         ctx->indent--;
         pandecode_log(ctx, "label_%" PRIx64 ":\n", bin + i * sizeof(uint64_t));
         ctx->indent++;
      }

      pandecode_make_indent(ctx);
      print_cs_instr(ctx->dump_stream, &cfg->instrs[i]);
      cs_unpack(&cfg->instrs[i], CS_BASE, base);
      switch (base.opcode) {
      case MALI_CS_OPCODE_JUMP:
      case MALI_CS_OPCODE_CALL: {
         struct cs_indirect_branch *ibranch = util_dynarray_element(
            &cfg->indirect_branches, struct cs_indirect_branch, ibranch_idx);

         assert(ibranch->instr_idx == i);
         fprintf(ctx->dump_stream, " // ");
         util_dynarray_foreach(&ibranch->targets,
                               struct cs_indirect_branch_target, target)
         {
            fprintf(ctx->dump_stream, "%scs@%" PRIx64,
                    target == ibranch->targets.data ? "" : ",",
                    target->address);
         }
         if (ibranch->has_unknown_targets)
            fprintf(ctx->dump_stream, "%s??", ibranch->targets.size ? "," : "");
         ibranch_idx++;
         break;
      }

      case MALI_CS_OPCODE_BRANCH: {
         cs_unpack(&cfg->instrs[i], CS_BRANCH, I);
         fprintf(ctx->dump_stream, " // ");

         unsigned target = i + 1 + I.offset;

         if (target < cfg->instr_count)
            fprintf(ctx->dump_stream, "label_%" PRIx64,
                    bin + (target * sizeof(uint64_t)));
         else
            fprintf(ctx->dump_stream, "end_of_cs");
         break;
      }

#if PAN_ARCH >= 12
      case MALI_CS_OPCODE_RUN_IDVS2:
#else
      case MALI_CS_OPCODE_RUN_IDVS:
#endif
      case MALI_CS_OPCODE_RUN_FRAGMENT:
      case MALI_CS_OPCODE_RUN_COMPUTE:
      case MALI_CS_OPCODE_RUN_COMPUTE_INDIRECT:
         fprintf(ctx->dump_stream, " // tracepoint_%" PRIx64,
                 bin + (i * sizeof(uint64_t)));
         break;

      default:
         break;
      }

      fprintf(ctx->dump_stream, "\n");
   }
   ctx->indent--;
   pandecode_log(ctx, "} // %s@%" PRIx64 "\n\n", name, bin);
}

void
GENX(pandecode_cs_binary)(struct pandecode_context *ctx, uint64_t bin,
                          uint32_t bin_size, unsigned gpu_id)
{
   if (!bin_size)
      return;

   pandecode_dump_file_open(ctx);

   struct hash_table_u64 *symbols = _mesa_hash_table_u64_create(NULL);
   struct cs_code_cfg *main_cfg = get_cs_cfg(ctx, symbols, bin, bin_size);

   print_cs_binary(ctx, bin, main_cfg, "main_cs");
   hash_table_u64_foreach(symbols, he)
   {
      struct cs_code_cfg *other_cfg = he.data;
      if (other_cfg == main_cfg)
         continue;

      print_cs_binary(ctx, he.key, other_cfg, "cs");
   }

   ralloc_free(symbols);

   pandecode_map_read_write(ctx);
}

void
GENX(pandecode_cs_trace)(struct pandecode_context *ctx, uint64_t trace,
                         uint32_t trace_size, unsigned gpu_id)
{
   pandecode_dump_file_open(ctx);

   void *trace_data = pandecode_fetch_gpu_mem(ctx, trace, trace_size);

   while (trace_size > 0) {
      uint32_t regs[256] = {};
      uint64_t *ip = trace_data;

      uint64_t *instr = pandecode_fetch_gpu_mem(ctx, *ip, sizeof(*instr));

      /* v10 has 96 registers. v12+ have 128. */
      struct queue_ctx qctx = {
         .nr_regs = PAN_ARCH >= 12 ? 128 : 96,
         .regs = regs,
         .ip = instr,
         .end = instr + 1,
         .gpu_id = gpu_id,
      };

      pandecode_make_indent(ctx);
      print_cs_instr(ctx->dump_stream, instr);
      fprintf(ctx->dump_stream, " // from tracepoint_%" PRIx64 "\n", *ip);

      cs_unpack(instr, CS_BASE, base);

      switch (base.opcode) {
#if PAN_ARCH >= 12
      case MALI_CS_OPCODE_RUN_IDVS2: {
         struct cs_run_idvs2_trace *idvs_trace = trace_data;

         assert(trace_size >= sizeof(idvs_trace));
         cs_unpack(instr, CS_RUN_IDVS2, I);
         memcpy(regs, idvs_trace->sr, sizeof(idvs_trace->sr));

         if (I.draw_id_register_enable)
            regs[I.draw_id] = idvs_trace->draw_id;

         pandecode_run_idvs2(ctx, ctx->dump_stream, &qctx, &I);
         trace_data = idvs_trace + 1;
         trace_size -= sizeof(*idvs_trace);
         break;
      }
#else
      case MALI_CS_OPCODE_RUN_IDVS: {
         struct cs_run_idvs_trace *idvs_trace = trace_data;

         assert(trace_size >= sizeof(idvs_trace));
         cs_unpack(instr, CS_RUN_IDVS, I);
         memcpy(regs, idvs_trace->sr, sizeof(idvs_trace->sr));

         if (I.draw_id_register_enable)
            regs[I.draw_id] = idvs_trace->draw_id;

         pandecode_run_idvs(ctx, ctx->dump_stream, &qctx, &I);
         trace_data = idvs_trace + 1;
         trace_size -= sizeof(*idvs_trace);
         break;
      }
#endif

      case MALI_CS_OPCODE_RUN_FRAGMENT: {
         struct cs_run_fragment_trace *frag_trace = trace_data;

         assert(trace_size >= sizeof(frag_trace));
         cs_unpack(instr, CS_RUN_FRAGMENT, I);
         memcpy(&regs[40], frag_trace->sr, sizeof(frag_trace->sr));
         pandecode_run_fragment(ctx, ctx->dump_stream, &qctx, &I);
         trace_data = frag_trace + 1;
         trace_size -= sizeof(*frag_trace);
         break;
      }

      case MALI_CS_OPCODE_RUN_COMPUTE: {
         struct cs_run_compute_trace *comp_trace = trace_data;

         assert(trace_size >= sizeof(comp_trace));
         cs_unpack(instr, CS_RUN_COMPUTE, I);
         memcpy(regs, comp_trace->sr, sizeof(comp_trace->sr));
         pandecode_run_compute(ctx, ctx->dump_stream, &qctx, &I);
         trace_data = comp_trace + 1;
         trace_size -= sizeof(*comp_trace);
         break;
      }

      case MALI_CS_OPCODE_RUN_COMPUTE_INDIRECT: {
         struct cs_run_compute_trace *comp_trace = trace_data;

         assert(trace_size >= sizeof(comp_trace));
         cs_unpack(instr, CS_RUN_COMPUTE_INDIRECT, I);
         memcpy(regs, comp_trace->sr, sizeof(comp_trace->sr));
         pandecode_run_compute_indirect(ctx, ctx->dump_stream, &qctx, &I);
         trace_data = comp_trace + 1;
         trace_size -= sizeof(*comp_trace);
         break;
      }

      default:
         assert(!"Invalid trace packet");
         break;
      }

      pandecode_log(ctx, "\n");
   }

   fflush(ctx->dump_stream);
   pandecode_map_read_write(ctx);
}
#endif
