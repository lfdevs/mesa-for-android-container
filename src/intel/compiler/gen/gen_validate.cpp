/*
 * Copyright © 2015-2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "gen_private.h"

#define ERROR(msg, ...) ERROR_IF(true, msg, ##__VA_ARGS__)
#define ERROR_IF(cond, msg, ...)                        \
   do {                                                 \
      if ((cond))                                       \
         report_errorf(msg, ##__VA_ARGS__);             \
   } while(0)

struct gen_validator {
   const intel_device_info *devinfo;

   const gen_inst *inst;
   void *mem_ctx;
   gen_error *errors;
   int num_errors;
   int error_index;
   gen_format format;
   unsigned num_sources;

   gen_validator(const intel_device_info *devinfo, void *mem_ctx)
      : devinfo(devinfo),
        mem_ctx(mem_ctx),
        errors(nullptr),
        num_errors(0),
        error_index(0)
   {}

   void PRINTFLIKE(2, 3)
   report_errorf(const char *fmt, ...)
   {
      errors = reralloc(mem_ctx, errors, gen_error, num_errors + 1);
      errors[num_errors].index = error_index;

      va_list args;
      va_start(args, fmt);
      errors[num_errors].msg = ralloc_vasprintf(mem_ctx, fmt, args);
      va_end(args);

      num_errors++;
   }

   bool
   validate(const gen_inst *inst)
   {
      int old_errors = num_errors;

      this->inst = inst;
      this->format = gen_inst_format(inst->opcode);
      this->num_sources = gen_inst_num_sources(devinfo, inst);

      opcodes();
      if (old_errors != num_errors)
         goto end;

      invalid_values();
      branch_restrictions();
      sources_not_null();
      immediate_restrictions();
      basic_three_source_restrictions();
      send_restrictions();
      general_restrictions_based_on_operand_types();
      general_restrictions_on_region_parameters();
      special_restrictions_for_mixed_float_mode();
      region_alignment_rules();
      vector_immediate_restrictions();
      special_requirements_for_handling_double_precision_data_types();
      instruction_restrictions();
      send_descriptor_restrictions();
      register_region_special_restrictions();
      scalar_register_restrictions();
      xe3p_src1_encoding_restrictions();

   end:
      error_index++;
      return old_errors == num_errors;
   }

private:
   static bool
   is_32bit_integer_operand(const gen_operand &op)
   {
      return gen_type_is_int(op.type) && gen_type_size_bytes(op.type) == 4;
   }

   static bool
   is_vector_immediate_type(gen_reg_type type)
   {
      return type == GEN_TYPE_V ||
             type == GEN_TYPE_UV ||
             type == GEN_TYPE_VF;
   }

   static bool
   is_scalar_register(const gen_operand &op)
   {
      return !op.indirect &&
             op.file == GEN_ARF &&
             op.nr == GEN_ARF_SCALAR;
   }

   bool
   is_csel_type(gen_reg_type type) const
   {
      if (devinfo->ver == 9)
         return type == GEN_TYPE_F;

      return type == GEN_TYPE_F ||
             type == GEN_TYPE_HF ||
             type == GEN_TYPE_D ||
             type == GEN_TYPE_UD ||
             type == GEN_TYPE_W ||
             type == GEN_TYPE_UW;
   }

   static gen_reg_type
   signed_type(gen_reg_type type)
   {
      return gen_type_is_uint(type) ? (gen_reg_type)(type | GEN_TYPE_BASE_SINT) : type;
   }

   static bool
   is_packed(unsigned vstride, unsigned width, unsigned hstride)
   {
      if (vstride == width)
         return vstride == 1 ? hstride == 0 : hstride == 1;

      return false;
   }

   static bool
   uses_basic_regions(gen_format format)
   {
      return format == GEN_FORMAT_BASIC_ONE_SRC ||
             format == GEN_FORMAT_BASIC_TWO_SRC;
   }

   void
   grfs_accessed(uint8_t grf_access_mask[32],
                 unsigned exec_size, unsigned element_size, unsigned subreg,
                 unsigned vstride, unsigned width, unsigned hstride) const
   {
      unsigned rowbase = subreg;
      unsigned element = 0;
      const unsigned safe_width = MAX2(width, 1u);

      for (unsigned y = 0; y < exec_size / safe_width; y++) {
         unsigned offset = rowbase;

         for (unsigned x = 0; x < width; x++) {
            const unsigned start_grf = (offset / devinfo->grf_size) % 8;
            const unsigned end_byte = offset + element_size - 1;
            const unsigned end_grf = (end_byte / devinfo->grf_size) % 8;
            grf_access_mask[element++] = (1u << start_grf) | (1u << end_grf);
            offset += hstride * element_size;
         }

         rowbase += vstride * element_size;
      }

      assert(element == 0 || element == exec_size);
   }

   static unsigned
   registers_read(const uint8_t grfs_accessed[32])
   {
      uint8_t all_read = 0;

      for (unsigned i = 0; i < 32; i++)
         all_read |= grfs_accessed[i];

      return __builtin_popcount(all_read);
   }

   bool
   inst_is_raw_move() const
   {
      const gen_reg_type dst_type = signed_type(inst->dst.type);
      const gen_reg_type src_type = signed_type(inst->src[0].type);

      if (inst->src[0].file == GEN_IMM) {
         if (gen_type_is_vector_imm(inst->src[0].type))
            return false;
      } else if (inst->src[0].negate || inst->src[0].abs) {
         return false;
      }

      return inst->opcode == GEN_OP_MOV &&
             !inst->saturate &&
             dst_type == src_type;
   }

   bool
   is_byte_conversion() const
   {
      const gen_reg_type dst_type = inst->dst.type;
      const gen_reg_type src0_type = inst->src[0].type;

      if (dst_type != src0_type &&
          (gen_type_size_bytes(dst_type) == 1 ||
           gen_type_size_bytes(src0_type) == 1)) {
         return true;
      } else if (num_sources > 1 && !is_null(inst->src[1])) {
         const gen_reg_type src1_type = inst->src[1].type;
         return dst_type != src1_type &&
                (gen_type_size_bytes(dst_type) == 1 ||
                 gen_type_size_bytes(src1_type) == 1);
      }

      return false;
   }

   bool
   is_half_float_conversion() const
   {
      const gen_reg_type dst_type = inst->dst.type;
      const gen_reg_type src0_type = inst->src[0].type;

      if (dst_type != src0_type &&
          (dst_type == GEN_TYPE_HF || src0_type == GEN_TYPE_HF)) {
         return true;
      } else if (num_sources > 1 && !is_null(inst->src[1])) {
         const gen_reg_type src1_type = inst->src[1].type;
         return dst_type != src1_type &&
                (dst_type == GEN_TYPE_HF || src1_type == GEN_TYPE_HF);
      }

      return false;
   }

   bool
   is_mixed_float() const
   {
      if (gen_inst_is_send(inst) || !gen_inst_has_dst(inst->opcode))
         return false;

      if (num_sources == 0)
         return false;

      const gen_reg_type dst_type = inst->dst.type;
      const gen_reg_type src0_type = inst->src[0].type;

      if (num_sources == 1 || (num_sources > 1 && is_null(inst->src[1])))
         return types_are_mixed_float(src0_type, dst_type);

      const gen_reg_type src1_type = inst->src[1].type;
      return types_are_mixed_float(src0_type, src1_type) ||
             types_are_mixed_float(src0_type, dst_type) ||
             types_are_mixed_float(src1_type, dst_type);
   }

   bool
   is_pure_bfloat() const
   {
      if (gen_inst_is_send(inst))
         return false;

      const bool has_dst = gen_inst_has_dst(inst->opcode);
      if (num_sources == 0 && !has_dst)
         return false;

      for (unsigned i = 0; i < num_sources; i++) {
         if (is_null(inst->src[i]) || !gen_type_is_bfloat(inst->src[i].type))
            return false;
      }

      if (has_dst && !gen_type_is_bfloat(inst->dst.type))
         return false;

      return true;
   }

   bool
   is_mixed_bfloat() const
   {
      if (gen_inst_is_send(inst))
         return false;

      const bool has_dst = gen_inst_has_dst(inst->opcode);
      const unsigned operands = num_sources + has_dst;
      if (operands == 0)
         return false;

      unsigned bfloat = 0;
      for (unsigned i = 0; i < num_sources; i++)
         bfloat += gen_type_is_bfloat(inst->src[i].type);
      if (has_dst)
         bfloat += gen_type_is_bfloat(inst->dst.type);

      return bfloat > 0 && bfloat != operands;
   }

   bool
   is_multiplier_instruction() const
   {
      switch (inst->opcode) {
      case GEN_OP_MUL:
      case GEN_OP_MAC:
      case GEN_OP_MACH:
      case GEN_OP_MACL:
      case GEN_OP_MAD:
         return true;
      default:
         return false;
      }
   }

   bool
   inst_uses_src_acc() const
   {
      switch (inst->opcode) {
      case GEN_OP_MAC:
      case GEN_OP_MACH:
      case GEN_OP_MACL:
         return true;
      default:
         break;
      }

      if (num_sources >= 1 && is_accumulator(inst->src[0]))
         return true;

      if (num_sources >= 2 && !is_null(inst->src[1]) && is_accumulator(inst->src[1]))
         return true;

      return false;
   }

   static bool
   types_are_mixed_float(gen_reg_type t0, gen_reg_type t1)
   {
      return (t0 == GEN_TYPE_F && t1 == GEN_TYPE_HF) ||
             (t1 == GEN_TYPE_F && t0 == GEN_TYPE_HF);
   }

   static gen_reg_type
   execution_type_for_type(gen_reg_type type)
   {
      switch (type) {
      case GEN_TYPE_DF:
      case GEN_TYPE_F:
      case GEN_TYPE_HF:
      case GEN_TYPE_BF8:
      case GEN_TYPE_HF8:
         return type;

      case GEN_TYPE_VF:
      case GEN_TYPE_BF:
         return GEN_TYPE_F;

      case GEN_TYPE_Q:
      case GEN_TYPE_UQ:
         return GEN_TYPE_Q;

      case GEN_TYPE_D:
      case GEN_TYPE_UD:
         return GEN_TYPE_D;

      case GEN_TYPE_W:
      case GEN_TYPE_UW:
      case GEN_TYPE_B:
      case GEN_TYPE_UB:
      case GEN_TYPE_V:
      case GEN_TYPE_UV:
         return GEN_TYPE_W;

      default:
         return GEN_TYPE_INVALID;
      }
   }

   gen_reg_type
   execution_type() const
   {
      const gen_reg_type dst_exec_type = inst->dst.type;
      const gen_reg_type src0_exec_type = execution_type_for_type(inst->src[0].type);

      if (num_sources == 1 || (num_sources > 1 && is_null(inst->src[1]))) {
         if (src0_exec_type == GEN_TYPE_HF)
            return dst_exec_type;
         return src0_exec_type;
      }

      const gen_reg_type src1_exec_type = execution_type_for_type(inst->src[1].type);

      if (types_are_mixed_float(src0_exec_type, src1_exec_type) ||
          types_are_mixed_float(src0_exec_type, dst_exec_type) ||
          types_are_mixed_float(src1_exec_type, dst_exec_type)) {
         return GEN_TYPE_F;
      }

      if (src0_exec_type == src1_exec_type)
         return src0_exec_type;

      if (src0_exec_type == GEN_TYPE_Q || src1_exec_type == GEN_TYPE_Q)
         return GEN_TYPE_Q;

      if (src0_exec_type == GEN_TYPE_D || src1_exec_type == GEN_TYPE_D)
         return GEN_TYPE_D;

      if (src0_exec_type == GEN_TYPE_W || src1_exec_type == GEN_TYPE_W)
         return GEN_TYPE_W;

      if (src0_exec_type == GEN_TYPE_DF || src1_exec_type == GEN_TYPE_DF)
         return GEN_TYPE_DF;

      UNREACHABLE("invalid execution type");
   }

   bool
   math_requires_src1() const
   {
      assert(inst->opcode == GEN_OP_MATH);

      switch (inst->math.func) {
      case GEN_MATH_POW:
      case GEN_MATH_FDIV:
      case GEN_MATH_INT_DIV_BOTH:
      case GEN_MATH_INT_DIV_QUOTIENT:
      case GEN_MATH_INT_DIV_REMAINDER:
      case GEN_MATH_INVM:
         return true;

      case GEN_MATH_INV:
      case GEN_MATH_LOG:
      case GEN_MATH_EXP:
      case GEN_MATH_SQRT:
      case GEN_MATH_RSQ:
      case GEN_MATH_SIN:
      case GEN_MATH_COS:
      case GEN_MATH_RSQRTM:
         return false;
      }

      return true;
   }

   void
   opcodes()
   {
      if (devinfo->ver != 9) {
         ERROR_IF(inst->opcode == GEN_OP_DP2,  "DP2 is Gfx9 only.");
         ERROR_IF(inst->opcode == GEN_OP_DP3,  "DP3 is Gfx9 only.");
         ERROR_IF(inst->opcode == GEN_OP_DP4,  "DP4 is Gfx9 only.");
         ERROR_IF(inst->opcode == GEN_OP_DPH,  "DPH is Gfx9 only.");
         ERROR_IF(inst->opcode == GEN_OP_LINE, "LINE is Gfx9 only.");
         ERROR_IF(inst->opcode == GEN_OP_LRP,  "LRP is Gfx9 only.");
         ERROR_IF(inst->opcode == GEN_OP_PLN,  "PLN is Gfx9 only.");
      }

      if (devinfo->ver != 9 && devinfo->ver != 11) {
         ERROR_IF(inst->opcode == GEN_OP_SENDS,  "SENDS is Gfx9 and Gfx11 only.");
         ERROR_IF(inst->opcode == GEN_OP_SENDSC, "SENDSC is Gfx9 and Gfx11 only.");
         ERROR_IF(inst->opcode == GEN_OP_WAIT,   "WAIT is Gfx9 and Gfx11 only.");
      }

      if (devinfo->ver < 11) {
         ERROR_IF(inst->opcode == GEN_OP_ROL, "ROL is Gfx11+ only.");
         ERROR_IF(inst->opcode == GEN_OP_ROR, "ROR is Gfx11+ only.");
      }

      if (devinfo->ver < 12) {
         ERROR_IF(inst->opcode == GEN_OP_ADD3, "ADD3 is Gfx12+ only.");
         ERROR_IF(inst->opcode == GEN_OP_BFN,  "BFN is Gfx12+ only.");
         ERROR_IF(inst->opcode == GEN_OP_DP4A, "DP4A is Gfx12+ only.");
         ERROR_IF(inst->opcode == GEN_OP_SYNC, "SYNC is Gfx12+ only.");
      }

      if (devinfo->verx10 < 125) {
         ERROR_IF(inst->opcode == GEN_OP_DPAS, "DPAS is Gfx12.5+ only.");
      }

      if (devinfo->ver < 20) {
         ERROR_IF(inst->opcode == GEN_OP_SRND, "SRND is Gfx20+ only.");
      }
   }

   void
   invalid_values()
   {
      if (format == GEN_FORMAT_NOP)
         return;

      const bool valid_exec_size =
         inst->exec_size == 1  ||
         inst->exec_size == 2  ||
         inst->exec_size == 4  ||
         inst->exec_size == 8  ||
         inst->exec_size == 16 ||
         inst->exec_size == 32;
      ERROR_IF(!valid_exec_size,
               "Execution size must be 1, 2, 4, 8, 16, or 32.");

      ERROR_IF(inst->align16 && devinfo->ver >= 11,
               "Align16 mode doesn't exist on Gfx11+.");

      if (devinfo->ver >= 20) {
         ERROR_IF(inst->chan_offset % 8 != 0,
                  "Channel offset must be a multiple of 8 for Gfx20+");
      } else {
         ERROR_IF(inst->chan_offset % 4 != 0,
                  "Channel offset must be a multiple of 4");
      }

      if (devinfo->ver >= 12) {
         ERROR_IF(inst->exec_size == 0 ||
                  inst->chan_offset % inst->exec_size != 0,
                  "The execution size must be a factor of the chosen offset");
      }

      if (inst->fusion_control) {
         ERROR_IF(devinfo->ver != 12,
                  "Fusion control bit only used for Gfx12.");
         ERROR_IF(devinfo->ver == 12 && !gen_inst_is_send(inst),
                  "Fusion control is only allowed on send instructions.");
      }

      ERROR_IF(inst->cmod != GEN_CONDITION_NONE &&
               devinfo->ver >= 12 &&
               format == GEN_FORMAT_BASIC_ONE_SRC &&
               inst->src[0].file == GEN_IMM &&
               gen_type_size_bytes(inst->src[0].type) >= 8,
               "Conditional modifier is not encodable for Xe basic 1-source instructions with a 64-bit immediate src0.");

      if (num_sources == 3 && devinfo->ver < 12) {
         if (devinfo->ver == 9) {
            ERROR_IF(!inst->align16,
                     "Gfx9 3-source instructions must use Align16 mode.");
         } else {
            ERROR_IF(inst->align16,
                     "Pre-Gfx12 3-source instructions must use Align1 mode except on Gfx9.");
         }
      }

      if (devinfo->ver >= 20) {
         ERROR_IF(inst->acc_wr_control,
                  "AccWrControl is not present on Gfx20+.");
      }

      /* TODO: Consider Large GRF for certain Xe platforms that support it. */
      const unsigned max_grf = devinfo->ver >= 20 ? 256 : 128;

      if (gen_inst_has_dst(inst->opcode)) {
         ERROR_IF(!inst->dst.indirect &&
                  inst->dst.file == GEN_GRF && inst->dst.nr >= max_grf,
                  "Destination GRF register number %u exceeds maximum %u.",
                  inst->dst.nr, max_grf - 1);
      }

      for (unsigned i = 0; i < num_sources; i++) {
         const gen_operand &src = inst->src[i];
         ERROR_IF(!src.indirect &&
                  src.file == GEN_GRF && src.nr >= max_grf,
                  "Source %u GRF register number %u exceeds maximum %u.",
                  i, src.nr, max_grf - 1);
      }
   }

   void
   branch_restrictions()
   {
      switch (inst->opcode) {
      case GEN_OP_JMPI:
      case GEN_OP_BRD:
         ERROR_IF(num_sources != 1,
                  "JMPI and BRD require exactly one source operand.");
         ERROR_IF(inst->src[0].file != GEN_GRF &&
                  inst->src[0].file != GEN_ARF &&
                  inst->src[0].file != GEN_IMM,
                  "JMPI and BRD source must be a register or immediate.");
         ERROR_IF(inst->src[1].file != GEN_BAD_FILE,
                  "JMPI and BRD use only src0 in the gen instruction model.");
         break;

      case GEN_OP_BRC:
         if (inst->src[0].file == GEN_GRF ||
             inst->src[0].file == GEN_ARF) {
            /* Register form: single GRF source. */
            ERROR_IF(inst->src[1].file != GEN_BAD_FILE,
                     "BRC register form uses only src0 in the gen instruction model.");
            ERROR_IF(!is_32bit_integer_operand(inst->src[0]),
                     "BRC register form requires a paired DWord integer source.");
            ERROR_IF(inst->src[0].region.vstride != 2 ||
                     inst->src[0].region.width != 2 ||
                     inst->src[0].region.hstride != 1,
                     "BRC register form requires src0 region <2;2,1>.");
         } else {
            /* Immediate form: two immediate sources. */
            ERROR_IF(inst->src[0].file != GEN_IMM || inst->src[1].file != GEN_IMM,
                     "BRC immediate form requires src0 and src1 to be immediates.");
            ERROR_IF(!is_32bit_integer_operand(inst->src[0]) ||
                     !is_32bit_integer_operand(inst->src[1]),
                     "BRC immediate form requires 32-bit integer immediates.");
         }
         break;

      default:
         break;
      }
   }

   void
   sources_not_null()
   {
      /* Nothing to test. 3-src instructions can only have GRF sources, and
       * there's no bit to control the file.
       */
      if (num_sources == 3)
         return;

      /* Nothing to test.  Split sends can only encode a file in sources that are
       * allowed to be NULL.
       */
      if (gen_inst_is_split_send(devinfo, inst))
         return;

      if (num_sources >= 1 && inst->opcode != GEN_OP_SYNC)
         ERROR_IF(is_null(inst->src[0]), "src0 is null");

      if (num_sources == 2) {
         const bool allow_null_src1 =
            (inst->opcode == GEN_OP_MATH && !math_requires_src1()) ||
            (format == GEN_FORMAT_BRANCH_TWO_SRC && inst->src[0].file != GEN_IMM);
         ERROR_IF(!allow_null_src1 && is_null(inst->src[1]), "src1 is null");
      }
   }

   void
   immediate_restrictions()
   {
      if (format != GEN_FORMAT_BASIC_THREE_SRC)
         return;

      const bool src0_is_imm = inst->src[0].file == GEN_IMM;
      const bool src1_is_imm = inst->src[1].file == GEN_IMM;
      const bool src2_is_imm = inst->src[2].file == GEN_IMM;
      const bool has_imm = src0_is_imm || src1_is_imm || src2_is_imm;

      ERROR_IF(src1_is_imm,
               "3-source instructions cannot use an immediate src1.");

      if (devinfo->ver == 9) {
         ERROR_IF(has_imm,
                  "Gfx9 3-source instructions cannot use immediate sources.");
      } else if (devinfo->ver == 11) {
         ERROR_IF((inst->opcode == GEN_OP_BFE ||
                   inst->opcode == GEN_OP_CSEL) && has_imm,
                  "BFE and CSEL cannot use immediate sources on Gfx11.");
         ERROR_IF((inst->opcode == GEN_OP_MAD ||
                   inst->opcode == GEN_OP_DP4A) &&
                  src0_is_imm && src2_is_imm,
                  "MAD and DP4A can have at most one immediate source on Gfx11.");
      }

      ERROR_IF(inst->opcode == GEN_OP_BFI2 && has_imm,
               "BFI2 cannot use immediate sources.");

      if (inst->opcode == GEN_OP_ADD3) {
         for (unsigned i = 0; i < 3; i++) {
            ERROR_IF(inst->src[i].file == GEN_IMM &&
                     inst->src[i].type != GEN_TYPE_W &&
                     inst->src[i].type != GEN_TYPE_UW,
                     "ADD3 immediate sources must be integer W or UW type.");
         }
      }
   }

   void
   basic_three_source_restrictions()
   {
      if (format != GEN_FORMAT_BASIC_THREE_SRC)
         return;

      ERROR_IF(inst->dst.indirect,
               "3-source destination must use direct addressing.");

      for (unsigned i = 0; i < 3; i++) {
         ERROR_IF(inst->src[i].indirect,
                  "3-source sources must use direct addressing.");
      }

      if (inst->align16) {
         ERROR_IF(inst->dst.file != GEN_GRF,
                  "Align16 3-source instructions require a GRF destination.");
         ERROR_IF(inst->dst.type != GEN_TYPE_F  &&
                  inst->dst.type != GEN_TYPE_DF &&
                  inst->dst.type != GEN_TYPE_D  &&
                  inst->dst.type != GEN_TYPE_UD &&
                  inst->dst.type != GEN_TYPE_HF,
                  "Align16 3-source destination type must be F, DF, D, UD, or HF.");
         ERROR_IF((inst->dst.subnr & 0x3) != 0 || inst->dst.subnr > 28,
                  "Align16 3-source destination subregister offset is not encodable.");

         for (unsigned i = 0; i < 3; i++) {
            ERROR_IF(inst->src[i].file != GEN_GRF,
                     "Align16 3-source instructions require GRF sources.");
            ERROR_IF((inst->src[i].subnr & ~0x1e) != 0,
                     "Align16 3-source source subregister offset is not encodable.");
         }

         return;
      }

      ERROR_IF(inst->dst.file != GEN_GRF &&
               !is_accumulator(inst->dst) &&
               !is_null(inst->dst),
               "Align1 3-source destination must be a GRF, accumulator, or null register.");
      ERROR_IF(inst->dst.region.hstride != 1 &&
               inst->dst.region.hstride != 2,
               "Align1 3-source destination horizontal stride must be 1 or 2.");
      ERROR_IF(devinfo->ver >= 20 ?
                  inst->dst.subnr > 62 || (inst->dst.subnr & 1) != 0 :
                  inst->dst.subnr > 31,
               "Align1 3-source destination subregister offset is not encodable.");

      for (unsigned i = 0; i < 3; i++) {
         const gen_operand &src = inst->src[i];

         ERROR_IF(gen_type_is_float_or_bfloat(inst->dst.type) !=
                  gen_type_is_float_or_bfloat(src.type),
                  "Align1 3-source source type must match the destination execution type.");

         if (src.file == GEN_IMM)
            continue;

         ERROR_IF(src.file != GEN_GRF && src.file != GEN_ARF,
                  "3-source register sources must use GRF or ARF files.");
         ERROR_IF(devinfo->ver >= 20 ?
                     src.subnr > 62 || (src.subnr & 1) != 0 :
                     src.subnr > 31,
                  "Align1 3-source source subregister offset is not encodable.");

         if (i < 2) {
            ERROR_IF(src.region.vstride != 0  &&
                     src.region.vstride != 1  &&
                     src.region.vstride != 2  &&
                     src.region.vstride != 4  &&
                     src.region.vstride != 8  &&
                     src.region.vstride != 16,
                     "Align1 3-source source vertical stride is not encodable.");
         }

         ERROR_IF(src.region.hstride != 0 &&
                  src.region.hstride != 1 &&
                  src.region.hstride != 2 &&
                  src.region.hstride != 4,
                  "Align1 3-source source horizontal stride must be 0, 1, 2, or 4.");
      }
   }

   void
   send_restrictions()
   {
      const bool scalar_gather =
         devinfo->ver >= 30 &&
         inst->src[0].file == GEN_ARF &&
         (inst->src[0].nr & 0xf0) == GEN_ARF_SCALAR;

      if (gen_inst_is_split_send(devinfo, inst)) {
         ERROR_IF(inst->src[0].indirect || inst->src[1].indirect,
                  "send must use direct addressing");
         ERROR_IF(!scalar_gather && inst->src[0].file != GEN_GRF,
                  "send from non-GRF");
         ERROR_IF(scalar_gather && (inst->src[0].subnr & 1),
                  "scalar gather send requires an even scalar subregister");
         ERROR_IF(inst->src[1].file == GEN_ARF &&
                  inst->src[1].nr != GEN_ARF_NULL,
                  "src1 of split send must be a GRF or NULL");

         if (devinfo->ver < 20) {
            ERROR_IF(inst->send.eot &&
                     inst->src[0].nr < 112,
                     "send with EOT must use g112-g127");
            ERROR_IF(inst->send.eot &&
                     inst->src[1].file == GEN_GRF &&
                     inst->src[1].nr < 112,
                     "send with EOT must use g112-g127");
         }

         if (inst->src[0].file == GEN_GRF && inst->src[1].file == GEN_GRF) {
            /* Assume minimums if we don't know. */
            const int src0_len = gen_inst_send_src0_len(inst);
            const int src1_len = gen_inst_send_src1_len(devinfo, inst);
            const unsigned mlen = src0_len >= 0 ? src0_len : 1;
            const unsigned ex_mlen = src1_len >= 0 ? src1_len : 1;
            const unsigned src0_reg_nr = inst->src[0].nr;
            const unsigned src1_reg_nr = inst->src[1].nr;

            ERROR_IF((src0_reg_nr <= src1_reg_nr &&
                      src1_reg_nr < src0_reg_nr + mlen) ||
                     (src1_reg_nr <= src0_reg_nr &&
                      src0_reg_nr < src1_reg_nr + ex_mlen),
                     "Split-send payloads must not overlap.");
         }
      } else if (gen_inst_is_send(inst)) {
         ERROR_IF(inst->src[0].indirect,
                  "send must use direct addressing");
         ERROR_IF(inst->src[0].file != GEN_GRF,
                  "send from non-GRF");
         ERROR_IF(inst->send.eot &&
                  inst->src[0].nr < 112,
                  "send with EOT must use g112-g127");

         if (devinfo->ver == 9) {
            const unsigned rlen = (inst->send.desc_imm >> 20) & 0x1F;
            const unsigned mlen = (inst->send.desc_imm >> 25) & 0xF;
            ERROR_IF(!is_null(inst->dst) &&
                     (inst->dst.nr + rlen > 127) &&
                     (inst->src[0].nr + mlen > inst->dst.nr),
                     "r127 must not be used for return address when there is "
                     "a src and dest overlap");
         }
      }
   }

   void
   general_restrictions_based_on_operand_types()
   {
      if (gen_inst_is_send(inst))
         return;

      if (devinfo->ver >= 11) {
         if (num_sources == 3 && inst->opcode != GEN_OP_DPAS) {
            ERROR_IF(gen_type_size_bytes(inst->src[1].type) == 1 ||
                     gen_type_size_bytes(inst->src[2].type) == 1,
                     "Byte data type is not supported for src1/2 regioning.");
         }

         if (num_sources == 2 && !is_null(inst->src[1]) &&
             inst->src[1].file != GEN_BAD_FILE) {
            ERROR_IF(gen_type_size_bytes(inst->src[1].type) == 1,
                     "Byte data type is not supported for src1 regioning.");
         }
      }

      if (devinfo->ver >= 20 && inst->opcode == GEN_OP_SRND) {
         const bool valid =
            inst->dst.type == GEN_TYPE_HF &&
            inst->src[0].type == GEN_TYPE_F &&
            inst->src[1].type == GEN_TYPE_F;
         ERROR_IF(!valid, "SRND requires dst=HF and src0/src1=F.");
      }

      if (!gen_inst_has_dst(inst->opcode))
         return;

      const gen_reg_type dst_type = inst->dst.type;

      ERROR_IF(gen_type_is_bfloat(dst_type) && !devinfo->has_bfloat16,
               "Bfloat destination requires bfloat16 support.");
      ERROR_IF(dst_type == GEN_TYPE_DF && !devinfo->has_64bit_float,
               "64-bit float destination requires 64-bit float support.");
      ERROR_IF((dst_type == GEN_TYPE_Q || dst_type == GEN_TYPE_UQ) &&
               !devinfo->has_64bit_int,
               "64-bit integer destination requires 64-bit integer support.");

      for (unsigned s = 0; s < num_sources; s++) {
         if (is_null(inst->src[s]))
            continue;

         const gen_reg_type src_type = inst->src[s].type;

         ERROR_IF(gen_type_is_bfloat(src_type) && !devinfo->has_bfloat16,
                  "Bfloat source requires bfloat16 support.");
         ERROR_IF(src_type == GEN_TYPE_DF && !devinfo->has_64bit_float,
                  "64-bit float source requires 64-bit float support.");
         ERROR_IF((src_type == GEN_TYPE_Q || src_type == GEN_TYPE_UQ) &&
                  !devinfo->has_64bit_int,
                  "64-bit integer source requires 64-bit integer support.");

         if (inst->align16 && num_sources == 3 && gen_type_size_bytes(src_type) > 4) {
            ERROR_IF(inst->src[s].rep_ctrl,
                     s == 0 ? "RepCtrl must be 0 for 64-bit src0." :
                     s == 1 ? "RepCtrl must be 0 for 64-bit src1." :
                              "RepCtrl must be 0 for 64-bit src2.");
         }
      }

      if (num_sources == 3 || inst->exec_size == 1)
         return;

      if (inst->opcode == GEN_OP_MATH && intel_needs_workaround(devinfo, 22016140776)) {
         ERROR_IF(inst->src[0].type == GEN_TYPE_HF &&
                  gen_region_is_scalar(inst->src[0].region),
                  "Scalar broadcast on HF MATH inputs is not supported on this platform.");

         if (num_sources > 1 && !is_null(inst->src[1])) {
            ERROR_IF(inst->src[1].type == GEN_TYPE_HF &&
                     gen_region_is_scalar(inst->src[1].region),
                     "Scalar broadcast on HF MATH inputs is not supported on this platform.");
         }
      }

      const unsigned dst_stride = inst->dst.region.hstride;
      const bool dst_type_is_byte =
         dst_type == GEN_TYPE_B || dst_type == GEN_TYPE_UB;

      if (dst_type_is_byte &&
          is_packed(inst->exec_size * dst_stride, inst->exec_size, dst_stride)) {
         ERROR_IF(!inst_is_raw_move(),
                  "Packed byte destinations are only allowed for raw MOV.");
      }

      if (is_byte_conversion()) {
         const gen_reg_type src0_type = inst->src[0].type;
         const bool has_src1 = num_sources > 1 && !is_null(inst->src[1]);
         const gen_reg_type src1_type = has_src1 ? inst->src[1].type : GEN_TYPE_INVALID;

         ERROR_IF(gen_type_size_bytes(dst_type) == 1 &&
                  (gen_type_size_bytes(src0_type) == 8 ||
                   (has_src1 && gen_type_size_bytes(src1_type) == 8)),
                  "There are no direct conversions between 64-bit types and B/UB.");

         ERROR_IF(gen_type_size_bytes(dst_type) == 8 &&
                  (gen_type_size_bytes(src0_type) == 1 ||
                   (has_src1 && gen_type_size_bytes(src1_type) == 1)),
                  "There are no direct conversions between 64-bit types and B/UB.");
      }

      if (is_half_float_conversion()) {
         const gen_reg_type src0_type = inst->src[0].type;
         const bool has_src1 = num_sources > 1 && !is_null(inst->src[1]);
         const gen_reg_type src1_type = has_src1 ? inst->src[1].type : GEN_TYPE_INVALID;

         ERROR_IF(dst_type == GEN_TYPE_HF &&
                  (gen_type_size_bytes(src0_type) == 8 ||
                   (has_src1 && gen_type_size_bytes(src1_type) == 8)),
                  "There are no direct conversions between 64-bit types and HF.");

         ERROR_IF(gen_type_size_bytes(dst_type) == 8 &&
                  (src0_type == GEN_TYPE_HF ||
                   (has_src1 && src1_type == GEN_TYPE_HF)),
                  "There are no direct conversions between 64-bit types and HF.");

         if (!inst->align16) {
            const bool int_to_hf =
               dst_type == GEN_TYPE_HF &&
               (gen_type_is_int(src0_type) ||
                (has_src1 && gen_type_is_int(src1_type)));
            const bool hf_to_int =
               gen_type_is_int(dst_type) &&
               (src0_type == GEN_TYPE_HF ||
                (has_src1 && src1_type == GEN_TYPE_HF));

            if (int_to_hf || hf_to_int) {
               ERROR_IF(dst_stride * gen_type_size_bytes(dst_type) != 4,
                        "Conversions between integer and HF must be strided by a DWord on the destination.");
               ERROR_IF(inst->dst.subnr % 4 != 0,
                        "Conversions between integer and HF must be DWord-aligned on the destination.");
            } else if (dst_type == GEN_TYPE_HF) {
               ERROR_IF(dst_stride != 2 &&
                        !(is_mixed_float() &&
                          dst_stride == 1 &&
                          inst->dst.subnr % 16 == 0),
                        "Conversions to HF must use even/odd word placement or an OWord-aligned packed mixed-float destination.");
            }
         }
      }

      const gen_reg_type exec_type = execution_type();
      const unsigned exec_type_size = gen_type_size_bytes(exec_type);
      const unsigned dst_type_size = gen_type_size_bytes(dst_type);
      const bool validate_dst_size_and_exec_size_ratio =
         !is_mixed_float() && !is_mixed_bfloat();

      if (validate_dst_size_and_exec_size_ratio && exec_type_size > dst_type_size) {
         if (!(dst_type_is_byte && inst_is_raw_move())) {
            ERROR_IF(dst_stride * dst_type_size != exec_type_size,
                     "Destination stride must match the ratio of execution type size to destination type size.");
         }

         if (!inst->align16 && !inst->dst.indirect) {
            const unsigned subreg = inst->dst.subnr;

            if (dst_type_is_byte) {
               ERROR_IF(subreg % exec_type_size != 0 &&
                        subreg % exec_type_size != 1,
                        "Destination subregister must align to the execution type size (or the next-lowest byte for byte destinations).");
            } else {
               ERROR_IF(subreg % exec_type_size != 0,
                        "Destination subregister must align to the execution type size.");
            }
         }
      }
   }

   void
   general_restrictions_on_region_parameters()
   {
      if (!uses_basic_regions(format) || num_sources == 0)
         return;

      if (inst->align16) {
         if (gen_inst_has_dst(inst->opcode) && !is_null(inst->dst)) {
            ERROR_IF(inst->dst.region.hstride != 1,
                     "In Align16 mode, destination horizontal stride must be 1.");
         }

         for (unsigned i = 0; i < num_sources; i++) {
            if (inst->src[i].file == GEN_IMM || is_null(inst->src[i]))
               continue;

            ERROR_IF(inst->src[i].region.vstride != 0 &&
                     inst->src[i].region.vstride != 2 &&
                     inst->src[i].region.vstride != 4,
                     "In Align16 mode, source vertical stride must be 0, 2, or 4.");
         }

         return;
      }

      for (unsigned i = 0; i < num_sources; i++) {
         if (inst->src[i].file == GEN_IMM || is_null(inst->src[i]))
            continue;

         const unsigned element_size = gen_type_size_bytes(inst->src[i].type);
         const unsigned subreg = inst->src[i].subnr;
         const unsigned vstride = inst->src[i].region.vstride;
         const unsigned width = inst->src[i].region.width;
         const unsigned hstride = inst->src[i].region.hstride;

         ERROR_IF(inst->exec_size < width,
                  "ExecSize must be greater than or equal to Width.");

         if (width == 1) {
            ERROR_IF(hstride != 0,
                     "If Width is 1, HorzStride must be 0.");
         }

         if (vstride != GEN_VSTRIDE_ONE_DIMENSIONAL) {
            if (inst->exec_size == width && hstride != 0) {
               ERROR_IF(vstride != width * hstride,
                        "If ExecSize equals Width and HorzStride is not 0, VertStride must equal Width * HorzStride.");
            }

            if (inst->exec_size == 1 && width == 1) {
               ERROR_IF(vstride != 0 || hstride != 0,
                        "If ExecSize is 1 and Width is 1, both VertStride and HorzStride must be 0.");
            }

            if (vstride == 0 && hstride == 0) {
               ERROR_IF(width != 1,
                        "If VertStride and HorzStride are 0, Width must be 1.");
            }
         }

         if (inst->src[i].file == GEN_GRF) {
            unsigned rowbase = subreg;

            for (unsigned y = 0; y < inst->exec_size / MAX2(width, 1u); y++) {
               bool spans_grfs = false;
               unsigned offset = rowbase;
               const unsigned first_grf = offset / devinfo->grf_size;

               for (unsigned x = 0; x < width; x++) {
                  const unsigned end_byte = offset + element_size - 1;
                  const unsigned end_grf = end_byte / devinfo->grf_size;
                  spans_grfs = end_grf != first_grf;
                  if (spans_grfs)
                     break;
                  offset += hstride * element_size;
               }

               rowbase += vstride * element_size;

               if (spans_grfs) {
                  ERROR("VertStride must be used to cross GRF register boundaries.");
                  break;
               }
            }
         }
      }

      if (gen_inst_has_dst(inst->opcode) && !is_null(inst->dst)) {
         ERROR_IF(inst->dst.region.hstride == 0,
                  "Destination horizontal stride must not be 0.");
      }
   }

   void
   special_restrictions_for_mixed_float_mode()
   {
      if (inst->opcode == GEN_OP_DPAS || inst->opcode == GEN_OP_SRND)
         return;

      ERROR_IF(is_pure_bfloat(),
               "Instructions with pure bfloat operands are not supported.");

      if (is_mixed_bfloat()) {
         ERROR_IF(devinfo->ver < 20 && inst->exec_size > 8,
                  "Mixed bfloat mode is limited to SIMD8 before Gfx20.");
         ERROR_IF(devinfo->ver >= 20 && inst->exec_size > 16,
                  "Mixed bfloat mode is limited to SIMD16 on Gfx20+.");

         for (unsigned i = 0; i < num_sources; i++) {
            ERROR_IF(gen_type_is_bfloat(inst->src[i].type) &&
                     gen_region_is_scalar(inst->src[i].region),
                     "Broadcast of bfloat scalar is not supported.");
         }

         if (is_multiplier_instruction()) {
            if (num_sources == 2) {
               ERROR_IF(gen_type_is_bfloat(inst->src[1].type),
                        "Bfloat is not allowed in src1 of 2-source multiplier instructions.");
            } else if (num_sources == 3) {
               ERROR_IF(gen_type_is_bfloat(inst->src[2].type),
                        "Bfloat is not allowed in src2 of 3-source multiplier instructions.");
            }
         }

         const unsigned half_offset = devinfo->grf_size / 2;

         if (gen_inst_has_dst(inst->opcode) && gen_type_is_bfloat(inst->dst.type)) {
            const unsigned dst_stride = inst->dst.region.hstride;
            const bool dst_is_packed =
               is_packed(inst->exec_size * dst_stride, inst->exec_size, dst_stride);

            if (dst_is_packed) {
               ERROR_IF(inst->dst.subnr != 0 && inst->dst.subnr != half_offset,
                        "Packed bfloat destination must have register offset 0 or half a GRF.");
            } else {
               const unsigned elem_size = gen_type_size_bytes(inst->dst.type);
               ERROR_IF(dst_stride != 2 ||
                        (inst->dst.subnr != 0 && inst->dst.subnr != elem_size),
                        "Unpacked bfloat destination must have stride 2 and register offset 0 or 1 element.");
            }
         }

         for (unsigned i = 0; i < num_sources; i++) {
            if (!gen_type_is_bfloat(inst->src[i].type))
               continue;

            ERROR_IF(!is_packed(inst->src[i].region.vstride,
                                inst->src[i].region.width,
                                inst->src[i].region.hstride),
                     "Bfloat sources must be packed.");
            ERROR_IF(inst->src[i].subnr != 0 && inst->src[i].subnr != half_offset,
                     "Bfloat sources must have register offset 0 or half a GRF.");
         }
      }

      if (num_sources >= 3)
         return;

      if (!is_mixed_float())
         return;

      const gen_reg_type src0_type = inst->src[0].type;
      const gen_reg_type src1_type = num_sources > 1 ? inst->src[1].type : GEN_TYPE_INVALID;
      const gen_reg_type dst_type = inst->dst.type;
      const unsigned dst_stride = inst->dst.region.hstride;
      const bool dst_is_packed =
         is_packed(inst->exec_size * dst_stride, inst->exec_size, dst_stride);

      ERROR_IF(inst->src[0].indirect ||
               (num_sources > 1 && !is_null(inst->src[1]) && inst->src[1].indirect),
               "Indirect addressing on source is not supported in mixed float mode.");

      ERROR_IF(inst->exec_size > 8 && devinfo->ver < 20 &&
               dst_type == GEN_TYPE_F && inst->opcode != GEN_OP_MOV,
               "Mixed float mode with 32-bit float destination is limited to SIMD8.");

      if (inst->align16) {
         ERROR_IF(inst->src[0].file != GEN_IMM && inst->src[0].region.vstride != 4,
                  "Align16 mixed float mode assumes packed data for src0 (vstride must be 4).");

         if (num_sources >= 2 && !is_null(inst->src[1]) && inst->src[1].file != GEN_IMM) {
            ERROR_IF(inst->src[1].region.vstride != 4,
                     "Align16 mixed float mode assumes packed data for src1 (vstride must be 4).");
         }

         ERROR_IF(inst->exec_size > 8,
                  "Align16 mixed float mode is limited to SIMD8.");

         ERROR_IF(inst_uses_src_acc(),
                  "Align16 mixed float mode does not allow accumulator reads.");
      } else {
         ERROR_IF(inst->exec_size > 8 && dst_is_packed && dst_type == GEN_TYPE_HF &&
                  inst->opcode != GEN_OP_MOV,
                  "Align1 mixed float mode is limited to SIMD8 when destination is packed half-float.");

         if (inst->opcode == GEN_OP_MATH) {
            if (src0_type == GEN_TYPE_HF) {
               ERROR_IF(inst->src[0].region.hstride <= 1,
                        "Align1 mixed-float MATH requires strided half-float src0 inputs.");
            }

            if (num_sources >= 2 && !is_null(inst->src[1]) && src1_type == GEN_TYPE_HF) {
               ERROR_IF(inst->src[1].region.hstride <= 1,
                        "Align1 mixed-float MATH requires strided half-float src1 inputs.");
            }
         }

         if (dst_type == GEN_TYPE_HF && dst_stride == 1) {
            ERROR_IF(inst->dst.subnr % 16 != 0,
                     "Align1 mixed-float packed half-float destinations must be OWord-aligned.");
            ERROR_IF(inst->exec_size > 8,
                     "Align1 mixed-float packed half-float destinations must not cross OWord boundaries.");

            if (is_accumulator(inst->src[0]) &&
                (src0_type == GEN_TYPE_F || src0_type == GEN_TYPE_HF)) {
               ERROR_IF(inst->src[0].subnr != 0,
                        "Mixed-float accumulator reads into packed half-float destinations require src0 subnr 0.");
            }

            if (num_sources > 1 && is_accumulator(inst->src[1]) &&
                (src1_type == GEN_TYPE_F || src1_type == GEN_TYPE_HF)) {
               ERROR_IF(inst->src[1].subnr != 0,
                        "Mixed-float accumulator reads into packed half-float destinations require src1 subnr 0.");
            }
         }

         if (dst_type == GEN_TYPE_HF && inst_uses_src_acc()) {
            ERROR_IF(dst_stride != 2,
                     "Mixed-float operations with accumulator sources and half-float destinations require destination stride 2.");
         }
      }
   }

   void
   region_alignment_rules()
   {
      if (!uses_basic_regions(format) || num_sources == 0 || inst->exec_size == 0)
         return;

      if (inst->align16 || gen_inst_is_send(inst))
         return;

      uint8_t dst_access_mask[32] = {};
      bool skip_detailed_grf_checks = false;

      for (unsigned i = 0; i < num_sources; i++) {
         const gen_operand &src = inst->src[i];

         if (src.file != GEN_GRF || src.indirect || is_null(src) || src.region.width == 0)
            continue;

         const unsigned element_size = gen_type_size_bytes(src.type);
         const unsigned subreg = src.subnr;
         const unsigned vstride = src.region.vstride;
         const unsigned width = src.region.width;
         const unsigned hstride = src.region.hstride;

         const unsigned num_vstride = inst->exec_size / width;
         const unsigned num_hstride = width;
         const unsigned vstride_elements = (num_vstride - 1) * vstride;
         const unsigned hstride_elements = (num_hstride - 1) * hstride;
         const unsigned offset = (vstride_elements + hstride_elements) * element_size +
                                 subreg;

         if (offset >= 2 * devinfo->grf_size) {
            ERROR("A source cannot span more than 2 adjacent GRF registers.");
            skip_detailed_grf_checks = true;
         }
      }

      if (!gen_inst_has_dst(inst->opcode) || is_null(inst->dst) ||
          inst->dst.file != GEN_GRF || inst->dst.indirect)
         return;

      const unsigned stride = inst->dst.region.hstride;
      const unsigned element_size = gen_type_size_bytes(inst->dst.type);
      const unsigned subreg = inst->dst.subnr;
      const unsigned offset = ((inst->exec_size - 1) * stride * element_size) + subreg;
      if (offset >= 2 * devinfo->grf_size) {
         ERROR("A destination cannot span more than 2 adjacent GRF registers.");
         skip_detailed_grf_checks = true;
      }

      if (skip_detailed_grf_checks)
         return;

      grfs_accessed(dst_access_mask, inst->exec_size, element_size, subreg,
                    inst->exec_size == 1 ? 0 : inst->exec_size * stride,
                    inst->exec_size == 1 ? 1 : inst->exec_size,
                    inst->exec_size == 1 ? 0 : stride);

      if (inst->opcode == GEN_OP_MATH && registers_read(dst_access_mask) == 2) {
         unsigned upper_reg_writes = 0, lower_reg_writes = 0;

         for (unsigned i = 0; i < inst->exec_size; i++) {
            if (dst_access_mask[i] == 0x2) {
               upper_reg_writes++;
            } else {
               assert(dst_access_mask[i] == 0x1);
               lower_reg_writes++;
            }
         }

         ERROR_IF(upper_reg_writes != lower_reg_writes,
                  "MATH writes must be evenly split between the two destination registers.");
      }
   }

   void
   vector_immediate_restrictions()
   {
      if (!uses_basic_regions(format))
         return;

      for (unsigned i = 0; i < num_sources; i++) {
         if (inst->src[i].file != GEN_IMM ||
             !is_vector_immediate_type(inst->src[i].type))
            continue;

         const gen_reg_type dst_type = inst->dst.type;
         const unsigned dst_type_size = gen_type_size_bytes(dst_type);
         const unsigned dst_subreg = inst->dst.subnr;
         const unsigned dst_stride = inst->dst.region.hstride;

         ERROR_IF(dst_subreg % 16 != 0,
                  "Destination must be 128-bit aligned for vector immediate types.");

         if (inst->src[i].type == GEN_TYPE_VF) {
            ERROR_IF(dst_type_size * dst_stride != 4,
                     "Destination stride must be dword-equivalent for VF immediate types.");
         } else {
            ERROR_IF(dst_type_size * dst_stride != 2,
                     "Destination stride must be word-equivalent for V/UV immediate types.");
         }
      }
   }

   void
   special_requirements_for_handling_double_precision_data_types()
   {
      if (num_sources == 0 || num_sources == 3)
         return;

      if (gen_inst_is_split_send(devinfo, inst))
         return;

      if (!gen_inst_has_dst(inst->opcode))
         return;

      const gen_reg_type exec_type = execution_type();
      const unsigned exec_type_size = gen_type_size_bytes(exec_type);

      const gen_reg_type dst_type = inst->dst.type;
      const unsigned dst_type_size = gen_type_size_bytes(dst_type);
      const unsigned dst_hstride = inst->dst.region.hstride;
      const unsigned dst_reg = inst->dst.nr;
      const unsigned dst_subreg = inst->dst.subnr;
      const bool dst_indirect = inst->dst.indirect;

      const bool is_integer_dword_multiply =
         inst->opcode == GEN_OP_MUL &&
         (inst->src[0].type == GEN_TYPE_D || inst->src[0].type == GEN_TYPE_UD) &&
         (inst->src[1].type == GEN_TYPE_D || inst->src[1].type == GEN_TYPE_UD);

      const bool is_double_precision =
         dst_type_size == 8 || exec_type_size == 8 || is_integer_dword_multiply;

      for (unsigned i = 0; i < num_sources; i++) {
         const gen_operand &src = inst->src[i];
         if (src.file == GEN_IMM || is_null(src))
            continue;

         const gen_reg_type type = src.type;
         const unsigned type_size = gen_type_size_bytes(type);
         const bool src_indirect = src.indirect;
         const unsigned reg = src.nr;
         const unsigned subreg = src.subnr;
         const bool is_scalar_region = gen_region_is_scalar(src.region);
         const unsigned vstride = src.region.vstride;
         const unsigned width = src.region.width;
         const unsigned hstride = src.region.hstride;

         const unsigned src_stride = (hstride ? hstride : vstride) * type_size;
         const unsigned dst_stride = dst_hstride * dst_type_size;

         if (is_double_precision && !inst->align16 && intel_device_info_is_9lp(devinfo)) {
            ERROR_IF(!is_scalar_region &&
                     (src_stride % 8 != 0 ||
                      dst_stride % 8 != 0 ||
                      src_stride != dst_stride),
                     "64-bit and integer DW-multiply Align1 regions require matching qword-aligned source and destination strides.");

            ERROR_IF(vstride != width * hstride,
                     "64-bit and integer DW-multiply Align1 regions require vstride = width * hstride.");

            ERROR_IF(!is_scalar_region && dst_subreg != subreg,
                     "64-bit and integer DW-multiply Align1 regions require matching source and destination subregister offsets.");
         }

         if (is_double_precision && intel_device_info_is_9lp(devinfo)) {
            ERROR_IF(src_indirect || dst_indirect,
                     "Indirect addressing is not allowed for 64-bit and integer DW-multiply operations on Gfx9 LP.");
         }

         if (is_double_precision && intel_device_info_is_9lp(devinfo)) {
            ERROR_IF(inst->opcode == GEN_OP_MAC ||
                     inst->acc_wr_control ||
                     (src.file == GEN_ARF && reg != GEN_ARF_NULL) ||
                     (inst->dst.file == GEN_ARF && dst_reg != GEN_ARF_NULL),
                     "Architecture registers cannot be used for 64-bit and integer DW-multiply operations on Gfx9 LP.");
         }

         if (devinfo->verx10 >= 125 &&
             (gen_type_is_float(dst_type) || is_double_precision)) {
            ERROR_IF(!gen_type_is_bfloat(type) &&
                     !is_scalar_region &&
                     !src_indirect &&
                     (!gen_region_is_linear(src.region) ||
                      src_stride != dst_stride ||
                      subreg != dst_subreg),
                     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.");

            ERROR_IF((!src_indirect && src.file == GEN_ARF &&
                      reg != GEN_ARF_SCALAR &&
                      reg != GEN_ARF_NULL &&
                      !is_accumulator(src)) ||
                     (inst->dst.file == GEN_ARF &&
                      dst_reg != GEN_ARF_SCALAR &&
                      dst_reg != GEN_ARF_NULL &&
                      !is_accumulator(inst->dst)),
                     "Explicit ARF operands must be null, accumulator, or scalar for float and 64-bit regioning restrictions.");
         }

         if (devinfo->verx10 >= 125 &&
             (gen_type_is_float_or_bfloat(type) || type_size == 8)) {
            ERROR_IF(src_indirect && vstride == GEN_VSTRIDE_ONE_DIMENSIONAL,
                     "Vx1 and VxH indirect addressing are not allowed for float, bfloat, and 64-bit source types.");
         }
      }

      if (is_double_precision) {
         const gen_reg_type src0_type = inst->src[0].type;
         const gen_reg_type src1_type =
            num_sources > 1 && !is_null(inst->src[1]) ? inst->src[1].type : src0_type;
         const unsigned src0_type_size = gen_type_size_bytes(src0_type);
         const unsigned src1_type_size = gen_type_size_bytes(src1_type);

         ERROR_IF(inst->align16 &&
                  dst_type_size == 8 &&
                  (src0_type_size != 8 || src1_type_size != 8) &&
                  inst->exec_size > 2,
                  "Align16 with a 64-bit destination and a non-64-bit source requires exec_size <= 2.");
      }

      if (is_double_precision && intel_device_info_is_9lp(devinfo)) {
         ERROR_IF(inst->no_dd_check || inst->no_dd_clear,
                  "DepCtrl is not allowed for 64-bit and integer DW-multiply operations on Gfx9 LP.");
      }
   }

   static bool
   message_desc_header_present(uint32_t desc)
   {
      return GET_BITS(desc, 19, 19);
   }

   static unsigned
   message_desc_response_length(uint32_t desc)
   {
      return GET_BITS(desc, 24, 20);
   }

   static unsigned
   urb_desc_msg_type(uint32_t desc)
   {
      return GET_BITS(desc, 3, 0);
   }

   void
   instruction_restrictions()
   {
      if (inst->opcode == GEN_OP_MUL) {
         const gen_reg_type src0_type = inst->src[0].type;
         const gen_reg_type src1_type = inst->src[1].type;
         const gen_reg_type dst_type = inst->dst.type;

         if (devinfo->ver >= 12) {
            const gen_reg_type exec_type = execution_type();
            const bool src0_valid =
               gen_type_size_bytes(src0_type) == 4 ||
               inst->src[0].file == GEN_IMM ||
               !(inst->src[0].negate || inst->src[0].abs);
            const bool src1_valid =
               gen_type_size_bytes(src1_type) == 4 ||
               inst->src[1].file == GEN_IMM ||
               !(inst->src[1].negate || inst->src[1].abs);

            ERROR_IF(!gen_type_is_float(exec_type) &&
                     gen_type_size_bytes(exec_type) == 4 &&
                     !(src0_valid && src1_valid),
                     "When multiplying a DWord and a lower-precision integer, source modifiers are not supported.");
         }

         ERROR_IF(gen_type_is_float(dst_type) &&
                  (gen_type_is_int(src0_type) ||
                   gen_type_is_int(src1_type)),
                  "MUL cannot mix floating-point destination types with integer sources.");

         ERROR_IF((gen_type_is_int(src0_type) && is_accumulator(inst->src[0])) ||
                  (gen_type_is_int(src1_type) && is_accumulator(inst->src[1])),
                  "MUL integer sources cannot use accumulator registers.");

         ERROR_IF(gen_type_is_int(src1_type) &&
                  gen_type_size_bytes(src0_type) < 4 &&
                  gen_type_size_bytes(src1_type) == 4,
                  "When multiplying a DWord and a lower-precision integer, the DWord operand must be src0.");

         ERROR_IF((src0_type == GEN_TYPE_UD ||
                   src0_type == GEN_TYPE_D ||
                   src1_type == GEN_TYPE_UD ||
                   src1_type == GEN_TYPE_D) &&
                  (dst_type == GEN_TYPE_UD ||
                   dst_type == GEN_TYPE_D ||
                   dst_type == GEN_TYPE_UW ||
                   dst_type == GEN_TYPE_W) &&
                  (inst->saturate || inst->cmod != GEN_CONDITION_NONE),
                  "DWord integer MUL with W/UW/D/UD destinations cannot use saturate or conditional modifiers.");
      }

      if (inst->opcode == GEN_OP_CMP || inst->opcode == GEN_OP_CMPN) {
         ERROR_IF(inst->cmod == GEN_CONDITION_NONE,
                  "CMP and CMPN must have a condition modifier.");
      }

      if (inst->opcode == GEN_OP_SEL) {
         ERROR_IF((inst->cmod != GEN_CONDITION_NONE) ==
                  (inst->pred_control != GEN_PREDICATE_NONE),
                  "SEL must be predicated or use a condition modifier, but not both.");
      }

      if (inst->opcode == GEN_OP_CSEL) {
         ERROR_IF(inst->pred_control != GEN_PREDICATE_NONE,
                  "CSEL cannot be predicated.");
         ERROR_IF(inst->cmod == GEN_CONDITION_NONE,
                  "CSEL must have a condition modifier.");
         ERROR_IF(!is_csel_type(inst->dst.type),
                  devinfo->ver == 9 ?
                     "CSEL destination must be F on Gfx9." :
                     "CSEL destination must be F, HF, D, UD, W, or UW.");

         for (unsigned i = 0; i < 3; i++) {
            const gen_reg_type src_type = inst->src[i].type;

            ERROR_IF(!is_csel_type(src_type),
                     devinfo->ver == 9 ?
                        "CSEL sources must be F on Gfx9." :
                        "CSEL sources must be F, HF, D, UD, W, or UW.");

            if (devinfo->ver != 9) {
               ERROR_IF(gen_type_is_float(src_type) != gen_type_is_float(inst->dst.type),
                        "CSEL cannot mix floating-point and integer types.");
               ERROR_IF(gen_type_size_bytes(src_type) !=
                        gen_type_size_bytes(inst->dst.type),
                        "CSEL cannot mix operand sizes.");
            }
         }
      }

      if (inst->opcode == GEN_OP_MATH) {
         switch (inst->math.func) {
         case GEN_MATH_INT_DIV_BOTH:
         case GEN_MATH_INT_DIV_QUOTIENT:
         case GEN_MATH_INT_DIV_REMAINDER:
            ERROR_IF(devinfo->verx10 >= 125,
                     "MATH integer divide functions are not supported on Gfx12.5+.");
            ERROR_IF(inst->src[0].negate || inst->src[0].abs ||
                     inst->src[1].negate || inst->src[1].abs,
                     "MATH integer divide functions do not support source modifiers.");
            ERROR_IF(inst->src[0].type != GEN_TYPE_D && inst->src[0].type != GEN_TYPE_UD,
                     "MATH integer divide functions require D or UD source types.");
            ERROR_IF(inst->src[0].type != inst->src[1].type ||
                     inst->src[0].type != inst->dst.type,
                     "MATH integer divide functions require matching operand types.");
            break;

         default: {
            const bool ieee_macro =
               inst->math.func == GEN_MATH_INVM ||
               inst->math.func == GEN_MATH_RSQRTM;
            const bool two_srcs =
               inst->math.func == GEN_MATH_INVM ||
               inst->math.func == GEN_MATH_POW ||
               inst->math.func == GEN_MATH_FDIV;

            ERROR_IF(devinfo->verx10 >= 125 &&
                     (inst->math.func == GEN_MATH_POW ||
                      inst->math.func == GEN_MATH_FDIV),
                     "MATH POW and FDIV are not supported on Gfx12.5+.");

            if (ieee_macro && devinfo->verx10 >= 125) {
               ERROR_IF(inst->src[0].type != GEN_TYPE_F &&
                        inst->src[0].type != GEN_TYPE_HF &&
                        inst->src[0].type != GEN_TYPE_DF,
                        "Gfx12.5+ IEEE macro MATH requires src0 type F, HF, or DF.");
            } else {
               ERROR_IF(inst->src[0].type != GEN_TYPE_F &&
                        inst->src[0].type != GEN_TYPE_HF,
                        "MATH src0 type must be F or HF.");
            }

            if (devinfo->verx10 >= 125) {
               ERROR_IF(inst->src[0].type != inst->dst.type,
                        "On Gfx12.5+, MATH src0 and dst types must match.");
               ERROR_IF(two_srcs && inst->src[0].type != inst->src[1].type,
                        "On Gfx12.5+, 2-source MATH requires both source types to match.");
            } else {
               ERROR_IF(inst->dst.type != GEN_TYPE_F &&
                        inst->dst.type != GEN_TYPE_HF,
                        "Before Gfx12.5, MATH dst type must be F or HF.");
               ERROR_IF(two_srcs &&
                        inst->src[1].type != GEN_TYPE_F &&
                        inst->src[1].type != GEN_TYPE_HF,
                        "Before Gfx12.5, 2-source MATH src1 type must be F or HF.");
            }

            ERROR_IF(inst->dst.file != GEN_GRF,
                     "MATH must use a GRF destination.");

            ERROR_IF((devinfo->ver >= 20 || !ieee_macro) &&
                     (is_accumulator(inst->src[0]) ||
                      (two_srcs && is_accumulator(inst->src[1]))),
                     "Accumulator reads are only allowed for IEEE macro MATH before Gfx20.");
            break;
         }
         }
      }

      if (inst->opcode == GEN_OP_DP4A) {
         ERROR_IF(is_accumulator(inst->src[0]) && is_accumulator(inst->src[1]),
                  "At most one of DP4A src0 and src1 may be an accumulator.");
      }

      if (inst->opcode == GEN_OP_BFN) {
         ERROR_IF(inst->cmod != GEN_CONDITION_NONE &&
                  inst->cmod != GEN_CONDITION_ZE &&
                  inst->cmod != GEN_CONDITION_GT &&
                  inst->cmod != GEN_CONDITION_LT,
                  "BFN supports only ZE, GT, LT or none conditional modifiers.");
         ERROR_IF(inst->saturate,
                  "BFN cannot use saturate.");

         for (unsigned i = 0; i < 3; i++) {
            ERROR_IF(inst->src[i].type != GEN_TYPE_UD &&
                     inst->src[i].type != GEN_TYPE_UW,
                     "BFN sources must be UD or UW type.");
            ERROR_IF(inst->src[i].abs || inst->src[i].negate,
                     "BFN does not support source modifiers.");
         }
      }

      if (inst->opcode == GEN_OP_DPAS) {
         ERROR_IF(inst->src[0].file != GEN_GRF && inst->src[0].file != GEN_ARF,
                  "DPAS currently only supports GRF or GEN_ARF for Source 0.");
         ERROR_IF(inst->src[1].file != GEN_GRF,
                  "DPAS currently only supports GRF for Source 1");
         ERROR_IF(inst->src[2].file != GEN_GRF,
                  "DPAS currently only supports GRF for Source 2");

         const gen_reg_type dst_type = inst->dst.type;
         const gen_reg_type src0_type = inst->src[0].type;
         const gen_reg_type src1_type = inst->src[1].type;
         const gen_reg_type src2_type = inst->src[2].type;

         const unsigned src1_subbyte = inst->dpas.src1_subbyte;
         const unsigned src2_subbyte = inst->dpas.src2_subbyte;

         ERROR_IF(inst->dpas.sdepth != 8,
                  "DPAS systolic depth must be 8.");

         if (devinfo->ver < 20) {
            ERROR_IF(inst->exec_size != 8,
                     "DPAS execution size must be 8 before Gfx20.");
         } else {
            ERROR_IF(inst->exec_size != 16,
                     "DPAS execution size must be 16 on Gfx20+.");
         }

         if (src1_type != GEN_TYPE_B && src1_type != GEN_TYPE_UB) {
            ERROR_IF(src1_subbyte != 0,
                     "DPAS src1 sub-byte precision must be none for source types wider than byte.");
         } else {
            ERROR_IF(src1_subbyte > 2,
                     "DPAS src1 sub-byte precision is invalid.");
         }

         if (src2_type != GEN_TYPE_B && src2_type != GEN_TYPE_UB) {
            ERROR_IF(src2_subbyte != 0,
                     "DPAS src2 sub-byte precision must be none for source types wider than byte.");
         } else {
            ERROR_IF(src2_subbyte > 2,
                     "DPAS src2 sub-byte precision is invalid.");
         }

         const unsigned src1_bits_per_element =
            src1_subbyte > 2 ? 1u : MAX2(1u, gen_type_size_bits(src1_type) >> src1_subbyte);
         const unsigned src2_bits_per_element =
            src2_subbyte > 2 ? 1u : MAX2(1u, gen_type_size_bits(src2_type) >> src2_subbyte);
         const unsigned ops_per_chan =
            MAX2(1u, 32u / MAX2(src1_bits_per_element, src2_bits_per_element));

         const unsigned dst_align = inst->exec_size * gen_type_size_bytes(dst_type);
         const unsigned src0_align = inst->exec_size * gen_type_size_bytes(src0_type);
         const unsigned src2_align = DIV_ROUND_UP(inst->dpas.sdepth * ops_per_chan *
                                                  src2_bits_per_element, 8u);

         ERROR_IF(dst_align > 0 && inst->dst.subnr % dst_align != 0,
                  "DPAS destination subregister offset must be aligned to the destination execution footprint.");
         ERROR_IF(src0_align > 0 && inst->src[0].subnr % src0_align != 0,
                  "DPAS src0 subregister offset must be aligned to the src0 execution footprint.");
         ERROR_IF(inst->src[1].subnr != 0,
                  "DPAS src1 subregister offset must be 0.");
         ERROR_IF(src2_align > 0 && inst->src[2].subnr % src2_align != 0,
                  "DPAS src2 subregister offset must be aligned to systolic depth times ops-per-channel.");

         ERROR_IF(inst->dst.subnr >= devinfo->grf_size,
                  "DPAS destination subregister offset must not specify the next GRF.");
         ERROR_IF(inst->src[0].subnr >= devinfo->grf_size,
                  "DPAS src0 subregister offset must not specify the next GRF.");
         ERROR_IF(inst->src[1].subnr >= devinfo->grf_size,
                  "DPAS src1 subregister offset must not specify the next GRF.");
         ERROR_IF(inst->src[2].subnr >= devinfo->grf_size,
                  "DPAS src2 subregister offset must not specify the next GRF.");

         ERROR_IF(inst->atomic_control,
                  "Atomic DPAS is not supported by the validator.");

         const bool float_mode =
            gen_type_is_float_or_bfloat(src1_type) ||
            gen_type_is_float_or_bfloat(src2_type) ||
            gen_type_is_float_or_bfloat(dst_type) ||
            gen_type_is_float_or_bfloat(src0_type);

         if (float_mode) {
            ERROR_IF(src1_type != GEN_TYPE_HF && src1_type != GEN_TYPE_BF,
                     "DPAS src1 type must be HF or BF in floating-point mode.");
            ERROR_IF(src2_type != GEN_TYPE_HF && src2_type != GEN_TYPE_BF,
                     "DPAS src2 type must be HF or BF in floating-point mode.");
            ERROR_IF(src1_type != src2_type,
                     "DPAS src1 and src2 types must match in floating-point mode.");

            if (devinfo->ver < 20) {
               ERROR_IF(dst_type != GEN_TYPE_F,
                        "DPAS destination type must be F before Gfx20 in floating-point mode.");
               ERROR_IF(src0_type != GEN_TYPE_F,
                        "DPAS src0 type must be F before Gfx20 in floating-point mode.");
            } else {
               ERROR_IF(dst_type != GEN_TYPE_F && dst_type != src1_type,
                        "DPAS destination type must be F or match src1/src2 on Gfx20+ in floating-point mode.");
               ERROR_IF(src0_type != GEN_TYPE_F && src0_type != src1_type,
                        "DPAS src0 type must be F or match src1/src2 on Gfx20+ in floating-point mode.");
            }
         } else {
            ERROR_IF(dst_type != GEN_TYPE_D && dst_type != GEN_TYPE_UD,
                     "DPAS destination type must be D or UD in integer mode.");
            ERROR_IF(src0_type != GEN_TYPE_D && src0_type != GEN_TYPE_UD,
                     "DPAS src0 type must be D or UD in integer mode.");
            ERROR_IF(src1_type != GEN_TYPE_B && src1_type != GEN_TYPE_UB,
                     "DPAS src1 type must be B or UB in integer mode.");
            ERROR_IF(src2_type != GEN_TYPE_B && src2_type != GEN_TYPE_UB,
                     "DPAS src2 type must be B or UB in integer mode.");

            if (gen_type_is_uint(dst_type)) {
               ERROR_IF(!gen_type_is_uint(src0_type) ||
                        !gen_type_is_uint(src1_type) ||
                        !gen_type_is_uint(src2_type),
                        "DPAS with an unsigned destination requires all source types to be unsigned.");
            }
         }
      }

      if (inst->opcode == GEN_OP_AVG) {
         ERROR_IF(!gen_type_is_int(inst->dst.type) ||
                  !gen_type_is_int(inst->src[0].type) ||
                  !gen_type_is_int(inst->src[1].type),
                  "AVG supports only integer operand types.");
         ERROR_IF(gen_type_size_bytes(inst->dst.type) > 4 ||
                  gen_type_size_bytes(inst->src[0].type) > 4 ||
                  gen_type_size_bytes(inst->src[1].type) > 4,
                  "AVG does not support 64-bit operand types.");
      }

      if (inst->opcode == GEN_OP_ADD) {
         ERROR_IF(gen_type_is_int(inst->src[0].type) !=
                  gen_type_is_int(inst->src[1].type),
                  "ADD cannot mix floating-point and integer source types.");
      }

      if (inst->opcode == GEN_OP_LINE || inst->opcode == GEN_OP_PLN) {
         ERROR_IF(!gen_region_is_scalar(inst->src[0].region),
                  "LINE and PLN require src0 to use the scalar region <0;1,0>.");
      }

      if (inst->opcode == GEN_OP_ROR || inst->opcode == GEN_OP_ROL) {
         ERROR_IF(inst->dst.type != GEN_TYPE_UD &&
                  inst->dst.type != GEN_TYPE_UW,
                  "ROR and ROL require dst type UD or UW.");
         ERROR_IF(inst->dst.type != inst->src[0].type,
                  "ROR and ROL require src0 and dst to have the same type.");
      }

      if (inst->opcode == GEN_OP_LRP) {
         ERROR_IF(inst->dst.type != GEN_TYPE_F ||
                  inst->src[0].type != GEN_TYPE_F ||
                  inst->src[1].type != GEN_TYPE_F ||
                  inst->src[2].type != GEN_TYPE_F,
                  "LRP requires dst and all sources to be F type.");
      }

      if (inst->opcode == GEN_OP_OR ||
          inst->opcode == GEN_OP_AND ||
          inst->opcode == GEN_OP_XOR ||
          inst->opcode == GEN_OP_NOT) {
         ERROR_IF(inst->src[0].abs,
                  "Logic instructions do not support abs on src0.");
         ERROR_IF(inst->opcode != GEN_OP_NOT &&
                  inst->src[1].file != GEN_IMM &&
                  inst->src[1].abs,
                  "Logic instructions do not support abs on src1.");

         ERROR_IF((inst->src[0].abs || inst->src[0].negate) &&
                  is_accumulator(inst->src[0]),
                  "Logic instruction source modifiers are not allowed on accumulator src0.");
         ERROR_IF(inst->opcode != GEN_OP_NOT &&
                  (inst->src[1].abs || inst->src[1].negate) &&
                  is_accumulator(inst->src[1]),
                  "Logic instruction source modifiers are not allowed on accumulator src1.");

         ERROR_IF(inst->cmod == GEN_CONDITION_OV || inst->cmod == GEN_CONDITION_UN,
                  "Logic instructions must not use OV or UN conditional modifiers.");
      }

      if (inst->opcode == GEN_OP_BFI2) {
         ERROR_IF(inst->cmod != GEN_CONDITION_NONE,
                  "BFI2 cannot use a condition modifier.");
         ERROR_IF(inst->saturate,
                  "BFI2 cannot use saturate.");
         ERROR_IF(inst->dst.type != GEN_TYPE_D && inst->dst.type != GEN_TYPE_UD,
                  "BFI2 destination type must be D or UD.");

         for (unsigned i = 0; i < 3; i++) {
            ERROR_IF(inst->src[i].type != inst->dst.type,
                     "BFI2 source types must match the destination type.");
         }
      }

      if (inst->opcode == GEN_OP_ADD3) {
         const gen_reg_type dst_type = inst->dst.type;

         ERROR_IF(dst_type != GEN_TYPE_D &&
                  dst_type != GEN_TYPE_UD &&
                  dst_type != GEN_TYPE_W &&
                  dst_type != GEN_TYPE_UW,
                  "ADD3 destination must be integer D, UD, W, or UW type.");

         for (unsigned i = 0; i < 3; i++) {
            const gen_reg_type src_type = inst->src[i].type;

            ERROR_IF(src_type != GEN_TYPE_D &&
                     src_type != GEN_TYPE_UD &&
                     src_type != GEN_TYPE_W &&
                     src_type != GEN_TYPE_UW,
                     "ADD3 sources must be integer D, UD, W, or UW type.");
         }
      }
   }

   void
   send_descriptor_restrictions()
   {
      if (!gen_inst_is_send(inst) || inst->send.desc_is_reg)
         return;

      const uint32_t desc = inst->send.desc_imm;

      switch (inst->send.sfid) {
      case GEN_SFID_URB:
         if (devinfo->ver < 20) {
            ERROR_IF(!message_desc_header_present(desc),
                     "Header must be present for all pre-Gfx20 URB messages.");

            switch (urb_desc_msg_type(desc)) {
            case GEN_URB_OPCODE_ATOMIC_INC:
            case GEN_URB_OPCODE_ATOMIC_MOV:
            case GEN_URB_OPCODE_ATOMIC_ADD:
            case GEN_URB_OPCODE_SIMD8_WRITE:
               break;

            case GEN_URB_OPCODE_SIMD8_READ:
               ERROR_IF(message_desc_response_length(desc) == 0,
                        "URB SIMD8 read messages must have a non-zero response length.");
               break;

            case GEN_GFX125_URB_OPCODE_FENCE:
               ERROR_IF(devinfo->verx10 < 125,
                        "URB fence messages require Gfx12.5+.");
               break;

            default:
               ERROR("Invalid URB message.");
               break;
            }

            return;
         }
         [[fallthrough]];

      case GEN_SFID_TGM:
      case GEN_SFID_SLM:
      case GEN_SFID_UGM:
         ERROR_IF(!devinfo->has_lsc,
                  "Platform does not support LSC.");

         if (!devinfo->has_lsc)
            return;

         ERROR_IF(lsc_opcode_has_transpose(lsc_msg_desc_opcode(devinfo, desc)) &&
                  lsc_msg_desc_transpose(devinfo, desc) &&
                  inst->exec_size != 1,
                  "Transposed LSC vectors are restricted to exec_size=1.");
         break;

      default:
         break;
      }
   }

   void
   register_region_special_restrictions()
   {
      if (devinfo->ver < 20)
         return;

      if (!gen_inst_has_dst(inst->opcode))
         return;

      if (format != GEN_FORMAT_BASIC_ONE_SRC &&
          format != GEN_FORMAT_BASIC_TWO_SRC &&
          format != GEN_FORMAT_BASIC_THREE_SRC)
         return;

      for (unsigned src_idx = 0; src_idx < 2; src_idx++) {
         if (src_idx >= num_sources)
            continue;

         const gen_operand &src = inst->src[src_idx];
         if (src.file != GEN_GRF)
            continue;

         const unsigned v = src.region.vstride;
         const unsigned w = src.region.width;
         const unsigned h = src.region.hstride;

         const bool multi_indirect =
            src.indirect && v == GEN_VSTRIDE_ONE_DIMENSIONAL;
         const bool is_vx1 = multi_indirect && w != 1;
         const bool is_vxh = multi_indirect && w == 1;

         const unsigned src_stride = w == 1 ? v : h;
         const bool src_uniform_stride = (w == 1) || (h * w == v) || is_vx1;
         const unsigned dst_stride = inst->dst.region.hstride;

         const unsigned src_size = gen_type_size_bytes(src.type);
         const unsigned dst_size = gen_type_size_bytes(inst->dst.type);
         const unsigned src_subnr = src.subnr / src_size;
         const unsigned dst_subnr = inst->dst.subnr / dst_size;

         const bool dst_dword_aligned = (dst_size >= 4) ||
                                        (dst_size == 2 && (dst_subnr % 2 == 0)) ||
                                        (dst_size == 1 && (dst_subnr % 4 == 0));

         bool allowed = false;
         if ((dst_size >= 4) ||
             (src_size >= 4) ||
             (dst_size == 2 && dst_stride > 1) ||
             (dst_size == 1 && dst_stride > 2) ||
             (src_idx == 0 && is_vxh)) {
            /* One element per DWord channel. */
            allowed = true;

         } else if (src_uniform_stride || dst_dword_aligned) {
            if (src_size == 2 && dst_size == 2) {
               if ((src_stride < 2) ||
                   (src_stride == 2 && src_uniform_stride && (dst_subnr % 16 == src_subnr / 2)))
                  allowed = true;

            } else if (src_size == 2 && dst_size == 1 && dst_stride == 2) {
               if ((src_stride < 2) ||
                   (src_stride == 2 && src_uniform_stride && (dst_subnr % 32 == src_subnr)))
                  allowed = true;

            } else if (src_idx == 0 && src_size == 1 && dst_size == 2) {
               if ((src_stride < 4) ||
                   (src_stride == 4 && src_uniform_stride && ((2 * dst_subnr) % 16 == src_subnr / 2)) ||
                   (src_stride == 8 && src_uniform_stride && ((2 * dst_subnr) % 8 == src_subnr / 4)))
                  allowed = true;

            } else if (src_idx == 0 && src_size == 1 && dst_size == 1 && dst_stride == 2) {
               if ((src_stride < 4) ||
                   (src_stride == 4 && src_uniform_stride && (dst_subnr % 32 == src_subnr / 2)) ||
                   (src_stride == 8 && src_uniform_stride && (dst_subnr % 16 == src_subnr / 4)))
                  allowed = true;

            } else if (src_idx == 0 && src_size == 1 && dst_size == 1 && dst_stride == 1 && w != 2) {
               if ((src_stride < 2) ||
                   (src_stride == 2 && src_uniform_stride && (dst_subnr % 32 == src_subnr / 2)) ||
                   (src_stride == 4 && src_uniform_stride && (dst_subnr % 16 == src_subnr / 4)))
                  allowed = true;

            } else if (src_idx == 0 && src_size == 1 && dst_size == 1 && dst_stride == 1 && w == 2) {
               if ((h == 0 && v < 4) ||
                   (h == 1 && v < 4) ||
                   (h == 2 && v < 2) ||
                   (h == 1 && v == 4 && (dst_subnr % 32 == 2 * (src_subnr / 4)) && (src_subnr % 2 == 0)) ||
                   (h == 2 && v == 4 && (dst_subnr % 32 == src_subnr / 2)) ||
                   (h == 4 && v == 8 && (dst_subnr % 32 == src_subnr / 4)))
                  allowed = true;
            }
         }

         ERROR_IF(!allowed,
                  "Invalid register region for source %u. See special restrictions section.",
                  src_idx);
      }
   }

   void
   scalar_register_restrictions()
   {
      const bool dst_is_scalar = is_scalar_register(inst->dst);
      const bool src0_is_scalar = is_scalar_register(inst->src[0]);
      const bool src1_is_scalar = is_scalar_register(inst->src[1]);
      const bool src2_is_scalar = is_scalar_register(inst->src[2]);

      if (devinfo->ver < 30) {
         ERROR_IF(dst_is_scalar || src0_is_scalar || src1_is_scalar || src2_is_scalar,
                  "Scalar registers are not available before Gfx30.");
         return;
      }

      if (dst_is_scalar) {
         switch (inst->opcode) {
         case GEN_OP_MOV: {
            const unsigned dst_size_bits = gen_type_size_bits(inst->dst.type);
            ERROR_IF(inst->dst.type != inst->src[0].type,
                     "Scalar-register MOV requires matching source and destination types.");
            ERROR_IF(!gen_type_is_int(inst->dst.type) ||
                     (dst_size_bits != 16 && dst_size_bits != 32 && dst_size_bits != 64),
                     "Scalar-register destinations must be 16-bit, 32-bit, or 64-bit integers.");
            if (inst->src[0].file == GEN_IMM) {
               ERROR_IF(inst->exec_size != 1,
                        "Scalar-register MOV with an immediate source requires exec_size=1.");
               ERROR_IF(inst->cmod != GEN_CONDITION_NONE,
                        "Scalar-register MOV with an immediate source cannot use a condition modifier.");
            }
            ERROR_IF((inst->dst.subnr / 32) !=
                     ((inst->dst.subnr + gen_type_size_bytes(inst->dst.type)) / 32),
                     "Scalar-register destinations must not cross the lower/upper 8-dword boundary.");
            break;
         }

         default:
            ERROR("Scalar-register destinations are only allowed for MOV.");
            break;
         }
      }

      if (src0_is_scalar) {
         switch (inst->opcode) {
         case GEN_OP_MOV:
            ERROR_IF(dst_is_scalar,
                     "A scalar-register source cannot be written to a scalar-register destination.");
            ERROR_IF(!gen_region_is_scalar(inst->src[0].region),
                     "Scalar-register MOV sources must use scalar broadcast region <0;1,0>.");
            break;

         case GEN_OP_SEND:
         case GEN_OP_SENDC:
            ERROR_IF(!is_null(inst->src[1]),
                     "SEND and SENDC with a scalar-register src0 require src1 to be null.");
            break;

         default:
            ERROR("Scalar-register sources are only allowed in src0 of MOV, SEND, or SENDC.");
            break;
         }
      }

      ERROR_IF(src1_is_scalar || src2_is_scalar,
               "Scalar registers are only allowed in src0.");
   }

   void
   xe3p_src1_encoding_restrictions()
   {
      if (devinfo->ver < 35 || num_sources < 2)
         return;

      if (format == GEN_FORMAT_DPAS_THREE_SRC ||
          format == GEN_FORMAT_SEND)
         return;

      if (is_null(inst->src[1]))
         return;

      const unsigned src1_stride = gen_byte_stride(inst->src[1]);
      if (src1_stride == 0)
         return;

      const unsigned dst_stride =
         MAX2(gen_byte_stride(inst->dst), gen_type_size_bytes(inst->dst.type));

      ERROR_IF(dst_stride != src1_stride,
               "On Xe3P+, src1 and dst byte stride must match.");
   }
};

bool
gen_validate(gen_validate_params *params)
{
   assert(params->devinfo);
   assert(params->mem_ctx);
   assert(params->errors == NULL);
   assert(params->insts);

   const intel_device_info *devinfo = params->devinfo;

   if (params->num_insts == 0)
      return true;

   auto v = gen_validator(devinfo, params->mem_ctx);

   for (int i = 0; i < params->num_insts; i++)
      v.validate(&params->insts[i]);

   params->errors = v.errors;
   params->num_errors = v.num_errors;
   return v.num_errors == 0;
}
