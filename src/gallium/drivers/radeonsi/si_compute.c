/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 *
 */

#include "util/u_memory.h"
#include "radeon/r600_pipe_common.h"
#include "radeon/radeon_elf_util.h"
#include "radeon/radeon_llvm_util.h"

#include "radeon/r600_cs.h"
#include "si_pipe.h"
#include "si_shader.h"
#include "sid.h"

#define MAX_GLOBAL_BUFFERS 20

struct si_compute {
	struct si_context *ctx;

	unsigned local_size;
	unsigned private_size;
	unsigned input_size;
	struct si_shader shader;
	unsigned num_user_sgprs;

	struct r600_resource *input_buffer;
	struct pipe_resource *global_buffers[MAX_GLOBAL_BUFFERS];
};

static void init_scratch_buffer(struct si_context *sctx, struct si_compute *program)
{
	unsigned scratch_bytes = 0;
	uint64_t scratch_buffer_va;
	unsigned i;

	/* Compute the scratch buffer size using the maximum number of waves.
	 * This way we don't need to recompute it for each kernel launch. */
	unsigned scratch_waves = 32 * sctx->screen->b.info.num_good_compute_units;
	for (i = 0; i < program->shader.binary.global_symbol_count; i++) {
		unsigned offset =
				program->shader.binary.global_symbol_offsets[i];
		unsigned scratch_bytes_needed;

		si_shader_binary_read_config(&program->shader.binary,
					     &program->shader.config, offset);
		scratch_bytes_needed = program->shader.config.scratch_bytes_per_wave;
		scratch_bytes = MAX2(scratch_bytes, scratch_bytes_needed);
	}

	if (scratch_bytes == 0)
		return;

	program->shader.scratch_bo =
				si_resource_create_custom(sctx->b.b.screen,
				PIPE_USAGE_DEFAULT,
				scratch_bytes * scratch_waves);

	scratch_buffer_va = program->shader.scratch_bo->gpu_address;

	/* apply_scratch_relocs needs scratch_bytes_per_wave to be set
	 * to the maximum bytes needed, so it can compute the stride
	 * correctly.
	 */
	program->shader.config.scratch_bytes_per_wave = scratch_bytes;

	/* Patch the shader with the scratch buffer address. */
	si_shader_apply_scratch_relocs(sctx,
				&program->shader, scratch_buffer_va);
}

static void *si_create_compute_state(
	struct pipe_context *ctx,
	const struct pipe_compute_state *cso)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_compute *program = CALLOC_STRUCT(si_compute);
	const struct pipe_llvm_program_header *header;
	const char *code;

	header = cso->prog;
	code = cso->prog + sizeof(struct pipe_llvm_program_header);

	program->ctx = sctx;
	program->local_size = cso->req_local_mem;
	program->private_size = cso->req_private_mem;
	program->input_size = cso->req_input_mem;

	radeon_elf_read(code, header->num_bytes, &program->shader.binary);

	/* init_scratch_buffer patches the shader code with the scratch address,
	 * so we need to call it before si_shader_binary_read() which uploads
	 * the shader code to the GPU.
	 */
	init_scratch_buffer(sctx, program);
	si_shader_binary_read_config(&program->shader.binary,
				     &program->shader.config, 0);
	si_shader_dump(sctx->screen, &program->shader, &sctx->b.debug,
		       TGSI_PROCESSOR_COMPUTE);
	si_shader_binary_upload(sctx->screen, &program->shader);

	program->input_buffer =	si_resource_create_custom(sctx->b.b.screen,
		PIPE_USAGE_IMMUTABLE, program->input_size);

	return program;
}

static void si_bind_compute_state(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context*)ctx;
	sctx->cs_shader_state.program = (struct si_compute*)state;
}

static void si_set_global_binding(
	struct pipe_context *ctx, unsigned first, unsigned n,
	struct pipe_resource **resources,
	uint32_t **handles)
{
	unsigned i;
	struct si_context *sctx = (struct si_context*)ctx;
	struct si_compute *program = sctx->cs_shader_state.program;

	if (!resources) {
		for (i = first; i < first + n; i++) {
			pipe_resource_reference(&program->global_buffers[i], NULL);
		}
		return;
	}

	for (i = first; i < first + n; i++) {
		uint64_t va;
		uint32_t offset;
		pipe_resource_reference(&program->global_buffers[i], resources[i]);
		va = r600_resource(resources[i])->gpu_address;
		offset = util_le32_to_cpu(*handles[i]);
		va += offset;
		va = util_cpu_to_le64(va);
		memcpy(handles[i], &va, sizeof(va));
	}
}

