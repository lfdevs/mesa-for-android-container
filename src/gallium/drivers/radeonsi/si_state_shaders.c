/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
 *      Christian König <christian.koenig@amd.com>
 *      Marek Olšák <maraeo@gmail.com>
 */

#include "si_pipe.h"
#include "sid.h"
#include "radeon/r600_cs.h"

#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_ureg.h"
#include "util/hash_table.h"
#include "util/crc32.h"
#include "util/u_memory.h"
#include "util/u_prim.h"

/* SHADER_CACHE */

/**
 * Return the TGSI binary in a buffer. The first 4 bytes contain its size as
 * integer.
 */
static void *si_get_tgsi_binary(struct si_shader_selector *sel)
{
	unsigned tgsi_size = tgsi_num_tokens(sel->tokens) *
			     sizeof(struct tgsi_token);
	unsigned size = 4 + tgsi_size + sizeof(sel->so);
	char *result = (char*)MALLOC(size);

	if (!result)
		return NULL;

	*((uint32_t*)result) = size;
	memcpy(result + 4, sel->tokens, tgsi_size);
	memcpy(result + 4 + tgsi_size, &sel->so, sizeof(sel->so));
	return result;
}

/** Copy "data" to "ptr" and return the next dword following copied data. */
static uint32_t *write_data(uint32_t *ptr, const void *data, unsigned size)
{
	/* data may be NULL if size == 0 */
	if (size)
		memcpy(ptr, data, size);
	ptr += DIV_ROUND_UP(size, 4);
	return ptr;
}

/** Read data from "ptr". Return the next dword following the data. */
static uint32_t *read_data(uint32_t *ptr, void *data, unsigned size)
{
	memcpy(data, ptr, size);
	ptr += DIV_ROUND_UP(size, 4);
	return ptr;
}

/**
 * Write the size as uint followed by the data. Return the next dword
 * following the copied data.
 */
static uint32_t *write_chunk(uint32_t *ptr, const void *data, unsigned size)
{
	*ptr++ = size;
	return write_data(ptr, data, size);
}

/**
 * Read the size as uint followed by the data. Return both via parameters.
 * Return the next dword following the data.
 */
static uint32_t *read_chunk(uint32_t *ptr, void **data, unsigned *size)
{
	*size = *ptr++;
	assert(*data == NULL);
	if (!*size)
		return ptr;
	*data = malloc(*size);
	return read_data(ptr, *data, *size);
}

/**
 * Return the shader binary in a buffer. The first 4 bytes contain its size
 * as integer.
 */
static void *si_get_shader_binary(struct si_shader *shader)
{
	/* There is always a size of data followed by the data itself. */
	unsigned relocs_size = shader->binary.reloc_count *
			       sizeof(shader->binary.relocs[0]);
	unsigned disasm_size = strlen(shader->binary.disasm_string) + 1;
	unsigned llvm_ir_size = shader->binary.llvm_ir_string ?
				strlen(shader->binary.llvm_ir_string) + 1 : 0;
	unsigned size =
		4 + /* total size */
		4 + /* CRC32 of the data below */
		align(sizeof(shader->config), 4) +
		align(sizeof(shader->info), 4) +
		4 + align(shader->binary.code_size, 4) +
		4 + align(shader->binary.rodata_size, 4) +
		4 + align(relocs_size, 4) +
		4 + align(disasm_size, 4) +
		4 + align(llvm_ir_size, 4);
	void *buffer = CALLOC(1, size);
	uint32_t *ptr = (uint32_t*)buffer;

	if (!buffer)
		return NULL;

	*ptr++ = size;
	ptr++; /* CRC32 is calculated at the end. */

	ptr = write_data(ptr, &shader->config, sizeof(shader->config));
	ptr = write_data(ptr, &shader->info, sizeof(shader->info));
	ptr = write_chunk(ptr, shader->binary.code, shader->binary.code_size);
	ptr = write_chunk(ptr, shader->binary.rodata, shader->binary.rodata_size);
	ptr = write_chunk(ptr, shader->binary.relocs, relocs_size);
	ptr = write_chunk(ptr, shader->binary.disasm_string, disasm_size);
	ptr = write_chunk(ptr, shader->binary.llvm_ir_string, llvm_ir_size);
	assert((char *)ptr - (char *)buffer == size);

	/* Compute CRC32. */
	ptr = (uint32_t*)buffer;
	ptr++;
	*ptr = util_hash_crc32(ptr + 1, size - 8);

	return buffer;
}

static bool si_load_shader_binary(struct si_shader *shader, void *binary)
{
	uint32_t *ptr = (uint32_t*)binary;
	uint32_t size = *ptr++;
	uint32_t crc32 = *ptr++;
	unsigned chunk_size;

	if (util_hash_crc32(ptr, size - 8) != crc32) {
		fprintf(stderr, "radeonsi: binary shader has invalid CRC32\n");
		return false;
	}

	ptr = read_data(ptr, &shader->config, sizeof(shader->config));
	ptr = read_data(ptr, &shader->info, sizeof(shader->info));
	ptr = read_chunk(ptr, (void**)&shader->binary.code,
			 &shader->binary.code_size);
	ptr = read_chunk(ptr, (void**)&shader->binary.rodata,
			 &shader->binary.rodata_size);
	ptr = read_chunk(ptr, (void**)&shader->binary.relocs, &chunk_size);
	shader->binary.reloc_count = chunk_size / sizeof(shader->binary.relocs[0]);
	ptr = read_chunk(ptr, (void**)&shader->binary.disasm_string, &chunk_size);
	ptr = read_chunk(ptr, (void**)&shader->binary.llvm_ir_string, &chunk_size);

	return true;
}

/**
 * Insert a shader into the cache. It's assumed the shader is not in the cache.
 * Use si_shader_cache_load_shader before calling this.
 *
 * Returns false on failure, in which case the tgsi_binary should be freed.
 */
static bool si_shader_cache_insert_shader(struct si_screen *sscreen,
					  void *tgsi_binary,
					  struct si_shader *shader)
{
	void *hw_binary;
	struct hash_entry *entry;

	entry = _mesa_hash_table_search(sscreen->shader_cache, tgsi_binary);
	if (entry)
		return false; /* already added */

	hw_binary = si_get_shader_binary(shader);
	if (!hw_binary)
		return false;

	if (_mesa_hash_table_insert(sscreen->shader_cache, tgsi_binary,
				    hw_binary) == NULL) {
		FREE(hw_binary);
		return false;
	}

	return true;
}

static bool si_shader_cache_load_shader(struct si_screen *sscreen,
					void *tgsi_binary,
				        struct si_shader *shader)
{
	struct hash_entry *entry =
		_mesa_hash_table_search(sscreen->shader_cache, tgsi_binary);
	if (!entry)
		return false;

	if (!si_load_shader_binary(shader, entry->data))
		return false;

	p_atomic_inc(&sscreen->b.num_shader_cache_hits);
	return true;
}

static uint32_t si_shader_cache_key_hash(const void *key)
{
	/* The first dword is the key size. */
	return util_hash_crc32(key, *(uint32_t*)key);
}

static bool si_shader_cache_key_equals(const void *a, const void *b)
{
	uint32_t *keya = (uint32_t*)a;
	uint32_t *keyb = (uint32_t*)b;

	/* The first dword is the key size. */
	if (*keya != *keyb)
		return false;

	return memcmp(keya, keyb, *keya) == 0;
}

static void si_destroy_shader_cache_entry(struct hash_entry *entry)
{
	FREE((void*)entry->key);
	FREE(entry->data);
}

bool si_init_shader_cache(struct si_screen *sscreen)
{
	pipe_mutex_init(sscreen->shader_cache_mutex);
	sscreen->shader_cache =
		_mesa_hash_table_create(NULL,
					si_shader_cache_key_hash,
					si_shader_cache_key_equals);
	return sscreen->shader_cache != NULL;
}

void si_destroy_shader_cache(struct si_screen *sscreen)
{
	if (sscreen->shader_cache)
		_mesa_hash_table_destroy(sscreen->shader_cache,
					 si_destroy_shader_cache_entry);
	pipe_mutex_destroy(sscreen->shader_cache_mutex);
}

/* SHADER STATES */

static void si_set_tesseval_regs(struct si_screen *sscreen,
				 struct si_shader *shader,
				 struct si_pm4_state *pm4)
{
	struct tgsi_shader_info *info = &shader->selector->info;
	unsigned tes_prim_mode = info->properties[TGSI_PROPERTY_TES_PRIM_MODE];
	unsigned tes_spacing = info->properties[TGSI_PROPERTY_TES_SPACING];
	bool tes_vertex_order_cw = info->properties[TGSI_PROPERTY_TES_VERTEX_ORDER_CW];
	bool tes_point_mode = info->properties[TGSI_PROPERTY_TES_POINT_MODE];
	unsigned type, partitioning, topology, distribution_mode;

	switch (tes_prim_mode) {
	case PIPE_PRIM_LINES:
		type = V_028B6C_TESS_ISOLINE;
		break;
	case PIPE_PRIM_TRIANGLES:
		type = V_028B6C_TESS_TRIANGLE;
		break;
	case PIPE_PRIM_QUADS:
		type = V_028B6C_TESS_QUAD;
		break;
	default:
		assert(0);
		return;
	}

	switch (tes_spacing) {
	case PIPE_TESS_SPACING_FRACTIONAL_ODD:
		partitioning = V_028B6C_PART_FRAC_ODD;
		break;
	case PIPE_TESS_SPACING_FRACTIONAL_EVEN:
		partitioning = V_028B6C_PART_FRAC_EVEN;
		break;
	case PIPE_TESS_SPACING_EQUAL:
		partitioning = V_028B6C_PART_INTEGER;
		break;
	default:
		assert(0);
		return;
	}

	if (tes_point_mode)
		topology = V_028B6C_OUTPUT_POINT;
	else if (tes_prim_mode == PIPE_PRIM_LINES)
		topology = V_028B6C_OUTPUT_LINE;
	else if (tes_vertex_order_cw)
		/* for some reason, this must be the other way around */
		topology = V_028B6C_OUTPUT_TRIANGLE_CCW;
	else
		topology = V_028B6C_OUTPUT_TRIANGLE_CW;

	if (sscreen->has_distributed_tess) {
		if (sscreen->b.family == CHIP_FIJI ||
		    sscreen->b.family >= CHIP_POLARIS10)
			distribution_mode = V_028B6C_DISTRIBUTION_MODE_TRAPEZOIDS;
		else
			distribution_mode = V_028B6C_DISTRIBUTION_MODE_DONUTS;
	} else
		distribution_mode = V_028B6C_DISTRIBUTION_MODE_NO_DIST;

	si_pm4_set_reg(pm4, R_028B6C_VGT_TF_PARAM,
		       S_028B6C_TYPE(type) |
		       S_028B6C_PARTITIONING(partitioning) |
		       S_028B6C_TOPOLOGY(topology) |
		       S_028B6C_DISTRIBUTION_MODE(distribution_mode));
}

static struct si_pm4_state *si_get_shader_pm4_state(struct si_shader *shader)
{
	if (shader->pm4)
		si_pm4_clear_state(shader->pm4);
	else
		shader->pm4 = CALLOC_STRUCT(si_pm4_state);

	return shader->pm4;
}

static void si_shader_ls(struct si_shader *shader)
{
	struct si_pm4_state *pm4;
	unsigned vgpr_comp_cnt;
	uint64_t va;

	pm4 = si_get_shader_pm4_state(shader);
	if (!pm4)
		return;

	va = shader->bo->gpu_address;
	si_pm4_add_bo(pm4, shader->bo, RADEON_USAGE_READ, RADEON_PRIO_SHADER_BINARY);

	/* We need at least 2 components for LS.
	 * VGPR0-3: (VertexID, RelAutoindex, ???, InstanceID). */
	vgpr_comp_cnt = shader->info.uses_instanceid ? 3 : 1;

	si_pm4_set_reg(pm4, R_00B520_SPI_SHADER_PGM_LO_LS, va >> 8);
	si_pm4_set_reg(pm4, R_00B524_SPI_SHADER_PGM_HI_LS, va >> 40);

	shader->config.rsrc1 = S_00B528_VGPRS((shader->config.num_vgprs - 1) / 4) |
			   S_00B528_SGPRS((shader->config.num_sgprs - 1) / 8) |
		           S_00B528_VGPR_COMP_CNT(vgpr_comp_cnt) |
			   S_00B528_DX10_CLAMP(1) |
			   S_00B528_FLOAT_MODE(shader->config.float_mode);
	shader->config.rsrc2 = S_00B52C_USER_SGPR(SI_LS_NUM_USER_SGPR) |
			   S_00B52C_SCRATCH_EN(shader->config.scratch_bytes_per_wave > 0);
}

