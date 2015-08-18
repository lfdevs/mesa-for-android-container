/*
 * Copyright © 2011 Intel Corporation
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

#ifndef BRW_VEC4_H
#define BRW_VEC4_H

#include <stdint.h>
#include "brw_shader.h"
#include "main/compiler.h"
#include "program/hash_table.h"
#include "brw_program.h"

#ifdef __cplusplus
#include "brw_ir_vec4.h"

extern "C" {
#endif

#include "brw_context.h"
#include "brw_eu.h"
#include "intel_asm_annotation.h"

#ifdef __cplusplus
}; /* extern "C" */
#endif

#include "glsl/ir.h"
#include "glsl/nir/nir.h"


#ifdef __cplusplus
extern "C" {
#endif

void
brw_vue_setup_prog_key_for_precompile(struct gl_context *ctx,
                                      struct brw_vue_prog_key *key,
                                      GLuint id, struct gl_program *prog);

#ifdef __cplusplus
} /* extern "C" */

namespace brw {

class vec4_live_variables;

/**
 * The vertex shader front-end.
 *
 * Translates either GLSL IR or Mesa IR (for ARB_vertex_program and
 * fixed-function) into VS IR.
 */
class vec4_visitor : public backend_shader, public ir_visitor
{
public:
   vec4_visitor(const struct brw_compiler *compiler,
                void *log_data,
                struct gl_program *prog,
                const struct brw_vue_prog_key *key,
                struct brw_vue_prog_data *prog_data,
		struct gl_shader_program *shader_prog,
                gl_shader_stage stage,
		void *mem_ctx,
                bool no_spills,
                int shader_time_index);
   ~vec4_visitor();

   dst_reg dst_null_f()
   {
      return dst_reg(brw_null_reg());
   }

   dst_reg dst_null_d()
   {
      return dst_reg(retype(brw_null_reg(), BRW_REGISTER_TYPE_D));
   }

   dst_reg dst_null_ud()
   {
      return dst_reg(retype(brw_null_reg(), BRW_REGISTER_TYPE_UD));
   }

   const struct brw_vue_prog_key * const key;
   struct brw_vue_prog_data * const prog_data;
   unsigned int sanity_param_count;

   char *fail_msg;
   bool failed;

   /**
    * GLSL IR currently being processed, which is associated with our
    * driver IR instructions for debugging purposes.
    */
   const void *base_ir;
   const char *current_annotation;

   int first_non_payload_grf;
   unsigned int max_grf;
   int *virtual_grf_start;
   int *virtual_grf_end;
   brw::vec4_live_variables *live_intervals;
   dst_reg userplane[MAX_CLIP_PLANES];

   dst_reg *variable_storage(ir_variable *var);

   void reladdr_to_temp(ir_instruction *ir, src_reg *reg, int *num_reladdr);

   bool need_all_constants_in_pull_buffer;

   /**
    * \name Visit methods
    *
    * As typical for the visitor pattern, there must be one \c visit method for
    * each concrete subclass of \c ir_instruction.  Virtual base classes within
    * the hierarchy should not have \c visit methods.
    */
   /*@{*/
   virtual void visit(ir_variable *);
   virtual void visit(ir_loop *);
   virtual void visit(ir_loop_jump *);
   virtual void visit(ir_function_signature *);
   virtual void visit(ir_function *);
   virtual void visit(ir_expression *);
   virtual void visit(ir_swizzle *);
   virtual void visit(ir_dereference_variable  *);
   virtual void visit(ir_dereference_array *);
   virtual void visit(ir_dereference_record *);
   virtual void visit(ir_assignment *);
   virtual void visit(ir_constant *);
   virtual void visit(ir_call *);
   virtual void visit(ir_return *);
   virtual void visit(ir_discard *);
   virtual void visit(ir_texture *);
   virtual void visit(ir_if *);
   virtual void visit(ir_emit_vertex *);
   virtual void visit(ir_end_primitive *);
   virtual void visit(ir_barrier *);
   /*@}*/

   src_reg result;

   /* Regs for vertex results.  Generated at ir_variable visiting time
    * for the ir->location's used.
    */
   dst_reg output_reg[BRW_VARYING_SLOT_COUNT];
   const char *output_reg_annotation[BRW_VARYING_SLOT_COUNT];
   int *uniform_size;
   int *uniform_vector_size;
   int uniform_array_size; /*< Size of uniform_[vector_]size arrays */
   int uniforms;

