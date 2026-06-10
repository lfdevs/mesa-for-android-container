/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

#include "brw_eu.h"
#include "brw_shader.h"
#include "brw_cfg.h"
#include "dev/intel_debug.h"
#include "util/ralloc.h"
#include "util/mesa-blake3.h"
#include "util/half_float.h"
#include "util/os_file.h"
#include "gen/gen.h"

namespace {

class brw_generator
{
public:
   brw_generator(const struct brw_compiler *compiler,
                const struct brw_compile_params *params,
                struct brw_stage_prog_data *prog_data,
                mesa_shader_stage stage);
   ~brw_generator();

   void enable_debug(const char *shader_name);
   int generate_code(const brw_shader &s,
                     struct genisa_stats *stats);
   void add_const_data(void *data, unsigned size);
   void add_resume_sbt(unsigned num_resume_shaders, uint64_t *sbt);
   const unsigned *get_assembly();

private:
   void generate_send(brw_send_inst *inst,
                      struct brw_reg dst,
                      struct brw_reg desc,
                      struct brw_reg ex_desc,
                      struct brw_reg payload,
                      struct brw_reg payload2,
                      bool ex_bso);
   void generate_barrier(brw_inst *inst, struct brw_reg src);
   void generate_ddx(const brw_inst *inst,
                     struct brw_reg dst, struct brw_reg src);
   void generate_ddy(const brw_inst *inst,
                     struct brw_reg dst, struct brw_reg src);
   void generate_scratch_header(brw_inst *inst,
                                struct brw_reg dst, struct brw_reg src);

   void generate_mov_indirect(brw_inst *inst,
                              struct brw_reg dst,
                              struct brw_reg reg,
                              struct brw_reg indirect_byte_offset);

   void generate_shuffle(brw_inst *inst,
                         struct brw_reg dst,
                         struct brw_reg src,
                         struct brw_reg idx);

   void generate_quad_swizzle(const brw_inst *inst,
                              struct brw_reg dst, struct brw_reg src,
                              unsigned swiz);

   void generate_broadcast(const brw_reg &dst, const brw_reg &src, const brw_reg &idx);
   void generate_math(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1, gen_math func);

   void generate_float_controls_mode(unsigned mode, unsigned mask);

   const struct brw_compiler *compiler;
   const struct brw_compile_params *params;

   const struct intel_device_info *devinfo;

   struct brw_stage_prog_data * const prog_data;

   unsigned dispatch_width; /**< 8, 16 or 32 */

   int final_halt_idx;
   bool needs_final_halt;

   bool debug_flag;
   const char *shader_name;
   mesa_shader_stage stage;
   void *mem_ctx;

   int output_size = 0;
   uint8_t *output = NULL;

   int allocate_output(unsigned size, unsigned alignemnt);
   int append_output(void *data, unsigned size, unsigned alignment);

   std::vector<gen_inst> gen_insts;

   const char *next_annotation;
   std::vector<const char *> annotations;

   struct state {
      uint8_t exec_size;
      uint8_t chan_offset;
      uint8_t flag_nr;
      uint8_t flag_subnr;
      gen_predicate pred_control;
      bool pred_inv;
      bool no_mask;
      bool saturate;
      bool align16;
      bool acc_wr_control;
      gen_swsb swsb;
   };

   std::vector<state> state_stack;

   void reset_state()     { state_stack.clear(); state_stack.push_back({}); }
   state *current_state() { return &state_stack.back(); }
   state *push_state()    { state_stack.push_back(*current_state()); return current_state(); }
   void pop_state()       { state_stack.pop_back(); }

   gen_operand to_gen(const brw_reg &r, bool align16 = false);
   gen_opcode to_gen(enum opcode op);
   gen_file to_gen(brw_reg_file file);

   gen_inst make_empty();
   gen_inst make(gen_opcode op);
   gen_inst *append(const gen_inst &gen);

   gen_inst *append(enum opcode opcode);
   gen_inst *append(enum opcode opcode, const brw_reg &dst,
                    const brw_reg &src0);
   gen_inst *append(enum opcode opcode, const brw_reg &dst,
                    const brw_reg &src0, const brw_reg &src1);
   gen_inst *append(enum opcode opcode, const brw_reg &dst,
                    const brw_reg &src0, const brw_reg &src1, const brw_reg &src2);

   gen_inst *append_SYNC(gen_sync_func func);
   gen_inst *append_NOP();

   inline gen_inst *append_MOV(const brw_reg &dst, const brw_reg &src0) { return append(BRW_OPCODE_MOV, dst, src0); }

   void update_branch_ips();

   int num_relocs = 0;
   intel_shader_reloc *relocs = NULL;

   void append_reloc(const intel_shader_reloc &r);
};

} /* anonymous namespace */

static gen_opcode
brw_opcode_to_gen(enum opcode op)
{
   switch (op) {
   case BRW_OPCODE_ILLEGAL:  return GEN_OP_ILLEGAL;

   case BRW_OPCODE_ADD:      return GEN_OP_ADD;
   case BRW_OPCODE_ADD3:     return GEN_OP_ADD3;
   case BRW_OPCODE_ADDC:     return GEN_OP_ADDC;
   case BRW_OPCODE_AND:      return GEN_OP_AND;
   case BRW_OPCODE_ASR:      return GEN_OP_ASR;
   case BRW_OPCODE_AVG:      return GEN_OP_AVG;
   case BRW_OPCODE_BFE:      return GEN_OP_BFE;
   case BRW_OPCODE_BFI1:     return GEN_OP_BFI1;
   case BRW_OPCODE_BFI2:     return GEN_OP_BFI2;
   case BRW_OPCODE_BFN:      return GEN_OP_BFN;
   case BRW_OPCODE_BFREV:    return GEN_OP_BFREV;
   case BRW_OPCODE_BRC:      return GEN_OP_BRC;
   case BRW_OPCODE_BRD:      return GEN_OP_BRD;
   case BRW_OPCODE_BREAK:    return GEN_OP_BREAK;
   case BRW_OPCODE_CALL:     return GEN_OP_CALL;
   case BRW_OPCODE_CALLA:    return GEN_OP_CALLA;
   case BRW_OPCODE_CBIT:     return GEN_OP_CBIT;
   case BRW_OPCODE_CMP:      return GEN_OP_CMP;
   case BRW_OPCODE_CMPN:     return GEN_OP_CMPN;
   case BRW_OPCODE_CONTINUE: return GEN_OP_CONTINUE;
   case BRW_OPCODE_CSEL:     return GEN_OP_CSEL;
   case BRW_OPCODE_DP2:      return GEN_OP_DP2;
   case BRW_OPCODE_DP3:      return GEN_OP_DP3;
   case BRW_OPCODE_DP4:      return GEN_OP_DP4;
   case BRW_OPCODE_DP4A:     return GEN_OP_DP4A;
   case BRW_OPCODE_DPAS:     return GEN_OP_DPAS;
   case BRW_OPCODE_DPH:      return GEN_OP_DPH;
   case BRW_OPCODE_ELSE:     return GEN_OP_ELSE;
   case BRW_OPCODE_ENDIF:    return GEN_OP_ENDIF;
   case BRW_OPCODE_FBH:      return GEN_OP_FBH;
   case BRW_OPCODE_FBL:      return GEN_OP_FBL;
   case BRW_OPCODE_FRC:      return GEN_OP_FRC;
   case BRW_OPCODE_GOTO:     return GEN_OP_GOTO;
   case BRW_OPCODE_HALT:     return GEN_OP_HALT;
   case BRW_OPCODE_IF:       return GEN_OP_IF;
   case BRW_OPCODE_JMPI:     return GEN_OP_JMPI;
   case BRW_OPCODE_JOIN:     return GEN_OP_JOIN;
   case BRW_OPCODE_LINE:     return GEN_OP_LINE;
   case BRW_OPCODE_LRP:      return GEN_OP_LRP;
   case BRW_OPCODE_LZD:      return GEN_OP_LZD;
   case BRW_OPCODE_MAC:      return GEN_OP_MAC;
   case BRW_OPCODE_MACH:     return GEN_OP_MACH;
   case BRW_OPCODE_MACL:     return GEN_OP_MACL;
   case BRW_OPCODE_MAD:      return GEN_OP_MAD;
   case BRW_OPCODE_MADM:     return GEN_OP_MADM;
   case BRW_OPCODE_MATH:     return GEN_OP_MATH;
   case BRW_OPCODE_MOV:      return GEN_OP_MOV;
   case BRW_OPCODE_MOVI:     return GEN_OP_MOVI;
   case BRW_OPCODE_MUL:      return GEN_OP_MUL;
   case BRW_OPCODE_NOP:      return GEN_OP_NOP;
   case BRW_OPCODE_NOT:      return GEN_OP_NOT;
   case BRW_OPCODE_OR:       return GEN_OP_OR;
   case BRW_OPCODE_PLN:      return GEN_OP_PLN;
   case BRW_OPCODE_RET:      return GEN_OP_RET;
   case BRW_OPCODE_RNDD:     return GEN_OP_RNDD;
   case BRW_OPCODE_RNDE:     return GEN_OP_RNDE;
   case BRW_OPCODE_RNDU:     return GEN_OP_RNDU;
   case BRW_OPCODE_RNDZ:     return GEN_OP_RNDZ;
   case BRW_OPCODE_ROL:      return GEN_OP_ROL;
   case BRW_OPCODE_ROR:      return GEN_OP_ROR;
   case BRW_OPCODE_SEL:      return GEN_OP_SEL;
   case BRW_OPCODE_SEND:     return GEN_OP_SEND;
   case BRW_OPCODE_SENDC:    return GEN_OP_SENDC;
   case BRW_OPCODE_SENDS:    return GEN_OP_SENDS;
   case BRW_OPCODE_SENDSC:   return GEN_OP_SENDSC;
   case BRW_OPCODE_SHL:      return GEN_OP_SHL;
   case BRW_OPCODE_SHR:      return GEN_OP_SHR;
   case BRW_OPCODE_SMOV:     return GEN_OP_SMOV;
   case BRW_OPCODE_SRND:     return GEN_OP_SRND;
   case BRW_OPCODE_SUBB:     return GEN_OP_SUBB;
   case BRW_OPCODE_SYNC:     return GEN_OP_SYNC;
   case BRW_OPCODE_WAIT:     return GEN_OP_WAIT;
   case BRW_OPCODE_WHILE:    return GEN_OP_WHILE;
   case BRW_OPCODE_XOR:      return GEN_OP_XOR;

   default:                  UNREACHABLE("invalid gen opcode");
   }
}

gen_opcode
brw_generator::to_gen(enum opcode op)
{
   return brw_opcode_to_gen(op);
}

static inline gen_region
gen_region_from_reg(brw_reg r)
{
   uint8_t v = r.vstride == 0                                   ? 0 :
               r.vstride == BRW_VERTICAL_STRIDE_ONE_DIMENSIONAL ? GEN_VSTRIDE_ONE_DIMENSIONAL :
                                                                  (1 << (r.vstride - 1));
   uint8_t w = 1 << r.width;
   uint8_t h = r.hstride == 0 ? 0 : (1 << (r.hstride - 1));
   return {v, w, h};
}

gen_file
brw_generator::to_gen(brw_reg_file file)
{
   switch (file) {
   case BAD_FILE:  return GEN_BAD_FILE;
   case ARF:       return GEN_ARF;
   case ADDRESS:   return GEN_ARF;
   case FIXED_GRF: return GEN_GRF;
   case IMM:       return GEN_IMM;
   default:        UNREACHABLE("invalid reg file to convert");
   }
}

gen_operand
brw_generator::to_gen(const brw_reg &r, bool align16)
{
   gen_operand o = {};

   o.type = (gen_reg_type) r.type;
   o.file = to_gen(r.file);
   o.indirect = r.address_mode;
   o.negate = r.negate;
   o.abs = r.abs;

   if (o.indirect) {
      o.region = gen_region_from_reg(r);
      o.addr_imm = r.indirect_offset;
   } else {
      if (r.file == IMM) {
         o.imm = r.u64;
      } else if (r.file == ADDRESS) {
         o.nr = BRW_ARF_ADDRESS;
         o.subnr = phys_subnr(devinfo, r);
         o.region = gen_region_from_reg(r);
      } else {
         o.nr = phys_nr(devinfo, r);
         o.subnr = phys_subnr(devinfo, r);
         o.region = gen_region_from_reg(r);
      }
   }

   if (align16) {
      o.writemask = r.writemask;
      o.swizzle = r.swizzle;
      o.rep_ctrl = o.region.vstride == 0;
   }

   return o;
}

