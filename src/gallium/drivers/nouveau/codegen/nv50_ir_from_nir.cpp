/*
 * Copyright 2017 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Karol Herbst <kherbst@redhat.com>
 */

#include "compiler/nir/nir.h"

#include "util/u_debug.h"

#include "codegen/nv50_ir.h"
#include "codegen/nv50_ir_from_common.h"
#include "codegen/nv50_ir_lowering_helper.h"
#include "codegen/nv50_ir_util.h"

#if __cplusplus >= 201103L
#include <unordered_map>
#else
#include <tr1/unordered_map>
#endif
#include <cstring>
#include <list>
#include <vector>

namespace {

#if __cplusplus >= 201103L
using std::hash;
using std::unordered_map;
#else
using std::tr1::hash;
using std::tr1::unordered_map;
#endif

using namespace nv50_ir;

int
type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

class Converter : public ConverterCommon
{
public:
   Converter(Program *, nir_shader *, nv50_ir_prog_info *);

   bool run();
private:
   typedef std::vector<LValue*> LValues;
   typedef unordered_map<unsigned, LValues> NirDefMap;
   typedef unordered_map<unsigned, nir_load_const_instr*> ImmediateMap;
   typedef unordered_map<unsigned, uint32_t> NirArrayLMemOffsets;
   typedef unordered_map<unsigned, BasicBlock*> NirBlockMap;

   CacheMode convert(enum gl_access_qualifier);
   TexTarget convert(glsl_sampler_dim, bool isArray, bool isShadow);
   LValues& convert(nir_alu_dest *);
   BasicBlock* convert(nir_block *);
   LValues& convert(nir_dest *);
   SVSemantic convert(nir_intrinsic_op);
   Value* convert(nir_load_const_instr*, uint8_t);
   LValues& convert(nir_register *);
   LValues& convert(nir_ssa_def *);

   ImgFormat convertGLImgFormat(GLuint);

   Value* getSrc(nir_alu_src *, uint8_t component = 0);
   Value* getSrc(nir_register *, uint8_t);
   Value* getSrc(nir_src *, uint8_t, bool indirect = false);
   Value* getSrc(nir_ssa_def *, uint8_t);

   // returned value is the constant part of the given source (either the
   // nir_src or the selected source component of an intrinsic). Even though
   // this is mostly an optimization to be able to skip indirects in a few
   // cases, sometimes we require immediate values or set some fileds on
   // instructions (e.g. tex) in order for codegen to consume those.
   // If the found value has not a constant part, the Value gets returned
   // through the Value parameter.
   uint32_t getIndirect(nir_src *, uint8_t, Value *&);
   uint32_t getIndirect(nir_intrinsic_instr *, uint8_t s, uint8_t c, Value *&);

   uint32_t getSlotAddress(nir_intrinsic_instr *, uint8_t idx, uint8_t slot);

   void setInterpolate(nv50_ir_varying *,
                       uint8_t,
                       bool centroid,
                       unsigned semantics);

   Instruction *loadFrom(DataFile, uint8_t, DataType, Value *def, uint32_t base,
                         uint8_t c, Value *indirect0 = NULL,
                         Value *indirect1 = NULL, bool patch = false);
   void storeTo(nir_intrinsic_instr *, DataFile, operation, DataType,
                Value *src, uint8_t idx, uint8_t c, Value *indirect0 = NULL,
                Value *indirect1 = NULL);

   bool isFloatType(nir_alu_type);
   bool isSignedType(nir_alu_type);
   bool isResultFloat(nir_op);
   bool isResultSigned(nir_op);

   DataType getDType(nir_alu_instr *);
   DataType getDType(nir_intrinsic_instr *);
   DataType getDType(nir_intrinsic_instr *, bool isSigned);
   DataType getDType(nir_op, uint8_t);

   std::vector<DataType> getSTypes(nir_alu_instr *);
   DataType getSType(nir_src &, bool isFloat, bool isSigned);

   operation getOperation(nir_intrinsic_op);
   operation getOperation(nir_op);
   operation getOperation(nir_texop);
   operation preOperationNeeded(nir_op);

   int getSubOp(nir_intrinsic_op);
   int getSubOp(nir_op);

   CondCode getCondCode(nir_op);

   bool assignSlots();
   bool parseNIR();

   bool visit(nir_alu_instr *);
   bool visit(nir_block *);
   bool visit(nir_cf_node *);
   bool visit(nir_deref_instr *);
   bool visit(nir_function *);
   bool visit(nir_if *);
   bool visit(nir_instr *);
   bool visit(nir_intrinsic_instr *);
   bool visit(nir_jump_instr *);
   bool visit(nir_load_const_instr*);
   bool visit(nir_loop *);
   bool visit(nir_ssa_undef_instr *);
   bool visit(nir_tex_instr *);

   // tex stuff
   Value* applyProjection(Value *src, Value *proj);
   unsigned int getNIRArgCount(TexInstruction::Target&);

   // image stuff
   uint16_t handleDeref(nir_deref_instr *, Value * & indirect, const nir_variable * &);
   CacheMode getCacheModeFromVar(const nir_variable *);

   nir_shader *nir;

   NirDefMap ssaDefs;
   NirDefMap regDefs;
   ImmediateMap immediates;
   NirArrayLMemOffsets regToLmemOffset;
   NirBlockMap blocks;
   unsigned int curLoopDepth;

   BasicBlock *exit;
   Value *zero;
   Instruction *immInsertPos;

   int clipVertexOutput;

   union {
      struct {
         Value *position;
      } fp;
   };
};

Converter::Converter(Program *prog, nir_shader *nir, nv50_ir_prog_info *info)
   : ConverterCommon(prog, info),
     nir(nir),
     curLoopDepth(0),
     clipVertexOutput(-1)
{
   zero = mkImm((uint32_t)0);
}

BasicBlock *
Converter::convert(nir_block *block)
{
   NirBlockMap::iterator it = blocks.find(block->index);
   if (it != blocks.end())
      return it->second;

   BasicBlock *bb = new BasicBlock(func);
   blocks[block->index] = bb;
   return bb;
}

bool
Converter::isFloatType(nir_alu_type type)
{
   return nir_alu_type_get_base_type(type) == nir_type_float;
}

bool
Converter::isSignedType(nir_alu_type type)
{
   return nir_alu_type_get_base_type(type) == nir_type_int;
}

bool
Converter::isResultFloat(nir_op op)
{
   const nir_op_info &info = nir_op_infos[op];
   if (info.output_type != nir_type_invalid)
      return isFloatType(info.output_type);

   ERROR("isResultFloat not implemented for %s\n", nir_op_infos[op].name);
   assert(false);
   return true;
}

bool
Converter::isResultSigned(nir_op op)
{
   switch (op) {
   // there is no umul and we get wrong results if we treat all muls as signed
   case nir_op_imul:
   case nir_op_inot:
      return false;
   default:
      const nir_op_info &info = nir_op_infos[op];
      if (info.output_type != nir_type_invalid)
         return isSignedType(info.output_type);
      ERROR("isResultSigned not implemented for %s\n", nir_op_infos[op].name);
      assert(false);
      return true;
   }
}

DataType
Converter::getDType(nir_alu_instr *insn)
{
   if (insn->dest.dest.is_ssa)
      return getDType(insn->op, insn->dest.dest.ssa.bit_size);
   else
      return getDType(insn->op, insn->dest.dest.reg.reg->bit_size);
}

DataType
Converter::getDType(nir_intrinsic_instr *insn)
{
   bool isSigned;
   switch (insn->intrinsic) {
   case nir_intrinsic_shared_atomic_imax:
   case nir_intrinsic_shared_atomic_imin:
   case nir_intrinsic_ssbo_atomic_imax:
   case nir_intrinsic_ssbo_atomic_imin:
      isSigned = true;
      break;
   default:
      isSigned = false;
      break;
   }

   return getDType(insn, isSigned);
}

DataType
Converter::getDType(nir_intrinsic_instr *insn, bool isSigned)
{
   if (insn->dest.is_ssa)
      return typeOfSize(insn->dest.ssa.bit_size / 8, false, isSigned);
   else
      return typeOfSize(insn->dest.reg.reg->bit_size / 8, false, isSigned);
}

DataType
Converter::getDType(nir_op op, uint8_t bitSize)
{
   DataType ty = typeOfSize(bitSize / 8, isResultFloat(op), isResultSigned(op));
   if (ty == TYPE_NONE) {
      ERROR("couldn't get Type for op %s with bitSize %u\n", nir_op_infos[op].name, bitSize);
      assert(false);
   }
   return ty;
}

std::vector<DataType>
Converter::getSTypes(nir_alu_instr *insn)
{
   const nir_op_info &info = nir_op_infos[insn->op];
   std::vector<DataType> res(info.num_inputs);

   for (uint8_t i = 0; i < info.num_inputs; ++i) {
      if (info.input_types[i] != nir_type_invalid) {
         res[i] = getSType(insn->src[i].src, isFloatType(info.input_types[i]), isSignedType(info.input_types[i]));
      } else {
         ERROR("getSType not implemented for %s idx %u\n", info.name, i);
         assert(false);
         res[i] = TYPE_NONE;
         break;
      }
   }

   return res;
}

DataType
Converter::getSType(nir_src &src, bool isFloat, bool isSigned)
{
   uint8_t bitSize;
   if (src.is_ssa)
      bitSize = src.ssa->bit_size;
   else
      bitSize = src.reg.reg->bit_size;

   DataType ty = typeOfSize(bitSize / 8, isFloat, isSigned);
   if (ty == TYPE_NONE) {
      const char *str;
      if (isFloat)
         str = "float";
      else if (isSigned)
         str = "int";
      else
         str = "uint";
      ERROR("couldn't get Type for %s with bitSize %u\n", str, bitSize);
      assert(false);
   }
   return ty;
}

operation
Converter::getOperation(nir_op op)
{
   switch (op) {
   // basic ops with float and int variants
   case nir_op_fabs:
   case nir_op_iabs:
      return OP_ABS;
   case nir_op_fadd:
   case nir_op_iadd:
      return OP_ADD;
   case nir_op_fand:
   case nir_op_iand:
      return OP_AND;
   case nir_op_ifind_msb:
   case nir_op_ufind_msb:
      return OP_BFIND;
   case nir_op_fceil:
      return OP_CEIL;
   case nir_op_fcos:
      return OP_COS;
   case nir_op_f2f32:
   case nir_op_f2f64:
   case nir_op_f2i32:
   case nir_op_f2i64:
   case nir_op_f2u32:
   case nir_op_f2u64:
   case nir_op_i2f32:
   case nir_op_i2f64:
   case nir_op_i2i32:
   case nir_op_i2i64:
   case nir_op_u2f32:
   case nir_op_u2f64:
   case nir_op_u2u32:
   case nir_op_u2u64:
      return OP_CVT;
   case nir_op_fddx:
   case nir_op_fddx_coarse:
   case nir_op_fddx_fine:
      return OP_DFDX;
   case nir_op_fddy:
   case nir_op_fddy_coarse:
   case nir_op_fddy_fine:
      return OP_DFDY;
   case nir_op_fdiv:
   case nir_op_idiv:
   case nir_op_udiv:
      return OP_DIV;
   case nir_op_fexp2:
      return OP_EX2;
   case nir_op_ffloor:
      return OP_FLOOR;
   case nir_op_ffma:
      return OP_FMA;
   case nir_op_flog2:
      return OP_LG2;
   case nir_op_fmax:
   case nir_op_imax:
   case nir_op_umax:
      return OP_MAX;
   case nir_op_pack_64_2x32_split:
      return OP_MERGE;
   case nir_op_fmin:
   case nir_op_imin:
   case nir_op_umin:
      return OP_MIN;
   case nir_op_fmod:
   case nir_op_imod:
   case nir_op_umod:
   case nir_op_frem:
   case nir_op_irem:
      return OP_MOD;
   case nir_op_fmul:
   case nir_op_imul:
   case nir_op_imul_high:
   case nir_op_umul_high:
      return OP_MUL;
   case nir_op_fneg:
   case nir_op_ineg:
      return OP_NEG;
   case nir_op_fnot:
   case nir_op_inot:
      return OP_NOT;
   case nir_op_for:
   case nir_op_ior:
      return OP_OR;
   case nir_op_fpow:
      return OP_POW;
   case nir_op_frcp:
      return OP_RCP;
   case nir_op_frsq:
      return OP_RSQ;
   case nir_op_fsat:
      return OP_SAT;
   case nir_op_feq32:
   case nir_op_ieq32:
   case nir_op_fge32:
   case nir_op_ige32:
   case nir_op_uge32:
   case nir_op_flt32:
   case nir_op_ilt32:
   case nir_op_ult32:
   case nir_op_fne32:
   case nir_op_ine32:
      return OP_SET;
   case nir_op_ishl:
      return OP_SHL;
   case nir_op_ishr:
   case nir_op_ushr:
      return OP_SHR;
   case nir_op_fsin:
      return OP_SIN;
   case nir_op_fsqrt:
      return OP_SQRT;
   case nir_op_fsub:
   case nir_op_isub:
      return OP_SUB;
   case nir_op_ftrunc:
      return OP_TRUNC;
   case nir_op_fxor:
   case nir_op_ixor:
      return OP_XOR;
   default:
      ERROR("couldn't get operation for op %s\n", nir_op_infos[op].name);
      assert(false);
      return OP_NOP;
   }
}

operation
Converter::getOperation(nir_texop op)
{
   switch (op) {
   case nir_texop_tex:
      return OP_TEX;
   case nir_texop_lod:
      return OP_TXLQ;
   case nir_texop_txb:
      return OP_TXB;
   case nir_texop_txd:
      return OP_TXD;
   case nir_texop_txf:
   case nir_texop_txf_ms:
      return OP_TXF;
   case nir_texop_tg4:
      return OP_TXG;
   case nir_texop_txl:
      return OP_TXL;
   case nir_texop_query_levels:
   case nir_texop_texture_samples:
   case nir_texop_txs:
      return OP_TXQ;
   default:
      ERROR("couldn't get operation for nir_texop %u\n", op);
      assert(false);
      return OP_NOP;
   }
}

operation
Converter::getOperation(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_emit_vertex:
      return OP_EMIT;
   case nir_intrinsic_end_primitive:
      return OP_RESTART;
   case nir_intrinsic_bindless_image_atomic_add:
   case nir_intrinsic_image_atomic_add:
   case nir_intrinsic_image_deref_atomic_add:
   case nir_intrinsic_bindless_image_atomic_and:
   case nir_intrinsic_image_atomic_and:
   case nir_intrinsic_image_deref_atomic_and:
   case nir_intrinsic_bindless_image_atomic_comp_swap:
   case nir_intrinsic_image_atomic_comp_swap:
   case nir_intrinsic_image_deref_atomic_comp_swap:
   case nir_intrinsic_bindless_image_atomic_exchange:
   case nir_intrinsic_image_atomic_exchange:
   case nir_intrinsic_image_deref_atomic_exchange:
   case nir_intrinsic_bindless_image_atomic_max:
   case nir_intrinsic_image_atomic_max:
   case nir_intrinsic_image_deref_atomic_max:
   case nir_intrinsic_bindless_image_atomic_min:
   case nir_intrinsic_image_atomic_min:
   case nir_intrinsic_image_deref_atomic_min:
   case nir_intrinsic_bindless_image_atomic_or:
   case nir_intrinsic_image_atomic_or:
   case nir_intrinsic_image_deref_atomic_or:
   case nir_intrinsic_bindless_image_atomic_xor:
   case nir_intrinsic_image_atomic_xor:
   case nir_intrinsic_image_deref_atomic_xor:
      return OP_SUREDP;
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_deref_load:
      return OP_SULDP;
   case nir_intrinsic_bindless_image_samples:
   case nir_intrinsic_image_samples:
   case nir_intrinsic_image_deref_samples:
   case nir_intrinsic_bindless_image_size:
   case nir_intrinsic_image_size:
   case nir_intrinsic_image_deref_size:
      return OP_SUQ;
   case nir_intrinsic_bindless_image_store:
   case nir_intrinsic_image_store:
   case nir_intrinsic_image_deref_store:
      return OP_SUSTP;
   default:
      ERROR("couldn't get operation for nir_intrinsic_op %u\n", op);
      assert(false);
      return OP_NOP;
   }
}

