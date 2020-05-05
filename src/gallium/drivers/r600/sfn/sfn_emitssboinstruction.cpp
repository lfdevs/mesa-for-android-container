#include "sfn_emitssboinstruction.h"

#include "sfn_instruction_fetch.h"
#include "sfn_instruction_gds.h"
#include "sfn_instruction_misc.h"
#include "../r600_pipe.h"

namespace r600 {

bool EmitSSBOInstruction::do_emit(nir_instr* instr)
{
   const nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_atomic_counter_add:
   case nir_intrinsic_atomic_counter_and:
   case nir_intrinsic_atomic_counter_exchange:
   case nir_intrinsic_atomic_counter_max:
   case nir_intrinsic_atomic_counter_min:
   case nir_intrinsic_atomic_counter_or:
   case nir_intrinsic_atomic_counter_xor:
   case nir_intrinsic_atomic_counter_comp_swap:
      return emit_atomic(intr);
   case nir_intrinsic_atomic_counter_read:
   case nir_intrinsic_atomic_counter_post_dec:
      return emit_unary_atomic(intr);
   case nir_intrinsic_atomic_counter_inc:
      return emit_atomic_inc(intr);
   case nir_intrinsic_atomic_counter_pre_dec:
      return emit_atomic_pre_dec(intr);
   case nir_intrinsic_load_ssbo:
       return emit_load_ssbo(intr);
    case nir_intrinsic_store_ssbo:
      return emit_store_ssbo(intr);
   default:
      return false;
   }
}

bool EmitSSBOInstruction::emit_atomic(const nir_intrinsic_instr* instr)
{
   ESDOp op = get_opcode(instr->intrinsic);

   if (DS_OP_INVALID == op)
      return false;

   GPRVector dest = make_dest(instr);

   int base = nir_intrinsic_base(instr);

   PValue uav_id = from_nir(instr->src[0], 0);

   PValue value = from_nir_with_fetch_constant(instr->src[1], 0);

   GDSInstr *ir = nullptr;
   if (instr->intrinsic == nir_intrinsic_atomic_counter_comp_swap)  {
      PValue value2 = from_nir_with_fetch_constant(instr->src[1], 1);
      ir = new GDSInstr(op, dest, value, value2, uav_id, base);
   } else {
      ir = new GDSInstr(op, dest, value, uav_id, base);
   }

   emit_instruction(ir);
   return true;
}

bool EmitSSBOInstruction::emit_unary_atomic(const nir_intrinsic_instr* instr)
{
   ESDOp op = get_opcode(instr->intrinsic);

   if (DS_OP_INVALID == op)
      return false;

   GPRVector dest = make_dest(instr);

   PValue uav_id = from_nir(instr->src[0], 0);

   auto ir = new GDSInstr(op, dest, uav_id, nir_intrinsic_base(instr));

   emit_instruction(ir);
   return true;
}

ESDOp EmitSSBOInstruction::get_opcode(const nir_intrinsic_op opcode)
{
   switch (opcode) {
   case nir_intrinsic_atomic_counter_add:
      return DS_OP_ADD_RET;
   case nir_intrinsic_atomic_counter_and:
      return DS_OP_AND_RET;
   case nir_intrinsic_atomic_counter_exchange:
      return DS_OP_XCHG_RET;
   case nir_intrinsic_atomic_counter_inc:
      return DS_OP_INC_RET;
   case nir_intrinsic_atomic_counter_max:
      return DS_OP_MAX_UINT_RET;
   case nir_intrinsic_atomic_counter_min:
      return DS_OP_MIN_UINT_RET;
   case nir_intrinsic_atomic_counter_or:
      return DS_OP_OR_RET;
   case nir_intrinsic_atomic_counter_read:
      return DS_OP_READ_RET;
   case nir_intrinsic_atomic_counter_xor:
      return DS_OP_XOR_RET;
   case nir_intrinsic_atomic_counter_post_dec:
      return DS_OP_DEC_RET;
   case nir_intrinsic_atomic_counter_comp_swap:
      return DS_OP_CMP_XCHG_RET;
   case nir_intrinsic_atomic_counter_pre_dec:
   default:
      return DS_OP_INVALID;
   }
}


bool EmitSSBOInstruction::emit_atomic_add(const nir_intrinsic_instr* instr)
{
   GPRVector dest = make_dest(instr);

   PValue value = from_nir_with_fetch_constant(instr->src[1], 0);

   PValue uav_id = from_nir(instr->src[0], 0);

   auto ir = new GDSInstr(DS_OP_ADD_RET, dest, value, uav_id,
                          nir_intrinsic_base(instr));

   emit_instruction(ir);
   return true;
}

bool EmitSSBOInstruction::emit_atomic_inc(const nir_intrinsic_instr* instr)
{
   GPRVector dest = make_dest(instr);

   PValue uav_id = from_nir(instr->src[0], 0);


   if (!m_atomic_limit) {
      int one_tmp = allocate_temp_register();
      m_atomic_limit = PValue(new GPRValue(one_tmp, 0));
      emit_instruction(new AluInstruction(op1_mov, m_atomic_limit,
                       PValue(new LiteralValue(0xffffffff)),
                       {alu_write, alu_last_instr}));
   }

   auto ir = new GDSInstr(DS_OP_INC_RET, dest, m_atomic_limit, uav_id,
                          nir_intrinsic_base(instr));
   emit_instruction(ir);
   return true;
}

