/* -*- mesa-c++  -*-
 * Copyright 2022 Collabora LTD
 * Author: Gert Wollny <gert.wollny@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef SFN_FILL_BYTECODE_H
#define SFN_FILL_BYTECODE_H

#include "../r600_asm.h"
#include "sfn_alu_defines.h"
#include "sfn_virtualvalues.h"

#include <tuple>

namespace r600 {

class AluInstr;
class ExportInstr;
class FetchInstr;
class GDSInstr;
class MemRingOutInstr;
class RatInstr;
class ScratchIOInstr;
class StreamOutInstr;
class TexInstr;
class WriteTFInstr;

bool emit_bytecode_tex(r600_bytecode& bc, const TexInstr& tex_instr);

bool emit_bytecode_export_alpha_to_coverage(r600_bytecode& bc,
                                            const ExportInstr& export_instr);

bool emit_bytecode_export(r600_bytecode& bc,
                          const ExportInstr& export_instr,
                          bool ps_alpha_to_one);

bool emit_bytecode_scratch(r600_bytecode& bc, const ScratchIOInstr& instr);

bool emit_bytecode_stream_out(r600_bytecode& bc, const StreamOutInstr& instr);

bool emit_bytecode_mem_ring(r600_bytecode& bc, const MemRingOutInstr& instr);

bool emit_bytecode_fetch(r600_bytecode& bc, const FetchInstr& fetch_instr, bool use_tc);

bool emit_bytecode_gds(r600_bytecode& bc, const GDSInstr& instr);

bool emit_bytecode_tf_write(r600_bytecode& bc, const WriteTFInstr& instr);

bool emit_bytecode_lds(r600_bytecode& bc, const AluInstr& lds);

void fill_bytecode_rat(r600_bytecode_cf& cf, const RatInstr& instr,
                       unsigned rat_base, unsigned shader_type);

PVirtualValue fill_alu_src(r600_bytecode_alu_src& src,
                           const VirtualValue& s,
                           r600_bytecode& bc);

void fill_alu_src_operands(r600_bytecode_alu& alu,
                           const AluInstr& ai,
                           r600_bytecode& bc);

void fill_alu_dst(r600_bytecode_alu& alu,
                  const AluInstr& ai,
                  r600_bytecode& bc);

auto emit_bytecode_alu(r600_bytecode& bc,
                       const AluInstr& ai,
                       EAluOp opcode) -> std::tuple<bool, int, int>;

} // namespace r600

#endif