operation
Converter::preOperationNeeded(nir_op op)
{
   switch (op) {
   case nir_op_fcos:
   case nir_op_fsin:
      return OP_PRESIN;
   default:
      return OP_NOP;
   }
}

int
Converter::getSubOp(nir_op op)
{
   switch (op) {
   case nir_op_imul_high:
   case nir_op_umul_high:
      return NV50_IR_SUBOP_MUL_HIGH;
   default:
      return 0;
   }
}

int
Converter::getSubOp(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_bindless_image_atomic_add:
   case nir_intrinsic_image_atomic_add:
   case nir_intrinsic_image_deref_atomic_add:
   case nir_intrinsic_shared_atomic_add:
   case nir_intrinsic_ssbo_atomic_add:
      return  NV50_IR_SUBOP_ATOM_ADD;
   case nir_intrinsic_bindless_image_atomic_and:
   case nir_intrinsic_image_atomic_and:
   case nir_intrinsic_image_deref_atomic_and:
   case nir_intrinsic_shared_atomic_and:
   case nir_intrinsic_ssbo_atomic_and:
      return  NV50_IR_SUBOP_ATOM_AND;
   case nir_intrinsic_bindless_image_atomic_comp_swap:
   case nir_intrinsic_image_atomic_comp_swap:
   case nir_intrinsic_image_deref_atomic_comp_swap:
   case nir_intrinsic_shared_atomic_comp_swap:
   case nir_intrinsic_ssbo_atomic_comp_swap:
      return  NV50_IR_SUBOP_ATOM_CAS;
   case nir_intrinsic_bindless_image_atomic_exchange:
   case nir_intrinsic_image_atomic_exchange:
   case nir_intrinsic_image_deref_atomic_exchange:
   case nir_intrinsic_shared_atomic_exchange:
   case nir_intrinsic_ssbo_atomic_exchange:
      return  NV50_IR_SUBOP_ATOM_EXCH;
   case nir_intrinsic_bindless_image_atomic_or:
   case nir_intrinsic_image_atomic_or:
   case nir_intrinsic_image_deref_atomic_or:
   case nir_intrinsic_shared_atomic_or:
   case nir_intrinsic_ssbo_atomic_or:
      return  NV50_IR_SUBOP_ATOM_OR;
   case nir_intrinsic_bindless_image_atomic_max:
   case nir_intrinsic_image_atomic_max:
   case nir_intrinsic_image_deref_atomic_max:
   case nir_intrinsic_shared_atomic_imax:
   case nir_intrinsic_shared_atomic_umax:
   case nir_intrinsic_ssbo_atomic_imax:
   case nir_intrinsic_ssbo_atomic_umax:
      return  NV50_IR_SUBOP_ATOM_MAX;
   case nir_intrinsic_bindless_image_atomic_min:
   case nir_intrinsic_image_atomic_min:
   case nir_intrinsic_image_deref_atomic_min:
   case nir_intrinsic_shared_atomic_imin:
   case nir_intrinsic_shared_atomic_umin:
   case nir_intrinsic_ssbo_atomic_imin:
   case nir_intrinsic_ssbo_atomic_umin:
      return  NV50_IR_SUBOP_ATOM_MIN;
   case nir_intrinsic_bindless_image_atomic_xor:
   case nir_intrinsic_image_atomic_xor:
   case nir_intrinsic_image_deref_atomic_xor:
   case nir_intrinsic_shared_atomic_xor:
   case nir_intrinsic_ssbo_atomic_xor:
      return  NV50_IR_SUBOP_ATOM_XOR;

   case nir_intrinsic_group_memory_barrier:
   case nir_intrinsic_memory_barrier:
   case nir_intrinsic_memory_barrier_atomic_counter:
   case nir_intrinsic_memory_barrier_buffer:
   case nir_intrinsic_memory_barrier_image:
      return NV50_IR_SUBOP_MEMBAR(M, GL);
   case nir_intrinsic_memory_barrier_shared:
      return NV50_IR_SUBOP_MEMBAR(M, CTA);

   case nir_intrinsic_vote_all:
      return NV50_IR_SUBOP_VOTE_ALL;
   case nir_intrinsic_vote_any:
      return NV50_IR_SUBOP_VOTE_ANY;
   case nir_intrinsic_vote_ieq:
      return NV50_IR_SUBOP_VOTE_UNI;
   default:
      return 0;
   }
}

CondCode
Converter::getCondCode(nir_op op)
{
   switch (op) {
   case nir_op_feq32:
   case nir_op_ieq32:
      return CC_EQ;
   case nir_op_fge32:
   case nir_op_ige32:
   case nir_op_uge32:
      return CC_GE;
   case nir_op_flt32:
   case nir_op_ilt32:
   case nir_op_ult32:
      return CC_LT;
   case nir_op_fne32:
      return CC_NEU;
   case nir_op_ine32:
      return CC_NE;
   default:
      ERROR("couldn't get CondCode for op %s\n", nir_op_infos[op].name);
      assert(false);
      return CC_FL;
   }
}

Converter::LValues&
Converter::convert(nir_alu_dest *dest)
{
   return convert(&dest->dest);
}

Converter::LValues&
Converter::convert(nir_dest *dest)
{
   if (dest->is_ssa)
      return convert(&dest->ssa);
   if (dest->reg.indirect) {
      ERROR("no support for indirects.");
      assert(false);
   }
   return convert(dest->reg.reg);
}

Converter::LValues&
Converter::convert(nir_register *reg)
{
   NirDefMap::iterator it = regDefs.find(reg->index);
   if (it != regDefs.end())
      return it->second;

   LValues newDef(reg->num_components);
   for (uint8_t i = 0; i < reg->num_components; i++)
      newDef[i] = getScratch(std::max(4, reg->bit_size / 8));
   return regDefs[reg->index] = newDef;
}

Converter::LValues&
Converter::convert(nir_ssa_def *def)
{
   NirDefMap::iterator it = ssaDefs.find(def->index);
   if (it != ssaDefs.end())
      return it->second;

   LValues newDef(def->num_components);
   for (uint8_t i = 0; i < def->num_components; i++)
      newDef[i] = getSSA(std::max(4, def->bit_size / 8));
   return ssaDefs[def->index] = newDef;
}

Value*
Converter::getSrc(nir_alu_src *src, uint8_t component)
{
   if (src->abs || src->negate) {
      ERROR("modifiers currently not supported on nir_alu_src\n");
      assert(false);
   }
   return getSrc(&src->src, src->swizzle[component]);
}

Value*
Converter::getSrc(nir_register *reg, uint8_t idx)
{
   NirDefMap::iterator it = regDefs.find(reg->index);
   if (it == regDefs.end())
      return convert(reg)[idx];
   return it->second[idx];
}

Value*
Converter::getSrc(nir_src *src, uint8_t idx, bool indirect)
{
   if (src->is_ssa)
      return getSrc(src->ssa, idx);

   if (src->reg.indirect) {
      if (indirect)
         return getSrc(src->reg.indirect, idx);
      ERROR("no support for indirects.");
      assert(false);
      return NULL;
   }

   return getSrc(src->reg.reg, idx);
}

Value*
Converter::getSrc(nir_ssa_def *src, uint8_t idx)
{
   ImmediateMap::iterator iit = immediates.find(src->index);
   if (iit != immediates.end())
      return convert((*iit).second, idx);

   NirDefMap::iterator it = ssaDefs.find(src->index);
   if (it == ssaDefs.end()) {
      ERROR("SSA value %u not found\n", src->index);
      assert(false);
      return NULL;
   }
   return it->second[idx];
}

uint32_t
Converter::getIndirect(nir_src *src, uint8_t idx, Value *&indirect)
{
   nir_const_value *offset = nir_src_as_const_value(*src);

   if (offset) {
      indirect = NULL;
      return offset[0].u32;
   }

   indirect = getSrc(src, idx, true);
   return 0;
}

uint32_t
Converter::getIndirect(nir_intrinsic_instr *insn, uint8_t s, uint8_t c, Value *&indirect)
{
   int32_t idx = nir_intrinsic_base(insn) + getIndirect(&insn->src[s], c, indirect);
   if (indirect)
      indirect = mkOp2v(OP_SHL, TYPE_U32, getSSA(4, FILE_ADDRESS), indirect, loadImm(NULL, 4));
   return idx;
}

static void
vert_attrib_to_tgsi_semantic(gl_vert_attrib slot, unsigned *name, unsigned *index)
{
   assert(name && index);

   if (slot >= VERT_ATTRIB_MAX) {
      ERROR("invalid varying slot %u\n", slot);
      assert(false);
      return;
   }

   if (slot >= VERT_ATTRIB_GENERIC0 &&
       slot < VERT_ATTRIB_GENERIC0 + VERT_ATTRIB_GENERIC_MAX) {
      *name = TGSI_SEMANTIC_GENERIC;
      *index = slot - VERT_ATTRIB_GENERIC0;
      return;
   }

   if (slot >= VERT_ATTRIB_TEX0 &&
       slot < VERT_ATTRIB_TEX0 + VERT_ATTRIB_TEX_MAX) {
      *name = TGSI_SEMANTIC_TEXCOORD;
      *index = slot - VERT_ATTRIB_TEX0;
      return;
   }

   switch (slot) {
   case VERT_ATTRIB_COLOR0:
      *name = TGSI_SEMANTIC_COLOR;
      *index = 0;
      break;
   case VERT_ATTRIB_COLOR1:
      *name = TGSI_SEMANTIC_COLOR;
      *index = 1;
      break;
   case VERT_ATTRIB_EDGEFLAG:
      *name = TGSI_SEMANTIC_EDGEFLAG;
      *index = 0;
      break;
   case VERT_ATTRIB_FOG:
      *name = TGSI_SEMANTIC_FOG;
      *index = 0;
      break;
   case VERT_ATTRIB_NORMAL:
      *name = TGSI_SEMANTIC_NORMAL;
      *index = 0;
      break;
   case VERT_ATTRIB_POS:
      *name = TGSI_SEMANTIC_POSITION;
      *index = 0;
      break;
   case VERT_ATTRIB_POINT_SIZE:
      *name = TGSI_SEMANTIC_PSIZE;
      *index = 0;
      break;
   default:
      ERROR("unknown vert attrib slot %u\n", slot);
      assert(false);
      break;
   }
}

static void
varying_slot_to_tgsi_semantic(gl_varying_slot slot, unsigned *name, unsigned *index)
{
   assert(name && index);

   if (slot >= VARYING_SLOT_TESS_MAX) {
      ERROR("invalid varying slot %u\n", slot);
      assert(false);
      return;
   }

   if (slot >= VARYING_SLOT_PATCH0) {
      *name = TGSI_SEMANTIC_PATCH;
      *index = slot - VARYING_SLOT_PATCH0;
      return;
   }

   if (slot >= VARYING_SLOT_VAR0) {
      *name = TGSI_SEMANTIC_GENERIC;
      *index = slot - VARYING_SLOT_VAR0;
      return;
   }

   if (slot >= VARYING_SLOT_TEX0 && slot <= VARYING_SLOT_TEX7) {
      *name = TGSI_SEMANTIC_TEXCOORD;
      *index = slot - VARYING_SLOT_TEX0;
      return;
   }

   switch (slot) {
   case VARYING_SLOT_BFC0:
      *name = TGSI_SEMANTIC_BCOLOR;
      *index = 0;
      break;
   case VARYING_SLOT_BFC1:
      *name = TGSI_SEMANTIC_BCOLOR;
      *index = 1;
      break;
   case VARYING_SLOT_CLIP_DIST0:
      *name = TGSI_SEMANTIC_CLIPDIST;
      *index = 0;
      break;
   case VARYING_SLOT_CLIP_DIST1:
      *name = TGSI_SEMANTIC_CLIPDIST;
      *index = 1;
      break;
   case VARYING_SLOT_CLIP_VERTEX:
      *name = TGSI_SEMANTIC_CLIPVERTEX;
      *index = 0;
      break;
   case VARYING_SLOT_COL0:
      *name = TGSI_SEMANTIC_COLOR;
      *index = 0;
      break;
   case VARYING_SLOT_COL1:
      *name = TGSI_SEMANTIC_COLOR;
      *index = 1;
      break;
   case VARYING_SLOT_EDGE:
      *name = TGSI_SEMANTIC_EDGEFLAG;
      *index = 0;
      break;
   case VARYING_SLOT_FACE:
      *name = TGSI_SEMANTIC_FACE;
      *index = 0;
      break;
   case VARYING_SLOT_FOGC:
      *name = TGSI_SEMANTIC_FOG;
      *index = 0;
      break;
   case VARYING_SLOT_LAYER:
      *name = TGSI_SEMANTIC_LAYER;
      *index = 0;
      break;
   case VARYING_SLOT_PNTC:
      *name = TGSI_SEMANTIC_PCOORD;
      *index = 0;
      break;
   case VARYING_SLOT_POS:
      *name = TGSI_SEMANTIC_POSITION;
      *index = 0;
      break;
   case VARYING_SLOT_PRIMITIVE_ID:
      *name = TGSI_SEMANTIC_PRIMID;
      *index = 0;
      break;
   case VARYING_SLOT_PSIZ:
      *name = TGSI_SEMANTIC_PSIZE;
      *index = 0;
      break;
   case VARYING_SLOT_TESS_LEVEL_INNER:
      *name = TGSI_SEMANTIC_TESSINNER;
      *index = 0;
      break;
   case VARYING_SLOT_TESS_LEVEL_OUTER:
      *name = TGSI_SEMANTIC_TESSOUTER;
      *index = 0;
      break;
   case VARYING_SLOT_VIEWPORT:
      *name = TGSI_SEMANTIC_VIEWPORT_INDEX;
      *index = 0;
      break;
   default:
      ERROR("unknown varying slot %u\n", slot);
      assert(false);
      break;
   }
}

static void
frag_result_to_tgsi_semantic(unsigned slot, unsigned *name, unsigned *index)
{
   if (slot >= FRAG_RESULT_DATA0) {
      *name = TGSI_SEMANTIC_COLOR;
      *index = slot - FRAG_RESULT_COLOR - 2; // intentional
      return;
   }

   switch (slot) {
   case FRAG_RESULT_COLOR:
      *name = TGSI_SEMANTIC_COLOR;
      *index = 0;
      break;
   case FRAG_RESULT_DEPTH:
      *name = TGSI_SEMANTIC_POSITION;
      *index = 0;
      break;
   case FRAG_RESULT_SAMPLE_MASK:
      *name = TGSI_SEMANTIC_SAMPLEMASK;
      *index = 0;
      break;
   default:
      ERROR("unknown frag result slot %u\n", slot);
      assert(false);
      break;
   }
}