/**
 * This function computes the value for R_00B860_COMPUTE_TMPRING_SIZE.WAVES
 * /p block_layout is the number of threads in each work group.
 * /p grid layout is the number of work groups.
 */
static unsigned compute_num_waves_for_scratch(
		const struct radeon_info *info,
		const uint *block_layout,
		const uint *grid_layout)
{
	unsigned num_sh = MAX2(info->max_sh_per_se, 1);
	unsigned num_se = MAX2(info->max_se, 1);
	unsigned num_blocks = 1;
	unsigned threads_per_block = 1;
	unsigned waves_per_block;
	unsigned waves_per_sh;
	unsigned waves;
	unsigned scratch_waves;
	unsigned i;

	for (i = 0; i < 3; i++) {
		threads_per_block *= block_layout[i];
		num_blocks *= grid_layout[i];
	}

	waves_per_block = align(threads_per_block, 64) / 64;
	waves = waves_per_block * num_blocks;
	waves_per_sh = align(waves, num_sh * num_se) / (num_sh * num_se);
	scratch_waves = waves_per_sh * num_sh * num_se;

	if (waves_per_block > waves_per_sh) {
		scratch_waves = waves_per_block * num_sh * num_se;
	}

	return scratch_waves;
}

static void si_launch_grid(
		struct pipe_context *ctx, const struct pipe_grid_info *info)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct radeon_winsys_cs *cs = sctx->b.gfx.cs;
	struct si_compute *program = sctx->cs_shader_state.program;
	struct si_pm4_state *pm4 = CALLOC_STRUCT(si_pm4_state);
	struct r600_resource *input_buffer = program->input_buffer;
	unsigned kernel_args_size;
	unsigned num_work_size_bytes = 36;
	uint32_t kernel_args_offset = 0;
	uint32_t *kernel_args;
	uint64_t kernel_args_va;
	uint64_t scratch_buffer_va = 0;
	uint64_t shader_va;
	unsigned i;
	struct si_shader *shader = &program->shader;
	unsigned lds_blocks;
	unsigned num_waves_for_scratch;

	radeon_emit(cs, PKT3(PKT3_CONTEXT_CONTROL, 1, 0) | PKT3_SHADER_TYPE_S(1));
	radeon_emit(cs, 0x80000000);
	radeon_emit(cs, 0x80000000);

	sctx->b.flags |= SI_CONTEXT_INV_VMEM_L1 |
			 SI_CONTEXT_INV_GLOBAL_L2 |
			 SI_CONTEXT_INV_ICACHE |
			 SI_CONTEXT_INV_SMEM_L1 |
			 SI_CONTEXT_FLUSH_WITH_INV_L2 |
			 SI_CONTEXT_FLAG_COMPUTE;
	si_emit_cache_flush(sctx, NULL);

	pm4->compute_pkt = true;

	/* Read the config information */
	si_shader_binary_read_config(&shader->binary, &shader->config, info->pc);

	/* Upload the kernel arguments */

	/* The extra num_work_size_bytes are for work group / work item size information */
	kernel_args_size = program->input_size + num_work_size_bytes + 8 /* For scratch va */;

	kernel_args = sctx->b.ws->buffer_map(input_buffer->buf,
			sctx->b.gfx.cs, PIPE_TRANSFER_WRITE);
	for (i = 0; i < 3; i++) {
		kernel_args[i] = info->grid[i];
		kernel_args[i + 3] = info->grid[i] * info->block[i];
		kernel_args[i + 6] = info->block[i];
	}

	num_waves_for_scratch =	compute_num_waves_for_scratch(
		&sctx->screen->b.info, info->block, info->grid);

	memcpy(kernel_args + (num_work_size_bytes / 4), info->input,
          program->input_size);

	if (shader->config.scratch_bytes_per_wave > 0) {

		COMPUTE_DBG(sctx->screen, "Waves: %u; Scratch per wave: %u bytes; "
		            "Total Scratch: %u bytes\n", num_waves_for_scratch,
			    shader->config.scratch_bytes_per_wave,
			    shader->config.scratch_bytes_per_wave *
			    num_waves_for_scratch);

		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
					  shader->scratch_bo,
					  RADEON_USAGE_READWRITE,
					  RADEON_PRIO_SCRATCH_BUFFER);

		scratch_buffer_va = shader->scratch_bo->gpu_address;
	}

	for (i = 0; i < (kernel_args_size / 4); i++) {
		COMPUTE_DBG(sctx->screen, "input %u : %u\n", i,
			kernel_args[i]);
	}

	kernel_args_va = input_buffer->gpu_address;
	kernel_args_va += kernel_args_offset;

	radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx, input_buffer,
				  RADEON_USAGE_READ, RADEON_PRIO_CONST_BUFFER);

	si_pm4_set_reg(pm4, R_00B900_COMPUTE_USER_DATA_0, kernel_args_va);
	si_pm4_set_reg(pm4, R_00B900_COMPUTE_USER_DATA_0 + 4, S_008F04_BASE_ADDRESS_HI (kernel_args_va >> 32) | S_008F04_STRIDE(0));
	si_pm4_set_reg(pm4, R_00B900_COMPUTE_USER_DATA_0 + 8, scratch_buffer_va);
	si_pm4_set_reg(pm4, R_00B900_COMPUTE_USER_DATA_0 + 12,
		S_008F04_BASE_ADDRESS_HI(scratch_buffer_va >> 32)
		|  S_008F04_STRIDE(shader->config.scratch_bytes_per_wave / 64));

	si_pm4_set_reg(pm4, R_00B810_COMPUTE_START_X, 0);
	si_pm4_set_reg(pm4, R_00B814_COMPUTE_START_Y, 0);
	si_pm4_set_reg(pm4, R_00B818_COMPUTE_START_Z, 0);

	si_pm4_set_reg(pm4, R_00B81C_COMPUTE_NUM_THREAD_X,
				S_00B81C_NUM_THREAD_FULL(info->block[0]));
	si_pm4_set_reg(pm4, R_00B820_COMPUTE_NUM_THREAD_Y,
				S_00B820_NUM_THREAD_FULL(info->block[1]));
	si_pm4_set_reg(pm4, R_00B824_COMPUTE_NUM_THREAD_Z,
				S_00B824_NUM_THREAD_FULL(info->block[2]));

	/* Global buffers */
	for (i = 0; i < MAX_GLOBAL_BUFFERS; i++) {
		struct r600_resource *buffer =
				(struct r600_resource*)program->global_buffers[i];
		if (!buffer) {
			continue;
		}
		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx, buffer,
					  RADEON_USAGE_READWRITE,
					  RADEON_PRIO_COMPUTE_GLOBAL);
	}

	/* This register has been moved to R_00CD20_COMPUTE_MAX_WAVE_ID
	 * and is now per pipe, so it should be handled in the
	 * kernel if we want to use something other than the default value,
	 * which is now 0x22f.
	 */
	if (sctx->b.chip_class <= SI) {
		/* XXX: This should be:
		 * (number of compute units) * 4 * (waves per simd) - 1 */

		si_pm4_set_reg(pm4, R_00B82C_COMPUTE_MAX_WAVE_ID,
						0x190 /* Default value */);
	}

	shader_va = shader->bo->gpu_address;
	shader_va += info->pc;

	radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx, shader->bo,
				  RADEON_USAGE_READ, RADEON_PRIO_USER_SHADER);
	si_pm4_set_reg(pm4, R_00B830_COMPUTE_PGM_LO, shader_va >> 8);
	si_pm4_set_reg(pm4, R_00B834_COMPUTE_PGM_HI, shader_va >> 40);

	si_pm4_set_reg(pm4, R_00B848_COMPUTE_PGM_RSRC1, shader->config.rsrc1);

	lds_blocks = shader->config.lds_size;
	/* XXX: We are over allocating LDS.  For SI, the shader reports LDS in
	 * blocks of 256 bytes, so if there are 4 bytes lds allocated in
	 * the shader and 4 bytes allocated by the state tracker, then
	 * we will set LDS_SIZE to 512 bytes rather than 256.
	 */
	if (sctx->b.chip_class <= SI) {
		lds_blocks += align(program->local_size, 256) >> 8;
	} else {
		lds_blocks += align(program->local_size, 512) >> 9;
	}

	assert(lds_blocks <= 0xFF);

	shader->config.rsrc2 &= C_00B84C_LDS_SIZE;
	shader->config.rsrc2 |=  S_00B84C_LDS_SIZE(lds_blocks);

	si_pm4_set_reg(pm4, R_00B84C_COMPUTE_PGM_RSRC2, shader->config.rsrc2);
	si_pm4_set_reg(pm4, R_00B854_COMPUTE_RESOURCE_LIMITS, 0);

	si_pm4_set_reg(pm4, R_00B858_COMPUTE_STATIC_THREAD_MGMT_SE0,
		S_00B858_SH0_CU_EN(0xffff /* Default value */)
		| S_00B858_SH1_CU_EN(0xffff /* Default value */))
		;

	si_pm4_set_reg(pm4, R_00B85C_COMPUTE_STATIC_THREAD_MGMT_SE1,
		S_00B85C_SH0_CU_EN(0xffff /* Default value */)
		| S_00B85C_SH1_CU_EN(0xffff /* Default value */))
		;

	num_waves_for_scratch =
		MIN2(num_waves_for_scratch,
		     32 * sctx->screen->b.info.num_good_compute_units);
	si_pm4_set_reg(pm4, R_00B860_COMPUTE_TMPRING_SIZE,
		/* The maximum value for WAVES is 32 * num CU.
		 * If you program this value incorrectly, the GPU will hang if
		 * COMPUTE_PGM_RSRC2.SCRATCH_EN is enabled.
		 */
		S_00B860_WAVES(num_waves_for_scratch)
		| S_00B860_WAVESIZE(shader->config.scratch_bytes_per_wave >> 10))
		;

	si_pm4_cmd_begin(pm4, PKT3_DISPATCH_DIRECT);
	si_pm4_cmd_add(pm4, info->grid[0]); /* Thread groups DIM_X */
	si_pm4_cmd_add(pm4, info->grid[1]); /* Thread groups DIM_Y */
	si_pm4_cmd_add(pm4, info->grid[2]); /* Thread gropus DIM_Z */
	si_pm4_cmd_add(pm4, 1); /* DISPATCH_INITIATOR */
        si_pm4_cmd_end(pm4, false);

	si_pm4_emit(sctx, pm4);

