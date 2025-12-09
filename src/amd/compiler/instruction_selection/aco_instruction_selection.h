/*
 * Copyright © 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ACO_INSTRUCTION_SELECTION_H
#define ACO_INSTRUCTION_SELECTION_H

#include "aco_builder.h"
#include "aco_ir.h"

#include "nir.h"

#include <array>
#include <unordered_map>
#include <vector>

namespace aco {

struct parameter_info {
   bool discardable;
   bool is_reg;
   union {
      Definition def;
      unsigned scratch_offset;
   };
};

struct call_info {
   nir_call_instr* nir_instr;
   Instruction* aco_instr;
   std::vector<parameter_info> return_info;
   unsigned scratch_param_size;
};

struct callee_info {
   std::vector<parameter_info> param_infos;
   parameter_info return_address;
   parameter_info stack_ptr;
   unsigned reg_param_count = 0;
   unsigned reg_discardable_param_count = 0;
   unsigned scratch_param_size = 0;
};

enum aco_color_output_type {
   ACO_TYPE_ANY32,
   ACO_TYPE_FLOAT16,
   ACO_TYPE_INT16,
   ACO_TYPE_UINT16,
};

struct shader_io_state {
   uint8_t mask[VARYING_SLOT_MAX];
   Temp temps[VARYING_SLOT_MAX * 4u];

   shader_io_state()
   {
      memset(mask, 0, sizeof(mask));
      std::fill_n(temps, VARYING_SLOT_MAX * 4u, Temp(0, RegClass::v1));
   }
};

struct exec_info {
   /* Set to false when in_divergent_cf==false */
   bool potentially_empty_discard = false;

   /* Set to false when leaving the loop, or if parent_if.is_divergent==false and
    * parent_loop.has_divergent_continue==false. */
   bool potentially_empty_break = false;

   /* Set to false when leaving the loop, or if parent_if.is_divergent==false. */
   bool potentially_empty_continue = false;

   void combine(struct exec_info& other)
   {
      potentially_empty_discard |= other.potentially_empty_discard;
      potentially_empty_break |= other.potentially_empty_break;
      potentially_empty_continue |= other.potentially_empty_continue;
   }

   bool empty() const noexcept
   {
      return potentially_empty_discard || potentially_empty_break || potentially_empty_continue;
   }
};

struct cf_context {
   struct {
      unsigned header_idx = 0;
      Block* exit = NULL;
      bool has_divergent_continue = false;
      bool has_divergent_break = false;
   } parent_loop;
   struct {
      bool is_divergent = false;
   } parent_if;

   bool has_branch;
   bool has_divergent_branch = false;
   bool had_divergent_discard = false;
   bool in_divergent_cf = false;
   struct exec_info exec;
};

struct if_context {
   Temp cond;

   cf_context cf_info_old;

   unsigned BB_if_idx;
   unsigned invert_idx;
   Block BB_invert;
   Block BB_endif;
};

struct loop_context {
   Block loop_exit;

   cf_context cf_info_old;
};

struct isel_context {
   const struct aco_compiler_options* options;
   const struct ac_shader_args* args;
   Program* program;
   nir_shader* shader;
   uint32_t constant_data_offset;
   Block* block;
   uint32_t first_temp_id;
   std::unordered_map<unsigned, std::array<Temp, NIR_MAX_VEC_COMPONENTS>> allocated_vec;
   std::vector<Temp> unended_linear_vgprs;
   Stage stage;

   cf_context cf_info;
   bool skipping_empty_exec = false;
   if_context empty_exec_skip;

   /* NIR range analysis. */
   struct hash_table* range_ht;
   struct hash_table* numlsb_ht;

   Temp arg_temps[AC_MAX_ARGS];
   Operand workgroup_id[3];
   Temp ttmp8;

   /* tessellation information */
   bool any_tcs_inputs_via_lds = false;
   bool tcs_in_out_eq = false;

   /* Fragment color output information */
   uint16_t output_color_types;

   /* I/O information */
   shader_io_state inputs;
   shader_io_state outputs;

   /* WQM information */
   uint32_t wqm_block_idx;
   uint32_t wqm_instruction_idx;

   BITSET_DECLARE(output_args, AC_MAX_ARGS);

   /* Function information */
   ABI callee_abi;
   struct callee_info callee_info;
   std::vector<call_info> call_infos;
   Temp next_divergent_pc;
   Temp next_pc;
};

inline Temp
get_ssa_temp(struct isel_context* ctx, nir_def* def)
{
   uint32_t id = ctx->first_temp_id + def->index;
   return Temp(id, ctx->program->temp_rc[id]);
}

inline Temp
get_arg(isel_context* ctx, struct ac_arg arg)
{
   assert(arg.used);
   return ctx->arg_temps[arg.arg_index];
}

inline PhysReg
get_arg_reg(const struct ac_shader_args* args, struct ac_arg arg)
{
   assert(arg.used);
   enum ac_arg_regfile file = args->args[arg.arg_index].file;
   unsigned reg = args->args[arg.arg_index].offset;
   return PhysReg(file == AC_ARG_SGPR ? reg : reg + 256);
}

inline void
set_wqm(isel_context* ctx, bool enable_helpers = false)
{
   if (ctx->program->stage == fragment_fs) {
      ctx->wqm_block_idx = ctx->block->index;
      ctx->wqm_instruction_idx = ctx->block->instructions.size();
      if (ctx->shader)
         enable_helpers |= ctx->shader->info.fs.require_full_quads;
      ctx->program->needs_wqm |= enable_helpers;
   }
}