// copy of _mesa_sysval_to_semantic
static void
system_val_to_tgsi_semantic(unsigned val, unsigned *name, unsigned *index)
{
   *index = 0;
   switch (val) {
   // Vertex shader
   case SYSTEM_VALUE_VERTEX_ID:
      *name = TGSI_SEMANTIC_VERTEXID;
      break;
   case SYSTEM_VALUE_INSTANCE_ID:
      *name = TGSI_SEMANTIC_INSTANCEID;
      break;
   case SYSTEM_VALUE_VERTEX_ID_ZERO_BASE:
      *name = TGSI_SEMANTIC_VERTEXID_NOBASE;
      break;
   case SYSTEM_VALUE_BASE_VERTEX:
      *name = TGSI_SEMANTIC_BASEVERTEX;
      break;
   case SYSTEM_VALUE_BASE_INSTANCE:
      *name = TGSI_SEMANTIC_BASEINSTANCE;
      break;
   case SYSTEM_VALUE_DRAW_ID:
      *name = TGSI_SEMANTIC_DRAWID;
      break;

   // Geometry shader
   case SYSTEM_VALUE_INVOCATION_ID:
      *name = TGSI_SEMANTIC_INVOCATIONID;
      break;

   // Fragment shader
   case SYSTEM_VALUE_FRAG_COORD:
      *name = TGSI_SEMANTIC_POSITION;
      break;
   case SYSTEM_VALUE_FRONT_FACE:
      *name = TGSI_SEMANTIC_FACE;
      break;
   case SYSTEM_VALUE_SAMPLE_ID:
      *name = TGSI_SEMANTIC_SAMPLEID;
      break;
   case SYSTEM_VALUE_SAMPLE_POS:
      *name = TGSI_SEMANTIC_SAMPLEPOS;
      break;
   case SYSTEM_VALUE_SAMPLE_MASK_IN:
      *name = TGSI_SEMANTIC_SAMPLEMASK;
      break;
   case SYSTEM_VALUE_HELPER_INVOCATION:
      *name = TGSI_SEMANTIC_HELPER_INVOCATION;
      break;

   // Tessellation shader
   case SYSTEM_VALUE_TESS_COORD:
      *name = TGSI_SEMANTIC_TESSCOORD;
      break;
   case SYSTEM_VALUE_VERTICES_IN:
      *name = TGSI_SEMANTIC_VERTICESIN;
      break;
   case SYSTEM_VALUE_PRIMITIVE_ID:
      *name = TGSI_SEMANTIC_PRIMID;
      break;
   case SYSTEM_VALUE_TESS_LEVEL_OUTER:
      *name = TGSI_SEMANTIC_TESSOUTER;
      break;
   case SYSTEM_VALUE_TESS_LEVEL_INNER:
      *name = TGSI_SEMANTIC_TESSINNER;
      break;

   // Compute shader
   case SYSTEM_VALUE_LOCAL_INVOCATION_ID:
      *name = TGSI_SEMANTIC_THREAD_ID;
      break;
   case SYSTEM_VALUE_WORK_GROUP_ID:
      *name = TGSI_SEMANTIC_BLOCK_ID;
      break;
   case SYSTEM_VALUE_NUM_WORK_GROUPS:
      *name = TGSI_SEMANTIC_GRID_SIZE;
      break;
   case SYSTEM_VALUE_LOCAL_GROUP_SIZE:
      *name = TGSI_SEMANTIC_BLOCK_SIZE;
      break;

   // ARB_shader_ballot
   case SYSTEM_VALUE_SUBGROUP_SIZE:
      *name = TGSI_SEMANTIC_SUBGROUP_SIZE;
      break;
   case SYSTEM_VALUE_SUBGROUP_INVOCATION:
      *name = TGSI_SEMANTIC_SUBGROUP_INVOCATION;
      break;
   case SYSTEM_VALUE_SUBGROUP_EQ_MASK:
      *name = TGSI_SEMANTIC_SUBGROUP_EQ_MASK;
      break;
   case SYSTEM_VALUE_SUBGROUP_GE_MASK:
      *name = TGSI_SEMANTIC_SUBGROUP_GE_MASK;
      break;
   case SYSTEM_VALUE_SUBGROUP_GT_MASK:
      *name = TGSI_SEMANTIC_SUBGROUP_GT_MASK;
      break;
   case SYSTEM_VALUE_SUBGROUP_LE_MASK:
      *name = TGSI_SEMANTIC_SUBGROUP_LE_MASK;
      break;
   case SYSTEM_VALUE_SUBGROUP_LT_MASK:
      *name = TGSI_SEMANTIC_SUBGROUP_LT_MASK;
      break;

   default:
      ERROR("unknown system value %u\n", val);
      assert(false);
      break;
   }
}

void
Converter::setInterpolate(nv50_ir_varying *var,
                          uint8_t mode,
                          bool centroid,
                          unsigned semantic)
{
   switch (mode) {
   case INTERP_MODE_FLAT:
      var->flat = 1;
      break;
   case INTERP_MODE_NONE:
      if (semantic == TGSI_SEMANTIC_COLOR)
         var->sc = 1;
      else if (semantic == TGSI_SEMANTIC_POSITION)
         var->linear = 1;
      break;
   case INTERP_MODE_NOPERSPECTIVE:
      var->linear = 1;
      break;
   case INTERP_MODE_SMOOTH:
      break;
   }
   var->centroid = centroid;
}

static uint16_t
calcSlots(const glsl_type *type, Program::Type stage, const shader_info &info,
          bool input, const nir_variable *var)
{
   if (!type->is_array())
      return type->count_attribute_slots(false);

   uint16_t slots;
   switch (stage) {
   case Program::TYPE_GEOMETRY:
      slots = type->uniform_locations();
      if (input)
         slots /= info.gs.vertices_in;
      break;
   case Program::TYPE_TESSELLATION_CONTROL:
   case Program::TYPE_TESSELLATION_EVAL:
      // remove first dimension
      if (var->data.patch || (!input && stage == Program::TYPE_TESSELLATION_EVAL))
         slots = type->uniform_locations();
      else
         slots = type->fields.array->uniform_locations();
      break;
   default:
      slots = type->count_attribute_slots(false);
      break;
   }

   return slots;
}

bool Converter::assignSlots() {
   unsigned name;
   unsigned index;

   info->io.viewportId = -1;
   info->numInputs = 0;

   // we have to fixup the uniform locations for arrays
   unsigned numImages = 0;
   nir_foreach_variable(var, &nir->uniforms) {
      const glsl_type *type = var->type;
      if (!type->without_array()->is_image())
         continue;
      var->data.driver_location = numImages;
      numImages += type->is_array() ? type->arrays_of_arrays_size() : 1;
   }

   nir_foreach_variable(var, &nir->inputs) {
      const glsl_type *type = var->type;
      int slot = var->data.location;
      uint16_t slots = calcSlots(type, prog->getType(), nir->info, true, var);
      uint32_t comp = type->is_array() ? type->without_array()->component_slots()
                                       : type->component_slots();
      uint32_t frac = var->data.location_frac;
      uint32_t vary = var->data.driver_location;

      if (glsl_base_type_is_64bit(type->without_array()->base_type)) {
         if (comp > 2)
            slots *= 2;
      }

      assert(vary + slots <= PIPE_MAX_SHADER_INPUTS);

      switch(prog->getType()) {
      case Program::TYPE_FRAGMENT:
         varying_slot_to_tgsi_semantic((gl_varying_slot)slot, &name, &index);
         for (uint16_t i = 0; i < slots; ++i) {
            setInterpolate(&info->in[vary + i], var->data.interpolation,
                           var->data.centroid | var->data.sample, name);
         }
         break;
      case Program::TYPE_GEOMETRY:
         varying_slot_to_tgsi_semantic((gl_varying_slot)slot, &name, &index);
         break;
      case Program::TYPE_TESSELLATION_CONTROL:
      case Program::TYPE_TESSELLATION_EVAL:
         varying_slot_to_tgsi_semantic((gl_varying_slot)slot, &name, &index);
         if (var->data.patch && name == TGSI_SEMANTIC_PATCH)
            info->numPatchConstants = MAX2(info->numPatchConstants, index + slots);
         break;
      case Program::TYPE_VERTEX:
         vert_attrib_to_tgsi_semantic((gl_vert_attrib)slot, &name, &index);
         switch (name) {
         case TGSI_SEMANTIC_EDGEFLAG:
            info->io.edgeFlagIn = vary;
            break;
         default:
            break;
         }
         break;
      default:
         ERROR("unknown shader type %u in assignSlots\n", prog->getType());
         return false;
      }

      for (uint16_t i = 0u; i < slots; ++i, ++vary) {
         info->in[vary].id = vary;
         info->in[vary].patch = var->data.patch;
         info->in[vary].sn = name;
         info->in[vary].si = index + i;
         if (glsl_base_type_is_64bit(type->without_array()->base_type))
            if (i & 0x1)
               info->in[vary].mask |= (((1 << (comp * 2)) - 1) << (frac * 2) >> 0x4);
            else
               info->in[vary].mask |= (((1 << (comp * 2)) - 1) << (frac * 2) & 0xf);
         else
            info->in[vary].mask |= ((1 << comp) - 1) << frac;
      }
      info->numInputs = std::max<uint8_t>(info->numInputs, vary);
   }

   info->numOutputs = 0;
   nir_foreach_variable(var, &nir->outputs) {
      const glsl_type *type = var->type;
      int slot = var->data.location;
      uint16_t slots = calcSlots(type, prog->getType(), nir->info, false, var);
      uint32_t comp = type->is_array() ? type->without_array()->component_slots()
                                       : type->component_slots();
      uint32_t frac = var->data.location_frac;
      uint32_t vary = var->data.driver_location;

      if (glsl_base_type_is_64bit(type->without_array()->base_type)) {
         if (comp > 2)
            slots *= 2;
      }

      assert(vary < PIPE_MAX_SHADER_OUTPUTS);

      switch(prog->getType()) {
      case Program::TYPE_FRAGMENT:
         frag_result_to_tgsi_semantic((gl_frag_result)slot, &name, &index);
         switch (name) {
         case TGSI_SEMANTIC_COLOR:
            if (!var->data.fb_fetch_output)
               info->prop.fp.numColourResults++;
            info->prop.fp.separateFragData = true;
            // sometimes we get FRAG_RESULT_DATAX with data.index 0
            // sometimes we get FRAG_RESULT_DATA0 with data.index X
            index = index == 0 ? var->data.index : index;
            break;
         case TGSI_SEMANTIC_POSITION:
            info->io.fragDepth = vary;
            info->prop.fp.writesDepth = true;
            break;
         case TGSI_SEMANTIC_SAMPLEMASK:
            info->io.sampleMask = vary;
            break;
         default:
            break;
         }
         break;
      case Program::TYPE_GEOMETRY:
      case Program::TYPE_TESSELLATION_CONTROL:
      case Program::TYPE_TESSELLATION_EVAL:
      case Program::TYPE_VERTEX:
         varying_slot_to_tgsi_semantic((gl_varying_slot)slot, &name, &index);

         if (var->data.patch && name != TGSI_SEMANTIC_TESSINNER &&
             name != TGSI_SEMANTIC_TESSOUTER)
            info->numPatchConstants = MAX2(info->numPatchConstants, index + slots);

         switch (name) {
         case TGSI_SEMANTIC_CLIPDIST:
            info->io.genUserClip = -1;
            break;
         case TGSI_SEMANTIC_CLIPVERTEX:
            clipVertexOutput = vary;
            break;
         case TGSI_SEMANTIC_EDGEFLAG:
            info->io.edgeFlagOut = vary;
            break;
         case TGSI_SEMANTIC_POSITION:
            if (clipVertexOutput < 0)
               clipVertexOutput = vary;
            break;
         default:
            break;
         }
         break;
      default:
         ERROR("unknown shader type %u in assignSlots\n", prog->getType());
         return false;
      }

      for (uint16_t i = 0u; i < slots; ++i, ++vary) {
         info->out[vary].id = vary;
         info->out[vary].patch = var->data.patch;
         info->out[vary].sn = name;
         info->out[vary].si = index + i;
         if (glsl_base_type_is_64bit(type->without_array()->base_type))
            if (i & 0x1)
               info->out[vary].mask |= (((1 << (comp * 2)) - 1) << (frac * 2) >> 0x4);
            else
               info->out[vary].mask |= (((1 << (comp * 2)) - 1) << (frac * 2) & 0xf);
         else
            info->out[vary].mask |= ((1 << comp) - 1) << frac;

         if (nir->info.outputs_read & 1ll << slot)
            info->out[vary].oread = 1;
      }
      info->numOutputs = std::max<uint8_t>(info->numOutputs, vary);
   }

   info->numSysVals = 0;
   for (uint8_t i = 0; i < 64; ++i) {
      if (!(nir->info.system_values_read & 1ll << i))
         continue;

      system_val_to_tgsi_semantic(i, &name, &index);
      info->sv[info->numSysVals].sn = name;
      info->sv[info->numSysVals].si = index;
      info->sv[info->numSysVals].input = 0; // TODO inferSysValDirection(sn);

      switch (i) {
      case SYSTEM_VALUE_INSTANCE_ID:
         info->io.instanceId = info->numSysVals;
         break;
      case SYSTEM_VALUE_TESS_LEVEL_INNER:
      case SYSTEM_VALUE_TESS_LEVEL_OUTER:
         info->sv[info->numSysVals].patch = 1;
         break;
      case SYSTEM_VALUE_VERTEX_ID:
         info->io.vertexId = info->numSysVals;
         break;
      default:
         break;
      }

      info->numSysVals += 1;
   }

   if (info->io.genUserClip > 0) {
      info->io.clipDistances = info->io.genUserClip;

      const unsigned int nOut = (info->io.genUserClip + 3) / 4;

      for (unsigned int n = 0; n < nOut; ++n) {
         unsigned int i = info->numOutputs++;
         info->out[i].id = i;
         info->out[i].sn = TGSI_SEMANTIC_CLIPDIST;
         info->out[i].si = n;
         info->out[i].mask = ((1 << info->io.clipDistances) - 1) >> (n * 4);
      }
   }

   return info->assignSlots(info) == 0;
}

uint32_t
Converter::getSlotAddress(nir_intrinsic_instr *insn, uint8_t idx, uint8_t slot)
{
   DataType ty;
   int offset = nir_intrinsic_component(insn);
   bool input;

   if (nir_intrinsic_infos[insn->intrinsic].has_dest)
      ty = getDType(insn);
   else
      ty = getSType(insn->src[0], false, false);

   switch (insn->intrinsic) {
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_load_per_vertex_input:
      input = true;
      break;
   case nir_intrinsic_load_output:
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_vertex_output:
      input = false;
      break;
   default:
      ERROR("unknown intrinsic in getSlotAddress %s",
            nir_intrinsic_infos[insn->intrinsic].name);
      input = false;
      assert(false);
      break;
   }

   if (typeSizeof(ty) == 8) {
      slot *= 2;
      slot += offset;
      if (slot >= 4) {
         idx += 1;
         slot -= 4;
      }
   } else {
      slot += offset;
   }

   assert(slot < 4);
   assert(!input || idx < PIPE_MAX_SHADER_INPUTS);
   assert(input || idx < PIPE_MAX_SHADER_OUTPUTS);

   const nv50_ir_varying *vary = input ? info->in : info->out;
   return vary[idx].slot[slot] * 4;
}

Instruction *
Converter::loadFrom(DataFile file, uint8_t i, DataType ty, Value *def,
                    uint32_t base, uint8_t c, Value *indirect0,
                    Value *indirect1, bool patch)
{
   unsigned int tySize = typeSizeof(ty);

   if (tySize == 8 &&
       (file == FILE_MEMORY_CONST || file == FILE_MEMORY_BUFFER || indirect0)) {
      Value *lo = getSSA();
      Value *hi = getSSA();

      Instruction *loi =
         mkLoad(TYPE_U32, lo,
                mkSymbol(file, i, TYPE_U32, base + c * tySize),
                indirect0);
      loi->setIndirect(0, 1, indirect1);
      loi->perPatch = patch;

      Instruction *hii =
         mkLoad(TYPE_U32, hi,
                mkSymbol(file, i, TYPE_U32, base + c * tySize + 4),
                indirect0);
      hii->setIndirect(0, 1, indirect1);
      hii->perPatch = patch;

      return mkOp2(OP_MERGE, ty, def, lo, hi);
   } else {
      Instruction *ld =
         mkLoad(ty, def, mkSymbol(file, i, ty, base + c * tySize), indirect0);
      ld->setIndirect(0, 1, indirect1);
      ld->perPatch = patch;
      return ld;
   }
}

