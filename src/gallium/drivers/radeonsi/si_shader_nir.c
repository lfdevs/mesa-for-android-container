/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#include "si_shader.h"
#include "si_shader_internal.h"

#include "ac_nir_to_llvm.h"

#include "tgsi/tgsi_from_mesa.h"

#include "compiler/nir/nir.h"
#include "compiler/nir_types.h"


static int
type_size(const struct glsl_type *type)
{
   return glsl_count_attribute_slots(type, false);
}

static void scan_instruction(struct tgsi_shader_info *info,
			     nir_instr *instr)
{
	if (instr->type == nir_instr_type_alu) {
		nir_alu_instr *alu = nir_instr_as_alu(instr);

		switch (alu->op) {
		case nir_op_fddx:
		case nir_op_fddy:
		case nir_op_fddx_fine:
		case nir_op_fddy_fine:
		case nir_op_fddx_coarse:
		case nir_op_fddy_coarse:
			info->uses_derivatives = true;
			break;
		default:
			break;
		}
	} else if (instr->type == nir_instr_type_tex) {
		nir_tex_instr *tex = nir_instr_as_tex(instr);

		if (!tex->texture) {
			info->samplers_declared |=
				u_bit_consecutive(tex->sampler_index, 1);
		}

		switch (tex->op) {
		case nir_texop_tex:
		case nir_texop_txb:
		case nir_texop_lod:
			info->uses_derivatives = true;
			break;
		default:
			break;
		}
	} else if (instr->type == nir_instr_type_intrinsic) {
		nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

		switch (intr->intrinsic) {
		case nir_intrinsic_load_front_face:
			info->uses_frontface = 1;
			break;
		case nir_intrinsic_load_instance_id:
			info->uses_instanceid = 1;
			break;
		case nir_intrinsic_load_invocation_id:
			info->uses_invocationid = true;
			break;
		case nir_intrinsic_load_vertex_id:
			info->uses_vertexid = 1;
			break;
		case nir_intrinsic_load_vertex_id_zero_base:
			info->uses_vertexid_nobase = 1;
			break;
		case nir_intrinsic_load_base_vertex:
			info->uses_basevertex = 1;
			break;
		case nir_intrinsic_load_primitive_id:
			info->uses_primid = 1;
			break;
		case nir_intrinsic_load_sample_mask_in:
			info->reads_samplemask = true;
			break;
		case nir_intrinsic_load_tess_level_inner:
		case nir_intrinsic_load_tess_level_outer:
			info->reads_tess_factors = true;
			break;
		case nir_intrinsic_image_store:
		case nir_intrinsic_image_atomic_add:
		case nir_intrinsic_image_atomic_min:
		case nir_intrinsic_image_atomic_max:
		case nir_intrinsic_image_atomic_and:
		case nir_intrinsic_image_atomic_or:
		case nir_intrinsic_image_atomic_xor:
		case nir_intrinsic_image_atomic_exchange:
		case nir_intrinsic_image_atomic_comp_swap:
		case nir_intrinsic_store_ssbo:
		case nir_intrinsic_ssbo_atomic_add:
		case nir_intrinsic_ssbo_atomic_imin:
		case nir_intrinsic_ssbo_atomic_umin:
		case nir_intrinsic_ssbo_atomic_imax:
		case nir_intrinsic_ssbo_atomic_umax:
		case nir_intrinsic_ssbo_atomic_and:
		case nir_intrinsic_ssbo_atomic_or:
		case nir_intrinsic_ssbo_atomic_xor:
		case nir_intrinsic_ssbo_atomic_exchange:
		case nir_intrinsic_ssbo_atomic_comp_swap:
			info->writes_memory = true;
			break;
		default:
			break;
		}
	}
}

void si_nir_scan_tess_ctrl(const struct nir_shader *nir,
			   const struct tgsi_shader_info *info,
			   struct tgsi_tessctrl_info *out)
{
	memset(out, 0, sizeof(*out));

	if (nir->info.stage != MESA_SHADER_TESS_CTRL)
		return;

	/* Initial value = true. Here the pass will accumulate results from
	 * multiple segments surrounded by barriers. If tess factors aren't
	 * written at all, it's a shader bug and we don't care if this will be
	 * true.
	 */
	out->tessfactors_are_def_in_all_invocs = true;

	/* TODO: Implement scanning of tess factors, see tgsi backend. */
}

