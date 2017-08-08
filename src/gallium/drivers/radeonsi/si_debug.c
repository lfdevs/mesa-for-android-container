/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 * Authors:
 *      Marek Olšák <maraeo@gmail.com>
 */

#include "si_pipe.h"
#include "si_compute.h"
#include "sid.h"
#include "gfx9d.h"
#include "sid_tables.h"
#include "ddebug/dd_util.h"
#include "util/u_memory.h"
#include "ac_debug.h"

DEBUG_GET_ONCE_OPTION(replace_shaders, "RADEON_REPLACE_SHADERS", NULL)

static void si_dump_shader(struct si_screen *sscreen,
			   enum pipe_shader_type processor,
			   const struct si_shader *shader, FILE *f)
{
	if (shader->shader_log)
		fwrite(shader->shader_log, shader->shader_log_size, 1, f);
	else
		si_shader_dump(sscreen, shader, NULL, processor, f, false);
}

static void si_dump_gfx_shader(struct si_screen *sscreen,
			       const struct si_shader_ctx_state *state, FILE *f)
{
	const struct si_shader *current = state->current;

	if (!state->cso || !current)
		return;

	si_dump_shader(sscreen, state->cso->info.processor, current, f);
}

static void si_dump_compute_shader(struct si_screen *sscreen,
				   const struct si_cs_shader_state *state, FILE *f)
{
	if (!state->program || state->program != state->emitted_program)
		return;

	si_dump_shader(sscreen, PIPE_SHADER_COMPUTE, &state->program->shader, f);
}

/**
 * Shader compiles can be overridden with arbitrary ELF objects by setting
 * the environment variable RADEON_REPLACE_SHADERS=num1:filename1[;num2:filename2]
 */
bool si_replace_shader(unsigned num, struct ac_shader_binary *binary)
{
	const char *p = debug_get_option_replace_shaders();
	const char *semicolon;
	char *copy = NULL;
	FILE *f;
	long filesize, nread;
	char *buf = NULL;
	bool replaced = false;

	if (!p)
		return false;

	while (*p) {
		unsigned long i;
		char *endp;
		i = strtoul(p, &endp, 0);

		p = endp;
		if (*p != ':') {
			fprintf(stderr, "RADEON_REPLACE_SHADERS formatted badly.\n");
			exit(1);
		}
		++p;

		if (i == num)
			break;

		p = strchr(p, ';');
		if (!p)
			return false;
		++p;
	}
	if (!*p)
		return false;

	semicolon = strchr(p, ';');
	if (semicolon) {
		p = copy = strndup(p, semicolon - p);
		if (!copy) {
			fprintf(stderr, "out of memory\n");
			return false;
		}
	}

	fprintf(stderr, "radeonsi: replace shader %u by %s\n", num, p);

	f = fopen(p, "r");
	if (!f) {
		perror("radeonsi: failed to open file");
		goto out_free;
	}

	if (fseek(f, 0, SEEK_END) != 0)
		goto file_error;

	filesize = ftell(f);
	if (filesize < 0)
		goto file_error;

	if (fseek(f, 0, SEEK_SET) != 0)
		goto file_error;

	buf = MALLOC(filesize);
	if (!buf) {
		fprintf(stderr, "out of memory\n");
		goto out_close;
	}

	nread = fread(buf, 1, filesize, f);
	if (nread != filesize)
		goto file_error;

	ac_elf_read(buf, filesize, binary);
	replaced = true;

out_close:
	fclose(f);
out_free:
	FREE(buf);
	free(copy);
	return replaced;

file_error:
	perror("radeonsi: reading shader");
	goto out_close;
}

/* Parsed IBs are difficult to read without colors. Use "less -R file" to
 * read them, or use "aha -b -f file" to convert them to html.
 */
#define COLOR_RESET	"\033[0m"
#define COLOR_RED	"\033[31m"
#define COLOR_GREEN	"\033[1;32m"
#define COLOR_YELLOW	"\033[1;33m"
#define COLOR_CYAN	"\033[1;36m"

static void si_dump_mmapped_reg(struct si_context *sctx, FILE *f,
				unsigned offset)
{
	struct radeon_winsys *ws = sctx->b.ws;
	uint32_t value;

	if (ws->read_registers(ws, offset, 1, &value))
		ac_dump_reg(f, offset, value, ~0);
}

