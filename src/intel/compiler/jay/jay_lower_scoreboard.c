/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <limits.h>
#include "compiler/brw/brw_eu_defines.h"
#include "compiler/gen/gen.h"
#include "util/bitscan.h"
#include "util/bitset.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

#define MAX_KEYS   (JAY_NUM_PHYS_GRF + JAY_NUM_UGPR)
#define NUM_TOKENS (16)

/** SBID scoreboarding */
struct key {
   unsigned base, width;
};

static inline struct key
def_to_key(jay_function *func, jay_inst *I, jay_def x)
{
   if (x.file == GPR || x.file == UGPR) {
      unsigned base = x.file == UGPR ? func->shader->num_regs[GPR] : 0;
      return (struct key) { base + x.reg, jay_num_values(x) };
   } else if (x.file == ACCUM || x.file == UACCUM) {
      unsigned base =
         func->shader->num_regs[GPR] + func->shader->num_regs[UGPR];

      return (struct key) { base + (x.reg / 2), jay_num_values(x) };
   } else {
      return (struct key) { 0, 0 };
   }
}

static inline void
sync_sbids(jay_builder *b, uint32_t mask, gen_sbid_mode mode)
{
   if (util_is_power_of_two_nonzero(mask)) {
      jay_SYNC(b, jay_null(), TGL_SYNC_NOP)->dep =
         gen_swsb_sbid(mode, util_logbase2(mask));
   } else if (mask) {
      jay_SYNC(b, mask, mode == GEN_SBID_DST ? TGL_SYNC_ALLWR : TGL_SYNC_ALLRD);
   }
}

static inline bool
jay_inst_is_unordered(const jay_inst *I)
{
   return I->op == JAY_OPCODE_SEND || I->op == JAY_OPCODE_DPAS;
}

static inline bool
jay_inst_has_sbid(const jay_inst *I)
{
   return jay_inst_is_unordered(I) &&
          !(I->op == JAY_OPCODE_SEND && jay_send_eot(I));
}

static inline unsigned
jay_inst_sbid(const jay_inst *I)
{
   return I->op == JAY_OPCODE_SEND ? jay_send_sbid(I) : jay_dpas_sbid(I);
}

static inline void
jay_inst_set_sbid(jay_inst *I, unsigned sbid)
{
   if (I->op == JAY_OPCODE_SEND)
      jay_set_send_sbid(I, sbid);
   else
      jay_set_dpas_sbid(I, sbid);
}