void si_nir_scan_shader(const struct nir_shader *nir,
			struct tgsi_shader_info *info)
{
	nir_function *func;
	unsigned i;

	assert(nir->info.stage == MESA_SHADER_VERTEX ||
	       nir->info.stage == MESA_SHADER_GEOMETRY ||
	       nir->info.stage == MESA_SHADER_TESS_CTRL ||
	       nir->info.stage == MESA_SHADER_TESS_EVAL ||
	       nir->info.stage == MESA_SHADER_FRAGMENT);

	info->processor = pipe_shader_type_from_mesa(nir->info.stage);
	info->num_tokens = 2; /* indicate that the shader is non-empty */
	info->num_instructions = 2;

	if (nir->info.stage == MESA_SHADER_TESS_CTRL) {
		info->properties[TGSI_PROPERTY_TCS_VERTICES_OUT] =
			nir->info.tess.tcs_vertices_out;
	}

	if (nir->info.stage == MESA_SHADER_TESS_EVAL) {
		if (nir->info.tess.primitive_mode == GL_ISOLINES)
			info->properties[TGSI_PROPERTY_TES_PRIM_MODE] = PIPE_PRIM_LINES;
		else
			info->properties[TGSI_PROPERTY_TES_PRIM_MODE] = nir->info.tess.primitive_mode;

		STATIC_ASSERT((TESS_SPACING_EQUAL + 1) % 3 == PIPE_TESS_SPACING_EQUAL);
		STATIC_ASSERT((TESS_SPACING_FRACTIONAL_ODD + 1) % 3 ==
			      PIPE_TESS_SPACING_FRACTIONAL_ODD);
		STATIC_ASSERT((TESS_SPACING_FRACTIONAL_EVEN + 1) % 3 ==
			      PIPE_TESS_SPACING_FRACTIONAL_EVEN);

		info->properties[TGSI_PROPERTY_TES_SPACING] = (nir->info.tess.spacing + 1) % 3;
		info->properties[TGSI_PROPERTY_TES_VERTEX_ORDER_CW] = !nir->info.tess.ccw;
		info->properties[TGSI_PROPERTY_TES_POINT_MODE] = nir->info.tess.point_mode;
	}

	if (nir->info.stage == MESA_SHADER_GEOMETRY) {
		info->properties[TGSI_PROPERTY_GS_INPUT_PRIM] = nir->info.gs.input_primitive;
		info->properties[TGSI_PROPERTY_GS_OUTPUT_PRIM] = nir->info.gs.output_primitive;
		info->properties[TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES] = nir->info.gs.vertices_out;
		info->properties[TGSI_PROPERTY_GS_INVOCATIONS] = nir->info.gs.invocations;
	}