static void si_dump_debug_registers(struct si_context *sctx, FILE *f)
{
	if (sctx->screen->b.info.drm_major == 2 &&
	    sctx->screen->b.info.drm_minor < 42)
		return; /* no radeon support */

	fprintf(f, "Memory-mapped registers:\n");
	si_dump_mmapped_reg(sctx, f, R_008010_GRBM_STATUS);

	/* No other registers can be read on DRM < 3.1.0. */
	if (sctx->screen->b.info.drm_major < 3 ||
	    sctx->screen->b.info.drm_minor < 1) {
		fprintf(f, "\n");
		return;
	}

	si_dump_mmapped_reg(sctx, f, R_008008_GRBM_STATUS2);
	si_dump_mmapped_reg(sctx, f, R_008014_GRBM_STATUS_SE0);
	si_dump_mmapped_reg(sctx, f, R_008018_GRBM_STATUS_SE1);
	si_dump_mmapped_reg(sctx, f, R_008038_GRBM_STATUS_SE2);
	si_dump_mmapped_reg(sctx, f, R_00803C_GRBM_STATUS_SE3);
	si_dump_mmapped_reg(sctx, f, R_00D034_SDMA0_STATUS_REG);
	si_dump_mmapped_reg(sctx, f, R_00D834_SDMA1_STATUS_REG);
	if (sctx->b.chip_class <= VI) {
		si_dump_mmapped_reg(sctx, f, R_000E50_SRBM_STATUS);
		si_dump_mmapped_reg(sctx, f, R_000E4C_SRBM_STATUS2);
		si_dump_mmapped_reg(sctx, f, R_000E54_SRBM_STATUS3);
	}
	si_dump_mmapped_reg(sctx, f, R_008680_CP_STAT);
	si_dump_mmapped_reg(sctx, f, R_008674_CP_STALLED_STAT1);
	si_dump_mmapped_reg(sctx, f, R_008678_CP_STALLED_STAT2);
	si_dump_mmapped_reg(sctx, f, R_008670_CP_STALLED_STAT3);
	si_dump_mmapped_reg(sctx, f, R_008210_CP_CPC_STATUS);
	si_dump_mmapped_reg(sctx, f, R_008214_CP_CPC_BUSY_STAT);
	si_dump_mmapped_reg(sctx, f, R_008218_CP_CPC_STALLED_STAT1);
	si_dump_mmapped_reg(sctx, f, R_00821C_CP_CPF_STATUS);
	si_dump_mmapped_reg(sctx, f, R_008220_CP_CPF_BUSY_STAT);
	si_dump_mmapped_reg(sctx, f, R_008224_CP_CPF_STALLED_STAT1);
	fprintf(f, "\n");
}

static void si_dump_last_ib(struct si_context *sctx, FILE *f)
{
	int last_trace_id = -1;

	if (!sctx->last_gfx.ib)
		return;

	if (sctx->last_trace_buf) {
		/* We are expecting that the ddebug pipe has already
		 * waited for the context, so this buffer should be idle.
		 * If the GPU is hung, there is no point in waiting for it.
		 */
		uint32_t *map = sctx->b.ws->buffer_map(sctx->last_trace_buf->buf,
						       NULL,
						       PIPE_TRANSFER_UNSYNCHRONIZED |
						       PIPE_TRANSFER_READ);
		if (map)
			last_trace_id = *map;
	}

	if (sctx->init_config)
		ac_parse_ib(f, sctx->init_config->pm4, sctx->init_config->ndw,
			    -1, "IB2: Init config", sctx->b.chip_class,
			    NULL, NULL);

	if (sctx->init_config_gs_rings)
		ac_parse_ib(f, sctx->init_config_gs_rings->pm4,
			    sctx->init_config_gs_rings->ndw,
			    -1, "IB2: Init GS rings", sctx->b.chip_class,
			    NULL, NULL);

	ac_parse_ib(f, sctx->last_gfx.ib, sctx->last_gfx.num_dw,
		    last_trace_id, "IB", sctx->b.chip_class,
		     NULL, NULL);
}

