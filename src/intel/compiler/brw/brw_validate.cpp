/*
 * Copyright © 2015 Intel Corporation
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

/** @file
 *
 * Implements a pass that validates various invariants of the IR.  The current
 * pass only validates that GRF's uses are sane.  More can be added later.
 */

#include "brw_shader.h"
#include "brw_cfg.h"
#include "brw_eu.h"

#ifndef NDEBUG
static void
brw_validate_abort_with_message_fmt(const brw_shader &s,
                                    const brw_inst *inst,
                                    const char *filename,
                                    unsigned line,
                                    const char *fmt, ...)
{
   fprintf(stderr,
           "ASSERT: BRW %s validation failed!\n",
           _mesa_shader_stage_to_abbrev(s.stage));

   brw_print_instruction(s, inst, stderr);

   fprintf(stderr, "%s:%d: ", filename, line);

   va_list args;
   va_start(args, fmt);
   vfprintf(stderr, fmt, args);
   va_end(args);

   fputc('\n', stderr);
   abort();
}

#define VAL_ASSERT(assertion)                                           \
   if (!(assertion)) {                                                  \
      brw_validate_abort_with_message_fmt(                              \
         s, inst, __FILE__, __LINE__,                                   \
         "'%s' failed\n", #assertion);                                  \
   }

#define VAL_ASSERT_CMP(A, COMPARATOR, B)                                \
   {                                                                    \
      const uintptr_t a = uintptr_t(A);                                 \
      const uintptr_t b = uintptr_t(B);                                 \
      if (!(a COMPARATOR b)) {                                          \
         brw_validate_abort_with_message_fmt(                           \
            s, inst, __FILE__, __LINE__,                                \
            "A %s B failed\n"                                           \
            "  A = %s = %" PRIuPTR "\n"                                 \
            "  B = %s = %" PRIuPTR "\n",                                \
            #COMPARATOR, #A, a, #B, b);                                 \
      }                                                                 \
   }

#define VAL_ASSERT_EQ(A, B) VAL_ASSERT_CMP(A, ==, B)
#define VAL_ASSERT_NE(A, B) VAL_ASSERT_CMP(A, !=, B)
#define VAL_ASSERT_LT(A, B) VAL_ASSERT_CMP(A,  <, B)
#define VAL_ASSERT_LE(A, B) VAL_ASSERT_CMP(A, <=, B)
#define VAL_ASSERT_GT(A, B) VAL_ASSERT_CMP(A,  >, B)
#define VAL_ASSERT_GE(A, B) VAL_ASSERT_CMP(A, >=, B)

static inline bool
is_ud_imm(const brw_reg &reg)
{
   return reg.file == IMM && reg.type == BRW_TYPE_UD;
}

static void
validate_memory_logical(const brw_shader &s, const brw_mem_inst *inst)
{
   const intel_device_info *devinfo = s.devinfo;

   enum lsc_opcode op = inst->lsc_op;
   enum memory_flags flags = (memory_flags)inst->flags;
   bool transpose = flags & MEMORY_FLAG_TRANSPOSE;
   bool include_helpers = flags & MEMORY_FLAG_INCLUDE_HELPERS;
   enum memory_logical_mode mode = inst->mode;

   enum lsc_data_size data_size = inst->data_size;
   unsigned data_size_B = lsc_data_size_bytes(data_size);

   if (!devinfo->has_lsc) {
      VAL_ASSERT(data_size == LSC_DATA_SIZE_D8U32 ||
                 data_size == LSC_DATA_SIZE_D16U32 ||
                 data_size == LSC_DATA_SIZE_D32 ||
                 data_size == LSC_DATA_SIZE_D64);

      if (transpose) {
         const unsigned min_alignment =
            mode == MEMORY_MODE_SHARED_LOCAL ? 16 : 4;
         VAL_ASSERT_GE(inst->alignment, min_alignment);
      }
   }

   VAL_ASSERT(!transpose || !include_helpers);
   VAL_ASSERT(!transpose || lsc_opcode_has_transpose(op));

   if (inst->binding_type == LSC_ADDR_SURFTYPE_FLAT)
      VAL_ASSERT_EQ(inst->src[MEMORY_LOGICAL_BINDING].file, BAD_FILE);

   if (inst->src[MEMORY_LOGICAL_DATA1].file != BAD_FILE) {
      VAL_ASSERT_EQ(inst->components,
                    inst->components_read(MEMORY_LOGICAL_DATA1));

      VAL_ASSERT_EQ(inst->src[MEMORY_LOGICAL_DATA0].type,
                    inst->src[MEMORY_LOGICAL_DATA1].type);
   }

   if (inst->src[MEMORY_LOGICAL_DATA0].file != BAD_FILE) {
      VAL_ASSERT_EQ(inst->components,
                    inst->components_read(MEMORY_LOGICAL_DATA0));

      VAL_ASSERT_EQ(brw_type_size_bytes(inst->src[MEMORY_LOGICAL_DATA0].type),
                    data_size_B);
   }

   if (inst->dst.file != BAD_FILE)
      VAL_ASSERT_EQ(brw_type_size_bytes(inst->dst.type), data_size_B);

   /** TGM messages cannot have a base offset */
   if (mode == MEMORY_MODE_TYPED)
      VAL_ASSERT_EQ(inst->as_mem()->address_offset, 0);

   /* Offset must be DWord aligned */
   VAL_ASSERT_EQ((inst->as_mem()->address_offset % 4), 0);

   switch (inst->opcode) {
   case SHADER_OPCODE_MEMORY_LOAD_LOGICAL:
      VAL_ASSERT(op == LSC_OP_LOAD || op == LSC_OP_LOAD_CMASK ||
                 op == LSC_OP_LOAD_CMASK_MSRT);
      VAL_ASSERT_EQ(inst->src[MEMORY_LOGICAL_DATA0].file, BAD_FILE);
      VAL_ASSERT_EQ(inst->src[MEMORY_LOGICAL_DATA1].file, BAD_FILE);
      break;
   case SHADER_OPCODE_MEMORY_STORE_LOGICAL:
      VAL_ASSERT(lsc_opcode_is_store(op));
      VAL_ASSERT_NE(inst->src[MEMORY_LOGICAL_DATA0].file, BAD_FILE);
      VAL_ASSERT_EQ(inst->src[MEMORY_LOGICAL_DATA1].file, BAD_FILE);
      break;
   case SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL:
      VAL_ASSERT(lsc_opcode_is_atomic(op));
      VAL_ASSERT_EQ((inst->src[MEMORY_LOGICAL_DATA0].file == BAD_FILE),
                    (lsc_op_num_data_values(op) < 1));
      VAL_ASSERT_EQ((inst->src[MEMORY_LOGICAL_DATA1].file == BAD_FILE),
                    (lsc_op_num_data_values(op) < 2));
      VAL_ASSERT_EQ(inst->components, 1);
      VAL_ASSERT(!include_helpers);
      break;
   default:
      UNREACHABLE("invalid opcode");
   }
}

static const char *
brw_shader_phase_to_string(enum brw_shader_phase phase)
{
   switch (phase) {
   case BRW_SHADER_PHASE_INITIAL:               return "INITIAL";
   case BRW_SHADER_PHASE_AFTER_NIR:             return "AFTER_NIR";
   case BRW_SHADER_PHASE_AFTER_OPT_LOOP:        return "AFTER_OPT_LOOP";
   case BRW_SHADER_PHASE_AFTER_EARLY_LOWERING:  return "AFTER_EARLY_LOWERING";
   case BRW_SHADER_PHASE_AFTER_MIDDLE_LOWERING: return "AFTER_MIDDLE_LOWERING";
   case BRW_SHADER_PHASE_AFTER_LATE_LOWERING:   return "AFTER_LATE_LOWERING";
   case BRW_SHADER_PHASE_AFTER_REGALLOC:        return "AFTER_REGALLOC";
   case BRW_SHADER_PHASE_INVALID:               break;
   }
   UNREACHABLE("invalid_phase");
   return NULL;
}

static void
brw_validate_instruction_phase(const brw_shader &s, brw_inst *inst)
{
   enum brw_shader_phase invalid_from = BRW_SHADER_PHASE_INVALID;

   switch (inst->opcode) {
   case FS_OPCODE_FB_WRITE_LOGICAL:
   case FS_OPCODE_FB_READ_LOGICAL:
   case SHADER_OPCODE_SAMPLER:
   case SHADER_OPCODE_MEMORY_LOAD_LOGICAL:
   case SHADER_OPCODE_MEMORY_STORE_LOGICAL:
   case SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL:
   case FS_OPCODE_VARYING_PULL_CONSTANT_LOAD_LOGICAL:
   case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
   case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
   case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
   case SHADER_OPCODE_BTD_SPAWN_LOGICAL:
   case SHADER_OPCODE_BTD_RETIRE_LOGICAL:
   case RT_OPCODE_TRACE_RAY_LOGICAL:
   case SHADER_OPCODE_URB_READ_LOGICAL:
   case SHADER_OPCODE_URB_WRITE_LOGICAL:
   case SHADER_OPCODE_REDUCE:
   case SHADER_OPCODE_INCLUSIVE_SCAN:
   case SHADER_OPCODE_EXCLUSIVE_SCAN:
   case SHADER_OPCODE_VOTE_ANY:
   case SHADER_OPCODE_VOTE_ALL:
   case SHADER_OPCODE_VOTE_EQUAL:
   case SHADER_OPCODE_BALLOT:
   case SHADER_OPCODE_QUAD_SWAP:
   case SHADER_OPCODE_READ_FROM_LIVE_CHANNEL:
   case SHADER_OPCODE_READ_FROM_CHANNEL:
   case SHADER_OPCODE_LOAD_REG:
      invalid_from = BRW_SHADER_PHASE_AFTER_EARLY_LOWERING;
      break;

   case SHADER_OPCODE_LOAD_PAYLOAD:
      invalid_from = BRW_SHADER_PHASE_AFTER_MIDDLE_LOWERING;
      break;

   default:
      /* Nothing to do. */
      break;
   }

   assert(s.phase < BRW_SHADER_PHASE_INVALID);
   if (s.phase >= invalid_from) {
      fprintf(stderr, "INVALID INSTRUCTION IN PHASE: %s\n",
              brw_shader_phase_to_string(s.phase));
      brw_print_instruction(s, inst, stderr);
      abort();
   }
}

void
brw_validate(const brw_shader &s)
{
   const intel_device_info *devinfo = s.devinfo;

   if (!s.cfg)
      return;

   s.cfg->validate(_mesa_shader_stage_to_abbrev(s.stage));

   foreach_block(block, s.cfg) {
      /* Track the last used address register. Usage of the address register
       * in the IR should be limited to within a block, otherwise we would
       * unable to schedule some instructions without spilling the address
       * register to a VGRF.
       *
       * Another pattern we stick to when using the address register in the IR
       * is that we write and read the register in pairs of instruction.
       */
      uint32_t last_used_address_register[16] = {};

      foreach_inst_in_block (brw_inst, inst, block) {
         VAL_ASSERT_EQ(inst->kind, brw_inst_kind_for_opcode(inst->opcode));

         brw_validate_instruction_phase(s, inst);

         switch (inst->opcode) {
         case SHADER_OPCODE_SEND:
            VAL_ASSERT_EQ(inst->sources, SEND_NUM_SRCS);
            VAL_ASSERT(is_uniform(inst->src[SEND_SRC_DESC]));
            VAL_ASSERT(is_uniform(inst->src[SEND_SRC_EX_DESC]));
            VAL_ASSERT_NE(inst->src[SEND_SRC_PAYLOAD1].file, BAD_FILE);
            VAL_ASSERT(inst->as_send()->ex_mlen > 0 ||
                       inst->src[SEND_SRC_PAYLOAD2].file == BAD_FILE);
            /* Send payloads cannot be immediates nor have source modifiers */
            for (unsigned i = 0; i < 2; i++) {
               VAL_ASSERT_NE(inst->src[SEND_SRC_PAYLOAD1 + i].file, IMM);
               VAL_ASSERT(!inst->src[SEND_SRC_PAYLOAD1 + i].abs);
               VAL_ASSERT(!inst->src[SEND_SRC_PAYLOAD1 + i].negate);
            }
            break;

         case SHADER_OPCODE_SEND_GATHER:
            VAL_ASSERT(is_uniform(inst->src[0]) && is_uniform(inst->src[1]));
            VAL_ASSERT_GE(devinfo->ver, 30);
            break;

         case BRW_OPCODE_MOV:
            VAL_ASSERT_EQ(inst->sources, 1);
            break;

         case SHADER_OPCODE_MEMORY_LOAD_LOGICAL:
         case SHADER_OPCODE_MEMORY_STORE_LOGICAL:
         case SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL:
            validate_memory_logical(s, inst->as_mem());
            break;

         case SHADER_OPCODE_MEMORY_FENCE:
         case SHADER_OPCODE_INTERLOCK:
            VAL_ASSERT_EQ(inst->exec_size, 1);
            VAL_ASSERT(inst->force_writemask_all);
            VAL_ASSERT_EQ(inst->sources, 2);
            VAL_ASSERT(is_ud_imm(inst->src[1])); /* commit enable */
            break;

         case SHADER_OPCODE_LOAD_REG: {
            VAL_ASSERT_EQ(inst->sources, 1);
            VAL_ASSERT_EQ(s.alloc.sizes[inst->dst.nr] * REG_SIZE, inst->size_written);
            VAL_ASSERT(!inst->is_partial_write());
            VAL_ASSERT_LE(inst->src[0].stride, 1);

            /* For example, if file == UNIFORM, stride will be zero and offset
             * may be non-zero.
             */
            if (inst->src[0].stride != 0)
               VAL_ASSERT_EQ(inst->src[0].offset, 0);

            const brw_def_analysis &defs = s.def_analysis.require();
            VAL_ASSERT_EQ(inst, defs.get(inst->dst));

            break;
         }

         case BRW_OPCODE_BFN:
            VAL_ASSERT(!inst->src[0].abs && !inst->src[0].negate);
            VAL_ASSERT(!inst->src[1].abs && !inst->src[1].negate);
            VAL_ASSERT(!inst->src[2].abs && !inst->src[2].negate);
            VAL_ASSERT_EQ(inst->src[3].file, IMM);

            /* BFN only supports a limited subset of conditions. */
            VAL_ASSERT(inst->conditional_mod == BRW_CONDITIONAL_NONE ||
                       inst->conditional_mod == BRW_CONDITIONAL_Z ||
                       inst->conditional_mod == BRW_CONDITIONAL_G ||
                       inst->conditional_mod == BRW_CONDITIONAL_L);
            break;

         default:
            break;
         }

         /* On Xe2, the "write the accumulator in addition to the explicit
          * destination" bit no longer exists. Try to catch uses of this
          * feature earlier in the process.
          */
         if (devinfo->ver >= 20 && inst->writes_accumulator) {
            VAL_ASSERT(inst->dst.is_accumulator() ||
                       inst->opcode == BRW_OPCODE_ADDC ||
                       inst->opcode == BRW_OPCODE_MACH ||
                       inst->opcode == BRW_OPCODE_SUBB);
         }

         if (inst->is_3src(s.compiler)) {
            const unsigned integer_sources =
               brw_type_is_int(inst->src[0].type) +
               brw_type_is_int(inst->src[1].type) +
               brw_type_is_int(inst->src[2].type);
            const unsigned float_sources =
               brw_type_is_float_or_bfloat(inst->src[0].type) +
               brw_type_is_float_or_bfloat(inst->src[1].type) +
               brw_type_is_float_or_bfloat(inst->src[2].type);

            VAL_ASSERT((integer_sources == 3 && float_sources == 0) ||
                       (integer_sources == 0 && float_sources == 3));

            if (devinfo->ver >= 10) {
               for (unsigned i = 0; i < 3; i++) {
                  if (inst->src[i].file == IMM)
                     continue;

                  switch (inst->src[i].vstride) {
                  case BRW_VERTICAL_STRIDE_0:
                  case BRW_VERTICAL_STRIDE_4:
                  case BRW_VERTICAL_STRIDE_8:
                  case BRW_VERTICAL_STRIDE_16:
                     break;

                  case BRW_VERTICAL_STRIDE_1:
                     VAL_ASSERT_GE(devinfo->ver, 12);
                     break;

                  case BRW_VERTICAL_STRIDE_2:
                     VAL_ASSERT_LE(devinfo->ver, 11);
                     break;

                  default:
                     VAL_ASSERT(!"invalid vstride");
                     break;
                  }
               }
            } else if (s.grf_used != 0) {
               /* Only perform the pre-Gfx10 checks after register allocation
                * has occured.
                *
                * Many passes (e.g., constant copy propagation) will
                * genenerate invalid 3-source instructions with the
                * expectation that later passes (e.g., combine constants) will
                * fix them.
                */
               for (unsigned i = 0; i < 3; i++) {
                  VAL_ASSERT_NE(inst->src[i].file, IMM);

                  /* A stride of 1 (the usual case) or 0, with a special
                   * "repctrl" bit, is allowed. The repctrl bit doesn't work
                   * for 64-bit datatypes, so if the source type is 64-bit
                   * then only a stride of 1 is allowed. From the Broadwell
                   * PRM, Volume 7 "3D Media GPGPU", page 944:
                   *
                   *    This is applicable to 32b datatypes and 16b datatype.
                   *    64b datatypes cannot use the replicate control.
                   */
                  const unsigned stride_in_bytes = byte_stride(inst->src[i]);
                  const unsigned size_in_bytes = brw_type_size_bytes(inst->src[i].type);
                  if (stride_in_bytes == 0) {
                     /* If the source is_scalar, then the stride will be
                      * converted to <4;4,1> in brw_lower_scalar_fp64_MAD
                      * after SIMD splitting.
                      */
                     if (!inst->src[i].is_scalar)
                        VAL_ASSERT_LE(size_in_bytes, 4);
                  } else {
                     VAL_ASSERT_EQ(stride_in_bytes, size_in_bytes);
                  }
               }
            }
         }

         if (inst->dst.file == VGRF) {
            VAL_ASSERT_LE(inst->dst.offset / REG_SIZE + regs_written(inst),
                          s.alloc.sizes[inst->dst.nr]);
            if (inst->exec_size > 1)
               VAL_ASSERT_NE(inst->dst.stride, 0);
         } else if (inst->dst.is_address()) {
            VAL_ASSERT_NE(inst->dst.nr, 0);
         }

         bool read_address_reg = false;
         for (unsigned i = 0; i < inst->sources; i++) {
            if (inst->src[i].file == VGRF) {
               VAL_ASSERT_LE(inst->src[i].offset / REG_SIZE + regs_read(devinfo, inst, i),
                             s.alloc.sizes[inst->src[i].nr]);
            } else if (inst->src[i].is_address()) {
               VAL_ASSERT_NE(inst->src[i].nr, 0);
               for (unsigned hw = 0; hw < inst->size_read(devinfo, i); hw += 2) {
                  VAL_ASSERT_EQ(inst->src[i].nr,
                                last_used_address_register[inst->src[i].address_slot(hw)]);
               }
               read_address_reg = true;
            }
         }

         /* Accumulator Registers, bspec 47251:
          *
          * "When destination is accumulator with offset 0, destination
          * horizontal stride must be 1."
          */
         if (intel_needs_workaround(devinfo, 14014617373) &&
             inst->dst.is_accumulator() &&
             phys_subnr(devinfo, inst->dst) == 0) {
            VAL_ASSERT_EQ(inst->dst.hstride, 1);
         }

         if (inst->is_math() && intel_needs_workaround(devinfo, 22016140776)) {
            /* Wa_22016140776:
             *
             *    Scalar broadcast on HF math (packed or unpacked) must not be
             *    used. Compiler must use a mov instruction to expand the
             *    scalar value to a vector before using in a HF (packed or
             *    unpacked) math operation.
             *
             * Since copy propagation knows about this restriction, nothing
             * should be able to generate these invalid source strides. Detect
             * potential problems sooner rather than later.
             */
            if (devinfo->ver >= 20 && inst->writes_accumulator) {
               VAL_ASSERT(inst->dst.is_accumulator() ||
                          inst->opcode == BRW_OPCODE_ADDC ||
                          inst->opcode == BRW_OPCODE_MACH ||
                          inst->opcode == BRW_OPCODE_SUBB);
            }

            if (inst->is_3src(s.compiler)) {
               const unsigned integer_sources =
                  brw_type_is_int(inst->src[0].type) +
                  brw_type_is_int(inst->src[1].type) +
                  brw_type_is_int(inst->src[2].type);
               const unsigned float_sources =
                  brw_type_is_float(inst->src[0].type) +
                  brw_type_is_float(inst->src[1].type) +
                  brw_type_is_float(inst->src[2].type);

               VAL_ASSERT((integer_sources == 3 && float_sources == 0) ||
                          (integer_sources == 0 && float_sources == 3));

               if (devinfo->ver >= 10) {
                  for (unsigned i = 0; i < 3; i++) {
                     if (inst->src[i].file == IMM)
                        continue;

                     switch (inst->src[i].vstride) {
                     case BRW_VERTICAL_STRIDE_0:
                     case BRW_VERTICAL_STRIDE_4:
                     case BRW_VERTICAL_STRIDE_8:
                     case BRW_VERTICAL_STRIDE_16:
                        break;

                     case BRW_VERTICAL_STRIDE_1:
                        VAL_ASSERT_GE(devinfo->ver, 12);
                        break;

                     case BRW_VERTICAL_STRIDE_2:
                        VAL_ASSERT_LE(devinfo->ver, 11);
                        break;

                     default:
                        VAL_ASSERT(!"invalid vstride");
                        break;
                     }
                  }
               } else if (s.grf_used != 0) {
                  /* Only perform the pre-Gfx10 checks after register
                   * allocation has occured.
                   *
                   * Many passes (e.g., constant copy propagation) will
                   * genenerate invalid 3-source instructions with the
                   * expectation that later passes (e.g., combine constants)
                   * will fix them.
                   */
                  for (unsigned i = 0; i < 3; i++) {
                     VAL_ASSERT_NE(inst->src[i].file, IMM);

                     /* A stride of 1 (the usual case) or 0, with a special
                      * "repctrl" bit, is allowed. The repctrl bit doesn't
                      * work for 64-bit datatypes, so if the source type is
                      * 64-bit then only a stride of 1 is allowed. From the
                      * Broadwell PRM, Volume 7 "3D Media GPGPU", page 944:
                      *
                      *    This is applicable to 32b datatypes and 16b
                      *    datatype. 64b datatypes cannot use the replicate
                      *    control.
                      */
                     const unsigned stride_in_bytes = byte_stride(inst->src[i]);
                     const unsigned size_in_bytes = brw_type_size_bytes(inst->src[i].type);
                     if (stride_in_bytes == 0) {
                        VAL_ASSERT_LE(size_in_bytes, 4);
                     } else {
                        VAL_ASSERT_EQ(stride_in_bytes, size_in_bytes);
                     }
                  }
               }
            }

            if (inst->dst.file == VGRF) {
               VAL_ASSERT_LE(inst->dst.offset / REG_SIZE + regs_written(inst),
                             s.alloc.sizes[inst->dst.nr]);
            }

            for (unsigned i = 0; i < inst->sources; i++) {
               VAL_ASSERT(inst->src[i].is_scalar ||
                          !is_uniform(inst->src[i]) ||
                          inst->src[i].type != BRW_TYPE_HF);
            }
         }

         /* Update the last used address register. */
         if (read_address_reg) {
            /* When an instruction only reads the address register, we assume
             * the read parts are never going to be used again.
             */
            for (unsigned i = 0; i < inst->sources; i++) {
               if (!inst->src[i].is_address())
                  continue;
               for (unsigned hw = 0; hw < inst->size_read(devinfo, i); hw += 2)
                  last_used_address_register[inst->src[i].address_slot(hw)] = 0;
            }
         }
         if (inst->dst.is_address()) {
            /* For the written part of the address register */
            for (unsigned hw = 0; hw < inst->size_written; hw += 2)
               last_used_address_register[inst->dst.address_slot(hw)] = inst->dst.nr;
         } else if (inst->uses_address_register_implicitly()) {
            /* If the instruction is making use of the address register,
             * discard the entire thing.
             */
            memset(last_used_address_register, 0,
                   sizeof(last_used_address_register));
         }
      }
   }
}
#endif
