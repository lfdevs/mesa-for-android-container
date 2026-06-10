/* -*- mesa-c++  -*-
 * Copyright 2022 Collabora LTD
 * Author: Gert Wollny <gert.wollny@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "sfn_fill_bytecode.h"
#include "../eg_sq.h"

#include <cassert>
#include <map>
#include <optional>

#include "sfn_alu_defines.h"
#include "sfn_instr_alu.h"
#include "sfn_instr_export.h"
#include "sfn_instr_fetch.h"
#include "sfn_instr_mem.h"
#include "sfn_instr_tex.h"
#include "sfn_debug.h"

namespace r600 {

const std::map<ESDOp, int> ds_opcode_map = {
   {DS_OP_ADD,                      FETCH_OP_GDS_ADD                 },
   {DS_OP_SUB,                      FETCH_OP_GDS_SUB                 },
   {DS_OP_RSUB,                     FETCH_OP_GDS_RSUB                },
   {DS_OP_INC,                      FETCH_OP_GDS_INC                 },
   {DS_OP_DEC,                      FETCH_OP_GDS_DEC                 },
   {DS_OP_MIN_INT,                  FETCH_OP_GDS_MIN_INT             },
   {DS_OP_MAX_INT,                  FETCH_OP_GDS_MAX_INT             },
   {DS_OP_MIN_UINT,                 FETCH_OP_GDS_MIN_UINT            },
   {DS_OP_MAX_UINT,                 FETCH_OP_GDS_MAX_UINT            },
   {DS_OP_AND,                      FETCH_OP_GDS_AND                 },
   {DS_OP_OR,                       FETCH_OP_GDS_OR                  },
   {DS_OP_XOR,                      FETCH_OP_GDS_XOR                 },
   {DS_OP_MSKOR,                    FETCH_OP_GDS_MSKOR               },
   {DS_OP_WRITE,                    FETCH_OP_GDS_WRITE               },
   {DS_OP_WRITE_REL,                FETCH_OP_GDS_WRITE_REL           },
   {DS_OP_WRITE2,                   FETCH_OP_GDS_WRITE2              },
   {DS_OP_CMP_STORE,                FETCH_OP_GDS_CMP_STORE           },
   {DS_OP_CMP_STORE_SPF,            FETCH_OP_GDS_CMP_STORE_SPF       },
   {DS_OP_BYTE_WRITE,               FETCH_OP_GDS_BYTE_WRITE          },
   {DS_OP_SHORT_WRITE,              FETCH_OP_GDS_SHORT_WRITE         },
   {DS_OP_ADD_RET,                  FETCH_OP_GDS_ADD_RET             },
   {DS_OP_SUB_RET,                  FETCH_OP_GDS_SUB_RET             },
   {DS_OP_RSUB_RET,                 FETCH_OP_GDS_RSUB_RET            },
   {DS_OP_INC_RET,                  FETCH_OP_GDS_INC_RET             },
   {DS_OP_DEC_RET,                  FETCH_OP_GDS_DEC_RET             },
   {DS_OP_MIN_INT_RET,              FETCH_OP_GDS_MIN_INT_RET         },
   {DS_OP_MAX_INT_RET,              FETCH_OP_GDS_MAX_INT_RET         },
   {DS_OP_MIN_UINT_RET,             FETCH_OP_GDS_MIN_UINT_RET        },
   {DS_OP_MAX_UINT_RET,             FETCH_OP_GDS_MAX_UINT_RET        },
   {DS_OP_AND_RET,                  FETCH_OP_GDS_AND_RET             },
   {DS_OP_OR_RET,                   FETCH_OP_GDS_OR_RET              },
   {DS_OP_XOR_RET,                  FETCH_OP_GDS_XOR_RET             },
   {DS_OP_MSKOR_RET,                FETCH_OP_GDS_MSKOR_RET           },
   {DS_OP_XCHG_RET,                 FETCH_OP_GDS_XCHG_RET            },
   {DS_OP_XCHG_REL_RET,             FETCH_OP_GDS_XCHG_REL_RET        },
   {DS_OP_XCHG2_RET,                FETCH_OP_GDS_XCHG2_RET           },
   {DS_OP_CMP_XCHG_RET,             FETCH_OP_GDS_CMP_XCHG_RET        },
   {DS_OP_CMP_XCHG_SPF_RET,         FETCH_OP_GDS_CMP_XCHG_SPF_RET    },
   {DS_OP_READ_RET,                 FETCH_OP_GDS_READ_RET            },
   {DS_OP_READ_REL_RET,             FETCH_OP_GDS_READ_REL_RET        },
   {DS_OP_READ2_RET,                FETCH_OP_GDS_READ2_RET           },
   {DS_OP_READWRITE_RET,            FETCH_OP_GDS_READWRITE_RET       },
   {DS_OP_BYTE_READ_RET,            FETCH_OP_GDS_BYTE_READ_RET       },
   {DS_OP_UBYTE_READ_RET,           FETCH_OP_GDS_UBYTE_READ_RET      },
   {DS_OP_SHORT_READ_RET,           FETCH_OP_GDS_SHORT_READ_RET      },
   {DS_OP_USHORT_READ_RET,          FETCH_OP_GDS_USHORT_READ_RET     },
   {DS_OP_ATOMIC_ORDERED_ALLOC_RET, FETCH_OP_GDS_ATOMIC_ORDERED_ALLOC},
   {DS_OP_INVALID,                  0                                },
};

static r600_bytecode_tex
fill_bytecode_tex(const TexInstr& tex_instr)
{
   r600_bytecode_tex tex = {};
   tex.op = tex_instr.opcode();
   tex.sampler_id = tex_instr.sampler_id();
   tex.resource_id = tex_instr.resource_id();
   tex.src_gpr = tex_instr.src().sel();
   tex.dst_gpr = tex_instr.dst().sel();
   tex.dst_sel_x = tex_instr.dest_swizzle(0);
   tex.dst_sel_y = tex_instr.dest_swizzle(1);
   tex.dst_sel_z = tex_instr.dest_swizzle(2);
   tex.dst_sel_w = tex_instr.dest_swizzle(3);
   tex.src_sel_x = tex_instr.src()[0]->chan();
   tex.src_sel_y = tex_instr.src()[1]->chan();
   tex.src_sel_z = tex_instr.src()[2]->chan();
   tex.src_sel_w = tex_instr.src()[3]->chan();
   tex.coord_type_x = !tex_instr.has_tex_flag(TexInstr::x_unnormalized);
   tex.coord_type_y = !tex_instr.has_tex_flag(TexInstr::y_unnormalized);
   tex.coord_type_z = !tex_instr.has_tex_flag(TexInstr::z_unnormalized);
   tex.coord_type_w = !tex_instr.has_tex_flag(TexInstr::w_unnormalized);
   tex.offset_x = tex_instr.get_offset(0);
   tex.offset_y = tex_instr.get_offset(1);
   tex.offset_z = tex_instr.get_offset(2);
   tex.resource_index_mode = tex_instr.resource_index_mode();
   tex.sampler_index_mode = tex_instr.sampler_index_mode();

   if (tex_instr.opcode() == TexInstr::get_gradient_h ||
       tex_instr.opcode() == TexInstr::get_gradient_v)
      tex.inst_mod = tex_instr.has_tex_flag(TexInstr::grad_fine) ? 1 : 0;
   else
      tex.inst_mod = tex_instr.inst_mode();

   return tex;
}

static r600_bytecode_output
fill_bytecode_export_alpha_to_coverage(const ExportInstr& export_instr)
{
   const auto& value = export_instr.value();

   r600_bytecode_output output = {};
   output.gpr = value.sel();
   output.elem_size = 3;
   output.swizzle_x = 7;
   output.swizzle_y = 7;
   output.swizzle_z = 7;
   output.swizzle_w = value[3]->chan();
   output.array_base = 61;
   output.burst_count = 1;
   output.op = CF_OP_EXPORT;
   output.type = export_instr.export_type();

   return output;
}

static std::optional<r600_bytecode_output>
fill_bytecode_export(const ExportInstr& export_instr,
                     bool ps_alpha_to_one)
{
   const auto& value = export_instr.value();

   r600_bytecode_output output = {};
   output.gpr = value.sel();
   output.elem_size = 3;
   output.swizzle_x = value[0]->chan();
   output.swizzle_y = value[1]->chan();
   output.swizzle_z = value[2]->chan();
   output.burst_count = 1;
   output.op = export_instr.is_last_export() ? CF_OP_EXPORT_DONE : CF_OP_EXPORT;
   output.type = export_instr.export_type();

   switch (export_instr.export_type()) {
   case ExportInstr::pixel:
      output.swizzle_w = ps_alpha_to_one ? 5 : value[3]->chan();
      output.array_base = export_instr.location();
      break;
   case ExportInstr::pos:
      output.swizzle_w = value[3]->chan();
      output.array_base = 60 + export_instr.location();
      break;
   case ExportInstr::param:
      output.swizzle_w = value[3]->chan();
      output.array_base = export_instr.location();
      break;
   default:
      return std::nullopt;
   }

   if (output.swizzle_x > 3 && output.swizzle_y > 3 && output.swizzle_z > 3 &&
       output.swizzle_w > 3)
      output.gpr = 0;

   return output;
}

static r600_bytecode_output
fill_bytecode_scratch(const ScratchIOInstr& instr,
                      enum amd_gfx_level gfx_level)
{
   r600_bytecode_output output = {};
   output.op = CF_OP_MEM_SCRATCH;
   output.elem_size = 3;
   output.gpr = instr.value().sel();
   output.mark = !instr.is_read();
   output.comp_mask = instr.is_read() ? 0xf : instr.write_mask();
   output.swizzle_x = 0;
   output.swizzle_y = 1;
   output.swizzle_z = 2;
   output.swizzle_w = 3;
   output.burst_count = 1;

   assert(!instr.is_read() || gfx_level < R700);

   if (instr.address()) {
      output.type = instr.is_read() || gfx_level > R600 ? 3 : 1;
      output.index_gpr = instr.address()->sel();
      output.array_size = instr.array_size();
   } else {
      output.type = instr.is_read() || gfx_level > R600 ? 2 : 0;
      output.array_base = instr.location();
   }

   return output;
}

static r600_bytecode_output
fill_bytecode_stream_out(const StreamOutInstr& instr,
                         enum amd_gfx_level gfx_level)
{
   r600_bytecode_output output = {};
   output.gpr = instr.value().sel();
   output.elem_size = instr.element_size();
   output.array_base = instr.array_base();
   output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_WRITE;
   output.burst_count = instr.burst_count();
   output.array_size = instr.array_size();
   output.comp_mask = instr.comp_mask();
   output.op = instr.op(gfx_level);

   return output;
}

static r600_bytecode_output
fill_bytecode_mem_ring(const MemRingOutInstr& instr)
{
   r600_bytecode_output output = {};
   output.gpr = instr.value().sel();
   output.type = instr.type();
   output.elem_size = 3;
   output.comp_mask = 0xf;
   output.burst_count = 1;
   output.op = instr.op();
   if (instr.type() == MemRingOutInstr::mem_write_ind ||
       instr.type() == MemRingOutInstr::mem_write_ind_ack) {
      output.index_gpr = instr.index_reg();
      output.array_size = 0xfff;
   }
   output.array_base = instr.array_base();

   return output;
}

static r600_bytecode_vtx
fill_bytecode_fetch(const FetchInstr& fetch_instr)
{
   r600_bytecode_vtx vtx = {};
   vtx.op = fetch_instr.opcode();
   vtx.buffer_id = fetch_instr.resource_id();
   vtx.fetch_type = fetch_instr.fetch_type();
   vtx.src_gpr = fetch_instr.src().sel();
   vtx.src_sel_x = fetch_instr.src().chan();
   vtx.mega_fetch_count = fetch_instr.mega_fetch_count();
   vtx.dst_gpr = fetch_instr.dst().sel();
   vtx.dst_sel_x = fetch_instr.dest_swizzle(0);
   vtx.dst_sel_y = fetch_instr.dest_swizzle(1);
   vtx.dst_sel_z = fetch_instr.dest_swizzle(2);
   vtx.dst_sel_w = fetch_instr.dest_swizzle(3);
   vtx.use_const_fields = fetch_instr.has_fetch_flag(FetchInstr::use_const_field);
   vtx.data_format = fetch_instr.data_format();
   vtx.num_format_all = fetch_instr.num_format();
   vtx.format_comp_all = fetch_instr.has_fetch_flag(FetchInstr::format_comp_signed);
   vtx.endian = fetch_instr.endian_swap();
   vtx.buffer_index_mode = fetch_instr.resource_index_mode();
   vtx.offset = fetch_instr.src_offset();
   vtx.indexed = fetch_instr.has_fetch_flag(FetchInstr::indexed);
   vtx.uncached = fetch_instr.has_fetch_flag(FetchInstr::uncached);
   vtx.elem_size = fetch_instr.elm_size();
   vtx.array_base = fetch_instr.array_base();
   vtx.array_size = fetch_instr.array_size();
   vtx.srf_mode_all = fetch_instr.has_fetch_flag(FetchInstr::srf_mode);

   return vtx;
}

static r600_bytecode_gds
fill_bytecode_gds(const GDSInstr& instr,
                  enum amd_gfx_level gfx_level)
{
   r600_bytecode_gds gds = {};
   gds.op = ds_opcode_map.at(instr.opcode());
   gds.uav_id = instr.resource_id();
   gds.uav_index_mode = instr.resource_index_mode();
   gds.src_gpr = instr.src().sel();

   gds.src_sel_x = instr.src()[0]->chan() < 7 ? instr.src()[0]->chan() : 4;
   gds.src_sel_y = instr.src()[1]->chan() < 7 ? instr.src()[1]->chan() : 4;
   gds.src_sel_z = instr.src()[2]->chan() < 7 ? instr.src()[2]->chan() : 4;

   gds.dst_sel_x = 7;
   gds.dst_sel_y = 7;
   gds.dst_sel_z = 7;
   gds.dst_sel_w = 7;

   if (instr.dest()) {
      gds.dst_gpr = instr.dest()->sel();
      switch (instr.dest()->chan()) {
      case 0:
         gds.dst_sel_x = 0;
         break;
      case 1:
         gds.dst_sel_y = 0;
         break;
      case 2:
         gds.dst_sel_z = 0;
         break;
      case 3:
         gds.dst_sel_w = 0;
      }
   }

   gds.src_gpr2 = 0;
   gds.alloc_consume = gfx_level < CAYMAN ? 1 : 0;

   return gds;
}

static r600_bytecode_gds
fill_bytecode_tf_write(const WriteTFInstr& instr,
                       unsigned start_chan)
{
   assert(start_chan == 0 || start_chan == 2);

   const auto& value = instr.value();

   r600_bytecode_gds gds = {};
   gds.src_gpr = value.sel();
   gds.src_sel_x = value[start_chan]->chan();
   gds.src_sel_y = value[start_chan + 1]->chan();
   gds.src_sel_z = 4;
   gds.dst_sel_x = 7;
   gds.dst_sel_y = 7;
   gds.dst_sel_z = 7;
   gds.dst_sel_w = 7;
   gds.op = FETCH_OP_TF_WRITE;

   return gds;
}

static auto
get_lds_opcode_properties(const AluInstr& lds)
   -> std::tuple<unsigned int, unsigned int, bool>
{
   unsigned int opcode = lds.lds_opcode();
   unsigned int lds_idx = 0;
   bool has_lds_fetch = false;

   switch (opcode) {
   case LDS_WRITE:
      opcode = LDS_OP2_LDS_WRITE;
      break;
   case LDS_WRITE_REL:
      opcode = LDS_OP3_LDS_WRITE_REL;
      lds_idx = 1;
      break;
   case DS_OP_READ_RET:
      opcode = LDS_OP1_LDS_READ_RET;
      FALLTHROUGH;
   case LDS_ADD_RET:
   case LDS_AND_RET:
   case LDS_OR_RET:
   case LDS_MAX_INT_RET:
   case LDS_MAX_UINT_RET:
   case LDS_MIN_INT_RET:
   case LDS_MIN_UINT_RET:
   case LDS_XOR_RET:
   case LDS_XCHG_RET:
   case LDS_CMP_XCHG_RET:
      has_lds_fetch = true;
      break;
   case LDS_ADD:
   case LDS_AND:
   case LDS_OR:
   case LDS_MAX_INT:
   case LDS_MAX_UINT:
   case LDS_MIN_INT:
   case LDS_MIN_UINT:
   case LDS_XOR:
      break;
   default:
      std::cerr << "\n R600: error op: " << lds << "\n";
      UNREACHABLE("Unhandled LDS op");
   }

   return std::make_tuple(opcode, lds_idx, has_lds_fetch);
}

bool
emit_bytecode_tex(r600_bytecode& bc, const TexInstr& tex_instr)
{
   r600_bytecode_tex tex = fill_bytecode_tex(tex_instr);
   if (r600_bytecode_add_tex(&bc, &tex)) {
      R600_ASM_ERR("shader_from_nir: Error creating tex assembly instruction\n");
      return false;
   }

   return true;
}

bool
emit_bytecode_export_alpha_to_coverage(r600_bytecode& bc, const ExportInstr& export_instr)
{
   r600_bytecode_output output = fill_bytecode_export_alpha_to_coverage(export_instr);

   if (r600_bytecode_add_output(&bc, &output)) {
      R600_ASM_ERR("Error adding export at location %d\n", output.array_base);
      return false;
   }

   return true;
}

bool
emit_bytecode_export(r600_bytecode& bc,
                     const ExportInstr& export_instr,
                     bool ps_alpha_to_one)
{
   auto output_opt = fill_bytecode_export(export_instr, ps_alpha_to_one);
   if (!output_opt) {
      R600_ASM_ERR("shader_from_nir: export %d type not yet supported\n",
                   export_instr.export_type());
      return false;
   }
   
   if (r600_bytecode_add_output(&bc, &output_opt.value())) {
      R600_ASM_ERR("Error adding export at location %d\n", export_instr.location());
      return false;
   }

   return true;
}

bool
emit_bytecode_scratch(r600_bytecode& bc, const ScratchIOInstr& instr)
{
   r600_bytecode_output output = fill_bytecode_scratch(instr, bc.gfx_level);

   if (r600_bytecode_add_output(&bc, &output)) {
      R600_ASM_ERR("shader_from_nir: Error creating SCRATCH_WR assembly instruction\n");
      return false;
   }

   return true;
}

bool
emit_bytecode_stream_out(r600_bytecode& bc,
                         const StreamOutInstr& instr)
{
   r600_bytecode_output output = fill_bytecode_stream_out(instr, bc.gfx_level);

   if (r600_bytecode_add_output(&bc, &output)) {
      R600_ASM_ERR("shader_from_nir: Error creating stream output instruction\n");
      return false;
   }

   return true;
}

bool
emit_bytecode_mem_ring(r600_bytecode& bc, const MemRingOutInstr& instr)
{
   r600_bytecode_output output = fill_bytecode_mem_ring(instr);

   if (r600_bytecode_add_output(&bc, &output)) {
      R600_ASM_ERR("shader_from_nir: Error creating mem ring write instruction\n");
      return false;
   }

   return true;
}

bool
emit_bytecode_fetch(r600_bytecode& bc, const FetchInstr& fetch_instr, bool use_tc)
{
   r600_bytecode_vtx vtx = fill_bytecode_fetch(fetch_instr);

   int r = use_tc ? r600_bytecode_add_vtx_tc(&bc, &vtx) : r600_bytecode_add_vtx(&bc, &vtx);
   if (r) {
      R600_ASM_ERR("shader_from_nir: Error creating tex assembly instruction\n");
      return false;
   }

   return true;
}

bool
emit_bytecode_gds(r600_bytecode& bc, const GDSInstr& instr)
{
   r600_bytecode_gds gds = fill_bytecode_gds(instr, bc.gfx_level);

   if (r600_bytecode_add_gds(&bc, &gds)) {
      R600_ASM_ERR("shader_from_nir: Error creating GDS instruction\n");
      return false;
   }

   return true;
}

bool
emit_bytecode_tf_write(r600_bytecode& bc, const WriteTFInstr& instr)
{
   r600_bytecode_gds gds = fill_bytecode_tf_write(instr, 0);
   if (r600_bytecode_add_gds(&bc, &gds) != 0) {
      R600_ASM_ERR("shader_from_nir: Error creating transform feedback write instruction\n");
      return false;
   }

   if (instr.value()[2]->chan() != 7) {
      gds = fill_bytecode_tf_write(instr, 2);
      if (r600_bytecode_add_gds(&bc, &gds) != 0) {
         R600_ASM_ERR("shader_from_nir: Error creating transform feedback write instruction\n");
         return false;
      }
   }
   return true;
}

bool
emit_bytecode_lds(r600_bytecode& bc, const AluInstr& lds)
{
   struct r600_bytecode_alu alu;
   memset(&alu, 0, sizeof(alu));

   alu.is_lds_idx_op = true;
   auto [opcode, lds_idx, has_lds_fetch] = get_lds_opcode_properties(lds);
   alu.op = opcode;
   alu.lds_idx = lds_idx;

   fill_alu_src(alu.src[0], lds.src(0), bc);

   if (lds.n_sources() > 1)
      fill_alu_src(alu.src[1], lds.src(1), bc);
   else
      alu.src[1].sel = V_SQ_ALU_SRC_0;

   if (lds.n_sources() > 2)
      fill_alu_src(alu.src[2], lds.src(2), bc);
   else
      alu.src[2].sel = V_SQ_ALU_SRC_0;

   alu.last = lds.has_alu_flag(alu_last_instr);
   int r = r600_bytecode_add_alu(&bc, &alu);
   if (has_lds_fetch)
      bc.cf_last->nlds_read++;

   return r == 0;
}

void
fill_bytecode_rat(r600_bytecode_cf& cf, const RatInstr& instr,
                  unsigned rat_base, unsigned shader_type)
{
   int rat_idx = instr.resource_id();

   cf.rat.id = rat_idx + rat_base;
   cf.rat.inst = instr.rat_op();
   cf.rat.index_mode = instr.resource_index_mode();
   cf.output.type = instr.need_ack() ? 3 : 1;
   cf.output.gpr = instr.data_gpr();
   cf.output.index_gpr = instr.index_gpr();
   cf.output.comp_mask = instr.comp_mask();
   cf.output.burst_count = instr.burst_count();
   assert(instr.data_swz(0) == PIPE_SWIZZLE_X);
   if (cf.rat.inst != RatInstr::STORE_TYPED) {
      assert(instr.data_swz(1) == PIPE_SWIZZLE_Y ||
             instr.data_swz(1) == PIPE_SWIZZLE_MAX);
      assert(instr.data_swz(2) == PIPE_SWIZZLE_Z ||
             instr.data_swz(2) == PIPE_SWIZZLE_MAX);
   }

   cf.vpm = shader_type == MESA_SHADER_FRAGMENT;
   cf.barrier = 1;
   cf.mark = instr.need_ack();
   cf.output.elem_size = instr.elm_size();
}

class EncodeSourceVisitor : public ConstRegisterVisitor {
public:
   EncodeSourceVisitor(r600_bytecode_alu_src& s, r600_bytecode& bc);
   void visit(const Register& value) override;
   void visit(const LocalArray& value) override;
   void visit(const LocalArrayValue& value) override;
   void visit(const UniformValue& value) override;
   void visit(const LiteralConstant& value) override;
   void visit(const InlineConstant& value) override;

   r600_bytecode_alu_src& src;
   r600_bytecode& m_bc;
   PVirtualValue m_buffer_offset{nullptr};
};

PVirtualValue
fill_alu_src(r600_bytecode_alu_src& src, const VirtualValue& s, r600_bytecode& bc)
{
   EncodeSourceVisitor visitor(src, bc);
   src.sel = s.sel();
   src.chan = s.chan();

   if (s.sel() >= g_clause_local_start && s.sel() < g_clause_local_end) {
      assert(bc.cf_last);
      int clidx = 4 * (s.sel() - g_clause_local_start) + s.chan();
      /* Ensure that the clause local register was already written */
      assert(bc.cf_last->clause_local_written & (1 << clidx));
   }

   s.accept(visitor);
   return visitor.m_buffer_offset;
}