static const char *priority_to_string(enum radeon_bo_priority priority)
{
#define ITEM(x) [RADEON_PRIO_##x] = #x
	static const char *table[64] = {
		ITEM(FENCE),
	        ITEM(TRACE),
	        ITEM(SO_FILLED_SIZE),
	        ITEM(QUERY),
	        ITEM(IB1),
	        ITEM(IB2),
	        ITEM(DRAW_INDIRECT),
	        ITEM(INDEX_BUFFER),
	        ITEM(VCE),
	        ITEM(UVD),
	        ITEM(SDMA_BUFFER),
	        ITEM(SDMA_TEXTURE),
		ITEM(CP_DMA),
	        ITEM(CONST_BUFFER),
	        ITEM(DESCRIPTORS),
	        ITEM(BORDER_COLORS),
	        ITEM(SAMPLER_BUFFER),
	        ITEM(VERTEX_BUFFER),
	        ITEM(SHADER_RW_BUFFER),
	        ITEM(COMPUTE_GLOBAL),
	        ITEM(SAMPLER_TEXTURE),
	        ITEM(SHADER_RW_IMAGE),
	        ITEM(SAMPLER_TEXTURE_MSAA),
	        ITEM(COLOR_BUFFER),
	        ITEM(DEPTH_BUFFER),
	        ITEM(COLOR_BUFFER_MSAA),
	        ITEM(DEPTH_BUFFER_MSAA),
	        ITEM(CMASK),
	        ITEM(DCC),
	        ITEM(HTILE),
		ITEM(SHADER_BINARY),
		ITEM(SHADER_RINGS),
		ITEM(SCRATCH_BUFFER),
	};
#undef ITEM

	assert(priority < ARRAY_SIZE(table));
	return table[priority];
}

static int bo_list_compare_va(const struct radeon_bo_list_item *a,
				   const struct radeon_bo_list_item *b)
{
	return a->vm_address < b->vm_address ? -1 :
	       a->vm_address > b->vm_address ? 1 : 0;
}

static void si_dump_bo_list(struct si_context *sctx,
			    const struct radeon_saved_cs *saved, FILE *f)
{
	unsigned i,j;

	if (!saved->bo_list)
		return;

	/* Sort the list according to VM adddresses first. */
	qsort(saved->bo_list, saved->bo_count,
	      sizeof(saved->bo_list[0]), (void*)bo_list_compare_va);

	fprintf(f, "Buffer list (in units of pages = 4kB):\n"
		COLOR_YELLOW "        Size    VM start page         "
		"VM end page           Usage" COLOR_RESET "\n");

	for (i = 0; i < saved->bo_count; i++) {
		/* Note: Buffer sizes are expected to be aligned to 4k by the winsys. */
		const unsigned page_size = sctx->b.screen->info.gart_page_size;
		uint64_t va = saved->bo_list[i].vm_address;
		uint64_t size = saved->bo_list[i].bo_size;
		bool hit = false;

		/* If there's unused virtual memory between 2 buffers, print it. */
		if (i) {
			uint64_t previous_va_end = saved->bo_list[i-1].vm_address +
						   saved->bo_list[i-1].bo_size;

			if (va > previous_va_end) {
				fprintf(f, "  %10"PRIu64"    -- hole --\n",
					(va - previous_va_end) / page_size);
			}
		}

		/* Print the buffer. */
		fprintf(f, "  %10"PRIu64"    0x%013"PRIX64"       0x%013"PRIX64"       ",
			size / page_size, va / page_size, (va + size) / page_size);

		/* Print the usage. */
		for (j = 0; j < 64; j++) {
			if (!(saved->bo_list[i].priority_usage & (1ull << j)))
				continue;

			fprintf(f, "%s%s", !hit ? "" : ", ", priority_to_string(j));
			hit = true;
		}
		fprintf(f, "\n");
	}
	fprintf(f, "\nNote: The holes represent memory not used by the IB.\n"
		   "      Other buffers can still be allocated there.\n\n");
}

static void si_dump_framebuffer(struct si_context *sctx, FILE *f)
{
	struct pipe_framebuffer_state *state = &sctx->framebuffer.state;
	struct r600_texture *rtex;
	int i;

	for (i = 0; i < state->nr_cbufs; i++) {
		if (!state->cbufs[i])
			continue;

		rtex = (struct r600_texture*)state->cbufs[i]->texture;
		fprintf(f, COLOR_YELLOW "Color buffer %i:" COLOR_RESET "\n", i);
		r600_print_texture_info(sctx->b.screen, rtex, f);
		fprintf(f, "\n");
	}

	if (state->zsbuf) {
		rtex = (struct r600_texture*)state->zsbuf->texture;
		fprintf(f, COLOR_YELLOW "Depth-stencil buffer:" COLOR_RESET "\n");
		r600_print_texture_info(sctx->b.screen, rtex, f);
		fprintf(f, "\n");
	}
}

typedef unsigned (*slot_remap_func)(unsigned);

