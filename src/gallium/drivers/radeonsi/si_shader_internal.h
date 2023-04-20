/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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

#ifndef SI_SHADER_PRIVATE_H
#define SI_SHADER_PRIVATE_H

#include "ac_shader_abi.h"
#include "si_shader.h"

struct util_debug_callback;

struct si_shader_args {
   struct ac_shader_args ac;

   struct ac_arg const_and_shader_buffers;
   struct ac_arg samplers_and_images;

   /* For merged shaders, the per-stage descriptors for the stage other
    * than the one we're processing, used to pass them through from the
    * first stage to the second.
    */
   struct ac_arg other_const_and_shader_buffers;
   struct ac_arg other_samplers_and_images;

   struct ac_arg internal_bindings;
   struct ac_arg bindless_samplers_and_images;
   struct ac_arg small_prim_cull_info;
   struct ac_arg gs_attr_address;
   /* API VS */
   struct ac_arg vb_descriptors[5];
   struct ac_arg vertex_index0;
   /* VS state bits. See the VS_STATE_* and GS_STATE_* definitions. */
   struct ac_arg vs_state_bits;
   struct ac_arg vs_blit_inputs;

   /* API TCS & TES */
   /* Layout of TCS outputs in the offchip buffer
    * # 6 bits
    *   [0:5] = the number of patches per threadgroup - 1, max = 63
    * # 5 bits
    *   [6:10] = the number of output vertices per patch - 1, max = 31
    * # 21 bits
    *   [11:31] = the offset of per patch attributes in the buffer in bytes.
    *             max = NUM_PATCHES*32*32*16 = 1M
    */
   struct ac_arg tcs_offchip_layout;

   /* API TCS */
   /* Offsets where TCS outputs and TCS patch outputs live in LDS (<= 16K):
    *   [16:31] = TCS output patch0 offset for per-patch / 4, max = 16K / 4 = 4K
    */
   struct ac_arg tcs_out_lds_offsets;
   /* Layout of TCS outputs / TES inputs:
    *   [13:18] = gl_PatchVerticesIn, max = 32
    *   [19:31] = high 13 bits of the 32-bit address of tessellation ring buffers
    */
   struct ac_arg tcs_out_lds_layout;

   /* API TES */
   struct ac_arg tes_offchip_addr;
   /* PS */
   struct ac_arg pos_fixed_pt;
   struct ac_arg alpha_reference;
   /* CS */
   struct ac_arg block_size;
   struct ac_arg cs_user_data;
   struct ac_arg cs_shaderbuf[3];
   struct ac_arg cs_image[3];
};

struct si_shader_context {
   struct ac_llvm_context ac;
   struct si_shader *shader;
   struct si_screen *screen;

   gl_shader_stage stage;

   /* For clamping the non-constant index in resource indexing: */
   unsigned num_const_buffers;
   unsigned num_shader_buffers;
   unsigned num_images;
   unsigned num_samplers;

   struct si_shader_args *args;
   struct ac_shader_abi abi;

   LLVMBasicBlockRef merged_wrap_if_entry_block;
   int merged_wrap_if_label;

   struct ac_llvm_pointer main_fn;
   LLVMTypeRef return_type;

   struct ac_llvm_compiler *compiler;

   /* Preloaded descriptors. */
   LLVMValueRef esgs_ring;
   LLVMValueRef gsvs_ring[4];
   LLVMValueRef tess_offchip_ring;
   LLVMValueRef instance_divisor_constbuf;

   LLVMValueRef gs_ngg_emit;
   struct ac_llvm_pointer gs_ngg_scratch;
   LLVMValueRef return_value;
};

static inline struct si_shader_context *si_shader_context_from_abi(struct ac_shader_abi *abi)
{
   return container_of(abi, struct si_shader_context, abi);
}

struct ac_nir_gs_output_info;
typedef struct ac_nir_gs_output_info ac_nir_gs_output_info;

struct nir_builder;
typedef struct nir_builder nir_builder;

/* si_shader.c */
bool si_is_multi_part_shader(struct si_shader *shader);
bool si_is_merged_shader(struct si_shader *shader);
void si_add_arg_checked(struct ac_shader_args *args, enum ac_arg_regfile file, unsigned registers,
                        enum ac_arg_type type, struct ac_arg *arg, unsigned idx);
void si_init_shader_args(struct si_shader *shader, struct si_shader_args *args);
unsigned si_get_max_workgroup_size(const struct si_shader *shader);
bool si_vs_needs_prolog(const struct si_shader_selector *sel,
                        const struct si_vs_prolog_bits *prolog_key);
void si_get_vs_prolog_key(const struct si_shader_info *info, unsigned num_input_sgprs,
                          const struct si_vs_prolog_bits *prolog_key,
                          struct si_shader *shader_out, union si_shader_part_key *key);
struct nir_shader *si_get_nir_shader(struct si_shader *shader, struct si_shader_args *args,
                                     bool *free_nir, uint64_t tcs_vgpr_only_inputs,
                                     ac_nir_gs_output_info *output_info);
void si_get_tcs_epilog_key(struct si_shader *shader, union si_shader_part_key *key);
bool si_need_ps_prolog(const union si_shader_part_key *key);
void si_get_ps_prolog_key(struct si_shader *shader, union si_shader_part_key *key,
                          bool separate_prolog);