static gen_math
gen_math_func_for_opcode(enum opcode op)
{
   switch (op) {
   case SHADER_OPCODE_RCP:           return GEN_MATH_INV;
   case SHADER_OPCODE_RSQ:           return GEN_MATH_RSQ;
   case SHADER_OPCODE_SQRT:          return GEN_MATH_SQRT;
   case SHADER_OPCODE_EXP2:          return GEN_MATH_EXP;
   case SHADER_OPCODE_LOG2:          return GEN_MATH_LOG;
   case SHADER_OPCODE_POW:           return GEN_MATH_POW;
   case SHADER_OPCODE_SIN:           return GEN_MATH_SIN;
   case SHADER_OPCODE_COS:           return GEN_MATH_COS;
   case SHADER_OPCODE_INT_QUOTIENT:  return GEN_MATH_INT_DIV_QUOTIENT;
   case SHADER_OPCODE_INT_REMAINDER: return GEN_MATH_INT_DIV_REMAINDER;
   default:
      UNREACHABLE("not reached: unknown math function");
   }
}

static struct brw_reg
normalize_brw_reg_for_encoding(brw_reg *reg)
{
   struct brw_reg brw_reg;

   switch (reg->file) {
   case ADDRESS:
   case ARF:
   case FIXED_GRF:
   case IMM:
      assert(reg->offset == 0);
      brw_reg = *reg;
      break;
   case BAD_FILE:
      /* Probably unused. */
      brw_reg = brw_null_reg();
      break;
   case VGRF:
   case ATTR:
   case UNIFORM:
      UNREACHABLE("not reached");
   }

   return brw_reg;
}

brw_generator::brw_generator(const struct brw_compiler *compiler,
                             const struct brw_compile_params *params,
                             struct brw_stage_prog_data *prog_data,
                             mesa_shader_stage stage)

   : compiler(compiler), params(params),
     devinfo(compiler->devinfo),
     prog_data(prog_data), dispatch_width(0),
     debug_flag(false),
     shader_name(NULL), stage(stage), mem_ctx(params->mem_ctx),
     next_annotation(NULL)
{
}

brw_generator::~brw_generator()
{
}

void
brw_generator::generate_send(brw_send_inst *inst,
                             struct brw_reg dst,
                             struct brw_reg desc,
                             struct brw_reg ex_desc,
                             struct brw_reg payload,
                             struct brw_reg payload2,
                             bool ex_bso)
{
   const bool gather = inst->opcode == SHADER_OPCODE_SEND_GATHER;
   if (gather) {
      assert(payload.file == ARF);
      assert(payload.nr == BRW_ARF_SCALAR);
      assert(payload2.file == ARF);
      assert(payload2.nr == BRW_ARF_NULL);
   }

   const brw_send_inst *send = inst->as_send();
   gen_inst *gen = append(devinfo->ver >= 12 ? BRW_OPCODE_SEND
                                             : BRW_OPCODE_SENDS);
   gen->send.eot = send->eot;
   gen->send.sfid = (gen_sfid) send->sfid;

   gen->dst.file = to_gen(dst.file);
   gen->dst.nr = phys_nr(devinfo, dst);
   gen->dst.type = GEN_TYPE_UD;

   gen->src[0] = to_gen(payload);
   gen->src[1] = to_gen(payload2);

   if (desc.file == IMM)
      gen->send.desc_imm = desc.ud;
   else
      gen->send.desc_is_reg = true;

   if (ex_desc.file == IMM) {
      gen->send.ex_desc_imm = ex_desc.ud;
   } else {
      gen->send.ex_desc_is_reg = true;
      gen->send.ex_desc_subnr = ex_desc.subnr;

      if (inst->ex_desc_imm)
         gen->send.ex_desc_imm_extra = inst->offset;
   }

   if (ex_bso) {
      gen->send.ex_bso = true;
      gen->send.src1_len = inst->ex_mlen / reg_unit(devinfo);
   }

   if (devinfo->ver >= 20 && gen->send.sfid == GEN_SFID_UGM) {
      gen->send.src1_len = inst->ex_mlen / reg_unit(devinfo);
   }

   if (send->check_tdr) {
      if      (gen->opcode == GEN_OP_SEND)  gen->opcode = GEN_OP_SENDC;
      else if (gen->opcode == GEN_OP_SENDS) gen->opcode = GEN_OP_SENDSC;
   }

   /* Serialize messages if needed */
   if (devinfo->ver == 12 && inst->fused_eu_disable)
      gen->fusion_control = true;
}

void
brw_generator::generate_mov_indirect(brw_inst *inst,
                                     struct brw_reg dst,
                                     struct brw_reg reg,
                                     struct brw_reg indirect_byte_offset)
{
   assert(indirect_byte_offset.type == BRW_TYPE_UD);
   assert(indirect_byte_offset.file == FIXED_GRF);
   assert(!reg.abs && !reg.negate);
   assert(brw_type_is_uint(reg.type));
   assert(reg.type == dst.type);

   unsigned imm_byte_offset = reg.nr * REG_SIZE + reg.subnr;

   if (indirect_byte_offset.file == IMM) {
      imm_byte_offset += indirect_byte_offset.ud;

      reg.nr = imm_byte_offset / REG_SIZE;
      reg.subnr = imm_byte_offset % REG_SIZE;
      if (brw_type_size_bytes(reg.type) > 4 && !devinfo->has_64bit_int) {
         append_MOV(subscript(dst, BRW_TYPE_D, 0),
                    subscript(reg, BRW_TYPE_D, 0));
         current_state()->swsb = gen_swsb_null();
         append_MOV(subscript(dst, BRW_TYPE_D, 1),
                    subscript(reg, BRW_TYPE_D, 1));
      } else {
         append_MOV(dst, reg);
      }
   } else {
      /* We use VxH indirect addressing, clobbering a0.0 through a0.7. */
      struct brw_reg addr = vec8(brw_address_reg(0));

      /* Whether we can use destination dependency control without running the
       * risk of a hang if an instruction gets shot down.
       */
      const bool use_dep_ctrl = !inst->predicate &&
                                inst->exec_size == dispatch_width;
      gen_inst *gen;

      /* The destination stride of an instruction (in bytes) must be greater
       * than or equal to the size of the rest of the instruction.  Since the
       * address register is of type UW, we can't use a D-type instruction.
       * In order to get around this, re retype to UW and use a stride.
       */
      indirect_byte_offset =
         retype(spread(indirect_byte_offset, 2), BRW_TYPE_UW);

      /* There are a number of reasons why we don't use the base offset here.
       * One reason is that the field is only 9 bits which means we can only
       * use it to access the first 16 GRFs.  Also, from the Haswell PRM
       * section "Register Region Restrictions":
       *
       *    "The lower bits of the AddressImmediate must not overflow to
       *    change the register address.  The lower 5 bits of Address
       *    Immediate when added to lower 5 bits of address register gives
       *    the sub-register offset. The upper bits of Address Immediate
       *    when added to upper bits of address register gives the register
       *    address. Any overflow from sub-register offset is dropped."
       *
       * Since the indirect may cause us to cross a register boundary, this
       * makes the base offset almost useless.  We could try and do something
       * clever where we use a actual base offset if base_offset % 32 == 0 but
       * that would mean we were generating different code depending on the
       * base offset.  Instead, for the sake of consistency, we'll just do the
       * add ourselves.  This restriction is only listed in the Haswell PRM
       * but empirical testing indicates that it applies on all older
       * generations and is lifted on Broadwell.
       *
       * In the end, while base_offset is nice to look at in the generated
       * code, using it saves us 0 instructions and would require quite a bit
       * of case-by-case work.  It's just not worth it.
       *
       * Due to a hardware bug some platforms (particularly Gfx11+) seem to
       * require the address components of all channels to be valid whether or
       * not they're active, which causes issues if we use VxH addressing
       * under non-uniform control-flow.  We can easily work around that by
       * initializing the whole address register with a pipelined NoMask MOV
       * instruction.
       */
      gen = append_MOV(addr, brw_imm_uw(imm_byte_offset));
      gen->no_mask = true;
      gen->pred_control = GEN_PREDICATE_NONE;
      if (devinfo->ver >= 12)
         current_state()->swsb = gen_swsb_null();
      else
         gen->no_dd_clear = use_dep_ctrl;

      gen = append(BRW_OPCODE_ADD, addr, indirect_byte_offset, brw_imm_uw(imm_byte_offset));
      if (devinfo->ver >= 12)
         current_state()->swsb = gen_swsb_regdist(1);
      else
         gen->no_dd_check = use_dep_ctrl;

      if (brw_type_size_bytes(reg.type) > 4 &&
          (devinfo->ver != 9 || intel_device_info_is_9lp(devinfo))) {
         /* From the Cherryview PRM Vol 7. "Register Region Restrictions":
          *
          *   "When source or destination datatype is 64b or operation is
          *    integer DWord multiply, indirect addressing must not be used."
          *
          * Later platforms either don't support Q/UQ types or have a
          * restriction in "Register Region Restrictions" similar to
          *
          *   "Vx1 and VxH indirect addressing for Float, Half-Float, Double-Float and
          *    Quad-Word data must not be used."
          *
          * Which means effectively all platforms except non-LP Gfx9 will
          * need to lower this MOV.
          *
          * To work around both of these, we do two integer MOVs instead
          * of one 64-bit MOV.  Because no double value should ever cross
          * a register boundary, it's safe to use the immediate offset in
          * the indirect here to handle adding 4 bytes to the offset and
          * avoid the extra ADD to the register file.
          */
         append_MOV(subscript(dst, BRW_TYPE_D, 0),
                    retype(brw_VxH_indirect(0, 0), BRW_TYPE_D));
         current_state()->swsb = gen_swsb_null();
         append_MOV(subscript(dst, BRW_TYPE_D, 1),
                    retype(brw_VxH_indirect(0, 4), BRW_TYPE_D));
      } else {
         struct brw_reg ind_src = brw_VxH_indirect(0, 0);

         append_MOV(dst, retype(ind_src, reg.type));
      }
   }
}

void
brw_generator::generate_shuffle(brw_inst *inst,
                                struct brw_reg dst,
                                struct brw_reg src,
                                struct brw_reg idx)
{
   assert(src.file == FIXED_GRF);
   assert(!src.abs && !src.negate);

   /* Ivy bridge has some strange behavior that makes this a real pain to
    * implement for 64-bit values so we just don't bother.
    */
   assert(devinfo->has_64bit_float || brw_type_size_bytes(src.type) <= 4);
   assert(brw_type_is_uint(src.type));
   assert(src.type == dst.type);

   /* Because we're using the address register, we're limited to 16-wide
    * by the address register file and 8-wide for 64-bit types.  We could try
    * and make this instruction splittable higher up in the compiler but that
    * gets weird because it reads all of the channels regardless of execution
    * size.  It's easier just to split it here.
    */
   unsigned lower_width = MIN2(16, inst->exec_size);
   if (devinfo->ver < 20 && (element_sz(src) > 4 || element_sz(dst) > 4)) {
      lower_width = 8;
   }

