/*
 * Copyright 2017-2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/macros.h"
#include "gen_enums.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/* General instruction latencies from IGC */
#define XE_LATENCY_FPU_ACC    6  /* SIMD8 latency if dst is acc. */
#define XE_LATENCY_FPU        10 /* SIMD8 latency for general FPU ops. */
#define XE_LATENCY_MATH       17 /* Math latency. */
#define XE_LATENCY_BRANCH     23 /* Latency for SIMD16 branch. */
#define XE_LATENCY_BARRIER    30 /* Latency for barrier. */
#define XE_LATENCY_DELTA      1  /* Extra cycles for wider SIMD sizes */
#define XE_LATENCY_DELTA_MATH 4
#define XE_LATENCY_ARF        16 /* Latency for ARF dependencies */
#define XE_LATENCY_DPAS 21 /* Latency for DPAS 8x1 */

/* Latency for SIMD16 SLM messages. If accessing the same location it takes 28
 * cycles. For the sequential access pattern it takes 26 cycles.
 */
#define XE_LATENCY_SLM16 28
#define XE_LATENCY_SLM32 45

#define XE_LATENCY_SEND_OTHERS       50  /* Latency for other messages. */
#define XE_LATENCY_DP_L3             146 /* Dataport L3 hit */
#define XE_LATENCY_SAMPLER_L3        214 /* Sampler L3 hit */
#define XE_LATENCY_SLM_FENCE         23  /* Fence SLM */
#define XE_LATENCY_LSC_UNTYPED_L1    45  /* LSC untyped L1 cache hit */
#define XE_LATENCY_LSC_UNTYPED_L3    200 /* LSC untyped L3 cache hit */
#define XE_LATENCY_LSC_UNTYPED_FENCE 35  /* LSC untyped fence (best case) */
#define XE_LATENCY_LSC_TYPED_L1      75  /* LSC typed L1 cache hit */
#define XE_LATENCY_LSC_TYPED_L3      200 /* LSC typed L3 cache hit */
#define XE_LATENCY_LSC_TYPED_FENCE   60  /* LSC typed fence */
#define XE_LATENCY_ADDR_MOV          2
#define XE_LATENCY_SEND_ARB          8 /* Arbitration acquire/release */

#define XE_OC_MATH   4
#define XE_OC_OTHERS 1

/*
 * Estimate latency in cycles for an instruction. This is roughly the IGC's Xe2+
 * cycle model. It could be tweaked.
 *
 * TODO: Need to handle Xe3P details.
 */
unsigned
jay_latency(jay_shader *s, jay_inst *I)
{
   if (I->op == JAY_OPCODE_SEND) {
      switch (jay_send_sfid(I)) {
      case GEN_SFID_TGM:
         /* TODO: Check cache settings */
         return XE_LATENCY_LSC_TYPED_L3;

      case GEN_SFID_UGM:
      case GEN_SFID_URB:
         /* TODO: Fences, check cache settings */
         return XE_LATENCY_LSC_UNTYPED_L3;

      case GEN_SFID_SLM:
         /* TODO: Fence */
         return jay_simd_width_logical(s, I) == 32 ? XE_LATENCY_SLM32 :
                                                     XE_LATENCY_SLM16;

      case GEN_SFID_SAMPLER:
         return XE_LATENCY_SAMPLER_L3;

      case GEN_SFID_HDC0:
      case GEN_SFID_HDC1:
      case GEN_SFID_HDC2:
      case GEN_SFID_HDC_READ_ONLY:
         return XE_LATENCY_DP_L3;

      case GEN_SFID_MESSAGE_GATEWAY:
         return XE_LATENCY_BARRIER;

      default:
         return XE_LATENCY_SEND_OTHERS;
      }
   } else if (I->op == JAY_OPCODE_DEMOTE) {
      /* Try to hoist demotes without waiting prematurely on sends. */
      return 100;
   } else if (I->op == JAY_OPCODE_MATH) {
      unsigned sz = jay_simd_width_logical(s, I);
      unsigned scale = (sz <= 8) ? 0 : (sz == 16) ? 1 : 3;
      return XE_LATENCY_MATH + XE_LATENCY_DELTA_MATH * scale;
   } else if (I->op == JAY_OPCODE_DPAS) {
      /* This is correct for Xe2 and PTL but not for older/newer platforms */
      switch (jay_dpas_rcount(I)) {
      case 1:
         return 22;
      case 2:
         return 23;
      case 8:
         return 33;
      default:
         return 33;
      }
   } else if (jay_is_flag(I->dst) ||
              !jay_is_null(I->cond_flag) ||
              I->dst.file == J_ADDRESS) {
      return XE_LATENCY_ARF;
   } else {
      /* Pre-RA, we conservatively assume we can't use accumulators. This could
       * be tuned to bias the scheduler.
       */
      bool acc = I->dst.file == ACCUM;
      unsigned sz = jay_simd_width_logical(s, I);
      unsigned scale = (sz <= 8) ? 0 : (sz == 16) ? 1 : 3;

      return (acc ? XE_LATENCY_FPU_ACC : XE_LATENCY_FPU) +
             XE_LATENCY_DELTA * scale;
   }
}