static void si_shader_hs(struct si_shader *shader)
{
	struct si_pm4_state *pm4;
	uint64_t va;

	pm4 = si_get_shader_pm4_state(shader);
	if (!pm4)
		return;

	va = shader->bo->gpu_address;
	si_pm4_add_bo(pm4, shader->bo, RADEON_USAGE_READ, RADEON_PRIO_SHADER_BINARY);

	si_pm4_set_reg(pm4, R_00B420_SPI_SHADER_PGM_LO_HS, va >> 8);
	si_pm4_set_reg(pm4, R_00B424_SPI_SHADER_PGM_HI_HS, va >> 40);
	si_pm4_set_reg(pm4, R_00B428_SPI_SHADER_PGM_RSRC1_HS,
		       S_00B428_VGPRS((shader->config.num_vgprs - 1) / 4) |
		       S_00B428_SGPRS((shader->config.num_sgprs - 1) / 8) |
		       S_00B428_DX10_CLAMP(1) |
		       S_00B428_FLOAT_MODE(shader->config.float_mode));
	si_pm4_set_reg(pm4, R_00B42C_SPI_SHADER_PGM_RSRC2_HS,
		       S_00B42C_USER_SGPR(SI_TCS_NUM_USER_SGPR) |
		       S_00B42C_OC_LDS_EN(1) |
		       S_00B42C_SCRATCH_EN(shader->config.scratch_bytes_per_wave > 0));
}

static void si_shader_es(struct si_screen *sscreen, struct si_shader *shader)
{
	struct si_pm4_state *pm4;
	unsigned num_user_sgprs;
	unsigned vgpr_comp_cnt;
	uint64_t va;
	unsigned oc_lds_en;

	pm4 = si_get_shader_pm4_state(shader);
	if (!pm4)
		return;

	va = shader->bo->gpu_address;
	si_pm4_add_bo(pm4, shader->bo, RADEON_USAGE_READ, RADEON_PRIO_SHADER_BINARY);

	if (shader->selector->type == PIPE_SHADER_VERTEX) {
		vgpr_comp_cnt = shader->info.uses_instanceid ? 3 : 0;
		num_user_sgprs = SI_ES_NUM_USER_SGPR;
	} else if (shader->selector->type == PIPE_SHADER_TESS_EVAL) {
		vgpr_comp_cnt = 3; /* all components are needed for TES */
		num_user_sgprs = SI_TES_NUM_USER_SGPR;
	} else
		unreachable("invalid shader selector type");

	oc_lds_en = shader->selector->type == PIPE_SHADER_TESS_EVAL ? 1 : 0;

	si_pm4_set_reg(pm4, R_028AAC_VGT_ESGS_RING_ITEMSIZE,
		       shader->selector->esgs_itemsize / 4);
	si_pm4_set_reg(pm4, R_00B320_SPI_SHADER_PGM_LO_ES, va >> 8);
	si_pm4_set_reg(pm4, R_00B324_SPI_SHADER_PGM_HI_ES, va >> 40);
	si_pm4_set_reg(pm4, R_00B328_SPI_SHADER_PGM_RSRC1_ES,
		       S_00B328_VGPRS((shader->config.num_vgprs - 1) / 4) |
		       S_00B328_SGPRS((shader->config.num_sgprs - 1) / 8) |
		       S_00B328_VGPR_COMP_CNT(vgpr_comp_cnt) |
		       S_00B328_DX10_CLAMP(1) |
		       S_00B328_FLOAT_MODE(shader->config.float_mode));
	si_pm4_set_reg(pm4, R_00B32C_SPI_SHADER_PGM_RSRC2_ES,
		       S_00B32C_USER_SGPR(num_user_sgprs) |
		       S_00B32C_OC_LDS_EN(oc_lds_en) |
		       S_00B32C_SCRATCH_EN(shader->config.scratch_bytes_per_wave > 0));

	if (shader->selector->type == PIPE_SHADER_TESS_EVAL)
		si_set_tesseval_regs(sscreen, shader, pm4);
}

/**
 * Calculate the appropriate setting of VGT_GS_MODE when \p shader is a
 * geometry shader.
 */
static uint32_t si_vgt_gs_mode(struct si_shader_selector *sel)
{
	unsigned gs_max_vert_out = sel->gs_max_out_vertices;
	unsigned cut_mode;

	if (gs_max_vert_out <= 128) {
		cut_mode = V_028A40_GS_CUT_128;
	} else if (gs_max_vert_out <= 256) {
		cut_mode = V_028A40_GS_CUT_256;
	} else if (gs_max_vert_out <= 512) {
		cut_mode = V_028A40_GS_CUT_512;
	} else {
		assert(gs_max_vert_out <= 1024);
		cut_mode = V_028A40_GS_CUT_1024;
	}

	return S_028A40_MODE(V_028A40_GS_SCENARIO_G) |
	       S_028A40_CUT_MODE(cut_mode)|
	       S_028A40_ES_WRITE_OPTIMIZE(1) |
	       S_028A40_GS_WRITE_OPTIMIZE(1);
}

static void si_shader_gs(struct si_shader *shader)
{
	struct si_shader_selector *sel = shader->selector;
	const ubyte *num_components = sel->info.num_stream_output_components;
	unsigned gs_num_invocations = sel->gs_num_invocations;
	struct si_pm4_state *pm4;
	uint64_t va;
	unsigned max_stream = sel->max_gs_stream;
	unsigned offset;

	pm4 = si_get_shader_pm4_state(shader);
	if (!pm4)
		return;

	si_pm4_set_reg(pm4, R_028A40_VGT_GS_MODE, si_vgt_gs_mode(shader->selector));

	offset = num_components[0] * sel->gs_max_out_vertices;
	si_pm4_set_reg(pm4, R_028A60_VGT_GSVS_RING_OFFSET_1, offset);
	if (max_stream >= 1)
		offset += num_components[1] * sel->gs_max_out_vertices;
	si_pm4_set_reg(pm4, R_028A64_VGT_GSVS_RING_OFFSET_2, offset);
	if (max_stream >= 2)
		offset += num_components[2] * sel->gs_max_out_vertices;
	si_pm4_set_reg(pm4, R_028A68_VGT_GSVS_RING_OFFSET_3, offset);
	if (max_stream >= 3)
		offset += num_components[3] * sel->gs_max_out_vertices;
	si_pm4_set_reg(pm4, R_028AB0_VGT_GSVS_RING_ITEMSIZE, offset);

	/* The GSVS_RING_ITEMSIZE register takes 15 bits */
	assert(offset < (1 << 15));

	si_pm4_set_reg(pm4, R_028B38_VGT_GS_MAX_VERT_OUT, shader->selector->gs_max_out_vertices);

	si_pm4_set_reg(pm4, R_028B5C_VGT_GS_VERT_ITEMSIZE, num_components[0]);
	si_pm4_set_reg(pm4, R_028B60_VGT_GS_VERT_ITEMSIZE_1, (max_stream >= 1) ? num_components[1] : 0);
	si_pm4_set_reg(pm4, R_028B64_VGT_GS_VERT_ITEMSIZE_2, (max_stream >= 2) ? num_components[2] : 0);
	si_pm4_set_reg(pm4, R_028B68_VGT_GS_VERT_ITEMSIZE_3, (max_stream >= 3) ? num_components[3] : 0);

	si_pm4_set_reg(pm4, R_028B90_VGT_GS_INSTANCE_CNT,
		       S_028B90_CNT(MIN2(gs_num_invocations, 127)) |
		       S_028B90_ENABLE(gs_num_invocations > 0));

	va = shader->bo->gpu_address;
	si_pm4_add_bo(pm4, shader->bo, RADEON_USAGE_READ, RADEON_PRIO_SHADER_BINARY);
	si_pm4_set_reg(pm4, R_00B220_SPI_SHADER_PGM_LO_GS, va >> 8);
	si_pm4_set_reg(pm4, R_00B224_SPI_SHADER_PGM_HI_GS, va >> 40);

	si_pm4_set_reg(pm4, R_00B228_SPI_SHADER_PGM_RSRC1_GS,
		       S_00B228_VGPRS((shader->config.num_vgprs - 1) / 4) |
		       S_00B228_SGPRS((shader->config.num_sgprs - 1) / 8) |
		       S_00B228_DX10_CLAMP(1) |
		       S_00B228_FLOAT_MODE(shader->config.float_mode));
	si_pm4_set_reg(pm4, R_00B22C_SPI_SHADER_PGM_RSRC2_GS,
		       S_00B22C_USER_SGPR(SI_GS_NUM_USER_SGPR) |
		       S_00B22C_SCRATCH_EN(shader->config.scratch_bytes_per_wave > 0));
}

/**
 * Compute the state for \p shader, which will run as a vertex shader on the
 * hardware.
 *
 * If \p gs is non-NULL, it points to the geometry shader for which this shader
 * is the copy shader.
 */
static void si_shader_vs(struct si_screen *sscreen, struct si_shader *shader,
                         struct si_shader_selector *gs)
{
	struct si_pm4_state *pm4;
	unsigned num_user_sgprs;
	unsigned nparams, vgpr_comp_cnt;
	uint64_t va;
	unsigned oc_lds_en;
	unsigned window_space =
	   shader->selector->info.properties[TGSI_PROPERTY_VS_WINDOW_SPACE_POSITION];
	bool enable_prim_id = si_vs_exports_prim_id(shader);

	pm4 = si_get_shader_pm4_state(shader);
	if (!pm4)
		return;

	/* We always write VGT_GS_MODE in the VS state, because every switch
	 * between different shader pipelines involving a different GS or no
	 * GS at all involves a switch of the VS (different GS use different
	 * copy shaders). On the other hand, when the API switches from a GS to
	 * no GS and then back to the same GS used originally, the GS state is
	 * not sent again.
	 */
	if (!gs) {
		si_pm4_set_reg(pm4, R_028A40_VGT_GS_MODE,
			       S_028A40_MODE(enable_prim_id ? V_028A40_GS_SCENARIO_A : 0));
		si_pm4_set_reg(pm4, R_028A84_VGT_PRIMITIVEID_EN, enable_prim_id);
	} else {
		si_pm4_set_reg(pm4, R_028A40_VGT_GS_MODE, si_vgt_gs_mode(gs));
		si_pm4_set_reg(pm4, R_028A84_VGT_PRIMITIVEID_EN, 0);
	}

	va = shader->bo->gpu_address;
	si_pm4_add_bo(pm4, shader->bo, RADEON_USAGE_READ, RADEON_PRIO_SHADER_BINARY);

	if (gs) {
		vgpr_comp_cnt = 0; /* only VertexID is needed for GS-COPY. */
		num_user_sgprs = SI_GSCOPY_NUM_USER_SGPR;
	} else if (shader->selector->type == PIPE_SHADER_VERTEX) {
		vgpr_comp_cnt = shader->info.uses_instanceid ? 3 : (enable_prim_id ? 2 : 0);
		num_user_sgprs = SI_VS_NUM_USER_SGPR;
	} else if (shader->selector->type == PIPE_SHADER_TESS_EVAL) {
		vgpr_comp_cnt = 3; /* all components are needed for TES */
		num_user_sgprs = SI_TES_NUM_USER_SGPR;
	} else
		unreachable("invalid shader selector type");

	/* VS is required to export at least one param. */
	nparams = MAX2(shader->info.nr_param_exports, 1);
	si_pm4_set_reg(pm4, R_0286C4_SPI_VS_OUT_CONFIG,
		       S_0286C4_VS_EXPORT_COUNT(nparams - 1));

	si_pm4_set_reg(pm4, R_02870C_SPI_SHADER_POS_FORMAT,
		       S_02870C_POS0_EXPORT_FORMAT(V_02870C_SPI_SHADER_4COMP) |
		       S_02870C_POS1_EXPORT_FORMAT(shader->info.nr_pos_exports > 1 ?
						   V_02870C_SPI_SHADER_4COMP :
						   V_02870C_SPI_SHADER_NONE) |
		       S_02870C_POS2_EXPORT_FORMAT(shader->info.nr_pos_exports > 2 ?
						   V_02870C_SPI_SHADER_4COMP :
						   V_02870C_SPI_SHADER_NONE) |
		       S_02870C_POS3_EXPORT_FORMAT(shader->info.nr_pos_exports > 3 ?
						   V_02870C_SPI_SHADER_4COMP :
						   V_02870C_SPI_SHADER_NONE));

	oc_lds_en = shader->selector->type == PIPE_SHADER_TESS_EVAL ? 1 : 0;

	si_pm4_set_reg(pm4, R_00B120_SPI_SHADER_PGM_LO_VS, va >> 8);
	si_pm4_set_reg(pm4, R_00B124_SPI_SHADER_PGM_HI_VS, va >> 40);
	si_pm4_set_reg(pm4, R_00B128_SPI_SHADER_PGM_RSRC1_VS,
		       S_00B128_VGPRS((shader->config.num_vgprs - 1) / 4) |
		       S_00B128_SGPRS((shader->config.num_sgprs - 1) / 8) |
		       S_00B128_VGPR_COMP_CNT(vgpr_comp_cnt) |
		       S_00B128_DX10_CLAMP(1) |
		       S_00B128_FLOAT_MODE(shader->config.float_mode));
	si_pm4_set_reg(pm4, R_00B12C_SPI_SHADER_PGM_RSRC2_VS,
		       S_00B12C_USER_SGPR(num_user_sgprs) |
		       S_00B12C_OC_LDS_EN(oc_lds_en) |
		       S_00B12C_SO_BASE0_EN(!!shader->selector->so.stride[0]) |
		       S_00B12C_SO_BASE1_EN(!!shader->selector->so.stride[1]) |
		       S_00B12C_SO_BASE2_EN(!!shader->selector->so.stride[2]) |
		       S_00B12C_SO_BASE3_EN(!!shader->selector->so.stride[3]) |
		       S_00B12C_SO_EN(!!shader->selector->so.num_outputs) |
		       S_00B12C_SCRATCH_EN(shader->config.scratch_bytes_per_wave > 0));
	if (window_space)
		si_pm4_set_reg(pm4, R_028818_PA_CL_VTE_CNTL,
			       S_028818_VTX_XY_FMT(1) | S_028818_VTX_Z_FMT(1));
	else
		si_pm4_set_reg(pm4, R_028818_PA_CL_VTE_CNTL,
			       S_028818_VTX_W0_FMT(1) |
			       S_028818_VPORT_X_SCALE_ENA(1) | S_028818_VPORT_X_OFFSET_ENA(1) |
			       S_028818_VPORT_Y_SCALE_ENA(1) | S_028818_VPORT_Y_OFFSET_ENA(1) |
			       S_028818_VPORT_Z_SCALE_ENA(1) | S_028818_VPORT_Z_OFFSET_ENA(1));