static void si_dump_descriptor_list(struct si_descriptors *desc,
				    const char *shader_name,
				    const char *elem_name,
				    unsigned element_dw_size,
				    unsigned num_elements,
				    slot_remap_func slot_remap,
				    FILE *f)
{
	unsigned i, j;

	for (i = 0; i < num_elements; i++) {
		unsigned dw_offset = slot_remap(i) * element_dw_size;
		uint32_t *gpu_ptr = desc->gpu_list ? desc->gpu_list : desc->list;
		const char *list_note = desc->gpu_list ? "GPU list" : "CPU list";
		uint32_t *cpu_list = desc->list + dw_offset;
		uint32_t *gpu_list = gpu_ptr + dw_offset;

		fprintf(f, COLOR_GREEN "%s%s slot %u (%s):" COLOR_RESET "\n",
			shader_name, elem_name, i, list_note);

		switch (element_dw_size) {
		case 4:
			for (j = 0; j < 4; j++)
				ac_dump_reg(f, R_008F00_SQ_BUF_RSRC_WORD0 + j*4,
					    gpu_list[j], 0xffffffff);
			break;
		case 8:
			for (j = 0; j < 8; j++)
				ac_dump_reg(f, R_008F10_SQ_IMG_RSRC_WORD0 + j*4,
					    gpu_list[j], 0xffffffff);

			fprintf(f, COLOR_CYAN "    Buffer:" COLOR_RESET "\n");
			for (j = 0; j < 4; j++)
				ac_dump_reg(f, R_008F00_SQ_BUF_RSRC_WORD0 + j*4,
					    gpu_list[4+j], 0xffffffff);
			break;
		case 16:
			for (j = 0; j < 8; j++)
				ac_dump_reg(f, R_008F10_SQ_IMG_RSRC_WORD0 + j*4,
					    gpu_list[j], 0xffffffff);

			fprintf(f, COLOR_CYAN "    Buffer:" COLOR_RESET "\n");
			for (j = 0; j < 4; j++)
				ac_dump_reg(f, R_008F00_SQ_BUF_RSRC_WORD0 + j*4,
					    gpu_list[4+j], 0xffffffff);

			fprintf(f, COLOR_CYAN "    FMASK:" COLOR_RESET "\n");
			for (j = 0; j < 8; j++)
				ac_dump_reg(f, R_008F10_SQ_IMG_RSRC_WORD0 + j*4,
					    gpu_list[8+j], 0xffffffff);

			fprintf(f, COLOR_CYAN "    Sampler state:" COLOR_RESET "\n");
			for (j = 0; j < 4; j++)
				ac_dump_reg(f, R_008F30_SQ_IMG_SAMP_WORD0 + j*4,
					    gpu_list[12+j], 0xffffffff);
			break;
		}

		if (memcmp(gpu_list, cpu_list, desc->element_dw_size * 4) != 0) {
			fprintf(f, COLOR_RED "!!!!! This slot was corrupted in GPU memory !!!!!"
				COLOR_RESET "\n");
		}

		fprintf(f, "\n");
	}
}

static unsigned si_identity(unsigned slot)
{
	return slot;
}

static void si_dump_descriptors(struct si_context *sctx,
				enum pipe_shader_type processor,
				const struct tgsi_shader_info *info, FILE *f)
{
	struct si_descriptors *descs =
		&sctx->descriptors[SI_DESCS_FIRST_SHADER +
				   processor * SI_NUM_SHADER_DESCS];
	static const char *shader_name[] = {"VS", "PS", "GS", "TCS", "TES", "CS"};
	const char *name = shader_name[processor];
	unsigned enabled_constbuf, enabled_shaderbuf, enabled_samplers;
	unsigned enabled_images;

	if (info) {
		enabled_constbuf = info->const_buffers_declared;
		enabled_shaderbuf = info->shader_buffers_declared;
		enabled_samplers = info->samplers_declared;
		enabled_images = info->images_declared;
	} else {
		enabled_constbuf = sctx->const_and_shader_buffers[processor].enabled_mask >>
				   SI_NUM_SHADER_BUFFERS;
		enabled_shaderbuf = sctx->const_and_shader_buffers[processor].enabled_mask &
				    u_bit_consecutive(0, SI_NUM_SHADER_BUFFERS);
		enabled_shaderbuf = util_bitreverse(enabled_shaderbuf) >>
				    (32 - SI_NUM_SHADER_BUFFERS);
		enabled_samplers = sctx->samplers[processor].views.enabled_mask;
		enabled_images = sctx->images[processor].enabled_mask;
	}

	if (processor == PIPE_SHADER_VERTEX) {
		assert(info); /* only CS may not have an info struct */

		si_dump_descriptor_list(&sctx->vertex_buffers, name,
					" - Vertex buffer", 4, info->num_inputs,
					si_identity, f);
	}

	si_dump_descriptor_list(&descs[SI_SHADER_DESCS_CONST_AND_SHADER_BUFFERS],
				name, " - Constant buffer", 4,
				util_last_bit(enabled_constbuf),
				si_get_constbuf_slot, f);
	si_dump_descriptor_list(&descs[SI_SHADER_DESCS_CONST_AND_SHADER_BUFFERS],
				name, " - Shader buffer", 4,
				util_last_bit(enabled_shaderbuf),
				si_get_shaderbuf_slot, f);
	si_dump_descriptor_list(&descs[SI_SHADER_DESCS_SAMPLERS_AND_IMAGES],
				name, " - Sampler", 16,
				util_last_bit(enabled_samplers),
				si_get_sampler_slot, f);
	si_dump_descriptor_list(&descs[SI_SHADER_DESCS_SAMPLERS_AND_IMAGES],
				name, " - Image", 8,
				util_last_bit(enabled_images),
				si_get_image_slot, f);
}