void
Converter::storeTo(nir_intrinsic_instr *insn, DataFile file, operation op,
                   DataType ty, Value *src, uint8_t idx, uint8_t c,
                   Value *indirect0, Value *indirect1)
{
   uint8_t size = typeSizeof(ty);
   uint32_t address = getSlotAddress(insn, idx, c);

   if (size == 8 && indirect0) {
      Value *split[2];
      mkSplit(split, 4, src);

      if (op == OP_EXPORT) {
         split[0] = mkMov(getSSA(), split[0], ty)->getDef(0);
         split[1] = mkMov(getSSA(), split[1], ty)->getDef(0);
      }

      mkStore(op, TYPE_U32, mkSymbol(file, 0, TYPE_U32, address), indirect0,
              split[0])->perPatch = info->out[idx].patch;
      mkStore(op, TYPE_U32, mkSymbol(file, 0, TYPE_U32, address + 4), indirect0,
              split[1])->perPatch = info->out[idx].patch;
   } else {
      if (op == OP_EXPORT)
         src = mkMov(getSSA(size), src, ty)->getDef(0);
      mkStore(op, ty, mkSymbol(file, 0, ty, address), indirect0,
              src)->perPatch = info->out[idx].patch;
   }
}

bool
Converter::parseNIR()
{
   info->bin.tlsSpace = 0;
   info->io.clipDistances = nir->info.clip_distance_array_size;
   info->io.cullDistances = nir->info.cull_distance_array_size;

   switch(prog->getType()) {
   case Program::TYPE_COMPUTE:
      info->prop.cp.numThreads[0] = nir->info.cs.local_size[0];
      info->prop.cp.numThreads[1] = nir->info.cs.local_size[1];
      info->prop.cp.numThreads[2] = nir->info.cs.local_size[2];
      info->bin.smemSize = nir->info.cs.shared_size;
      break;
   case Program::TYPE_FRAGMENT:
      info->prop.fp.earlyFragTests = nir->info.fs.early_fragment_tests;
      info->prop.fp.persampleInvocation =
         (nir->info.system_values_read & SYSTEM_BIT_SAMPLE_ID) ||
         (nir->info.system_values_read & SYSTEM_BIT_SAMPLE_POS);
      info->prop.fp.postDepthCoverage = nir->info.fs.post_depth_coverage;
      info->prop.fp.readsSampleLocations =
         (nir->info.system_values_read & SYSTEM_BIT_SAMPLE_POS);
      info->prop.fp.usesDiscard = nir->info.fs.uses_discard;
      info->prop.fp.usesSampleMaskIn =
         !!(nir->info.system_values_read & SYSTEM_BIT_SAMPLE_MASK_IN);
      break;
   case Program::TYPE_GEOMETRY:
      info->prop.gp.inputPrim = nir->info.gs.input_primitive;
      info->prop.gp.instanceCount = nir->info.gs.invocations;
      info->prop.gp.maxVertices = nir->info.gs.vertices_out;
      info->prop.gp.outputPrim = nir->info.gs.output_primitive;
      break;
   case Program::TYPE_TESSELLATION_CONTROL:
   case Program::TYPE_TESSELLATION_EVAL:
      if (nir->info.tess.primitive_mode == GL_ISOLINES)
         info->prop.tp.domain = GL_LINES;
      else
         info->prop.tp.domain = nir->info.tess.primitive_mode;
      info->prop.tp.outputPatchSize = nir->info.tess.tcs_vertices_out;
      info->prop.tp.outputPrim =
         nir->info.tess.point_mode ? PIPE_PRIM_POINTS : PIPE_PRIM_TRIANGLES;
      info->prop.tp.partitioning = (nir->info.tess.spacing + 1) % 3;
      info->prop.tp.winding = !nir->info.tess.ccw;
      break;
   case Program::TYPE_VERTEX:
      info->prop.vp.usesDrawParameters =
         (nir->info.system_values_read & BITFIELD64_BIT(SYSTEM_VALUE_BASE_VERTEX)) ||
         (nir->info.system_values_read & BITFIELD64_BIT(SYSTEM_VALUE_BASE_INSTANCE)) ||
         (nir->info.system_values_read & BITFIELD64_BIT(SYSTEM_VALUE_DRAW_ID));
      break;
   default:
      break;
   }

   return true;
}

bool
Converter::visit(nir_function *function)
{
   // we only support emiting the main function for now
   assert(!strcmp(function->name, "main"));
   assert(function->impl);

   // usually the blocks will set everything up, but main is special
   BasicBlock *entry = new BasicBlock(prog->main);
   exit = new BasicBlock(prog->main);
   blocks[nir_start_block(function->impl)->index] = entry;
   prog->main->setEntry(entry);
   prog->main->setExit(exit);

   setPosition(entry, true);

   if (info->io.genUserClip > 0) {
      for (int c = 0; c < 4; ++c)
         clipVtx[c] = getScratch();
   }

   switch (prog->getType()) {
   case Program::TYPE_TESSELLATION_CONTROL:
      outBase = mkOp2v(
         OP_SUB, TYPE_U32, getSSA(),
         mkOp1v(OP_RDSV, TYPE_U32, getSSA(), mkSysVal(SV_LANEID, 0)),
         mkOp1v(OP_RDSV, TYPE_U32, getSSA(), mkSysVal(SV_INVOCATION_ID, 0)));
      break;
   case Program::TYPE_FRAGMENT: {
      Symbol *sv = mkSysVal(SV_POSITION, 3);
      fragCoord[3] = mkOp1v(OP_RDSV, TYPE_F32, getSSA(), sv);
      fp.position = mkOp1v(OP_RCP, TYPE_F32, fragCoord[3], fragCoord[3]);
      break;
   }
   default:
      break;
   }

   nir_foreach_register(reg, &function->impl->registers) {
      if (reg->num_array_elems) {
         // TODO: packed variables would be nice, but MemoryOpt fails
         // replace 4 with reg->num_components
         uint32_t size = 4 * reg->num_array_elems * (reg->bit_size / 8);
         regToLmemOffset[reg->index] = info->bin.tlsSpace;
         info->bin.tlsSpace += size;
      }
   }

   nir_index_ssa_defs(function->impl);
   foreach_list_typed(nir_cf_node, node, node, &function->impl->body) {
      if (!visit(node))
         return false;
   }

   bb->cfg.attach(&exit->cfg, Graph::Edge::TREE);
   setPosition(exit, true);

   if ((prog->getType() == Program::TYPE_VERTEX ||
        prog->getType() == Program::TYPE_TESSELLATION_EVAL)
       && info->io.genUserClip > 0)
      handleUserClipPlanes();

   // TODO: for non main function this needs to be a OP_RETURN
   mkOp(OP_EXIT, TYPE_NONE, NULL)->terminator = 1;
   return true;
}

bool
Converter::visit(nir_cf_node *node)
{
   switch (node->type) {
   case nir_cf_node_block:
      return visit(nir_cf_node_as_block(node));
   case nir_cf_node_if:
      return visit(nir_cf_node_as_if(node));
   case nir_cf_node_loop:
      return visit(nir_cf_node_as_loop(node));
   default:
      ERROR("unknown nir_cf_node type %u\n", node->type);
      return false;
   }
}

bool
Converter::visit(nir_block *block)
{
   if (!block->predecessors->entries && block->instr_list.is_empty())
      return true;

   BasicBlock *bb = convert(block);

   setPosition(bb, true);
   nir_foreach_instr(insn, block) {
      if (!visit(insn))
         return false;
   }
   return true;
}

bool
Converter::visit(nir_if *nif)
{
   DataType sType = getSType(nif->condition, false, false);
   Value *src = getSrc(&nif->condition, 0);

   nir_block *lastThen = nir_if_last_then_block(nif);
   nir_block *lastElse = nir_if_last_else_block(nif);

   assert(!lastThen->successors[1]);
   assert(!lastElse->successors[1]);

   BasicBlock *ifBB = convert(nir_if_first_then_block(nif));
   BasicBlock *elseBB = convert(nir_if_first_else_block(nif));

   bb->cfg.attach(&ifBB->cfg, Graph::Edge::TREE);
   bb->cfg.attach(&elseBB->cfg, Graph::Edge::TREE);

   // we only insert joinats, if both nodes end up at the end of the if again.
   // the reason for this to not happens are breaks/continues/ret/... which
   // have their own handling
   if (lastThen->successors[0] == lastElse->successors[0])
      bb->joinAt = mkFlow(OP_JOINAT, convert(lastThen->successors[0]),
                          CC_ALWAYS, NULL);

   mkFlow(OP_BRA, elseBB, CC_EQ, src)->setType(sType);

   foreach_list_typed(nir_cf_node, node, node, &nif->then_list) {
      if (!visit(node))
         return false;
   }
   setPosition(convert(lastThen), true);
   if (!bb->getExit() ||
       !bb->getExit()->asFlow() ||
        bb->getExit()->asFlow()->op == OP_JOIN) {
      BasicBlock *tailBB = convert(lastThen->successors[0]);
      mkFlow(OP_BRA, tailBB, CC_ALWAYS, NULL);
      bb->cfg.attach(&tailBB->cfg, Graph::Edge::FORWARD);
   }

   foreach_list_typed(nir_cf_node, node, node, &nif->else_list) {
      if (!visit(node))
         return false;
   }
   setPosition(convert(lastElse), true);
   if (!bb->getExit() ||
       !bb->getExit()->asFlow() ||
        bb->getExit()->asFlow()->op == OP_JOIN) {
      BasicBlock *tailBB = convert(lastElse->successors[0]);
      mkFlow(OP_BRA, tailBB, CC_ALWAYS, NULL);
      bb->cfg.attach(&tailBB->cfg, Graph::Edge::FORWARD);
   }

   if (lastThen->successors[0] == lastElse->successors[0]) {
      setPosition(convert(lastThen->successors[0]), true);
      mkFlow(OP_JOIN, NULL, CC_ALWAYS, NULL)->fixed = 1;
   }

   return true;
}

bool
Converter::visit(nir_loop *loop)
{
   curLoopDepth += 1;
   func->loopNestingBound = std::max(func->loopNestingBound, curLoopDepth);

   BasicBlock *loopBB = convert(nir_loop_first_block(loop));
   BasicBlock *tailBB =
      convert(nir_cf_node_as_block(nir_cf_node_next(&loop->cf_node)));
   bb->cfg.attach(&loopBB->cfg, Graph::Edge::TREE);

   mkFlow(OP_PREBREAK, tailBB, CC_ALWAYS, NULL);
   setPosition(loopBB, false);
   mkFlow(OP_PRECONT, loopBB, CC_ALWAYS, NULL);

   foreach_list_typed(nir_cf_node, node, node, &loop->body) {
      if (!visit(node))
         return false;
   }
   Instruction *insn = bb->getExit();
   if (bb->cfg.incidentCount() != 0) {
      if (!insn || !insn->asFlow()) {
         mkFlow(OP_CONT, loopBB, CC_ALWAYS, NULL);
         bb->cfg.attach(&loopBB->cfg, Graph::Edge::BACK);
      } else if (insn && insn->op == OP_BRA && !insn->getPredicate() &&
                 tailBB->cfg.incidentCount() == 0) {
         // RA doesn't like having blocks around with no incident edge,
         // so we create a fake one to make it happy
         bb->cfg.attach(&tailBB->cfg, Graph::Edge::TREE);
      }
   }

   curLoopDepth -= 1;

   return true;
}

bool
Converter::visit(nir_instr *insn)
{
   // we need an insertion point for on the fly generated immediate loads
   immInsertPos = bb->getExit();
   switch (insn->type) {
   case nir_instr_type_alu:
      return visit(nir_instr_as_alu(insn));
   case nir_instr_type_deref:
      return visit(nir_instr_as_deref(insn));
   case nir_instr_type_intrinsic:
      return visit(nir_instr_as_intrinsic(insn));
   case nir_instr_type_jump:
      return visit(nir_instr_as_jump(insn));
   case nir_instr_type_load_const:
      return visit(nir_instr_as_load_const(insn));
   case nir_instr_type_ssa_undef:
      return visit(nir_instr_as_ssa_undef(insn));
   case nir_instr_type_tex:
      return visit(nir_instr_as_tex(insn));
   default:
      ERROR("unknown nir_instr type %u\n", insn->type);
      return false;
   }
   return true;
}

SVSemantic
Converter::convert(nir_intrinsic_op intr)
{
   switch (intr) {
   case nir_intrinsic_load_base_vertex:
      return SV_BASEVERTEX;
   case nir_intrinsic_load_base_instance:
      return SV_BASEINSTANCE;
   case nir_intrinsic_load_draw_id:
      return SV_DRAWID;
   case nir_intrinsic_load_front_face:
      return SV_FACE;
   case nir_intrinsic_load_helper_invocation:
      return SV_THREAD_KILL;
   case nir_intrinsic_load_instance_id:
      return SV_INSTANCE_ID;
   case nir_intrinsic_load_invocation_id:
      return SV_INVOCATION_ID;
   case nir_intrinsic_load_local_group_size:
      return SV_NTID;
   case nir_intrinsic_load_local_invocation_id:
      return SV_TID;
   case nir_intrinsic_load_num_work_groups:
      return SV_NCTAID;
   case nir_intrinsic_load_patch_vertices_in:
      return SV_VERTEX_COUNT;
   case nir_intrinsic_load_primitive_id:
      return SV_PRIMITIVE_ID;
   case nir_intrinsic_load_sample_id:
      return SV_SAMPLE_INDEX;
   case nir_intrinsic_load_sample_mask_in:
      return SV_SAMPLE_MASK;
   case nir_intrinsic_load_sample_pos:
      return SV_SAMPLE_POS;
   case nir_intrinsic_load_subgroup_eq_mask:
      return SV_LANEMASK_EQ;
   case nir_intrinsic_load_subgroup_ge_mask:
      return SV_LANEMASK_GE;
   case nir_intrinsic_load_subgroup_gt_mask:
      return SV_LANEMASK_GT;
   case nir_intrinsic_load_subgroup_le_mask:
      return SV_LANEMASK_LE;
   case nir_intrinsic_load_subgroup_lt_mask:
      return SV_LANEMASK_LT;
   case nir_intrinsic_load_subgroup_invocation:
      return SV_LANEID;
   case nir_intrinsic_load_tess_coord:
      return SV_TESS_COORD;
   case nir_intrinsic_load_tess_level_inner:
      return SV_TESS_INNER;
   case nir_intrinsic_load_tess_level_outer:
      return SV_TESS_OUTER;
   case nir_intrinsic_load_vertex_id:
      return SV_VERTEX_ID;
   case nir_intrinsic_load_work_group_id:
      return SV_CTAID;
   default:
      ERROR("unknown SVSemantic for nir_intrinsic_op %s\n",
            nir_intrinsic_infos[intr].name);
      assert(false);
      return SV_LAST;
   }
}

ImgFormat
Converter::convertGLImgFormat(GLuint format)
{
#define FMT_CASE(a, b) \
  case GL_ ## a: return nv50_ir::FMT_ ## b

   switch (format) {
   FMT_CASE(NONE, NONE);

   FMT_CASE(RGBA32F, RGBA32F);
   FMT_CASE(RGBA16F, RGBA16F);
   FMT_CASE(RG32F, RG32F);
   FMT_CASE(RG16F, RG16F);
   FMT_CASE(R11F_G11F_B10F, R11G11B10F);
   FMT_CASE(R32F, R32F);
   FMT_CASE(R16F, R16F);

   FMT_CASE(RGBA32UI, RGBA32UI);
   FMT_CASE(RGBA16UI, RGBA16UI);
   FMT_CASE(RGB10_A2UI, RGB10A2UI);
   FMT_CASE(RGBA8UI, RGBA8UI);
   FMT_CASE(RG32UI, RG32UI);
   FMT_CASE(RG16UI, RG16UI);
   FMT_CASE(RG8UI, RG8UI);
   FMT_CASE(R32UI, R32UI);
   FMT_CASE(R16UI, R16UI);
   FMT_CASE(R8UI, R8UI);

   FMT_CASE(RGBA32I, RGBA32I);
   FMT_CASE(RGBA16I, RGBA16I);
   FMT_CASE(RGBA8I, RGBA8I);
   FMT_CASE(RG32I, RG32I);
   FMT_CASE(RG16I, RG16I);
   FMT_CASE(RG8I, RG8I);
   FMT_CASE(R32I, R32I);
   FMT_CASE(R16I, R16I);
   FMT_CASE(R8I, R8I);

   FMT_CASE(RGBA16, RGBA16);
   FMT_CASE(RGB10_A2, RGB10A2);
   FMT_CASE(RGBA8, RGBA8);
   FMT_CASE(RG16, RG16);
   FMT_CASE(RG8, RG8);
   FMT_CASE(R16, R16);
   FMT_CASE(R8, R8);

   FMT_CASE(RGBA16_SNORM, RGBA16_SNORM);
   FMT_CASE(RGBA8_SNORM, RGBA8_SNORM);
   FMT_CASE(RG16_SNORM, RG16_SNORM);
   FMT_CASE(RG8_SNORM, RG8_SNORM);
   FMT_CASE(R16_SNORM, R16_SNORM);
   FMT_CASE(R8_SNORM, R8_SNORM);

   FMT_CASE(BGRA_INTEGER, BGRA8);
   default:
      ERROR("unknown format %x\n", format);
      assert(false);
      return nv50_ir::FMT_NONE;
   }
#undef FMT_CASE
}

