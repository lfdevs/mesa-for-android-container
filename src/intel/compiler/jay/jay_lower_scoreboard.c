/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <limits.h>
#include "compiler/brw/brw_eu_defines.h"
#include "util/bitscan.h"
#include "util/bitset.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

#define MAX_KEYS   (JAY_NUM_PHYS_GRF + JAY_NUM_UGPR)
#define NUM_TOKENS (16)

/** SEND scoreboarding */
struct gpr_range {
   unsigned base, width;
};

static inline struct gpr_range
def_to_gpr(jay_function *func, jay_inst *I, jay_def x)
{
   if (x.file == GPR || x.file == UGPR) {
      unsigned base = x.file == UGPR ? func->shader->num_regs[GPR] : 0;
      return (struct gpr_range) { base + x.reg, jay_num_values(x) };
   } else {
      return (struct gpr_range) { 0, 0 };
   }
}

static inline void
sync_sbids(jay_builder *b, uint32_t mask, enum tgl_sbid_mode mode)
{
   if (util_is_power_of_two_nonzero(mask)) {
      jay_SYNC(b, jay_null(), TGL_SYNC_NOP)->dep =
         tgl_swsb_sbid(mode, util_logbase2(mask));
   } else if (mask) {
      jay_SYNC(b, mask, mode == TGL_SBID_DST ? TGL_SYNC_ALLWR : TGL_SYNC_ALLRD);
   }
}

static void
lower_send_local(jay_function *func, jay_block *block)
{
   struct {
      BITSET_DECLARE(reading, MAX_KEYS);
      BITSET_DECLARE(writing, MAX_KEYS);
   } tokens[NUM_TOKENS];

   uint32_t busy = 0;
   unsigned roundrobin = 0;

   jay_foreach_inst_in_block_safe(block, I) {
      jay_builder b = jay_init_builder(func, jay_before_inst(I));
      uint32_t sbid_dst = 0, sbid_src = 0;

      /* Read-after-write */
      jay_foreach_src(I, s) {
         struct gpr_range src = def_to_gpr(func, I, I->src[s]);

         u_foreach_bit(sbid, busy) {
            if (BITSET_TEST_COUNT(tokens[sbid].writing, src.base, src.width)) {
               sbid_dst |= BITFIELD_BIT(sbid);
               busy &= ~BITFIELD_BIT(sbid);
            }
         }
      }

      /* Write-after-write & write-after-read */
      jay_foreach_dst(I, d) {
         struct gpr_range dst = def_to_gpr(func, I, I->dst);

         u_foreach_bit(sbid, busy) {
            if (BITSET_TEST_COUNT(tokens[sbid].writing, dst.base, dst.width)) {
               sbid_dst |= BITFIELD_BIT(sbid);
            } else if (BITSET_TEST_COUNT(tokens[sbid].reading, dst.base,
                                         dst.width)) {
               sbid_src |= BITFIELD_BIT(sbid);
               BITSET_ZERO(tokens[sbid].reading);
            }
         }
      }

      if (I->op == JAY_OPCODE_SEND && !jay_send_eot(I)) {
         unsigned sbid = (roundrobin++) % NUM_TOKENS;
         jay_set_send_sbid(I, sbid);

         if (!(busy & BITFIELD_BIT(sbid))) {
            busy |= BITFIELD_BIT(sbid);
            BITSET_ZERO(tokens[sbid].writing);
            BITSET_ZERO(tokens[sbid].reading);
         }

         /* SBID.set implies SBID.dst (which implies SBID.src), so elide */
         sbid_dst &= ~BITFIELD_BIT(sbid);
         sbid_src &= ~BITFIELD_BIT(sbid);

         struct gpr_range dst = def_to_gpr(func, I, I->dst);
         BITSET_SET_COUNT(tokens[sbid].writing, dst.base, dst.width);

         jay_foreach_src(I, s) {
            struct gpr_range src = def_to_gpr(func, I, I->src[s]);
            BITSET_SET_COUNT(tokens[sbid].reading, src.base, src.width);
         }

         /* Barriers are non-EOT gateway messages. Insert the needed SYNC */
         if (jay_send_sfid(I) == BRW_SFID_MESSAGE_GATEWAY) {
            b.cursor = jay_after_inst(I);
            jay_SYNC(&b, jay_null(), TGL_SYNC_BAR);
         }
      } else if (I->op == JAY_OPCODE_SCHEDULE_BARRIER) {
         sbid_dst |= busy;
      }

      b.cursor = jay_before_inst(I);
      assert(((sbid_dst & sbid_src) == 0) && "by construction");

      busy &= ~sbid_dst;
      sync_sbids(&b, sbid_dst, TGL_SBID_DST);
      sync_sbids(&b, sbid_src, TGL_SBID_SRC);

      if (I->op == JAY_OPCODE_SCHEDULE_BARRIER) {
         /* Lowered above into a sync, but removed late to keep the cursor */
         jay_remove_instruction(I);
      }
   }

   /* Sync on block boundaries. */
   if (block != jay_last_block(func)) {
      jay_builder b = jay_init_builder(func, jay_before_jump(block));
      sync_sbids(&b, busy, TGL_SBID_DST);
   }
}