	i = 0;
	uint64_t processed_inputs = 0;
	unsigned num_inputs = 0;
	nir_foreach_variable(variable, &nir->inputs) {
		unsigned semantic_name, semantic_index;
		unsigned attrib_count = glsl_count_attribute_slots(variable->type,
								   nir->info.stage == MESA_SHADER_VERTEX);

		/* Vertex shader inputs don't have semantics. The state
		 * tracker has already mapped them to attributes via
		 * variable->data.driver_location.
		 */
		if (nir->info.stage == MESA_SHADER_VERTEX)
			continue;

		assert(nir->info.stage != MESA_SHADER_FRAGMENT ||
		       (attrib_count == 1 && "not implemented"));

		/* Fragment shader position is a system value. */
		if (nir->info.stage == MESA_SHADER_FRAGMENT &&
		    variable->data.location == VARYING_SLOT_POS) {
			if (variable->data.pixel_center_integer)
				info->properties[TGSI_PROPERTY_FS_COORD_PIXEL_CENTER] =
					TGSI_FS_COORD_PIXEL_CENTER_INTEGER;

			num_inputs++;
			continue;
		}

		i = variable->data.driver_location;
		if (processed_inputs & ((uint64_t)1 << i))
			continue;

		processed_inputs |= ((uint64_t)1 << i);
		num_inputs++;

		tgsi_get_gl_varying_semantic(variable->data.location, true,
					     &semantic_name, &semantic_index);

		info->input_semantic_name[i] = semantic_name;
		info->input_semantic_index[i] = semantic_index;

		if (semantic_name == TGSI_SEMANTIC_PRIMID)
			info->uses_primid = true;

		if (variable->data.sample)
			info->input_interpolate_loc[i] = TGSI_INTERPOLATE_LOC_SAMPLE;
		else if (variable->data.centroid)
			info->input_interpolate_loc[i] = TGSI_INTERPOLATE_LOC_CENTROID;
		else
			info->input_interpolate_loc[i] = TGSI_INTERPOLATE_LOC_CENTER;

		enum glsl_base_type base_type =
			glsl_get_base_type(glsl_without_array(variable->type));

		switch (variable->data.interpolation) {
		case INTERP_MODE_NONE:
			if (glsl_base_type_is_integer(base_type)) {
				info->input_interpolate[i] = TGSI_INTERPOLATE_CONSTANT;
				break;
			}

			if (semantic_name == TGSI_SEMANTIC_COLOR) {
				info->input_interpolate[i] = TGSI_INTERPOLATE_COLOR;
				goto persp_locations;
			}
			/* fall-through */
		case INTERP_MODE_SMOOTH:
			assert(!glsl_base_type_is_integer(base_type));

			info->input_interpolate[i] = TGSI_INTERPOLATE_PERSPECTIVE;

		persp_locations:
			if (variable->data.sample)
				info->uses_persp_sample = true;
			else if (variable->data.centroid)
				info->uses_persp_centroid = true;
			else
				info->uses_persp_center = true;
			break;

		case INTERP_MODE_NOPERSPECTIVE:
			assert(!glsl_base_type_is_integer(base_type));

			info->input_interpolate[i] = TGSI_INTERPOLATE_LINEAR;

			if (variable->data.sample)
				info->uses_linear_sample = true;
			else if (variable->data.centroid)
				info->uses_linear_centroid = true;
			else
				info->uses_linear_center = true;
			break;

		case INTERP_MODE_FLAT:
			info->input_interpolate[i] = TGSI_INTERPOLATE_CONSTANT;
			break;
		}

		/* TODO make this more precise */
		if (variable->data.location == VARYING_SLOT_COL0)
			info->colors_read |= 0x0f;
		else if (variable->data.location == VARYING_SLOT_COL1)
			info->colors_read |= 0xf0;
	}

	if (nir->info.stage != MESA_SHADER_VERTEX)
		info->num_inputs = num_inputs;
	else
		info->num_inputs = nir->num_inputs;

