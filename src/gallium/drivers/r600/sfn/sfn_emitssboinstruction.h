#ifndef SFN_EMITSSBOINSTRUCTION_H
#define SFN_EMITSSBOINSTRUCTION_H

#include "sfn_emitinstruction.h"

namespace r600 {

class EmitSSBOInstruction: public EmitInstruction {
public:
   using EmitInstruction::EmitInstruction;
private:
   bool do_emit(nir_instr *instr);

   bool emit_atomic(const nir_intrinsic_instr* instr);
   bool emit_unary_atomic(const nir_intrinsic_instr* instr);
   bool emit_atomic_add(const nir_intrinsic_instr* instr);
   bool emit_atomic_inc(const nir_intrinsic_instr* instr);
   bool emit_atomic_pre_dec(const nir_intrinsic_instr* instr);

   bool emit_load_ssbo(const nir_intrinsic_instr* instr);
   bool emit_store_ssbo(const nir_intrinsic_instr* instr);
   ESDOp get_opcode(nir_intrinsic_op opcode);

   GPRVector make_dest(const nir_intrinsic_instr* instr);

   PValue m_atomic_limit;
};

}

#endif // SFN_EMITSSBOINSTRUCTION_H