   current_state()->exec_size = lower_width;
   for (unsigned group = 0; group < inst->exec_size; group += lower_width) {
      current_state()->chan_offset = group;

      if ((src.vstride == 0 && src.hstride == 0) ||
          idx.file == IMM) {
         /* Trivial, the source is already uniform or the index is a constant.
          * We will typically not get here if the optimizer is doing its job,
          * but asserting would be mean.
          */
         const unsigned i = idx.file == IMM ? idx.ud : 0;
         struct brw_reg group_src = stride(suboffset(src, i), 0, 1, 0);
         struct brw_reg group_dst = suboffset(dst, group << (dst.hstride - 1));
         append_MOV(group_dst, group_src);
      } else {
         /* We use VxH indirect addressing, clobbering a0.0 through a0.7. */
         struct brw_reg addr = vec8(brw_address_reg(0));

         struct brw_reg group_idx = idx.is_scalar || is_uniform(idx) ?
            component(idx, 0) : suboffset(idx, group);

         if (lower_width == 8 && group_idx.width == BRW_WIDTH_16) {
            /* Things get grumpy if the register is too wide. */
            group_idx.width--;
            group_idx.vstride--;
         }

         assert(brw_type_size_bytes(group_idx.type) <= 4);
         if (brw_type_size_bytes(group_idx.type) == 4) {
            /* The destination stride of an instruction (in bytes) must be
             * greater than or equal to the size of the rest of the
             * instruction.  Since the address register is of type UW, we
             * can't use a D-type instruction.  In order to get around this,
             * re retype to UW and use a stride.
             */
            group_idx = retype(spread(group_idx, 2), BRW_TYPE_W);
         }

         uint32_t src_start_offset = src.nr * REG_SIZE + src.subnr;

         /* From the Haswell PRM:
          *
          *    "When a sequence of NoDDChk and NoDDClr are used, the last
          *    instruction that completes the scoreboard clear must have a
          *    non-zero execution mask. This means, if any kind of predication
          *    can change the execution mask or channel enable of the last
          *    instruction, the optimization must be avoided.  This is to
          *    avoid instructions being shot down the pipeline when no writes
          *    are required."
          *
          * Whenever predication is enabled or the instructions being emitted
          * aren't the full width, it's possible that it will be run with zero
          * channels enabled so we can't use dependency control without
          * running the risk of a hang if an instruction gets shot down.
          */
         const bool use_dep_ctrl = !inst->predicate &&
                                   lower_width == dispatch_width;
         gen_inst *gen;

         /* Due to a hardware bug some platforms (particularly Gfx11+) seem
          * to require the address components of all channels to be valid
          * whether or not they're active, which causes issues if we use VxH
          * addressing under non-uniform control-flow.  We can easily work
          * around that by initializing the whole address register with a
          * pipelined NoMask MOV instruction.
          */
         gen = append_MOV(addr, brw_imm_uw(src_start_offset));
         gen->no_mask = true;
         gen->pred_control = GEN_PREDICATE_NONE;
         if (devinfo->ver >= 12)
            current_state()->swsb = gen_swsb_null();
         else
            gen->no_dd_clear = use_dep_ctrl;

         /* Take into account the component size and horizontal stride. */
         assert(src.vstride == src.hstride + src.width);
         gen = append(BRW_OPCODE_SHL, addr, group_idx,
                      brw_imm_uw(util_logbase2(brw_type_size_bytes(src.type)) +
                                 src.hstride - 1));
         if (devinfo->ver >= 12)
            current_state()->swsb = gen_swsb_regdist(1);
         else
            gen->no_dd_check = use_dep_ctrl;

         /* Add on the register start offset */
         append(BRW_OPCODE_ADD, addr, addr, brw_imm_uw(src_start_offset));
         append(BRW_OPCODE_MOV, suboffset(dst, group << (dst.hstride - 1)),
                                retype(brw_VxH_indirect(0, 0), src.type));
      }

      current_state()->swsb = gen_swsb_null();
   }
}

void
brw_generator::generate_quad_swizzle(const brw_inst *inst,
                                     struct brw_reg dst, struct brw_reg src,
                                     unsigned swiz)
{
   /* Requires a quad. */
   assert(inst->exec_size >= 4);

   if (src.file == IMM ||
       has_scalar_region(src)) {
      /* The value is uniform across all channels */
      append_MOV(dst, src);

   } else if (devinfo->ver == 9 && brw_type_size_bytes(src.type) == 4) {
      /* This only works on 8-wide 32-bit values */
      assert(inst->exec_size == 8);
      assert(src.hstride == BRW_HORIZONTAL_STRIDE_1);
      assert(src.vstride == src.width + 1);
      current_state()->align16 = true;
      struct brw_reg swiz_src = stride(src, 4, 4, 1);
      swiz_src.swizzle = swiz;
      append_MOV(dst, swiz_src);

   } else {
      assert(src.hstride == BRW_HORIZONTAL_STRIDE_1);
      assert(src.vstride == src.width + 1);
      const struct brw_reg src_0 = suboffset(src, BRW_GET_SWZ(swiz, 0));

      switch (swiz) {
      case BRW_SWIZZLE_XXXX:
      case BRW_SWIZZLE_YYYY:
      case BRW_SWIZZLE_ZZZZ:
      case BRW_SWIZZLE_WWWW:
         append_MOV(dst, stride(src_0, 4, 4, 0));
         break;

      case BRW_SWIZZLE_XXZZ:
      case BRW_SWIZZLE_YYWW:
         append_MOV(dst, stride(src_0, 2, 2, 0));
         break;

      case BRW_SWIZZLE_XYXY:
      case BRW_SWIZZLE_ZWZW:
         assert(inst->exec_size == 4);
         append_MOV(dst, stride(src_0, 0, 2, 1));
         break;

      default:
         assert(inst->force_writemask_all);
         current_state()->exec_size = inst->exec_size / 4;

         for (unsigned c = 0; c < 4; c++) {
            gen_inst *gen = append_MOV(
               stride(suboffset(dst, c),
                      4 * inst->dst.stride, 1, 4 * inst->dst.stride),
               stride(suboffset(src, BRW_GET_SWZ(swiz, c)), 4, 1, 0));

            if (devinfo->ver < 12) {
               gen->no_dd_clear = c < 3;
               gen->no_dd_check = c > 0;
            }

            current_state()->swsb = gen_swsb_null();
         }

         break;
      }
   }
}

void
brw_generator::generate_barrier(brw_inst *, struct brw_reg src)
{
   gen_inst *gen = append(devinfo->ver >= 12 ? BRW_OPCODE_SEND : BRW_OPCODE_SENDS,
                          retype(brw_null_reg(), BRW_TYPE_UD),
                          src,
                          brw_null_reg());

   uint32_t desc =
      brw_message_desc(devinfo, 1 * reg_unit(devinfo), 0, false);
   desc |= (uint32_t)GEN_MESSAGE_GATEWAY_SFID_BARRIER_MSG;
   gen->align16 = false;
   gen->send.sfid = GEN_SFID_MESSAGE_GATEWAY;
   gen->send.desc_imm = desc;
   gen->no_mask = true;

   if (devinfo->ver >= 12) {
      current_state()->swsb = gen_swsb_null();
      append_SYNC(GEN_SYNC_BAR);
   } else {
      gen = append(BRW_OPCODE_WAIT,
                   brw_notification_reg(),
                   brw_notification_reg(),
                   brw_null_reg());
      gen->exec_size = 1;
      gen->no_mask = true;
   }
}

/* For OPCODE_DDX and OPCODE_DDY, per channel of output we've got input
 * looking like:
 *
 * arg0: ss0.tl ss0.tr ss0.bl ss0.br ss1.tl ss1.tr ss1.bl ss1.br
 *
 * Ideally, we want to produce:
 *
 *           DDX                     DDY
 * dst: (ss0.tr - ss0.tl)     (ss0.tl - ss0.bl)
 *      (ss0.tr - ss0.tl)     (ss0.tr - ss0.br)
 *      (ss0.br - ss0.bl)     (ss0.tl - ss0.bl)
 *      (ss0.br - ss0.bl)     (ss0.tr - ss0.br)
 *      (ss1.tr - ss1.tl)     (ss1.tl - ss1.bl)
 *      (ss1.tr - ss1.tl)     (ss1.tr - ss1.br)
 *      (ss1.br - ss1.bl)     (ss1.tl - ss1.bl)
 *      (ss1.br - ss1.bl)     (ss1.tr - ss1.br)
 *
 * and add another set of two more subspans if in 16-pixel dispatch mode.
 *
 * For DDX, it ends up being easy: width = 2, horiz=0 gets us the same result
 * for each pair, and vertstride = 2 jumps us 2 elements after processing a
 * pair.  But the ideal approximation may impose a huge performance cost on
 * sample_d.  On at least Haswell, sample_d instruction does some
 * optimizations if the same LOD is used for all pixels in the subspan.
 *
 * For DDY, we need to use ALIGN16 mode since it's capable of doing the
 * appropriate swizzling.
 */
void
brw_generator::generate_ddx(const brw_inst *inst,
                            struct brw_reg dst, struct brw_reg src)
{
   unsigned vstride, width;

   if (inst->opcode == FS_OPCODE_DDX_FINE) {
      /* produce accurate derivatives */
      vstride = BRW_VERTICAL_STRIDE_2;
      width = BRW_WIDTH_2;
   } else {
      /* replicate the derivative at the top-left pixel to other pixels */
      vstride = BRW_VERTICAL_STRIDE_4;
      width = BRW_WIDTH_4;
   }

   struct brw_reg src0 = byte_offset(src, brw_type_size_bytes(src.type));;
   struct brw_reg src1 = src;

   src0.vstride = vstride;
   src0.width   = width;
   src0.hstride = BRW_HORIZONTAL_STRIDE_0;
   src1.vstride = vstride;
   src1.width   = width;
   src1.hstride = BRW_HORIZONTAL_STRIDE_0;

   append(BRW_OPCODE_ADD, dst, src0, negate(src1));
}

/* The negate_value boolean is used to negate the derivative computation for
 * FBOs, since they place the origin at the upper left instead of the lower
 * left.
 */
void
brw_generator::generate_ddy(const brw_inst *inst,
                            struct brw_reg dst, struct brw_reg src)
{
   const uint32_t type_size = brw_type_size_bytes(src.type);

   if (inst->opcode == FS_OPCODE_DDY_FINE) {
      /* produce accurate derivatives.
       *
       * From the Broadwell PRM, Volume 7 (3D-Media-GPGPU)
       * "Register Region Restrictions", Section "1. Special Restrictions":
       *
       *    "In Align16 mode, the channel selects and channel enables apply to
       *     a pair of half-floats, because these parameters are defined for
       *     DWord elements ONLY. This is applicable when both source and
       *     destination are half-floats."
       *
       * So for half-float operations we use the Gfx11+ Align1 path. CHV
       * inherits its FP16 hardware from SKL, so it is not affected.
       */
      if (devinfo->ver >= 11) {
         src = stride(src, 0, 2, 1);

         push_state();
         current_state()->exec_size = 4;
         for (uint32_t g = 0; g < inst->exec_size; g += 4) {
            gen_inst *gen = append(BRW_OPCODE_ADD,
                                   byte_offset(dst, g * type_size),
                                   negate(byte_offset(src,  g * type_size)),
                                   byte_offset(src, (g + 2) * type_size));
            gen->chan_offset = inst->group + g;

            current_state()->swsb = gen_swsb_null();
         }
         pop_state();
      } else {
         push_state();
         current_state()->align16 = true;

         struct brw_reg src0 = stride(src, 4, 4, 1);
         struct brw_reg src1 = stride(src, 4, 4, 1);
         src0.swizzle = BRW_SWIZZLE_XYXY;
         src1.swizzle = BRW_SWIZZLE_ZWZW;

         append(BRW_OPCODE_ADD, dst, negate(src0), src1);
         pop_state();
      }
   } else {
      /* replicate the derivative at the top-left pixel to other pixels */
      struct brw_reg src0 = byte_offset(stride(src, 4, 4, 0), 0 * type_size);
      struct brw_reg src1 = byte_offset(stride(src, 4, 4, 0), 2 * type_size);

      append(BRW_OPCODE_ADD, dst, negate(src0), src1);
   }
}