	i = 0;
	uint64_t processed_outputs = 0;
	unsigned num_outputs = 0;
	nir_foreach_variable(variable, &nir->outputs) {
		unsigned semantic_name, semantic_index;

		if (nir->info.stage == MESA_SHADER_FRAGMENT) {
			tgsi_get_gl_frag_result_semantic(variable->data.location,
				&semantic_name, &semantic_index);

			/* Adjust for dual source blending */
			if (variable->data.index > 0) {
				semantic_index++;
			}
		} else {
			tgsi_get_gl_varying_semantic(variable->data.location, true,
						     &semantic_name, &semantic_index);
		}

		i = variable->data.driver_location;
		if (processed_outputs & ((uint64_t)1 << i))
			continue;

		processed_outputs |= ((uint64_t)1 << i);
		num_outputs++;

		info->output_semantic_name[i] = semantic_name;
		info->output_semantic_index[i] = semantic_index;
		info->output_usagemask[i] = TGSI_WRITEMASK_XYZW;

		unsigned num_components = 4;
		unsigned vector_elements = glsl_get_vector_elements(glsl_without_array(variable->type));
		if (vector_elements)
			num_components = vector_elements;

		unsigned gs_out_streams;
		if (variable->data.stream & (1u << 31)) {
			gs_out_streams = variable->data.stream & ~(1u << 31);
		} else {
			assert(variable->data.stream < 4);
			gs_out_streams = 0;
			for (unsigned j = 0; j < num_components; ++j)
				gs_out_streams |= variable->data.stream << (2 * (variable->data.location_frac + j));
		}

		unsigned streamx = gs_out_streams & 3;
		unsigned streamy = (gs_out_streams >> 2) & 3;
		unsigned streamz = (gs_out_streams >> 4) & 3;
		unsigned streamw = (gs_out_streams >> 6) & 3;

		if (info->output_usagemask[i] & TGSI_WRITEMASK_X) {
			info->output_streams[i] |= streamx;
			info->num_stream_output_components[streamx]++;
		}
		if (info->output_usagemask[i] & TGSI_WRITEMASK_Y) {
			info->output_streams[i] |= streamy << 2;
			info->num_stream_output_components[streamy]++;
		}
		if (info->output_usagemask[i] & TGSI_WRITEMASK_Z) {
			info->output_streams[i] |= streamz << 4;
			info->num_stream_output_components[streamz]++;
		}
		if (info->output_usagemask[i] & TGSI_WRITEMASK_W) {
			info->output_streams[i] |= streamw << 6;
			info->num_stream_output_components[streamw]++;
		}

		switch (semantic_name) {
		case TGSI_SEMANTIC_PRIMID:
			info->writes_primid = true;
			break;
		case TGSI_SEMANTIC_VIEWPORT_INDEX:
			info->writes_viewport_index = true;
			break;
		case TGSI_SEMANTIC_LAYER:
			info->writes_layer = true;
			break;
		case TGSI_SEMANTIC_PSIZE:
			info->writes_psize = true;
			break;
		case TGSI_SEMANTIC_CLIPVERTEX:
			info->writes_clipvertex = true;
			break;
		case TGSI_SEMANTIC_COLOR:
			info->colors_written |= 1 << semantic_index;
			break;
		case TGSI_SEMANTIC_STENCIL:
			info->writes_stencil = true;
			break;
		case TGSI_SEMANTIC_SAMPLEMASK:
			info->writes_samplemask = true;
			break;
		case TGSI_SEMANTIC_EDGEFLAG:
			info->writes_edgeflag = true;
			break;
		case TGSI_SEMANTIC_POSITION:
			if (info->processor == PIPE_SHADER_FRAGMENT)
				info->writes_z = true;
			else
				info->writes_position = true;
			break;
		}

		if (nir->info.stage == MESA_SHADER_TESS_CTRL) {
			switch (semantic_name) {
			case TGSI_SEMANTIC_PATCH:
				info->reads_perpatch_outputs = true;
			break;
			case TGSI_SEMANTIC_TESSINNER:
			case TGSI_SEMANTIC_TESSOUTER:
				info->reads_tessfactor_outputs = true;
			break;
			default:
				info->reads_pervertex_outputs = true;
			}
		}
	}

	info->num_outputs = num_outputs;

	nir_foreach_variable(variable, &nir->uniforms) {
		const struct glsl_type *type = variable->type;
		enum glsl_base_type base_type =
			glsl_get_base_type(glsl_without_array(type));
		unsigned aoa_size = MAX2(1, glsl_get_aoa_size(type));

		/* We rely on the fact that nir_lower_samplers_as_deref has
		 * eliminated struct dereferences.
		 */
		if (base_type == GLSL_TYPE_SAMPLER)
			info->samplers_declared |=
				u_bit_consecutive(variable->data.binding, aoa_size);
		else if (base_type == GLSL_TYPE_IMAGE)
			info->images_declared |=
				u_bit_consecutive(variable->data.binding, aoa_size);
	}

	info->num_written_clipdistance = nir->info.clip_distance_array_size;
	info->num_written_culldistance = nir->info.cull_distance_array_size;
	info->clipdist_writemask = u_bit_consecutive(0, info->num_written_clipdistance);
	info->culldist_writemask = u_bit_consecutive(0, info->num_written_culldistance);

	if (info->processor == PIPE_SHADER_FRAGMENT)
		info->uses_kill = nir->info.fs.uses_discard;

	/* TODO make this more accurate */
	info->const_buffers_declared = u_bit_consecutive(0, SI_NUM_CONST_BUFFERS);
	info->shader_buffers_declared = u_bit_consecutive(0, SI_NUM_SHADER_BUFFERS);

	func = (struct nir_function *)exec_list_get_head_const(&nir->functions);
	nir_foreach_block(block, func->impl) {
		nir_foreach_instr(instr, block)
			scan_instruction(info, instr);
	}
}

/**
 * Perform "lowering" operations on the NIR that are run once when the shader
 * selector is created.
 */