	if (shader->selector->type == PIPE_SHADER_TESS_EVAL)
		si_set_tesseval_regs(sscreen, shader, pm4);
}

static unsigned si_get_ps_num_interp(struct si_shader *ps)
{
	struct tgsi_shader_info *info = &ps->selector->info;
	unsigned num_colors = !!(info->colors_read & 0x0f) +
			      !!(info->colors_read & 0xf0);
	unsigned num_interp = ps->selector->info.num_inputs +
			      (ps->key.part.ps.prolog.color_two_side ? num_colors : 0);

	assert(num_interp <= 32);
	return MIN2(num_interp, 32);
}

static unsigned si_get_spi_shader_col_format(struct si_shader *shader)
{
	unsigned value = shader->key.part.ps.epilog.spi_shader_col_format;
	unsigned i, num_targets = (util_last_bit(value) + 3) / 4;

	/* If the i-th target format is set, all previous target formats must
	 * be non-zero to avoid hangs.
	 */
	for (i = 0; i < num_targets; i++)
		if (!(value & (0xf << (i * 4))))
			value |= V_028714_SPI_SHADER_32_R << (i * 4);

	return value;
}

static unsigned si_get_cb_shader_mask(unsigned spi_shader_col_format)
{
	unsigned i, cb_shader_mask = 0;

	for (i = 0; i < 8; i++) {
		switch ((spi_shader_col_format >> (i * 4)) & 0xf) {
		case V_028714_SPI_SHADER_ZERO:
			break;
		case V_028714_SPI_SHADER_32_R:
			cb_shader_mask |= 0x1 << (i * 4);
			break;
		case V_028714_SPI_SHADER_32_GR:
			cb_shader_mask |= 0x3 << (i * 4);
			break;
		case V_028714_SPI_SHADER_32_AR:
			cb_shader_mask |= 0x9 << (i * 4);
			break;
		case V_028714_SPI_SHADER_FP16_ABGR:
		case V_028714_SPI_SHADER_UNORM16_ABGR:
		case V_028714_SPI_SHADER_SNORM16_ABGR:
		case V_028714_SPI_SHADER_UINT16_ABGR:
		case V_028714_SPI_SHADER_SINT16_ABGR:
		case V_028714_SPI_SHADER_32_ABGR:
			cb_shader_mask |= 0xf << (i * 4);
			break;
		default:
			assert(0);
		}
	}
	return cb_shader_mask;
}

static void si_shader_ps(struct si_shader *shader)
{
	struct tgsi_shader_info *info = &shader->selector->info;
	struct si_pm4_state *pm4;
	unsigned spi_ps_in_control, spi_shader_col_format, cb_shader_mask;
	unsigned spi_baryc_cntl = S_0286E0_FRONT_FACE_ALL_BITS(1);
	uint64_t va;
	unsigned input_ena = shader->config.spi_ps_input_ena;

	/* we need to enable at least one of them, otherwise we hang the GPU */
	assert(G_0286CC_PERSP_SAMPLE_ENA(input_ena) ||
	       G_0286CC_PERSP_CENTER_ENA(input_ena) ||
	       G_0286CC_PERSP_CENTROID_ENA(input_ena) ||
	       G_0286CC_PERSP_PULL_MODEL_ENA(input_ena) ||
	       G_0286CC_LINEAR_SAMPLE_ENA(input_ena) ||
	       G_0286CC_LINEAR_CENTER_ENA(input_ena) ||
	       G_0286CC_LINEAR_CENTROID_ENA(input_ena) ||
	       G_0286CC_LINE_STIPPLE_TEX_ENA(input_ena));
	/* POS_W_FLOAT_ENA requires one of the perspective weights. */
	assert(!G_0286CC_POS_W_FLOAT_ENA(input_ena) ||
	       G_0286CC_PERSP_SAMPLE_ENA(input_ena) ||
	       G_0286CC_PERSP_CENTER_ENA(input_ena) ||
	       G_0286CC_PERSP_CENTROID_ENA(input_ena) ||
	       G_0286CC_PERSP_PULL_MODEL_ENA(input_ena));

	/* Validate interpolation optimization flags (read as implications). */
	assert(!shader->key.part.ps.prolog.bc_optimize_for_persp ||
	       (G_0286CC_PERSP_CENTER_ENA(input_ena) &&
		G_0286CC_PERSP_CENTROID_ENA(input_ena)));
	assert(!shader->key.part.ps.prolog.bc_optimize_for_linear ||
	       (G_0286CC_LINEAR_CENTER_ENA(input_ena) &&
		G_0286CC_LINEAR_CENTROID_ENA(input_ena)));
	assert(!shader->key.part.ps.prolog.force_persp_center_interp ||
	       (!G_0286CC_PERSP_SAMPLE_ENA(input_ena) &&
		!G_0286CC_PERSP_CENTROID_ENA(input_ena)));
	assert(!shader->key.part.ps.prolog.force_linear_center_interp ||
	       (!G_0286CC_LINEAR_SAMPLE_ENA(input_ena) &&
		!G_0286CC_LINEAR_CENTROID_ENA(input_ena)));
	assert(!shader->key.part.ps.prolog.force_persp_sample_interp ||
	       (!G_0286CC_PERSP_CENTER_ENA(input_ena) &&
		!G_0286CC_PERSP_CENTROID_ENA(input_ena)));
	assert(!shader->key.part.ps.prolog.force_linear_sample_interp ||
	       (!G_0286CC_LINEAR_CENTER_ENA(input_ena) &&
		!G_0286CC_LINEAR_CENTROID_ENA(input_ena)));

	/* Validate cases when the optimizations are off (read as implications). */
	assert(shader->key.part.ps.prolog.bc_optimize_for_persp ||
	       !G_0286CC_PERSP_CENTER_ENA(input_ena) ||
	       !G_0286CC_PERSP_CENTROID_ENA(input_ena));
	assert(shader->key.part.ps.prolog.bc_optimize_for_linear ||
	       !G_0286CC_LINEAR_CENTER_ENA(input_ena) ||
	       !G_0286CC_LINEAR_CENTROID_ENA(input_ena));

	pm4 = si_get_shader_pm4_state(shader);
	if (!pm4)
		return;

	/* SPI_BARYC_CNTL.POS_FLOAT_LOCATION
	 * Possible vaules:
	 * 0 -> Position = pixel center
	 * 1 -> Position = pixel centroid
	 * 2 -> Position = at sample position
	 *
	 * From GLSL 4.5 specification, section 7.1:
	 *   "The variable gl_FragCoord is available as an input variable from
	 *    within fragment shaders and it holds the window relative coordinates
	 *    (x, y, z, 1/w) values for the fragment. If multi-sampling, this
	 *    value can be for any location within the pixel, or one of the
	 *    fragment samples. The use of centroid does not further restrict
	 *    this value to be inside the current primitive."
	 *
	 * Meaning that centroid has no effect and we can return anything within
	 * the pixel. Thus, return the value at sample position, because that's
	 * the most accurate one shaders can get.
	 */
	spi_baryc_cntl |= S_0286E0_POS_FLOAT_LOCATION(2);

	if (info->properties[TGSI_PROPERTY_FS_COORD_PIXEL_CENTER] ==
	    TGSI_FS_COORD_PIXEL_CENTER_INTEGER)
		spi_baryc_cntl |= S_0286E0_POS_FLOAT_ULC(1);

	spi_shader_col_format = si_get_spi_shader_col_format(shader);
	cb_shader_mask = si_get_cb_shader_mask(spi_shader_col_format);

	/* Ensure that some export memory is always allocated, for two reasons:
	 *
	 * 1) Correctness: The hardware ignores the EXEC mask if no export
	 *    memory is allocated, so KILL and alpha test do not work correctly
	 *    without this.
	 * 2) Performance: Every shader needs at least a NULL export, even when
	 *    it writes no color/depth output. The NULL export instruction
	 *    stalls without this setting.
	 *
	 * Don't add this to CB_SHADER_MASK.
	 */
	if (!spi_shader_col_format &&
	    !info->writes_z && !info->writes_stencil && !info->writes_samplemask)
		spi_shader_col_format = V_028714_SPI_SHADER_32_R;

	si_pm4_set_reg(pm4, R_0286CC_SPI_PS_INPUT_ENA, input_ena);
	si_pm4_set_reg(pm4, R_0286D0_SPI_PS_INPUT_ADDR,
		       shader->config.spi_ps_input_addr);

	/* Set interpolation controls. */
	spi_ps_in_control = S_0286D8_NUM_INTERP(si_get_ps_num_interp(shader));

	/* Set registers. */
	si_pm4_set_reg(pm4, R_0286E0_SPI_BARYC_CNTL, spi_baryc_cntl);
	si_pm4_set_reg(pm4, R_0286D8_SPI_PS_IN_CONTROL, spi_ps_in_control);

	si_pm4_set_reg(pm4, R_028710_SPI_SHADER_Z_FORMAT,
		       si_get_spi_shader_z_format(info->writes_z,
						  info->writes_stencil,
						  info->writes_samplemask));

	si_pm4_set_reg(pm4, R_028714_SPI_SHADER_COL_FORMAT, spi_shader_col_format);
	si_pm4_set_reg(pm4, R_02823C_CB_SHADER_MASK, cb_shader_mask);

	va = shader->bo->gpu_address;
	si_pm4_add_bo(pm4, shader->bo, RADEON_USAGE_READ, RADEON_PRIO_SHADER_BINARY);
	si_pm4_set_reg(pm4, R_00B020_SPI_SHADER_PGM_LO_PS, va >> 8);
	si_pm4_set_reg(pm4, R_00B024_SPI_SHADER_PGM_HI_PS, va >> 40);

	si_pm4_set_reg(pm4, R_00B028_SPI_SHADER_PGM_RSRC1_PS,
		       S_00B028_VGPRS((shader->config.num_vgprs - 1) / 4) |
		       S_00B028_SGPRS((shader->config.num_sgprs - 1) / 8) |
		       S_00B028_DX10_CLAMP(1) |
		       S_00B028_FLOAT_MODE(shader->config.float_mode));
	si_pm4_set_reg(pm4, R_00B02C_SPI_SHADER_PGM_RSRC2_PS,
		       S_00B02C_EXTRA_LDS_SIZE(shader->config.lds_size) |
		       S_00B02C_USER_SGPR(SI_PS_NUM_USER_SGPR) |
		       S_00B32C_SCRATCH_EN(shader->config.scratch_bytes_per_wave > 0));
}

static void si_shader_init_pm4_state(struct si_screen *sscreen,
                                     struct si_shader *shader)
{
	switch (shader->selector->type) {
	case PIPE_SHADER_VERTEX:
		if (shader->key.as_ls)
			si_shader_ls(shader);
		else if (shader->key.as_es)
			si_shader_es(sscreen, shader);
		else
			si_shader_vs(sscreen, shader, NULL);
		break;
	case PIPE_SHADER_TESS_CTRL:
		si_shader_hs(shader);
		break;
	case PIPE_SHADER_TESS_EVAL:
		if (shader->key.as_es)
			si_shader_es(sscreen, shader);
		else
			si_shader_vs(sscreen, shader, NULL);
		break;
	case PIPE_SHADER_GEOMETRY:
		si_shader_gs(shader);
		break;
	case PIPE_SHADER_FRAGMENT:
		si_shader_ps(shader);
		break;
	default:
		assert(0);
	}
}

static unsigned si_get_alpha_test_func(struct si_context *sctx)
{
	/* Alpha-test should be disabled if colorbuffer 0 is integer. */
	if (sctx->queued.named.dsa)
		return sctx->queued.named.dsa->alpha_func;

	return PIPE_FUNC_ALWAYS;
}

static void si_shader_selector_key_hw_vs(struct si_context *sctx,
					 struct si_shader_selector *vs,
					 struct si_shader_key *key)
{
	struct si_shader_selector *ps = sctx->ps_shader.cso;

	key->opt.hw_vs.clip_disable =
		sctx->queued.named.rasterizer->clip_plane_enable == 0 &&
		(vs->info.clipdist_writemask ||
		 vs->info.writes_clipvertex) &&
		!vs->info.culldist_writemask;

	/* Find out if PS is disabled. */
	bool ps_disabled = true;
	if (ps) {
		bool ps_modifies_zs = ps->info.uses_kill ||
				      ps->info.writes_z ||
				      ps->info.writes_stencil ||
				      ps->info.writes_samplemask ||
				      si_get_alpha_test_func(sctx) != PIPE_FUNC_ALWAYS;

		unsigned ps_colormask = sctx->framebuffer.colorbuf_enabled_4bit &
					sctx->queued.named.blend->cb_target_mask;
		if (!ps->info.properties[TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS])
			ps_colormask &= ps->colors_written_4bit;

		ps_disabled = sctx->queued.named.rasterizer->rasterizer_discard ||
			      (!ps_colormask &&
			       !ps_modifies_zs &&
			       !ps->info.writes_memory);
	}