bool
Converter::visit(nir_intrinsic_instr *insn)
{
   nir_intrinsic_op op = insn->intrinsic;
   const nir_intrinsic_info &opInfo = nir_intrinsic_infos[op];

   switch (op) {
   case nir_intrinsic_load_uniform: {
      LValues &newDefs = convert(&insn->dest);
      const DataType dType = getDType(insn);
      Value *indirect;
      uint32_t coffset = getIndirect(insn, 0, 0, indirect);
      for (uint8_t i = 0; i < insn->num_components; ++i) {
         loadFrom(FILE_MEMORY_CONST, 0, dType, newDefs[i], 16 * coffset, i, indirect);
      }
      break;
   }
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_vertex_output: {
      Value *indirect;
      DataType dType = getSType(insn->src[0], false, false);
      uint32_t idx = getIndirect(insn, op == nir_intrinsic_store_output ? 1 : 2, 0, indirect);

      for (uint8_t i = 0u; i < insn->num_components; ++i) {
         if (!((1u << i) & nir_intrinsic_write_mask(insn)))
            continue;

         uint8_t offset = 0;
         Value *src = getSrc(&insn->src[0], i);
         switch (prog->getType()) {
         case Program::TYPE_FRAGMENT: {
            if (info->out[idx].sn == TGSI_SEMANTIC_POSITION) {
               // TGSI uses a different interface than NIR, TGSI stores that
               // value in the z component, NIR in X
               offset += 2;
               src = mkOp1v(OP_SAT, TYPE_F32, getScratch(), src);
            }
            break;
         }
         case Program::TYPE_GEOMETRY:
         case Program::TYPE_VERTEX: {
            if (info->io.genUserClip > 0 && idx == clipVertexOutput) {
               mkMov(clipVtx[i], src);
               src = clipVtx[i];
            }
            break;
         }
         default:
            break;
         }

         storeTo(insn, FILE_SHADER_OUTPUT, OP_EXPORT, dType, src, idx, i + offset, indirect);
      }
      break;
   }
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_load_output: {
      LValues &newDefs = convert(&insn->dest);

      // FBFetch
      if (prog->getType() == Program::TYPE_FRAGMENT &&
          op == nir_intrinsic_load_output) {
         std::vector<Value*> defs, srcs;
         uint8_t mask = 0;

         srcs.push_back(getSSA());
         srcs.push_back(getSSA());
         Value *x = mkOp1v(OP_RDSV, TYPE_F32, getSSA(), mkSysVal(SV_POSITION, 0));
         Value *y = mkOp1v(OP_RDSV, TYPE_F32, getSSA(), mkSysVal(SV_POSITION, 1));
         mkCvt(OP_CVT, TYPE_U32, srcs[0], TYPE_F32, x)->rnd = ROUND_Z;
         mkCvt(OP_CVT, TYPE_U32, srcs[1], TYPE_F32, y)->rnd = ROUND_Z;

         srcs.push_back(mkOp1v(OP_RDSV, TYPE_U32, getSSA(), mkSysVal(SV_LAYER, 0)));
         srcs.push_back(mkOp1v(OP_RDSV, TYPE_U32, getSSA(), mkSysVal(SV_SAMPLE_INDEX, 0)));

         for (uint8_t i = 0u; i < insn->num_components; ++i) {
            defs.push_back(newDefs[i]);
            mask |= 1 << i;
         }

         TexInstruction *texi = mkTex(OP_TXF, TEX_TARGET_2D_MS_ARRAY, 0, 0, defs, srcs);
         texi->tex.levelZero = 1;
         texi->tex.mask = mask;
         texi->tex.useOffsets = 0;
         texi->tex.r = 0xffff;
         texi->tex.s = 0xffff;

         info->prop.fp.readsFramebuffer = true;
         break;
      }

      const DataType dType = getDType(insn);
      Value *indirect;
      bool input = op != nir_intrinsic_load_output;
      operation nvirOp;
      uint32_t mode = 0;

      uint32_t idx = getIndirect(insn, op == nir_intrinsic_load_interpolated_input ? 1 : 0, 0, indirect);
      nv50_ir_varying& vary = input ? info->in[idx] : info->out[idx];

      // see load_barycentric_* handling
      if (prog->getType() == Program::TYPE_FRAGMENT) {
         mode = translateInterpMode(&vary, nvirOp);
         if (op == nir_intrinsic_load_interpolated_input) {
            ImmediateValue immMode;
            if (getSrc(&insn->src[0], 1)->getUniqueInsn()->src(0).getImmediate(immMode))
               mode |= immMode.reg.data.u32;
         }
      }

      for (uint8_t i = 0u; i < insn->num_components; ++i) {
         uint32_t address = getSlotAddress(insn, idx, i);
         Symbol *sym = mkSymbol(input ? FILE_SHADER_INPUT : FILE_SHADER_OUTPUT, 0, dType, address);
         if (prog->getType() == Program::TYPE_FRAGMENT) {
            int s = 1;
            if (typeSizeof(dType) == 8) {
               Value *lo = getSSA();
               Value *hi = getSSA();
               Instruction *interp;

               interp = mkOp1(nvirOp, TYPE_U32, lo, sym);
               if (nvirOp == OP_PINTERP)
                  interp->setSrc(s++, fp.position);
               if (mode & NV50_IR_INTERP_OFFSET)
                  interp->setSrc(s++, getSrc(&insn->src[0], 0));
               interp->setInterpolate(mode);
               interp->setIndirect(0, 0, indirect);

               Symbol *sym1 = mkSymbol(input ? FILE_SHADER_INPUT : FILE_SHADER_OUTPUT, 0, dType, address + 4);
               interp = mkOp1(nvirOp, TYPE_U32, hi, sym1);
               if (nvirOp == OP_PINTERP)
                  interp->setSrc(s++, fp.position);
               if (mode & NV50_IR_INTERP_OFFSET)
                  interp->setSrc(s++, getSrc(&insn->src[0], 0));
               interp->setInterpolate(mode);
               interp->setIndirect(0, 0, indirect);

               mkOp2(OP_MERGE, dType, newDefs[i], lo, hi);
            } else {
               Instruction *interp = mkOp1(nvirOp, dType, newDefs[i], sym);
               if (nvirOp == OP_PINTERP)
                  interp->setSrc(s++, fp.position);
               if (mode & NV50_IR_INTERP_OFFSET)
                  interp->setSrc(s++, getSrc(&insn->src[0], 0));
               interp->setInterpolate(mode);
               interp->setIndirect(0, 0, indirect);
            }
         } else {
            mkLoad(dType, newDefs[i], sym, indirect)->perPatch = vary.patch;
         }
      }
      break;
   }
   case nir_intrinsic_load_barycentric_at_offset:
   case nir_intrinsic_load_barycentric_at_sample:
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_sample: {
      LValues &newDefs = convert(&insn->dest);
      uint32_t mode;

      if (op == nir_intrinsic_load_barycentric_centroid ||
          op == nir_intrinsic_load_barycentric_sample) {
         mode = NV50_IR_INTERP_CENTROID;
      } else if (op == nir_intrinsic_load_barycentric_at_offset) {
         Value *offs[2];
         for (uint8_t c = 0; c < 2; c++) {
            offs[c] = getScratch();
            mkOp2(OP_MIN, TYPE_F32, offs[c], getSrc(&insn->src[0], c), loadImm(NULL, 0.4375f));
            mkOp2(OP_MAX, TYPE_F32, offs[c], offs[c], loadImm(NULL, -0.5f));
            mkOp2(OP_MUL, TYPE_F32, offs[c], offs[c], loadImm(NULL, 4096.0f));
            mkCvt(OP_CVT, TYPE_S32, offs[c], TYPE_F32, offs[c]);
         }
         mkOp3v(OP_INSBF, TYPE_U32, newDefs[0], offs[1], mkImm(0x1010), offs[0]);

         mode = NV50_IR_INTERP_OFFSET;
      } else if (op == nir_intrinsic_load_barycentric_pixel) {
         mode = NV50_IR_INTERP_DEFAULT;
      } else if (op == nir_intrinsic_load_barycentric_at_sample) {
         info->prop.fp.readsSampleLocations = true;
         mkOp1(OP_PIXLD, TYPE_U32, newDefs[0], getSrc(&insn->src[0], 0))->subOp = NV50_IR_SUBOP_PIXLD_OFFSET;
         mode = NV50_IR_INTERP_OFFSET;
      } else {
         unreachable("all intrinsics already handled above");
      }

      loadImm(newDefs[1], mode);
      break;
   }
   case nir_intrinsic_discard:
      mkOp(OP_DISCARD, TYPE_NONE, NULL);
      break;
   case nir_intrinsic_discard_if: {
      Value *pred = getSSA(1, FILE_PREDICATE);
      if (insn->num_components > 1) {
         ERROR("nir_intrinsic_discard_if only with 1 component supported!\n");
         assert(false);
         return false;
      }
      mkCmp(OP_SET, CC_NE, TYPE_U8, pred, TYPE_U32, getSrc(&insn->src[0], 0), zero);
      mkOp(OP_DISCARD, TYPE_NONE, NULL)->setPredicate(CC_P, pred);
      break;
   }
   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_base_instance:
   case nir_intrinsic_load_draw_id:
   case nir_intrinsic_load_front_face:
   case nir_intrinsic_load_helper_invocation:
   case nir_intrinsic_load_instance_id:
   case nir_intrinsic_load_invocation_id:
   case nir_intrinsic_load_local_group_size:
   case nir_intrinsic_load_local_invocation_id:
   case nir_intrinsic_load_num_work_groups:
   case nir_intrinsic_load_patch_vertices_in:
   case nir_intrinsic_load_primitive_id:
   case nir_intrinsic_load_sample_id:
   case nir_intrinsic_load_sample_mask_in:
   case nir_intrinsic_load_sample_pos:
   case nir_intrinsic_load_subgroup_eq_mask:
   case nir_intrinsic_load_subgroup_ge_mask:
   case nir_intrinsic_load_subgroup_gt_mask:
   case nir_intrinsic_load_subgroup_le_mask:
   case nir_intrinsic_load_subgroup_lt_mask:
   case nir_intrinsic_load_subgroup_invocation:
   case nir_intrinsic_load_tess_coord:
   case nir_intrinsic_load_tess_level_inner:
   case nir_intrinsic_load_tess_level_outer:
   case nir_intrinsic_load_vertex_id:
   case nir_intrinsic_load_work_group_id: {
      const DataType dType = getDType(insn);
      SVSemantic sv = convert(op);
      LValues &newDefs = convert(&insn->dest);

      for (uint8_t i = 0u; i < insn->num_components; ++i) {
         Value *def;
         if (typeSizeof(dType) == 8)
            def = getSSA();
         else
            def = newDefs[i];

         if (sv == SV_TID && info->prop.cp.numThreads[i] == 1) {
            loadImm(def, 0u);
         } else {
            Symbol *sym = mkSysVal(sv, i);
            Instruction *rdsv = mkOp1(OP_RDSV, TYPE_U32, def, sym);
            if (sv == SV_TESS_OUTER || sv == SV_TESS_INNER)
               rdsv->perPatch = 1;
         }

         if (typeSizeof(dType) == 8)
            mkOp2(OP_MERGE, dType, newDefs[i], def, loadImm(getSSA(), 0u));
      }
      break;
   }
   // constants
   case nir_intrinsic_load_subgroup_size: {
      LValues &newDefs = convert(&insn->dest);
      loadImm(newDefs[0], 32u);
      break;
   }
   case nir_intrinsic_vote_all:
   case nir_intrinsic_vote_any:
   case nir_intrinsic_vote_ieq: {
      LValues &newDefs = convert(&insn->dest);
      Value *pred = getScratch(1, FILE_PREDICATE);
      mkCmp(OP_SET, CC_NE, TYPE_U32, pred, TYPE_U32, getSrc(&insn->src[0], 0), zero);
      mkOp1(OP_VOTE, TYPE_U32, pred, pred)->subOp = getSubOp(op);
      mkCvt(OP_CVT, TYPE_U32, newDefs[0], TYPE_U8, pred);
      break;
   }
   case nir_intrinsic_ballot: {
      LValues &newDefs = convert(&insn->dest);
      Value *pred = getSSA(1, FILE_PREDICATE);
      mkCmp(OP_SET, CC_NE, TYPE_U32, pred, TYPE_U32, getSrc(&insn->src[0], 0), zero);
      mkOp1(OP_VOTE, TYPE_U32, newDefs[0], pred)->subOp = NV50_IR_SUBOP_VOTE_ANY;
      break;
   }
   case nir_intrinsic_read_first_invocation:
   case nir_intrinsic_read_invocation: {
      LValues &newDefs = convert(&insn->dest);
      const DataType dType = getDType(insn);
      Value *tmp = getScratch();

      if (op == nir_intrinsic_read_first_invocation) {
         mkOp1(OP_VOTE, TYPE_U32, tmp, mkImm(1))->subOp = NV50_IR_SUBOP_VOTE_ANY;
         mkOp2(OP_EXTBF, TYPE_U32, tmp, tmp, mkImm(0x2000))->subOp = NV50_IR_SUBOP_EXTBF_REV;
         mkOp1(OP_BFIND, TYPE_U32, tmp, tmp)->subOp = NV50_IR_SUBOP_BFIND_SAMT;
      } else
         tmp = getSrc(&insn->src[1], 0);

      for (uint8_t i = 0; i < insn->num_components; ++i) {
         mkOp3(OP_SHFL, dType, newDefs[i], getSrc(&insn->src[0], i), tmp, mkImm(0x1f))
            ->subOp = NV50_IR_SUBOP_SHFL_IDX;
      }
      break;
   }
   case nir_intrinsic_load_per_vertex_input: {
      const DataType dType = getDType(insn);
      LValues &newDefs = convert(&insn->dest);
      Value *indirectVertex;
      Value *indirectOffset;
      uint32_t baseVertex = getIndirect(&insn->src[0], 0, indirectVertex);
      uint32_t idx = getIndirect(insn, 1, 0, indirectOffset);

      Value *vtxBase = mkOp2v(OP_PFETCH, TYPE_U32, getSSA(4, FILE_ADDRESS),
                              mkImm(baseVertex), indirectVertex);
      for (uint8_t i = 0u; i < insn->num_components; ++i) {
         uint32_t address = getSlotAddress(insn, idx, i);
         loadFrom(FILE_SHADER_INPUT, 0, dType, newDefs[i], address, 0,
                  indirectOffset, vtxBase, info->in[idx].patch);
      }
      break;
   }
   case nir_intrinsic_load_per_vertex_output: {
      const DataType dType = getDType(insn);
      LValues &newDefs = convert(&insn->dest);
      Value *indirectVertex;
      Value *indirectOffset;
      uint32_t baseVertex = getIndirect(&insn->src[0], 0, indirectVertex);
      uint32_t idx = getIndirect(insn, 1, 0, indirectOffset);
      Value *vtxBase = NULL;

      if (indirectVertex)
         vtxBase = indirectVertex;
      else
         vtxBase = loadImm(NULL, baseVertex);

      vtxBase = mkOp2v(OP_ADD, TYPE_U32, getSSA(4, FILE_ADDRESS), outBase, vtxBase);

      for (uint8_t i = 0u; i < insn->num_components; ++i) {
         uint32_t address = getSlotAddress(insn, idx, i);
         loadFrom(FILE_SHADER_OUTPUT, 0, dType, newDefs[i], address, 0,
                  indirectOffset, vtxBase, info->in[idx].patch);
      }
      break;
   }
   case nir_intrinsic_emit_vertex:
      if (info->io.genUserClip > 0)
         handleUserClipPlanes();
      // fallthrough
   case nir_intrinsic_end_primitive: {
      uint32_t idx = nir_intrinsic_stream_id(insn);
      mkOp1(getOperation(op), TYPE_U32, NULL, mkImm(idx))->fixed = 1;
      break;
   }
   case nir_intrinsic_load_ubo: {
      const DataType dType = getDType(insn);
      LValues &newDefs = convert(&insn->dest);
      Value *indirectIndex;
      Value *indirectOffset;
      uint32_t index = getIndirect(&insn->src[0], 0, indirectIndex) + 1;
      uint32_t offset = getIndirect(&insn->src[1], 0, indirectOffset);

      for (uint8_t i = 0u; i < insn->num_components; ++i) {
         loadFrom(FILE_MEMORY_CONST, index, dType, newDefs[i], offset, i,
                  indirectOffset, indirectIndex);
      }
      break;
   }
   case nir_intrinsic_get_buffer_size: {
      LValues &newDefs = convert(&insn->dest);
      const DataType dType = getDType(insn);
      Value *indirectBuffer;
      uint32_t buffer = getIndirect(&insn->src[0], 0, indirectBuffer);

      Symbol *sym = mkSymbol(FILE_MEMORY_BUFFER, buffer, dType, 0);
      mkOp1(OP_BUFQ, dType, newDefs[0], sym)->setIndirect(0, 0, indirectBuffer);
      break;
   }
   case nir_intrinsic_store_ssbo: {
      DataType sType = getSType(insn->src[0], false, false);
      Value *indirectBuffer;
      Value *indirectOffset;
      uint32_t buffer = getIndirect(&insn->src[1], 0, indirectBuffer);
      uint32_t offset = getIndirect(&insn->src[2], 0, indirectOffset);

      for (uint8_t i = 0u; i < insn->num_components; ++i) {
         if (!((1u << i) & nir_intrinsic_write_mask(insn)))
            continue;
         Symbol *sym = mkSymbol(FILE_MEMORY_BUFFER, buffer, sType,
                                offset + i * typeSizeof(sType));
         mkStore(OP_STORE, sType, sym, indirectOffset, getSrc(&insn->src[0], i))
            ->setIndirect(0, 1, indirectBuffer);
      }
      info->io.globalAccess |= 0x2;
      break;
   }
   case nir_intrinsic_load_ssbo: {
      const DataType dType = getDType(insn);
      LValues &newDefs = convert(&insn->dest);
      Value *indirectBuffer;
      Value *indirectOffset;
      uint32_t buffer = getIndirect(&insn->src[0], 0, indirectBuffer);
      uint32_t offset = getIndirect(&insn->src[1], 0, indirectOffset);

      for (uint8_t i = 0u; i < insn->num_components; ++i)
         loadFrom(FILE_MEMORY_BUFFER, buffer, dType, newDefs[i], offset, i,
                  indirectOffset, indirectBuffer);

      info->io.globalAccess |= 0x1;
      break;
   }
   case nir_intrinsic_shared_atomic_add:
   case nir_intrinsic_shared_atomic_and:
   case nir_intrinsic_shared_atomic_comp_swap:
   case nir_intrinsic_shared_atomic_exchange:
   case nir_intrinsic_shared_atomic_or:
   case nir_intrinsic_shared_atomic_imax:
   case nir_intrinsic_shared_atomic_imin:
   case nir_intrinsic_shared_atomic_umax:
   case nir_intrinsic_shared_atomic_umin:
   case nir_intrinsic_shared_atomic_xor: {
      const DataType dType = getDType(insn);
      LValues &newDefs = convert(&insn->dest);
      Value *indirectOffset;
      uint32_t offset = getIndirect(&insn->src[0], 0, indirectOffset);
      Symbol *sym = mkSymbol(FILE_MEMORY_SHARED, 0, dType, offset);
      Instruction *atom = mkOp2(OP_ATOM, dType, newDefs[0], sym, getSrc(&insn->src[1], 0));
      if (op == nir_intrinsic_shared_atomic_comp_swap)
         atom->setSrc(2, getSrc(&insn->src[2], 0));
      atom->setIndirect(0, 0, indirectOffset);
      atom->subOp = getSubOp(op);
      break;
   }
   case nir_intrinsic_ssbo_atomic_add:
   case nir_intrinsic_ssbo_atomic_and:
   case nir_intrinsic_ssbo_atomic_comp_swap:
   case nir_intrinsic_ssbo_atomic_exchange:
   case nir_intrinsic_ssbo_atomic_or:
   case nir_intrinsic_ssbo_atomic_imax:
   case nir_intrinsic_ssbo_atomic_imin:
   case nir_intrinsic_ssbo_atomic_umax:
   case nir_intrinsic_ssbo_atomic_umin:
   case nir_intrinsic_ssbo_atomic_xor: {
      const DataType dType = getDType(insn);
      LValues &newDefs = convert(&insn->dest);
      Value *indirectBuffer;
      Value *indirectOffset;
      uint32_t buffer = getIndirect(&insn->src[0], 0, indirectBuffer);
      uint32_t offset = getIndirect(&insn->src[1], 0, indirectOffset);

      Symbol *sym = mkSymbol(FILE_MEMORY_BUFFER, buffer, dType, offset);
      Instruction *atom = mkOp2(OP_ATOM, dType, newDefs[0], sym,
                                getSrc(&insn->src[2], 0));
      if (op == nir_intrinsic_ssbo_atomic_comp_swap)
         atom->setSrc(2, getSrc(&insn->src[3], 0));
      atom->setIndirect(0, 0, indirectOffset);
      atom->setIndirect(0, 1, indirectBuffer);
      atom->subOp = getSubOp(op);

      info->io.globalAccess |= 0x2;
      break;
   }
   case nir_intrinsic_bindless_image_atomic_add:
   case nir_intrinsic_bindless_image_atomic_and:
   case nir_intrinsic_bindless_image_atomic_comp_swap:
   case nir_intrinsic_bindless_image_atomic_exchange:
   case nir_intrinsic_bindless_image_atomic_max:
   case nir_intrinsic_bindless_image_atomic_min:
   case nir_intrinsic_bindless_image_atomic_or:
   case nir_intrinsic_bindless_image_atomic_xor:
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_bindless_image_samples:
   case nir_intrinsic_bindless_image_size:
   case nir_intrinsic_bindless_image_store: {
      std::vector<Value*> srcs, defs;
      Value *indirect = getSrc(&insn->src[0], 0);
      DataType ty;

      uint32_t mask = 0;
      TexInstruction::Target target =
         convert(nir_intrinsic_image_dim(insn), !!nir_intrinsic_image_array(insn), false);
      unsigned int argCount = getNIRArgCount(target);
      uint16_t location = 0;

      if (opInfo.has_dest) {
         LValues &newDefs = convert(&insn->dest);
         for (uint8_t i = 0u; i < newDefs.size(); ++i) {
            defs.push_back(newDefs[i]);
            mask |= 1 << i;
         }
      }

      switch (op) {
      case nir_intrinsic_bindless_image_atomic_add:
      case nir_intrinsic_bindless_image_atomic_and:
      case nir_intrinsic_bindless_image_atomic_comp_swap:
      case nir_intrinsic_bindless_image_atomic_exchange:
      case nir_intrinsic_bindless_image_atomic_max:
      case nir_intrinsic_bindless_image_atomic_min:
      case nir_intrinsic_bindless_image_atomic_or:
      case nir_intrinsic_bindless_image_atomic_xor:
         ty = getDType(insn);
         mask = 0x1;
         info->io.globalAccess |= 0x2;
         break;
      case nir_intrinsic_bindless_image_load:
         ty = TYPE_U32;
         info->io.globalAccess |= 0x1;
         break;
      case nir_intrinsic_bindless_image_store:
         ty = TYPE_U32;
         mask = 0xf;
         info->io.globalAccess |= 0x2;
         break;
      case nir_intrinsic_bindless_image_samples:
         mask = 0x8;
         ty = TYPE_U32;
         break;
      case nir_intrinsic_bindless_image_size:
         ty = TYPE_U32;
         break;
      default:
         unreachable("unhandled image opcode");
         break;
      }

      // coords
      if (opInfo.num_srcs >= 2)
         for (unsigned int i = 0u; i < argCount; ++i)
            srcs.push_back(getSrc(&insn->src[1], i));

      // the sampler is just another src added after coords
      if (opInfo.num_srcs >= 3 && target.isMS())
         srcs.push_back(getSrc(&insn->src[2], 0));

      if (opInfo.num_srcs >= 4) {
         unsigned components = opInfo.src_components[3] ? opInfo.src_components[3] : insn->num_components;
         for (uint8_t i = 0u; i < components; ++i)
            srcs.push_back(getSrc(&insn->src[3], i));
      }

      if (opInfo.num_srcs >= 5)
         // 1 for aotmic swap
         for (uint8_t i = 0u; i < opInfo.src_components[4]; ++i)
            srcs.push_back(getSrc(&insn->src[4], i));

      TexInstruction *texi = mkTex(getOperation(op), target.getEnum(), location, 0, defs, srcs);
      texi->tex.bindless = false;
      texi->tex.format = &nv50_ir::TexInstruction::formatTable[convertGLImgFormat(nir_intrinsic_format(insn))];
      texi->tex.mask = mask;
      texi->tex.bindless = true;
      texi->cache = convert(nir_intrinsic_access(insn));
      texi->setType(ty);
      texi->subOp = getSubOp(op);

      if (indirect)
         texi->setIndirectR(indirect);

      break;
   }
   case nir_intrinsic_image_deref_atomic_add:
   case nir_intrinsic_image_deref_atomic_and:
   case nir_intrinsic_image_deref_atomic_comp_swap:
   case nir_intrinsic_image_deref_atomic_exchange:
   case nir_intrinsic_image_deref_atomic_max:
   case nir_intrinsic_image_deref_atomic_min:
   case nir_intrinsic_image_deref_atomic_or:
   case nir_intrinsic_image_deref_atomic_xor:
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_samples:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_store: {
      const nir_variable *tex;
      std::vector<Value*> srcs, defs;
      Value *indirect;
      DataType ty;

      uint32_t mask = 0;
      nir_deref_instr *deref = nir_src_as_deref(insn->src[0]);
      const glsl_type *type = deref->type;
      TexInstruction::Target target =
         convert((glsl_sampler_dim)type->sampler_dimensionality,
                 type->sampler_array, type->sampler_shadow);
      unsigned int argCount = getNIRArgCount(target);
      uint16_t location = handleDeref(deref, indirect, tex);

      if (opInfo.has_dest) {
         LValues &newDefs = convert(&insn->dest);
         for (uint8_t i = 0u; i < newDefs.size(); ++i) {
            defs.push_back(newDefs[i]);
            mask |= 1 << i;
         }
      }

      switch (op) {
      case nir_intrinsic_image_deref_atomic_add:
      case nir_intrinsic_image_deref_atomic_and:
      case nir_intrinsic_image_deref_atomic_comp_swap:
      case nir_intrinsic_image_deref_atomic_exchange:
      case nir_intrinsic_image_deref_atomic_max:
      case nir_intrinsic_image_deref_atomic_min:
      case nir_intrinsic_image_deref_atomic_or:
      case nir_intrinsic_image_deref_atomic_xor:
         ty = getDType(insn);
         mask = 0x1;
         info->io.globalAccess |= 0x2;
         break;
      case nir_intrinsic_image_deref_load:
         ty = TYPE_U32;
         info->io.globalAccess |= 0x1;
         break;
      case nir_intrinsic_image_deref_store:
         ty = TYPE_U32;
         mask = 0xf;
         info->io.globalAccess |= 0x2;
         break;
      case nir_intrinsic_image_deref_samples:
         mask = 0x8;
         ty = TYPE_U32;
         break;
      case nir_intrinsic_image_deref_size:
         ty = TYPE_U32;
         break;
      default:
         unreachable("unhandled image opcode");
         break;
      }

      // coords
      if (opInfo.num_srcs >= 2)
         for (unsigned int i = 0u; i < argCount; ++i)
            srcs.push_back(getSrc(&insn->src[1], i));

      // the sampler is just another src added after coords
      if (opInfo.num_srcs >= 3 && target.isMS())
         srcs.push_back(getSrc(&insn->src[2], 0));

      if (opInfo.num_srcs >= 4) {
         unsigned components = opInfo.src_components[3] ? opInfo.src_components[3] : insn->num_components;
         for (uint8_t i = 0u; i < components; ++i)
            srcs.push_back(getSrc(&insn->src[3], i));
      }

      if (opInfo.num_srcs >= 5)
         // 1 for aotmic swap
         for (uint8_t i = 0u; i < opInfo.src_components[4]; ++i)
            srcs.push_back(getSrc(&insn->src[4], i));

      TexInstruction *texi = mkTex(getOperation(op), target.getEnum(), location, 0, defs, srcs);
      texi->tex.bindless = false;
      texi->tex.format = &nv50_ir::TexInstruction::formatTable[convertGLImgFormat(tex->data.image.format)];
      texi->tex.mask = mask;
      texi->cache = getCacheModeFromVar(tex);
      texi->setType(ty);
      texi->subOp = getSubOp(op);

      if (indirect)
         texi->setIndirectR(indirect);

      break;
   }
   case nir_intrinsic_store_shared: {
      DataType sType = getSType(insn->src[0], false, false);
      Value *indirectOffset;
      uint32_t offset = getIndirect(&insn->src[1], 0, indirectOffset);

      for (uint8_t i = 0u; i < insn->num_components; ++i) {
         if (!((1u << i) & nir_intrinsic_write_mask(insn)))
            continue;
         Symbol *sym = mkSymbol(FILE_MEMORY_SHARED, 0, sType, offset + i * typeSizeof(sType));
         mkStore(OP_STORE, sType, sym, indirectOffset, getSrc(&insn->src[0], i));
      }
      break;
   }
   case nir_intrinsic_load_shared: {
      const DataType dType = getDType(insn);
      LValues &newDefs = convert(&insn->dest);
      Value *indirectOffset;
      uint32_t offset = getIndirect(&insn->src[0], 0, indirectOffset);

      for (uint8_t i = 0u; i < insn->num_components; ++i)
         loadFrom(FILE_MEMORY_SHARED, 0, dType, newDefs[i], offset, i, indirectOffset);

      break;
   }
   case nir_intrinsic_barrier: {
      // TODO: add flag to shader_info
      info->numBarriers = 1;
      Instruction *bar = mkOp2(OP_BAR, TYPE_U32, NULL, mkImm(0), mkImm(0));
      bar->fixed = 1;
      bar->subOp = NV50_IR_SUBOP_BAR_SYNC;
      break;
   }
   case nir_intrinsic_group_memory_barrier:
   case nir_intrinsic_memory_barrier:
   case nir_intrinsic_memory_barrier_atomic_counter:
   case nir_intrinsic_memory_barrier_buffer:
   case nir_intrinsic_memory_barrier_image:
   case nir_intrinsic_memory_barrier_shared: {
      Instruction *bar = mkOp(OP_MEMBAR, TYPE_NONE, NULL);
      bar->fixed = 1;
      bar->subOp = getSubOp(op);
      break;
   }
   case nir_intrinsic_shader_clock: {
      const DataType dType = getDType(insn);
      LValues &newDefs = convert(&insn->dest);

      loadImm(newDefs[0], 0u);
      mkOp1(OP_RDSV, dType, newDefs[1], mkSysVal(SV_CLOCK, 0))->fixed = 1;
      break;
   }
   default:
      ERROR("unknown nir_intrinsic_op %s\n", nir_intrinsic_infos[op].name);
      return false;
   }

   return true;
}