#if 0
	fprintf(stderr, "cdw: %i\n", sctx->cs->cdw);
	for (i = 0; i < sctx->cs->cdw; i++) {
		fprintf(stderr, "%4i : 0x%08X\n", i, sctx->cs->buf[i]);
	}
#endif

	si_pm4_free_state(sctx, pm4, ~0);

	sctx->b.flags |= SI_CONTEXT_CS_PARTIAL_FLUSH |
			 SI_CONTEXT_INV_VMEM_L1 |
			 SI_CONTEXT_INV_GLOBAL_L2 |
			 SI_CONTEXT_INV_ICACHE |
			 SI_CONTEXT_INV_SMEM_L1 |
			 SI_CONTEXT_FLAG_COMPUTE;
	si_emit_cache_flush(sctx, NULL);
}


static void si_delete_compute_state(struct pipe_context *ctx, void* state){
	struct si_compute *program = (struct si_compute *)state;

	if (!state) {
		return;
	}

	si_shader_destroy(&program->shader);
	pipe_resource_reference(
		(struct pipe_resource **)&program->input_buffer, NULL);
	FREE(program);
}

static void si_set_compute_resources(struct pipe_context * ctx_,
		unsigned start, unsigned count,
		struct pipe_surface ** surfaces) { }

void si_init_compute_functions(struct si_context *sctx)
{
	sctx->b.b.create_compute_state = si_create_compute_state;
	sctx->b.b.delete_compute_state = si_delete_compute_state;
	sctx->b.b.bind_compute_state = si_bind_compute_state;
/*	 ctx->context.create_sampler_view = evergreen_compute_create_sampler_view; */
	sctx->b.b.set_compute_resources = si_set_compute_resources;
	sctx->b.b.set_global_binding = si_set_global_binding;
	sctx->b.b.launch_grid = si_launch_grid;
}