static void
lower_sbid_local(jay_function *func, jay_block *block)
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
         struct key src = def_to_key(func, I, I->src[s]);

         u_foreach_bit(sbid, busy) {
            if (BITSET_TEST_COUNT(tokens[sbid].writing, src.base, src.width)) {
               sbid_dst |= BITFIELD_BIT(sbid);
               busy &= ~BITFIELD_BIT(sbid);
            }
         }
      }

      /* Write-after-write & write-after-read */
      jay_foreach_dst(I, d) {
         struct key dst = def_to_key(func, I, I->dst);

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

      if (jay_inst_has_sbid(I)) {
         unsigned sbid = (roundrobin++) % NUM_TOKENS;
         jay_inst_set_sbid(I, sbid);

         if (!(busy & BITFIELD_BIT(sbid))) {
            busy |= BITFIELD_BIT(sbid);
            BITSET_ZERO(tokens[sbid].writing);
            BITSET_ZERO(tokens[sbid].reading);
         }

         /* SBID.set implies SBID.dst (which implies SBID.src), so elide */
         sbid_dst &= ~BITFIELD_BIT(sbid);
         sbid_src &= ~BITFIELD_BIT(sbid);

         struct key dst = def_to_key(func, I, I->dst);
         BITSET_SET_COUNT(tokens[sbid].writing, dst.base, dst.width);

         jay_foreach_src(I, s) {
            struct key src = def_to_key(func, I, I->src[s]);
            BITSET_SET_COUNT(tokens[sbid].reading, src.base, src.width);
         }

         /* Barriers are non-EOT gateway messages. Insert the needed SYNC */
         if (I->op == JAY_OPCODE_SEND &&
             jay_send_sfid(I) == GEN_SFID_MESSAGE_GATEWAY) {
            b.cursor = jay_after_inst(I);
            jay_SYNC(&b, jay_null(), TGL_SYNC_BAR);
         }
      } else if (I->op == JAY_OPCODE_SCHEDULE_BARRIER) {
         sbid_dst |= busy;
      }

      b.cursor = jay_before_inst(I);
      assert(((sbid_dst & sbid_src) == 0) && "by construction");

      busy &= ~sbid_dst;
      sync_sbids(&b, sbid_dst, GEN_SBID_DST);
      sync_sbids(&b, sbid_src, GEN_SBID_SRC);

      if (I->op == JAY_OPCODE_SCHEDULE_BARRIER) {
         /* Lowered above into a sync, but removed late to keep the cursor */
         jay_remove_instruction(I);
      }
   }

   /* Sync on block boundaries. */
   if (block != jay_last_block(func)) {
      jay_builder b = jay_init_builder(func, jay_after_block_logical(block));
      sync_sbids(&b, busy, GEN_SBID_DST);
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
#define writer_pipe(writer)   (gen_pipe)(writer & BITFIELD_MASK(3))

#define GEN_NUM_PIPES (GEN_PIPE_ALL)
typedef uint32_t u32_per_pipe[GEN_NUM_PIPES];

struct swsb_state {
   uint32_t nr_keys;
   unsigned ip[GEN_NUM_PIPES];
   unsigned last_shape[GEN_NUM_PIPES];

   /* finished_ip[X / GEN_NUM_PIPES + SBID][Y] = ip means from the perspective
    * of pipe X or send SBID X, ip on pipe Y has already been waited on.
    */
   unsigned finished_ip[GEN_NUM_PIPES + NUM_TOKENS][GEN_NUM_PIPES];
   u32_per_pipe *access;

   jay_inst *last_sync;
};

static gen_pipe
inst_exec_pipe(const struct intel_device_info *devinfo, jay_inst *I)
{
   return jay_inst_is_unordered(I)       ? GEN_PIPE_NONE :
          I->op == JAY_OPCODE_MATH       ? GEN_PIPE_MATH :
          I->type == JAY_TYPE_F64        ? GEN_PIPE_LONG :
          jay_type_is_any_float(I->type) ? GEN_PIPE_FLOAT :
                                           GEN_PIPE_INT;
}

/**
 * Return the RegDist pipeline the hardware will synchronize with if no
 * pipeline information is provided in the SWSB annotation of an
 * instruction (e.g. when GEN_PIPE_NONE is specified in gen_swsb).
 */
static gen_pipe
inferred_sync_pipe(const struct intel_device_info *devinfo, const jay_inst *I)
{
   enum jay_type type = I->num_srcs ? jay_src_type(I, 0) : JAY_TYPE_UNTYPED;

   if (I->op == JAY_OPCODE_SEND) {
      return GEN_PIPE_NONE;
   } else if (I->op == JAY_OPCODE_DPAS) {
      return jay_type_is_any_float(jay_dpas_acc_type(I)) ? GEN_PIPE_FLOAT :
                                                           GEN_PIPE_INT;
   } else if (devinfo->verx10 >= 125 && type == JAY_TYPE_F64) {
      /* Avoid emitting (RegDist, SWSB) annotations for long instructions on
       * platforms where they are unordered as they may not be allowed.
       */
      return devinfo->has_64bit_float ? GEN_PIPE_LONG : GEN_PIPE_NONE;
   } else {
      return jay_type_is_any_float(type) ? GEN_PIPE_FLOAT : GEN_PIPE_INT;
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
max_dependence(gen_pipe pipe)
{
   return pipe == GEN_PIPE_SCALAR ? 2 :
          pipe == GEN_PIPE_MATH   ? 18 :
          pipe == GEN_PIPE_LONG   ? 15 :
                                    11;
}

static void
depend_on_writer(struct swsb_state *state,
                 struct key r,
                 unsigned *dep,
                 gen_pipe exec,
                 bool except_exec)
{
   for (unsigned i = 0; i < r.width; ++i) {
      assert(r.base + i < state->nr_keys);
      uint32_t w = state->access[r.base + i][0];
      gen_pipe write = writer_pipe(w);

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
   for (unsigned pipe = 1; pipe < GEN_NUM_PIPES; ++pipe)

static void
lower_regdist(jay_function *func, jay_inst *I, struct swsb_state *ctx)
{
   if (I->op == JAY_OPCODE_SYNC) {
      ctx->last_sync = I;
      uint32_t sbid_mask = 0;
      if (jay_sync_op(I) == TGL_SYNC_NOP) {
         /* The SYNC.nops added by this function that are RegDist-only, are
          * added *before* the instruction so are not seen here.
          */
         assert(I->dep.mode != GEN_SBID_NULL);
         sbid_mask = BITFIELD_BIT(I->dep.sbid);
      } else if (jay_sync_op(I) == TGL_SYNC_ALLRD ||
                 jay_sync_op(I) == TGL_SYNC_ALLWR) {
         sbid_mask = jay_as_uint(I->src[0]);
      }

      /* Syncs execute on all pipes, so any regdist that the synced SEND waited
       * on gets cleared for all pipes. This reduces annotations.
       */
      u_foreach_bit(sbid, sbid_mask) {
         jay_foreach_pipe(p) {
            jay_foreach_pipe(q) {
               ctx->finished_ip[p][q] =
                  MAX2(ctx->finished_ip[p][q],
                       ctx->finished_ip[GEN_NUM_PIPES + sbid][q]);
            }
         }
      }

      return;
   }

   gen_pipe exec_pipe = inst_exec_pipe(func->shader->devinfo, I);
   unsigned dep[GEN_NUM_PIPES] = { 0 };
   jay_def dsts[3] = { I->dst, I->cond_flag };

   /* MUL_32 is a macro implicitly clobbering acc0/acc1 */
   if (I->op == JAY_OPCODE_MUL_32) {
      unsigned n = func->shader->dispatch_width < 32 ? 2 : 1;

      dsts[2] = jay_bare_reg(ACCUM, 0);
      dsts[2].num_values_m1 = n - 1;
   }

   for (unsigned i = 0; i < ARRAY_SIZE(dsts); ++i) {
      struct key r = def_to_key(func, I, dsts[i]);
      depend_on_writer(ctx, r, dep, exec_pipe, true /* except_pipe */);

      for (unsigned i = 0; i < r.width; ++i) {
         jay_foreach_pipe(p) {
            if (p != exec_pipe) {
               dep[p] = MAX2(dep[p], ctx->access[r.base + i][p]);
            }
         }
      }
   }

   /* Read-after-write. The hardware scoreboards accumulators within a pipe, so
    * we set except_pipe for that to omit those annotations. The hardware does
    * *not* scoreboard accumulators across pipes so we can't just ignore
    * accumulators when scoreboarding. For example, the I@1 annotation is
    * required in the following code:
    *
    * (16)        mul.s32 acc0, g26, g24<16,8,2>:u16                  │
    * (32)        mad.f32 acc0, u8.6, u8.8, g20                       │ I@1
    */
   jay_foreach_src(I, s) {
      depend_on_writer(ctx, def_to_key(func, I, I->src[s]), dep, exec_pipe,
                       I->src[s].file == ACCUM /* except_pipe */);
   }

   /* If dependency P implies dependency Q, drop dependency Q to avoid
    * unnecessary annotations.
    */
   jay_foreach_pipe(p) {
      if (dep[p]) {
         jay_foreach_pipe(q) {
            if (p != q && dep[q] && ctx->finished_ip[p][q] >= dep[q]) {
               dep[q] = 0;
            }
         }
      }
   }

   uint32_t wait_pipes = 0;
   unsigned min_delta = 7;

   jay_foreach_pipe(p) {
      if (dep[p] && (exec_pipe == GEN_PIPE_NONE ||
                     dep[p] > ctx->finished_ip[exec_pipe][p])) {

         min_delta = MIN2(min_delta, ctx->ip[p] - dep[p] + 1);
         wait_pipes |= BITFIELD_BIT(p);
      }
   }

   /* Unordered instructions are modelled as a pipe per SBID for
    * finished_ip purposes.
    */
   unsigned generalized_pipe = exec_pipe;
   if (jay_inst_is_unordered(I)) {
      generalized_pipe = GEN_NUM_PIPES + jay_inst_sbid(I);
   }

   /* We'll wait on the unioned dependency. Update the tracking for that. */
   u_foreach_bit(p, wait_pipes) {
      ctx->finished_ip[generalized_pipe][p] = ctx->ip[p] + 1 - min_delta;
   }

   uint32_t last_pipe = util_logbase2(wait_pipes);
   bool single_wait = wait_pipes == BITFIELD_BIT(last_pipe);

   /* If we're SIMD split the same way as our dependency, we can relax the
    * dependency to have each half wait in parallel. We could do even better
    * with more tracking but this should be good enough for now.
    */
   unsigned simd_split = jay_simd_split(func->shader, I);
   unsigned shape = ((simd_split << 2) | jay_macro_length(I)) + 1;
   bool same_shape = ctx->last_shape[last_pipe] == shape;

   if (simd_split && same_shape && single_wait && min_delta == 1) {
      min_delta += ((1 << simd_split) - 1) * jay_macro_length(I);
      I->replicate_dep = true;
      I->decrement_dep = last_pipe != exec_pipe;
   }

   bool has_sbid = jay_inst_has_sbid(I);
   I->dep = (gen_swsb) {
      .sbid = has_sbid ? jay_inst_sbid(I) : 0,
      .mode = has_sbid ? GEN_SBID_SET : GEN_SBID_NULL,
      .regdist = wait_pipes ? min_delta : 0,
      .pipe = single_wait && (!has_sbid ||
                              last_pipe == GEN_PIPE_FLOAT ||
                              last_pipe == GEN_PIPE_INT) ?
                 last_pipe :
              wait_pipes ? GEN_PIPE_ALL :
                           GEN_PIPE_NONE,
   };

   /* DPAS can only represent in-order dependency for its inferred pipe,
    * so if it depends on something else, add an extra SYNC.nop for that.
    */
   if (I->op == JAY_OPCODE_DPAS &&
       wait_pipes &&
       (!single_wait ||
        last_pipe != inferred_sync_pipe(func->shader->devinfo, I))) {
      assert(I->dep.regdist > 0);
      jay_builder b = jay_init_builder(func, jay_before_inst(I));

      jay_inst *sync = jay_SYNC(&b, jay_null(), TGL_SYNC_NOP);
      sync->dep.regdist = I->dep.regdist;
      sync->dep.pipe = I->dep.pipe;

      I->dep.regdist = 0;
      I->dep.pipe = GEN_PIPE_NONE;
   }

   /* Fold the immediate preceding SYNC.nop into this instruction, allowing
    * us to wait on both ALU and a SBID in the same annotation. We cannot do
    * this safely in the presence of predication or SIMD splitting that could
    * cause any part of the instruction to get shot down, skipping the sync
    * for future instructions (at least not without more tricky logic).
    */
   if (ctx->last_sync &&
       jay_sync_op(ctx->last_sync) == TGL_SYNC_NOP &&
       I->dep.mode == GEN_SBID_NULL &&
       !I->predication &&
       !jay_simd_split(func->shader, I) &&
       (I->dep.regdist == 0 ||
        inferred_sync_pipe(func->shader->devinfo, I) == I->dep.pipe)) {

      assert(ctx->last_sync->dep.regdist == 0);
      assert(ctx->last_sync->dep.pipe == GEN_PIPE_NONE);

      I->dep.mode = ctx->last_sync->dep.mode;
      I->dep.sbid = ctx->last_sync->dep.sbid;

      jay_remove_instruction(ctx->last_sync);
   }

   if (exec_pipe != GEN_PIPE_NONE) {
      /* Advance the IP by the number of physical instructions emitted */
      ctx->ip[exec_pipe] +=
         jay_macro_length(I) << jay_simd_split(func->shader, I);

      uint32_t now = make_writer(exec_pipe, ctx->ip[exec_pipe]);

      for (unsigned i = 0; i < ARRAY_SIZE(dsts); ++i) {
         struct key r = def_to_key(func, I, dsts[i]);

         for (unsigned i = 0; i < r.width; ++i) {
            ctx->access[r.base + i][0] = now;
         }
      }

      jay_foreach_src(I, s) {
         struct key r = def_to_key(func, I, I->src[s]);
         for (unsigned i = 0; i < r.width; ++i) {
            ctx->access[r.base + i][exec_pipe] = ctx->ip[exec_pipe];
         }
      }

      ctx->last_shape[exec_pipe] = shape;
   }

   ctx->last_sync = NULL;
}

/*
 * Trivial scoreboard lowering pass for debugging use. Stalls after every
 * instruction and assigns SBID zero to all messages.
 */
void
jay_lower_scoreboard_trivial(jay_shader *shader)
{
   jay_foreach_inst_in_shader_safe(shader, func, I) {
      if (jay_inst_has_sbid(I)) {
         if (I->op == JAY_OPCODE_DPAS) {
            /* DPAS can't have an A@1, so insert an extra SYNC.nop. */
            jay_builder before = jay_init_builder(func, jay_before_inst(I));
            jay_SYNC(&before, jay_null(), TGL_SYNC_NOP)->dep =
               gen_swsb_regdist(1);
            I->dep = gen_swsb_sbid(GEN_SBID_SET, 0);
         } else {
            I->dep = gen_swsb_dst_dep(gen_swsb_sbid(GEN_SBID_SET, 0), 1);
         }

         jay_builder b = jay_init_builder(func, jay_after_inst(I));
         sync_sbids(&b, BITFIELD_BIT(0), GEN_SBID_DST);
      } else if (I->op == JAY_OPCODE_SCHEDULE_BARRIER) {
         jay_remove_instruction(I);
      } else {
         I->dep = gen_swsb_regdist(1);
      }
   }
}

void
jay_lower_scoreboard(jay_shader *shader)
{
   unsigned accums = 4;
   uint32_t nr_keys = shader->num_regs[GPR] + shader->num_regs[UGPR] + accums;
   assert(nr_keys <= MAX_KEYS && "SENDs use uninitialized stack allocation");
   u32_per_pipe *access = malloc(sizeof(*access) * nr_keys);

   jay_foreach_function(shader, f) {
      memset(access, 0, sizeof(*access) * nr_keys);
      struct swsb_state state = { .nr_keys = nr_keys, .access = access };

      jay_foreach_block(f, block) {
         lower_sbid_local(f, block);
      }

      /* RegDist scoreboarding is global but requires no dataflow analysis,
       * because taking a branch stalls all ALU pipelines. Therefore, it
       * suffices to propagate scoreboard state along fallthrough edges. We
       * implement that backwards: state is preserved (correctness), except we
       * clear access[] when entering blocks that are unreachable by falling
       * through from the previous source-order block and hence must be branch
       * targets coming in with a clear scoreboard. next[] tracks the
       * fallthrough block for the logical & physical CFGs respectively.
       */
      jay_block *next[UGPR + 1] = { NULL };

      jay_foreach_block(f, block) {
         /* Clear access[] for GPRs according to the logical CFG and for UGPRs
          * according to the physical CFG. This is a bit pedantic but it ensures
          * we keep the dependencies for UGPRs across halves of if-else.
          */
         for (unsigned f = GPR; f <= UGPR; f++) {
            if (!list_is_empty(&block->instructions) && next[f] != block) {
               memset(access + (f ? shader->num_regs[GPR] : 0), 0,
                      sizeof(access[0]) * shader->num_regs[f]);
            }

            next[f] = jay_successors(block, f)[0];
         }

         jay_foreach_inst_in_block_safe(block, I) {
            lower_regdist(f, I, &state);
         }
      }
   }

   free(access);
}