void
brw_generator::generate_broadcast(const brw_reg &dst, const brw_reg &src, const brw_reg &idx)
{
   push_state();
   current_state()->no_mask = true;
   current_state()->exec_size = 1;

   assert(src.file == FIXED_GRF &&
          src.address_mode == BRW_ADDRESS_DIRECT);
   assert(!src.abs && !src.negate);
   assert(brw_type_is_uint(src.type));
   assert(src.type == dst.type);

   if ((src.vstride == 0 && src.hstride == 0) ||
       idx.file == IMM) {
      /* Trivial, the source is already uniform or the index is a constant.
       * We will typically not get here if the optimizer is doing its job, but
       * asserting would be mean.
       */
      const unsigned i = (src.vstride == 0 && src.hstride == 0) ? 0 : idx.ud;
      const brw_reg s = stride(suboffset(src, i), 0, 1, 0);

      if (brw_type_size_bytes(s.type) > 4 && !devinfo->has_64bit_int) {
         append_MOV(subscript(dst, BRW_TYPE_D, 0),
                    subscript(s, BRW_TYPE_D, 0));
         current_state()->swsb = gen_swsb_null();
         append_MOV(subscript(dst, BRW_TYPE_D, 1),
                    subscript(s, BRW_TYPE_D, 1));
      } else {
         append_MOV(dst, s);
      }
   } else {
      /* From the Haswell PRM section "Register Region Restrictions":
       *
       *    "The lower bits of the AddressImmediate must not overflow to
       *    change the register address.  The lower 5 bits of Address
       *    Immediate when added to lower 5 bits of address register gives
       *    the sub-register offset. The upper bits of Address Immediate
       *    when added to upper bits of address register gives the register
       *    address. Any overflow from sub-register offset is dropped."
       *
       * Fortunately, for broadcast, we never have a sub-register offset so
       * this isn't an issue.
       */
      assert(src.subnr == 0);

      const struct brw_reg addr =
         retype(brw_address_reg(0), BRW_TYPE_UD);
      unsigned offset = src.nr * REG_SIZE + src.subnr;
      /* Limit in bytes of the signed indirect addressing immediate. */
      const unsigned limit = 512;

      push_state();
      current_state()->no_mask = true;
      current_state()->pred_control = GEN_PREDICATE_NONE;
      current_state()->flag_nr = 0;
      current_state()->flag_subnr = 0;

      /* Take into account the component size and horizontal stride. */
      assert(src.vstride == src.hstride + src.width);
      append(BRW_OPCODE_SHL, addr, vec1(idx),
             brw_imm_ud(util_logbase2(brw_type_size_bytes(src.type)) +
                        src.hstride - 1));

      /* We can only address up to limit bytes using the indirect
       * addressing immediate, account for the difference if the source
       * register is above this limit.
       */
      if (offset >= limit) {
         current_state()->swsb = gen_swsb_regdist(1);
         append(BRW_OPCODE_ADD, addr, addr, brw_imm_ud(offset - offset % limit));
         offset = offset % limit;
      }

      pop_state();

      current_state()->swsb = gen_swsb_regdist(1);

      /* Use indirect addressing to fetch the specified component. */
      if (brw_type_size_bytes(src.type) > 4 &&
          (intel_device_info_is_9lp(devinfo) || !devinfo->has_64bit_int)) {
         /* From the Cherryview PRM Vol 7. "Register Region Restrictions":
          *
          *   "When source or destination datatype is 64b or operation is
          *    integer DWord multiply, indirect addressing must not be
          *    used."
          *
          * We may also not support Q/UQ types.
          *
          * To work around both of these, we do two integer MOVs instead
          * of one 64-bit MOV.  Because no double value should ever cross
          * a register boundary, it's safe to use the immediate offset in
          * the indirect here to handle adding 4 bytes to the offset and
          * avoid the extra ADD to the register file.
          */
         append_MOV(subscript(dst, BRW_TYPE_D, 0),
                    retype(brw_vec1_indirect(addr.subnr, offset),
                           BRW_TYPE_D));
         current_state()->swsb = gen_swsb_null();
         append_MOV(subscript(dst, BRW_TYPE_D, 1),
                    retype(brw_vec1_indirect(addr.subnr, offset + 4),
                           BRW_TYPE_D));
      } else {
         append_MOV(dst,
                    retype(brw_vec1_indirect(addr.subnr, offset), src.type));
      }
   }

   pop_state();
}

void
brw_generator::generate_math(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1,
                             gen_math func)
{
   gen_inst *gen = append(BRW_OPCODE_MATH, dst, src0, src1);
   gen->math.func = func;

   /* This workaround says that we cannot use scalar broadcast with HF types.
    * However, for is_scalar values, all 16 elements contain the same value, so
    * we can replace a <0,1,0> region with <16,16,1> without ill effect.
    */
   if (intel_needs_workaround(devinfo, 22016140776)) {
      if (src0.is_scalar && src0.type == BRW_TYPE_HF) {
         gen->src[0].region = { 16, 16, 1 };
         gen->src[0].swizzle = BRW_SWIZZLE_XYZW;
      }

      if (src1.is_scalar && src1.type == BRW_TYPE_HF) {
         gen->src[1].region = { 16, 16, 1 };
         gen->src[1].swizzle = BRW_SWIZZLE_XYZW;
      }
   }
}

DEBUG_GET_ONCE_OPTION(shader_bin_override_path, "INTEL_SHADER_ASM_READ_PATH",
                      NULL);

static bool
brw_try_override_encoded_shader_binary(const struct intel_device_info *devinfo,
                                       void *mem_ctx,
                                       uint8_t **output,
                                       int *output_size,
                                       int start_offset,
                                       int old_binary_size,
                                       const char *read_path,
                                       const char *identifier,
                                       int *new_binary_size)
{
   assert(read_path != NULL);
   assert(start_offset >= 0);
   assert(old_binary_size >= 0);
   assert(output != NULL && *output != NULL);
   assert(output_size != NULL);
   assert(new_binary_size != NULL);
   assert(old_binary_size <= INT_MAX - start_offset);
   assert(start_offset + old_binary_size == *output_size);

   if (old_binary_size > INT_MAX - start_offset ||
       start_offset + old_binary_size != *output_size)
      return false;

   void *tmp_ctx = ralloc_context(NULL);
   char *name = ralloc_asprintf(tmp_ctx, "%s/%s.bin", read_path, identifier);

   size_t size = 0;
   char *override_data = os_read_file(name, &size);
   if (override_data == NULL) {
      ralloc_free(tmp_ctx);
      return false;
   }

   bool success = false;

   do {
      if (size == 0 || size > INT_MAX || size % sizeof(uint64_t) != 0)
         break;

      const int override_binary_size = (int)size;

      gen_scan_raw_layout_params layout = {
         .raw_bytes = override_data,
         .raw_bytes_size = override_binary_size,
         .num_insts = override_binary_size / (int)sizeof(uint64_t),
      };
      if (!gen_scan_raw_layout(&layout) ||
          layout.end_offset != override_binary_size ||
          layout.num_insts == 0)
         break;

      gen_decode_params decode_params = {
         .devinfo = devinfo,
         .raw_bytes = override_data,
         .raw_bytes_size = override_binary_size,
         .mem_ctx = tmp_ctx,
      };
      if (!gen_decode(&decode_params) ||
          decode_params.num_insts != layout.num_insts)
         break;

      gen_validate_params validate_params = {
         .devinfo = devinfo,
         .insts = decode_params.insts,
         .num_insts = decode_params.num_insts,
         .mem_ctx = tmp_ctx,
      };
      if (!gen_validate(&validate_params))
         break;

      if (override_binary_size > INT_MAX - start_offset)
         break;

      const int new_output_size = start_offset + override_binary_size;
      uint8_t *new_output =
         (uint8_t *)reralloc_array_size(mem_ctx, *output, 1,
                                        new_output_size);
      assert(new_output != NULL);

      memcpy(new_output + start_offset, override_data, override_binary_size);

      *output = new_output;
      *output_size = new_output_size;
      *new_binary_size = override_binary_size;
      success = true;
   } while (false);

   free(override_data);
   ralloc_free(tmp_ctx);
   return success;
}

/* The A32 messages take a buffer base address in header.5:[31:0] (See
 * MH1_A32_PSM for typed messages or MH_A32_GO for byte/dword scattered
 * and OWord block messages in the SKL PRM Vol. 2d for more details.)
 * Unfortunately, there are a number of subtle differences:
 *
 * For the block read/write messages:
 *
 *   - We always stomp header.2 to fill in the actual scratch address (in
 *     units of OWORDs) so we don't care what's in there.
 *
 *   - They rely on per-thread scratch space value in header.3[3:0] to do
 *     bounds checking so that needs to be valid.  The upper bits of
 *     header.3 are ignored, though, so we can copy all of g0.3.
 *
 *   - They ignore header.5[9:0] and assumes the address is 1KB aligned.
 *
 *
 * For the byte/dword scattered read/write messages:
 *
 *   - We want header.2 to be zero because that gets added to the per-channel
 *     offset in the non-header portion of the message.
 *
 *   - Contrary to what the docs claim, they don't do any bounds checking so
 *     the value of header.3[3:0] doesn't matter.
 *
 *   - They consider all of header.5 for the base address and header.5[9:0]
 *     are not ignored.  This means that we can't copy g0.5 verbatim because
 *     g0.5[9:0] contains the FFTID on most platforms.  Instead, we have to
 *     use an AND to mask off the bottom 10 bits.
 *
 *
 * For block messages, just copying g0 gives a valid header because all the
 * garbage gets ignored except for header.2 which we stomp as part of message
 * setup.  For byte/dword scattered messages, we can just zero out the header
 * and copy over the bits we need from g0.5.  This opcode, however, tries to
 * satisfy the requirements of both by starting with 0 and filling out the
 * information required by either set of opcodes.
 */
void
brw_generator::generate_scratch_header(brw_inst *inst,
                                       struct brw_reg dst,
                                       struct brw_reg src)
{
   assert(inst->exec_size == 8 && inst->force_writemask_all);
   assert(dst.file == FIXED_GRF);
   assert(src.file == FIXED_GRF);
   assert(src.type == BRW_TYPE_UD);

   dst.type = BRW_TYPE_UD;

   gen_inst *gen;

   gen = append_MOV(dst, brw_imm_ud(0));

   if (devinfo->ver >= 12)
      current_state()->swsb = gen_swsb_null();
   else
      gen->no_dd_clear = true;

   /* Copy the per-thread scratch space size from g0.3[3:0] */
   current_state()->exec_size = 1;

   gen = append(BRW_OPCODE_AND, suboffset(dst, 3), component(src, 3),
                brw_imm_ud(INTEL_MASK(3, 0)));
   if (devinfo->ver < 12) {
      gen->no_dd_clear = true;
      gen->no_dd_check = true;
   }

   /* Copy the scratch base address from g0.5[31:10] */
   gen = append(BRW_OPCODE_AND, suboffset(dst, 5), component(src, 5),
                brw_imm_ud(INTEL_MASK(31, 10)));
   if (devinfo->ver < 12)
      gen->no_dd_check = true;
}

void
brw_generator::generate_float_controls_mode(unsigned mode, unsigned mask)
{
   assert(current_state()->no_mask == true);

   /* From the Skylake PRM, Volume 7, page 760:
    *  "Implementation Restriction on Register Access: When the control
    *   register is used as an explicit source and/or destination, hardware
    *   does not ensure execution pipeline coherency. Software must set the
    *   thread control field to ‘switch’ for an instruction that uses
    *   control register as an explicit operand."
    *
    * On Gfx12+ this is implemented in terms of SWSB annotations instead.
    */
   current_state()->swsb = gen_swsb_regdist(1);

   gen_inst *inst = append(BRW_OPCODE_AND,
                           brw_cr0_reg(0),
                           brw_cr0_reg(0),
                           brw_imm_ud(~mask));
   inst->exec_size = 1;

   if (devinfo->ver < 12)
      inst->thread_control = BRW_THREAD_SWITCH;

   if (mode) {
      gen_inst *inst_or = append(BRW_OPCODE_OR,
                                 brw_cr0_reg(0),
                                 brw_cr0_reg(0),
                                 brw_imm_ud(mode));
      inst_or->exec_size = 1;
      if (devinfo->ver < 12)
         inst_or->thread_control = BRW_THREAD_SWITCH;
   }

   if (devinfo->ver >= 12)
      append_SYNC(GEN_SYNC_NOP);
}

void
brw_generator::enable_debug(const char *shader_name)
{
   debug_flag = true;
   this->shader_name = shader_name;
}