   src_reg shader_start_time;

   struct hash_table *variable_ht;

   bool run(gl_clip_plane *clip_planes);
   void fail(const char *msg, ...);

   void setup_uniform_clipplane_values(gl_clip_plane *clip_planes);
   virtual void setup_vector_uniform_values(const gl_constant_value *values,
                                            unsigned n);
   void setup_uniform_values(ir_variable *ir);
   void setup_builtin_uniform_values(ir_variable *ir);
   int setup_uniforms(int payload_reg);

   bool reg_allocate_trivial();
   bool reg_allocate();
   void evaluate_spill_costs(float *spill_costs, bool *no_spill);
   int choose_spill_reg(struct ra_graph *g);
   void spill_reg(int spill_reg);
   void move_grf_array_access_to_scratch();
   void move_uniform_array_access_to_pull_constants();
   void move_push_constants_to_pull_constants();
   void split_uniform_registers();
   void pack_uniform_registers();
   void calculate_live_intervals();
   void invalidate_live_intervals();
   void split_virtual_grfs();
   bool opt_vector_float();
   bool opt_reduce_swizzle();
   bool dead_code_eliminate();
   int var_range_start(unsigned v, unsigned n) const;
   int var_range_end(unsigned v, unsigned n) const;
   bool virtual_grf_interferes(int a, int b);
   bool opt_copy_propagation(bool do_constant_prop = true);
   bool opt_cse_local(bblock_t *block);
   bool opt_cse();
   bool opt_algebraic();
   bool opt_register_coalesce();
   bool eliminate_find_live_channel();
   bool is_dep_ctrl_unsafe(const vec4_instruction *inst);
   void opt_set_dependency_control();
   void opt_schedule_instructions();

   vec4_instruction *emit(vec4_instruction *inst);

   vec4_instruction *emit(enum opcode opcode);
   vec4_instruction *emit(enum opcode opcode, const dst_reg &dst);
   vec4_instruction *emit(enum opcode opcode, const dst_reg &dst,
                          const src_reg &src0);
   vec4_instruction *emit(enum opcode opcode, const dst_reg &dst,
                          const src_reg &src0, const src_reg &src1);
   vec4_instruction *emit(enum opcode opcode, const dst_reg &dst,
                          const src_reg &src0, const src_reg &src1,
                          const src_reg &src2);

   vec4_instruction *emit_before(bblock_t *block,
                                 vec4_instruction *inst,
				 vec4_instruction *new_inst);

#define EMIT1(op) vec4_instruction *op(const dst_reg &, const src_reg &);
#define EMIT2(op) vec4_instruction *op(const dst_reg &, const src_reg &, const src_reg &);
#define EMIT3(op) vec4_instruction *op(const dst_reg &, const src_reg &, const src_reg &, const src_reg &);
   EMIT1(MOV)
   EMIT1(NOT)
   EMIT1(RNDD)
   EMIT1(RNDE)
   EMIT1(RNDZ)
   EMIT1(FRC)
   EMIT1(F32TO16)
   EMIT1(F16TO32)
   EMIT2(ADD)
   EMIT2(MUL)
   EMIT2(MACH)
   EMIT2(MAC)
   EMIT2(AND)
   EMIT2(OR)
   EMIT2(XOR)
   EMIT2(DP3)
   EMIT2(DP4)
   EMIT2(DPH)
   EMIT2(SHL)
   EMIT2(SHR)
   EMIT2(ASR)
   vec4_instruction *CMP(dst_reg dst, src_reg src0, src_reg src1,
			 enum brw_conditional_mod condition);
   vec4_instruction *IF(src_reg src0, src_reg src1,
                        enum brw_conditional_mod condition);
   vec4_instruction *IF(enum brw_predicate predicate);
   EMIT1(SCRATCH_READ)
   EMIT2(SCRATCH_WRITE)
   EMIT3(LRP)
   EMIT1(BFREV)
   EMIT3(BFE)
   EMIT2(BFI1)
   EMIT3(BFI2)
   EMIT1(FBH)
   EMIT1(FBL)
   EMIT1(CBIT)
   EMIT3(MAD)
   EMIT2(ADDC)
   EMIT2(SUBB)
#undef EMIT1
#undef EMIT2
#undef EMIT3