EncodeSourceVisitor::EncodeSourceVisitor(r600_bytecode_alu_src& s, r600_bytecode& bc):
    src(s),
    m_bc(bc)
{
}

void
EncodeSourceVisitor::visit(const Register& value)
{
   assert(value.sel() < g_clause_local_end && "Only have 123 reisters + 4 clause local");
}

void
EncodeSourceVisitor::visit(const LocalArray& value)
{
   (void)value;
   UNREACHABLE("An array can't be a source register");
}

void
EncodeSourceVisitor::visit(const LocalArrayValue& value)
{
   src.rel = value.addr() ? 1 : 0;
}

void
EncodeSourceVisitor::visit(const UniformValue& value)
{
   assert(value.sel() >= 512 && "Uniform values must have a sel >= 512");
   m_buffer_offset = value.buf_addr();
   src.kc_bank = value.kcache_bank();
}

void
EncodeSourceVisitor::visit(const LiteralConstant& value)
{
   src.value = value.value();
}

void
EncodeSourceVisitor::visit(const InlineConstant& value)
{
   (void)value;
}

void
fill_alu_src_operands(r600_bytecode_alu& alu, const AluInstr& ai, r600_bytecode& bc)
{
   EBufferIndexMode kcache_index_mode = bim_none;
   PVirtualValue buffer_offset = nullptr;

   for (unsigned i = 0; i < ai.n_sources(); ++i) {
      buffer_offset = fill_alu_src(alu.src[i], ai.src(i), bc);
      alu.src[i].neg = ai.has_source_mod(i, AluInstr::mod_neg);
      if (!alu.is_op3)
         alu.src[i].abs = ai.has_source_mod(i, AluInstr::mod_abs);

      if (buffer_offset && kcache_index_mode == bim_none) {
         auto idx_reg = buffer_offset->as_register();
         if (idx_reg && idx_reg->has_flag(Register::addr_or_idx)) {
            switch (idx_reg->sel()) {
            case 1: kcache_index_mode = bim_zero; break;
            case 2: kcache_index_mode = bim_one; break;
            default:
               UNREACHABLE("Unsupported index mode");
            }
         } else {
            kcache_index_mode = bim_zero;
         }
         alu.src[i].kc_rel = kcache_index_mode;
      }
   }
}