static void si_dump_gfx_descriptors(struct si_context *sctx,
				    const struct si_shader_ctx_state *state,
				    FILE *f)
{
	if (!state->cso || !state->current)
		return;

	si_dump_descriptors(sctx, state->cso->type, &state->cso->info, f);
}

static void si_dump_compute_descriptors(struct si_context *sctx, FILE *f)
{
	if (!sctx->cs_shader_state.program ||
	    sctx->cs_shader_state.program != sctx->cs_shader_state.emitted_program)
		return;

	si_dump_descriptors(sctx, PIPE_SHADER_COMPUTE, NULL, f);
}

struct si_shader_inst {
	char text[160];  /* one disasm line */
	unsigned offset; /* instruction offset */
	unsigned size;   /* instruction size = 4 or 8 */
};

/* Split a disassembly string into lines and add them to the array pointed
 * to by "instructions". */
static void si_add_split_disasm(const char *disasm,
				uint64_t start_addr,
				unsigned *num,
				struct si_shader_inst *instructions)
{
	struct si_shader_inst *last_inst = *num ? &instructions[*num - 1] : NULL;
	char *next;

	while ((next = strchr(disasm, '\n'))) {
		struct si_shader_inst *inst = &instructions[*num];
		unsigned len = next - disasm;

		assert(len < ARRAY_SIZE(inst->text));
		memcpy(inst->text, disasm, len);
		inst->text[len] = 0;
		inst->offset = last_inst ? last_inst->offset + last_inst->size : 0;

		const char *semicolon = strchr(disasm, ';');
		assert(semicolon);
		/* More than 16 chars after ";" means the instruction is 8 bytes long. */
		inst->size = next - semicolon > 16 ? 8 : 4;

		snprintf(inst->text + len, ARRAY_SIZE(inst->text) - len,
			" [PC=0x%"PRIx64", off=%u, size=%u]",
			start_addr + inst->offset, inst->offset, inst->size);

		last_inst = inst;
		(*num)++;
		disasm = next + 1;
	}
}

#define MAX_WAVES_PER_CHIP (64 * 40)

struct si_wave_info {
	unsigned se; /* shader engine */
	unsigned sh; /* shader array */
	unsigned cu; /* compute unit */
	unsigned simd;
	unsigned wave;
	uint32_t status;
	uint64_t pc; /* program counter */
	uint32_t inst_dw0;
	uint32_t inst_dw1;
	uint64_t exec;
	bool matched; /* whether the wave is used by a currently-bound shader */
};

static int compare_wave(const void *p1, const void *p2)
{
	struct si_wave_info *w1 = (struct si_wave_info *)p1;
	struct si_wave_info *w2 = (struct si_wave_info *)p2;

	/* Sort waves according to PC and then SE, SH, CU, etc. */
	if (w1->pc < w2->pc)
		return -1;
	if (w1->pc > w2->pc)
		return 1;
	if (w1->se < w2->se)
		return -1;
	if (w1->se > w2->se)
		return 1;
	if (w1->sh < w2->sh)
		return -1;
	if (w1->sh > w2->sh)
		return 1;
	if (w1->cu < w2->cu)
		return -1;
	if (w1->cu > w2->cu)
		return 1;
	if (w1->simd < w2->simd)
		return -1;
	if (w1->simd > w2->simd)
		return 1;
	if (w1->wave < w2->wave)
		return -1;
	if (w1->wave > w2->wave)
		return 1;

	return 0;
}