int
brw_generator::generate_code(const brw_shader &s,
                             struct genisa_stats *stats)
{
   const int dispatch_width = s.dispatch_width;
   struct brw_shader_stats shader_stats = s.shader_stats;
   const brw_performance &perf = s.performance_analysis.require();

   this->dispatch_width = dispatch_width;
   this->final_halt_idx = -1;
   this->needs_final_halt = false;

   int loop_count = 0, send_count = 0, nop_count = 0, sync_nop_count = 0;
   bool is_accum_used = false;

   const bool annotate = INTEL_DEBUG(DEBUG_ANNOTATION) &&
                         (debug_flag || params->archiver);

   std::vector<std::pair<unsigned, unsigned>> if_stack;
   std::vector<unsigned> loop_stack;
   const int expected_start_offset = align(output_size, 64);
#ifndef NDEBUG
   std::vector<const char *> label_annotations;
#endif

   gen_insts.reserve(s.cfg->total_instructions * 5 / 4);

   brw_inst *prev_inst = NULL;
   int current_block_num = -1;
   foreach_block_and_inst (block, brw_inst, inst, s.cfg) {
      if (block->num != current_block_num) {
         current_block_num = block->num;

#ifndef NDEBUG
         /* Label the block's first gen_inst.  An empty block's label is
          * overwritten by the next block.
          */
         const unsigned idx = gen_insts.size();
         if (idx >= label_annotations.size())
            label_annotations.resize(idx + 1, NULL);
         label_annotations[idx] =
            ralloc_asprintf(mem_ctx, "%u cycles",
                            perf.block_latency[block->num]);
#endif
      }

      if (inst->opcode == SHADER_OPCODE_UNDEF ||
          inst->opcode == SHADER_OPCODE_FLOW)
         continue;

      reset_state();

#ifndef NDEBUG
      if (unlikely(annotate))
         next_annotation = NULL;
#endif

      struct brw_reg src[4], dst;
      unsigned int last_insn_offset = gen_insts.size();
      bool multiple_instructions_emitted = false;
      gen_swsb swsb = inst->sched;

      /* From the Broadwell PRM, Volume 7, "3D-Media-GPGPU", in the
       * "Register Region Restrictions" section: for BDW, SKL:
       *
       *    "A POW/FDIV operation must not be followed by an instruction
       *     that requires two destination registers."
       *
       * The documentation is often lacking annotations for Atom parts,
       * and empirically this affects CHV as well.
       */
      if (devinfo->ver <= 9 &&
          !gen_insts.empty() &&
          gen_insts.back().opcode == GEN_OP_MATH &&
          gen_insts.back().math.func == GEN_MATH_POW &&
          inst->dst.component_size(inst->exec_size) > REG_SIZE) {
         append_NOP();
         last_insn_offset = gen_insts.size();

         /* In order to avoid spurious instruction count differences when the
          * instruction schedule changes, keep track of the number of inserted
          * NOPs.
          */
         nop_count++;
      }

      /* Wa_14010017096:
       *
       * Clear accumulator register before end of thread.
       */
      if (inst->eot && is_accum_used &&
          intel_needs_workaround(devinfo, 14010017096)) {

         gen_inst gen = make_empty();
         gen.opcode = GEN_OP_MOV;
         gen.exec_size = 16;
         gen.no_mask = true;
         gen.swsb = gen_swsb_src_dep(swsb);
         gen.dst = to_gen(brw_acc_reg(8));
         gen.src[0] = to_gen(brw_imm_f(0.0f));
         append(gen);

         last_insn_offset = gen_insts.size();
         swsb = gen_swsb_dst_dep(swsb, 1);
      }

      if (!is_accum_used && !inst->eot) {
         is_accum_used = inst->writes_accumulator_implicitly(devinfo) ||
                         inst->dst.is_accumulator();
      }

      /* Wa_14013672992:
       *
       * Always use @1 SWSB for EOT.
       */
      if (inst->eot && intel_needs_workaround(devinfo, 14013672992)) {
         if (gen_swsb_src_dep(swsb).mode) {
            gen_inst gen = make_empty();
            gen.opcode = GEN_OP_SYNC;
            gen.exec_size = 1;
            gen.no_mask = true;
            gen.swsb = gen_swsb_src_dep(swsb);
            gen.dst = gen_null();
            gen.src[0] = gen_null();
            gen.sync.func = GEN_SYNC_NOP;
            append(gen);

            last_insn_offset = gen_insts.size();
         }

         swsb = gen_swsb_dst_dep(swsb, 1);
      }

#ifndef NDEBUG
      if (unlikely(annotate))
         next_annotation = inst->annotation;
#endif

      if (devinfo->ver >= 20 && inst->group % 8 != 0) {
         assert(inst->force_writemask_all);
         assert(!inst->predicate && !inst->conditional_mod);
         assert(!inst->writes_accumulator_implicitly(devinfo) &&
                !inst->reads_accumulator_implicitly());
         assert(inst->opcode != SHADER_OPCODE_SEL_EXEC);
         current_state()->chan_offset = 0;
      } else {
         current_state()->chan_offset = inst->group;
      }

      /* For SEND_GATHER, the payload sources are represented inside the
       * scalar register in src[2], so we can skip them.
       */
      const unsigned num_sources =
         inst->opcode == SHADER_OPCODE_SEND_GATHER ? 3 : inst->sources;
      assert(num_sources <= ARRAY_SIZE(src));

      for (unsigned int i = 0; i < num_sources; i++) {
         src[i] = normalize_brw_reg_for_encoding(&inst->src[i]);
	 /* The accumulator result appears to get used for the
	  * conditional modifier generation.  When negating a UD
	  * value, there is a 33rd bit generated for the sign in the
	  * accumulator value, so now you can't check, for example,
	  * equality with a 32-bit value.  See piglit fs-op-neg-uvec4.
	  */
	 assert(!inst->conditional_mod ||
		inst->src[i].type != BRW_TYPE_UD ||
		!inst->src[i].negate);
      }
      dst = normalize_brw_reg_for_encoding(&inst->dst);

      {
         auto state = current_state();
         state->align16 = false;
         state->pred_control = (gen_predicate)inst->predicate;
         state->pred_inv = inst->predicate_inverse;

         /* On gfx7 and above, hardware automatically adds the group onto the
          * flag subregister number.
          */
         state->flag_nr = inst->flag_subreg >> 1;
         state->flag_subnr = inst->flag_subreg & 1;

         state->saturate = inst->saturate;
         state->no_mask = inst->force_writemask_all;

         if (devinfo->ver >= 20 && inst->writes_accumulator) {
            assert(inst->dst.is_accumulator() ||
                   inst->opcode == BRW_OPCODE_ADDC ||
                   inst->opcode == BRW_OPCODE_MACH ||
                   inst->opcode == BRW_OPCODE_SUBB);
         } else {
            state->acc_wr_control = inst->writes_accumulator;
         }

         state->swsb = swsb;
         state->exec_size = inst->exec_size;
      }

      assert(inst->force_writemask_all || inst->exec_size >= 4);
      assert(inst->force_writemask_all || inst->group % inst->exec_size == 0);
      if (const brw_send_inst *send = inst->as_send())
         assert(send->mlen <= BRW_MAX_MSG_LENGTH * reg_unit(devinfo));

      switch (inst->opcode) {
      case BRW_OPCODE_NOP:
         append_NOP();
         break;

      case BRW_OPCODE_SYNC:
         assert(src[0].file == IMM);
         append_SYNC(gen_sync_func(src[0].ud));

         if (tgl_sync_function(src[0].ud) == TGL_SYNC_NOP)
            ++sync_nop_count;

         break;

      case BRW_OPCODE_MOV:
      case BRW_OPCODE_FRC:
      case BRW_OPCODE_RNDD:
      case BRW_OPCODE_RNDE:
      case BRW_OPCODE_RNDZ:
      case BRW_OPCODE_NOT:
      case BRW_OPCODE_LZD:
	 append(inst->opcode, dst, src[0]);
	 break;

      case BRW_OPCODE_ADD:
      case BRW_OPCODE_MUL:
      case BRW_OPCODE_AVG:
      case BRW_OPCODE_MACH:
      case BRW_OPCODE_AND:
      case BRW_OPCODE_OR:
      case BRW_OPCODE_XOR:
      case BRW_OPCODE_ASR:
      case BRW_OPCODE_SHR:
      case BRW_OPCODE_SHL:
      case BRW_OPCODE_SEL:
      case BRW_OPCODE_ADDC:
      case BRW_OPCODE_SUBB:
      case BRW_OPCODE_MAC:
      case BRW_OPCODE_BFI1:
      case BRW_OPCODE_PLN:
      case BRW_OPCODE_SRND:
      case BRW_OPCODE_ROL:
      case BRW_OPCODE_ROR:
      case BRW_OPCODE_CMP:
      case BRW_OPCODE_CMPN:
         assert(inst->opcode != BRW_OPCODE_SRND || devinfo->ver >= 20);
         assert(inst->opcode != BRW_OPCODE_ROL || devinfo->ver >= 11);
         assert(inst->opcode != BRW_OPCODE_ROR || devinfo->ver >= 11);

	 append(inst->opcode, dst, src[0], src[1]);
	 break;

      case BRW_OPCODE_MAD:
      case BRW_OPCODE_CSEL:
      case BRW_OPCODE_BFE:
      case BRW_OPCODE_BFI2:
      case BRW_OPCODE_DP4A:
      case BRW_OPCODE_LRP:
      case BRW_OPCODE_ADD3:
         assert(inst->opcode != BRW_OPCODE_DP4A || devinfo->ver >= 12);
         assert(inst->opcode != BRW_OPCODE_LRP  || devinfo->ver == 9);
         assert(inst->opcode != BRW_OPCODE_ADD3 || devinfo->verx10 >= 125);

         if (devinfo->ver >= 12) {
            /* Having two immediate sources is allowed, but this should have been
             * converted to a regular ADD by brw_opt_algebraic.
             */
            assert(inst->opcode != BRW_OPCODE_ADD3 ||
                   !(src[0].file == IMM && src[2].file == IMM));
         }

         if (devinfo->ver == 9)
            current_state()->align16 = true;

         append(inst->opcode, dst, src[0], src[1], src[2]);
	 break;

      case BRW_OPCODE_DPAS: {
         assert(devinfo->verx10 >= 125);
         const brw_dpas_inst *dpas = inst->as_dpas();
         gen_inst *gen = append(BRW_OPCODE_DPAS, dst, src[0], src[1], src[2]);
         gen->dpas.sdepth = dpas->sdepth;
         gen->dpas.rcount = dpas->rcount;
         break;
      }

      case BRW_OPCODE_BFN: {
         gen_inst *gen = append(inst->opcode, dst, src[0], src[1], src[2]);
         gen->boolean_func_ctrl = src[3].ud;
         break;
      }

      case BRW_OPCODE_BFREV:
      case BRW_OPCODE_FBL:
      case BRW_OPCODE_CBIT:
         append(inst->opcode, retype(dst, BRW_TYPE_UD), retype(src[0], BRW_TYPE_UD));
         break;

      case BRW_OPCODE_FBH:
         append(inst->opcode, retype(dst, src[0].type), src[0]);
         break;

      case BRW_OPCODE_IF: {
         gen_inst *gen = append(inst->opcode);
         gen->chan_offset = 0;
         gen->pred_control = GEN_PREDICATE_NORMAL;
         gen->no_mask = false;

         /* UIP and JIP will be filled later. */

         const unsigned if_idx = gen_insts.size()-1;
         if_stack.push_back({if_idx, 0});
	 break;
      }

      case BRW_OPCODE_ELSE: {
         gen_inst *gen = append(inst->opcode);
         gen->chan_offset = 0;
         gen->no_mask = false;

         /* UIP and JIP will be filled later. */

         const unsigned else_idx = gen_insts.size()-1;
         if_stack.back().second = else_idx;
	 break;
      }

      case BRW_OPCODE_ENDIF: {
         assert(!if_stack.empty());

         int else_idx = if_stack.back().second;
         if_stack.pop_back();

         if (devinfo->ver == 9 && else_idx) {
            /* Insert a NOP to be specified as join instruction within the
             * ELSE block, which is valid for an ELSE instruction with
             * branch_ctrl on.  The ELSE instruction will be set to jump
             * here instead of to the ENDIF instruction, since attempting to
             * do the latter would prevent the ENDIF from being executed in
             * some cases due to Wa_220160235, which could cause the program
             * to continue running with all channels disabled.
             */
            append_NOP();
            gen_inst *else_inst = &gen_insts[else_idx];

            const unsigned nop_idx = gen_insts.size()-1;
            const unsigned endif_idx = nop_idx + 1;

            else_inst->src[0] = gen_imm_d(nop_idx);
            else_inst->src[1] = gen_imm_d(endif_idx);
            else_inst->branch_control = true;
         }

         append(inst->opcode);
	 break;
      }

      case BRW_OPCODE_DO:
         /* In Gfx9+ there's no actual hardware instruction for DO,
          * so just keep track that the next instruction will
          * start a loop.  Later WHILE will use this index.
          */
         loop_stack.push_back(gen_insts.size());
	 break;

      case BRW_OPCODE_BREAK:
      case BRW_OPCODE_CONTINUE:
         assert(!loop_stack.empty());
         append(inst->opcode);
	 break;

      case BRW_OPCODE_WHILE: {
         /* Workaround for an issue with branch prediction for WHILE
          * instructions that may lead to misrendering or GPU hangs.
          * See HSDs 22020521218 and 16026360541.
          */
         if (devinfo->ver >= 20 && prev_inst &&
             unlikely(prev_inst->is_control_flow()))
            append_NOP();

         const unsigned header_idx = loop_stack.back();
         loop_stack.pop_back();

         // fprintf(stderr, "setting %d for idx=%d\n", header_idx, (int)gen_insts.size());

         gen_inst *while_inst = append(inst->opcode);
         while_inst->src[0] = gen_imm_d(header_idx);

         loop_count++;
         break;
      }

      case SHADER_OPCODE_RCP:
      case SHADER_OPCODE_RSQ:
      case SHADER_OPCODE_SQRT:
      case SHADER_OPCODE_EXP2:
      case SHADER_OPCODE_LOG2:
      case SHADER_OPCODE_SIN:
      case SHADER_OPCODE_COS:
         assert(inst->conditional_mod == BRW_CONDITIONAL_NONE);
         generate_math(dst, src[0], retype(brw_null_reg(), src[0].type),
                       gen_math_func_for_opcode(inst->opcode));
	 break;

      case SHADER_OPCODE_INT_QUOTIENT:
      case SHADER_OPCODE_INT_REMAINDER:
      case SHADER_OPCODE_POW:
         assert(devinfo->verx10 < 125);
         assert(inst->conditional_mod == BRW_CONDITIONAL_NONE);
         assert(inst->opcode == SHADER_OPCODE_POW || inst->exec_size == 8);
         generate_math(dst, src[0], src[1], gen_math_func_for_opcode(inst->opcode));
	 break;

      case FS_OPCODE_PIXEL_X:
         assert(src[0].type == BRW_TYPE_UW);
         assert(src[1].type == BRW_TYPE_UW);
         src[0].subnr = 0 * brw_type_size_bytes(src[0].type);
         if (src[1].file == IMM) {
            assert(src[1].ud == 0);
            append_MOV(dst, stride(src[0], 8, 4, 1));
         } else {
            /* Coarse pixel case */
            append(BRW_OPCODE_ADD, dst, stride(src[0], 8, 4, 1), src[1]);
         }
         break;
      case FS_OPCODE_PIXEL_Y:
         assert(src[0].type == BRW_TYPE_UW);
         assert(src[1].type == BRW_TYPE_UW);
         src[0].subnr = 4 * brw_type_size_bytes(src[0].type);
         if (src[1].file == IMM) {
            assert(src[1].ud == 0);
            append_MOV(dst, stride(src[0], 8, 4, 1));
         } else {
            /* Coarse pixel case */
            append(BRW_OPCODE_ADD, dst, stride(src[0], 8, 4, 1), src[1]);
         }
         break;

      case SHADER_OPCODE_SEND:
         generate_send(inst->as_send(), dst, src[SEND_SRC_DESC], src[SEND_SRC_EX_DESC],
                       src[SEND_SRC_PAYLOAD1], src[SEND_SRC_PAYLOAD2],
                       inst->as_send()->bindless_surface &&
                       intel_has_extended_bindless(devinfo));
         send_count++;
         break;

      case SHADER_OPCODE_SEND_GATHER:
         generate_send(inst->as_send(), dst,
                       src[SEND_GATHER_SRC_DESC], src[SEND_GATHER_SRC_EX_DESC],
                       src[SEND_GATHER_SRC_SCALAR], brw_null_reg(),
                       inst->as_send()->bindless_surface &&
                       intel_has_extended_bindless(devinfo));
         send_count++;
         break;

      case FS_OPCODE_DDX_COARSE:
      case FS_OPCODE_DDX_FINE:
         generate_ddx(inst, dst, src[0]);
         break;
      case FS_OPCODE_DDY_COARSE:
      case FS_OPCODE_DDY_FINE:
         generate_ddy(inst, dst, src[0]);
	 break;

      case SHADER_OPCODE_SCRATCH_HEADER:
         generate_scratch_header(inst, dst, src[0]);
         break;

      case SHADER_OPCODE_MOV_INDIRECT:
         generate_mov_indirect(inst, dst, src[0], src[1]);
         break;

      case SHADER_OPCODE_MOV_RELOC_IMM: {
         assert(src[0].file == IMM);
         assert(src[1].file == IMM);

         append_reloc({
            .id = src[0].ud,
            .type = INTEL_SHADER_RELOC_TYPE_MOV_IMM,
            .offset = 16u * (unsigned)gen_insts.size() + expected_start_offset,
            .delta = src[1].ud,
         });

         append_MOV(dst, retype(brw_imm_ud(GEN_UNCOMPACTABLE_PATCH_IMM), dst.type));

         break;
      }

      case BRW_OPCODE_HALT:
         /* The UIP and JIP will be filled later. */
         this->needs_final_halt = true;
         append(inst->opcode);
         break;

      case FS_OPCODE_SCHEDULING_FENCE:
         if (inst->sources == 0 && swsb.regdist == 0 &&
                                   swsb.mode == GEN_SBID_NULL) {
            break;
         }

         if (devinfo->ver >= 12) {
            /* Use the available SWSB information to stall.  A single SYNC is
             * sufficient since if there were multiple dependencies, the
             * scoreboard algorithm already injected other SYNCs before this
             * instruction.
             */
            append_SYNC(GEN_SYNC_NOP);
         } else {
            for (unsigned i = 0; i < inst->sources; i++) {
               /* Emit a MOV to force a stall until the instruction producing the
                * registers finishes.
                */
               append(BRW_OPCODE_MOV, retype(brw_null_reg(), BRW_TYPE_UW),
                                      retype(src[i], BRW_TYPE_UW));
            }

            if (inst->sources > 1)
               multiple_instructions_emitted = true;
         }

         break;

      case SHADER_OPCODE_FIND_LIVE_CHANNEL:
      case SHADER_OPCODE_FIND_LAST_LIVE_CHANNEL:
      case SHADER_OPCODE_LOAD_LIVE_CHANNELS:
         UNREACHABLE("Should be lowered by lower_find_live_channel()");
         break;

      case FS_OPCODE_LOAD_LIVE_CHANNELS: {
         assert(inst->force_writemask_all && inst->group == 0);
         assert(inst->dst.file == BAD_FILE);
         current_state()->exec_size = 1;
         current_state()->swsb = gen_swsb_dst_dep(swsb, 1);
         append(BRW_OPCODE_MOV,
                retype(brw_flag_subreg(inst->flag_subreg), BRW_TYPE_UD),
                retype(brw_mask_reg(0), BRW_TYPE_UD));
         /* Reading certain ARF registers (like 'ce', the mask register) on
          * Gfx12+ requires requires a dependency on all pipes on the read
          * instruction and the next instructions
          */
         if (devinfo->ver >= 12)
            append_SYNC(GEN_SYNC_NOP);
         break;
      }
      case SHADER_OPCODE_BROADCAST:
         assert(current_state()->align16 == false);
         generate_broadcast(dst, src[0], src[1]);
         break;

      case SHADER_OPCODE_SHUFFLE:
         generate_shuffle(inst, dst, src[0], src[1]);
         break;

      case SHADER_OPCODE_SEL_EXEC: {
         assert(inst->force_writemask_all);
         assert(devinfo->has_64bit_float || brw_type_size_bytes(dst.type) <= 4);

         gen_inst *gen;

         gen = append_MOV(dst, src[1]);
         gen->no_mask = true;

         gen = append_MOV(dst, src[0]);
         gen->no_mask = false;
         gen->swsb = gen_swsb_null();

         break;
      }

      case SHADER_OPCODE_QUAD_SWIZZLE:
         assert(src[1].file == IMM);
         assert(src[1].type == BRW_TYPE_UD);
         generate_quad_swizzle(inst, dst, src[0], src[1].ud);
         break;

      case SHADER_OPCODE_CLUSTER_BROADCAST: {
         assert((!intel_device_info_is_9lp(devinfo) &&
                 devinfo->has_64bit_float) || brw_type_size_bytes(src[0].type) <= 4);
         assert(!src[0].negate && !src[0].abs);
         assert(src[1].file == IMM);
         assert(src[1].type == BRW_TYPE_UD);
         assert(src[2].file == IMM);
         assert(src[2].type == BRW_TYPE_UD);
         const unsigned component = src[1].ud;
         const unsigned cluster_size = src[2].ud;
         assert(inst->src[0].file != ARF);

         unsigned s;
         if (inst->src[0].file == FIXED_GRF) {
            s = inst->src[0].hstride ? 1 << (inst->src[0].hstride - 1) : 0;
         } else {
            s = inst->src[0].stride;
         }
         unsigned vstride = cluster_size * s;
         unsigned width = cluster_size;

         /* The maximum exec_size is 32, but the maximum width is only 16. */
         if (inst->exec_size == width) {
            vstride = 0;
            width = 1;
         }

         struct brw_reg strided = stride(suboffset(src[0], component * s),
                                         vstride, width, 0);
         append_MOV(dst, strided);
         break;
      }

      case SHADER_OPCODE_HALT_TARGET:
         /* This is the place where the final HALT needs to be inserted if
          * we've emitted any discards.  If not, this will emit no code.
          */
         if (!this->needs_final_halt)
            break;

         /* HALT temporarily disables channels, and the same instruction
          * is used to re-enable them: once all channels are
          * disabled, then they are re-enabled again immediately.
          *
          * So put a HALT right before the "epilogue" of the shader to make
          * sure all channels get HALTed, so that this last HALT will re-enable
          * them again.
          */
         final_halt_idx = gen_insts.size();
         append(BRW_OPCODE_HALT);

         if (devinfo->ver >= 12) {
            /* This works around synchronization issues consequence of the
             * HALT instruction not being considered a control flow
             * instruction by the back-end -- The fact that it doesn't
             * cause the CFG pass to introduce an edge in the graph means
             * that the software scoreboard pass is completely blind to the
             * effect of discard jumps on control flow, so it doesn't
             * introduce the required annotations to avoid data hazards
             * when the discard path of the CFG is taken.  Note that
             * because of the very limited set of instructions that can
             * follow the HALT target in a fragment shader this was very
             * unlikely to lead to issues in practice, but starting on xe3
             * it appears to have become far more likely due to the use of
             * SENDG, since SENDG requires the scalar register to be set
             * prior to the submission of the render target write payloads,
             * which can easily lead to a WaR hazard if there was another
             * SENDG before the HALT jump that wasn't done reading out its
             * payload from the GRF.
             *
             * In an ideal world this would be avoided by having HALT be a
             * normal control flow instruction represented as an edge in
             * the control flow graph -- But unfortunately that would
             * prevent the optimizations we currently do that take
             * advantage of the ability of reordering code past the HALT
             * instruction, so it would have a pretty large performance
             * cost.  Instead this simply adds a SYNC.ALLWR instruction
             * after the HALT target to guarantee that all pending SEND
             * messages have finished execution -- That may also seem
             * costly, however its cost in practice appears to be minimal
             * since at the point of the program when the target HALT is
             * executed there is almost nothing left to do other than send
             * out the render target write payloads, so any pending
             * operations had to be waited on at roughly this point of the
             * program regardless.
             */
            append_SYNC(GEN_SYNC_ALLWR);
         }
         break;

      case SHADER_OPCODE_BARRIER:
	 generate_barrier(inst, src[0]);
         send_count++;
	 break;

      case SHADER_OPCODE_RND_MODE: {
         assert(src[0].file == IMM);
         /*
          * Changes the floating point rounding mode updating the control
          * register field defined at cr0.0[5-6] bits.
          */
         enum brw_rnd_mode mode =
            (enum brw_rnd_mode) (src[0].d << BRW_CR0_RND_MODE_SHIFT);
         generate_float_controls_mode(mode, BRW_CR0_RND_MODE_MASK);
         break;
      }

      case SHADER_OPCODE_FLOAT_CONTROL_MODE:
         assert(src[0].file == IMM);
         assert(src[1].file == IMM);
         generate_float_controls_mode(src[0].d, src[1].d);
         break;

      case SHADER_OPCODE_READ_ARCH_REG:
         if (devinfo->ver >= 12) {
            auto state = current_state();
            /* There is a SWSB restriction that requires that any time sr0 is
             * accessed both the instruction doing the access and the next one
             * have SWSB set to RegDist(1).
             */
            if (state->swsb.mode != GEN_SBID_NULL)
               append_SYNC(GEN_SYNC_NOP);
            state->swsb = gen_swsb_regdist(1);
            append(BRW_OPCODE_MOV, dst, src[0]);
            state->swsb = gen_swsb_regdist(1);
            append(BRW_OPCODE_AND, dst, dst, brw_imm_ud(0xffffffff));
         } else {
            append(BRW_OPCODE_MOV, dst, src[0]);
         }
         break;

      default:
         UNREACHABLE("Unsupported opcode");

      case SHADER_OPCODE_LOAD_PAYLOAD:
         UNREACHABLE("Should be lowered by lower_load_payload()");
      }
      prev_inst = inst;

      if (multiple_instructions_emitted)
         continue;

      if (inst->conditional_mod) {
         assert(last_insn_offset == gen_insts.size() - 1 ||
                !"conditional_mod for IR "
                 "emitting more than 1 instruction");
         gen_inst *gen = &gen_insts.back();
         gen->cmod = (gen_condition)inst->conditional_mod;
      }

      /* When enabled, insert sync NOP after every instruction and make sure
       * that current instruction depends on the previous instruction.
       */
      if (INTEL_DEBUG(DEBUG_SWSB_STALL) && devinfo->ver >= 12) {
         current_state()->swsb = gen_swsb_regdist(1);
         /* TODO: Consider syncing with any SBIDs that were added above. */
         append_SYNC(GEN_SYNC_NOP);
      }
   }

#ifndef NDEBUG
   /* Pad with NULLs so annotations same size as gen_insts. */
   label_annotations.resize(gen_insts.size(), NULL);
#endif