void
si_lower_nir(struct si_shader_selector* sel)
{
	/* Adjust the driver location of inputs and outputs. The state tracker
	 * interprets them as slots, while the ac/nir backend interprets them
	 * as individual components.
	 */
	nir_foreach_variable(variable, &sel->nir->inputs)
		variable->data.driver_location *= 4;

	nir_foreach_variable(variable, &sel->nir->outputs) {
		variable->data.driver_location *= 4;

		if (sel->nir->info.stage == MESA_SHADER_FRAGMENT) {
			if (variable->data.location == FRAG_RESULT_DEPTH)
				variable->data.driver_location += 2;
			else if (variable->data.location == FRAG_RESULT_STENCIL)
				variable->data.driver_location += 1;
		}
	}

	/* Perform lowerings (and optimizations) of code.
	 *
	 * Performance considerations aside, we must:
	 * - lower certain ALU operations
	 * - ensure constant offsets for texture instructions are folded
	 *   and copy-propagated
	 */
	NIR_PASS_V(sel->nir, nir_lower_io, nir_var_uniform, type_size,
		   (nir_lower_io_options)0);
	NIR_PASS_V(sel->nir, nir_lower_uniforms_to_ubo);

	NIR_PASS_V(sel->nir, nir_lower_returns);
	NIR_PASS_V(sel->nir, nir_lower_vars_to_ssa);
	NIR_PASS_V(sel->nir, nir_lower_alu_to_scalar);
	NIR_PASS_V(sel->nir, nir_lower_phis_to_scalar);

	static const struct nir_lower_tex_options lower_tex_options = {
		.lower_txp = ~0u,
	};
	NIR_PASS_V(sel->nir, nir_lower_tex, &lower_tex_options);

	const nir_lower_subgroups_options subgroups_options = {
		.subgroup_size = 64,
		.ballot_bit_size = 32,
		.lower_to_scalar = true,
		.lower_subgroup_masks = true,
		.lower_vote_trivial = false,
	};
	NIR_PASS_V(sel->nir, nir_lower_subgroups, &subgroups_options);

	bool progress;
	do {
		progress = false;

		/* (Constant) copy propagation is needed for txf with offsets. */
		NIR_PASS(progress, sel->nir, nir_copy_prop);
		NIR_PASS(progress, sel->nir, nir_opt_remove_phis);
		NIR_PASS(progress, sel->nir, nir_opt_dce);
		if (nir_opt_trivial_continues(sel->nir)) {
			progress = true;
			NIR_PASS(progress, sel->nir, nir_copy_prop);
			NIR_PASS(progress, sel->nir, nir_opt_dce);
		}
		NIR_PASS(progress, sel->nir, nir_opt_if);
		NIR_PASS(progress, sel->nir, nir_opt_dead_cf);
		NIR_PASS(progress, sel->nir, nir_opt_cse);
		NIR_PASS(progress, sel->nir, nir_opt_peephole_select, 8);

		/* Needed for algebraic lowering */
		NIR_PASS(progress, sel->nir, nir_opt_algebraic);
		NIR_PASS(progress, sel->nir, nir_opt_constant_folding);

		NIR_PASS(progress, sel->nir, nir_opt_undef);
		NIR_PASS(progress, sel->nir, nir_opt_conditional_discard);
		if (sel->nir->options->max_unroll_iterations) {
			NIR_PASS(progress, sel->nir, nir_opt_loop_unroll, 0);
		}
	} while (progress);
}

static void declare_nir_input_vs(struct si_shader_context *ctx,
				 struct nir_variable *variable,
				 LLVMValueRef out[4])
{
	si_llvm_load_input_vs(ctx, variable->data.driver_location / 4, out);
}

static void declare_nir_input_fs(struct si_shader_context *ctx,
				 struct nir_variable *variable,
				 unsigned input_index,
				 LLVMValueRef out[4])
{
	unsigned slot = variable->data.location;
	if (slot == VARYING_SLOT_POS) {
		out[0] = LLVMGetParam(ctx->main_fn, SI_PARAM_POS_X_FLOAT);
		out[1] = LLVMGetParam(ctx->main_fn, SI_PARAM_POS_Y_FLOAT);
		out[2] = LLVMGetParam(ctx->main_fn, SI_PARAM_POS_Z_FLOAT);
		out[3] = ac_build_fdiv(&ctx->ac, ctx->ac.f32_1,
				LLVMGetParam(ctx->main_fn, SI_PARAM_POS_W_FLOAT));
		return;
	}

	si_llvm_load_input_fs(ctx, input_index, out);
}