/* Return wave information. "waves" should be a large enough array. */
static unsigned si_get_wave_info(struct si_wave_info waves[MAX_WAVES_PER_CHIP])
{
	char line[2000];
	unsigned num_waves = 0;

	FILE *p = popen("umr -wa", "r");
	if (!p)
		return 0;

	if (!fgets(line, sizeof(line), p) ||
	    strncmp(line, "SE", 2) != 0) {
		pclose(p);
		return 0;
	}

	while (fgets(line, sizeof(line), p)) {
		struct si_wave_info *w;
		uint32_t pc_hi, pc_lo, exec_hi, exec_lo;

		assert(num_waves < MAX_WAVES_PER_CHIP);
		w = &waves[num_waves];

		if (sscanf(line, "%u %u %u %u %u %x %x %x %x %x %x %x",
			   &w->se, &w->sh, &w->cu, &w->simd, &w->wave,
			   &w->status, &pc_hi, &pc_lo, &w->inst_dw0,
			   &w->inst_dw1, &exec_hi, &exec_lo) == 12) {
			w->pc = ((uint64_t)pc_hi << 32) | pc_lo;
			w->exec = ((uint64_t)exec_hi << 32) | exec_lo;
			w->matched = false;
			num_waves++;
		}
	}

	qsort(waves, num_waves, sizeof(struct si_wave_info), compare_wave);

	pclose(p);
	return num_waves;
}

/* If the shader is being executed, print its asm instructions, and annotate
 * those that are being executed right now with information about waves that
 * execute them. This is most useful during a GPU hang.
 */
static void si_print_annotated_shader(struct si_shader *shader,
				      struct si_wave_info *waves,
				      unsigned num_waves,
				      FILE *f)
{
	if (!shader || !shader->binary.disasm_string)
		return;

	uint64_t start_addr = shader->bo->gpu_address;
	uint64_t end_addr = start_addr + shader->bo->b.b.width0;
	unsigned i;

	/* See if any wave executes the shader. */
	for (i = 0; i < num_waves; i++) {
		if (start_addr <= waves[i].pc && waves[i].pc <= end_addr)
			break;
	}
	if (i == num_waves)
		return; /* the shader is not being executed */

	/* Remember the first found wave. The waves are sorted according to PC. */
	waves = &waves[i];
	num_waves -= i;

	/* Get the list of instructions.
	 * Buffer size / 4 is the upper bound of the instruction count.
	 */
	unsigned num_inst = 0;
	struct si_shader_inst *instructions =
		calloc(shader->bo->b.b.width0 / 4, sizeof(struct si_shader_inst));

	if (shader->prolog) {
		si_add_split_disasm(shader->prolog->binary.disasm_string,
				    start_addr, &num_inst, instructions);
	}
	if (shader->previous_stage) {
		si_add_split_disasm(shader->previous_stage->binary.disasm_string,
				    start_addr, &num_inst, instructions);
	}
	if (shader->prolog2) {
		si_add_split_disasm(shader->prolog2->binary.disasm_string,
				    start_addr, &num_inst, instructions);
	}
	si_add_split_disasm(shader->binary.disasm_string,
			    start_addr, &num_inst, instructions);
	if (shader->epilog) {
		si_add_split_disasm(shader->epilog->binary.disasm_string,
				    start_addr, &num_inst, instructions);
	}

	fprintf(f, COLOR_YELLOW "%s - annotated disassembly:" COLOR_RESET "\n",
		si_get_shader_name(shader, shader->selector->type));

	/* Print instructions with annotations. */
	for (i = 0; i < num_inst; i++) {
		struct si_shader_inst *inst = &instructions[i];

		fprintf(f, "%s\n", inst->text);

		/* Print which waves execute the instruction right now. */
		while (num_waves && start_addr + inst->offset == waves->pc) {
			fprintf(f,
				"          " COLOR_GREEN "^ SE%u SH%u CU%u "
				"SIMD%u WAVE%u  EXEC=%016"PRIx64 "  ",
				waves->se, waves->sh, waves->cu, waves->simd,
				waves->wave, waves->exec);

			if (inst->size == 4) {
				fprintf(f, "INST32=%08X" COLOR_RESET "\n",
					waves->inst_dw0);
			} else {
				fprintf(f, "INST64=%08X %08X" COLOR_RESET "\n",
					waves->inst_dw0, waves->inst_dw1);
			}

			waves->matched = true;
			waves = &waves[1];
			num_waves--;
		}
	}

	fprintf(f, "\n\n");
	free(instructions);
}