	/* Find out which VS outputs aren't used by the PS. */
	uint64_t outputs_written = vs->outputs_written;
	uint32_t outputs_written2 = vs->outputs_written2;
	uint64_t inputs_read = 0;
	uint32_t inputs_read2 = 0;

	outputs_written &= ~0x3; /* ignore POSITION, PSIZE */

	if (!ps_disabled) {
		inputs_read = ps->inputs_read;
		inputs_read2 = ps->inputs_read2;
	}

	uint64_t linked = outputs_written & inputs_read;
	uint32_t linked2 = outputs_written2 & inputs_read2;

	key->opt.hw_vs.kill_outputs = ~linked & outputs_written;
	key->opt.hw_vs.kill_outputs2 = ~linked2 & outputs_written2;
}

/* Compute the key for the hw shader variant */
static inline void si_shader_selector_key(struct pipe_context *ctx,
					  struct si_shader_selector *sel,
					  struct si_shader_key *key)
{
	struct si_context *sctx = (struct si_context *)ctx;
	unsigned i;

	memset(key, 0, sizeof(*key));

	switch (sel->type) {
	case PIPE_SHADER_VERTEX:
		if (sctx->vertex_elements) {
			unsigned count = MIN2(sel->info.num_inputs,
					      sctx->vertex_elements->count);
			for (i = 0; i < count; ++i)
				key->part.vs.prolog.instance_divisors[i] =
					sctx->vertex_elements->elements[i].instance_divisor;

			key->mono.vs.fix_fetch =
				sctx->vertex_elements->fix_fetch &
				u_bit_consecutive64(0, 4 * count);
		}
		if (sctx->tes_shader.cso)
			key->as_ls = 1;
		else if (sctx->gs_shader.cso)
			key->as_es = 1;
		else {
			si_shader_selector_key_hw_vs(sctx, sel, key);

			if (sctx->ps_shader.cso && sctx->ps_shader.cso->info.uses_primid)
				key->part.vs.epilog.export_prim_id = 1;
		}
		break;
	case PIPE_SHADER_TESS_CTRL:
		key->part.tcs.epilog.prim_mode =
			sctx->tes_shader.cso->info.properties[TGSI_PROPERTY_TES_PRIM_MODE];

		if (sel == sctx->fixed_func_tcs_shader.cso)
			key->mono.tcs.inputs_to_copy = sctx->vs_shader.cso->outputs_written;
		break;
	case PIPE_SHADER_TESS_EVAL:
		if (sctx->gs_shader.cso)
			key->as_es = 1;
		else {
			si_shader_selector_key_hw_vs(sctx, sel, key);

			if (sctx->ps_shader.cso && sctx->ps_shader.cso->info.uses_primid)
				key->part.tes.epilog.export_prim_id = 1;
		}
		break;
	case PIPE_SHADER_GEOMETRY:
		key->part.gs.prolog.tri_strip_adj_fix = sctx->gs_tri_strip_adj_fix;
		break;
	case PIPE_SHADER_FRAGMENT: {
		struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
		struct si_state_blend *blend = sctx->queued.named.blend;

		if (sel->info.properties[TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS] &&
		    sel->info.colors_written == 0x1)
			key->part.ps.epilog.last_cbuf = MAX2(sctx->framebuffer.state.nr_cbufs, 1) - 1;

		if (blend) {
			/* Select the shader color format based on whether
			 * blending or alpha are needed.
			 */
			key->part.ps.epilog.spi_shader_col_format =
				(blend->blend_enable_4bit & blend->need_src_alpha_4bit &
				 sctx->framebuffer.spi_shader_col_format_blend_alpha) |
				(blend->blend_enable_4bit & ~blend->need_src_alpha_4bit &
				 sctx->framebuffer.spi_shader_col_format_blend) |
				(~blend->blend_enable_4bit & blend->need_src_alpha_4bit &
				 sctx->framebuffer.spi_shader_col_format_alpha) |
				(~blend->blend_enable_4bit & ~blend->need_src_alpha_4bit &
				 sctx->framebuffer.spi_shader_col_format);

			/* The output for dual source blending should have
			 * the same format as the first output.
			 */
			if (blend->dual_src_blend)
				key->part.ps.epilog.spi_shader_col_format |=
					(key->part.ps.epilog.spi_shader_col_format & 0xf) << 4;
		} else
			key->part.ps.epilog.spi_shader_col_format = sctx->framebuffer.spi_shader_col_format;

		/* If alpha-to-coverage is enabled, we have to export alpha
		 * even if there is no color buffer.
		 */
		if (!(key->part.ps.epilog.spi_shader_col_format & 0xf) &&
		    blend && blend->alpha_to_coverage)
			key->part.ps.epilog.spi_shader_col_format |= V_028710_SPI_SHADER_32_AR;

		/* On SI and CIK except Hawaii, the CB doesn't clamp outputs
		 * to the range supported by the type if a channel has less
		 * than 16 bits and the export format is 16_ABGR.
		 */
		if (sctx->b.chip_class <= CIK && sctx->b.family != CHIP_HAWAII) {
			key->part.ps.epilog.color_is_int8 = sctx->framebuffer.color_is_int8;
			key->part.ps.epilog.color_is_int10 = sctx->framebuffer.color_is_int10;
		}

		/* Disable unwritten outputs (if WRITE_ALL_CBUFS isn't enabled). */
		if (!key->part.ps.epilog.last_cbuf) {
			key->part.ps.epilog.spi_shader_col_format &= sel->colors_written_4bit;
			key->part.ps.epilog.color_is_int8 &= sel->info.colors_written;
			key->part.ps.epilog.color_is_int10 &= sel->info.colors_written;
		}

		if (rs) {
			bool is_poly = (sctx->current_rast_prim >= PIPE_PRIM_TRIANGLES &&
					sctx->current_rast_prim <= PIPE_PRIM_POLYGON) ||
				       sctx->current_rast_prim >= PIPE_PRIM_TRIANGLES_ADJACENCY;
			bool is_line = !is_poly && sctx->current_rast_prim != PIPE_PRIM_POINTS;

			key->part.ps.prolog.color_two_side = rs->two_side && sel->info.colors_read;
			key->part.ps.prolog.flatshade_colors = rs->flatshade && sel->info.colors_read;

			if (sctx->queued.named.blend) {
				key->part.ps.epilog.alpha_to_one = sctx->queued.named.blend->alpha_to_one &&
							      rs->multisample_enable;
			}

			key->part.ps.prolog.poly_stipple = rs->poly_stipple_enable && is_poly;
			key->part.ps.epilog.poly_line_smoothing = ((is_poly && rs->poly_smooth) ||
							      (is_line && rs->line_smooth)) &&
							     sctx->framebuffer.nr_samples <= 1;
			key->part.ps.epilog.clamp_color = rs->clamp_fragment_color;

			if (rs->force_persample_interp &&
			    rs->multisample_enable &&
			    sctx->framebuffer.nr_samples > 1 &&
			    sctx->ps_iter_samples > 1) {
				key->part.ps.prolog.force_persp_sample_interp =
					sel->info.uses_persp_center ||
					sel->info.uses_persp_centroid;

				key->part.ps.prolog.force_linear_sample_interp =
					sel->info.uses_linear_center ||
					sel->info.uses_linear_centroid;
			} else if (rs->multisample_enable &&
				   sctx->framebuffer.nr_samples > 1) {
				key->part.ps.prolog.bc_optimize_for_persp =
					sel->info.uses_persp_center &&
					sel->info.uses_persp_centroid;
				key->part.ps.prolog.bc_optimize_for_linear =
					sel->info.uses_linear_center &&
					sel->info.uses_linear_centroid;
			} else {
				/* Make sure SPI doesn't compute more than 1 pair
				 * of (i,j), which is the optimization here. */
				key->part.ps.prolog.force_persp_center_interp =
					sel->info.uses_persp_center +
					sel->info.uses_persp_centroid +
					sel->info.uses_persp_sample > 1;

				key->part.ps.prolog.force_linear_center_interp =
					sel->info.uses_linear_center +
					sel->info.uses_linear_centroid +
					sel->info.uses_linear_sample > 1;
			}
		}

		key->part.ps.epilog.alpha_func = si_get_alpha_test_func(sctx);
		break;
	}
	default:
		assert(0);
	}
}

static void si_build_shader_variant(void *job, int thread_index)
{
	struct si_shader *shader = (struct si_shader *)job;
	struct si_shader_selector *sel = shader->selector;
	struct si_screen *sscreen = sel->screen;
	LLVMTargetMachineRef tm;
	struct pipe_debug_callback *debug = &shader->compiler_ctx_state.debug;
	int r;

	if (thread_index >= 0) {
		assert(thread_index < ARRAY_SIZE(sscreen->tm));
		tm = sscreen->tm[thread_index];
		if (!debug->async)
			debug = NULL;
	} else {
		tm = shader->compiler_ctx_state.tm;
	}

	r = si_shader_create(sscreen, tm, shader, debug);
	if (unlikely(r)) {
		R600_ERR("Failed to build shader variant (type=%u) %d\n",
			 sel->type, r);
		shader->compilation_failed = true;
		return;
	}

	if (shader->compiler_ctx_state.is_debug_context) {
		FILE *f = open_memstream(&shader->shader_log,
					 &shader->shader_log_size);
		if (f) {
			si_shader_dump(sscreen, shader, NULL, sel->type, f, false);
			fclose(f);
		}
	}

	si_shader_init_pm4_state(sscreen, shader);
}

/* Select the hw shader variant depending on the current state. */
static int si_shader_select_with_key(struct si_screen *sscreen,
				     struct si_shader_ctx_state *state,
				     struct si_compiler_ctx_state *compiler_state,
				     struct si_shader_key *key,
				     int thread_index)
{
	static const struct si_shader_key zeroed;
	struct si_shader_selector *sel = state->cso;
	struct si_shader *current = state->current;
	struct si_shader *iter, *shader = NULL;

	if (unlikely(sscreen->b.debug_flags & DBG_NO_OPT_VARIANT)) {
		memset(&key->opt, 0, sizeof(key->opt));
	}

again:
	/* Check if we don't need to change anything.
	 * This path is also used for most shaders that don't need multiple
	 * variants, it will cost just a computation of the key and this
	 * test. */
	if (likely(current &&
		   memcmp(&current->key, key, sizeof(*key)) == 0 &&
		   (!current->is_optimized ||
		    util_queue_fence_is_signalled(&current->optimized_ready))))
		return current->compilation_failed ? -1 : 0;

	/* This must be done before the mutex is locked, because async GS
	 * compilation calls this function too, and therefore must enter
	 * the mutex first.
	 *
	 * Only wait if we are in a draw call. Don't wait if we are
	 * in a compiler thread.
	 */
	if (thread_index < 0)
		util_queue_job_wait(&sel->ready);

	pipe_mutex_lock(sel->mutex);

	/* Find the shader variant. */
	for (iter = sel->first_variant; iter; iter = iter->next_variant) {
		/* Don't check the "current" shader. We checked it above. */
		if (current != iter &&
		    memcmp(&iter->key, key, sizeof(*key)) == 0) {
			/* If it's an optimized shader and its compilation has
			 * been started but isn't done, use the unoptimized
			 * shader so as not to cause a stall due to compilation.
			 */
			if (iter->is_optimized &&
			    !util_queue_fence_is_signalled(&iter->optimized_ready)) {
				memset(&key->opt, 0, sizeof(key->opt));
				pipe_mutex_unlock(sel->mutex);
				goto again;
			}

			if (iter->compilation_failed) {
				pipe_mutex_unlock(sel->mutex);
				return -1; /* skip the draw call */
			}

			state->current = iter;
			pipe_mutex_unlock(sel->mutex);
			return 0;
		}
	}

	/* Build a new shader. */
	shader = CALLOC_STRUCT(si_shader);
	if (!shader) {
		pipe_mutex_unlock(sel->mutex);
		return -ENOMEM;
	}
	shader->selector = sel;
	shader->key = *key;
	shader->compiler_ctx_state = *compiler_state;

	/* Monolithic-only shaders don't make a distinction between optimized
	 * and unoptimized. */
	shader->is_monolithic =
		!sel->main_shader_part ||
		sel->main_shader_part->key.as_ls != key->as_ls ||
		sel->main_shader_part->key.as_es != key->as_es ||
		memcmp(&key->opt, &zeroed.opt, sizeof(key->opt)) != 0 ||
		memcmp(&key->mono, &zeroed.mono, sizeof(key->mono)) != 0;

	shader->is_optimized =
		!sscreen->use_monolithic_shaders &&
		memcmp(&key->opt, &zeroed.opt, sizeof(key->opt)) != 0;
	if (shader->is_optimized)
		util_queue_fence_init(&shader->optimized_ready);

	if (!sel->last_variant) {
		sel->first_variant = shader;
		sel->last_variant = shader;
	} else {
		sel->last_variant->next_variant = shader;
		sel->last_variant = shader;
	}

	/* If it's an optimized shader, compile it asynchronously. */
	if (shader->is_optimized &&
	    thread_index < 0) {
		/* Compile it asynchronously. */
		util_queue_add_job(&sscreen->shader_compiler_queue,
				   shader, &shader->optimized_ready,
				   si_build_shader_variant, NULL);

		/* Use the default (unoptimized) shader for now. */
		memset(&key->opt, 0, sizeof(key->opt));
		pipe_mutex_unlock(sel->mutex);
		goto again;
	}

	assert(!shader->is_optimized);
	si_build_shader_variant(shader, thread_index);

	if (!shader->compilation_failed)
		state->current = shader;

	pipe_mutex_unlock(sel->mutex);
	return shader->compilation_failed ? -1 : 0;
}