bool
Converter::visit(nir_jump_instr *insn)
{
   switch (insn->type) {
   case nir_jump_return:
      // TODO: this only works in the main function
      mkFlow(OP_BRA, exit, CC_ALWAYS, NULL);
      bb->cfg.attach(&exit->cfg, Graph::Edge::CROSS);
      break;
   case nir_jump_break:
   case nir_jump_continue: {
      bool isBreak = insn->type == nir_jump_break;
      nir_block *block = insn->instr.block;
      assert(!block->successors[1]);
      BasicBlock *target = convert(block->successors[0]);
      mkFlow(isBreak ? OP_BREAK : OP_CONT, target, CC_ALWAYS, NULL);
      bb->cfg.attach(&target->cfg, isBreak ? Graph::Edge::CROSS : Graph::Edge::BACK);
      break;
   }
   default:
      ERROR("unknown nir_jump_type %u\n", insn->type);
      return false;
   }

   return true;
}

Value*
Converter::convert(nir_load_const_instr *insn, uint8_t idx)
{
   Value *val;

   if (immInsertPos)
      setPosition(immInsertPos, true);
   else
      setPosition(bb, false);

   switch (insn->def.bit_size) {
   case 64:
      val = loadImm(getSSA(8), insn->value[idx].u64);
      break;
   case 32:
      val = loadImm(getSSA(4), insn->value[idx].u32);
      break;
   case 16:
      val = loadImm(getSSA(2), insn->value[idx].u16);
      break;
   case 8:
      val = loadImm(getSSA(1), insn->value[idx].u8);
      break;
   default:
      unreachable("unhandled bit size!\n");
   }
   setPosition(bb, true);
   return val;
}