void si_get_ps_epilog_key(struct si_shader *shader, union si_shader_part_key *key);

/* gfx10_shader_ngg.c */
unsigned gfx10_ngg_get_vertices_per_prim(struct si_shader *shader);
bool gfx10_ngg_export_prim_early(struct si_shader *shader);
unsigned gfx10_ngg_get_scratch_dw_size(struct si_shader *shader);
bool gfx10_ngg_calculate_subgroup_info(struct si_shader *shader);

/* si_nir_lower_abi.c */
nir_ssa_def *si_nir_load_internal_binding(nir_builder *b, struct si_shader_args *args,
                                          unsigned slot, unsigned num_components);
bool si_nir_lower_abi(nir_shader *nir, struct si_shader *shader, struct si_shader_args *args);

/* si_nir_lower_resource.c */
bool si_nir_lower_resource(nir_shader *nir, struct si_shader *shader,
                           struct si_shader_args *args);

/* si_nir_lower_vs_inputs.c */
bool si_nir_lower_vs_inputs(nir_shader *nir, struct si_shader *shader,
                            struct si_shader_args *args);

/* si_shader_llvm.c */
bool si_compile_llvm(struct si_screen *sscreen, struct si_shader_binary *binary,
                     struct ac_shader_config *conf, struct ac_llvm_compiler *compiler,
                     struct ac_llvm_context *ac, struct util_debug_callback *debug,
                     gl_shader_stage stage, const char *name, bool less_optimized);
void si_llvm_context_init(struct si_shader_context *ctx, struct si_screen *sscreen,
                          struct ac_llvm_compiler *compiler, unsigned wave_size,
                          bool exports_color_null, bool exports_mrtz,
                          enum ac_float_mode float_mode);
void si_llvm_create_func(struct si_shader_context *ctx, const char *name, LLVMTypeRef *return_types,
                         unsigned num_return_elems, unsigned max_workgroup_size);
void si_llvm_create_main_func(struct si_shader_context *ctx);
void si_llvm_optimize_module(struct si_shader_context *ctx);
void si_llvm_dispose(struct si_shader_context *ctx);
LLVMValueRef si_buffer_load_const(struct si_shader_context *ctx, LLVMValueRef resource,
                                  LLVMValueRef offset);
void si_llvm_build_ret(struct si_shader_context *ctx, LLVMValueRef ret);
LLVMValueRef si_insert_input_ret(struct si_shader_context *ctx, LLVMValueRef ret,
                                 struct ac_arg param, unsigned return_index);
LLVMValueRef si_insert_input_ret_float(struct si_shader_context *ctx, LLVMValueRef ret,
                                       struct ac_arg param, unsigned return_index);
LLVMValueRef si_insert_input_ptr(struct si_shader_context *ctx, LLVMValueRef ret,
                                 struct ac_arg param, unsigned return_index);
LLVMValueRef si_prolog_get_internal_bindings(struct si_shader_context *ctx);
void si_llvm_declare_esgs_ring(struct si_shader_context *ctx);
LLVMValueRef si_unpack_param(struct si_shader_context *ctx, struct ac_arg param, unsigned rshift,
                             unsigned bitwidth);
void si_build_wrapper_function(struct si_shader_context *ctx, struct ac_llvm_pointer *parts,
                               unsigned num_parts, unsigned main_part,
                               unsigned next_shader_first_part,
                               enum ac_arg_type *main_arg_types,
                               bool same_thread_count);
bool si_llvm_compile_shader(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                            struct si_shader *shader, struct si_shader_args *args,
                            struct util_debug_callback *debug, struct nir_shader *nir);

/* si_shader_llvm_gs.c */
LLVMValueRef si_is_es_thread(struct si_shader_context *ctx);
LLVMValueRef si_is_gs_thread(struct si_shader_context *ctx);
void si_llvm_es_build_end(struct si_shader_context *ctx);
void si_preload_esgs_ring(struct si_shader_context *ctx);
void si_preload_gs_rings(struct si_shader_context *ctx);
void si_llvm_gs_build_end(struct si_shader_context *ctx);
void si_llvm_init_gs_callbacks(struct si_shader_context *ctx);

/* si_shader_llvm_tess.c */
LLVMValueRef si_get_rel_patch_id(struct si_shader_context *ctx);
void si_llvm_preload_tess_rings(struct si_shader_context *ctx);
void si_llvm_ls_build_end(struct si_shader_context *ctx);
void si_llvm_build_tcs_epilog(struct si_shader_context *ctx, union si_shader_part_key *key,
                              bool separate_epilog);
void si_llvm_tcs_build_end(struct si_shader_context *ctx);
void si_llvm_init_tcs_callbacks(struct si_shader_context *ctx);

/* si_shader_llvm_ps.c */
void si_llvm_build_ps_prolog(struct si_shader_context *ctx, union si_shader_part_key *key,
                             bool separate_prolog);
void si_llvm_build_ps_epilog(struct si_shader_context *ctx, union si_shader_part_key *key,
                             bool separate_epilog);
void si_llvm_build_monolithic_ps(struct si_shader_context *ctx, struct si_shader *shader);
void si_llvm_ps_build_end(struct si_shader_context *ctx);

/* si_shader_llvm_vs.c */
void si_llvm_build_vs_prolog(struct si_shader_context *ctx, union si_shader_part_key *key,
                             bool separate_prolog);

#endif