static int si_shader_select(struct pipe_context *ctx,
			    struct si_shader_ctx_state *state,
			    struct si_compiler_ctx_state *compiler_state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_key key;

	si_shader_selector_key(ctx, state->cso, &key);
	return si_shader_select_with_key(sctx->screen, state, compiler_state,
					 &key, -1);
}

static void si_parse_next_shader_property(const struct tgsi_shader_info *info,
					  struct si_shader_key *key)
{
	unsigned next_shader = info->properties[TGSI_PROPERTY_NEXT_SHADER];

	switch (info->processor) {
	case PIPE_SHADER_VERTEX:
		switch (next_shader) {
		case PIPE_SHADER_GEOMETRY:
			key->as_es = 1;
			break;
		case PIPE_SHADER_TESS_CTRL:
		case PIPE_SHADER_TESS_EVAL:
			key->as_ls = 1;
			break;
		default:
			/* If POSITION isn't written, it can't be a HW VS.
			 * Assume that it's a HW LS. (the next shader is TCS)
			 * This heuristic is needed for separate shader objects.
			 */
			if (!info->writes_position)
				key->as_ls = 1;
		}
		break;

	case PIPE_SHADER_TESS_EVAL:
		if (next_shader == PIPE_SHADER_GEOMETRY ||
		    !info->writes_position)
			key->as_es = 1;
		break;
	}
}

/**
 * Compile the main shader part or the monolithic shader as part of
 * si_shader_selector initialization. Since it can be done asynchronously,
 * there is no way to report compile failures to applications.
 */
void si_init_shader_selector_async(void *job, int thread_index)
{
	struct si_shader_selector *sel = (struct si_shader_selector *)job;
	struct si_screen *sscreen = sel->screen;
	LLVMTargetMachineRef tm;
	struct pipe_debug_callback *debug = &sel->compiler_ctx_state.debug;
	unsigned i;

	if (thread_index >= 0) {
		assert(thread_index < ARRAY_SIZE(sscreen->tm));
		tm = sscreen->tm[thread_index];
		if (!debug->async)
			debug = NULL;
	} else {
		tm = sel->compiler_ctx_state.tm;
	}

	/* Compile the main shader part for use with a prolog and/or epilog.
	 * If this fails, the driver will try to compile a monolithic shader
	 * on demand.
	 */
	if (!sscreen->use_monolithic_shaders) {
		struct si_shader *shader = CALLOC_STRUCT(si_shader);
		void *tgsi_binary;

		if (!shader) {
			fprintf(stderr, "radeonsi: can't allocate a main shader part\n");
			return;
		}

		shader->selector = sel;
		si_parse_next_shader_property(&sel->info, &shader->key);

		tgsi_binary = si_get_tgsi_binary(sel);

		/* Try to load the shader from the shader cache. */
		pipe_mutex_lock(sscreen->shader_cache_mutex);

		if (tgsi_binary &&
		    si_shader_cache_load_shader(sscreen, tgsi_binary, shader)) {
			FREE(tgsi_binary);
			pipe_mutex_unlock(sscreen->shader_cache_mutex);
		} else {
			pipe_mutex_unlock(sscreen->shader_cache_mutex);

			/* Compile the shader if it hasn't been loaded from the cache. */
			if (si_compile_tgsi_shader(sscreen, tm, shader, false,
						   debug) != 0) {
				FREE(shader);
				FREE(tgsi_binary);
				fprintf(stderr, "radeonsi: can't compile a main shader part\n");
				return;
			}

			if (tgsi_binary) {
				pipe_mutex_lock(sscreen->shader_cache_mutex);
				if (!si_shader_cache_insert_shader(sscreen, tgsi_binary, shader))
					FREE(tgsi_binary);
				pipe_mutex_unlock(sscreen->shader_cache_mutex);
			}
		}

		sel->main_shader_part = shader;

		/* Unset "outputs_written" flags for outputs converted to
		 * DEFAULT_VAL, so that later inter-shader optimizations don't
		 * try to eliminate outputs that don't exist in the final
		 * shader.
		 *
		 * This is only done if non-monolithic shaders are enabled.
		 */
		if ((sel->type == PIPE_SHADER_VERTEX ||
		     sel->type == PIPE_SHADER_TESS_EVAL) &&
		    !shader->key.as_ls &&
		    !shader->key.as_es) {
			unsigned i;

			for (i = 0; i < sel->info.num_outputs; i++) {
				unsigned offset = shader->info.vs_output_param_offset[i];

				if (offset <= EXP_PARAM_OFFSET_31)
					continue;

				unsigned name = sel->info.output_semantic_name[i];
				unsigned index = sel->info.output_semantic_index[i];
				unsigned id;

				switch (name) {
				case TGSI_SEMANTIC_GENERIC:
					/* don't process indices the function can't handle */
					if (index >= 60)
						break;
					/* fall through */
				case TGSI_SEMANTIC_CLIPDIST:
					id = si_shader_io_get_unique_index(name, index);
					sel->outputs_written &= ~(1ull << id);
					break;
				case TGSI_SEMANTIC_POSITION: /* ignore these */
				case TGSI_SEMANTIC_PSIZE:
				case TGSI_SEMANTIC_CLIPVERTEX:
				case TGSI_SEMANTIC_EDGEFLAG:
					break;
				default:
					id = si_shader_io_get_unique_index2(name, index);
					sel->outputs_written2 &= ~(1u << id);
				}
			}
		}
	}

	/* Pre-compilation. */
	if (sscreen->b.debug_flags & DBG_PRECOMPILE) {
		struct si_shader_ctx_state state = {sel};
		struct si_shader_key key;

		memset(&key, 0, sizeof(key));
		si_parse_next_shader_property(&sel->info, &key);

		/* Set reasonable defaults, so that the shader key doesn't
		 * cause any code to be eliminated.
		 */
		switch (sel->type) {
		case PIPE_SHADER_TESS_CTRL:
			key.part.tcs.epilog.prim_mode = PIPE_PRIM_TRIANGLES;
			break;
		case PIPE_SHADER_FRAGMENT:
			key.part.ps.prolog.bc_optimize_for_persp =
				sel->info.uses_persp_center &&
				sel->info.uses_persp_centroid;
			key.part.ps.prolog.bc_optimize_for_linear =
				sel->info.uses_linear_center &&
				sel->info.uses_linear_centroid;
			key.part.ps.epilog.alpha_func = PIPE_FUNC_ALWAYS;
			for (i = 0; i < 8; i++)
				if (sel->info.colors_written & (1 << i))
					key.part.ps.epilog.spi_shader_col_format |=
						V_028710_SPI_SHADER_FP16_ABGR << (i * 4);
			break;
		}

		if (si_shader_select_with_key(sscreen, &state,
					      &sel->compiler_ctx_state, &key,
					      thread_index))
			fprintf(stderr, "radeonsi: can't create a monolithic shader\n");
	}

	/* The GS copy shader is always pre-compiled. */
	if (sel->type == PIPE_SHADER_GEOMETRY) {
		sel->gs_copy_shader = si_generate_gs_copy_shader(sscreen, tm, sel, debug);
		if (!sel->gs_copy_shader) {
			fprintf(stderr, "radeonsi: can't create GS copy shader\n");
			return;
		}

		si_shader_vs(sscreen, sel->gs_copy_shader, sel);
	}
}

static void *si_create_shader_selector(struct pipe_context *ctx,
				       const struct pipe_shader_state *state)
{
	struct si_screen *sscreen = (struct si_screen *)ctx->screen;
	struct si_context *sctx = (struct si_context*)ctx;
	struct si_shader_selector *sel = CALLOC_STRUCT(si_shader_selector);
	int i;

	if (!sel)
		return NULL;

	sel->screen = sscreen;
	sel->compiler_ctx_state.tm = sctx->tm;
	sel->compiler_ctx_state.debug = sctx->b.debug;
	sel->compiler_ctx_state.is_debug_context = sctx->is_debug;
	sel->tokens = tgsi_dup_tokens(state->tokens);
	if (!sel->tokens) {
		FREE(sel);
		return NULL;
	}

	sel->so = state->stream_output;
	tgsi_scan_shader(state->tokens, &sel->info);
	sel->type = sel->info.processor;
	p_atomic_inc(&sscreen->b.num_shaders_created);

	/* Set which opcode uses which (i,j) pair. */
	if (sel->info.uses_persp_opcode_interp_centroid)
		sel->info.uses_persp_centroid = true;

	if (sel->info.uses_linear_opcode_interp_centroid)
		sel->info.uses_linear_centroid = true;

	if (sel->info.uses_persp_opcode_interp_offset ||
	    sel->info.uses_persp_opcode_interp_sample)
		sel->info.uses_persp_center = true;

	if (sel->info.uses_linear_opcode_interp_offset ||
	    sel->info.uses_linear_opcode_interp_sample)
		sel->info.uses_linear_center = true;

	switch (sel->type) {
	case PIPE_SHADER_GEOMETRY:
		sel->gs_output_prim =
			sel->info.properties[TGSI_PROPERTY_GS_OUTPUT_PRIM];
		sel->gs_max_out_vertices =
			sel->info.properties[TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES];
		sel->gs_num_invocations =
			sel->info.properties[TGSI_PROPERTY_GS_INVOCATIONS];
		sel->gsvs_vertex_size = sel->info.num_outputs * 16;
		sel->max_gsvs_emit_size = sel->gsvs_vertex_size *
					  sel->gs_max_out_vertices;

		sel->max_gs_stream = 0;
		for (i = 0; i < sel->so.num_outputs; i++)
			sel->max_gs_stream = MAX2(sel->max_gs_stream,
						  sel->so.output[i].stream);

		sel->gs_input_verts_per_prim =
			u_vertices_per_prim(sel->info.properties[TGSI_PROPERTY_GS_INPUT_PRIM]);
		break;

	case PIPE_SHADER_TESS_CTRL:
		/* Always reserve space for these. */
		sel->patch_outputs_written |=
			(1llu << si_shader_io_get_unique_index(TGSI_SEMANTIC_TESSINNER, 0)) |
			(1llu << si_shader_io_get_unique_index(TGSI_SEMANTIC_TESSOUTER, 0));
		/* fall through */
	case PIPE_SHADER_VERTEX:
	case PIPE_SHADER_TESS_EVAL:
		for (i = 0; i < sel->info.num_outputs; i++) {
			unsigned name = sel->info.output_semantic_name[i];
			unsigned index = sel->info.output_semantic_index[i];

			switch (name) {
			case TGSI_SEMANTIC_TESSINNER:
			case TGSI_SEMANTIC_TESSOUTER:
			case TGSI_SEMANTIC_PATCH:
				sel->patch_outputs_written |=
					1llu << si_shader_io_get_unique_index(name, index);
				break;

			case TGSI_SEMANTIC_GENERIC:
				/* don't process indices the function can't handle */
				if (index >= 60)
					break;
				/* fall through */
			case TGSI_SEMANTIC_POSITION:
			case TGSI_SEMANTIC_PSIZE:
			case TGSI_SEMANTIC_CLIPDIST:
				sel->outputs_written |=
					1llu << si_shader_io_get_unique_index(name, index);
				break;
			case TGSI_SEMANTIC_CLIPVERTEX: /* ignore these */
			case TGSI_SEMANTIC_EDGEFLAG:
				break;
			default:
				sel->outputs_written2 |=
					1u << si_shader_io_get_unique_index2(name, index);
			}
		}
		sel->esgs_itemsize = util_last_bit64(sel->outputs_written) * 16;
		break;

	case PIPE_SHADER_FRAGMENT:
		for (i = 0; i < sel->info.num_inputs; i++) {
			unsigned name = sel->info.input_semantic_name[i];
			unsigned index = sel->info.input_semantic_index[i];

			switch (name) {
			case TGSI_SEMANTIC_CLIPDIST:
			case TGSI_SEMANTIC_GENERIC:
				sel->inputs_read |=
					1llu << si_shader_io_get_unique_index(name, index);
				break;
			case TGSI_SEMANTIC_PCOORD: /* ignore this */
				break;
			default:
				sel->inputs_read2 |=
					1u << si_shader_io_get_unique_index2(name, index);
			}
		}

		for (i = 0; i < 8; i++)
			if (sel->info.colors_written & (1 << i))
				sel->colors_written_4bit |= 0xf << (4 * i);

		for (i = 0; i < sel->info.num_inputs; i++) {
			if (sel->info.input_semantic_name[i] == TGSI_SEMANTIC_COLOR) {
				int index = sel->info.input_semantic_index[i];
				sel->color_attr_index[index] = i;
			}
		}
		break;
	}

	/* DB_SHADER_CONTROL */
	sel->db_shader_control =
		S_02880C_Z_EXPORT_ENABLE(sel->info.writes_z) |
		S_02880C_STENCIL_TEST_VAL_EXPORT_ENABLE(sel->info.writes_stencil) |
		S_02880C_MASK_EXPORT_ENABLE(sel->info.writes_samplemask) |
		S_02880C_KILL_ENABLE(sel->info.uses_kill);

	switch (sel->info.properties[TGSI_PROPERTY_FS_DEPTH_LAYOUT]) {
	case TGSI_FS_DEPTH_LAYOUT_GREATER:
		sel->db_shader_control |=
			S_02880C_CONSERVATIVE_Z_EXPORT(V_02880C_EXPORT_GREATER_THAN_Z);
		break;
	case TGSI_FS_DEPTH_LAYOUT_LESS:
		sel->db_shader_control |=
			S_02880C_CONSERVATIVE_Z_EXPORT(V_02880C_EXPORT_LESS_THAN_Z);
		break;
	}

	/* Z_ORDER, EXEC_ON_HIER_FAIL and EXEC_ON_NOOP should be set as following:
	 *
	 *   | early Z/S | writes_mem | allow_ReZ? |      Z_ORDER       | EXEC_ON_HIER_FAIL | EXEC_ON_NOOP
	 * --|-----------|------------|------------|--------------------|-------------------|-------------
	 * 1a|   false   |   false    |   true     | EarlyZ_Then_ReZ    |         0         |     0
	 * 1b|   false   |   false    |   false    | EarlyZ_Then_LateZ  |         0         |     0
	 * 2 |   false   |   true     |   n/a      |       LateZ        |         1         |     0
	 * 3 |   true    |   false    |   n/a      | EarlyZ_Then_LateZ  |         0         |     0
	 * 4 |   true    |   true     |   n/a      | EarlyZ_Then_LateZ  |         0         |     1
	 *
	 * In cases 3 and 4, HW will force Z_ORDER to EarlyZ regardless of what's set in the register.
	 * In case 2, NOOP_CULL is a don't care field. In case 2, 3 and 4, ReZ doesn't make sense.
	 *
	 * Don't use ReZ without profiling !!!
	 *
	 * ReZ decreases performance by 15% in DiRT: Showdown on Ultra settings, which has pretty complex
	 * shaders.
	 */
	if (sel->info.properties[TGSI_PROPERTY_FS_EARLY_DEPTH_STENCIL]) {
		/* Cases 3, 4. */
		sel->db_shader_control |= S_02880C_DEPTH_BEFORE_SHADER(1) |
					  S_02880C_Z_ORDER(V_02880C_EARLY_Z_THEN_LATE_Z) |
					  S_02880C_EXEC_ON_NOOP(sel->info.writes_memory);
	} else if (sel->info.writes_memory) {
		/* Case 2. */
		sel->db_shader_control |= S_02880C_Z_ORDER(V_02880C_LATE_Z) |
					  S_02880C_EXEC_ON_HIER_FAIL(1);
	} else {
		/* Case 1. */
		sel->db_shader_control |= S_02880C_Z_ORDER(V_02880C_EARLY_Z_THEN_LATE_Z);
	}

	pipe_mutex_init(sel->mutex);
	util_queue_fence_init(&sel->ready);

	if ((sctx->b.debug.debug_message && !sctx->b.debug.async) ||
	    sctx->is_debug ||
	    r600_can_dump_shader(&sscreen->b, sel->info.processor) ||
	    !util_queue_is_initialized(&sscreen->shader_compiler_queue))
		si_init_shader_selector_async(sel, -1);
	else
		util_queue_add_job(&sscreen->shader_compiler_queue, sel,
                                   &sel->ready, si_init_shader_selector_async,
                                   NULL);

	return sel;
}

