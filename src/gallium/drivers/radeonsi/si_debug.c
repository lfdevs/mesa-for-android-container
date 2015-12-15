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
#include "si_shader.h"
#include "sid.h"
#include "sid_tables.h"
#include "ddebug/dd_util.h"


static void si_dump_shader(struct si_shader_ctx_state *state, const char *name,
			   FILE *f)
{
	if (!state->cso || !state->current)
		return;

	fprintf(f, "%s shader disassembly:\n", name);
	si_dump_shader_key(state->cso->type, &state->current->key, f);
	fprintf(f, "%s\n\n", state->current->binary.disasm_string);
}

/* Parsed IBs are difficult to read without colors. Use "less -R file" to
 * read them, or use "aha -b -f file" to convert them to html.
 */
#define COLOR_RESET	"\033[0m"
#define COLOR_RED	"\033[31m"
#define COLOR_GREEN	"\033[1;32m"
#define COLOR_YELLOW	"\033[1;33m"
#define COLOR_CYAN	"\033[1;36m"

#define INDENT_PKT 8

static void print_spaces(FILE *f, unsigned num)
{
	fprintf(f, "%*s", num, "");
}

static void print_value(FILE *file, uint32_t value, int bits)
{
	/* Guess if it's int or float */
	if (value <= (1 << 15))
		fprintf(file, "%u\n", value);
	else {
		float f = uif(value);

		if (fabs(f) < 100000 && f*10 == floor(f*10))
			fprintf(file, "%.1ff\n", f);
		else
			/* Don't print more leading zeros than there are bits. */
			fprintf(file, "0x%0*x\n", bits / 4, value);
	}
}

static void print_named_value(FILE *file, const char *name, uint32_t value,
			      int bits)
{
	print_spaces(file, INDENT_PKT);
	fprintf(file, COLOR_YELLOW "%s" COLOR_RESET " <- ", name);
	print_value(file, value, bits);
}

static void si_dump_reg(FILE *file, unsigned offset, uint32_t value,
			uint32_t field_mask)
{
	int r, f;

	for (r = 0; r < ARRAY_SIZE(reg_table); r++) {
		const struct si_reg *reg = &reg_table[r];

		if (reg->offset == offset) {
			bool first_field = true;

			print_spaces(file, INDENT_PKT);
			fprintf(file, COLOR_YELLOW "%s" COLOR_RESET " <- ",
				reg->name);

			if (!reg->num_fields) {
				print_value(file, value, 32);
				return;
			}

			for (f = 0; f < reg->num_fields; f++) {
				const struct si_field *field = &reg->fields[f];
				uint32_t val = (value & field->mask) >>
					       (ffs(field->mask) - 1);

				if (!(field->mask & field_mask))
					continue;

				/* Indent the field. */
				if (!first_field)
					print_spaces(file,
						     INDENT_PKT + strlen(reg->name) + 4);

				/* Print the field. */
				fprintf(file, "%s = ", field->name);

				if (val < field->num_values && field->values[val])
					fprintf(file, "%s\n", field->values[val]);
				else
					print_value(file, val,
						    util_bitcount(field->mask));

				first_field = false;
			}
			return;
		}
	}

	fprintf(file, COLOR_YELLOW "0x%05x" COLOR_RESET " = 0x%08x", offset, value);
}

static void si_parse_set_reg_packet(FILE *f, uint32_t *ib, unsigned count,
				    unsigned reg_offset)
{
	unsigned reg = (ib[1] << 2) + reg_offset;
	int i;

	for (i = 0; i < count; i++)
		si_dump_reg(f, reg + i*4, ib[2+i], ~0);
}