static unsigned
jay_send_src_latency(jay_shader *s, jay_inst *I)
{
   /* ARB cycle + GRF read lengths */
   return XE_LATENCY_SEND_ARB + jay_send_mlen(I) + jay_send_ex_mlen(I);
}

static unsigned
jay_occupancy(jay_shader *s, jay_inst *I)
{
   unsigned exec_size = jay_simd_width_logical(s, I);
   unsigned native = jay_ugpr_per_grf(s);
   uint16_t scale = (exec_size <= native)     ? 1 :
                    (exec_size == native * 2) ? 2 :
                                                4;

   if (I->op == JAY_OPCODE_MATH)
      return XE_OC_MATH * scale;
   if (false /* TODO: isFastHFInstruction */) {
      scale = (exec_size <= native * 2) ? 1 : 2;
   } else if (I->op == JAY_OPCODE_SHUFFLE) {
      /* TODO: This isn't quite right because macro stuff */
      scale = exec_size;
   } else if (jay_type_size_bits(I->type) == 64) {
      scale = (exec_size <= native / 2) ? 1 : 2;
   }

   return XE_OC_OTHERS * scale;
}

gen_pipe
jay_inst_exec_pipe(const struct intel_device_info *devinfo, jay_inst *I)
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
gen_pipe
jay_inferred_sync_pipe(const struct intel_device_info *devinfo,
                       const jay_inst *I)
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

struct alu_fifo {
   unsigned cycle[16];
   unsigned first;
};

static void
fifo_add(struct alu_fifo *fifo, unsigned cycle)
{
   fifo->cycle[fifo->first] = cycle;
   fifo->first++;

   /* Wraparound */
   if (fifo->first == ARRAY_SIZE(fifo->cycle)) {
      fifo->first = 0;
   }
}

static unsigned
fifo_get(struct alu_fifo *fifo, unsigned index)
{
   return fifo->cycle[(fifo->first - index) % ARRAY_SIZE(fifo->cycle)];
}

/*
 * Estimate cycle count of a post-RA, post-SWSB block by using the SWSB
 * annotations, plus tracking for the hardware scoreboards. This is based on
 * IGC's StaticCycleProfiling pass. It could be improved, but it's a start.
 */
static unsigned
estimate_block_cycles(jay_function *f, jay_block *block)
{
   unsigned cycles = 0;
   jay_inst *prev = NULL;
   jay_shader *shader = f->shader;

   struct alu_fifo alu[GEN_PIPE_ALL] = { 0 };
   unsigned acc[8] = { 0 }, flag[8] = { 0 };
   jay_inst *sbid[32] = { NULL };
   unsigned sbid_time[32] = { 0 };

   jay_foreach_inst_in_block(block, I) {
      if (prev) {
         cycles += jay_occupancy(shader, prev);
      }

      unsigned dep_cycles = 0;
      if (I->dep.mode && sbid[I->dep.pipe]) {
         unsigned latency = 0;
         if (I->dep.mode == GEN_SBID_SRC) {
            if (I->op == JAY_OPCODE_SEND) {
               latency = jay_send_src_latency(shader, sbid[I->dep.pipe]);
            } else {
               /* IGC uses occupancy for this. Hit only by DPAS */
               latency = jay_occupancy(shader, sbid[I->dep.pipe]);
            }
         } else {
            latency = jay_latency(shader, sbid[I->dep.pipe]);
            sbid[I->dep.pipe] = NULL;
         }

         dep_cycles = sbid_time[I->dep.sbid] + latency;
      }

      if (I->dep.regdist) {
         unsigned pipes = (I->dep.pipe == GEN_PIPE_ALL) ?
                             BITFIELD_MASK(GEN_PIPE_ALL) :
                             BITFIELD_BIT(I->dep.pipe);

         u_foreach_bit(pipe, pipes) {
            dep_cycles = MAX2(dep_cycles, fifo_get(&alu[pipe], I->dep.regdist));
         }
      }

      jay_foreach_src(I, s) {
         if (I->src[s].file == ACCUM || I->src[s].file == UACCUM) {
            dep_cycles = MAX2(dep_cycles, acc[I->src[s].reg]);
         } else if (jay_is_flag(I->src[s])) {
            dep_cycles = MAX2(dep_cycles, flag[I->src[s].reg]);
         }
      }

      cycles = MAX2(cycles, dep_cycles);
      unsigned ready_cycle = cycles + jay_latency(shader, I);
      fifo_add(&alu[jay_inst_exec_pipe(shader->devinfo, I)], ready_cycle);

      if (I->dst.file == ACCUM || I->dst.file == UACCUM) {
         acc[I->dst.reg] = ready_cycle;
      } else if (jay_is_flag(I->dst)) {
         flag[I->dst.reg] = ready_cycle;
      }

      if (jay_is_flag(I->cond_flag)) {
         flag[I->cond_flag.reg] = ready_cycle;
      }

      if (I->dep.mode == GEN_SBID_SET) {
         sbid[I->dep.sbid] = I;
         sbid_time[I->dep.sbid] = ready_cycle;
      }

      prev = I;
   }

   return cycles;
}

unsigned
jay_estimate_cycles(jay_function *f)
{
   unsigned cycles = 0;

   jay_foreach_block(f, block) {
      /* TODO: Scale by block repetition */
      cycles += estimate_block_cycles(f, block);
   }

   return cycles;
}