inline bool
should_declare_array(ac_image_dim dim)
{
   return dim == ac_image_cube || dim == ac_image_1darray || dim == ac_image_2darray ||
          dim == ac_image_2darraymsaa;
}

template <typename T>
inline void
init_disable_wqm(Builder& bld, T& instr, bool disable_wqm)
{
   if (disable_wqm) {
      instr_exact_mask(&instr) = Operand();
      instr_wqm_mask(&instr) = Operand();
      instr.disable_wqm = true;
      bld.program->needs_exact = true;
   }
}

/* aco_isel_setup.cpp */
void init_context(isel_context* ctx, nir_shader* shader);
void cleanup_context(isel_context* ctx);
isel_context setup_isel_context(Program* program, unsigned shader_count,
                                struct nir_shader* const* shaders, ac_shader_config* config,
                                const struct aco_compiler_options* options,
                                const struct aco_shader_info* info,
                                const struct ac_shader_args* args,
                                SWStage sw_stage = SWStage::None);

/* aco_isel_cfg.cpp */
void emit_loop_break(isel_context* ctx);
void emit_loop_continue(isel_context* ctx);
void begin_loop(isel_context* ctx, loop_context* lc);
void end_loop(isel_context* ctx, loop_context* lc);
void begin_uniform_if_then(isel_context* ctx, if_context* ic, Temp cond);
void begin_uniform_if_else(isel_context* ctx, if_context* ic, bool logical_else = true);
void end_uniform_if(isel_context* ctx, if_context* ic, bool logical_else = true);
void begin_divergent_if_then(isel_context* ctx, if_context* ic, Temp cond,
                             nir_selection_control sel_ctrl = nir_selection_control_none);
void begin_divergent_if_else(isel_context* ctx, if_context* ic,
                             nir_selection_control sel_ctrl = nir_selection_control_none);
void end_divergent_if(isel_context* ctx, if_context* ic);
void begin_empty_exec_skip(isel_context* ctx, nir_instr* after_instr, nir_block* block);
void end_empty_exec_skip(isel_context* ctx);

/* aco_isel_helpers.cpp */
void append_logical_start(Block* b);
void append_logical_end(isel_context* ctx, bool append_reload_preserved = true);
Temp get_ssa_temp_tex(struct isel_context* ctx, nir_def* def, bool is_16bit);
Temp bool_to_vector_condition(isel_context* ctx, Temp val, Temp dst = Temp(0, s2));
Temp bool_to_scalar_condition(isel_context* ctx, Temp val, Temp dst = Temp(0, s1));
Temp as_vgpr(isel_context* ctx, Temp val);
Temp emit_extract_vector(isel_context* ctx, Temp src, uint32_t idx, RegClass dst_rc);
void emit_split_vector(isel_context* ctx, Temp vec_src, unsigned num_components);
void expand_vector(isel_context* ctx, Temp vec_src, Temp dst, unsigned num_components,
                   unsigned mask, bool zero_padding = false);
Temp convert_int(isel_context* ctx, Builder& bld, Temp src, unsigned src_bits, unsigned dst_bits,
                 bool sign_extend, Temp dst = Temp());
Temp convert_pointer_to_64_bit(isel_context* ctx, Temp ptr, bool non_uniform = false);
void select_vec2(isel_context* ctx, Temp dst, Temp cond, Temp then, Temp els);
Operand load_lds_size_m0(Builder& bld);
Temp create_vec_from_array(isel_context* ctx, Temp arr[], unsigned cnt, RegType reg_type,
                           unsigned elem_size_bytes, unsigned split_cnt = 0u, Temp dst = Temp());
void emit_interp_instr(isel_context* ctx, unsigned idx, unsigned component, Temp src, Temp dst,
                       Temp prim_mask, bool high_16bits);
void emit_interp_mov_instr(isel_context* ctx, unsigned idx, unsigned component, unsigned vertex_id,
                           Temp dst, Temp prim_mask, bool high_16bits);
std::vector<Temp> emit_pack_v1(isel_context* ctx, const std::vector<Temp>& unpacked);
MIMG_instruction* emit_mimg(Builder& bld, aco_opcode op, std::vector<Temp> dsts, Temp rsrc,
                            Operand samp, std::vector<Temp> coords, bool disable_wqm,
                            Operand vdata = Operand(v1));
Operand emit_tfe_init(Builder& bld, Temp dst);
struct aco_export_mrt {
   Operand out[4];
   unsigned enabled_channels;
   unsigned target;
   bool compr;
};
void create_fs_dual_src_export_gfx11(isel_context* ctx, const struct aco_export_mrt* mrt0,
                                     const struct aco_export_mrt* mrt1);
Temp lanecount_to_mask(isel_context* ctx, Temp count, unsigned bit_offset);
void build_end_with_regs(isel_context* ctx, std::vector<Operand>& regs);
Instruction* add_startpgm(struct isel_context* ctx);
void finish_program(isel_context* ctx);

struct callee_info get_callee_info(amd_gfx_level gfx_level, const ABI& abi, unsigned param_count,
                                   const nir_parameter* parameters, Program* program,
                                   RegisterDemand reg_limit);

#define isel_err(...) _isel_err(ctx, __FILE__, __LINE__, __VA_ARGS__)

void _isel_err(isel_context* ctx, const char* file, unsigned line, const nir_instr* instr,
               const char* msg);

/* aco_select_nir_alu.cpp */
void visit_alu_instr(isel_context* ctx, nir_alu_instr* instr);

/* aco_select_nir_intrinsics.cpp */
void visit_intrinsic(isel_context* ctx, nir_intrinsic_instr* instr);

} // namespace aco

#endif /* ACO_INSTRUCTION_SELECTION_H */