static uint32_t *si_parse_packet3(FILE *f, uint32_t *ib, int *num_dw,
				  int trace_id)
{
	unsigned count = PKT_COUNT_G(ib[0]);
	unsigned op = PKT3_IT_OPCODE_G(ib[0]);
	const char *predicate = PKT3_PREDICATE(ib[0]) ? "(predicate)" : "";
	int i;

	/* Print the name first. */
	for (i = 0; i < ARRAY_SIZE(packet3_table); i++)
		if (packet3_table[i].op == op)
			break;

	if (i < ARRAY_SIZE(packet3_table))
		if (op == PKT3_SET_CONTEXT_REG ||
		    op == PKT3_SET_CONFIG_REG ||
		    op == PKT3_SET_UCONFIG_REG ||
		    op == PKT3_SET_SH_REG)
			fprintf(f, COLOR_CYAN "%s%s" COLOR_CYAN ":\n",
				packet3_table[i].name, predicate);
		else
			fprintf(f, COLOR_GREEN "%s%s" COLOR_RESET ":\n",
				packet3_table[i].name, predicate);
	else
		fprintf(f, COLOR_RED "PKT3_UNKNOWN 0x%x%s" COLOR_RESET ":\n",
			op, predicate);

	/* Print the contents. */
	switch (op) {
	case PKT3_SET_CONTEXT_REG:
		si_parse_set_reg_packet(f, ib, count, SI_CONTEXT_REG_OFFSET);
		break;
	case PKT3_SET_CONFIG_REG:
		si_parse_set_reg_packet(f, ib, count, SI_CONFIG_REG_OFFSET);
		break;
	case PKT3_SET_UCONFIG_REG:
		si_parse_set_reg_packet(f, ib, count, CIK_UCONFIG_REG_OFFSET);
		break;
	case PKT3_SET_SH_REG:
		si_parse_set_reg_packet(f, ib, count, SI_SH_REG_OFFSET);
		break;
	case PKT3_DRAW_PREAMBLE:
		si_dump_reg(f, R_030908_VGT_PRIMITIVE_TYPE, ib[1], ~0);
		si_dump_reg(f, R_028AA8_IA_MULTI_VGT_PARAM, ib[2], ~0);
		si_dump_reg(f, R_028B58_VGT_LS_HS_CONFIG, ib[3], ~0);
		break;
	case PKT3_ACQUIRE_MEM:
		si_dump_reg(f, R_0301F0_CP_COHER_CNTL, ib[1], ~0);
		si_dump_reg(f, R_0301F4_CP_COHER_SIZE, ib[2], ~0);
		si_dump_reg(f, R_030230_CP_COHER_SIZE_HI, ib[3], ~0);
		si_dump_reg(f, R_0301F8_CP_COHER_BASE, ib[4], ~0);
		si_dump_reg(f, R_0301E4_CP_COHER_BASE_HI, ib[5], ~0);
		print_named_value(f, "POLL_INTERVAL", ib[6], 16);
		break;
	case PKT3_SURFACE_SYNC:
		si_dump_reg(f, R_0085F0_CP_COHER_CNTL, ib[1], ~0);
		si_dump_reg(f, R_0085F4_CP_COHER_SIZE, ib[2], ~0);
		si_dump_reg(f, R_0085F8_CP_COHER_BASE, ib[3], ~0);
		print_named_value(f, "POLL_INTERVAL", ib[4], 16);
		break;
	case PKT3_EVENT_WRITE:
		si_dump_reg(f, R_028A90_VGT_EVENT_INITIATOR, ib[1],
			    S_028A90_EVENT_TYPE(~0));
		print_named_value(f, "EVENT_INDEX", (ib[1] >> 8) & 0xf, 4);
		print_named_value(f, "INV_L2", (ib[1] >> 20) & 0x1, 1);
		if (count > 0) {
			print_named_value(f, "ADDRESS_LO", ib[2], 32);
			print_named_value(f, "ADDRESS_HI", ib[3], 16);
		}
		break;
	case PKT3_DRAW_INDEX_AUTO:
		si_dump_reg(f, R_030930_VGT_NUM_INDICES, ib[1], ~0);
		si_dump_reg(f, R_0287F0_VGT_DRAW_INITIATOR, ib[2], ~0);
		break;
	case PKT3_DRAW_INDEX_2:
		si_dump_reg(f, R_028A78_VGT_DMA_MAX_SIZE, ib[1], ~0);
		si_dump_reg(f, R_0287E8_VGT_DMA_BASE, ib[2], ~0);
		si_dump_reg(f, R_0287E4_VGT_DMA_BASE_HI, ib[3], ~0);
		si_dump_reg(f, R_030930_VGT_NUM_INDICES, ib[4], ~0);
		si_dump_reg(f, R_0287F0_VGT_DRAW_INITIATOR, ib[5], ~0);
		break;
	case PKT3_INDEX_TYPE:
		si_dump_reg(f, R_028A7C_VGT_DMA_INDEX_TYPE, ib[1], ~0);
		break;
	case PKT3_NUM_INSTANCES:
		si_dump_reg(f, R_030934_VGT_NUM_INSTANCES, ib[1], ~0);
		break;
	case PKT3_WRITE_DATA:
		si_dump_reg(f, R_370_CONTROL, ib[1], ~0);
		si_dump_reg(f, R_371_DST_ADDR_LO, ib[2], ~0);
		si_dump_reg(f, R_372_DST_ADDR_HI, ib[3], ~0);
		for (i = 2; i < count; i++) {
			print_spaces(f, INDENT_PKT);
			fprintf(f, "0x%08x\n", ib[2+i]);
		}
		break;
	case PKT3_CP_DMA:
		si_dump_reg(f, R_410_CP_DMA_WORD0, ib[1], ~0);
		si_dump_reg(f, R_411_CP_DMA_WORD1, ib[2], ~0);
		si_dump_reg(f, R_412_CP_DMA_WORD2, ib[3], ~0);
		si_dump_reg(f, R_413_CP_DMA_WORD3, ib[4], ~0);
		si_dump_reg(f, R_414_COMMAND, ib[5], ~0);
		break;
	case PKT3_DMA_DATA:
		si_dump_reg(f, R_500_DMA_DATA_WORD0, ib[1], ~0);
		si_dump_reg(f, R_501_SRC_ADDR_LO, ib[2], ~0);
		si_dump_reg(f, R_502_SRC_ADDR_HI, ib[3], ~0);
		si_dump_reg(f, R_503_DST_ADDR_LO, ib[4], ~0);
		si_dump_reg(f, R_504_DST_ADDR_HI, ib[5], ~0);
		si_dump_reg(f, R_414_COMMAND, ib[6], ~0);
		break;
	case PKT3_NOP:
		if (ib[0] == 0xffff1000) {
			count = -1; /* One dword NOP. */
			break;
		} else if (count == 0 && SI_IS_TRACE_POINT(ib[1])) {
			unsigned packet_id = SI_GET_TRACE_POINT_ID(ib[1]);

			print_spaces(f, INDENT_PKT);
			fprintf(f, COLOR_RED "Trace point ID: %u\n", packet_id);

			if (trace_id == -1)
				break; /* tracing was disabled */

			print_spaces(f, INDENT_PKT);
			if (packet_id < trace_id)
				fprintf(f, COLOR_RED
					"This trace point was reached by the CP."
					COLOR_RESET "\n");
			else if (packet_id == trace_id)
				fprintf(f, COLOR_RED
					"!!!!! This is the last trace point that "
					"was reached by the CP !!!!!"
					COLOR_RESET "\n");
			else if (packet_id+1 == trace_id)
				fprintf(f, COLOR_RED
					"!!!!! This is the first trace point that "
					"was NOT been reached by the CP !!!!!"
					COLOR_RESET "\n");
			else
				fprintf(f, COLOR_RED
					"!!!!! This trace point was NOT reached "
					"by the CP !!!!!"
					COLOR_RESET "\n");
			break;
		}
		/* fall through, print all dwords */
	default:
		for (i = 0; i < count+1; i++) {
			print_spaces(f, INDENT_PKT);
			fprintf(f, "0x%08x\n", ib[1+i]);
		}
	}

	ib += count + 2;
	*num_dw -= count + 2;
	return ib;
}