void
fill_alu_dst(r600_bytecode_alu& alu, const AluInstr& ai, r600_bytecode& bc)
{
   auto dst = ai.dest();
   if (dst) {
      sfn_log << SfnLog::assembly << "  Current dst register is " << *dst << "\n";
      if (ai.opcode() != op1_mova_int) {
         alu.dst.sel = dst->sel() != g_registers_unused ? dst->sel() : g_registers_end;
         alu.dst.chan = dst->chan();

         alu.dst.write = ai.has_alu_flag(alu_write);
         alu.dst.rel = dst->addr() ? 1 : 0;
      } else if (bc.gfx_level == CAYMAN && dst->sel() > 0) {
         alu.dst.sel = dst->sel() + 1;
      }
   } else {
      alu.dst.chan = ai.dest_chan();
   }

   alu.dst.clamp = ai.has_alu_flag(alu_dst_clamp);
}

static const std::map<EAluOp, int> opcode_map = {

   {op2_add,                       ALU_OP2_ADD                      },
   {op2_mul,                       ALU_OP2_MUL                      },
   {op2_mul_ieee,                  ALU_OP2_MUL_IEEE                 },
   {op2_max,                       ALU_OP2_MAX                      },
   {op2_min,                       ALU_OP2_MIN                      },
   {op2_max_dx10,                  ALU_OP2_MAX_DX10                 },
   {op2_min_dx10,                  ALU_OP2_MIN_DX10                 },
   {op2_sete,                      ALU_OP2_SETE                     },
   {op2_setgt,                     ALU_OP2_SETGT                    },
   {op2_setge,                     ALU_OP2_SETGE                    },
   {op2_setne,                     ALU_OP2_SETNE                    },
   {op2_sete_dx10,                 ALU_OP2_SETE_DX10                },
   {op2_setgt_dx10,                ALU_OP2_SETGT_DX10               },
   {op2_setge_dx10,                ALU_OP2_SETGE_DX10               },
   {op2_setne_dx10,                ALU_OP2_SETNE_DX10               },
   {op1_fract,                     ALU_OP1_FRACT                    },
   {op1_trunc,                     ALU_OP1_TRUNC                    },
   {op1_ceil,                      ALU_OP1_CEIL                     },
   {op1_rndne,                     ALU_OP1_RNDNE                    },
   {op1_floor,                     ALU_OP1_FLOOR                    },
   {op2_ashr_int,                  ALU_OP2_ASHR_INT                 },
   {op2_lshr_int,                  ALU_OP2_LSHR_INT                 },
   {op2_lshl_int,                  ALU_OP2_LSHL_INT                 },
   {op1_mov,                       ALU_OP1_MOV                      },
   {op0_nop,                       ALU_OP0_NOP                      },
   {op2_mul_64,                    ALU_OP2_MUL_64                   },
   {op1v_flt64_to_flt32,           ALU_OP1_FLT64_TO_FLT32           },
   {op1v_flt32_to_flt64,           ALU_OP1_FLT32_TO_FLT64           },
   {op2_prede_int,                 ALU_OP2_PRED_SETE_INT            },
   {op2_pred_setne_int,            ALU_OP2_PRED_SETNE_INT           },
   {op2_pred_setge_int,            ALU_OP2_PRED_SETGE_INT           },
   {op2_pred_setgt_int,            ALU_OP2_PRED_SETGT_INT           },
   {op2_pred_setgt_uint,           ALU_OP2_PRED_SETGT_UINT          },
   {op2_pred_setge_uint,           ALU_OP2_PRED_SETGE_UINT          },
   {op2_pred_sete,                 ALU_OP2_PRED_SETE                },
   {op2_pred_setgt,                ALU_OP2_PRED_SETGT               },
   {op2_pred_setge,                ALU_OP2_PRED_SETGE               },
   {op2_pred_setne,                ALU_OP2_PRED_SETNE               },
   {op0_pred_set_clr,              ALU_OP0_PRED_SET_CLR             },
   {op1_pred_set_restore,          ALU_OP1_PRED_SET_RESTORE         },
   {op2_pred_sete_push,            ALU_OP2_PRED_SETE_PUSH           },
   {op2_pred_setgt_push,           ALU_OP2_PRED_SETGT_PUSH          },
   {op2_pred_setge_push,           ALU_OP2_PRED_SETGE_PUSH          },
   {op2_pred_setne_push,           ALU_OP2_PRED_SETNE_PUSH          },
   {op2_kille,                     ALU_OP2_KILLE                    },
   {op2_killgt,                    ALU_OP2_KILLGT                   },
   {op2_killge,                    ALU_OP2_KILLGE                   },
   {op2_killne,                    ALU_OP2_KILLNE                   },
   {op2_and_int,                   ALU_OP2_AND_INT                  },
   {op2_or_int,                    ALU_OP2_OR_INT                   },
   {op2_xor_int,                   ALU_OP2_XOR_INT                  },
   {op1_not_int,                   ALU_OP1_NOT_INT                  },
   {op2_add_int,                   ALU_OP2_ADD_INT                  },
   {op2_sub_int,                   ALU_OP2_SUB_INT                  },
   {op2_max_int,                   ALU_OP2_MAX_INT                  },
   {op2_min_int,                   ALU_OP2_MIN_INT                  },
   {op2_max_uint,                  ALU_OP2_MAX_UINT                 },
   {op2_min_uint,                  ALU_OP2_MIN_UINT                 },
   {op2_sete_int,                  ALU_OP2_SETE_INT                 },
   {op2_setgt_int,                 ALU_OP2_SETGT_INT                },
   {op2_setge_int,                 ALU_OP2_SETGE_INT                },
   {op2_setne_int,                 ALU_OP2_SETNE_INT                },
   {op2_setgt_uint,                ALU_OP2_SETGT_UINT               },
   {op2_setge_uint,                ALU_OP2_SETGE_UINT               },
   {op2_killgt_uint,               ALU_OP2_KILLGT_UINT              },
   {op2_killge_uint,               ALU_OP2_KILLGE_UINT              },
   {op2_pred_setgt_int,            ALU_OP2_PRED_SETGT_INT           },
   {op2_pred_setge_int,            ALU_OP2_PRED_SETGE_INT           },
   {op2_pred_setne_int,            ALU_OP2_PRED_SETNE_INT           },
   {op2_kille_int,                 ALU_OP2_KILLE_INT                },
   {op2_killgt_int,                ALU_OP2_KILLGT_INT               },
   {op2_killge_int,                ALU_OP2_KILLGE_INT               },
   {op2_killne_int,                ALU_OP2_KILLNE_INT               },
   {op2_pred_sete_push_int,        ALU_OP2_PRED_SETE_PUSH_INT       },
   {op2_pred_setgt_push_int,       ALU_OP2_PRED_SETGT_PUSH_INT      },
   {op2_pred_setge_push_int,       ALU_OP2_PRED_SETGE_PUSH_INT      },
   {op2_pred_setne_push_int,       ALU_OP2_PRED_SETNE_PUSH_INT      },
   {op2_pred_setlt_push_int,       ALU_OP2_PRED_SETLT_PUSH_INT      },
   {op2_pred_setle_push_int,       ALU_OP2_PRED_SETLE_PUSH_INT      },
   {op1_flt_to_int,                ALU_OP1_FLT_TO_INT               },
   {op1_bfrev_int,                 ALU_OP1_BFREV_INT                },
   {op2_addc_uint,                 ALU_OP2_ADDC_UINT                },
   {op2_subb_uint,                 ALU_OP2_SUBB_UINT                },
   {op0_group_barrier,             ALU_OP0_GROUP_BARRIER            },
   {op0_group_seq_begin,           ALU_OP0_GROUP_SEQ_BEGIN          },
   {op0_group_seq_end,             ALU_OP0_GROUP_SEQ_END            },
   {op2_set_mode,                  ALU_OP2_SET_MODE                 },
   {op1_set_cf_idx0,               ALU_OP0_SET_CF_IDX0              },
   {op1_set_cf_idx1,               ALU_OP0_SET_CF_IDX1              },
   {op2_set_lds_size,              ALU_OP2_SET_LDS_SIZE             },
   {op1_exp_ieee,                  ALU_OP1_EXP_IEEE                 },
   {op1_log_clamped,               ALU_OP1_LOG_CLAMPED              },
   {op1_log_ieee,                  ALU_OP1_LOG_IEEE                 },
   {op1_recip_clamped,             ALU_OP1_RECIP_CLAMPED            },
   {op1_recip_ff,                  ALU_OP1_RECIP_FF                 },
   {op1_recip_ieee,                ALU_OP1_RECIP_IEEE               },
   {op1_recipsqrt_clamped,         ALU_OP1_RECIPSQRT_CLAMPED        },
   {op1_recipsqrt_ff,              ALU_OP1_RECIPSQRT_FF             },
   {op1_recipsqrt_ieee1,           ALU_OP1_RECIPSQRT_IEEE           },
   {op1_sqrt_ieee,                 ALU_OP1_SQRT_IEEE                },
   {op1_sin,                       ALU_OP1_SIN                      },
   {op1_cos,                       ALU_OP1_COS                      },
   {op2_mullo_int,                 ALU_OP2_MULLO_INT                },
   {op2_mulhi_int,                 ALU_OP2_MULHI_INT                },
   {op2_mullo_uint,                ALU_OP2_MULLO_UINT               },
   {op2_mulhi_uint,                ALU_OP2_MULHI_UINT               },
   {op1_recip_int,                 ALU_OP1_RECIP_INT                },
   {op1_recip_uint,                ALU_OP1_RECIP_UINT               },
   {op1_recip_64,                  ALU_OP2_RECIP_64                 },
   {op1_recip_clamped_64,          ALU_OP2_RECIP_CLAMPED_64         },
   {op1_recipsqrt_64,              ALU_OP2_RECIPSQRT_64             },
   {op1_recipsqrt_clamped_64,      ALU_OP2_RECIPSQRT_CLAMPED_64     },
   {op1_sqrt_64,                   ALU_OP2_SQRT_64                  },
   {op1_flt_to_uint,               ALU_OP1_FLT_TO_UINT              },
   {op1_int_to_flt,                ALU_OP1_INT_TO_FLT               },
   {op1_uint_to_flt,               ALU_OP1_UINT_TO_FLT              },
   {op2_bfm_int,                   ALU_OP2_BFM_INT                  },
   {op1_flt32_to_flt16,            ALU_OP1_FLT32_TO_FLT16           },
   {op1_flt16_to_flt32,            ALU_OP1_FLT16_TO_FLT32           },
   {op1_ubyte0_flt,                ALU_OP1_UBYTE0_FLT               },
   {op1_ubyte1_flt,                ALU_OP1_UBYTE1_FLT               },
   {op1_ubyte2_flt,                ALU_OP1_UBYTE2_FLT               },
   {op1_ubyte3_flt,                ALU_OP1_UBYTE3_FLT               },
   {op1_bcnt_int,                  ALU_OP1_BCNT_INT                 },
   {op1_ffbh_uint,                 ALU_OP1_FFBH_UINT                },
   {op1_ffbl_int,                  ALU_OP1_FFBL_INT                 },
   {op1_ffbh_int,                  ALU_OP1_FFBH_INT                 },
   {op1_flt_to_uint4,              ALU_OP1_FLT_TO_UINT4             },
   {op2_dot_ieee,                  ALU_OP2_DOT_IEEE                 },
   {op1_flt_to_int_rpi,            ALU_OP1_FLT_TO_INT_RPI           },
   {op1_flt_to_int_floor,          ALU_OP1_FLT_TO_INT_FLOOR         },
   {op2_mulhi_uint24,              ALU_OP2_MULHI_UINT24             },
   {op1_mbcnt_32hi_int,            ALU_OP1_MBCNT_32HI_INT           },
   {op1_offset_to_flt,             ALU_OP1_OFFSET_TO_FLT            },
   {op2_mul_uint24,                ALU_OP2_MUL_UINT24               },
   {op1_bcnt_accum_prev_int,       ALU_OP1_BCNT_ACCUM_PREV_INT      },
   {op1_mbcnt_32lo_accum_prev_int, ALU_OP1_MBCNT_32LO_ACCUM_PREV_INT},
   {op2_sete_64,                   ALU_OP2_SETE_64                  },
   {op2_setne_64,                  ALU_OP2_SETNE_64                 },
   {op2_setgt_64,                  ALU_OP2_SETGT_64                 },
   {op2_setge_64,                  ALU_OP2_SETGE_64                 },
   {op2_min_64,                    ALU_OP2_MIN_64                   },
   {op2_max_64,                    ALU_OP2_MAX_64                   },
   {op2_dot4,                      ALU_OP2_DOT4                     },
   {op2_dot4_ieee,                 ALU_OP2_DOT4_IEEE                },
   {op2_cube,                      ALU_OP2_CUBE                     },
   {op1_max4,                      ALU_OP1_MAX4                     },
   {op1_frexp_64,                  ALU_OP1_FREXP_64                 },
   {op1_ldexp_64,                  ALU_OP2_LDEXP_64                 },
   {op1_fract_64,                  ALU_OP1_FRACT_64                 },
   {op2_pred_setgt_64,             ALU_OP2_PRED_SETGT_64            },
   {op2_pred_sete_64,              ALU_OP2_PRED_SETE_64             },
   {op2_pred_setge_64,             ALU_OP2_PRED_SETGE_64            },
   {op2_add_64,                    ALU_OP2_ADD_64                   },
   {op1_mova_int,                  ALU_OP1_MOVA_INT                 },
   {op1v_flt64_to_flt32,           ALU_OP1_FLT64_TO_FLT32           },
   {op1_flt32_to_flt64,            ALU_OP1_FLT32_TO_FLT64           },
   {op2_sad_accum_prev_uint,       ALU_OP2_SAD_ACCUM_PREV_UINT      },
   {op2_dot,                       ALU_OP2_DOT                      },
   {op1_mul_prev,                  ALU_OP1_MUL_PREV                 },
   {op1_mul_ieee_prev,             ALU_OP1_MUL_IEEE_PREV            },
   {op1_add_prev,                  ALU_OP1_ADD_PREV                 },
   {op2_muladd_prev,               ALU_OP2_MULADD_PREV              },
   {op2_muladd_ieee_prev,          ALU_OP2_MULADD_IEEE_PREV         },
   {op2_interp_xy,                 ALU_OP2_INTERP_XY                },
   {op2_interp_zw,                 ALU_OP2_INTERP_ZW                },
   {op2_interp_x,                  ALU_OP2_INTERP_X                 },
   {op2_interp_z,                  ALU_OP2_INTERP_Z                 },
   {op0_store_flags,               ALU_OP1_STORE_FLAGS              },
   {op1_load_store_flags,          ALU_OP1_LOAD_STORE_FLAGS         },
   {op0_lds_1a,                    ALU_OP2_LDS_1A                   },
   {op0_lds_1a1d,                  ALU_OP2_LDS_1A1D                 },
   {op0_lds_2a,                    ALU_OP2_LDS_2A                   },
   {op1_interp_load_p0,            ALU_OP1_INTERP_LOAD_P0           },
   {op1_interp_load_p10,           ALU_OP1_INTERP_LOAD_P10          },
   {op1_interp_load_p20,           ALU_OP1_INTERP_LOAD_P20          },
   {op3_bfe_uint,                  ALU_OP3_BFE_UINT                 },
   {op3_bfe_int,                   ALU_OP3_BFE_INT                  },
   {op3_bfi_int,                   ALU_OP3_BFI_INT                  },
   {op3_fma,                       ALU_OP3_FMA                      },
   {op3_cndne_64,                  ALU_OP3_CNDNE_64                 },
   {op3_fma_64,                    ALU_OP3_FMA_64                   },
   {op3_lerp_uint,                 ALU_OP3_LERP_UINT                },
   {op3_bit_align_int,             ALU_OP3_BIT_ALIGN_INT            },
   {op3_byte_align_int,            ALU_OP3_BYTE_ALIGN_INT           },
   {op3_sad_accum_uint,            ALU_OP3_SAD_ACCUM_UINT           },
   {op3_sad_accum_hi_uint,         ALU_OP3_SAD_ACCUM_HI_UINT        },
   {op3_muladd_uint24,             ALU_OP3_MULADD_UINT24            },
   {op3_lds_idx_op,                ALU_OP3_LDS_IDX_OP               },
   {op3_muladd,                    ALU_OP3_MULADD                   },
   {op3_muladd_m2,                 ALU_OP3_MULADD_M2                },
   {op3_muladd_m4,                 ALU_OP3_MULADD_M4                },
   {op3_muladd_d2,                 ALU_OP3_MULADD_D2                },
   {op3_muladd_ieee,               ALU_OP3_MULADD_IEEE              },
   {op3_cnde,                      ALU_OP3_CNDE                     },
   {op3_cndgt,                     ALU_OP3_CNDGT                    },
   {op3_cndge,                     ALU_OP3_CNDGE                    },
   {op3_cnde_int,                  ALU_OP3_CNDE_INT                 },
   {op3_cndgt_int,                 ALU_OP3_CNDGT_INT                },
   {op3_cndge_int,                 ALU_OP3_CNDGE_INT                },
   {op3_mul_lit,                   ALU_OP3_MUL_LIT                  },
};

auto
emit_bytecode_alu(r600_bytecode& bc,
                  const AluInstr& ai,
                  EAluOp opcode) -> std::tuple<bool, int, int>
{
   auto hw_opcode = opcode_map.find(opcode);
   if (hw_opcode == opcode_map.end()) {
      std::cerr << "Opcode not handled for " << ai << "\n";
      return std::make_tuple(false, 0, 0);
   }

   struct r600_bytecode_alu alu;
   memset(&alu, 0, sizeof(alu));

   alu.op = hw_opcode->second;
   alu.is_op3 = ai.n_sources() == 3;
   alu.omod = !alu.is_op3 ? ai.output_modifier() : 0;
   alu.last = ai.has_alu_flag(alu_last_instr);
   alu.execute_mask = ai.has_alu_flag(alu_update_exec);
   if (ai.bank_swizzle() != alu_vec_unknown)
      alu.bank_swizzle_force = ai.bank_swizzle();

   fill_alu_dst(alu, ai, bc);
   fill_alu_src_operands(alu, ai, bc);

   return std::make_tuple(!r600_bytecode_add_alu(&bc, &alu), alu.dst.sel, alu.dst.chan);
}

} // namespace r600