static void si_bind_vs_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = state;

	if (sctx->vs_shader.cso == sel)
		return;

	sctx->vs_shader.cso = sel;
	sctx->vs_shader.current = sel ? sel->first_variant : NULL;
	sctx->do_update_shaders = true;
	si_mark_atom_dirty(sctx, &sctx->clip_regs);
	r600_update_vs_writes_viewport_index(&sctx->b, si_get_vs_info(sctx));
}

static void si_bind_gs_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = state;
	bool enable_changed = !!sctx->gs_shader.cso != !!sel;

	if (sctx->gs_shader.cso == sel)
		return;

	sctx->gs_shader.cso = sel;
	sctx->gs_shader.current = sel ? sel->first_variant : NULL;
	sctx->do_update_shaders = true;
	si_mark_atom_dirty(sctx, &sctx->clip_regs);
	sctx->last_rast_prim = -1; /* reset this so that it gets updated */

	if (enable_changed)
		si_shader_change_notify(sctx);
	r600_update_vs_writes_viewport_index(&sctx->b, si_get_vs_info(sctx));
}

static void si_bind_tcs_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = state;
	bool enable_changed = !!sctx->tcs_shader.cso != !!sel;

	if (sctx->tcs_shader.cso == sel)
		return;

	sctx->tcs_shader.cso = sel;
	sctx->tcs_shader.current = sel ? sel->first_variant : NULL;
	sctx->do_update_shaders = true;

	if (enable_changed)
		sctx->last_tcs = NULL; /* invalidate derived tess state */
}

static void si_bind_tes_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = state;
	bool enable_changed = !!sctx->tes_shader.cso != !!sel;

	if (sctx->tes_shader.cso == sel)
		return;

	sctx->tes_shader.cso = sel;
	sctx->tes_shader.current = sel ? sel->first_variant : NULL;
	sctx->do_update_shaders = true;
	si_mark_atom_dirty(sctx, &sctx->clip_regs);
	sctx->last_rast_prim = -1; /* reset this so that it gets updated */

	if (enable_changed) {
		si_shader_change_notify(sctx);
		sctx->last_tes_sh_base = -1; /* invalidate derived tess state */
	}
	r600_update_vs_writes_viewport_index(&sctx->b, si_get_vs_info(sctx));
}

static void si_bind_ps_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = state;

	/* skip if supplied shader is one already in use */
	if (sctx->ps_shader.cso == sel)
		return;

	sctx->ps_shader.cso = sel;
	sctx->ps_shader.current = sel ? sel->first_variant : NULL;
	sctx->do_update_shaders = true;
	si_mark_atom_dirty(sctx, &sctx->cb_render_state);
}

static void si_delete_shader(struct si_context *sctx, struct si_shader *shader)
{
	if (shader->is_optimized) {
		util_queue_job_wait(&shader->optimized_ready);
		util_queue_fence_destroy(&shader->optimized_ready);
	}

	if (shader->pm4) {
		switch (shader->selector->type) {
		case PIPE_SHADER_VERTEX:
			if (shader->key.as_ls)
				si_pm4_delete_state(sctx, ls, shader->pm4);
			else if (shader->key.as_es)
				si_pm4_delete_state(sctx, es, shader->pm4);
			else
				si_pm4_delete_state(sctx, vs, shader->pm4);
			break;
		case PIPE_SHADER_TESS_CTRL:
			si_pm4_delete_state(sctx, hs, shader->pm4);
			break;
		case PIPE_SHADER_TESS_EVAL:
			if (shader->key.as_es)
				si_pm4_delete_state(sctx, es, shader->pm4);
			else
				si_pm4_delete_state(sctx, vs, shader->pm4);
			break;
		case PIPE_SHADER_GEOMETRY:
			if (shader->is_gs_copy_shader)
				si_pm4_delete_state(sctx, vs, shader->pm4);
			else
				si_pm4_delete_state(sctx, gs, shader->pm4);
			break;
		case PIPE_SHADER_FRAGMENT:
			si_pm4_delete_state(sctx, ps, shader->pm4);
			break;
		}
	}

	si_shader_destroy(shader);
	free(shader);
}

static void si_delete_shader_selector(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = (struct si_shader_selector *)state;
	struct si_shader *p = sel->first_variant, *c;
	struct si_shader_ctx_state *current_shader[SI_NUM_SHADERS] = {
		[PIPE_SHADER_VERTEX] = &sctx->vs_shader,
		[PIPE_SHADER_TESS_CTRL] = &sctx->tcs_shader,
		[PIPE_SHADER_TESS_EVAL] = &sctx->tes_shader,
		[PIPE_SHADER_GEOMETRY] = &sctx->gs_shader,
		[PIPE_SHADER_FRAGMENT] = &sctx->ps_shader,
	};

	util_queue_job_wait(&sel->ready);

	if (current_shader[sel->type]->cso == sel) {
		current_shader[sel->type]->cso = NULL;
		current_shader[sel->type]->current = NULL;
	}

	while (p) {
		c = p->next_variant;
		si_delete_shader(sctx, p);
		p = c;
	}

	if (sel->main_shader_part)
		si_delete_shader(sctx, sel->main_shader_part);
	if (sel->gs_copy_shader)
		si_delete_shader(sctx, sel->gs_copy_shader);

	util_queue_fence_destroy(&sel->ready);
	pipe_mutex_destroy(sel->mutex);
	free(sel->tokens);
	free(sel);
}

static unsigned si_get_ps_input_cntl(struct si_context *sctx,
				     struct si_shader *vs, unsigned name,
				     unsigned index, unsigned interpolate)
{
	struct tgsi_shader_info *vsinfo = &vs->selector->info;
	unsigned j, offset, ps_input_cntl = 0;

	if (interpolate == TGSI_INTERPOLATE_CONSTANT ||
	    (interpolate == TGSI_INTERPOLATE_COLOR && sctx->flatshade))
		ps_input_cntl |= S_028644_FLAT_SHADE(1);

	if (name == TGSI_SEMANTIC_PCOORD ||
	    (name == TGSI_SEMANTIC_TEXCOORD &&
	     sctx->sprite_coord_enable & (1 << index))) {
		ps_input_cntl |= S_028644_PT_SPRITE_TEX(1);
	}

	for (j = 0; j < vsinfo->num_outputs; j++) {
		if (name == vsinfo->output_semantic_name[j] &&
		    index == vsinfo->output_semantic_index[j]) {
			offset = vs->info.vs_output_param_offset[j];

			if (offset <= EXP_PARAM_OFFSET_31) {
				/* The input is loaded from parameter memory. */
				ps_input_cntl |= S_028644_OFFSET(offset);
			} else if (!G_028644_PT_SPRITE_TEX(ps_input_cntl)) {
				if (offset == EXP_PARAM_UNDEFINED) {
					/* This can happen with depth-only rendering. */
					offset = 0;
				} else {
					/* The input is a DEFAULT_VAL constant. */
					assert(offset >= EXP_PARAM_DEFAULT_VAL_0000 &&
					       offset <= EXP_PARAM_DEFAULT_VAL_1111);
					offset -= EXP_PARAM_DEFAULT_VAL_0000;
				}

				ps_input_cntl = S_028644_OFFSET(0x20) |
						S_028644_DEFAULT_VAL(offset);
			}
			break;
		}
	}

	if (name == TGSI_SEMANTIC_PRIMID)
		/* PrimID is written after the last output. */
		ps_input_cntl |= S_028644_OFFSET(vs->info.vs_output_param_offset[vsinfo->num_outputs]);
	else if (j == vsinfo->num_outputs && !G_028644_PT_SPRITE_TEX(ps_input_cntl)) {
		/* No corresponding output found, load defaults into input.
		 * Don't set any other bits.
		 * (FLAT_SHADE=1 completely changes behavior) */
		ps_input_cntl = S_028644_OFFSET(0x20);
		/* D3D 9 behaviour. GL is undefined */
		if (name == TGSI_SEMANTIC_COLOR && index == 0)
			ps_input_cntl |= S_028644_DEFAULT_VAL(3);
	}
	return ps_input_cntl;
}

static void si_emit_spi_map(struct si_context *sctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = sctx->b.gfx.cs;
	struct si_shader *ps = sctx->ps_shader.current;
	struct si_shader *vs = si_get_vs_state(sctx);
	struct tgsi_shader_info *psinfo = ps ? &ps->selector->info : NULL;
	unsigned i, num_interp, num_written = 0, bcol_interp[2];

	if (!ps || !ps->selector->info.num_inputs)
		return;

	num_interp = si_get_ps_num_interp(ps);
	assert(num_interp > 0);
	radeon_set_context_reg_seq(cs, R_028644_SPI_PS_INPUT_CNTL_0, num_interp);

	for (i = 0; i < psinfo->num_inputs; i++) {
		unsigned name = psinfo->input_semantic_name[i];
		unsigned index = psinfo->input_semantic_index[i];
		unsigned interpolate = psinfo->input_interpolate[i];

		radeon_emit(cs, si_get_ps_input_cntl(sctx, vs, name, index,
						     interpolate));
		num_written++;

		if (name == TGSI_SEMANTIC_COLOR) {
			assert(index < ARRAY_SIZE(bcol_interp));
			bcol_interp[index] = interpolate;
		}
	}

	if (ps->key.part.ps.prolog.color_two_side) {
		unsigned bcol = TGSI_SEMANTIC_BCOLOR;

		for (i = 0; i < 2; i++) {
			if (!(psinfo->colors_read & (0xf << (i * 4))))
				continue;

			radeon_emit(cs, si_get_ps_input_cntl(sctx, vs, bcol,
							     i, bcol_interp[i]));
			num_written++;
		}
	}
	assert(num_interp == num_written);
}