static void si_dump_annotated_shaders(struct si_context *sctx, FILE *f)
{
	struct si_wave_info waves[MAX_WAVES_PER_CHIP];
	unsigned num_waves = si_get_wave_info(waves);

	fprintf(f, COLOR_CYAN "The number of active waves = %u" COLOR_RESET
		"\n\n", num_waves);

	si_print_annotated_shader(sctx->vs_shader.current, waves, num_waves, f);
	si_print_annotated_shader(sctx->tcs_shader.current, waves, num_waves, f);
	si_print_annotated_shader(sctx->tes_shader.current, waves, num_waves, f);
	si_print_annotated_shader(sctx->gs_shader.current, waves, num_waves, f);
	si_print_annotated_shader(sctx->ps_shader.current, waves, num_waves, f);

	/* Print waves executing shaders that are not currently bound. */
	unsigned i;
	bool found = false;
	for (i = 0; i < num_waves; i++) {
		if (waves[i].matched)
			continue;

		if (!found) {
			fprintf(f, COLOR_CYAN
				"Waves not executing currently-bound shaders:"
				COLOR_RESET "\n");
			found = true;
		}
		fprintf(f, "    SE%u SH%u CU%u SIMD%u WAVE%u  EXEC=%016"PRIx64
			"  INST=%08X %08X  PC=%"PRIx64"\n",
			waves[i].se, waves[i].sh, waves[i].cu, waves[i].simd,
			waves[i].wave, waves[i].exec, waves[i].inst_dw0,
			waves[i].inst_dw1, waves[i].pc);
	}
	if (found)
		fprintf(f, "\n\n");
}

static void si_dump_command(const char *title, const char *command, FILE *f)
{
	char line[2000];

	FILE *p = popen(command, "r");
	if (!p)
		return;

	fprintf(f, COLOR_YELLOW "%s: " COLOR_RESET "\n", title);
	while (fgets(line, sizeof(line), p))
		fputs(line, f);
	fprintf(f, "\n\n");
	pclose(p);
}

static void si_dump_debug_state(struct pipe_context *ctx, FILE *f,
				unsigned flags)
{
	struct si_context *sctx = (struct si_context*)ctx;

	if (flags & PIPE_DUMP_DEVICE_STATUS_REGISTERS)
		si_dump_debug_registers(sctx, f);

	if (flags & PIPE_DUMP_CURRENT_STATES)
		si_dump_framebuffer(sctx, f);

	if (flags & PIPE_DUMP_CURRENT_SHADERS) {
		si_dump_gfx_shader(sctx->screen, &sctx->vs_shader, f);
		si_dump_gfx_shader(sctx->screen, &sctx->tcs_shader, f);
		si_dump_gfx_shader(sctx->screen, &sctx->tes_shader, f);
		si_dump_gfx_shader(sctx->screen, &sctx->gs_shader, f);
		si_dump_gfx_shader(sctx->screen, &sctx->ps_shader, f);
		si_dump_compute_shader(sctx->screen, &sctx->cs_shader_state, f);

		if (flags & PIPE_DUMP_DEVICE_STATUS_REGISTERS) {
			si_dump_annotated_shaders(sctx, f);
			si_dump_command("Active waves (raw data)", "umr -wa | column -t", f);
			si_dump_command("Wave information", "umr -O bits -wa", f);
		}

		si_dump_descriptor_list(&sctx->descriptors[SI_DESCS_RW_BUFFERS],
					"", "RW buffers", 4, SI_NUM_RW_BUFFERS,
					si_identity, f);
		si_dump_gfx_descriptors(sctx, &sctx->vs_shader, f);
		si_dump_gfx_descriptors(sctx, &sctx->tcs_shader, f);
		si_dump_gfx_descriptors(sctx, &sctx->tes_shader, f);
		si_dump_gfx_descriptors(sctx, &sctx->gs_shader, f);
		si_dump_gfx_descriptors(sctx, &sctx->ps_shader, f);
		si_dump_compute_descriptors(sctx, f);
	}

	if (flags & PIPE_DUMP_LAST_COMMAND_BUFFER) {
		si_dump_bo_list(sctx, &sctx->last_gfx, f);
		si_dump_last_ib(sctx, f);

		fprintf(f, "Done.\n");

		/* dump only once */
		radeon_clear_saved_cs(&sctx->last_gfx);
		r600_resource_reference(&sctx->last_trace_buf, NULL);
	}
}

static void si_dump_dma(struct si_context *sctx,
			struct radeon_saved_cs *saved, FILE *f)
{
	static const char ib_name[] = "sDMA IB";
	unsigned i;

	si_dump_bo_list(sctx, saved, f);

	fprintf(f, "------------------ %s begin ------------------\n", ib_name);

	for (i = 0; i < saved->num_dw; ++i) {
		fprintf(f, " %08x\n", saved->ib[i]);
	}

	fprintf(f, "------------------- %s end -------------------\n", ib_name);
	fprintf(f, "\n");

	fprintf(f, "SDMA Dump Done.\n");
}