   int implied_mrf_writes(vec4_instruction *inst);

   bool try_rewrite_rhs_to_dst(ir_assignment *ir,
			       dst_reg dst,
			       src_reg src,
			       vec4_instruction *pre_rhs_inst,
			       vec4_instruction *last_rhs_inst);

   /** Walks an exec_list of ir_instruction and sends it through this visitor. */
   void visit_instructions(const exec_list *list);

   void emit_vp_sop(enum brw_conditional_mod condmod, dst_reg dst,
                    src_reg src0, src_reg src1, src_reg one);

   void emit_bool_to_cond_code(ir_rvalue *ir, enum brw_predicate *predicate);
   void emit_if_gen6(ir_if *ir);

   vec4_instruction *emit_minmax(enum brw_conditional_mod conditionalmod, dst_reg dst,
                                 src_reg src0, src_reg src1);

   vec4_instruction *emit_lrp(const dst_reg &dst, const src_reg &x,
                              const src_reg &y, const src_reg &a);

   /**
    * Copy any live channel from \p src to the first channel of the
    * result.
    */
   src_reg emit_uniformize(const src_reg &src);

   void emit_block_move(dst_reg *dst, src_reg *src,
                        const struct glsl_type *type, brw_predicate predicate);

   void emit_constant_values(dst_reg *dst, ir_constant *value);

   /**
    * Emit the correct dot-product instruction for the type of arguments
    */
   void emit_dp(dst_reg dst, src_reg src0, src_reg src1, unsigned elements);

   void emit_scalar(ir_instruction *ir, enum prog_opcode op,
		    dst_reg dst, src_reg src0);

   void emit_scalar(ir_instruction *ir, enum prog_opcode op,
		    dst_reg dst, src_reg src0, src_reg src1);

   src_reg fix_3src_operand(const src_reg &src);
   src_reg resolve_source_modifiers(const src_reg &src);

   vec4_instruction *emit_math(enum opcode opcode, const dst_reg &dst, const src_reg &src0,
                               const src_reg &src1 = src_reg());

   src_reg fix_math_operand(const src_reg &src);

   void emit_pack_half_2x16(dst_reg dst, src_reg src0);
   void emit_unpack_half_2x16(dst_reg dst, src_reg src0);
   void emit_unpack_unorm_4x8(const dst_reg &dst, src_reg src0);
   void emit_unpack_snorm_4x8(const dst_reg &dst, src_reg src0);
   void emit_pack_unorm_4x8(const dst_reg &dst, const src_reg &src0);
   void emit_pack_snorm_4x8(const dst_reg &dst, const src_reg &src0);

   void emit_texture(ir_texture_opcode op,
                     dst_reg dest,
                     const glsl_type *dest_type,
                     src_reg coordinate,
                     int coord_components,
                     src_reg shadow_comparitor,
                     src_reg lod, src_reg lod2,
                     src_reg sample_index,
                     uint32_t constant_offset,
                     src_reg offset_value,
                     src_reg mcs,
                     bool is_cube_array,
                     uint32_t sampler, src_reg sampler_reg);

   uint32_t gather_channel(unsigned gather_component, uint32_t sampler);
   src_reg emit_mcs_fetch(const glsl_type *coordinate_type, src_reg coordinate,
                          src_reg sampler);
   void emit_gen6_gather_wa(uint8_t wa, dst_reg dst);
   void swizzle_result(ir_texture_opcode op, dst_reg dest,
                       src_reg orig_val, uint32_t sampler,
                       const glsl_type *dest_type);

   void emit_ndc_computation();
   void emit_psiz_and_flags(dst_reg reg);
   void emit_clip_distances(dst_reg reg, int offset);
   vec4_instruction *emit_generic_urb_slot(dst_reg reg, int varying);
   void emit_urb_slot(dst_reg reg, int varying);

   void emit_shader_time_begin();
   void emit_shader_time_end();
   void emit_shader_time_write(int shader_time_subindex, src_reg value);

   void emit_untyped_atomic(unsigned atomic_op, unsigned surf_index,
                            dst_reg dst, src_reg offset, src_reg src0,
                            src_reg src1);

   void emit_untyped_surface_read(unsigned surf_index, dst_reg dst,
                                  src_reg offset);