/**
 * Regdist scoreboarding
 *
 * Register access is tracked per pipe, with 0 (NONE) having data on the writer
 * packed into a u32 with the following macros.
 */
#define make_writer(pipe, ip) (((uint32_t) ip << 3) | (uint32_t) (pipe))
#define writer_ip(writer)     (writer >> 3)
#define writer_pipe(writer)   (enum tgl_pipe)(writer & BITFIELD_MASK(3))

#define TGL_NUM_PIPES (TGL_PIPE_ALL)
typedef uint32_t u32_per_pipe[TGL_NUM_PIPES];

struct swsb_state {
   unsigned ip[TGL_NUM_PIPES];
   unsigned last_shape[TGL_NUM_PIPES];

   /* finished_ip[X][Y] = ip means from the perspective of pipe X, ip on pipe Y
    * has already been waited on.
    */
   unsigned finished_ip[TGL_NUM_PIPES][TGL_NUM_PIPES];
   u32_per_pipe *access;
};

static enum tgl_pipe
inst_exec_pipe(const struct intel_device_info *devinfo, jay_inst *I)
{
   if (I->op == JAY_OPCODE_SEND || jay_op_is_control_flow(I->op)) {
      return TGL_PIPE_NONE;
   } else if (I->op == JAY_OPCODE_MATH) {
      return TGL_PIPE_MATH;
   } else if (I->type == JAY_TYPE_F64) {
      return TGL_PIPE_LONG;
   } else if (jay_type_is_any_float(I->type)) {
      return TGL_PIPE_FLOAT;
   } else {
      return TGL_PIPE_INT;
   }
}

/**
 * Return the RegDist pipeline the hardware will synchronize with if no
 * pipeline information is provided in the SWSB annotation of an
 * instruction (e.g. when TGL_PIPE_NONE is specified in tgl_swsb).
 */
static enum tgl_pipe
inferred_sync_pipe(const struct intel_device_info *devinfo, const jay_inst *I)
{
   enum jay_type type = I->num_srcs ? jay_src_type(I, 0) : JAY_TYPE_UNTYPED;

   if (I->op == JAY_OPCODE_SEND) {
      return TGL_PIPE_NONE;
   } else if (devinfo->verx10 >= 125 && type == JAY_TYPE_F64) {
      /* Avoid emitting (RegDist, SWSB) annotations for long instructions on
       * platforms where they are unordered as they may not be allowed.
       */
      return devinfo->has_64bit_float ? TGL_PIPE_LONG : TGL_PIPE_NONE;
   } else {
      return jay_type_is_any_float(type) ? TGL_PIPE_FLOAT : TGL_PIPE_INT;
   }
}

/*
 * Return the maximum ALU distance to consider. Anything further is guaranteed
 * to have already written its result by the time we issue. These values are not
 * in the bspec but are #define'd in IGC as SWSB_MAX_*_DEPENDENCE_DISTANCE.
 *
 * Confusingly, IGC also defines SWSB_MAX_ALU_DEPENDENCE_DISTANCE_VALUE as 7.
 * There is a discrepency between what the hardware does and what we can encode.
 * Any writes from 11 instructions ago are guaranteed to have landed, whereas if
 * you need to sync, you can only sync with something up to 7 instructions ago
 * (and implicitly, everything in-order before that).
 *
 * These are conservative values. Some archeology suggests the real values may
 * be lower on some platforms but for now we match IGC to be safe.
 */