bool
Converter::visit(nir_load_const_instr *insn)
{
   assert(insn->def.bit_size <= 64);
   immediates[insn->def.index] = insn;
   return true;
}

#define DEFAULT_CHECKS \
      if (insn->dest.dest.ssa.num_components > 1) { \
         ERROR("nir_alu_instr only supported with 1 component!\n"); \
         return false; \
      } \
      if (insn->dest.write_mask != 1) { \
         ERROR("nir_alu_instr only with write_mask of 1 supported!\n"); \
         return false; \
      }
bool
Converter::visit(nir_alu_instr *insn)
{
   const nir_op op = insn->op;
   const nir_op_info &info = nir_op_infos[op];
   DataType dType = getDType(insn);
   const std::vector<DataType> sTypes = getSTypes(insn);

   Instruction *oldPos = this->bb->getExit();

   switch (op) {
   case nir_op_fabs:
   case nir_op_iabs:
   case nir_op_fadd:
   case nir_op_iadd:
   case nir_op_fand:
   case nir_op_iand:
   case nir_op_fceil:
   case nir_op_fcos:
   case nir_op_fddx:
   case nir_op_fddx_coarse:
   case nir_op_fddx_fine:
   case nir_op_fddy:
   case nir_op_fddy_coarse:
   case nir_op_fddy_fine:
   case nir_op_fdiv:
   case nir_op_idiv:
   case nir_op_udiv:
   case nir_op_fexp2:
   case nir_op_ffloor:
   case nir_op_ffma:
   case nir_op_flog2:
   case nir_op_fmax:
   case nir_op_imax:
   case nir_op_umax:
   case nir_op_fmin:
   case nir_op_imin:
   case nir_op_umin:
   case nir_op_fmod:
   case nir_op_imod:
   case nir_op_umod:
   case nir_op_fmul:
   case nir_op_imul:
   case nir_op_imul_high:
   case nir_op_umul_high:
   case nir_op_fneg:
   case nir_op_ineg:
   case nir_op_fnot:
   case nir_op_inot:
   case nir_op_for:
   case nir_op_ior:
   case nir_op_pack_64_2x32_split:
   case nir_op_fpow:
   case nir_op_frcp:
   case nir_op_frem:
   case nir_op_irem:
   case nir_op_frsq:
   case nir_op_fsat:
   case nir_op_ishr:
   case nir_op_ushr:
   case nir_op_fsin:
   case nir_op_fsqrt:
   case nir_op_fsub:
   case nir_op_isub:
   case nir_op_ftrunc:
   case nir_op_ishl:
   case nir_op_fxor:
   case nir_op_ixor: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      operation preOp = preOperationNeeded(op);
      if (preOp != OP_NOP) {
         assert(info.num_inputs < 2);
         Value *tmp = getSSA(typeSizeof(dType));
         Instruction *i0 = mkOp(preOp, dType, tmp);
         Instruction *i1 = mkOp(getOperation(op), dType, newDefs[0]);
         if (info.num_inputs) {
            i0->setSrc(0, getSrc(&insn->src[0]));
            i1->setSrc(0, tmp);
         }
         i1->subOp = getSubOp(op);
      } else {
         Instruction *i = mkOp(getOperation(op), dType, newDefs[0]);
         for (unsigned s = 0u; s < info.num_inputs; ++s) {
            i->setSrc(s, getSrc(&insn->src[s]));
         }
         i->subOp = getSubOp(op);
      }
      break;
   }
   case nir_op_ifind_msb:
   case nir_op_ufind_msb: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      dType = sTypes[0];
      mkOp1(getOperation(op), dType, newDefs[0], getSrc(&insn->src[0]));
      break;
   }
   case nir_op_fround_even: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      mkCvt(OP_CVT, dType, newDefs[0], dType, getSrc(&insn->src[0]))->rnd = ROUND_NI;
      break;
   }
   // convert instructions
   case nir_op_f2f32:
   case nir_op_f2i32:
   case nir_op_f2u32:
   case nir_op_i2f32:
   case nir_op_i2i32:
   case nir_op_u2f32:
   case nir_op_u2u32:
   case nir_op_f2f64:
   case nir_op_f2i64:
   case nir_op_f2u64:
   case nir_op_i2f64:
   case nir_op_i2i64:
   case nir_op_u2f64:
   case nir_op_u2u64: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      Instruction *i = mkOp1(getOperation(op), dType, newDefs[0], getSrc(&insn->src[0]));
      if (op == nir_op_f2i32 || op == nir_op_f2i64 || op == nir_op_f2u32 || op == nir_op_f2u64)
         i->rnd = ROUND_Z;
      i->sType = sTypes[0];
      break;
   }
   // compare instructions
   case nir_op_feq32:
   case nir_op_ieq32:
   case nir_op_fge32:
   case nir_op_ige32:
   case nir_op_uge32:
   case nir_op_flt32:
   case nir_op_ilt32:
   case nir_op_ult32:
   case nir_op_fne32:
   case nir_op_ine32: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      Instruction *i = mkCmp(getOperation(op),
                             getCondCode(op),
                             dType,
                             newDefs[0],
                             dType,
                             getSrc(&insn->src[0]),
                             getSrc(&insn->src[1]));
      if (info.num_inputs == 3)
         i->setSrc(2, getSrc(&insn->src[2]));
      i->sType = sTypes[0];
      break;
   }
   // those are weird ALU ops and need special handling, because
   //   1. they are always componend based
   //   2. they basically just merge multiple values into one data type
   case nir_op_imov:
   case nir_op_fmov:
      if (!insn->dest.dest.is_ssa && insn->dest.dest.reg.reg->num_array_elems) {
         nir_reg_dest& reg = insn->dest.dest.reg;
         uint32_t goffset = regToLmemOffset[reg.reg->index];
         uint8_t comps = reg.reg->num_components;
         uint8_t size = reg.reg->bit_size / 8;
         uint8_t csize = 4 * size; // TODO after fixing MemoryOpts: comps * size;
         uint32_t aoffset = csize * reg.base_offset;
         Value *indirect = NULL;

         if (reg.indirect)
            indirect = mkOp2v(OP_MUL, TYPE_U32, getSSA(4, FILE_ADDRESS),
                              getSrc(reg.indirect, 0), mkImm(csize));

         for (uint8_t i = 0u; i < comps; ++i) {
            if (!((1u << i) & insn->dest.write_mask))
               continue;

            Symbol *sym = mkSymbol(FILE_MEMORY_LOCAL, 0, dType, goffset + aoffset + i * size);
            mkStore(OP_STORE, dType, sym, indirect, getSrc(&insn->src[0], i));
         }
         break;
      } else if (!insn->src[0].src.is_ssa && insn->src[0].src.reg.reg->num_array_elems) {
         LValues &newDefs = convert(&insn->dest);
         nir_reg_src& reg = insn->src[0].src.reg;
         uint32_t goffset = regToLmemOffset[reg.reg->index];
         // uint8_t comps = reg.reg->num_components;
         uint8_t size = reg.reg->bit_size / 8;
         uint8_t csize = 4 * size; // TODO after fixing MemoryOpts: comps * size;
         uint32_t aoffset = csize * reg.base_offset;
         Value *indirect = NULL;

         if (reg.indirect)
            indirect = mkOp2v(OP_MUL, TYPE_U32, getSSA(4, FILE_ADDRESS), getSrc(reg.indirect, 0), mkImm(csize));

         for (uint8_t i = 0u; i < newDefs.size(); ++i)
            loadFrom(FILE_MEMORY_LOCAL, 0, dType, newDefs[i], goffset + aoffset, i, indirect);

         break;
      } else {
         LValues &newDefs = convert(&insn->dest);
         for (LValues::size_type c = 0u; c < newDefs.size(); ++c) {
            mkMov(newDefs[c], getSrc(&insn->src[0], c), dType);
         }
      }
      break;
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4: {
      LValues &newDefs = convert(&insn->dest);
      for (LValues::size_type c = 0u; c < newDefs.size(); ++c) {
         mkMov(newDefs[c], getSrc(&insn->src[c]), dType);
      }
      break;
   }
   // (un)pack
   case nir_op_pack_64_2x32: {
      LValues &newDefs = convert(&insn->dest);
      Instruction *merge = mkOp(OP_MERGE, dType, newDefs[0]);
      merge->setSrc(0, getSrc(&insn->src[0], 0));
      merge->setSrc(1, getSrc(&insn->src[0], 1));
      break;
   }
   case nir_op_pack_half_2x16_split: {
      LValues &newDefs = convert(&insn->dest);
      Value *tmpH = getSSA();
      Value *tmpL = getSSA();

      mkCvt(OP_CVT, TYPE_F16, tmpL, TYPE_F32, getSrc(&insn->src[0]));
      mkCvt(OP_CVT, TYPE_F16, tmpH, TYPE_F32, getSrc(&insn->src[1]));
      mkOp3(OP_INSBF, TYPE_U32, newDefs[0], tmpH, mkImm(0x1010), tmpL);
      break;
   }
   case nir_op_unpack_half_2x16_split_x:
   case nir_op_unpack_half_2x16_split_y: {
      LValues &newDefs = convert(&insn->dest);
      Instruction *cvt = mkCvt(OP_CVT, TYPE_F32, newDefs[0], TYPE_F16, getSrc(&insn->src[0]));
      if (op == nir_op_unpack_half_2x16_split_y)
         cvt->subOp = 1;
      break;
   }
   case nir_op_unpack_64_2x32: {
      LValues &newDefs = convert(&insn->dest);
      mkOp1(OP_SPLIT, dType, newDefs[0], getSrc(&insn->src[0]))->setDef(1, newDefs[1]);
      break;
   }
   case nir_op_unpack_64_2x32_split_x: {
      LValues &newDefs = convert(&insn->dest);
      mkOp1(OP_SPLIT, dType, newDefs[0], getSrc(&insn->src[0]))->setDef(1, getSSA());
      break;
   }
   case nir_op_unpack_64_2x32_split_y: {
      LValues &newDefs = convert(&insn->dest);
      mkOp1(OP_SPLIT, dType, getSSA(), getSrc(&insn->src[0]))->setDef(1, newDefs[0]);
      break;
   }
   // special instructions
   case nir_op_fsign:
   case nir_op_isign: {
      DEFAULT_CHECKS;
      DataType iType;
      if (::isFloatType(dType))
         iType = TYPE_F32;
      else
         iType = TYPE_S32;

      LValues &newDefs = convert(&insn->dest);
      LValue *val0 = getScratch();
      LValue *val1 = getScratch();
      mkCmp(OP_SET, CC_GT, iType, val0, dType, getSrc(&insn->src[0]), zero);
      mkCmp(OP_SET, CC_LT, iType, val1, dType, getSrc(&insn->src[0]), zero);

      if (dType == TYPE_F64) {
         mkOp2(OP_SUB, iType, val0, val0, val1);
         mkCvt(OP_CVT, TYPE_F64, newDefs[0], iType, val0);
      } else if (dType == TYPE_S64 || dType == TYPE_U64) {
         mkOp2(OP_SUB, iType, val0, val1, val0);
         mkOp2(OP_SHR, iType, val1, val0, loadImm(NULL, 31));
         mkOp2(OP_MERGE, dType, newDefs[0], val0, val1);
      } else if (::isFloatType(dType))
         mkOp2(OP_SUB, iType, newDefs[0], val0, val1);
      else
         mkOp2(OP_SUB, iType, newDefs[0], val1, val0);
      break;
   }
   case nir_op_fcsel:
   case nir_op_b32csel: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      mkCmp(OP_SLCT, CC_NE, dType, newDefs[0], sTypes[0], getSrc(&insn->src[1]), getSrc(&insn->src[2]), getSrc(&insn->src[0]));
      break;
   }
   case nir_op_ibitfield_extract:
   case nir_op_ubitfield_extract: {
      DEFAULT_CHECKS;
      Value *tmp = getSSA();
      LValues &newDefs = convert(&insn->dest);
      mkOp3(OP_INSBF, dType, tmp, getSrc(&insn->src[2]), loadImm(NULL, 0x808), getSrc(&insn->src[1]));
      mkOp2(OP_EXTBF, dType, newDefs[0], getSrc(&insn->src[0]), tmp);
      break;
   }
   case nir_op_bfm: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      mkOp3(OP_INSBF, dType, newDefs[0], getSrc(&insn->src[0]), loadImm(NULL, 0x808), getSrc(&insn->src[1]));
      break;
   }
   case nir_op_bitfield_insert: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      LValue *temp = getSSA();
      mkOp3(OP_INSBF, TYPE_U32, temp, getSrc(&insn->src[3]), mkImm(0x808), getSrc(&insn->src[2]));
      mkOp3(OP_INSBF, dType, newDefs[0], getSrc(&insn->src[1]), temp, getSrc(&insn->src[0]));
      break;
   }
   case nir_op_bit_count: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      mkOp2(OP_POPCNT, dType, newDefs[0], getSrc(&insn->src[0]), getSrc(&insn->src[0]));
      break;
   }
   case nir_op_bitfield_reverse: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      mkOp2(OP_EXTBF, TYPE_U32, newDefs[0], getSrc(&insn->src[0]), mkImm(0x2000))->subOp = NV50_IR_SUBOP_EXTBF_REV;
      break;
   }
   case nir_op_find_lsb: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      Value *tmp = getSSA();
      mkOp2(OP_EXTBF, TYPE_U32, tmp, getSrc(&insn->src[0]), mkImm(0x2000))->subOp = NV50_IR_SUBOP_EXTBF_REV;
      mkOp1(OP_BFIND, TYPE_U32, newDefs[0], tmp)->subOp = NV50_IR_SUBOP_BFIND_SAMT;
      break;
   }
   // boolean conversions
   case nir_op_b2f32: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      mkOp2(OP_AND, TYPE_U32, newDefs[0], getSrc(&insn->src[0]), loadImm(NULL, 1.0f));
      break;
   }
   case nir_op_b2f64: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      Value *tmp = getSSA(4);
      mkOp2(OP_AND, TYPE_U32, tmp, getSrc(&insn->src[0]), loadImm(NULL, 0x3ff00000));
      mkOp2(OP_MERGE, TYPE_U64, newDefs[0], loadImm(NULL, 0), tmp);
      break;
   }
   case nir_op_f2b32:
   case nir_op_i2b32: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      Value *src1;
      if (typeSizeof(sTypes[0]) == 8) {
         src1 = loadImm(getSSA(8), 0.0);
      } else {
         src1 = zero;
      }
      CondCode cc = op == nir_op_f2b32 ? CC_NEU : CC_NE;
      mkCmp(OP_SET, cc, TYPE_U32, newDefs[0], sTypes[0], getSrc(&insn->src[0]), src1);
      break;
   }
   case nir_op_b2i32: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      mkOp2(OP_AND, TYPE_U32, newDefs[0], getSrc(&insn->src[0]), loadImm(NULL, 1));
      break;
   }
   case nir_op_b2i64: {
      DEFAULT_CHECKS;
      LValues &newDefs = convert(&insn->dest);
      LValue *def = getScratch();
      mkOp2(OP_AND, TYPE_U32, def, getSrc(&insn->src[0]), loadImm(NULL, 1));
      mkOp2(OP_MERGE, TYPE_S64, newDefs[0], def, loadImm(NULL, 0));
      break;
   }
   default:
      ERROR("unknown nir_op %s\n", info.name);
      return false;
   }

   if (!oldPos) {
      oldPos = this->bb->getEntry();
      oldPos->precise = insn->exact;
   }

   if (unlikely(!oldPos))
      return true;

   while (oldPos->next) {
      oldPos = oldPos->next;
      oldPos->precise = insn->exact;
   }
   oldPos->saturate = insn->dest.saturate;

   return true;
}
#undef DEFAULT_CHECKS