bool EmitSSBOInstruction::emit_atomic_pre_dec(const nir_intrinsic_instr *instr)
{
   GPRVector dest = make_dest(instr);

   PValue uav_id = from_nir(instr->src[0], 0);

   int one_tmp = allocate_temp_register();
   PValue value(new GPRValue(one_tmp, 0));
   emit_instruction(new AluInstruction(op1_mov, value,  Value::one_i,
                    {alu_write, alu_last_instr}));

   auto ir = new GDSInstr(DS_OP_SUB_RET, dest, value, uav_id,
                          nir_intrinsic_base(instr));
   emit_instruction(ir);

   ir = new GDSInstr(DS_OP_READ_RET, dest, uav_id, nir_intrinsic_base(instr));
   emit_instruction(ir);

   return true;
}

bool EmitSSBOInstruction::emit_load_ssbo(const nir_intrinsic_instr* instr)
{
   GPRVector dest = make_dest(instr);

   /** src0 not used, should be some offset */
   auto addr = from_nir_with_fetch_constant(instr->src[1], 0);
   PValue addr_temp = create_register_from_nir_src(instr->src[1], 1);

   /** Should be lowered in nir */
   emit_instruction(new AluInstruction(op2_lshr_int, addr_temp, {addr, PValue(new LiteralValue(2))},
                    {alu_write, alu_last_instr}));

   const EVTXDataFormat formats[4] = {
      fmt_32,
      fmt_32_32,
      fmt_32_32_32,
      fmt_32_32_32_32
   };

   const std::array<int,4> dest_swt[4] = {
      {0,7,7,7},
      {0,1,7,7},
      {0,1,2,7},
      {0,1,2,3}
   };

   /* TODO fix resource index */
   auto ir = new FetchInstruction(dest, addr_temp,
                                  R600_IMAGE_REAL_RESOURCE_OFFSET, from_nir(instr->src[0], 0),
                                  formats[instr->num_components-1], vtx_nf_int);
   ir->set_dest_swizzle(dest_swt[instr->num_components - 1]);
   ir->set_flag(vtx_use_tc);

   emit_instruction(ir);
   return true;
}

bool EmitSSBOInstruction::emit_store_ssbo(const nir_intrinsic_instr* instr)
{

   GPRVector::Swizzle swz = {7,7,7,7};
   for (int i = 0; i <  instr->src[0].ssa->num_components; ++i)
      swz[i] = i;

   auto orig_addr = from_nir(instr->src[2], 0);

   int temp1 = allocate_temp_register();
   GPRVector addr_vec(temp1, {0,1,2,7});

   auto rat_id = from_nir(instr->src[1], 0);

   emit_instruction(new AluInstruction(op2_lshr_int, addr_vec.reg_i(0), orig_addr,
                                       PValue(new LiteralValue(2)), write));
   emit_instruction(new AluInstruction(op1_mov, addr_vec.reg_i(1), Value::zero, write));
   emit_instruction(new AluInstruction(op1_mov, addr_vec.reg_i(2), Value::zero, last_write));


//#define WRITE_AS_VECTOR
#ifdef WRITE_AS_VECTOR
   std::unique_ptr<GPRVector> value(vec_from_nir_with_fetch_constant(instr->src[0],
                                    (1 << instr->src[0].ssa->num_components) - 1, swz));

   /* TODO fix resource index */
   int nelements = instr->src[0].ssa->num_components - 1;
   if (nelements == 2)
      nelements = 3;
   auto ir = new RatInstruction(cf_mem_rat, RatInstruction::STORE_TYPED,
                                *value, addr_vec, 0, rat_id, 11,
                                (1 << instr->src[0].ssa->num_components) - 1,
                                0, false);
   emit_instruction(ir);
#else

   PValue value(from_nir_with_fetch_constant(instr->src[0], 0));
   GPRVector out_vec({value, value, value, value});
   emit_instruction(new RatInstruction(cf_mem_rat, RatInstruction::STORE_TYPED,
                                       out_vec, addr_vec, 0, rat_id, 1,
                                       1, 0, false));
   for (int i = 1; i < instr->src[0].ssa->num_components; ++i) {
      emit_instruction(new AluInstruction(op1_mov, out_vec.reg_i(0), from_nir(instr->src[0], i), write));
      emit_instruction(new AluInstruction(op2_add_int, addr_vec.reg_i(0),
                                          {addr_vec.reg_i(0), Value::one_i}, last_write));
      emit_instruction(new RatInstruction(cf_mem_rat, RatInstruction::STORE_TYPED,
                                          out_vec, addr_vec, 0, rat_id, 1,
                                          1, 0, false));
   }
#endif
   return true;
}

GPRVector EmitSSBOInstruction::make_dest(const nir_intrinsic_instr* ir)
{
   GPRVector::Values v;
   int i;
   for (i = 0; i < 4; ++i)
      v[i] = from_nir(ir->dest, i);
   return GPRVector(v);
}

}