/**
 * Writing CONFIG or UCONFIG VGT registers requires VGT_FLUSH before that.
 */
static void si_init_config_add_vgt_flush(struct si_context *sctx)
{
	if (sctx->init_config_has_vgt_flush)
		return;

	/* Done by Vulkan before VGT_FLUSH. */
	si_pm4_cmd_begin(sctx->init_config, PKT3_EVENT_WRITE);
	si_pm4_cmd_add(sctx->init_config,
		       EVENT_TYPE(V_028A90_VS_PARTIAL_FLUSH) | EVENT_INDEX(4));
	si_pm4_cmd_end(sctx->init_config, false);

	/* VGT_FLUSH is required even if VGT is idle. It resets VGT pointers. */
	si_pm4_cmd_begin(sctx->init_config, PKT3_EVENT_WRITE);
	si_pm4_cmd_add(sctx->init_config, EVENT_TYPE(V_028A90_VGT_FLUSH) | EVENT_INDEX(0));
	si_pm4_cmd_end(sctx->init_config, false);
	sctx->init_config_has_vgt_flush = true;
}

/* Initialize state related to ESGS / GSVS ring buffers */
static bool si_update_gs_ring_buffers(struct si_context *sctx)
{
	struct si_shader_selector *es =
		sctx->tes_shader.cso ? sctx->tes_shader.cso : sctx->vs_shader.cso;
	struct si_shader_selector *gs = sctx->gs_shader.cso;
	struct si_pm4_state *pm4;

	/* Chip constants. */
	unsigned num_se = sctx->screen->b.info.max_se;
	unsigned wave_size = 64;
	unsigned max_gs_waves = 32 * num_se; /* max 32 per SE on GCN */
	unsigned gs_vertex_reuse = 16 * num_se; /* GS_VERTEX_REUSE register (per SE) */
	unsigned alignment = 256 * num_se;
	/* The maximum size is 63.999 MB per SE. */
	unsigned max_size = ((unsigned)(63.999 * 1024 * 1024) & ~255) * num_se;

	/* Calculate the minimum size. */
	unsigned min_esgs_ring_size = align(es->esgs_itemsize * gs_vertex_reuse *
					    wave_size, alignment);

	/* These are recommended sizes, not minimum sizes. */
	unsigned esgs_ring_size = max_gs_waves * 2 * wave_size *
				  es->esgs_itemsize * gs->gs_input_verts_per_prim;
	unsigned gsvs_ring_size = max_gs_waves * 2 * wave_size *
				  gs->max_gsvs_emit_size;

	min_esgs_ring_size = align(min_esgs_ring_size, alignment);
	esgs_ring_size = align(esgs_ring_size, alignment);
	gsvs_ring_size = align(gsvs_ring_size, alignment);

	esgs_ring_size = CLAMP(esgs_ring_size, min_esgs_ring_size, max_size);
	gsvs_ring_size = MIN2(gsvs_ring_size, max_size);

	/* Some rings don't have to be allocated if shaders don't use them.
	 * (e.g. no varyings between ES and GS or GS and VS)
	 */
	bool update_esgs = esgs_ring_size &&
			   (!sctx->esgs_ring ||
			    sctx->esgs_ring->width0 < esgs_ring_size);
	bool update_gsvs = gsvs_ring_size &&
			   (!sctx->gsvs_ring ||
			    sctx->gsvs_ring->width0 < gsvs_ring_size);

	if (!update_esgs && !update_gsvs)
		return true;

	if (update_esgs) {
		pipe_resource_reference(&sctx->esgs_ring, NULL);
		sctx->esgs_ring = pipe_buffer_create(sctx->b.b.screen, 0,
						     PIPE_USAGE_DEFAULT,
						     esgs_ring_size);
		if (!sctx->esgs_ring)
			return false;
	}

	if (update_gsvs) {
		pipe_resource_reference(&sctx->gsvs_ring, NULL);
		sctx->gsvs_ring = pipe_buffer_create(sctx->b.b.screen, 0,
						     PIPE_USAGE_DEFAULT,
						     gsvs_ring_size);
		if (!sctx->gsvs_ring)
			return false;
	}

	/* Create the "init_config_gs_rings" state. */
	pm4 = CALLOC_STRUCT(si_pm4_state);
	if (!pm4)
		return false;

	if (sctx->b.chip_class >= CIK) {
		if (sctx->esgs_ring)
			si_pm4_set_reg(pm4, R_030900_VGT_ESGS_RING_SIZE,
				       sctx->esgs_ring->width0 / 256);
		if (sctx->gsvs_ring)
			si_pm4_set_reg(pm4, R_030904_VGT_GSVS_RING_SIZE,
				       sctx->gsvs_ring->width0 / 256);
	} else {
		if (sctx->esgs_ring)
			si_pm4_set_reg(pm4, R_0088C8_VGT_ESGS_RING_SIZE,
				       sctx->esgs_ring->width0 / 256);
		if (sctx->gsvs_ring)
			si_pm4_set_reg(pm4, R_0088CC_VGT_GSVS_RING_SIZE,
				       sctx->gsvs_ring->width0 / 256);
	}

	/* Set the state. */
	if (sctx->init_config_gs_rings)
		si_pm4_free_state(sctx, sctx->init_config_gs_rings, ~0);
	sctx->init_config_gs_rings = pm4;

	if (!sctx->init_config_has_vgt_flush) {
		si_init_config_add_vgt_flush(sctx);
		si_pm4_upload_indirect_buffer(sctx, sctx->init_config);
	}

	/* Flush the context to re-emit both init_config states. */
	sctx->b.initial_gfx_cs_size = 0; /* force flush */
	si_context_gfx_flush(sctx, RADEON_FLUSH_ASYNC, NULL);

	/* Set ring bindings. */
	if (sctx->esgs_ring) {
		si_set_ring_buffer(&sctx->b.b, SI_ES_RING_ESGS,
				   sctx->esgs_ring, 0, sctx->esgs_ring->width0,
				   true, true, 4, 64, 0);
		si_set_ring_buffer(&sctx->b.b, SI_GS_RING_ESGS,
				   sctx->esgs_ring, 0, sctx->esgs_ring->width0,
				   false, false, 0, 0, 0);
	}
	if (sctx->gsvs_ring) {
		si_set_ring_buffer(&sctx->b.b, SI_RING_GSVS,
				   sctx->gsvs_ring, 0, sctx->gsvs_ring->width0,
				   false, false, 0, 0, 0);
	}

	return true;
}

/**
 * @returns 1 if \p sel has been updated to use a new scratch buffer
 *          0 if not
 *          < 0 if there was a failure
 */
static int si_update_scratch_buffer(struct si_context *sctx,
				    struct si_shader *shader)
{
	uint64_t scratch_va = sctx->scratch_buffer->gpu_address;
	int r;

	if (!shader)
		return 0;

	/* This shader doesn't need a scratch buffer */
	if (shader->config.scratch_bytes_per_wave == 0)
		return 0;

	/* This shader is already configured to use the current
	 * scratch buffer. */
	if (shader->scratch_bo == sctx->scratch_buffer)
		return 0;

	assert(sctx->scratch_buffer);

	si_shader_apply_scratch_relocs(sctx, shader, &shader->config, scratch_va);

	/* Replace the shader bo with a new bo that has the relocs applied. */
	r = si_shader_binary_upload(sctx->screen, shader);
	if (r)
		return r;

	/* Update the shader state to use the new shader bo. */
	si_shader_init_pm4_state(sctx->screen, shader);

	r600_resource_reference(&shader->scratch_bo, sctx->scratch_buffer);

	return 1;
}

static unsigned si_get_current_scratch_buffer_size(struct si_context *sctx)
{
	return sctx->scratch_buffer ? sctx->scratch_buffer->b.b.width0 : 0;
}

static unsigned si_get_scratch_buffer_bytes_per_wave(struct si_shader *shader)
{
	return shader ? shader->config.scratch_bytes_per_wave : 0;
}

static unsigned si_get_max_scratch_bytes_per_wave(struct si_context *sctx)
{
	unsigned bytes = 0;

	bytes = MAX2(bytes, si_get_scratch_buffer_bytes_per_wave(sctx->ps_shader.current));
	bytes = MAX2(bytes, si_get_scratch_buffer_bytes_per_wave(sctx->gs_shader.current));
	bytes = MAX2(bytes, si_get_scratch_buffer_bytes_per_wave(sctx->vs_shader.current));
	bytes = MAX2(bytes, si_get_scratch_buffer_bytes_per_wave(sctx->tcs_shader.current));
	bytes = MAX2(bytes, si_get_scratch_buffer_bytes_per_wave(sctx->tes_shader.current));
	return bytes;
}

static bool si_update_spi_tmpring_size(struct si_context *sctx)
{
	unsigned current_scratch_buffer_size =
		si_get_current_scratch_buffer_size(sctx);
	unsigned scratch_bytes_per_wave =
		si_get_max_scratch_bytes_per_wave(sctx);
	unsigned scratch_needed_size = scratch_bytes_per_wave *
		sctx->scratch_waves;
	unsigned spi_tmpring_size;
	int r;

	if (scratch_needed_size > 0) {
		if (scratch_needed_size > current_scratch_buffer_size) {
			/* Create a bigger scratch buffer */
			r600_resource_reference(&sctx->scratch_buffer, NULL);

			sctx->scratch_buffer = (struct r600_resource*)
					pipe_buffer_create(&sctx->screen->b.b, 0,
	                                PIPE_USAGE_DEFAULT, scratch_needed_size);
			if (!sctx->scratch_buffer)
				return false;
			sctx->emit_scratch_reloc = true;
		}

		/* Update the shaders, so they are using the latest scratch.  The
		 * scratch buffer may have been changed since these shaders were
		 * last used, so we still need to try to update them, even if
		 * they require scratch buffers smaller than the current size.
		 */
		r = si_update_scratch_buffer(sctx, sctx->ps_shader.current);
		if (r < 0)
			return false;
		if (r == 1)
			si_pm4_bind_state(sctx, ps, sctx->ps_shader.current->pm4);

		r = si_update_scratch_buffer(sctx, sctx->gs_shader.current);
		if (r < 0)
			return false;
		if (r == 1)
			si_pm4_bind_state(sctx, gs, sctx->gs_shader.current->pm4);

		r = si_update_scratch_buffer(sctx, sctx->tcs_shader.current);
		if (r < 0)
			return false;
		if (r == 1)
			si_pm4_bind_state(sctx, hs, sctx->tcs_shader.current->pm4);

		/* VS can be bound as LS, ES, or VS. */
		r = si_update_scratch_buffer(sctx, sctx->vs_shader.current);
		if (r < 0)
			return false;
		if (r == 1) {
			if (sctx->tes_shader.current)
				si_pm4_bind_state(sctx, ls, sctx->vs_shader.current->pm4);
			else if (sctx->gs_shader.current)
				si_pm4_bind_state(sctx, es, sctx->vs_shader.current->pm4);
			else
				si_pm4_bind_state(sctx, vs, sctx->vs_shader.current->pm4);
		}

		/* TES can be bound as ES or VS. */
		r = si_update_scratch_buffer(sctx, sctx->tes_shader.current);
		if (r < 0)
			return false;
		if (r == 1) {
			if (sctx->gs_shader.current)
				si_pm4_bind_state(sctx, es, sctx->tes_shader.current->pm4);
			else
				si_pm4_bind_state(sctx, vs, sctx->tes_shader.current->pm4);
		}
	}

	/* The LLVM shader backend should be reporting aligned scratch_sizes. */
	assert((scratch_needed_size & ~0x3FF) == scratch_needed_size &&
		"scratch size should already be aligned correctly.");

	spi_tmpring_size = S_0286E8_WAVES(sctx->scratch_waves) |
			   S_0286E8_WAVESIZE(scratch_bytes_per_wave >> 10);
	if (spi_tmpring_size != sctx->spi_tmpring_size) {
		sctx->spi_tmpring_size = spi_tmpring_size;
		sctx->emit_scratch_reloc = true;
	}
	return true;
}

