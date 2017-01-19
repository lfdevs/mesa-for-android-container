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
#include "sid.h"
#include "sid_tables.h"
#include "radeon/radeon_elf_util.h"
#include "ddebug/dd_util.h"
#include "util/u_memory.h"
#include "ac_debug.h"

DEBUG_GET_ONCE_OPTION(replace_shaders, "RADEON_REPLACE_SHADERS", NULL)

static void si_dump_shader(struct si_screen *sscreen,
			   struct si_shader_ctx_state *state, FILE *f)
{
	struct si_shader *current = state->current;

	if (!state->cso || !current)
		return;

	if (current->shader_log)
		fwrite(current->shader_log, current->shader_log_size, 1, f);
	else
		si_shader_dump(sscreen, state->current, NULL,
			       state->cso->info.processor, f, false);
}

/**
 * Shader compiles can be overridden with arbitrary ELF objects by setting
 * the environment variable RADEON_REPLACE_SHADERS=num1:filename1[;num2:filename2]
 */
bool si_replace_shader(unsigned num, struct radeon_shader_binary *binary)
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

	radeon_elf_read(buf, filesize, binary);
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
	si_dump_mmapped_reg(sctx, f, R_000E50_SRBM_STATUS);
	si_dump_mmapped_reg(sctx, f, R_000E4C_SRBM_STATUS2);
	si_dump_mmapped_reg(sctx, f, R_000E54_SRBM_STATUS3);
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
			if (!(saved->bo_list[i].priority_usage & (1llu << j)))
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
		r600_print_texture_info(rtex, f);
		fprintf(f, "\n");
	}

	if (state->zsbuf) {
		rtex = (struct r600_texture*)state->zsbuf->texture;
		fprintf(f, COLOR_YELLOW "Depth-stencil buffer:" COLOR_RESET "\n");
		r600_print_texture_info(rtex, f);
		fprintf(f, "\n");
	}
}

static void si_dump_descriptor_list(struct si_descriptors *desc,
				    const char *shader_name,
				    const char *elem_name,
				    unsigned num_elements,
				    FILE *f)
{
	unsigned i, j;
	uint32_t *cpu_list = desc->list;
	uint32_t *gpu_list = desc->gpu_list;
	const char *list_note = "GPU list";

	if (!gpu_list) {
		gpu_list = cpu_list;
		list_note = "CPU list";
	}

	for (i = 0; i < num_elements; i++) {
		fprintf(f, COLOR_GREEN "%s%s slot %u (%s):" COLOR_RESET "\n",
			shader_name, elem_name, i, list_note);

		switch (desc->element_dw_size) {
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
		gpu_list += desc->element_dw_size;
		cpu_list += desc->element_dw_size;
	}
}

static void si_dump_descriptors(struct si_context *sctx,
				struct si_shader_ctx_state *state,
				FILE *f)
{
	if (!state->cso || !state->current)
		return;

	unsigned type = state->cso->type;
	const struct tgsi_shader_info *info = &state->cso->info;
	struct si_descriptors *descs =
		&sctx->descriptors[SI_DESCS_FIRST_SHADER +
				   type * SI_NUM_SHADER_DESCS];
	static const char *shader_name[] = {"VS", "PS", "GS", "TCS", "TES", "CS"};

	static const char *elem_name[] = {
		" - Constant buffer",
		" - Shader buffer",
		" - Sampler",
		" - Image",
	};
	unsigned num_elements[] = {
		util_last_bit(info->const_buffers_declared),
		util_last_bit(info->shader_buffers_declared),
		util_last_bit(info->samplers_declared),
		util_last_bit(info->images_declared),
	};

	if (type == PIPE_SHADER_VERTEX) {
		si_dump_descriptor_list(&sctx->vertex_buffers, shader_name[type],
					" - Vertex buffer", info->num_inputs, f);
	}

	for (unsigned i = 0; i < SI_NUM_SHADER_DESCS; ++i, ++descs)
		si_dump_descriptor_list(descs, shader_name[type], elem_name[i],
					num_elements[i], f);
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
		si_dump_shader(sctx->screen, &sctx->vs_shader, f);
		si_dump_shader(sctx->screen, &sctx->tcs_shader, f);
		si_dump_shader(sctx->screen, &sctx->tes_shader, f);
		si_dump_shader(sctx->screen, &sctx->gs_shader, f);
		si_dump_shader(sctx->screen, &sctx->ps_shader, f);

		si_dump_descriptor_list(&sctx->descriptors[SI_DESCS_RW_BUFFERS],
					"", "RW buffers", SI_NUM_RW_BUFFERS, f);
		si_dump_descriptors(sctx, &sctx->vs_shader, f);
		si_dump_descriptors(sctx, &sctx->tcs_shader, f);
		si_dump_descriptors(sctx, &sctx->tes_shader, f);
		si_dump_descriptors(sctx, &sctx->gs_shader, f);
		si_dump_descriptors(sctx, &sctx->ps_shader, f);
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

static bool si_vm_fault_occured(struct si_context *sctx, uint32_t *out_addr)
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
		timestamp = sec * 1000000llu + usec;

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

		switch (progress) {
		case 0:
			if (strstr(msg, "GPU fault detected:"))
				progress = 1;
			break;
		case 1:
			msg = strstr(msg, "VM_CONTEXT1_PROTECTION_FAULT_ADDR");
			if (msg) {
				msg = strstr(msg, "0x");
				if (msg) {
					msg += 2;
					if (sscanf(msg, "%X", out_addr) == 1)
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
	uint32_t addr;
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
	fprintf(f, "Failing VM page: 0x%08x\n\n", addr);

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