LLVMValueRef si_nir_load_input_gs(struct ac_shader_abi *abi,
				  unsigned location,
				  unsigned driver_location,
				  unsigned component,
				  unsigned num_components,
				  unsigned vertex_index,
				  unsigned const_index,
				  LLVMTypeRef type)
{
	struct si_shader_context *ctx = si_shader_context_from_abi(abi);

	LLVMValueRef value[4];
	for (unsigned i = component; i < num_components + component; i++) {
		value[i] = si_llvm_load_input_gs(&ctx->abi, driver_location  / 4,
						 vertex_index, type, i);
	}

	return ac_build_varying_gather_values(&ctx->ac, value, num_components, component);
}

static LLVMValueRef
si_nir_load_sampler_desc(struct ac_shader_abi *abi,
		         unsigned descriptor_set, unsigned base_index,
		         unsigned constant_index, LLVMValueRef dynamic_index,
		         enum ac_descriptor_type desc_type, bool image,
			 bool write)
{
	struct si_shader_context *ctx = si_shader_context_from_abi(abi);
	LLVMBuilderRef builder = ctx->ac.builder;
	LLVMValueRef list = LLVMGetParam(ctx->main_fn, ctx->param_samplers_and_images);
	LLVMValueRef index = dynamic_index;

	assert(!descriptor_set);

	if (!index)
		index = ctx->ac.i32_0;

	index = LLVMBuildAdd(builder, index,
			     LLVMConstInt(ctx->ac.i32, base_index + constant_index, false),
			     "");

	if (image) {
		assert(desc_type == AC_DESC_IMAGE || desc_type == AC_DESC_BUFFER);
		assert(base_index + constant_index < ctx->num_images);

		if (dynamic_index)
			index = si_llvm_bound_index(ctx, index, ctx->num_images);

		index = LLVMBuildSub(ctx->gallivm.builder,
				     LLVMConstInt(ctx->i32, SI_NUM_IMAGES - 1, 0),
				     index, "");

		/* TODO: be smarter about when we use dcc_off */
		return si_load_image_desc(ctx, list, index, desc_type, write);
	}

	assert(base_index + constant_index < ctx->num_samplers);

	if (dynamic_index)
		index = si_llvm_bound_index(ctx, index, ctx->num_samplers);

	index = LLVMBuildAdd(ctx->gallivm.builder, index,
			     LLVMConstInt(ctx->i32, SI_NUM_IMAGES / 2, 0), "");

	return si_load_sampler_desc(ctx, list, index, desc_type);
}

bool si_nir_build_llvm(struct si_shader_context *ctx, struct nir_shader *nir)
{
	struct tgsi_shader_info *info = &ctx->shader->selector->info;

	if (nir->info.stage == MESA_SHADER_VERTEX ||
	    nir->info.stage == MESA_SHADER_FRAGMENT) {
		uint64_t processed_inputs = 0;
		nir_foreach_variable(variable, &nir->inputs) {
			unsigned attrib_count = glsl_count_attribute_slots(variable->type,
									   nir->info.stage == MESA_SHADER_VERTEX);
			unsigned input_idx = variable->data.driver_location;

			assert(attrib_count == 1);

			LLVMValueRef data[4];
			unsigned loc = variable->data.location;

			/* Packed components share the same location so skip
			 * them if we have already processed the location.
			 */
			if (processed_inputs & ((uint64_t)1 << loc))
				continue;

			if (nir->info.stage == MESA_SHADER_VERTEX)
				declare_nir_input_vs(ctx, variable, data);
			else if (nir->info.stage == MESA_SHADER_FRAGMENT)
				declare_nir_input_fs(ctx, variable, input_idx / 4, data);

			for (unsigned chan = 0; chan < 4; chan++) {
				ctx->inputs[input_idx + chan] =
					LLVMBuildBitCast(ctx->ac.builder, data[chan], ctx->ac.i32, "");
			}
			processed_inputs |= ((uint64_t)1 << loc);
		}
	}

	ctx->abi.inputs = &ctx->inputs[0];
	ctx->abi.load_sampler_desc = si_nir_load_sampler_desc;
	ctx->abi.clamp_shadow_reference = true;

	ctx->num_samplers = util_last_bit(info->samplers_declared);
	ctx->num_images = util_last_bit(info->images_declared);

	ac_nir_translate(&ctx->ac, &ctx->abi, nir, NULL);

	return true;
}