bool
Converter::visit(nir_ssa_undef_instr *insn)
{
   LValues &newDefs = convert(&insn->def);
   for (uint8_t i = 0u; i < insn->def.num_components; ++i) {
      mkOp(OP_NOP, TYPE_NONE, newDefs[i]);
   }
   return true;
}

#define CASE_SAMPLER(ty) \
   case GLSL_SAMPLER_DIM_ ## ty : \
      if (isArray && !isShadow) \
         return TEX_TARGET_ ## ty ## _ARRAY; \
      else if (!isArray && isShadow) \
         return TEX_TARGET_## ty ## _SHADOW; \
      else if (isArray && isShadow) \
         return TEX_TARGET_## ty ## _ARRAY_SHADOW; \
      else \
         return TEX_TARGET_ ## ty

TexTarget
Converter::convert(glsl_sampler_dim dim, bool isArray, bool isShadow)
{
   switch (dim) {
   CASE_SAMPLER(1D);
   CASE_SAMPLER(2D);
   CASE_SAMPLER(CUBE);
   case GLSL_SAMPLER_DIM_3D:
      return TEX_TARGET_3D;
   case GLSL_SAMPLER_DIM_MS:
      if (isArray)
         return TEX_TARGET_2D_MS_ARRAY;
      return TEX_TARGET_2D_MS;
   case GLSL_SAMPLER_DIM_RECT:
      if (isShadow)
         return TEX_TARGET_RECT_SHADOW;
      return TEX_TARGET_RECT;
   case GLSL_SAMPLER_DIM_BUF:
      return TEX_TARGET_BUFFER;
   case GLSL_SAMPLER_DIM_EXTERNAL:
      return TEX_TARGET_2D;
   default:
      ERROR("unknown glsl_sampler_dim %u\n", dim);
      assert(false);
      return TEX_TARGET_COUNT;
   }
}
#undef CASE_SAMPLER

Value*
Converter::applyProjection(Value *src, Value *proj)
{
   if (!proj)
      return src;
   return mkOp2v(OP_MUL, TYPE_F32, getScratch(), src, proj);
}

unsigned int
Converter::getNIRArgCount(TexInstruction::Target& target)
{
   unsigned int result = target.getArgCount();
   if (target.isCube() && target.isArray())
      result--;
   if (target.isMS())
      result--;
   return result;
}

uint16_t
Converter::handleDeref(nir_deref_instr *deref, Value * &indirect, const nir_variable * &tex)
{
   typedef std::pair<uint32_t,Value*> DerefPair;
   std::list<DerefPair> derefs;

   uint16_t result = 0;
   while (deref->deref_type != nir_deref_type_var) {
      switch (deref->deref_type) {
      case nir_deref_type_array: {
         Value *indirect;
         uint8_t size = type_size(deref->type, true);
         result += size * getIndirect(&deref->arr.index, 0, indirect);

         if (indirect) {
            derefs.push_front(std::make_pair(size, indirect));
         }

         break;
      }
      case nir_deref_type_struct: {
         result += nir_deref_instr_parent(deref)->type->struct_location_offset(deref->strct.index);
         break;
      }
      case nir_deref_type_var:
      default:
         unreachable("nir_deref_type_var reached in handleDeref!");
         break;
      }
      deref = nir_deref_instr_parent(deref);
   }

   indirect = NULL;
   for (std::list<DerefPair>::const_iterator it = derefs.begin(); it != derefs.end(); ++it) {
      Value *offset = mkOp2v(OP_MUL, TYPE_U32, getSSA(), loadImm(getSSA(), it->first), it->second);
      if (indirect)
         indirect = mkOp2v(OP_ADD, TYPE_U32, getSSA(), indirect, offset);
      else
         indirect = offset;
   }

   tex = nir_deref_instr_get_variable(deref);
   assert(tex);

   return result + tex->data.driver_location;
}

CacheMode
Converter::convert(enum gl_access_qualifier access)
{
   switch (access) {
   case ACCESS_VOLATILE:
      return CACHE_CV;
   case ACCESS_COHERENT:
      return CACHE_CG;
   default:
      return CACHE_CA;
   }
}

CacheMode
Converter::getCacheModeFromVar(const nir_variable *var)
{
   return convert(var->data.image.access);
}

bool
Converter::visit(nir_tex_instr *insn)
{
   switch (insn->op) {
   case nir_texop_lod:
   case nir_texop_query_levels:
   case nir_texop_tex:
   case nir_texop_texture_samples:
   case nir_texop_tg4:
   case nir_texop_txb:
   case nir_texop_txd:
   case nir_texop_txf:
   case nir_texop_txf_ms:
   case nir_texop_txl:
   case nir_texop_txs: {
      LValues &newDefs = convert(&insn->dest);
      std::vector<Value*> srcs;
      std::vector<Value*> defs;
      std::vector<nir_src*> offsets;
      uint8_t mask = 0;
      bool lz = false;
      Value *proj = NULL;
      TexInstruction::Target target = convert(insn->sampler_dim, insn->is_array, insn->is_shadow);
      operation op = getOperation(insn->op);

      int r, s;
      int biasIdx = nir_tex_instr_src_index(insn, nir_tex_src_bias);
      int compIdx = nir_tex_instr_src_index(insn, nir_tex_src_comparator);
      int coordsIdx = nir_tex_instr_src_index(insn, nir_tex_src_coord);
      int ddxIdx = nir_tex_instr_src_index(insn, nir_tex_src_ddx);
      int ddyIdx = nir_tex_instr_src_index(insn, nir_tex_src_ddy);
      int msIdx = nir_tex_instr_src_index(insn, nir_tex_src_ms_index);
      int lodIdx = nir_tex_instr_src_index(insn, nir_tex_src_lod);
      int offsetIdx = nir_tex_instr_src_index(insn, nir_tex_src_offset);
      int projIdx = nir_tex_instr_src_index(insn, nir_tex_src_projector);
      int sampOffIdx = nir_tex_instr_src_index(insn, nir_tex_src_sampler_offset);
      int texOffIdx = nir_tex_instr_src_index(insn, nir_tex_src_texture_offset);
      int sampHandleIdx = nir_tex_instr_src_index(insn, nir_tex_src_sampler_handle);
      int texHandleIdx = nir_tex_instr_src_index(insn, nir_tex_src_texture_handle);

      bool bindless = sampHandleIdx != -1 || texHandleIdx != -1;
      assert((sampHandleIdx != -1) == (texHandleIdx != -1));

      if (projIdx != -1)
         proj = mkOp1v(OP_RCP, TYPE_F32, getScratch(), getSrc(&insn->src[projIdx].src, 0));

      srcs.resize(insn->coord_components);
      for (uint8_t i = 0u; i < insn->coord_components; ++i)
         srcs[i] = applyProjection(getSrc(&insn->src[coordsIdx].src, i), proj);

      // sometimes we get less args than target.getArgCount, but codegen expects the latter
      if (insn->coord_components) {
         uint32_t argCount = target.getArgCount();

         if (target.isMS())
            argCount -= 1;

         for (uint32_t i = 0u; i < (argCount - insn->coord_components); ++i)
            srcs.push_back(getSSA());
      }

      if (insn->op == nir_texop_texture_samples)
         srcs.push_back(zero);
      else if (!insn->num_srcs)
         srcs.push_back(loadImm(NULL, 0));
      if (biasIdx != -1)
         srcs.push_back(getSrc(&insn->src[biasIdx].src, 0));
      if (lodIdx != -1)
         srcs.push_back(getSrc(&insn->src[lodIdx].src, 0));
      else if (op == OP_TXF)
         lz = true;
      if (msIdx != -1)
         srcs.push_back(getSrc(&insn->src[msIdx].src, 0));
      if (offsetIdx != -1)
         offsets.push_back(&insn->src[offsetIdx].src);
      if (compIdx != -1)
         srcs.push_back(applyProjection(getSrc(&insn->src[compIdx].src, 0), proj));
      if (texOffIdx != -1) {
         srcs.push_back(getSrc(&insn->src[texOffIdx].src, 0));
         texOffIdx = srcs.size() - 1;
      }
      if (sampOffIdx != -1) {
         srcs.push_back(getSrc(&insn->src[sampOffIdx].src, 0));
         sampOffIdx = srcs.size() - 1;
      }
      if (bindless) {
         // currently we use the lower bits
         Value *split[2];
         Value *handle = getSrc(&insn->src[sampHandleIdx].src, 0);

         mkSplit(split, 4, handle);

         srcs.push_back(split[0]);
         texOffIdx = srcs.size() - 1;
      }

      r = bindless ? 0xff : insn->texture_index;
      s = bindless ? 0x1f : insn->sampler_index;

      defs.resize(newDefs.size());
      for (uint8_t d = 0u; d < newDefs.size(); ++d) {
         defs[d] = newDefs[d];
         mask |= 1 << d;
      }
      if (target.isMS() || (op == OP_TEX && prog->getType() != Program::TYPE_FRAGMENT))
         lz = true;

      TexInstruction *texi = mkTex(op, target.getEnum(), r, s, defs, srcs);
      texi->tex.levelZero = lz;
      texi->tex.mask = mask;
      texi->tex.bindless = bindless;

      if (texOffIdx != -1)
         texi->tex.rIndirectSrc = texOffIdx;
      if (sampOffIdx != -1)
         texi->tex.sIndirectSrc = sampOffIdx;

      switch (insn->op) {
      case nir_texop_tg4:
         if (!target.isShadow())
            texi->tex.gatherComp = insn->component;
         break;
      case nir_texop_txs:
         texi->tex.query = TXQ_DIMS;
         break;
      case nir_texop_texture_samples:
         texi->tex.mask = 0x4;
         texi->tex.query = TXQ_TYPE;
         break;
      case nir_texop_query_levels:
         texi->tex.mask = 0x8;
         texi->tex.query = TXQ_DIMS;
         break;
      default:
         break;
      }

      texi->tex.useOffsets = offsets.size();
      if (texi->tex.useOffsets) {
         for (uint8_t s = 0; s < texi->tex.useOffsets; ++s) {
            for (uint32_t c = 0u; c < 3; ++c) {
               uint8_t s2 = std::min(c, target.getDim() - 1);
               texi->offset[s][c].set(getSrc(offsets[s], s2));
               texi->offset[s][c].setInsn(texi);
            }
         }
      }

      if (op == OP_TXG && offsetIdx == -1) {
         if (nir_tex_instr_has_explicit_tg4_offsets(insn)) {
            texi->tex.useOffsets = 4;
            setPosition(texi, false);
            for (uint8_t i = 0; i < 4; ++i) {
               for (uint8_t j = 0; j < 2; ++j) {
                  texi->offset[i][j].set(loadImm(NULL, insn->tg4_offsets[i][j]));
                  texi->offset[i][j].setInsn(texi);
               }
            }
            setPosition(texi, true);
         }
      }

      if (ddxIdx != -1 && ddyIdx != -1) {
         for (uint8_t c = 0u; c < target.getDim() + target.isCube(); ++c) {
            texi->dPdx[c].set(getSrc(&insn->src[ddxIdx].src, c));
            texi->dPdy[c].set(getSrc(&insn->src[ddyIdx].src, c));
         }
      }

      break;
   }
   default:
      ERROR("unknown nir_texop %u\n", insn->op);
      return false;
   }
   return true;
}

bool
Converter::visit(nir_deref_instr *deref)
{
   // we just ignore those, because images intrinsics are the only place where
   // we should end up with deref sources and those have to backtrack anyway
   // to get the nir_variable. This code just exists to handle some special
   // cases.
   switch (deref->deref_type) {
   case nir_deref_type_array:
   case nir_deref_type_struct:
   case nir_deref_type_var:
      break;
   default:
      ERROR("unknown nir_deref_instr %u\n", deref->deref_type);
      return false;
   }
   return true;
}

bool
Converter::run()
{
   bool progress;

   if (prog->dbgFlags & NV50_IR_DEBUG_VERBOSE)
      nir_print_shader(nir, stderr);

   struct nir_lower_subgroups_options subgroup_options = {
      .subgroup_size = 32,
      .ballot_bit_size = 32,
   };

   NIR_PASS_V(nir, nir_lower_io, nir_var_all, type_size, (nir_lower_io_options)0);
   NIR_PASS_V(nir, nir_lower_subgroups, &subgroup_options);
   NIR_PASS_V(nir, nir_lower_regs_to_ssa);
   NIR_PASS_V(nir, nir_lower_load_const_to_scalar);
   NIR_PASS_V(nir, nir_lower_vars_to_ssa);
   NIR_PASS_V(nir, nir_lower_alu_to_scalar);
   NIR_PASS_V(nir, nir_lower_phis_to_scalar);

   do {
      progress = false;
      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_trivial_continues);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);
      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
   } while (progress);

   NIR_PASS_V(nir, nir_lower_bool_to_int32);
   NIR_PASS_V(nir, nir_lower_locals_to_regs);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp);
   NIR_PASS_V(nir, nir_convert_from_ssa, true);

   // Garbage collect dead instructions
   nir_sweep(nir);

   if (!parseNIR()) {
      ERROR("Couldn't prase NIR!\n");
      return false;
   }

   if (!assignSlots()) {
      ERROR("Couldn't assign slots!\n");
      return false;
   }

   if (prog->dbgFlags & NV50_IR_DEBUG_BASIC)
      nir_print_shader(nir, stderr);

   nir_foreach_function(function, nir) {
      if (!visit(function))
         return false;
   }

   return true;
}

} // unnamed namespace

namespace nv50_ir {

bool
Program::makeFromNIR(struct nv50_ir_prog_info *info)
{
   nir_shader *nir = (nir_shader*)info->bin.source;
   Converter converter(this, nir, info);
   bool result = converter.run();
   if (!result)
      return result;
   LoweringHelper lowering;
   lowering.run(this);
   tlsSize = info->bin.tlsSpace;
   return result;
}

} // namespace nv50_ir