static bool si_vm_fault_occured(struct si_context *sctx, uint64_t *out_addr)
{
	char line[2000];
	unsigned sec, usec;
	int progress = 0;
	uint64_t timestamp = 0;
	bool fault = false;

	FILE *p = popen("dmesg", "r");
	if (!p)
		return false;

	while (fgets(line, sizeof(line), p)) {
		char *msg, len;

		if (!line[0] || line[0] == '\n')
			continue;

		/* Get the timestamp. */
		if (sscanf(line, "[%u.%u]", &sec, &usec) != 2) {
			static bool hit = false;
			if (!hit) {
				fprintf(stderr, "%s: failed to parse line '%s'\n",
					__func__, line);
				hit = true;
			}
			continue;
		}
		timestamp = sec * 1000000ull + usec;

		/* If just updating the timestamp. */
		if (!out_addr)
			continue;

		/* Process messages only if the timestamp is newer. */
		if (timestamp <= sctx->dmesg_timestamp)
			continue;

		/* Only process the first VM fault. */
		if (fault)
			continue;

		/* Remove trailing \n */
		len = strlen(line);
		if (len && line[len-1] == '\n')
			line[len-1] = 0;

		/* Get the message part. */
		msg = strchr(line, ']');
		if (!msg) {
			assert(0);
			continue;
		}
		msg++;

		const char *header_line, *addr_line_prefix, *addr_line_format;

		if (sctx->b.chip_class >= GFX9) {
			/* Match this:
			 * ..: [gfxhub] VMC page fault (src_id:0 ring:158 vm_id:2 pas_id:0)
			 * ..:   at page 0x0000000219f8f000 from 27
			 * ..: VM_L2_PROTECTION_FAULT_STATUS:0x0020113C
			 */
			header_line = "VMC page fault";
			addr_line_prefix = "   at page";
			addr_line_format = "%"PRIx64;
		} else {
			header_line = "GPU fault detected:";
			addr_line_prefix = "VM_CONTEXT1_PROTECTION_FAULT_ADDR";
			addr_line_format = "%"PRIX64;
		}

		switch (progress) {
		case 0:
			if (strstr(msg, header_line))
				progress = 1;
			break;
		case 1:
			msg = strstr(msg, addr_line_prefix);
			if (msg) {
				msg = strstr(msg, "0x");
				if (msg) {
					msg += 2;
					if (sscanf(msg, addr_line_format, out_addr) == 1)
						fault = true;
				}
			}
			progress = 0;
			break;
		default:
			progress = 0;
		}
	}
	pclose(p);

	if (timestamp > sctx->dmesg_timestamp)
		sctx->dmesg_timestamp = timestamp;
	return fault;
}

void si_check_vm_faults(struct r600_common_context *ctx,
			struct radeon_saved_cs *saved, enum ring_type ring)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct pipe_screen *screen = sctx->b.b.screen;
	FILE *f;
	uint64_t addr;
	char cmd_line[4096];

	if (!si_vm_fault_occured(sctx, &addr))
		return;

	f = dd_get_debug_file(false);
	if (!f)
		return;

	fprintf(f, "VM fault report.\n\n");
	if (os_get_command_line(cmd_line, sizeof(cmd_line)))
		fprintf(f, "Command: %s\n", cmd_line);
	fprintf(f, "Driver vendor: %s\n", screen->get_vendor(screen));
	fprintf(f, "Device vendor: %s\n", screen->get_device_vendor(screen));
	fprintf(f, "Device name: %s\n\n", screen->get_name(screen));
	fprintf(f, "Failing VM page: 0x%08"PRIx64"\n\n", addr);

	if (sctx->apitrace_call_number)
		fprintf(f, "Last apitrace call: %u\n\n",
			sctx->apitrace_call_number);

	switch (ring) {
	case RING_GFX:
		si_dump_debug_state(&sctx->b.b, f,
				    PIPE_DUMP_CURRENT_STATES |
				    PIPE_DUMP_CURRENT_SHADERS |
				    PIPE_DUMP_LAST_COMMAND_BUFFER);
		break;

	case RING_DMA:
		si_dump_dma(sctx, saved, f);
		break;

	default:
		break;
	}

	fclose(f);

	fprintf(stderr, "Detected a VM fault, exiting...\n");
	exit(0);
}

void si_init_debug_functions(struct si_context *sctx)
{
	sctx->b.b.dump_debug_state = si_dump_debug_state;
	sctx->b.check_vm_faults = si_check_vm_faults;

	/* Set the initial dmesg timestamp for this context, so that
	 * only new messages will be checked for VM faults.
	 */
	if (sctx->screen->b.debug_flags & DBG_CHECK_VM)
		si_vm_fault_occured(sctx, NULL);
}