   /* TODO: See if we can move those normalizations elsewhere. */
   for (auto &gen : gen_insts) {
      if (!gen.align16 && gen.exec_size == 1) {
         if (gen.src[0].file != GEN_BAD_FILE && gen.src[0].region.width == 1)
            gen.src[0].region = { 0, 1, 0 };
         if (gen.src[1].file != GEN_BAD_FILE && gen.src[1].region.width == 1)
            gen.src[1].region = { 0, 1, 0 };

      }
      if ((gen.dst.file == GEN_GRF || gen.dst.file == GEN_ARF) &&
           gen.dst.region.hstride == 0) {
         gen.dst.region.hstride = 1;
      }
   }

   /* Translate any pre-filled IMM branch sources from absolute indices
    * into the relative byte offsets.
    */
   for (int idx = 0; idx < (int)gen_insts.size(); idx++) {
      gen_inst *gen = &gen_insts[idx];
      const int jip_src = gen_inst_jip_src_index(gen->opcode);
      if (jip_src >= 0 && gen->src[jip_src].file == GEN_IMM)
         gen->src[jip_src].imm = 16 * ((int32_t)gen->src[jip_src].imm - idx);
      const int uip_src = gen_inst_uip_src_index(gen->opcode);
      if (uip_src >= 0 && gen->src[uip_src].file == GEN_IMM)
         gen->src[uip_src].imm = 16 * ((int32_t)gen->src[uip_src].imm - idx);
   }