static inline unsigned
max_dependence(enum tgl_pipe pipe)
{
   return pipe == TGL_PIPE_SCALAR ? 2 :
          pipe == TGL_PIPE_MATH   ? 18 :
          pipe == TGL_PIPE_LONG   ? 15 :
                                    11;
}

static void
depend_on_writer(struct swsb_state *state,
                 struct gpr_range r,
                 unsigned *dep,
                 enum tgl_pipe exec,
                 bool except_exec)
{
   for (unsigned i = 0; i < r.width; ++i) {
      uint32_t w = state->access[r.base + i][0];
      enum tgl_pipe write = writer_pipe(w);

      /* We omit write-after-{read,write} dependencies (except_exec) within a
       * single execution pipe, since each pipe is internally in-order. We also
       * omit dependencies on the same pipe that are too far to be relevant.
       */
      if (write != exec ||
          (!except_exec &&
           writer_ip(w) + max_dependence(exec) > state->ip[write])) {

         dep[write] = MAX2(dep[write], writer_ip(w));
      }
   }
}

#define jay_foreach_pipe(pipe)                                                 \
   for (unsigned pipe = 1; pipe < TGL_NUM_PIPES; ++pipe)

static void
lower_regdist_local(jay_function *func, jay_block *block, u32_per_pipe *access)
{
   struct swsb_state state = { .access = access };
   jay_inst *last_sync = NULL;

   jay_foreach_inst_in_block_safe(block, I) {
      enum tgl_pipe exec_pipe = inst_exec_pipe(func->shader->devinfo, I);
      unsigned dep[TGL_NUM_PIPES] = { 0 };
      if (I->op == JAY_OPCODE_SYNC) {
         last_sync = I;
         continue;
      }

      jay_foreach_dst(I, def) {
         struct gpr_range r = def_to_gpr(func, I, def);
         depend_on_writer(&state, r, dep, exec_pipe, true /* except_pipe */);

         for (unsigned i = 0; i < r.width; ++i) {
            jay_foreach_pipe(p) {
               if (p != exec_pipe) {
                  dep[p] = MAX2(dep[p], state.access[r.base + i][p]);
               }
            }
         }
      }

      /* Read-after-write */
      jay_foreach_src(I, s) {
         depend_on_writer(&state, def_to_gpr(func, I, I->src[s]), dep,
                          exec_pipe, false);
      }

      /* If dependency P implies dependency Q, drop dependency Q to avoid
       * unnecessary annotations.
       */
      jay_foreach_pipe(p) {
         if (dep[p]) {
            jay_foreach_pipe(q) {
               if (p != q && dep[q] && state.finished_ip[p][q] >= dep[q]) {
                  dep[q] = 0;
               }
            }
         }
      }

      uint32_t wait_pipes = 0;
      unsigned min_delta = 7;

      jay_foreach_pipe(p) {
         if (dep[p] && (exec_pipe == TGL_PIPE_NONE /* TODO: Sends */ ||
                        dep[p] > state.finished_ip[exec_pipe][p])) {

            min_delta = MIN2(min_delta, state.ip[p] - dep[p] + 1);
            wait_pipes |= BITFIELD_BIT(p);
         }
      }

      /* We'll wait on the unioned dependency. Update the tracking for that. */
      u_foreach_bit(p, wait_pipes) {
         state.finished_ip[exec_pipe][p] = state.ip[p] + 1 - min_delta;
      }

      uint32_t last_pipe = util_logbase2(wait_pipes);
      bool single_wait = wait_pipes == BITFIELD_BIT(last_pipe);

      /* If we're SIMD split the same way as our dependency, we can relax the
       * dependency to have each half wait in parallel. We could do even better
       * with more tracking but this should be good enough for now.
       */
      unsigned simd_split = jay_simd_split(func->shader, I);
      unsigned shape = ((simd_split << 2) | jay_macro_length(I)) + 1;
      bool same_shape = state.last_shape[last_pipe] == shape;

      if (simd_split && same_shape && single_wait && min_delta == 1) {
         min_delta += ((1 << simd_split) - 1) * jay_macro_length(I);
         I->replicate_dep = true;
         I->decrement_dep = last_pipe != exec_pipe;
      }

      bool has_sbid = I->op == JAY_OPCODE_SEND && !jay_send_eot(I);
      I->dep = (struct tgl_swsb) {
         .sbid = has_sbid ? jay_send_sbid(I) : 0,
         .mode = has_sbid ? TGL_SBID_SET : TGL_SBID_NULL,
         .regdist = wait_pipes ? min_delta : 0,
         .pipe = single_wait && (!has_sbid ||
                                 last_pipe == TGL_PIPE_FLOAT ||
                                 last_pipe == TGL_PIPE_INT) ?
                    last_pipe :
                    TGL_PIPE_ALL,
      };

      /* Fold the immediate preceding SYNC.nop into this instruction, allowing
       * us to wait on both ALU and a SEND in the same annotation. We cannot do
       * this safely in the presence of predication or SIMD splitting that could
       * cause any part of the instruction to get shot down, skipping the sync
       * for future instructions (at least not without more tricky logic).
       */
      if (last_sync &&
          jay_sync_op(last_sync) == TGL_SYNC_NOP &&
          I->dep.mode == TGL_SBID_NULL &&
          !I->predication &&
          !jay_simd_split(func->shader, I) &&
          (I->dep.regdist == 0 ||
           inferred_sync_pipe(func->shader->devinfo, I) == I->dep.pipe)) {

         assert(last_sync->dep.regdist == 0);
         assert(last_sync->dep.pipe == TGL_PIPE_NONE);

         I->dep.mode = last_sync->dep.mode;
         I->dep.sbid = last_sync->dep.sbid;

         jay_remove_instruction(last_sync);
      }

      if (exec_pipe != TGL_PIPE_NONE) {
         /* Advance the IP by the number of physical instructions emitted */
         state.ip[exec_pipe] +=
            jay_macro_length(I) << jay_simd_split(func->shader, I);

         struct gpr_range r = def_to_gpr(func, I, I->dst);
         uint32_t now = make_writer(exec_pipe, state.ip[exec_pipe]);

         for (unsigned i = 0; i < r.width; ++i) {
            state.access[r.base + i][0] = now;
         }

         jay_foreach_src(I, s) {
            struct gpr_range r = def_to_gpr(func, I, I->src[s]);
            for (unsigned i = 0; i < r.width; ++i) {
               state.access[r.base + i][exec_pipe] = state.ip[exec_pipe];
            }
         }

         state.last_shape[exec_pipe] = shape;
      }

      last_sync = NULL;
   }

   /* Sync on block boundaries. */
   jay_inst *first = jay_first_inst(block);
   if (block != jay_first_block(func) && first && first->op != JAY_OPCODE_SEND) {
      first->dep = tgl_swsb_regdist(1);
   }
}