   src_reg get_scratch_offset(bblock_t *block, vec4_instruction *inst,
			      src_reg *reladdr, int reg_offset);
   src_reg get_pull_constant_offset(bblock_t *block, vec4_instruction *inst,
				    src_reg *reladdr, int reg_offset);
   void emit_scratch_read(bblock_t *block, vec4_instruction *inst,
			  dst_reg dst,
			  src_reg orig_src,
			  int base_offset);
   void emit_scratch_write(bblock_t *block, vec4_instruction *inst,
			   int base_offset);
   void emit_pull_constant_load(bblock_t *block, vec4_instruction *inst,
				dst_reg dst,
				src_reg orig_src,
				int base_offset);
   void emit_pull_constant_load_reg(dst_reg dst,
                                    src_reg surf_index,
                                    src_reg offset,
                                    bblock_t *before_block,
                                    vec4_instruction *before_inst);
   src_reg emit_resolve_reladdr(int scratch_loc[], bblock_t *block,
                                vec4_instruction *inst, src_reg src);

   bool try_emit_mad(ir_expression *ir);
   bool try_emit_b2f_of_compare(ir_expression *ir);
   void resolve_ud_negate(src_reg *reg);
   void resolve_bool_comparison(ir_rvalue *rvalue, src_reg *reg);

   src_reg get_timestamp();

   bool process_move_condition(ir_rvalue *ir);

   void dump_instruction(backend_instruction *inst);
   void dump_instruction(backend_instruction *inst, FILE *file);

   void visit_atomic_counter_intrinsic(ir_call *ir);

   int type_size(const struct glsl_type *type);
   bool is_high_sampler(src_reg sampler);

   virtual void emit_nir_code();
   virtual void nir_setup_inputs(nir_shader *shader);
   virtual void nir_setup_uniforms(nir_shader *shader);
   virtual void nir_setup_uniform(nir_variable *var);
   virtual void nir_setup_builtin_uniform(nir_variable *var);
   virtual void nir_setup_system_value_intrinsic(nir_intrinsic_instr *instr);
   virtual void nir_setup_system_values(nir_shader *shader);
   virtual void nir_emit_impl(nir_function_impl *impl);
   virtual void nir_emit_cf_list(exec_list *list);
   virtual void nir_emit_if(nir_if *if_stmt);
   virtual void nir_emit_loop(nir_loop *loop);
   virtual void nir_emit_block(nir_block *block);
   virtual void nir_emit_instr(nir_instr *instr);
   virtual void nir_emit_load_const(nir_load_const_instr *instr);
   virtual void nir_emit_intrinsic(nir_intrinsic_instr *instr);
   virtual void nir_emit_alu(nir_alu_instr *instr);
   virtual void nir_emit_jump(nir_jump_instr *instr);
   virtual void nir_emit_texture(nir_tex_instr *instr);

   dst_reg get_nir_dest(nir_dest dest, enum brw_reg_type type);
   dst_reg get_nir_dest(nir_dest dest, nir_alu_type type);
   dst_reg get_nir_dest(nir_dest dest);
   src_reg get_nir_src(nir_src src, enum brw_reg_type type,
                       unsigned num_components = 4);
   src_reg get_nir_src(nir_src src, nir_alu_type type,
                       unsigned num_components = 4);
   src_reg get_nir_src(nir_src src,
                       unsigned num_components = 4);

   virtual dst_reg *make_reg_for_system_value(int location,
                                              const glsl_type *type) = 0;

   dst_reg *nir_locals;
   dst_reg *nir_ssa_values;
   src_reg *nir_inputs;
   unsigned *nir_uniform_driver_location;
   dst_reg *nir_system_values;

protected:
   void emit_vertex();
   void lower_attributes_to_hw_regs(const int *attribute_map,
                                    bool interleaved);
   void setup_payload_interference(struct ra_graph *g, int first_payload_node,
                                   int reg_node_count);
   virtual void assign_binding_table_offsets();
   virtual void setup_payload() = 0;
   virtual void emit_prolog() = 0;
   virtual void emit_program_code() = 0;
   virtual void emit_thread_end() = 0;
   virtual void emit_urb_write_header(int mrf) = 0;
   virtual vec4_instruction *emit_urb_write_opcode(bool complete) = 0;
   virtual int compute_array_stride(ir_dereference_array *ir);
   virtual void gs_emit_vertex(int stream_id);
   virtual void gs_end_primitive();

private:
   /**
    * If true, then register allocation should fail instead of spilling.
    */
   const bool no_spills;