   gen_finish_structured_cf(gen_insts.data(), gen_insts.size(), final_halt_idx);

   /* `send_count` explicitly does not include spills or fills, as we'd
    * like to use it as a metric for intentional memory access or other
    * shared function use.  Otherwise, subtle changes to scheduling or
    * register allocation could cause it to fluctuate wildly - and that
    * effect is already counted in spill/fill counts.
    */
   send_count -= shader_stats.spill_count;
   send_count -= shader_stats.fill_count;

   bool needs_validation = debug_flag;
#ifndef NDEBUG
   needs_validation = true;
#endif

   if (unlikely(needs_validation)) {
      gen_validate_params val_params = {
         .devinfo   = devinfo,
         .insts = gen_insts.data(),
         .num_insts = (int)gen_insts.size(),
         .mem_ctx   = params->mem_ctx,
      };

      bool validated = gen_validate(&val_params);
      if (!validated) {
         if (!debug_flag) {
            fprintf(stderr,
                  "Validation failed. Rerun with INTEL_DEBUG=shaders to get more information.\n");
         } else {
            gen_print_params print_params = {
               .devinfo = devinfo,
               .insts = gen_insts.data(),
               .num_insts = (int)gen_insts.size(),
               .errors = val_params.errors,
               .num_errors = val_params.num_errors,
#ifndef NDEBUG
               .annotations = annotations.data(),
               .label_annotations = label_annotations.data(),
#endif
            };
            gen_print(&print_params);
            fprintf(stderr, "Validation failed. See inline errors above.\n");
         }
      }

      assert(validated);
   }

   /* Ensure shaders start at 64 byte boundary. */
   int uncompact_size = (int)gen_insts.size() * 16;
   int start_offset = allocate_output(uncompact_size, 64);
   assert(start_offset == expected_start_offset);

   gen_raw_inst *start = (gen_raw_inst *)(output + start_offset);
   int *enc_offsets = num_relocs > 0 ?
      ralloc_array(mem_ctx, int, gen_insts.size()) : NULL;

   gen_encode_params enc_params = {
      .devinfo = devinfo,
      .compact_all = !INTEL_DEBUG(DEBUG_NO_COMPACTION),

      /* Will explicitly call validation later. */
      .skip_validation = true,

      .insts = gen_insts.data(),
      .num_insts = (int)gen_insts.size(),

      .mem_ctx = mem_ctx,
      .raw_bytes = (void *)start,
      .raw_bytes_size = uncompact_size,
      .encoded_offsets = enc_offsets,
   };

   const int before_size = enc_params.raw_bytes_size;

   bool encoded = gen_encode(&enc_params);
   assert(encoded);

   assert(uncompact_size >= enc_params.raw_bytes_size);
   /* Reduce size based on compact */
   output_size -= uncompact_size - enc_params.raw_bytes_size;

   int after_size = enc_params.raw_bytes_size;

   /* Update relocs based on compaction */
   for (int i = 0; i < num_relocs; i++) {
      if (relocs[i].offset < (unsigned)start_offset)
         continue;

      int inst_num = (relocs[i].offset - start_offset) / sizeof(gen_raw_inst);
      assert(inst_num <= enc_params.num_insts);
      int delta = inst_num == enc_params.num_insts ?
         after_size - before_size :
         enc_offsets[inst_num] - (sizeof(gen_raw_inst) * inst_num);
      relocs[i].offset += delta;
   }

   ralloc_free(enc_offsets);
   enc_offsets = NULL;

   bool dump_shader_bin = brw_should_dump_shader_bin();
   unsigned char blake3[BLAKE3_KEY_LEN + 1];
   char blake3buf[BLAKE3_HEX_LEN];

   auto override_path = debug_get_option_shader_bin_override_path();
   if (unlikely(debug_flag || dump_shader_bin || override_path != NULL ||
                params->archiver)) {
      _mesa_blake3_compute(enc_params.raw_bytes, after_size, blake3);
      _mesa_blake3_format(blake3buf, blake3);
   }

   if (unlikely(dump_shader_bin))
      brw_dump_shader_bin(enc_params.raw_bytes, 0, after_size, blake3buf);

   if (unlikely(override_path != NULL &&
                brw_try_override_encoded_shader_binary(devinfo, mem_ctx,
                                                       &output, &output_size,
                                                       start_offset, after_size,
                                                       override_path, blake3buf,
                                                       &after_size))) {
      fprintf(stderr, "Successfully overrode shader with blake3 %s\n", blake3buf);
      /* gen_insts and the gathered statistics describe the original shader.
       * Once replaced by an encoded binary override, they are no longer a
       * reliable source for disassembly or statistics output.
       */
      if (debug_flag) {
         fprintf(stderr, "Skipping disassembly and statistics "
                 "output for this shader.\n\n");
      }
      gen_insts.clear();
#ifndef NDEBUG
      annotations.clear();
#endif
      return start_offset;
   }

   if (unlikely(debug_flag || params->archiver)) {
      FILE *files[2] = { NULL, NULL };

      if (debug_flag && (!intel_shader_dump_filter ||
                         (intel_shader_dump_filter && intel_shader_dump_filter == params->source_hash)))
         files[0] = stderr;

      if (params->archiver) {
         const char *filename =
            ralloc_asprintf(mem_ctx, "GEN%d/0", dispatch_width);
         files[1] = debug_archiver_start_file(params->archiver, filename);
      }

      for (unsigned i = 0; i < ARRAY_SIZE(files); i++) {
         if (!files[i]) continue;
         fprintf(files[i], "Native code for %s (src_hash 0x%016" PRIx64 ") (blake3 %s)\n"
                 "SIMD%d shader: %d instructions. %d loops. %u cycles. "
                 "%d:%d spills:fills, %u sends, "
                 "scheduled with mode %s. "
                 "Promoted %u constants. "
                 "GRF registers: %u. "
                 "Non-SSA regs (after NIR): %u. "
                 "Compacted %d to %d bytes (%.0f%%)\n",
                 shader_name, params->source_hash, blake3buf,
                 dispatch_width,
                 before_size / 16 - nop_count - sync_nop_count,
                 loop_count, perf.latency,
                 shader_stats.spill_count,
                 shader_stats.fill_count,
                 send_count,
                 shader_stats.scheduler_mode,
                 shader_stats.promoted_constants,
                 s.grf_used,
                 shader_stats.non_ssa_registers_after_nir,
                 before_size, after_size,
                 100.0f * (before_size - after_size) / before_size);

         gen_print_params print_params = {
            .devinfo = devinfo,
            .fp = files[i],
            .insts = gen_insts.data(),
            .num_insts = (int)gen_insts.size(),
#ifndef NDEBUG
            .annotations = annotations.data(),
            .label_annotations = label_annotations.data(),
#endif
            .raw_bytes = enc_params.raw_bytes,
            .raw_bytes_size = after_size,
         };
         gen_print(&print_params);
      }

      if (params->archiver) {
         debug_archiver_finish_file(params->archiver);
      }
   }

   brw_shader_debug_log(compiler, params->log_data,
                        "%s SIMD%d shader: %d inst, %d loops, %u cycles, "
                        "%d:%d spills:fills, %u sends, "
                        "scheduled with mode %s, "
                        "Promoted %u constants, "
                        "compacted %d to %d bytes.\n",
                        _mesa_shader_stage_to_abbrev(stage),
                        dispatch_width,
                        before_size / 16 - nop_count - sync_nop_count,
                        loop_count, perf.latency,
                        shader_stats.spill_count,
                        shader_stats.fill_count,
                        send_count,
                        shader_stats.scheduler_mode,
                        shader_stats.promoted_constants,
                        before_size, after_size);

