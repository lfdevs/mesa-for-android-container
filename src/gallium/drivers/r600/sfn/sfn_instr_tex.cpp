/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2022 Collabora LTD
 *
 * Author: Gert Wollny <gert.wollny@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "sfn_instr_tex.h"
#include "sfn_instr_alu.h"
#include "sfn_instr_fetch.h"
#include "sfn_debug.h"

namespace r600 {

using std::string;

TexInstr::TexInstr(Opcode op, const RegisterVec4& dest,
                   const RegisterVec4::Swizzle& dest_swizzle,
                   const RegisterVec4& src, unsigned sid, unsigned rid,
                   PVirtualValue sampler_offs):
   InstrWithVectorResult(dest, dest_swizzle),
   m_opcode(op),
   m_src(src),
   m_sampler_offset(sampler_offs),
   m_inst_mode(0),
   m_sampler_id(sid),
   m_resource_id(rid)
{
   memset(m_offset, 0, sizeof(m_offset));
   m_src.add_use(this);

   if (m_sampler_offset && m_sampler_offset->as_register())
      m_sampler_offset->as_register()->add_use(this);
}

void TexInstr::accept(ConstInstrVisitor& visitor) const
{
   visitor.visit(*this);
}

void TexInstr::accept(InstrVisitor& visitor)
{
   visitor.visit(this);
}

void TexInstr::set_offset(unsigned index, int32_t val)
{
   assert(index < 3);
   m_offset[index] = val;
}

int TexInstr::get_offset(unsigned index) const
{
   assert(index < 3);
   return m_offset[index] << 1;
}

void TexInstr::set_gather_comp(int cmp)
{
   m_inst_mode = cmp;
}

bool TexInstr::is_equal_to(const TexInstr& lhs) const
{
   if (m_opcode != lhs.m_opcode)
      return false;

   if (!comp_dest(lhs.dst(), lhs.all_dest_swizzle()))
      return false;

   if (m_src != lhs.m_src)
      return false;

   if (m_sampler_offset && lhs.m_sampler_offset) {
      if (!m_sampler_offset->equal_to(*lhs.m_sampler_offset))
         return false;
   } else if ((m_sampler_offset && !lhs.m_sampler_offset) ||
              (!m_sampler_offset && lhs.m_sampler_offset))
      return false;

   if (m_tex_flags != lhs.m_tex_flags)
      return false;

   for(int i = 0; i < 3; ++i) {
      if (m_offset[i] != lhs.m_offset[i])
         return false;
   }
   return m_inst_mode == lhs.m_inst_mode &&
         m_sampler_id == lhs.m_sampler_id &&
         m_resource_id == lhs.m_resource_id;
}

bool TexInstr::propagate_death()
{
   m_src.del_use(this);
   return true;
}

bool TexInstr::do_ready() const
{
   for (auto p : m_prepare_instr)
      if (!p->ready())
         return false;

   for (auto p :required_instr())
      if (!p->is_scheduled() && !p->is_dead()) {
         return false;
      }

   if (m_sampler_offset && m_sampler_offset->as_register() &&
       !m_sampler_offset->as_register()->ready(block_id(), index()))
       return false;
   return m_src.ready(block_id(), index());
}

void TexInstr::do_print(std::ostream& os) const
{

   os << "TEX " << opname(m_opcode) << " ";
   print_dest(os);

   os << " : ";
   m_src.print(os);

   os << " RID:" << m_resource_id
      << " SID:" << m_sampler_id;

   if (m_sampler_offset)
      os << " SO:" << *m_sampler_offset;

   if (m_offset[0])
      os << " OX:" << m_offset[0];
   if (m_offset[1])
      os << " OY:" << m_offset[1];
   if (m_offset[2])
      os << " OZ:" << m_offset[2];

   if (m_inst_mode || is_gather(m_opcode))
      os << " MODE:" << m_inst_mode;

   os << " ";
   os << (m_tex_flags.test(x_unnormalized) ? "U" : "N");
   os << (m_tex_flags.test(y_unnormalized) ? "U" : "N");
   os << (m_tex_flags.test(z_unnormalized) ? "U" : "N");
   os << (m_tex_flags.test(w_unnormalized) ? "U" : "N");
}

const char *TexInstr::opname(Opcode op)
{
   switch (op) {
   case ld: return "LD";
   case get_resinfo: return "GET_TEXTURE_RESINFO";
   case get_nsamples: return "GET_NUMBER_OF_SAMPLES";
   case get_tex_lod: return "GET_LOD";
   case get_gradient_h: return "GET_GRADIENTS_H";
   case get_gradient_v: return "GET_GRADIENTS_V";
   case set_offsets: return "SET_TEXTURE_OFFSETS";
   case keep_gradients: return "KEEP_GRADIENTS";
   case set_gradient_h: return "SET_GRADIENTS_H";
   case set_gradient_v: return "SET_GRADIENTS_V";
   case sample: return "SAMPLE";
   case sample_l: return "SAMPLE_L";
   case sample_lb: return "SAMPLE_LB";
   case sample_lz: return "SAMPLE_LZ";
   case sample_g: return "SAMPLE_G";
   case sample_g_lb: return "SAMPLE_G_L";
   case gather4: return "GATHER4";
   case gather4_o: return "GATHER4_O";
   case sample_c: return "SAMPLE_C";
   case sample_c_l: return "SAMPLE_C_L";
   case sample_c_lb: return "SAMPLE_C_LB";
   case sample_c_lz: return "SAMPLE_C_LZ";
   case sample_c_g: return "SAMPLE_C_G";
   case sample_c_g_lb: return "SAMPLE_C_G_L";
   case gather4_c: return "GATHER4_C";
   case gather4_c_o: return "OP_GATHER4_C_O";
   default: return "ERROR";
   }

}

const std::map<TexInstr::Opcode, std::string> TexInstr::s_opcode_map = {
   {ld, "LD"},
   {get_resinfo,"GET_TEXTURE_RESINFO"},
   {get_nsamples,"GET_NUMBER_OF_SAMPLES"},
   {get_tex_lod,"GET_LOD"},
   {get_gradient_h,"GET_GRADIENTS_H"},
   {get_gradient_v,"GET_GRADIENTS_V"},
   {set_offsets,"SET_TEXTURE_OFFSETS"},
   {keep_gradients,"KEEP_GRADIENTS"},
   {set_gradient_h,"SET_GRADIENTS_H"},
   {set_gradient_v,"SET_GRADIENTS_V"},
   {sample,"SAMPLE"},
   {sample_l,"SAMPLE_L"},
   {sample_lb,"SAMPLE_LB"},
   {sample_lz,"SAMPLE_LZ"},
   {sample_g,"SAMPLE_G"},
   {sample_g_lb,"SAMPLE_G_L"},
   {gather4,"GATHER4"},
   {gather4_o,"GATHER4_O"},
   {sample_c,"SAMPLE_C"},
   {sample_c_l,"SAMPLE_C_L"},
   {sample_c_lb,"SAMPLE_C_LB"},
   {sample_c_lz,"SAMPLE_C_LZ"},
   {sample_c_g,"SAMPLE_C_G"},
   {sample_c_g_lb,"SAMPLE_C_G_L"},
   {gather4_c,"GATHER4_C"},
   {gather4_c_o,"OP_GATHER4_C_O"},
   {unknown, "ERROR"}
};

bool TexInstr::is_gather(Opcode op)
{
   return op == gather4 || op == gather4_c ||
         op == gather4_o || op == gather4_c_o;
}

TexInstr::Opcode TexInstr::op_from_string(const std::string& s)
{
   for (auto& [op, str] : s_opcode_map) {
      if (s == str)
         return op;
   }
   return unknown;
}

Instr::Pointer TexInstr::from_string(std::istream& is, ValueFactory& value_fctory)
{
   string opstr;
   string deststr;
   is >> opstr >> deststr;

   auto opcode = TexInstr::op_from_string(opstr);

   RegisterVec4::Swizzle dest_swz;

   auto dest = value_fctory.dest_vec4_from_string(deststr, dest_swz, pin_group);

   char dummy;
   is >> dummy;
   assert(dummy == ':');

   string srcstr;
   is >> srcstr;

   auto src = value_fctory.src_vec4_from_string(srcstr);

   string res_id_str;
   string sampler_id_str;

   is >> res_id_str >> sampler_id_str;

   int res_id = int_from_string_with_prefix(res_id_str, "RID:");
   int sampler_id = int_from_string_with_prefix(sampler_id_str, "SID:");

   auto tex = new TexInstr( opcode, dest, dest_swz, src, sampler_id, res_id, nullptr);

   while (!is.eof() && is.good()) {
      std::string next_token;
      is >> next_token;

      if (next_token.empty())
         break;

      if (next_token[0] == 'U' || next_token[0] == 'N') {
         tex->read_tex_coord_normalitazion(next_token);
      } else {
         tex->set_tex_param(next_token);
      }
   }

   return tex;
}

void TexInstr::read_tex_coord_normalitazion(const std::string& flags)
{
   assert(flags.length() == 4);
   if (flags[0] == 'U') set_tex_flag(x_unnormalized);
   if (flags[1] == 'U') set_tex_flag(y_unnormalized);
   if (flags[2] == 'U') set_tex_flag(z_unnormalized);
   if (flags[3] == 'U') set_tex_flag(w_unnormalized);
}

void TexInstr::set_tex_param(const std::string& token)
{
   if (token.substr(0,3) == "OX:")
      set_offset(0, int_from_string_with_prefix(token, "OX:"));
   else if (token.substr(0,3) == "OY:")
      set_offset(1, int_from_string_with_prefix(token, "OY:"));
   else if (token.substr(0,3) == "OZ:")
      set_offset(2, int_from_string_with_prefix(token, "OZ:"));
   else if (token.substr(0,5) == "MODE:")
      set_inst_mode(int_from_string_with_prefix(token, "MODE:"));
   else if (token.substr(0,3) == "SO:")
      set_sampler_offset(VirtualValue::from_string(token.substr(3)));
   else {
      std::cerr << "Token '" << token << "': ";
      unreachable("Unknown token in tex param");
   }
}

bool TexInstr::from_nir(nir_tex_instr *tex, Shader& shader)
{
   Inputs src(*tex, shader.value_factory());

   if (tex->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
      switch (tex->op) {
      case nir_texop_txs:
         return emit_tex_txs(tex, src, {0,1,2,3}, shader);
      case nir_texop_txf:
         return emit_buf_txf(tex, src, shader);
      default:
         return false;
      }
   } else {
      switch (tex->op) {
      case nir_texop_tex:
         return emit_tex_tex(tex, src, shader);
      case nir_texop_txf:
         return emit_tex_txf(tex, src, shader);
      case nir_texop_txb:
      case nir_texop_txl:
         return emit_tex_txl_txb(tex, src, shader);
      case nir_texop_txs:
         return emit_tex_txs(tex, src, {0, 1, 2, 3}, shader);
      case nir_texop_lod:
         return emit_tex_lod(tex, src, shader);
      case nir_texop_query_levels:
         return emit_tex_txs(tex, src, {3,7,7,7}, shader);
      case nir_texop_txd:
          return emit_tex_txd(tex, src, shader);
      case nir_texop_txf_ms:
         if (shader.chip_class() < ISA_CC_EVERGREEN)
            return emit_tex_tex_ms_direct(tex, src, shader);
         else
            return emit_tex_tex_ms(tex, src, shader);
      case nir_texop_tg4:
         return emit_tex_tg4(tex, src, shader);
      case nir_texop_texture_samples:
         return emit_tex_texture_samples(tex, src, shader);
      default:
      return false;
      }
   }
   return true;
}

struct SamplerId {
   int id;
   bool indirect;
};

SamplerId
get_sampler_id(int sampler_id, const nir_variable *deref)
{
   SamplerId result = {sampler_id, false};

   if (deref) {
      assert(glsl_type_is_sampler(deref->type));
      result.id = deref->data.binding;
   }
   return result;
}


bool TexInstr::emit_tex_tex(nir_tex_instr *tex, Inputs& src, Shader& shader)
{
   auto& vf = shader.value_factory();

   sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(tex)
                 << "' (" << __func__ << ")\n";

   auto sampler = get_sampler_id(tex->sampler_index, src.sampler_deref);
   assert(!sampler.indirect);

   auto src_coord = prepare_source(tex, src, shader);
   auto dst = vf.dest_vec4(tex->dest, pin_group);

   auto irt = new TexInstr(src.opcode, dst, {0,1,2,3},  src_coord, sampler.id,
                           sampler.id + R600_MAX_CONST_BUFFERS,
                           src.sampler_offset);
   if (tex->is_array)
      irt->set_tex_flag(TexInstr::z_unnormalized);

   irt->set_rect_coordinate_flags(tex);
   irt->set_coord_offsets(src.offset);

   shader.emit_instruction(irt);
   return true;
}

bool TexInstr::emit_tex_txl_txb(nir_tex_instr *tex, Inputs& src, Shader& shader)
{
   auto& vf = shader.value_factory();

   auto sampler = get_sampler_id(tex->sampler_index, src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   auto src_coord = prepare_source(tex, src, shader);

   auto dst = vf.dest_vec4(tex->dest, pin_group);

   auto irt = new TexInstr(src.opcode, dst, {0,1,2,3},  src_coord, sampler.id,
                           sampler.id + R600_MAX_CONST_BUFFERS,
                           src.sampler_offset);

   if (tex->is_array)
      irt->set_tex_flag(TexInstr::z_unnormalized);

   irt->set_rect_coordinate_flags(tex);
   irt->set_coord_offsets(src.offset);

   shader.emit_instruction(irt);
   return true;
}


bool TexInstr::emit_tex_txf(nir_tex_instr *tex, Inputs& src, Shader& shader)
{
   auto& vf = shader.value_factory();

   int sampler = tex->sampler_index;

   auto swizzle = src.swizzle_from_ncomps(tex->coord_components);
   swizzle[3] = 3;

   if (tex->is_array && tex->sampler_dim == GLSL_SAMPLER_DIM_1D) {
      swizzle[2] = 1;
      swizzle[1] = 7;
   }

   auto src_coord = vf.temp_vec4(pin_group, swizzle);

   for (unsigned i = 0; i < tex->coord_components; i++) {
      unsigned k = i;
      if (tex->is_array && tex->sampler_dim == GLSL_SAMPLER_DIM_1D && i == 1)
         k = 2;


      if (src.offset) {
         shader.emit_instruction(new AluInstr(op2_add_int, src_coord[k], src.coord[i],
                                              vf.src(src.offset[i], i),
                                              AluInstr::write));
      } else {
         shader.emit_instruction(new AluInstr(op1_mov, src_coord[k], src.coord[i],AluInstr::write));
      }
   }

   shader.emit_instruction(new AluInstr(op1_mov, src_coord[3], src.lod, AluInstr::last_write));

   auto dst = vf.dest_vec4(tex->dest, pin_group);

   auto tex_ir = new TexInstr(src.opcode, dst, {0, 1, 2, 3}, src_coord,
                              sampler,
                              sampler + R600_MAX_CONST_BUFFERS,
                              src.sampler_offset);

   if (tex->is_array)
      tex_ir->set_tex_flag(z_unnormalized);

   tex_ir->set_rect_coordinate_flags(tex);
   tex_ir->set_sampler_offset(src.sampler_offset);

   shader.emit_instruction(tex_ir);

   return true;
}

bool TexInstr::emit_buf_txf(nir_tex_instr *tex, Inputs& src, Shader& shader)
{
   auto& vf = shader.value_factory();
   auto dst = vf.dest_vec4(tex->dest, pin_group);

   PRegister tex_offset = nullptr;
   if (src.texture_offset)
      tex_offset = shader.emit_load_to_register(src.texture_offset);

   auto *real_dst = &dst;
   RegisterVec4 tmp = vf.temp_vec4(pin_group);

   if (shader.chip_class() < ISA_CC_EVERGREEN) {
      real_dst = &tmp;
   }

   auto ir = new LoadFromBuffer(*real_dst, {0,1,2,3}, src.coord[0], 0,
                                tex->texture_index +  R600_MAX_CONST_BUFFERS,
                                tex_offset, fmt_invalid);
   ir->set_fetch_flag(FetchInstr::use_const_field);
   shader.emit_instruction(ir);
   shader.set_flag(Shader::sh_uses_tex_buffer);

   if (shader.chip_class() < ISA_CC_EVERGREEN) {
      auto tmp_w = vf.temp_register();
      int buf_sel = (512 + R600_BUFFER_INFO_OFFSET / 16) + 2 * tex->texture_index;
      AluInstr *ir = nullptr;
      for (int i = 0; i < 4; ++i) {
         auto d = i < 3 ? dst[i] : tmp_w;
         ir = new AluInstr(op2_and_int,  d, tmp[i],
                           vf.uniform(buf_sel, i, R600_BUFFER_INFO_CONST_BUFFER),
                           AluInstr::write);
         shader.emit_instruction(ir);
      }

      ir->set_alu_flag(alu_last_instr);
      shader.emit_instruction(new AluInstr(op2_or_int, dst[3], tmp_w,
                              vf.uniform(buf_sel + 1, 0, R600_BUFFER_INFO_CONST_BUFFER),
                              AluInstr::last_write));
   }

   return true;
}

bool TexInstr::emit_tex_tex_ms_direct(nir_tex_instr *tex, Inputs& src, Shader& shader)
{
   assert(tex->src[0].src.is_ssa);
   auto& vf = shader.value_factory();

   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(tex)
                 << "' (" << __func__ << ")\n";

   auto sampler = get_sampler_id(tex->sampler_index, src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   auto temp2 = vf.temp_vec4(pin_group);

   for (unsigned i = 0; i < tex->coord_components; ++i) {
      unsigned k = i;
      if (tex->is_array && tex->sampler_dim == GLSL_SAMPLER_DIM_1D && i == 1)
         k = 2;

      shader.emit_instruction(new AluInstr(op1_mov, temp2[k],
                                           src.coord[k], AluInstr::write));
   }

   shader.emit_instruction(new AluInstr(op1_mov, temp2[3], src.ms_index,
                           AluInstr::last_write));

   auto dst = vf.dest_vec4(tex->dest, pin_group);

   /* txf doesn't need rounding for the array index, but 1D has the array index
    * in the z component */
   auto tex_ir = new TexInstr(ld, dst, {0,1,2,3}, temp2,
                                    sampler.id,
                                    sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);

   shader.emit_instruction(tex_ir);
   return true;
}

bool TexInstr::emit_tex_tex_ms(nir_tex_instr *tex, Inputs& src, Shader& shader)
{
   assert(tex->src[0].src.is_ssa);
   auto& vf = shader.value_factory();

   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(tex)
                 << "' (" << __func__ << ")\n";

   auto sampler = get_sampler_id(tex->sampler_index, src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   auto sample_id_dest = vf.temp_vec4(pin_group);
   RegisterVec4::Swizzle dest_swz = {0,7,7,7};

   auto temp1 = vf.temp_vec4(pin_group);
   for (unsigned i = 0; i < tex->coord_components; ++i) {
      unsigned k = i;
      if (tex->is_array && tex->sampler_dim == GLSL_SAMPLER_DIM_1D && i == 1)
         k = 2;

      if (src.offset && i < src.offset->ssa->num_components)
         shader.emit_instruction(new AluInstr(op2_add_int, temp1[k],
                                              src.coord[i],
                                              vf.src(*src.offset, i),
                                              AluInstr::write));
      else
         shader.emit_instruction(new AluInstr(op1_mov, temp1[k],
                                              src.coord[i], AluInstr::write));
   }

   shader.emit_instruction(new AluInstr(op1_mov, temp1[3],
                                       src.ms_index, AluInstr::last_write));

   auto tex_sample_id_ir = new TexInstr(ld, sample_id_dest, dest_swz, temp1,
                                              sampler.id,
                                              sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);

   tex_sample_id_ir->set_tex_flag(x_unnormalized);
   tex_sample_id_ir->set_tex_flag(y_unnormalized);
   tex_sample_id_ir->set_tex_flag(z_unnormalized);
   tex_sample_id_ir->set_tex_flag(w_unnormalized);
   tex_sample_id_ir->set_inst_mode(1);

   shader.emit_instruction(tex_sample_id_ir);

   Register *sample_id_dest_reg = sample_id_dest[0];

   if (!src.ms_index->as_inline_const() ||
       src.ms_index->as_inline_const()->sel() != ALU_SRC_0) {

      auto help = vf.temp_register();

      shader.emit_instruction(new AluInstr(op2_lshl_int, help,
                                           src.ms_index, vf.literal(2),
                                           AluInstr::last_write));

      sample_id_dest_reg = vf.temp_register();
      shader.emit_instruction(new AluInstr(op2_lshr_int, sample_id_dest_reg,
                                           sample_id_dest[0], help,
                                           AluInstr::last_write));
   }

   auto temp2 = vf.temp_vec4(pin_group);

   for (unsigned i = 0; i < tex->coord_components; ++i) {
      unsigned k = i;
      if (tex->is_array && tex->sampler_dim == GLSL_SAMPLER_DIM_1D && i == 1)
         k = 2;

      shader.emit_instruction(new AluInstr(op1_mov, temp2[k],
                                           temp1[k], AluInstr::write));
   }

   shader.emit_instruction(new AluInstr(op2_and_int, temp2[3],
                                        sample_id_dest_reg, vf.literal(15),
                                        AluInstr::last_write));

   auto dst = vf.dest_vec4(tex->dest, pin_group);

   /* txf doesn't need rounding for the array index, but 1D has the array index
    * in the z component */
   auto tex_ir = new TexInstr(ld, dst, {0,1,2,3}, temp2,
                                    sampler.id,
                                    sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);

   shader.emit_instruction(tex_ir);
   return true;
}

bool TexInstr::emit_tex_texture_samples(nir_tex_instr* instr, Inputs& src, Shader& shader)
{
   RegisterVec4 dest = shader.value_factory().dest_vec4(instr->dest, pin_chan);
   RegisterVec4 help{0, true, {4,4,4,4}};

   int res_id = R600_MAX_CONST_BUFFERS + instr->sampler_index;

   auto ir = new TexInstr(src.opcode, dest, {3, 7, 7, 7}, help,
                          0, res_id, src.sampler_offset);
   shader.emit_instruction(ir);
   return true;
}


bool TexInstr::emit_tex_txd(nir_tex_instr *tex, Inputs& src, Shader& shader)
{

   auto& vf = shader.value_factory();

   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(tex)
                 << "' (" << __func__ << ")\n";

   auto dst = vf.dest_vec4(tex->dest, pin_group);
   RegisterVec4 empty_dst(0, false, {0,0,0,0}, pin_group);

   auto swizzle = src.swizzle_from_ncomps(tex->coord_components);

   if (tex->is_shadow)
      swizzle[3] = 3;

   unsigned array_coord = 2;
   if (tex->is_array && tex->sampler_dim == GLSL_SAMPLER_DIM_1D) {
      swizzle[2] = 1;
      swizzle[1] = 7;
      array_coord = 1;
   }

   auto src_coord = vf.temp_vec4(pin_group, swizzle);

   auto sampler = get_sampler_id(tex->sampler_index, src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   auto irgh = new TexInstr(set_gradient_h, empty_dst, {7,7,7,7}, src.ddx,
                            sampler.id,
                            sampler.id + R600_MAX_CONST_BUFFERS,
                            src.sampler_offset);

   auto irgv = new TexInstr(set_gradient_v, empty_dst, {7,7,7,7}, src.ddy,
                            sampler.id, sampler.id + R600_MAX_CONST_BUFFERS,
                            src.sampler_offset);

   auto tir = new TexInstr(src.opcode, dst, {0,1,2,3}, src_coord, sampler.id,
                          sampler.id + R600_MAX_CONST_BUFFERS,
                          src.sampler_offset);


   /* r600_bytecode_add_tex has a hack that will start a new tex CF if
    * set_gradient_h is emitted, so make sure it is emitted first */

   AluInstr *ir = nullptr;
   for (unsigned i = 0; i < tex->coord_components; ++i) {
      int k = i;
      if (tex->is_array && tex->sampler_dim == GLSL_SAMPLER_DIM_1D && i == 1)
         k = 2;

      ir = new AluInstr(tex->is_array && i == array_coord  ? op1_rndne : op1_mov,
                        src_coord[k], src.coord[i],
                        AluInstr::write);
      shader.emit_instruction(ir);
   }

   if (tex->is_shadow)  {
      ir = new AluInstr(op1_mov, src_coord[3], src.comperator, AluInstr::last_write);
      shader.emit_instruction(ir);
   }

   tir->add_prepare_instr(irgh);
   tir->add_prepare_instr(irgv);

   if (tex->is_array)
      tir->set_tex_flag(TexInstr::z_unnormalized);

   irgh->set_rect_coordinate_flags(tex);
   irgv->set_rect_coordinate_flags(tex);
   irgh->set_always_keep();
   irgv->set_always_keep();

   tir->set_rect_coordinate_flags(tex);

   tir->set_coord_offsets(src.offset);

   if (shader.last_txd())
      tir->add_required_instr(shader.last_txd());

   shader.emit_instruction(tir);
   shader.set_last_txd(tir);

   return true;
}

bool TexInstr::emit_tex_txs(nir_tex_instr *tex, Inputs& src,
                            RegisterVec4::Swizzle dest_swz, Shader& shader)
{
   auto& vf = shader.value_factory();

   auto dest = vf.dest_vec4(tex->dest, pin_group);

   if (tex->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
      if (shader.chip_class() >= ISA_CC_EVERGREEN) {
         shader.emit_instruction(new QueryBufferSizeInstr(dest, {0,7,7,7},
                                                          tex->sampler_index + R600_MAX_CONST_BUFFERS));
      } else {
         int id = 2 * tex->sampler_index + (512 + R600_BUFFER_INFO_OFFSET / 16) + 1;
         auto src = vf.uniform(id, 1, R600_BUFFER_INFO_CONST_BUFFER);
         shader.emit_instruction(new AluInstr(op1_mov, dest[0], src, AluInstr::last_write));
         shader.set_flag(Shader::sh_uses_tex_buffer);
      }
   } else {

      auto src_lod = vf.temp_register();
      shader.emit_instruction(new AluInstr(op1_mov, src_lod, src.lod, AluInstr::last_write));

      RegisterVec4 src_coord(src_lod, src_lod, src_lod, src_lod, pin_free);

      auto sampler = get_sampler_id(tex->sampler_index, src.sampler_deref);
      assert(!sampler.indirect && "Indirect sampler selection not yet supported");

      if (tex->is_array && tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE)
         dest_swz[2] = 7;

      auto ir = new TexInstr(get_resinfo, dest, dest_swz, src_coord,
                             sampler.id,
                             sampler.id + R600_MAX_CONST_BUFFERS,
                             src.sampler_offset);

      ir->set_dest_swizzle(dest_swz);
      shader.emit_instruction(ir);

      if (tex->is_array && tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
         auto src_loc = vf.uniform(512 + R600_BUFFER_INFO_OFFSET / 16 + (sampler.id >> 2),
                                   sampler.id & 3, R600_BUFFER_INFO_CONST_BUFFER);


         auto alu = new AluInstr(op1_mov, dest[2], src_loc, AluInstr::last_write);
         shader.emit_instruction(alu);
         shader.set_flag(Shader::sh_txs_cube_array_comp);
      }
   }

   return true;
}

bool TexInstr::emit_tex_tg4(nir_tex_instr* tex, Inputs& src , Shader& shader)
{
   auto& vf = shader.value_factory();

   r600::sfn_log << SfnLog::instr << "emit '"
              << *reinterpret_cast<nir_instr*>(tex)
              << "' (" << __func__ << ")\n";

   TexInstr *set_ofs = nullptr;

   auto src_coord = prepare_source(tex, src, shader);

   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(tex)
                 << "' (" << __func__ << ")\n";

   auto dst = vf.dest_vec4(tex->dest, pin_group);

   RegisterVec4 empty_dst(125, false, {7,7,7,7}, pin_group);

   /* pre CAYMAN needs swizzle */
   auto dest_swizzle = shader.chip_class() <= ISA_CC_EVERGREEN ?
            RegisterVec4::Swizzle{1, 2, 0, 3} :
            RegisterVec4::Swizzle{0, 1, 2, 3};

   auto sampler = get_sampler_id(tex->sampler_index, src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   bool literal_offset = false;
   if (src.offset) {
      literal_offset =  nir_src_as_const_value(*src.offset) != 0;
      r600::sfn_log << SfnLog::tex << " really have offsets and they are " <<
                       (literal_offset ? "literal" : "varying") <<
                       "\n";

      if (!literal_offset) {
         RegisterVec4::Swizzle swizzle = {4,4,4,4};
         int src_components = tex->coord_components;
         if (tex->is_array)
            --src_components;

         for (int i = 0; i < src_components; ++i)
            swizzle[i] = i;

         int noffsets = tex->coord_components;
         if (tex->is_array)
            --noffsets;

         auto ofs = vf.src_vec4(*src.offset, pin_group, swizzle);
         RegisterVec4 dummy(0, true, {7,7,7,7});

         set_ofs = new TexInstr(TexInstr::set_offsets, dummy, {7,7,7,7},
                                ofs, sampler.id,
                                sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);
      } else {
         src.opcode = src.opcode == gather4_o ? gather4 : gather4_c;
      }
   }

   auto irt = new TexInstr(src.opcode, dst, dest_swizzle, src_coord, sampler.id,
                           sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);

   irt->set_gather_comp(tex->component);

   if (tex->is_array)
      irt->set_tex_flag(z_unnormalized);

   if (literal_offset) {
      r600::sfn_log << SfnLog::tex << "emit literal offsets\n";
      irt->set_coord_offsets(src.offset);
   }

   irt->set_rect_coordinate_flags(tex);

   if (set_ofs) {
      set_ofs->set_always_keep();
      irt->add_prepare_instr(set_ofs);
   }

   shader.emit_instruction(irt);
   return true;
}

auto TexInstr::prepare_source(nir_tex_instr *tex, const Inputs& inputs, Shader& shader) -> RegisterVec4
{
   RegisterVec4::Swizzle target{7,7,7,7};
   PVirtualValue src[4]{nullptr,nullptr,nullptr,nullptr};


   for (unsigned i = 0; i < tex->coord_components; ++i) {
      target[i] = i;
      src[i] = inputs.coord[i];
   }

   // array index always goes into z
   if (tex->is_array && tex->sampler_dim == GLSL_SAMPLER_DIM_1D) {
      target[2]  = 1;
      target[1]  = 7;
      src[2] = inputs.coord[1];
   }

   /* With txl and txb shadow goes into z and lod or bias go into w */
   if (tex->op == nir_texop_txl || tex->op == nir_texop_txb) {
      target[3] = 3;
      src[3] = tex->op == nir_texop_txl ? inputs.lod : inputs.bias;
      if (tex->is_shadow){
         target[2] = 2;
         src[2] = inputs.comperator;
      }
   } else if (tex->is_shadow) {
      /* Other ops have shadow in w */
      target[3] = 3;
      src[3] = inputs.comperator;
   }

   auto src_coord = shader.value_factory().temp_vec4(pin_group, target);

   AluInstr *ir = nullptr;
   for (int i = 0; i < 4; ++i) {
      if (target[i] > 3)
        continue;

      auto op = tex->is_array && i == 2 ? op1_rndne : op1_mov;

      ir = new AluInstr(op,  src_coord[i], src[i], AluInstr::write);
      shader.emit_instruction(ir);
   }

   if (ir)
      ir->set_alu_flag(alu_last_instr);

   return src_coord;
}

TexInstr::Inputs::Inputs(const nir_tex_instr& instr, ValueFactory& vf):
   sampler_deref(nullptr),
   texture_deref(nullptr),
   bias(nullptr),
   comperator(nullptr),
   lod(nullptr),
   offset(nullptr),
   gather_comp(nullptr),
   ms_index(nullptr),
   sampler_offset(nullptr),
   texture_offset(nullptr),
   opcode(ld)
{
   //sfn_log << SfnLog::tex << "Get Inputs with " << instr.coord_components << " components\n";

   unsigned grad_components = instr.coord_components;
   if (instr.is_array && !instr.array_is_lowered_cube)
      --grad_components;

   for (unsigned i = 0; i < instr.num_srcs; ++i) {
      switch (instr.src[i].src_type) {
      case nir_tex_src_bias:
         bias = vf.src(instr.src[i], 0);
      break;

      case nir_tex_src_coord: {
         coord = vf.src_vec4(instr.src[i].src, pin_none, swizzle_from_ncomps(instr.coord_components));
      } break;
      case nir_tex_src_comparator:
         comperator = vf.src(instr.src[i], 0);
      break;
      case nir_tex_src_ddx:
         ddx = vf.src_vec4(instr.src[i].src, pin_group, swizzle_from_ncomps(grad_components));
      break;
      case nir_tex_src_ddy:
         ddy = vf.src_vec4(instr.src[i].src, pin_group, swizzle_from_ncomps(grad_components));
      break;
      case nir_tex_src_lod:
         lod = vf.src(instr.src[i].src, 0);
      break;
      case nir_tex_src_offset:
         offset = &instr.src[i].src;
      break;
         /* case nir_tex_src_sampler_deref:
         sampler_deref = get_deref_location(instr.src[i].src);
         break;
      case nir_tex_src_texture_deref:
         texture_deref = get_deref_location(instr.src[i].src);
         break;
      */
      case nir_tex_src_ms_index:
         ms_index = vf.src(instr.src[i], 0);
      break;
      case nir_tex_src_texture_offset:
         texture_offset = vf.src(instr.src[i], 0);
      break;
      case nir_tex_src_sampler_offset:
         sampler_offset = vf.src(instr.src[i], 0);
      break;
      case nir_tex_src_plane:
      case nir_tex_src_projector:
      case nir_tex_src_min_lod:
      default:
         unreachable("unsupported texture input type");
      }
   }

   opcode = get_opcode(instr);


}

auto TexInstr::Inputs::get_opcode(const nir_tex_instr& instr) -> Opcode
{
   switch (instr.op) {
   case nir_texop_tex:
      return instr.is_shadow ? sample_c : sample;
   case nir_texop_txf:
      return ld;
   case nir_texop_txb:
      return instr.is_shadow ? sample_c_lb : sample_lb;
   case nir_texop_txl:
      return instr.is_shadow ? sample_c_l : sample_l;
   case nir_texop_txs:
      return get_resinfo;
   case nir_texop_lod:
      return get_resinfo;
   case nir_texop_txd:
      return instr.is_shadow ? sample_c_g : sample_g;
   case nir_texop_tg4:
      return instr.is_shadow ?
               (offset ? gather4_c_o : gather4_c) :
               (offset ? gather4_o : gather4);

   case nir_texop_txf_ms:
      return ld;
   case nir_texop_query_levels:
      return get_resinfo;
   case nir_texop_texture_samples:
      return TexInstr::get_nsamples;
   default:
      unreachable("unsupported texture input opcode");
   }
}

bool TexInstr::emit_tex_lod(nir_tex_instr* tex, Inputs& src, Shader& shader)
{
   auto& vf = shader.value_factory();
   auto sampler = get_sampler_id(tex->sampler_index, src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   auto dst = shader.value_factory().dest_vec4(tex->dest, pin_group);

   auto swizzle = src.swizzle_from_ncomps(tex->coord_components);

   auto src_coord = vf.temp_vec4(pin_group, swizzle);

   AluInstr *ir = nullptr;
   for (unsigned i = 0; i < tex->coord_components; ++i) {
      ir = new AluInstr(op1_mov,
                        src_coord[i], src.coord[i],
                        AluInstr::write);
      shader.emit_instruction(ir);
   }
   if (ir)
      ir->set_alu_flag(alu_last_instr);

   auto irt = new TexInstr(TexInstr::get_tex_lod, dst, {1,0,7,7}, src_coord,
                           sampler.id, sampler.id + R600_MAX_CONST_BUFFERS);

   shader.emit_instruction(irt);
   return true;
}


RegisterVec4::Swizzle TexInstr::Inputs::swizzle_from_ncomps(int comps) const
{
   RegisterVec4::Swizzle swz;
   for (int i = 0; i < 4; ++i)
      swz[i] = i < comps ? i : 7;
   return swz;
}

void TexInstr::set_coord_offsets(nir_src *offset)
{
   if (!offset)
      return;

   assert(offset->is_ssa);
   auto literal = nir_src_as_const_value(*offset);
   assert(literal);

   for (int i = 0; i < offset->ssa->num_components; ++i)
      set_offset(i, literal[i].i32);
}

void TexInstr::set_rect_coordinate_flags(nir_tex_instr* instr)
{
   if (instr->sampler_dim == GLSL_SAMPLER_DIM_RECT) {
      set_tex_flag(x_unnormalized);
      set_tex_flag(y_unnormalized);
   }
}


}