static void si_init_tess_factor_ring(struct si_context *sctx)
{
	bool double_offchip_buffers = sctx->b.chip_class >= CIK &&
				      sctx->b.family != CHIP_CARRIZO &&
				      sctx->b.family != CHIP_STONEY;
	unsigned max_offchip_buffers_per_se = double_offchip_buffers ? 128 : 64;
	unsigned max_offchip_buffers = max_offchip_buffers_per_se *
				       sctx->screen->b.info.max_se;
	unsigned offchip_granularity;

	switch (sctx->screen->tess_offchip_block_dw_size) {
	default:
		assert(0);
		/* fall through */
	case 8192:
		offchip_granularity = V_03093C_X_8K_DWORDS;
		break;
	case 4096:
		offchip_granularity = V_03093C_X_4K_DWORDS;
		break;
	}

	switch (sctx->b.chip_class) {
	case SI:
		max_offchip_buffers = MIN2(max_offchip_buffers, 126);
		break;
	case CIK:
		max_offchip_buffers = MIN2(max_offchip_buffers, 508);
		break;
	case VI:
	default:
		max_offchip_buffers = MIN2(max_offchip_buffers, 512);
		break;
	}

	assert(!sctx->tf_ring);
	sctx->tf_ring = pipe_buffer_create(sctx->b.b.screen, 0,
					   PIPE_USAGE_DEFAULT,
					   32768 * sctx->screen->b.info.max_se);
	if (!sctx->tf_ring)
		return;

	assert(((sctx->tf_ring->width0 / 4) & C_030938_SIZE) == 0);

	sctx->tess_offchip_ring = pipe_buffer_create(sctx->b.b.screen, 0,
	                                             PIPE_USAGE_DEFAULT,
	                                             max_offchip_buffers *
	                                             sctx->screen->tess_offchip_block_dw_size * 4);
	if (!sctx->tess_offchip_ring)
		return;

	si_init_config_add_vgt_flush(sctx);

	/* Append these registers to the init config state. */
	if (sctx->b.chip_class >= CIK) {
		if (sctx->b.chip_class >= VI)
			--max_offchip_buffers;

		si_pm4_set_reg(sctx->init_config, R_030938_VGT_TF_RING_SIZE,
			       S_030938_SIZE(sctx->tf_ring->width0 / 4));
		si_pm4_set_reg(sctx->init_config, R_030940_VGT_TF_MEMORY_BASE,
			       r600_resource(sctx->tf_ring)->gpu_address >> 8);
		si_pm4_set_reg(sctx->init_config, R_03093C_VGT_HS_OFFCHIP_PARAM,
		             S_03093C_OFFCHIP_BUFFERING(max_offchip_buffers) |
		             S_03093C_OFFCHIP_GRANULARITY(offchip_granularity));
	} else {
		assert(offchip_granularity == V_03093C_X_8K_DWORDS);
		si_pm4_set_reg(sctx->init_config, R_008988_VGT_TF_RING_SIZE,
			       S_008988_SIZE(sctx->tf_ring->width0 / 4));
		si_pm4_set_reg(sctx->init_config, R_0089B8_VGT_TF_MEMORY_BASE,
			       r600_resource(sctx->tf_ring)->gpu_address >> 8);
		si_pm4_set_reg(sctx->init_config, R_0089B0_VGT_HS_OFFCHIP_PARAM,
		               S_0089B0_OFFCHIP_BUFFERING(max_offchip_buffers));
	}

	/* Flush the context to re-emit the init_config state.
	 * This is done only once in a lifetime of a context.
	 */
	si_pm4_upload_indirect_buffer(sctx, sctx->init_config);
	sctx->b.initial_gfx_cs_size = 0; /* force flush */
	si_context_gfx_flush(sctx, RADEON_FLUSH_ASYNC, NULL);

	si_set_ring_buffer(&sctx->b.b, SI_HS_RING_TESS_FACTOR, sctx->tf_ring,
			   0, sctx->tf_ring->width0, false, false, 0, 0, 0);

	si_set_ring_buffer(&sctx->b.b, SI_HS_RING_TESS_OFFCHIP,
	                   sctx->tess_offchip_ring, 0,
	                   sctx->tess_offchip_ring->width0, false, false, 0, 0, 0);
}

/**
 * This is used when TCS is NULL in the VS->TCS->TES chain. In this case,
 * VS passes its outputs to TES directly, so the fixed-function shader only
 * has to write TESSOUTER and TESSINNER.
 */
static void si_generate_fixed_func_tcs(struct si_context *sctx)
{
	struct ureg_src outer, inner;
	struct ureg_dst tessouter, tessinner;
	struct ureg_program *ureg = ureg_create(PIPE_SHADER_TESS_CTRL);

	if (!ureg)
		return; /* if we get here, we're screwed */

	assert(!sctx->fixed_func_tcs_shader.cso);

	outer = ureg_DECL_system_value(ureg,
				       TGSI_SEMANTIC_DEFAULT_TESSOUTER_SI, 0);
	inner = ureg_DECL_system_value(ureg,
				       TGSI_SEMANTIC_DEFAULT_TESSINNER_SI, 0);

	tessouter = ureg_DECL_output(ureg, TGSI_SEMANTIC_TESSOUTER, 0);
	tessinner = ureg_DECL_output(ureg, TGSI_SEMANTIC_TESSINNER, 0);

	ureg_MOV(ureg, tessouter, outer);
	ureg_MOV(ureg, tessinner, inner);
	ureg_END(ureg);

	sctx->fixed_func_tcs_shader.cso =
		ureg_create_shader_and_destroy(ureg, &sctx->b.b);
}

static void si_update_vgt_shader_config(struct si_context *sctx)
{
	/* Calculate the index of the config.
	 * 0 = VS, 1 = VS+GS, 2 = VS+Tess, 3 = VS+Tess+GS */
	unsigned index = 2*!!sctx->tes_shader.cso + !!sctx->gs_shader.cso;
	struct si_pm4_state **pm4 = &sctx->vgt_shader_config[index];

	if (!*pm4) {
		uint32_t stages = 0;

		*pm4 = CALLOC_STRUCT(si_pm4_state);

		if (sctx->tes_shader.cso) {
			stages |= S_028B54_LS_EN(V_028B54_LS_STAGE_ON) |
				  S_028B54_HS_EN(1) | S_028B54_DYNAMIC_HS(1);

			if (sctx->gs_shader.cso)
				stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_DS) |
					  S_028B54_GS_EN(1) |
				          S_028B54_VS_EN(V_028B54_VS_STAGE_COPY_SHADER);
			else
				stages |= S_028B54_VS_EN(V_028B54_VS_STAGE_DS);
		} else if (sctx->gs_shader.cso) {
			stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_REAL) |
				  S_028B54_GS_EN(1) |
			          S_028B54_VS_EN(V_028B54_VS_STAGE_COPY_SHADER);
		}

		si_pm4_set_reg(*pm4, R_028B54_VGT_SHADER_STAGES_EN, stages);
	}
	si_pm4_bind_state(sctx, vgt_shader_config, *pm4);
}

static void si_update_so(struct si_context *sctx, struct si_shader_selector *shader)
{
	struct pipe_stream_output_info *so = &shader->so;
	uint32_t enabled_stream_buffers_mask = 0;
	int i;

	for (i = 0; i < so->num_outputs; i++)
		enabled_stream_buffers_mask |= (1 << so->output[i].output_buffer) << (so->output[i].stream * 4);
	sctx->b.streamout.enabled_stream_buffers_mask = enabled_stream_buffers_mask;
	sctx->b.streamout.stride_in_dw = shader->so.stride;
}

bool si_update_shaders(struct si_context *sctx)
{
	struct pipe_context *ctx = (struct pipe_context*)sctx;
	struct si_compiler_ctx_state compiler_state;
	struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
	struct si_shader *old_vs = si_get_vs_state(sctx);
	bool old_clip_disable = old_vs ? old_vs->key.opt.hw_vs.clip_disable : false;
	int r;

	compiler_state.tm = sctx->tm;
	compiler_state.debug = sctx->b.debug;
	compiler_state.is_debug_context = sctx->is_debug;

	/* Update stages before GS. */
	if (sctx->tes_shader.cso) {
		if (!sctx->tf_ring) {
			si_init_tess_factor_ring(sctx);
			if (!sctx->tf_ring)
				return false;
		}

		/* VS as LS */
		r = si_shader_select(ctx, &sctx->vs_shader, &compiler_state);
		if (r)
			return false;
		si_pm4_bind_state(sctx, ls, sctx->vs_shader.current->pm4);

		if (sctx->tcs_shader.cso) {
			r = si_shader_select(ctx, &sctx->tcs_shader,
					     &compiler_state);
			if (r)
				return false;
			si_pm4_bind_state(sctx, hs, sctx->tcs_shader.current->pm4);
		} else {
			if (!sctx->fixed_func_tcs_shader.cso) {
				si_generate_fixed_func_tcs(sctx);
				if (!sctx->fixed_func_tcs_shader.cso)
					return false;
			}

			r = si_shader_select(ctx, &sctx->fixed_func_tcs_shader,
					     &compiler_state);
			if (r)
				return false;
			si_pm4_bind_state(sctx, hs,
					  sctx->fixed_func_tcs_shader.current->pm4);
		}

		r = si_shader_select(ctx, &sctx->tes_shader, &compiler_state);
		if (r)
			return false;

		if (sctx->gs_shader.cso) {
			/* TES as ES */
			si_pm4_bind_state(sctx, es, sctx->tes_shader.current->pm4);
		} else {
			/* TES as VS */
			si_pm4_bind_state(sctx, vs, sctx->tes_shader.current->pm4);
			si_update_so(sctx, sctx->tes_shader.cso);
		}
	} else if (sctx->gs_shader.cso) {
		/* VS as ES */
		r = si_shader_select(ctx, &sctx->vs_shader, &compiler_state);
		if (r)
			return false;
		si_pm4_bind_state(sctx, es, sctx->vs_shader.current->pm4);
	} else {
		/* VS as VS */
		r = si_shader_select(ctx, &sctx->vs_shader, &compiler_state);
		if (r)
			return false;
		si_pm4_bind_state(sctx, vs, sctx->vs_shader.current->pm4);
		si_update_so(sctx, sctx->vs_shader.cso);
	}

	/* Update GS. */
	if (sctx->gs_shader.cso) {
		r = si_shader_select(ctx, &sctx->gs_shader, &compiler_state);
		if (r)
			return false;
		si_pm4_bind_state(sctx, gs, sctx->gs_shader.current->pm4);
		si_pm4_bind_state(sctx, vs, sctx->gs_shader.cso->gs_copy_shader->pm4);
		si_update_so(sctx, sctx->gs_shader.cso);

		if (!si_update_gs_ring_buffers(sctx))
			return false;
	} else {
		si_pm4_bind_state(sctx, gs, NULL);
		si_pm4_bind_state(sctx, es, NULL);
	}

	si_update_vgt_shader_config(sctx);

	if (old_clip_disable != si_get_vs_state(sctx)->key.opt.hw_vs.clip_disable)
		si_mark_atom_dirty(sctx, &sctx->clip_regs);

	if (sctx->ps_shader.cso) {
		unsigned db_shader_control;

		r = si_shader_select(ctx, &sctx->ps_shader, &compiler_state);
		if (r)
			return false;
		si_pm4_bind_state(sctx, ps, sctx->ps_shader.current->pm4);

		db_shader_control =
			sctx->ps_shader.cso->db_shader_control |
			S_02880C_KILL_ENABLE(si_get_alpha_test_func(sctx) != PIPE_FUNC_ALWAYS);

		if (si_pm4_state_changed(sctx, ps) || si_pm4_state_changed(sctx, vs) ||
		    sctx->sprite_coord_enable != rs->sprite_coord_enable ||
		    sctx->flatshade != rs->flatshade) {
			sctx->sprite_coord_enable = rs->sprite_coord_enable;
			sctx->flatshade = rs->flatshade;
			si_mark_atom_dirty(sctx, &sctx->spi_map);
		}

		if (sctx->b.family == CHIP_STONEY && si_pm4_state_changed(sctx, ps))
			si_mark_atom_dirty(sctx, &sctx->cb_render_state);

		if (sctx->ps_db_shader_control != db_shader_control) {
			sctx->ps_db_shader_control = db_shader_control;
			si_mark_atom_dirty(sctx, &sctx->db_render_state);
		}

		if (sctx->smoothing_enabled != sctx->ps_shader.current->key.part.ps.epilog.poly_line_smoothing) {
			sctx->smoothing_enabled = sctx->ps_shader.current->key.part.ps.epilog.poly_line_smoothing;
			si_mark_atom_dirty(sctx, &sctx->msaa_config);

			if (sctx->b.chip_class == SI)
				si_mark_atom_dirty(sctx, &sctx->db_render_state);

			if (sctx->framebuffer.nr_samples <= 1)
				si_mark_atom_dirty(sctx, &sctx->msaa_sample_locs.atom);
		}
	}

	if (si_pm4_state_changed(sctx, ls) ||
	    si_pm4_state_changed(sctx, hs) ||
	    si_pm4_state_changed(sctx, es) ||
	    si_pm4_state_changed(sctx, gs) ||
	    si_pm4_state_changed(sctx, vs) ||
	    si_pm4_state_changed(sctx, ps)) {
		if (!si_update_spi_tmpring_size(sctx))
			return false;
	}

	sctx->do_update_shaders = false;
	return true;
}

void si_init_shader_functions(struct si_context *sctx)
{
	si_init_atom(sctx, &sctx->spi_map, &sctx->atoms.s.spi_map, si_emit_spi_map);

	sctx->b.b.create_vs_state = si_create_shader_selector;
	sctx->b.b.create_tcs_state = si_create_shader_selector;
	sctx->b.b.create_tes_state = si_create_shader_selector;
	sctx->b.b.create_gs_state = si_create_shader_selector;
	sctx->b.b.create_fs_state = si_create_shader_selector;

	sctx->b.b.bind_vs_state = si_bind_vs_shader;
	sctx->b.b.bind_tcs_state = si_bind_tcs_shader;
	sctx->b.b.bind_tes_state = si_bind_tes_shader;
	sctx->b.b.bind_gs_state = si_bind_gs_shader;
	sctx->b.b.bind_fs_state = si_bind_ps_shader;

	sctx->b.b.delete_vs_state = si_delete_shader_selector;
	sctx->b.b.delete_tcs_state = si_delete_shader_selector;
	sctx->b.b.delete_tes_state = si_delete_shader_selector;
	sctx->b.b.delete_gs_state = si_delete_shader_selector;
	sctx->b.b.delete_fs_state = si_delete_shader_selector;
}
