/*
 * Copyright © 2020 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "brw_eu.h"
#include "brw_shader.h"
#include "brw_cfg.h"
#include <algorithm>

namespace {
   /**
    * Enumeration representing the various asynchronous units that can run
    * computations in parallel on behalf of a shader thread.
    */
   enum intel_eu_unit {
      /** EU front-end. */
      EU_UNIT_FE,
      /** EU FPU0 (Note that co-issue to FPU1 is currently not modeled here). */
      EU_UNIT_FPU,
      /** Extended Math unit (AKA FPU1 on Gfx8-11, part of the EU on Gfx6+). */
      EU_UNIT_EM,
      /** Sampler shared function. */
      EU_UNIT_SAMPLER,
      /** Pixel Interpolator shared function. */
      EU_UNIT_PI,
      /** Unified Return Buffer shared function. */
      EU_UNIT_URB,
      /** Data Port Data Cache shared function. */
      EU_UNIT_DP_DC,
      /** Data Port Render Cache shared function. */
      EU_UNIT_DP_RC,
      /** Data Port Constant Cache shared function. */
      EU_UNIT_DP_CC,
      /** Message Gateway shared function. */
      EU_UNIT_GATEWAY,
      /** Thread Spawner shared function. */
      EU_UNIT_SPAWNER,
      /** Number of asynchronous units currently tracked. */
      EU_NUM_UNITS,
      /** Dummy unit for instructions that don't consume runtime from the above. */
      EU_UNIT_NULL = EU_NUM_UNITS
   };

   /**
    * Enumeration representing a computation result another computation can
    * potentially depend on.
    */
   enum intel_eu_dependency_id {
      /* Address register part of the ARF. */
      EU_DEPENDENCY_ID_ADDR0 = 0,
      /* Accumulator register part of the ARF. */
      EU_DEPENDENCY_ID_ACCUM0 = EU_DEPENDENCY_ID_ADDR0 + 1,
      /* Flag register part of the ARF. */
      EU_DEPENDENCY_ID_FLAG0 = EU_DEPENDENCY_ID_ACCUM0 + 12,
      /* SBID token write completion.  Only used on Gfx12+. */
      EU_DEPENDENCY_ID_SBID_WR0 = EU_DEPENDENCY_ID_FLAG0 + 16,
      /* SBID token read completion.  Only used on Gfx12+. */
      EU_DEPENDENCY_ID_SBID_RD0 = EU_DEPENDENCY_ID_SBID_WR0 + 32,
      /* Register part of the GRF. */
      EU_DEPENDENCY_ID_GRF0 = EU_DEPENDENCY_ID_SBID_RD0 + 32,
      EU_DEPENDENCY_ID_INVALID = ~0u
   };

   unsigned
   num_grf_dependency_ids(const brw_shader *s) {
      return (!s->grf_used ? s->alloc.count :
              s->devinfo->ver >= 30 ? XE3_MAX_GRF :
              s->devinfo->ver >= 20 ? XE2_MAX_GRF :
              BRW_MAX_GRF);
   }

   /**
    * State of our modeling of the program execution.
    */
   struct state {
      state(const brw_shader *s) :
	 unit_ready(), dep_ready(), num_dependency_ids(EU_DEPENDENCY_ID_GRF0 + num_grf_dependency_ids(s)), unit_busy(), weight(1.0)
      {
	 dep_ready = new unsigned[num_dependency_ids]();
      }

      ~state() {
	 delete[] dep_ready;
      }

      /**
       * Time at which a given unit will be ready to execute the next
       * computation, in clock units.
       */
      unsigned unit_ready[EU_NUM_UNITS];
      /**
       * Time at which an instruction dependent on a given dependency ID will
       * be ready to execute, in clock units.
       */
      unsigned *dep_ready;
      unsigned num_dependency_ids;

      /**
       * Aggregated utilization of a given unit excluding idle cycles,
       * in clock units.
       */
      float unit_busy[EU_NUM_UNITS];
      /**
       * Factor of the overhead of a computation accounted for in the
       * aggregated utilization calculation.
       */
      float weight;
   };

   /**
    * Information derived from an IR instruction used to compute performance
    * estimates.  Allows the timing calculation to work on both FS and VEC4
    * instructions.
    */
   struct instruction_info {
      instruction_info(const struct brw_isa_info *isa, const brw_inst *inst) :
         isa(isa), devinfo(isa->devinfo), op(inst->opcode),
         td(inst->dst.type), sd(DIV_ROUND_UP(inst->size_written, REG_SIZE)),
         tx(get_exec_type(inst)), sx(0), ss(0),
         sc(has_bank_conflict(isa, inst) ? sd : 0),
         desc(0), sfid(0), fused_send_disable(false)
      {
         const brw_send_inst *send = inst->as_send();
         if (send) {
            desc = send->desc;
            sfid = send->sfid;
            /* We typically want the maximum source size, except for split send
             * messages which require the total size.
             */
            if (inst->opcode == SHADER_OPCODE_SEND) {
               ss = DIV_ROUND_UP(inst->size_read(devinfo, 2), REG_SIZE) +
                    DIV_ROUND_UP(inst->size_read(devinfo, 3), REG_SIZE);
            } else if (inst->opcode == SHADER_OPCODE_SEND_GATHER) {
               ss = send->mlen;
               /* If haven't lowered yet, count the sources. */
               if (!ss) {
                  for (int i = 3; i < inst->sources; i++)
                     ss += DIV_ROUND_UP(inst->size_read(devinfo, i), REG_SIZE);
               }
            }
            fused_send_disable = send->fused_eu_disable;
         } else {
            for (unsigned i = 0; i < inst->sources; i++)
               ss = MAX2(ss, DIV_ROUND_UP(inst->size_read(devinfo, i), REG_SIZE));
         }

         /* Convert the execution size to GRF units. */
         sx = DIV_ROUND_UP(inst->exec_size * brw_type_size_bytes(tx), REG_SIZE);

         /* 32x32 integer multiplication has half the usual ALU throughput.
          * Treat it as double-precision.
          */
         if ((inst->opcode == BRW_OPCODE_MUL || inst->opcode == BRW_OPCODE_MAD) &&
             !brw_type_is_float(tx) && brw_type_size_bytes(tx) == 4 &&
             brw_type_size_bytes(inst->src[0].type) == brw_type_size_bytes(inst->src[1].type))
            tx = brw_int_type(8, tx == BRW_TYPE_D);

         const brw_dpas_inst *dpas = inst->as_dpas();
         rcount = dpas ? dpas->rcount : 0;
      }

      /** ISA encoding information */
      const struct brw_isa_info *isa;
      /** Device information. */
      const struct intel_device_info *devinfo;
      /** Instruction opcode. */
      opcode op;
      /** Destination type. */
      brw_reg_type td;
      /** Destination size in GRF units. */
      unsigned sd;
      /** Execution type. */
      brw_reg_type tx;
      /** Execution size in GRF units. */
      unsigned sx;
      /** Source size. */
      unsigned ss;
      /** Bank conflict penalty size in GRF units (equal to sd if non-zero). */
      unsigned sc;
      /** Send message descriptor. */
      uint32_t desc;
      /** Send message shared function ID. */
      uint8_t sfid;
      /** Repeat count for DPAS instructions. */
      uint8_t rcount;
      /** Whether SEND message fusion is disabled (Gfx12.x only) */
      bool fused_send_disable;
   };

   /**
    * Timing information of an instruction used to estimate the performance of
    * the program.
    */
   struct perf_desc {
      perf_desc(enum intel_eu_unit u, int df, int db,
                int ls, int ld, int la, int lf) :
         u(u), df(df), db(db), ls(ls), ld(ld), la(la), lf(lf) {}

      /**
       * Back-end unit its runtime shall be accounted to, in addition to the
       * EU front-end which is always assumed to be involved.
       */
      enum intel_eu_unit u;
      /**
       * Overhead cycles from the time that the EU front-end starts executing
       * the instruction until it's ready to execute the next instruction.
       */
      int df;
      /**
       * Overhead cycles from the time that the back-end starts executing the
       * instruction until it's ready to execute the next instruction.
       */
      int db;
      /**
       * Latency cycles from the time that the back-end starts executing the
       * instruction until its sources have been read from the register file.
       */
      int ls;
      /**
       * Latency cycles from the time that the back-end starts executing the
       * instruction until its regular destination has been written to the
       * register file.
       */
      int ld;
      /**
       * Latency cycles from the time that the back-end starts executing the
       * instruction until its accumulator destination has been written to the
       * ARF file.
       *
       * Note that this is an approximation of the real behavior of
       * accumulating instructions in the hardware: Instead of modeling a pair
       * of back-to-back accumulating instructions as a first computation with
       * latency equal to ld followed by another computation with a
       * mid-pipeline stall (e.g. after the "M" part of a MAC instruction), we
       * model the stall as if it occurred at the top of the pipeline, with
       * the latency of the accumulator computation offset accordingly.
       */
      int la;
      /**
       * Latency cycles from the time that the back-end starts executing the
       * instruction until its flag destination has been written to the ARF
       * file.
       */
      int lf;
   };

   /**
    * Compute the timing information of an instruction based on any relevant
    * information from the IR and a number of parameters specifying a linear
    * approximation: Parameter X_Y specifies the derivative of timing X
    * relative to info field Y, while X_1 specifies the independent term of
    * the approximation of timing X.
    */
   perf_desc
   calculate_desc(const instruction_info &info, enum intel_eu_unit u,
                  int df_1, int df_sd, int df_sc,
                  int db_1, int db_sx,
                  int ls_1, int ld_1, int la_1, int lf_1,
                  int l_ss, int l_sd)
   {
      /* We fused SEND are disabled double those parameters */
      ls_1 *= info.fused_send_disable ? 2 : 1;
      l_ss *= info.fused_send_disable ? 2 : 1;
      return perf_desc(u, df_1 + df_sd * int(info.sd) + df_sc * int(info.sc),
                          db_1 + db_sx * int(info.sx),
                          ls_1 + l_ss * int(info.ss),
                          ld_1 + l_ss * int(info.ss) + l_sd * int(info.sd),
                          la_1, lf_1);
   }

   /**
    * Compute the timing information of an instruction based on any relevant
    * information from the IR and a number of linear approximation parameters
    * hard-coded for each IR instruction.
    *
    * Most timing parameters are obtained from the multivariate linear
    * regression of a sample of empirical timings measured using the tm0
    * register (as can be done today by using the shader_time debugging
    * option).  The Gfx4-5 math timings are obtained from BSpec Volume 5c.3
    * "Shared Functions - Extended Math", Section 3.2 "Performance".
    * Parameters marked XXX shall be considered low-quality, they're possibly
    * high variance or completely guessed in cases where experimental data was
    * unavailable.
    */
   const perf_desc
   instruction_desc(const instruction_info &info)
   {
      const struct intel_device_info *devinfo = info.devinfo;

      switch (info.op) {
      case BRW_OPCODE_SYNC:
      case BRW_OPCODE_SEL:
      case BRW_OPCODE_NOT:
      case BRW_OPCODE_AND:
      case BRW_OPCODE_OR:
      case BRW_OPCODE_XOR:
      case BRW_OPCODE_SHR:
      case BRW_OPCODE_SHL:
      case BRW_OPCODE_ASR:
      case BRW_OPCODE_CMPN:
      case BRW_OPCODE_BFREV:
      case BRW_OPCODE_BFI1:
      case BRW_OPCODE_AVG:
      case BRW_OPCODE_FRC:
      case BRW_OPCODE_RNDU:
      case BRW_OPCODE_RNDD:
      case BRW_OPCODE_RNDE:
      case BRW_OPCODE_RNDZ:
      case BRW_OPCODE_MAC:
      case BRW_OPCODE_MACH:
      case BRW_OPCODE_LZD:
      case BRW_OPCODE_FBH:
      case BRW_OPCODE_FBL:
      case BRW_OPCODE_CBIT:
      case BRW_OPCODE_ADDC:
      case BRW_OPCODE_ROR:
      case BRW_OPCODE_ROL:
      case BRW_OPCODE_SUBB:
      case BRW_OPCODE_LINE:
      case BRW_OPCODE_NOP:
      case SHADER_OPCODE_CLUSTER_BROADCAST:
      case SHADER_OPCODE_SCRATCH_HEADER:
      case FS_OPCODE_DDX_COARSE:
      case FS_OPCODE_DDX_FINE:
      case FS_OPCODE_DDY_COARSE:
      case FS_OPCODE_PIXEL_X:
      case FS_OPCODE_PIXEL_Y:
         if (devinfo->ver >= 11) {
            return calculate_desc(info, EU_UNIT_FPU, 0, 2, 0, 0, 2,
                                  0, 10, 6 /* XXX */, 14, 0, 0);
         } else {
            if (brw_type_size_bytes(info.tx) > 4)
               return calculate_desc(info, EU_UNIT_FPU, 0, 4, 0, 0, 4,
                                     0, 12, 8 /* XXX */, 16 /* XXX */, 0, 0);
            else
               return calculate_desc(info, EU_UNIT_FPU, 0, 2, 0, 0, 2,
                                     0, 8, 4, 12, 0, 0);
         }

      case BRW_OPCODE_MOV:
      case BRW_OPCODE_CMP:
      case BRW_OPCODE_ADD:
      case BRW_OPCODE_ADD3:
      case BRW_OPCODE_MUL:
      case SHADER_OPCODE_MOV_RELOC_IMM:
         if (devinfo->ver >= 11) {
            return calculate_desc(info, EU_UNIT_FPU, 0, 2, 0, 0, 2,
                                  0, 10, 6, 14, 0, 0);
         } else {
            if (brw_type_size_bytes(info.tx) > 4)
               return calculate_desc(info, EU_UNIT_FPU, 0, 4, 0, 0, 4,
                                     0, 12, 8 /* XXX */, 16 /* XXX */, 0, 0);
            else
               return calculate_desc(info, EU_UNIT_FPU, 0, 2, 0, 0, 2,
                                     0, 8, 4, 12, 0, 0);
         }

      case BRW_OPCODE_BFE:
      case BRW_OPCODE_BFI2:
      case BRW_OPCODE_CSEL:
      case BRW_OPCODE_BFN:
         if (devinfo->ver >= 11)
            return calculate_desc(info, EU_UNIT_FPU, 0, 2, 1, 0, 2,
                                  0, 10, 6 /* XXX */, 14 /* XXX */, 0, 0);
         else
            return calculate_desc(info, EU_UNIT_FPU, 0, 2, 1, 0, 2,
                                  0, 8, 4 /* XXX */, 12 /* XXX */, 0, 0);

      case BRW_OPCODE_MAD:
         if (devinfo->ver >= 11) {
            return calculate_desc(info, EU_UNIT_FPU, 0, 2, 1, 0, 2,
                                  0, 10, 6 /* XXX */, 14 /* XXX */, 0, 0);
         } else {
            if (brw_type_size_bytes(info.tx) > 4)
               return calculate_desc(info, EU_UNIT_FPU, 0, 4, 1, 0, 4,
                                     0, 12, 8 /* XXX */, 16 /* XXX */, 0, 0);
            else
               return calculate_desc(info, EU_UNIT_FPU, 0, 2, 1, 0, 2,
                                     0, 8, 4 /* XXX */, 12 /* XXX */, 0, 0);
         }

      case BRW_OPCODE_DP4:
      case BRW_OPCODE_DPH:
      case BRW_OPCODE_DP3:
      case BRW_OPCODE_DP2:
         return calculate_desc(info, EU_UNIT_FPU, 0, 2, 0, 0, 2,
                               0, 12, 8 /* XXX */, 16 /* XXX */, 0, 0);

      case BRW_OPCODE_DP4A:
         if (devinfo->ver >= 12)
            return calculate_desc(info, EU_UNIT_FPU, 0, 2, 1, 0, 2,
                                  0, 10, 6 /* XXX */, 14 /* XXX */, 0, 0);
         else
            abort();

      case BRW_OPCODE_DPAS: {
         unsigned ld;

         switch (info.rcount) {
         case 1:
            ld = 21;
            break;
         case 2:
            ld = 22;
            break;
         case 8:
         default:
            ld = 32;
            break;
         }

         /* DPAS cannot write the accumulator or the flags, so pass UINT_MAX
          * for la and lf.
          */
         if (devinfo->verx10 >= 125)
            return calculate_desc(info, EU_UNIT_FPU, 0, 2, 1, 0, 2,
                                  0, ld, UINT_MAX, UINT_MAX, 0, 0);
         else
            abort();
      }

      case SHADER_OPCODE_RCP:
      case SHADER_OPCODE_RSQ:
      case SHADER_OPCODE_SQRT:
      case SHADER_OPCODE_EXP2:
      case SHADER_OPCODE_LOG2:
      case SHADER_OPCODE_SIN:
      case SHADER_OPCODE_COS:
         return calculate_desc(info, EU_UNIT_EM, -2, 4, 0, 0, 4,
                               0, 16, 0, 0, 0, 0);

      case SHADER_OPCODE_POW:
         return calculate_desc(info, EU_UNIT_EM, -2, 4, 0, 0, 8,
                               0, 24, 0, 0, 0, 0);

      case SHADER_OPCODE_INT_QUOTIENT:
      case SHADER_OPCODE_INT_REMAINDER:
         return calculate_desc(info, EU_UNIT_EM, 2, 0, 0, 26, 0,
                               0, 28 /* XXX */, 0, 0, 0, 0);

      case BRW_OPCODE_DO:
         return calculate_desc(info, EU_UNIT_NULL, 0, 0, 0, 0, 0,
                               0, 0, 0, 0, 0, 0);

      case BRW_OPCODE_IF:
      case BRW_OPCODE_ELSE:
      case BRW_OPCODE_ENDIF:
      case BRW_OPCODE_WHILE:
      case BRW_OPCODE_BREAK:
      case BRW_OPCODE_CONTINUE:
      case BRW_OPCODE_HALT:
         return calculate_desc(info, EU_UNIT_NULL, 8, 0, 0, 0, 0,
                               0, 0, 0, 0, 0, 0);

      case BRW_OPCODE_PLN:
         return calculate_desc(info, EU_UNIT_FPU, 0, 4, 0, 0, 4,
                               0, 12, 8 /* XXX */, 16 /* XXX */, 0, 0);

      case BRW_OPCODE_LRP:
         return calculate_desc(info, EU_UNIT_FPU, 0, 4, 1, 0, 4,
                               0, 12, 8 /* XXX */, 16 /* XXX */, 0, 0);

      case FS_OPCODE_PACK_HALF_2x16_SPLIT:
         if (devinfo->ver >= 11)
            return calculate_desc(info, EU_UNIT_FPU, 20, 6, 0, 0, 6,
                                  0, 10 /* XXX */, 6 /* XXX */,
                                  14 /* XXX */, 0, 0);
         else
            return calculate_desc(info, EU_UNIT_FPU, 16, 6, 0, 0, 6,
                                  0, 8 /* XXX */, 4 /* XXX */,
                                  12 /* XXX */, 0, 0);

      case SHADER_OPCODE_READ_ARCH_REG:
         if (devinfo->ver >= 12) {
            return calculate_desc(info, EU_UNIT_FPU, 20, 6, 0, 0, 6,
                                  0, 10, 6 /* XXX */, 14, 0, 0);
         } else {
            return calculate_desc(info, EU_UNIT_FPU, 0, 2, 0, 0, 2,
                                  0, 8, 4, 12, 0, 0);
         }

      case SHADER_OPCODE_MOV_INDIRECT:
         if (devinfo->ver >= 11)
            return calculate_desc(info, EU_UNIT_FPU, 34, 0, 0, 34, 0,
                                  0, 10 /* XXX */, 6 /* XXX */,
                                  14 /* XXX */, 0, 0);
         else
            return calculate_desc(info, EU_UNIT_FPU, 34, 0, 0, 34, 0,
                                  0, 8 /* XXX */, 4 /* XXX */,
                                  12 /* XXX */, 0, 0);

      case SHADER_OPCODE_BROADCAST:
         if (devinfo->ver >= 11)
            return calculate_desc(info, EU_UNIT_FPU, 20 /* XXX */, 0, 0, 4, 0,
                                  0, 10, 6 /* XXX */, 14 /* XXX */, 0, 0);
         else
            return calculate_desc(info, EU_UNIT_FPU, 18, 0, 0, 4, 0,
                                  0, 8, 4 /* XXX */, 12 /* XXX */, 0, 0);

      case SHADER_OPCODE_FIND_LIVE_CHANNEL:
      case SHADER_OPCODE_FIND_LAST_LIVE_CHANNEL:
      case SHADER_OPCODE_LOAD_LIVE_CHANNELS:
         if (devinfo->ver >= 11)
            return calculate_desc(info, EU_UNIT_FPU, 2, 0, 0, 2, 0,
                                  0, 10, 6 /* XXX */, 14 /* XXX */, 0, 0);
         else
            return calculate_desc(info, EU_UNIT_FPU, 2, 0, 0, 2, 0,
                                  0, 8, 4 /* XXX */, 12 /* XXX */, 0, 0);

      case SHADER_OPCODE_RND_MODE:
      case SHADER_OPCODE_FLOAT_CONTROL_MODE:
         if (devinfo->ver >= 11)
            return calculate_desc(info, EU_UNIT_FPU, 24 /* XXX */, 0, 0,
                                  4 /* XXX */, 0,
                                  0, 0, 0, 0, 0, 0);
         else
            return calculate_desc(info, EU_UNIT_FPU, 20 /* XXX */, 0, 0,
                                  4 /* XXX */, 0,
                                  0, 0, 0, 0, 0, 0);

      case SHADER_OPCODE_SHUFFLE:
         if (devinfo->ver >= 11)
            return calculate_desc(info, EU_UNIT_FPU, 44 /* XXX */, 0, 0,
                                  44 /* XXX */, 0,
                                  0, 10 /* XXX */, 6 /* XXX */,
                                  14 /* XXX */, 0, 0);
         else
            return calculate_desc(info, EU_UNIT_FPU, 42 /* XXX */, 0, 0,
                                  42 /* XXX */, 0,
                                  0, 8 /* XXX */, 4 /* XXX */,
                                  12 /* XXX */, 0, 0);

      case SHADER_OPCODE_SEL_EXEC:
         if (devinfo->ver >= 11)
            return calculate_desc(info, EU_UNIT_FPU, 10 /* XXX */, 4 /* XXX */, 0,
                                  0, 4 /* XXX */,
                                  0, 10 /* XXX */, 6 /* XXX */,
                                  14 /* XXX */, 0, 0);
         else
            return calculate_desc(info, EU_UNIT_FPU, 8 /* XXX */, 4 /* XXX */, 0,
                                  0, 4 /* XXX */,
                                  0, 8 /* XXX */, 4 /* XXX */,
                                  12 /* XXX */, 0, 0);

      case SHADER_OPCODE_QUAD_SWIZZLE:
         if (devinfo->ver >= 11)
            return calculate_desc(info, EU_UNIT_FPU, 0 /* XXX */, 8 /* XXX */, 0,
                                  0, 8 /* XXX */,
                                  0, 10 /* XXX */, 6 /* XXX */,
                                  14 /* XXX */, 0, 0);
         else
            return calculate_desc(info, EU_UNIT_FPU, 0 /* XXX */, 8 /* XXX */, 0,
                                  0, 8 /* XXX */,
                                  0, 8 /* XXX */, 4 /* XXX */,
                                  12 /* XXX */, 0, 0);

      case FS_OPCODE_DDY_FINE:
         if (devinfo->ver >= 11)
            return calculate_desc(info, EU_UNIT_FPU, 0, 14, 0, 0, 4,
                                  0, 10, 6 /* XXX */, 14 /* XXX */, 0, 0);
         else
            return calculate_desc(info, EU_UNIT_FPU, 0, 2, 0, 0, 2,
                                  0, 8, 4 /* XXX */, 12 /* XXX */, 0, 0);

      case FS_OPCODE_LOAD_LIVE_CHANNELS:
         if (devinfo->ver >= 11)
            return calculate_desc(info, EU_UNIT_FPU, 2 /* XXX */, 0, 0,
                                  2 /* XXX */, 0,
                                  0, 0, 0, 10 /* XXX */, 0, 0);
         else
            return calculate_desc(info, EU_UNIT_FPU, 0, 2 /* XXX */, 0,
                                  0, 2 /* XXX */,
                                  0, 0, 0, 8 /* XXX */, 0, 0);

      case FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD:
         return calculate_desc(info, EU_UNIT_DP_CC, 2, 0, 0, 0, 16 /* XXX */,
                               10 /* XXX */, 100 /* XXX */, 0, 0, 0, 0);

      case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
      case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
      case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
         return calculate_desc(info, EU_UNIT_PI, 2, 0, 0, 14 /* XXX */, 0,
                               0, 90 /* XXX */, 0, 0, 0, 0);

      case SHADER_OPCODE_BARRIER:
         return calculate_desc(info, EU_UNIT_GATEWAY, 90 /* XXX */, 0, 0,
                               0 /* XXX */, 0,
                               0, 0, 0, 0, 0, 0);

      case SHADER_OPCODE_SEND:
      case SHADER_OPCODE_SEND_GATHER:
         switch (info.sfid) {
         case BRW_SFID_HDC_READ_ONLY:
            /* See FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD */
            return calculate_desc(info, EU_UNIT_DP_CC, 2, 0, 0, 0, 16 /* XXX */,
                                  10 /* XXX */, 100 /* XXX */, 0, 0, 0, 0);
         case BRW_SFID_RENDER_CACHE:
            switch (brw_dp_desc_msg_type(devinfo, info.desc)) {
            case GFX7_DATAPORT_RC_TYPED_ATOMIC_OP:
               return calculate_desc(info, EU_UNIT_DP_RC, 2, 0, 0,
                                     30 /* XXX */, 450 /* XXX */,
                                     10 /* XXX */, 100 /* XXX */,
                                     0, 0, 0, 400 /* XXX */);
            case GFX7_DATAPORT_RC_MEMORY_FENCE:
               return calculate_desc(info, EU_UNIT_DP_RC, 2, 0, 0,
                                     30 /* XXX */, 0,
                                     10 /* XXX */, 300 /* XXX */, 0, 0, 0, 0);
            default:
               if (devinfo->ver >= 30)
                  return calculate_desc(info, EU_UNIT_DP_RC, 2, 0, 0,
                                        0, 400 /* XXX */,
                                        10 /* XXX */, 300 /* XXX */, 0, 0,
                                        0, 0);
               else
                  return calculate_desc(info, EU_UNIT_DP_RC, 2, 0, 0,
                                        0, 450 /* XXX */,
                                        10 /* XXX */, 300 /* XXX */, 0, 0,
                                        0, 0);
            }
         case BRW_SFID_SAMPLER: {
            return calculate_desc(info, EU_UNIT_SAMPLER, 2, 0, 0, 0, 16,
                                  8, 750, 0, 0, 2, 0);
         }
         case BRW_SFID_HDC0:
            switch (brw_dp_desc_msg_type(devinfo, info.desc)) {
            case GFX7_DATAPORT_DC_MEMORY_FENCE:
               return calculate_desc(info, EU_UNIT_DP_DC, 2, 0, 0,
                                     30 /* XXX */, 0,
                                     10 /* XXX */, 100 /* XXX */, 0, 0, 0, 0);
            default:
               return calculate_desc(info, EU_UNIT_DP_DC, 2, 0, 0,
                                     0, 20 /* XXX */,
                                     10 /* XXX */, 100 /* XXX */, 0, 0,
                                     0, 0);
            }

         case BRW_SFID_HDC1:
            switch (brw_dp_desc_msg_type(devinfo, info.desc)) {
            case HSW_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP:
            case HSW_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP_SIMD4X2:
            case HSW_DATAPORT_DC_PORT1_TYPED_ATOMIC_OP_SIMD4X2:
            case HSW_DATAPORT_DC_PORT1_TYPED_ATOMIC_OP:
               return calculate_desc(info, EU_UNIT_DP_DC, 2, 0, 0,
                                     30 /* XXX */, 400 /* XXX */,
                                     10 /* XXX */, 100 /* XXX */, 0, 0,
                                     0, 400 /* XXX */);

            default:
               return calculate_desc(info, EU_UNIT_DP_DC, 2, 0, 0,
                                     0, 20 /* XXX */,
                                     10 /* XXX */, 100 /* XXX */, 0, 0,
                                     0, 0);
            }

         case BRW_SFID_PIXEL_INTERPOLATOR:
            return calculate_desc(info, EU_UNIT_PI, 2, 0, 0, 14 /* XXX */, 0,
                                  0, 90 /* XXX */, 0, 0, 0, 0);

         case BRW_SFID_UGM:
         case BRW_SFID_TGM:
         case BRW_SFID_SLM:
            switch (lsc_msg_desc_opcode(devinfo, info.desc)) {
            case LSC_OP_LOAD:
            case LSC_OP_STORE:
            case LSC_OP_LOAD_CMASK:
            case LSC_OP_STORE_CMASK:
            case LSC_OP_LOAD_CMASK_MSRT:
            case LSC_OP_STORE_CMASK_MSRT:
               return calculate_desc(info, EU_UNIT_DP_DC, 2, 0, 0,
                                     0, 20 /* XXX */,
                                     10 /* XXX */, 100 /* XXX */, 0, 0,
                                     0, 0);

            case LSC_OP_FENCE:
               return calculate_desc(info, EU_UNIT_DP_DC, 2, 0, 0,
                                     30 /* XXX */, 0,
                                     10 /* XXX */, 100 /* XXX */, 0, 0, 0, 0);

            case LSC_OP_ATOMIC_INC:
            case LSC_OP_ATOMIC_DEC:
            case LSC_OP_ATOMIC_LOAD:
            case LSC_OP_ATOMIC_STORE:
            case LSC_OP_ATOMIC_ADD:
            case LSC_OP_ATOMIC_SUB:
            case LSC_OP_ATOMIC_MIN:
            case LSC_OP_ATOMIC_MAX:
            case LSC_OP_ATOMIC_UMIN:
            case LSC_OP_ATOMIC_UMAX:
            case LSC_OP_ATOMIC_CMPXCHG:
            case LSC_OP_ATOMIC_FADD:
            case LSC_OP_ATOMIC_FSUB:
            case LSC_OP_ATOMIC_FMIN:
            case LSC_OP_ATOMIC_FMAX:
            case LSC_OP_ATOMIC_FCMPXCHG:
            case LSC_OP_ATOMIC_AND:
            case LSC_OP_ATOMIC_OR:
            case LSC_OP_ATOMIC_XOR:
               return calculate_desc(info, EU_UNIT_DP_DC, 2, 0, 0,
                                     1300 /* XXX */, 0 /* XXX */,
                                     10 /* XXX */, 100 /* XXX */, 0, 0,
                                     0, 400 /* XXX */);
            default:
               abort();
            }

         case BRW_SFID_MESSAGE_GATEWAY:
         case BRW_SFID_BINDLESS_THREAD_DISPATCH: /* or THREAD_SPAWNER */
         case BRW_SFID_RAY_TRACE_ACCELERATOR:
            return calculate_desc(info, EU_UNIT_SPAWNER, 2, 0, 0, 0 /* XXX */, 0,
                                  10 /* XXX */, 0, 0, 0, 0, 0);

         case BRW_SFID_URB:
            if (brw_urb_desc_msg_type(devinfo, info.desc) ==
                GFX125_URB_OPCODE_FENCE) {
               return calculate_desc(info, EU_UNIT_DP_DC, 2, 0, 0,
                                     30 /* XXX */, 0,
                                     10 /* XXX */, 100 /* XXX */, 0, 0, 0, 0);
            }

            return calculate_desc(info, EU_UNIT_URB, 2, 0, 0, 0, 6 /* XXX */,
                                  32 /* XXX */, 200 /* XXX */, 0, 0, 0, 0);

         default:
            abort();
         }

      case SHADER_OPCODE_UNDEF:
      case SHADER_OPCODE_HALT_TARGET:
      case FS_OPCODE_SCHEDULING_FENCE:
      case SHADER_OPCODE_FLOW:
         return calculate_desc(info, EU_UNIT_NULL, 0, 0, 0, 0, 0,
                               0, 0, 0, 0, 0, 0);

      default:
         abort();
      }
   }

   /**
    * Model the performance behavior of a stall on the specified dependency
    * ID.
    */
   void
   stall_on_dependency(state &st, enum intel_eu_dependency_id id)
   {
      if (id < st.num_dependency_ids)
         st.unit_ready[EU_UNIT_FE] = MAX2(st.unit_ready[EU_UNIT_FE],
                                       st.dep_ready[id]);
   }

   /**
    * Model the performance behavior of the front-end and back-end while
    * executing an instruction with the specified timing information, assuming
    * all dependencies are already clear.
    */
   void
   execute_instruction(state &st, const perf_desc &perf)
   {
      /* Compute the time at which the front-end will be ready to execute the
       * next instruction.
       */
      st.unit_ready[EU_UNIT_FE] += perf.df;

      if (perf.u < EU_NUM_UNITS) {
         /* Wait for the back-end to be ready to execute this instruction. */
         st.unit_ready[EU_UNIT_FE] = MAX2(st.unit_ready[EU_UNIT_FE],
                                       st.unit_ready[perf.u]);

         /* Compute the time at which the back-end will be ready to execute
          * the next instruction, and update the back-end utilization.
          */
         st.unit_ready[perf.u] = st.unit_ready[EU_UNIT_FE] + perf.db;
         st.unit_busy[perf.u] += perf.db * st.weight;
      }
   }

   /**
    * Model the performance behavior of a read dependency provided by an
    * instruction.
    */
   void
   mark_read_dependency(state &st, const perf_desc &perf,
                        enum intel_eu_dependency_id id)
   {
      if (id < st.num_dependency_ids)
         st.dep_ready[id] = st.unit_ready[EU_UNIT_FE] + perf.ls;
   }

   /**
    * Model the performance behavior of a write dependency provided by an
    * instruction.
    */
   void
   mark_write_dependency(state &st, const perf_desc &perf,
                         enum intel_eu_dependency_id id)
   {
      if (id >= EU_DEPENDENCY_ID_ACCUM0 && id < EU_DEPENDENCY_ID_FLAG0)
         st.dep_ready[id] = st.unit_ready[EU_UNIT_FE] + perf.la;
      else if (id >= EU_DEPENDENCY_ID_FLAG0 && id < EU_DEPENDENCY_ID_SBID_WR0)
         st.dep_ready[id] = st.unit_ready[EU_UNIT_FE] + perf.lf;
      else if (id < st.num_dependency_ids)
         st.dep_ready[id] = st.unit_ready[EU_UNIT_FE] + perf.ld;
   }

   /**
    * Return the dependency ID of a backend_reg, offset by \p delta GRFs.
    */
   enum intel_eu_dependency_id
   reg_dependency_id(const intel_device_info *devinfo, const brw_reg &r,
                     const int delta)
   {
      if (r.file == VGRF) {
         const unsigned i = r.nr;
         return intel_eu_dependency_id(EU_DEPENDENCY_ID_GRF0 + i);

      } else if (r.file == FIXED_GRF) {
         const unsigned i = r.nr + delta;
         assert(i < XE3_MAX_GRF);
         return intel_eu_dependency_id(EU_DEPENDENCY_ID_GRF0 + i);

      } else if (r.file == ARF && r.nr >= BRW_ARF_ADDRESS &&
                 r.nr < BRW_ARF_ACCUMULATOR) {
         assert(delta == 0);
         return EU_DEPENDENCY_ID_ADDR0;

      } else if (r.file == ARF && r.nr >= BRW_ARF_ACCUMULATOR &&
                 r.nr < BRW_ARF_FLAG) {
         const unsigned i = r.nr - BRW_ARF_ACCUMULATOR + delta;
         assert(i < EU_DEPENDENCY_ID_FLAG0 - EU_DEPENDENCY_ID_ACCUM0);
         return intel_eu_dependency_id(EU_DEPENDENCY_ID_ACCUM0 + i);

      } else {
         return EU_DEPENDENCY_ID_INVALID;
      }
   }

   /**
    * Return the dependency ID of flag register starting at offset \p i.
    */
   enum intel_eu_dependency_id
   flag_dependency_id(unsigned i)
   {
      assert(i < EU_DEPENDENCY_ID_SBID_WR0 - EU_DEPENDENCY_ID_FLAG0);
      return intel_eu_dependency_id(EU_DEPENDENCY_ID_FLAG0 + i);
   }

   /**
    * Return the dependency ID corresponding to the SBID read completion
    * condition of a Gfx12+ SWSB.
    */
   enum intel_eu_dependency_id
   tgl_swsb_rd_dependency_id(tgl_swsb swsb)
   {
      if (swsb.mode) {
         assert(swsb.sbid < EU_DEPENDENCY_ID_GRF0 - EU_DEPENDENCY_ID_SBID_RD0);
         return intel_eu_dependency_id(EU_DEPENDENCY_ID_SBID_RD0 + swsb.sbid);
      } else {
         return EU_DEPENDENCY_ID_INVALID;
      }
   }

   /**
    * Return the dependency ID corresponding to the SBID write completion
    * condition of a Gfx12+ SWSB.
    */
   enum intel_eu_dependency_id
   tgl_swsb_wr_dependency_id(tgl_swsb swsb)
   {
      if (swsb.mode) {
         assert(swsb.sbid <
                EU_DEPENDENCY_ID_SBID_RD0 - EU_DEPENDENCY_ID_SBID_WR0);
         return intel_eu_dependency_id(EU_DEPENDENCY_ID_SBID_WR0 + swsb.sbid);
      } else {
         return EU_DEPENDENCY_ID_INVALID;
      }
   }

   /**
    * Return the implicit accumulator register accessed by channel \p i of the
    * instruction.
    */
   unsigned
   accum_reg_of_channel(const intel_device_info *devinfo,
                        const brw_inst *inst,
                        brw_reg_type tx, unsigned i)
   {
      assert(inst->reads_accumulator_implicitly() ||
             inst->writes_accumulator_implicitly(devinfo));
      const unsigned offset = (inst->group + i) * brw_type_size_bytes(tx) *
         (brw_type_is_float(tx) ? 1 : 2);
      return offset / (reg_unit(devinfo) * REG_SIZE) % 2;
   }

   /**
    * Model the performance behavior of an FS back-end instruction.
    */
   void
   issue_inst(state &st, const struct brw_isa_info *isa,
              const brw_inst *inst)
   {
      const struct intel_device_info *devinfo = isa->devinfo;
      const instruction_info info(isa, inst);
      const perf_desc perf = instruction_desc(info);

      /* Stall on any source dependencies. */
      for (unsigned i = 0; i < inst->sources; i++) {
         for (unsigned j = 0; j < regs_read(devinfo, inst, i); j++)
            stall_on_dependency(
               st, reg_dependency_id(devinfo, inst->src[i], j));
      }

      if (inst->reads_accumulator_implicitly()) {
         for (unsigned j = accum_reg_of_channel(devinfo, inst, info.tx, 0);
              j <= accum_reg_of_channel(devinfo, inst, info.tx,
                                        inst->exec_size - 1); j++)
            stall_on_dependency(
               st, reg_dependency_id(devinfo, brw_acc_reg(8), j));
      }

      if (const unsigned mask = inst->flags_read(devinfo)) {
         for (unsigned i = 0; i < sizeof(mask) * CHAR_BIT; i++) {
            if (mask & (1 << i))
               stall_on_dependency(st, flag_dependency_id(i));
         }
      }

      /* Stall on any write dependencies. */
      if (inst->dst.file != BAD_FILE && !inst->dst.is_null()) {
         for (unsigned j = 0; j < regs_written(inst); j++)
            stall_on_dependency(
               st, reg_dependency_id(devinfo, inst->dst, j));
      }

      if (inst->writes_accumulator_implicitly(devinfo)) {
         for (unsigned j = accum_reg_of_channel(devinfo, inst, info.tx, 0);
              j <= accum_reg_of_channel(devinfo, inst, info.tx,
                                        inst->exec_size - 1); j++)
            stall_on_dependency(
               st, reg_dependency_id(devinfo, brw_acc_reg(8), j));
      }

      if (const unsigned mask = inst->flags_written(devinfo)) {
         for (unsigned i = 0; i < sizeof(mask) * CHAR_BIT; i++) {
            if (mask & (1 << i))
               stall_on_dependency(st, flag_dependency_id(i));
         }
      }

      /* Stall on any SBID dependencies. */
      if (inst->sched.mode & (TGL_SBID_SET | TGL_SBID_DST))
         stall_on_dependency(st, tgl_swsb_wr_dependency_id(inst->sched));
      else if (inst->sched.mode & TGL_SBID_SRC)
         stall_on_dependency(st, tgl_swsb_rd_dependency_id(inst->sched));

      /* Execute the instruction. */
      execute_instruction(st, perf);

      /* Mark any source dependencies. */
      if (inst->is_send()) {
         for (unsigned i = 0; i < inst->sources; i++) {
            if (inst->is_payload(i)) {
               for (unsigned j = 0; j < regs_read(devinfo, inst, i); j++)
                  mark_read_dependency(
                     st, perf, reg_dependency_id(devinfo, inst->src[i], j));
            }
         }
      }

      /* Mark any destination dependencies. */
      if (inst->dst.file != BAD_FILE && !inst->dst.is_null()) {
         for (unsigned j = 0; j < regs_written(inst); j++) {
            mark_write_dependency(st, perf,
                                  reg_dependency_id(devinfo, inst->dst, j));
         }
      }

      if (inst->writes_accumulator_implicitly(devinfo)) {
         for (unsigned j = accum_reg_of_channel(devinfo, inst, info.tx, 0);
              j <= accum_reg_of_channel(devinfo, inst, info.tx,
                                        inst->exec_size - 1); j++)
            mark_write_dependency(st, perf,
                                  reg_dependency_id(devinfo, brw_acc_reg(8), j));
      }

      if (const unsigned mask = inst->flags_written(devinfo)) {
         for (unsigned i = 0; i < sizeof(mask) * CHAR_BIT; i++) {
            if (mask & (1 << i))
               mark_write_dependency(st, perf, flag_dependency_id(i));
         }
      }

      /* Mark any SBID dependencies. */
      if (inst->sched.mode & TGL_SBID_SET) {
         mark_read_dependency(st, perf, tgl_swsb_rd_dependency_id(inst->sched));
         mark_write_dependency(st, perf, tgl_swsb_wr_dependency_id(inst->sched));
      }
   }

   /**
    * Calculate the maximum possible throughput of the program compatible with
    * the cycle-count utilization estimated for each asynchronous unit, in
    * threads-per-cycle units.
    */
   float
   calculate_thread_throughput(const state &st, float busy)
   {
      for (unsigned i = 0; i < EU_NUM_UNITS; i++)
         busy = MAX2(busy, st.unit_busy[i]);

      return 1.0 / busy;
   }

   /**
    * Calculate the number of threads of this program that can run
    * concurrently in an EU based on the estimate of register pressure
    * derived from liveness information (pre-RA) or on the actual
    * number of GRFs used if available (post-RA).  Platforms prior to
    * xe3 don't support VRT so we can just return the constant value
    * from device info.
    */
   unsigned
   calculate_threads_per_eu(const brw_shader *s)
   {
      if (s->devinfo->ver >= 30) {
         unsigned grf_used = s->grf_used;

         if (!grf_used) {
            const brw_register_pressure &rp = s->regpressure_analysis.require();
            const unsigned max_regs_live = *std::max_element(rp.regs_live_at_ip,
               rp.regs_live_at_ip + s->cfg->total_instructions);
            grf_used = DIV_ROUND_UP(max_regs_live, reg_unit(s->devinfo));
         }

         return 32 / MAX2(3, ptl_register_blocks(grf_used) + 1);
      } else {
         return s->devinfo->num_thread_per_eu;
      }
   }

   /**
    * Estimate the performance of the specified shader.
    */
   void
   calculate_performance(brw_performance &p, const brw_shader *s,
                         unsigned dispatch_width)
   {
      /* XXX - Note that the previous version of this code used worst-case
       *       scenario estimation of branching divergence for SIMD32 shaders,
       *       but this heuristic was removed to improve performance in common
       *       scenarios. Wider shader variants are less optimal when divergence
       *       is high, e.g. when application renders complex scene on a small
       *       surface. It is assumed that such renders are short, so their
       *       time doesn't matter and when it comes to the overall performance,
       *       they are dominated by more optimal larger renders.
       *
       *       It's possible that we could do better with divergence analysis
       *       by isolating branches which are 100% uniform.
       *
       *       Plumbing the trip counts from NIR loop analysis would allow us
       *       to do a better job regarding the loop weights.
       *
       *       In the meantime use values that roughly match the control flow
       *       weights used elsewhere in the compiler back-end.
       *
       *       Note that we provide slightly more pessimistic weights on
       *       Gfx12.x for SIMD32, since the effective warp size on that
       *       platform is 2x the SIMD width due to EU fusion, which increases
       *       the likelihood of divergent control flow in comparison to
       *       previous generations, giving narrower SIMD modes a performance
       *       advantage in several test-cases with non-uniform discard jumps.
       *       EU fusion has been removed on Xe2+ so its divergence behavior is
       *       expected to be closer to pre-Gfx12 platforms.
       */
      const float discard_weight =
         s->devinfo->ver >= 30 ? (dispatch_width > 16 ? 0.75 : 0.525) :
         s->devinfo->ver == 12 ? (dispatch_width > 16 ? 1.0 : 0.5) :
         1.0;
      const float loop_weight = 10;
      unsigned halt_count = 0;
      unsigned elapsed = 0;
      state st { s };

      foreach_block(block, s->cfg) {
         const unsigned elapsed0 = elapsed;

         foreach_inst_in_block(brw_inst, inst, block) {
            const unsigned clock0 = st.unit_ready[EU_UNIT_FE];

            issue_inst(st, &s->compiler->isa, inst);

            if (inst->opcode == SHADER_OPCODE_HALT_TARGET && halt_count)
               st.weight /= discard_weight;

            elapsed += (st.unit_ready[EU_UNIT_FE] - clock0) * st.weight;

            if (inst->opcode == BRW_OPCODE_DO)
               st.weight *= loop_weight;
            else if (inst->opcode == BRW_OPCODE_WHILE)
               st.weight /= loop_weight;
            else if (inst->opcode == BRW_OPCODE_HALT && !halt_count++)
               st.weight *= discard_weight;
         }

         p.block_latency[block->num] = elapsed - elapsed0;
      }

      p.latency = elapsed;
      p.throughput = dispatch_width * calculate_threads_per_eu(s) *
                     calculate_thread_throughput(st, elapsed);
   }
}

brw_performance::brw_performance(const brw_shader *v) :
   block_latency(new unsigned[v->cfg->num_blocks])
{
   calculate_performance(*this, v, v->dispatch_width);
}

brw_performance::~brw_performance()
{
   delete[] block_latency;
}