/*
 * Trivial scoreboard lowering pass for debugging use. Stalls after every
 * instruction and assigns SBID zero to all messages.
 */
static void
lower_trivial(jay_function *func)
{
   jay_foreach_inst_in_func_safe(func, block, I) {
      if (I->op == JAY_OPCODE_SEND && !jay_send_eot(I)) {
         I->dep = tgl_swsb_dst_dep(tgl_swsb_sbid(TGL_SBID_SET, 0), 1);

         jay_builder b = jay_init_builder(func, jay_after_inst(I));
         sync_sbids(&b, BITFIELD_BIT(0), TGL_SBID_DST);
      } else {
         I->dep = tgl_swsb_regdist(1);
      }
   }
}

void
jay_lower_scoreboard(jay_shader *s)
{
   uint32_t nr_keys = s->num_regs[GPR] + s->num_regs[UGPR];
   assert(nr_keys <= MAX_KEYS && "SENDs use uninitialized stack allocation");
   u32_per_pipe *access = malloc(sizeof(*access) * nr_keys);

   jay_foreach_function(s, func) {
      if (jay_debug & JAY_DBG_SYNC) {
         lower_trivial(func);
      } else {
         jay_foreach_block(func, block) {
            memset(access, 0, sizeof(*access) * nr_keys);
            lower_send_local(func, block);
            lower_regdist_local(func, block, access);
         }
      }
   }

   free(access);
}