/**
 * Parse and print an IB into a file.
 *
 * \param f		file
 * \param ib		IB
 * \param num_dw	size of the IB
 * \param chip_class	chip class
 * \param trace_id	the last trace ID that is known to have been reached
 *			and executed by the CP, typically read from a buffer
 */
static void si_parse_ib(FILE *f, uint32_t *ib, int num_dw, int trace_id)
{
	fprintf(f, "------------------ IB begin ------------------\n");

	while (num_dw > 0) {
		unsigned type = PKT_TYPE_G(ib[0]);

		switch (type) {
		case 3:
			ib = si_parse_packet3(f, ib, &num_dw, trace_id);
			break;
		case 2:
			/* type-2 nop */
			if (ib[0] == 0x80000000) {
				fprintf(f, COLOR_GREEN "NOP (type 2)" COLOR_RESET "\n");
				ib++;
				break;
			}
			/* fall through */
		default:
			fprintf(f, "Unknown packet type %i\n", type);
			return;
		}
	}

	fprintf(f, "------------------- IB end -------------------\n");
	if (num_dw < 0) {
		printf("Packet ends after the end of IB.\n");
		exit(0);
	}
}

static void si_dump_mmapped_reg(struct si_context *sctx, FILE *f,
				unsigned offset)
{
	struct radeon_winsys *ws = sctx->b.ws;
	uint32_t value;

	if (ws->read_registers(ws, offset, 1, &value))
		si_dump_reg(f, offset, value, ~0);
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

	if (!sctx->last_ib)
		return;

	if (sctx->last_trace_buf) {
		/* We are expecting that the ddebug pipe has already
		 * waited for the context, so this buffer should be idle.
		 * If the GPU is hung, there is no point in waiting for it.
		 */
		uint32_t *map = sctx->b.ws->buffer_map(sctx->last_trace_buf->cs_buf,
						       NULL,
						       PIPE_TRANSFER_UNSYNCHRONIZED |
						       PIPE_TRANSFER_READ);
		if (map)
			last_trace_id = *map;
	}

	si_parse_ib(f, sctx->last_ib, sctx->last_ib_dw_size,
		    last_trace_id);
	free(sctx->last_ib); /* dump only once */
	sctx->last_ib = NULL;
	r600_resource_reference(&sctx->last_trace_buf, NULL);
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
	        ITEM(CP_DMA),
	        ITEM(VCE),
	        ITEM(UVD),
	        ITEM(SDMA_BUFFER),
	        ITEM(SDMA_TEXTURE),
	        ITEM(USER_SHADER),
	        ITEM(INTERNAL_SHADER),
	        ITEM(CONST_BUFFER),
	        ITEM(DESCRIPTORS),
	        ITEM(BORDER_COLORS),
	        ITEM(SAMPLER_BUFFER),
	        ITEM(VERTEX_BUFFER),
	        ITEM(SHADER_RW_BUFFER),
	        ITEM(RINGS_STREAMOUT),
	        ITEM(SCRATCH_BUFFER),
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

static void si_dump_last_bo_list(struct si_context *sctx, FILE *f)
{
	unsigned i,j;

	if (!sctx->last_bo_list)
		return;

	/* Sort the list according to VM adddresses first. */
	qsort(sctx->last_bo_list, sctx->last_bo_count,
	      sizeof(sctx->last_bo_list[0]), (void*)bo_list_compare_va);

	fprintf(f, "Buffer list (in units of pages = 4kB):\n"
		COLOR_YELLOW "        Size    VM start page         "
		"VM end page           Usage" COLOR_RESET "\n");

	for (i = 0; i < sctx->last_bo_count; i++) {
		/* Note: Buffer sizes are expected to be aligned to 4k by the winsys. */
		const unsigned page_size = 4096;
		uint64_t va = sctx->last_bo_list[i].vm_address;
		uint64_t size = sctx->last_bo_list[i].buf->size;
		bool hit = false;

		/* If there's unused virtual memory between 2 buffers, print it. */
		if (i) {
			uint64_t previous_va_end = sctx->last_bo_list[i-1].vm_address +
						   sctx->last_bo_list[i-1].buf->size;

			if (va > previous_va_end) {
				fprintf(f, "  %10"PRIu64"    -- hole --\n",
					(va - previous_va_end) / page_size);
			}
		}

		/* Print the buffer. */
		fprintf(f, "  %10"PRIu64"    0x%013"PRIx64"       0x%013"PRIx64"       ",
			size / page_size, va / page_size, (va + size) / page_size);

		/* Print the usage. */
		for (j = 0; j < 64; j++) {
			if (!(sctx->last_bo_list[i].priority_usage & (1llu << j)))
				continue;

			fprintf(f, "%s%s", !hit ? "" : ", ", priority_to_string(j));
			hit = true;
		}
		fprintf(f, "\n");
	}
	fprintf(f, "\nNote: The holes represent memory not used by the IB.\n"
		   "      Other buffers can still be allocated there.\n\n");

	for (i = 0; i < sctx->last_bo_count; i++)
		pb_reference(&sctx->last_bo_list[i].buf, NULL);
	free(sctx->last_bo_list);
	sctx->last_bo_list = NULL;
}

static void si_dump_debug_state(struct pipe_context *ctx, FILE *f,
				unsigned flags)
{
	struct si_context *sctx = (struct si_context*)ctx;

	if (flags & PIPE_DEBUG_DEVICE_IS_HUNG)
		si_dump_debug_registers(sctx, f);

	si_dump_shader(&sctx->vs_shader, "Vertex", f);
	si_dump_shader(&sctx->tcs_shader, "Tessellation control", f);
	si_dump_shader(&sctx->tes_shader, "Tessellation evaluation", f);
	si_dump_shader(&sctx->gs_shader, "Geometry", f);
	si_dump_shader(&sctx->ps_shader, "Fragment", f);

	si_dump_last_bo_list(sctx, f);
	si_dump_last_ib(sctx, f);

	fprintf(f, "Done.\n");
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

		/* Get the timestamp. */
		if (sscanf(line, "[%u.%u]", &sec, &usec) != 2) {
			assert(0);
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

void si_check_vm_faults(struct si_context *sctx)
{
	struct pipe_screen *screen = sctx->b.b.screen;
	FILE *f;
	uint32_t addr;

	/* Use conservative timeout 800ms, after which we won't wait any
	 * longer and assume the GPU is hung.
	 */
	sctx->b.ws->fence_wait(sctx->b.ws, sctx->last_gfx_fence, 800*1000*1000);

	if (!si_vm_fault_occured(sctx, &addr))
		return;

	f = dd_get_debug_file();
	if (!f)
		return;

	fprintf(f, "VM fault report.\n\n");
	fprintf(f, "Driver vendor: %s\n", screen->get_vendor(screen));
	fprintf(f, "Device vendor: %s\n", screen->get_device_vendor(screen));
	fprintf(f, "Device name: %s\n\n", screen->get_name(screen));
	fprintf(f, "Failing VM page: 0x%08x\n\n", addr);

	si_dump_last_bo_list(sctx, f);
	si_dump_last_ib(sctx, f);
	fclose(f);

	fprintf(stderr, "Detected a VM fault, exiting...\n");
	exit(0);
}

void si_init_debug_functions(struct si_context *sctx)
{
	sctx->b.b.dump_debug_state = si_dump_debug_state;

	/* Set the initial dmesg timestamp for this context, so that
	 * only new messages will be checked for VM faults.
	 */
	if (sctx->screen->b.debug_flags & DBG_CHECK_VM)
		si_vm_fault_occured(sctx, NULL);
}