   if (stats) {
      stats->dispatch_width = dispatch_width;
      stats->max_polygons = s.max_polygons;
      stats->instrs = before_size / 16 - nop_count - sync_nop_count;
      stats->code_size = after_size;
      stats->sends = send_count;
      stats->loops = loop_count;
      stats->cycles = perf.latency;
      stats->spills = shader_stats.spill_count;
      stats->fills = shader_stats.fill_count;
      stats->max_live_registers = shader_stats.max_register_pressure;
      stats->non_ssa_regs_after_nir = shader_stats.non_ssa_registers_after_nir;
      stats->source_hash = prog_data->source_hash;
      stats->grf_registers = devinfo->ver >= 30 ? s.grf_used : 0;
      stats->scheduler_mode = shader_stats.scheduler_mode;

      switch (stage) {
      case MESA_SHADER_VERTEX:
      case MESA_SHADER_TESS_CTRL:
      case MESA_SHADER_TESS_EVAL:
      case MESA_SHADER_GEOMETRY:
      case MESA_SHADER_FRAGMENT:
         stats->push_constant_ranges = 0;
         stats->push_constant_registers = 0;
         for (uint32_t i = 0; i < 4; i++) {
            stats->push_constant_ranges += prog_data->push_sizes[i] != 0;
            stats->push_constant_registers +=
               DIV_ROUND_UP(prog_data->push_sizes[i], reg_unit(devinfo) * REG_SIZE);
         }
         break;

      case MESA_SHADER_COMPUTE:
      case MESA_SHADER_KERNEL:
         /* Pre Gfx12.5, there is only one push constant buffer for compute
          * shaders, post Gfx12.5 the shader has to pull the constant data.
          */
         stats->push_constant_ranges =
            devinfo->verx10 < 125 ? (prog_data->push_sizes[0] != 0) : 0;
         stats->push_constant_registers =
            devinfo->verx10 < 125 ?
            DIV_ROUND_UP(prog_data->push_sizes[0], reg_unit(devinfo) * REG_SIZE) : 0;
         break;

      case MESA_SHADER_MESH:
      case MESA_SHADER_TASK:
      case MESA_SHADER_RAYGEN:
      case MESA_SHADER_ANY_HIT:
      case MESA_SHADER_CLOSEST_HIT:
      case MESA_SHADER_MISS:
      case MESA_SHADER_INTERSECTION:
      case MESA_SHADER_CALLABLE:
         stats->push_constant_ranges = 0;
         stats->push_constant_registers = 0;
         break;

      default:
         UNREACHABLE("invalid stage");
      }

      /* Report the max dispatch width only on the smallest SIMD variant.
       *
       * XXX: SIMD8 is not the smallest on Xe2. This logic should be adjusted.
       */
      if (stage != MESA_SHADER_FRAGMENT || dispatch_width == 8)
         stats->max_dispatch_width = dispatch_width;
      else
         stats->max_dispatch_width = 0;

      if (mesa_shader_stage_uses_workgroup(stage))
         stats->workgroup_memory_size = prog_data->total_shared;
      else
         stats->workgroup_memory_size = 0;
   }

   gen_insts.clear();
#ifndef NDEBUG
   annotations.clear();
#endif

   return start_offset;
}

void
brw_generator::add_const_data(void *data, unsigned size)
{
   assert(prog_data->const_data_size == 0);

   if (size > 0) {
      prog_data->const_data_size = size;
      prog_data->const_data_offset = append_output(data, size, 32);
   }
}

void
brw_generator::add_resume_sbt(unsigned num_resume_shaders, uint64_t *sbt)
{
   assert(brw_shader_stage_is_bindless(stage));
   struct brw_bs_prog_data *bs_prog_data = brw_bs_prog_data(prog_data);
   if (num_resume_shaders > 0) {
      bs_prog_data->resume_sbt_offset =
         append_output(sbt, num_resume_shaders * sizeof(uint64_t), 32);
      for (unsigned i = 0; i < num_resume_shaders; i++) {
         size_t offset = bs_prog_data->resume_sbt_offset + i * sizeof(*sbt);
         assert(offset <= UINT32_MAX);
         append_reloc({
            .id = INTEL_SHADER_RELOC_SHADER_START_OFFSET,
            .type = INTEL_SHADER_RELOC_TYPE_U32,
            .offset = (uint32_t)offset,
            .delta = (uint32_t)sbt[i],
         });
      }
   }
}

const unsigned *
brw_generator::get_assembly()
{
   prog_data->relocs = relocs;
   prog_data->num_relocs = num_relocs;

   /* TODO: Check if we really need this padding at the end. */

   /* Align final size with instruction size. */
   allocate_output(0, 16);
   assert(output_size % 16 == 0);

   prog_data->program_size = output_size;

   return (unsigned *)output;
}

gen_inst
brw_generator::make_empty()
{
   return gen_inst{};
}

gen_inst
brw_generator::make(gen_opcode op)
{
   gen_inst gen = make_empty();

   const auto state = current_state();
   gen.exec_size      = state->exec_size;
   gen.chan_offset    = state->chan_offset;
   gen.flag_nr        = state->flag_nr;
   gen.flag_subnr     = state->flag_subnr;
   gen.pred_control   = state->pred_control;
   gen.pred_inv       = state->pred_inv;
   gen.no_mask        = state->no_mask;
   gen.saturate       = state->saturate;
   gen.align16        = state->align16;
   gen.acc_wr_control = state->acc_wr_control;
   gen.swsb           = state->swsb;

   gen.opcode = op;

   return gen;
}

gen_inst *
brw_generator::append(const gen_inst &gen)
{
   gen_insts.push_back(gen);
#ifndef NDEBUG
   annotations.push_back(next_annotation);
#endif
   return &gen_insts.back();
}

gen_inst *
brw_generator::append(enum opcode opcode)
{
   return append(make(to_gen(opcode)));
}

gen_inst *
brw_generator::append(enum opcode opcode, const brw_reg &dst,
                      const brw_reg &src0)
{
   gen_inst *gen = append(opcode);
   gen->dst    = to_gen(dst,  gen->align16);
   gen->src[0] = to_gen(src0, gen->align16);
   return gen;
}

gen_inst *
brw_generator::append(enum opcode opcode, const brw_reg &dst,
                      const brw_reg &src0, const brw_reg &src1)
{
   gen_inst *gen = append(opcode);
   gen->dst    = to_gen(dst,  gen->align16);
   gen->src[0] = to_gen(src0, gen->align16);
   gen->src[1] = to_gen(src1, gen->align16);
   return gen;
}

gen_inst *
brw_generator::append(enum opcode opcode, const brw_reg &dst,
                      const brw_reg &src0, const brw_reg &src1, const brw_reg &src2)
{
   gen_inst *gen = append(opcode);
   gen->dst    = to_gen(dst,  gen->align16);
   gen->src[0] = to_gen(src0, gen->align16);
   gen->src[1] = to_gen(src1, gen->align16);
   gen->src[2] = to_gen(src2, gen->align16);
   return gen;
}

gen_inst *
brw_generator::append_SYNC(gen_sync_func func)
{
   gen_inst gen = make(GEN_OP_SYNC);
   gen.dst      = gen_null();
   gen.src[0]   = gen_null();

   gen.sync.func = func;

   return append(gen);
}

gen_inst *
brw_generator::append_NOP()
{
   gen_inst nop = make_empty();
   nop.opcode = GEN_OP_NOP;
   nop.exec_size = 1;
   return append(nop);
}

int
brw_generator::allocate_output(unsigned size, unsigned alignment)
{
   assert(util_is_power_of_two_nonzero(alignment));

   unsigned padding = 0;
   if (output_size && (output_size % alignment != 0)) {
      padding = alignment - output_size % alignment;
   }

   const unsigned new_output_size = output_size + padding + size;
   output = (uint8_t *)reralloc_array_size(mem_ctx, output, 1, new_output_size);
   assert(output);

   /* Memset any padding due to alignment to 0.  We don't want to be hashing
    * or caching a bunch of random bits we got from a memory allocation.
    */
   memset(output + output_size, 0, padding);

   output_size = new_output_size;
   return new_output_size - size;
}

int
brw_generator::append_output(void *data, unsigned size, unsigned alignment)
{
   int offset = allocate_output(size, alignment);
   memcpy(output + offset, data, size);
   return offset;
}

void
brw_generator::append_reloc(const intel_shader_reloc &r)
{
   relocs = reralloc(mem_ctx, relocs, intel_shader_reloc, num_relocs + 1);
   relocs[num_relocs++] = r;
}

static uint64_t
brw_bsr(const struct intel_device_info *devinfo,
        uint32_t offset, uint8_t simd_size, uint8_t local_arg_offset,
        uint8_t grf_used)
{
   assert(offset % 64 == 0);
   assert(simd_size == 8 || simd_size == 16);
   assert(local_arg_offset % 8 == 0);

   return ((uint64_t)ptl_register_blocks(grf_used) << 60) |
          offset |
          SET_BITS(simd_size == 8, 4, 4) |
          SET_BITS(local_arg_offset / 8, 2, 0);
}

static void
brw_to_binary_emit_shader(brw_generator &g,
                          const brw_to_binary_params *p,
                          brw_shader *shader,
                          struct genisa_stats *stats,
                          const char *resume_suffix)
{
   if (unlikely(shader->debug_enabled)) {
      const shader_info *info = &shader->nir->info;
      const char *debug_name =
         ralloc_asprintf(p->params->mem_ctx, "%s %s%s shader %s",
                         info->label ? info->label : "unnamed",
                         _mesa_shader_stage_to_string(shader->prog_data->stage),
                         resume_suffix ? resume_suffix : "",
                         info->name);
      g.enable_debug(debug_name);
   }

   shader->start_offset = g.generate_code(*shader, stats);
}

const unsigned *
brw_to_binary(const brw_to_binary_params *p)
{
   struct brw_stage_prog_data *prog_data = p->prog_data;
   assert(prog_data);

   brw_generator g(p->compiler, p->params, prog_data, prog_data->stage);

   struct genisa_stats *stats = p->params->stats;
   for (unsigned i = 0; i < BRW_TO_BINARY_MAX_SHADERS; i++) {
      brw_shader *shader = p->shaders[i];
      if (shader == NULL)
         continue;

      brw_to_binary_emit_shader(g, p, shader, stats, NULL);

      if (stats)
         stats++;
   }

   if (p->num_resume_shaders > 0) {
      assert(brw_shader_stage_is_bindless(prog_data->stage));
      assert(p->resume_shaders != NULL);

      uint64_t *resume_sbt = ralloc_array(p->params->mem_ctx,
                                          uint64_t, p->num_resume_shaders);
      for (unsigned i = 0; i < p->num_resume_shaders; i++) {
         brw_shader *shader = p->resume_shaders[i];
         assert(shader != NULL);

         const char *suffix = unlikely(shader->debug_enabled) ?
            ralloc_asprintf(p->params->mem_ctx, " resume(%u)", i) : NULL;
         brw_to_binary_emit_shader(g, p, shader, NULL, suffix);
         assert(shader->start_offset > 0);

         resume_sbt[i] = brw_bsr(p->compiler->devinfo, shader->start_offset,
                                 shader->dispatch_width, 0,
                                 shader->grf_used);
      }

      g.add_resume_sbt(p->num_resume_shaders, resume_sbt);
   }

   const nir_shader *nir = p->params->nir;
   if (nir->constant_data_size > 0 || p->extra_const_data_size > 0) {
      if (p->extra_const_data_size == 0) {
         g.add_const_data(nir->constant_data, nir->constant_data_size);
      } else {
         const unsigned const_data_aligned_size =
            align(nir->constant_data_size, 32);
         const unsigned total =
            const_data_aligned_size + p->extra_const_data_size;
         uint8_t *combined = (uint8_t *)rzalloc_size(p->params->mem_ctx, total);
         memcpy(combined, nir->constant_data, nir->constant_data_size);
         memcpy(combined + const_data_aligned_size,
                p->extra_const_data, p->extra_const_data_size);
         g.add_const_data(combined, total);
      }
   }

   return g.get_assembly();
}