   int shader_time_index;

   unsigned last_scratch; /**< measured in 32-byte (register size) units */
};


/**
 * The vertex shader code generator.
 *
 * Translates VS IR to actual i965 assembly code.
 */
class vec4_generator
{
public:
   vec4_generator(const struct brw_compiler *compiler, void *log_data,
                  struct gl_shader_program *shader_prog,
                  struct gl_program *prog,
                  struct brw_vue_prog_data *prog_data,
                  void *mem_ctx,
                  bool debug_flag,
                  const char *stage_name,
                  const char *stage_abbrev);
   ~vec4_generator();

   const unsigned *generate_assembly(const cfg_t *cfg, unsigned *asm_size);

private:
   void generate_code(const cfg_t *cfg);

   void generate_math1_gen4(vec4_instruction *inst,
			    struct brw_reg dst,
			    struct brw_reg src);
   void generate_math2_gen4(vec4_instruction *inst,
			    struct brw_reg dst,
			    struct brw_reg src0,
			    struct brw_reg src1);
   void generate_math_gen6(vec4_instruction *inst,
                           struct brw_reg dst,
                           struct brw_reg src0,
                           struct brw_reg src1);

   void generate_tex(vec4_instruction *inst,
                     struct brw_reg dst,
                     struct brw_reg src,
                     struct brw_reg sampler_index);

   void generate_vs_urb_write(vec4_instruction *inst);
   void generate_gs_urb_write(vec4_instruction *inst);
   void generate_gs_urb_write_allocate(vec4_instruction *inst);
   void generate_gs_thread_end(vec4_instruction *inst);
   void generate_gs_set_write_offset(struct brw_reg dst,
                                     struct brw_reg src0,
                                     struct brw_reg src1);
   void generate_gs_set_vertex_count(struct brw_reg dst,
                                     struct brw_reg src);
   void generate_gs_svb_write(vec4_instruction *inst,
                              struct brw_reg dst,
                              struct brw_reg src0,
                              struct brw_reg src1);
   void generate_gs_svb_set_destination_index(vec4_instruction *inst,
                                              struct brw_reg dst,
                                              struct brw_reg src);
   void generate_gs_set_dword_2(struct brw_reg dst, struct brw_reg src);
   void generate_gs_prepare_channel_masks(struct brw_reg dst);
   void generate_gs_set_channel_masks(struct brw_reg dst, struct brw_reg src);
   void generate_gs_get_instance_id(struct brw_reg dst);
   void generate_gs_ff_sync_set_primitives(struct brw_reg dst,
                                           struct brw_reg src0,
                                           struct brw_reg src1,
                                           struct brw_reg src2);
   void generate_gs_ff_sync(vec4_instruction *inst,
                            struct brw_reg dst,
                            struct brw_reg src0,
                            struct brw_reg src1);
   void generate_gs_set_primitive_id(struct brw_reg dst);
   void generate_oword_dual_block_offsets(struct brw_reg m1,
					  struct brw_reg index);
   void generate_scratch_write(vec4_instruction *inst,
			       struct brw_reg dst,
			       struct brw_reg src,
			       struct brw_reg index);
   void generate_scratch_read(vec4_instruction *inst,
			      struct brw_reg dst,
			      struct brw_reg index);
   void generate_pull_constant_load(vec4_instruction *inst,
				    struct brw_reg dst,
				    struct brw_reg index,
				    struct brw_reg offset);
   void generate_pull_constant_load_gen7(vec4_instruction *inst,
                                         struct brw_reg dst,
                                         struct brw_reg surf_index,
                                         struct brw_reg offset);
   void generate_set_simd4x2_header_gen9(vec4_instruction *inst,
                                         struct brw_reg dst);
   void generate_unpack_flags(struct brw_reg dst);

   const struct brw_compiler *compiler;
   void *log_data; /* Passed to compiler->*_log functions */

   const struct brw_device_info *devinfo;

   struct brw_codegen *p;

   struct gl_shader_program *shader_prog;
   const struct gl_program *prog;

   struct brw_vue_prog_data *prog_data;

   void *mem_ctx;
   const char *stage_name;
   const char *stage_abbrev;
   const bool debug_flag;
};

} /* namespace brw */
#endif /* __cplusplus */

#endif /* BRW_VEC4_H */
