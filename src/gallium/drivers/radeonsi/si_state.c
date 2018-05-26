/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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

#include "si_build_pm4.h"
#include "gfx9d.h"
#include "si_query.h"

#include "util/u_dual_blend.h"
#include "util/u_format.h"
#include "util/u_format_s3tc.h"
#include "util/u_memory.h"
#include "util/u_resource.h"
#include "util/u_upload_mgr.h"

/* Initialize an external atom (owned by ../radeon). */
static void
si_init_external_atom(struct si_context *sctx, struct r600_atom *atom,
		      struct r600_atom **list_elem)
{
	atom->id = list_elem - sctx->atoms.array;
	*list_elem = atom;
}

/* Initialize an atom owned by radeonsi.  */
void si_init_atom(struct si_context *sctx, struct r600_atom *atom,
		  struct r600_atom **list_elem,
		  void (*emit_func)(struct si_context *ctx, struct r600_atom *state))
{
	atom->emit = emit_func;
	atom->id = list_elem - sctx->atoms.array;
	*list_elem = atom;
}

static unsigned si_map_swizzle(unsigned swizzle)
{
	switch (swizzle) {
	case PIPE_SWIZZLE_Y:
		return V_008F0C_SQ_SEL_Y;
	case PIPE_SWIZZLE_Z:
		return V_008F0C_SQ_SEL_Z;
	case PIPE_SWIZZLE_W:
		return V_008F0C_SQ_SEL_W;
	case PIPE_SWIZZLE_0:
		return V_008F0C_SQ_SEL_0;
	case PIPE_SWIZZLE_1:
		return V_008F0C_SQ_SEL_1;
	default: /* PIPE_SWIZZLE_X */
		return V_008F0C_SQ_SEL_X;
	}
}

/* 12.4 fixed-point */
static unsigned si_pack_float_12p4(float x)
{
	return x <= 0    ? 0 :
	       x >= 4096 ? 0xffff : x * 16;
}

/*
 * Inferred framebuffer and blender state.
 *
 * CB_TARGET_MASK is emitted here to avoid a hang with dual source blending
 * if there is not enough PS outputs.
 */
static void si_emit_cb_render_state(struct si_context *sctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = sctx->gfx_cs;
	struct si_state_blend *blend = sctx->queued.named.blend;
	/* CB_COLORn_INFO.FORMAT=INVALID should disable unbound colorbuffers,
	 * but you never know. */
	uint32_t cb_target_mask = sctx->framebuffer.colorbuf_enabled_4bit;
	unsigned i;

	if (blend)
		cb_target_mask &= blend->cb_target_mask;

	/* Avoid a hang that happens when dual source blending is enabled
	 * but there is not enough color outputs. This is undefined behavior,
	 * so disable color writes completely.
	 *
	 * Reproducible with Unigine Heaven 4.0 and drirc missing.
	 */
	if (blend && blend->dual_src_blend &&
	    sctx->ps_shader.cso &&
	    (sctx->ps_shader.cso->info.colors_written & 0x3) != 0x3)
		cb_target_mask = 0;

	radeon_set_context_reg(cs, R_028238_CB_TARGET_MASK, cb_target_mask);

	/* GFX9: Flush DFSM when CB_TARGET_MASK changes.
	 * I think we don't have to do anything between IBs.
	 */
	if (sctx->screen->dfsm_allowed &&
	    sctx->last_cb_target_mask != cb_target_mask) {
		sctx->last_cb_target_mask = cb_target_mask;

		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_FLUSH_DFSM) | EVENT_INDEX(0));
	}

	if (sctx->chip_class >= VI) {
		/* DCC MSAA workaround for blending.
		 * Alternatively, we can set CB_COLORi_DCC_CONTROL.OVERWRITE_-
		 * COMBINER_DISABLE, but that would be more complicated.
		 */
		bool oc_disable = (sctx->chip_class == VI ||
				   sctx->chip_class == GFX9) &&
				  blend &&
				  blend->blend_enable_4bit & cb_target_mask &&
				  sctx->framebuffer.nr_samples >= 2;

		radeon_set_context_reg(cs, R_028424_CB_DCC_CONTROL,
				       S_028424_OVERWRITE_COMBINER_MRT_SHARING_DISABLE(1) |
				       S_028424_OVERWRITE_COMBINER_WATERMARK(4) |
				       S_028424_OVERWRITE_COMBINER_DISABLE(oc_disable));
	}

	/* RB+ register settings. */
	if (sctx->screen->rbplus_allowed) {
		unsigned spi_shader_col_format =
			sctx->ps_shader.cso ?
			sctx->ps_shader.current->key.part.ps.epilog.spi_shader_col_format : 0;
		unsigned sx_ps_downconvert = 0;
		unsigned sx_blend_opt_epsilon = 0;
		unsigned sx_blend_opt_control = 0;

		for (i = 0; i < sctx->framebuffer.state.nr_cbufs; i++) {
			struct r600_surface *surf =
				(struct r600_surface*)sctx->framebuffer.state.cbufs[i];
			unsigned format, swap, spi_format, colormask;
			bool has_alpha, has_rgb;

			if (!surf)
				continue;

			format = G_028C70_FORMAT(surf->cb_color_info);
			swap = G_028C70_COMP_SWAP(surf->cb_color_info);
			spi_format = (spi_shader_col_format >> (i * 4)) & 0xf;
			colormask = (cb_target_mask >> (i * 4)) & 0xf;

			/* Set if RGB and A are present. */
			has_alpha = !G_028C74_FORCE_DST_ALPHA_1(surf->cb_color_attrib);

			if (format == V_028C70_COLOR_8 ||
			    format == V_028C70_COLOR_16 ||
			    format == V_028C70_COLOR_32)
				has_rgb = !has_alpha;
			else
				has_rgb = true;

			/* Check the colormask and export format. */
			if (!(colormask & (PIPE_MASK_RGBA & ~PIPE_MASK_A)))
				has_rgb = false;
			if (!(colormask & PIPE_MASK_A))
				has_alpha = false;

			if (spi_format == V_028714_SPI_SHADER_ZERO) {
				has_rgb = false;
				has_alpha = false;
			}

			/* Disable value checking for disabled channels. */
			if (!has_rgb)
				sx_blend_opt_control |= S_02875C_MRT0_COLOR_OPT_DISABLE(1) << (i * 4);
			if (!has_alpha)
				sx_blend_opt_control |= S_02875C_MRT0_ALPHA_OPT_DISABLE(1) << (i * 4);

			/* Enable down-conversion for 32bpp and smaller formats. */
			switch (format) {
			case V_028C70_COLOR_8:
			case V_028C70_COLOR_8_8:
			case V_028C70_COLOR_8_8_8_8:
				/* For 1 and 2-channel formats, use the superset thereof. */
				if (spi_format == V_028714_SPI_SHADER_FP16_ABGR ||
				    spi_format == V_028714_SPI_SHADER_UINT16_ABGR ||
				    spi_format == V_028714_SPI_SHADER_SINT16_ABGR) {
					sx_ps_downconvert |= V_028754_SX_RT_EXPORT_8_8_8_8 << (i * 4);
					sx_blend_opt_epsilon |= V_028758_8BIT_FORMAT << (i * 4);
				}
				break;

			case V_028C70_COLOR_5_6_5:
				if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
					sx_ps_downconvert |= V_028754_SX_RT_EXPORT_5_6_5 << (i * 4);
					sx_blend_opt_epsilon |= V_028758_6BIT_FORMAT << (i * 4);
				}
				break;

			case V_028C70_COLOR_1_5_5_5:
				if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
					sx_ps_downconvert |= V_028754_SX_RT_EXPORT_1_5_5_5 << (i * 4);
					sx_blend_opt_epsilon |= V_028758_5BIT_FORMAT << (i * 4);
				}
				break;

			case V_028C70_COLOR_4_4_4_4:
				if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
					sx_ps_downconvert |= V_028754_SX_RT_EXPORT_4_4_4_4 << (i * 4);
					sx_blend_opt_epsilon |= V_028758_4BIT_FORMAT << (i * 4);
				}
				break;

			case V_028C70_COLOR_32:
				if (swap == V_028C70_SWAP_STD &&
				    spi_format == V_028714_SPI_SHADER_32_R)
					sx_ps_downconvert |= V_028754_SX_RT_EXPORT_32_R << (i * 4);
				else if (swap == V_028C70_SWAP_ALT_REV &&
					 spi_format == V_028714_SPI_SHADER_32_AR)
					sx_ps_downconvert |= V_028754_SX_RT_EXPORT_32_A << (i * 4);
				break;

			case V_028C70_COLOR_16:
			case V_028C70_COLOR_16_16:
				/* For 1-channel formats, use the superset thereof. */
				if (spi_format == V_028714_SPI_SHADER_UNORM16_ABGR ||
				    spi_format == V_028714_SPI_SHADER_SNORM16_ABGR ||
				    spi_format == V_028714_SPI_SHADER_UINT16_ABGR ||
				    spi_format == V_028714_SPI_SHADER_SINT16_ABGR) {
					if (swap == V_028C70_SWAP_STD ||
					    swap == V_028C70_SWAP_STD_REV)
						sx_ps_downconvert |= V_028754_SX_RT_EXPORT_16_16_GR << (i * 4);
					else
						sx_ps_downconvert |= V_028754_SX_RT_EXPORT_16_16_AR << (i * 4);
				}
				break;

			case V_028C70_COLOR_10_11_11:
				if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
					sx_ps_downconvert |= V_028754_SX_RT_EXPORT_10_11_11 << (i * 4);
					sx_blend_opt_epsilon |= V_028758_11BIT_FORMAT << (i * 4);
				}
				break;

			case V_028C70_COLOR_2_10_10_10:
				if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
					sx_ps_downconvert |= V_028754_SX_RT_EXPORT_2_10_10_10 << (i * 4);
					sx_blend_opt_epsilon |= V_028758_10BIT_FORMAT << (i * 4);
				}
				break;
			}
		}

		radeon_set_context_reg_seq(cs, R_028754_SX_PS_DOWNCONVERT, 3);
		radeon_emit(cs, sx_ps_downconvert);	/* R_028754_SX_PS_DOWNCONVERT */
		radeon_emit(cs, sx_blend_opt_epsilon);	/* R_028758_SX_BLEND_OPT_EPSILON */
		radeon_emit(cs, sx_blend_opt_control);	/* R_02875C_SX_BLEND_OPT_CONTROL */
	}
}

/*
 * Blender functions
 */

static uint32_t si_translate_blend_function(int blend_func)
{
	switch (blend_func) {
	case PIPE_BLEND_ADD:
		return V_028780_COMB_DST_PLUS_SRC;
	case PIPE_BLEND_SUBTRACT:
		return V_028780_COMB_SRC_MINUS_DST;
	case PIPE_BLEND_REVERSE_SUBTRACT:
		return V_028780_COMB_DST_MINUS_SRC;
	case PIPE_BLEND_MIN:
		return V_028780_COMB_MIN_DST_SRC;
	case PIPE_BLEND_MAX:
		return V_028780_COMB_MAX_DST_SRC;
	default:
		PRINT_ERR("Unknown blend function %d\n", blend_func);
		assert(0);
		break;
	}
	return 0;
}

static uint32_t si_translate_blend_factor(int blend_fact)
{
	switch (blend_fact) {
	case PIPE_BLENDFACTOR_ONE:
		return V_028780_BLEND_ONE;
	case PIPE_BLENDFACTOR_SRC_COLOR:
		return V_028780_BLEND_SRC_COLOR;
	case PIPE_BLENDFACTOR_SRC_ALPHA:
		return V_028780_BLEND_SRC_ALPHA;
	case PIPE_BLENDFACTOR_DST_ALPHA:
		return V_028780_BLEND_DST_ALPHA;
	case PIPE_BLENDFACTOR_DST_COLOR:
		return V_028780_BLEND_DST_COLOR;
	case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
		return V_028780_BLEND_SRC_ALPHA_SATURATE;
	case PIPE_BLENDFACTOR_CONST_COLOR:
		return V_028780_BLEND_CONSTANT_COLOR;
	case PIPE_BLENDFACTOR_CONST_ALPHA:
		return V_028780_BLEND_CONSTANT_ALPHA;
	case PIPE_BLENDFACTOR_ZERO:
		return V_028780_BLEND_ZERO;
	case PIPE_BLENDFACTOR_INV_SRC_COLOR:
		return V_028780_BLEND_ONE_MINUS_SRC_COLOR;
	case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
		return V_028780_BLEND_ONE_MINUS_SRC_ALPHA;
	case PIPE_BLENDFACTOR_INV_DST_ALPHA:
		return V_028780_BLEND_ONE_MINUS_DST_ALPHA;
	case PIPE_BLENDFACTOR_INV_DST_COLOR:
		return V_028780_BLEND_ONE_MINUS_DST_COLOR;
	case PIPE_BLENDFACTOR_INV_CONST_COLOR:
		return V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR;
	case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
		return V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA;
	case PIPE_BLENDFACTOR_SRC1_COLOR:
		return V_028780_BLEND_SRC1_COLOR;
	case PIPE_BLENDFACTOR_SRC1_ALPHA:
		return V_028780_BLEND_SRC1_ALPHA;
	case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
		return V_028780_BLEND_INV_SRC1_COLOR;
	case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
		return V_028780_BLEND_INV_SRC1_ALPHA;
	default:
		PRINT_ERR("Bad blend factor %d not supported!\n", blend_fact);
		assert(0);
		break;
	}
	return 0;
}

static uint32_t si_translate_blend_opt_function(int blend_func)
{
	switch (blend_func) {
	case PIPE_BLEND_ADD:
		return V_028760_OPT_COMB_ADD;
	case PIPE_BLEND_SUBTRACT:
		return V_028760_OPT_COMB_SUBTRACT;
	case PIPE_BLEND_REVERSE_SUBTRACT:
		return V_028760_OPT_COMB_REVSUBTRACT;
	case PIPE_BLEND_MIN:
		return V_028760_OPT_COMB_MIN;
	case PIPE_BLEND_MAX:
		return V_028760_OPT_COMB_MAX;
	default:
		return V_028760_OPT_COMB_BLEND_DISABLED;
	}
}

static uint32_t si_translate_blend_opt_factor(int blend_fact, bool is_alpha)
{
	switch (blend_fact) {
	case PIPE_BLENDFACTOR_ZERO:
		return V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_ALL;
	case PIPE_BLENDFACTOR_ONE:
		return V_028760_BLEND_OPT_PRESERVE_ALL_IGNORE_NONE;
	case PIPE_BLENDFACTOR_SRC_COLOR:
		return is_alpha ? V_028760_BLEND_OPT_PRESERVE_A1_IGNORE_A0
				: V_028760_BLEND_OPT_PRESERVE_C1_IGNORE_C0;
	case PIPE_BLENDFACTOR_INV_SRC_COLOR:
		return is_alpha ? V_028760_BLEND_OPT_PRESERVE_A0_IGNORE_A1
				: V_028760_BLEND_OPT_PRESERVE_C0_IGNORE_C1;
	case PIPE_BLENDFACTOR_SRC_ALPHA:
		return V_028760_BLEND_OPT_PRESERVE_A1_IGNORE_A0;
	case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
		return V_028760_BLEND_OPT_PRESERVE_A0_IGNORE_A1;
	case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
		return is_alpha ? V_028760_BLEND_OPT_PRESERVE_ALL_IGNORE_NONE
				: V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_A0;
	default:
		return V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;
	}
}

static void si_blend_check_commutativity(struct si_screen *sscreen,
					 struct si_state_blend *blend,
					 enum pipe_blend_func func,
					 enum pipe_blendfactor src,
					 enum pipe_blendfactor dst,
					 unsigned chanmask)
{
	/* Src factor is allowed when it does not depend on Dst */
	static const uint32_t src_allowed =
		(1u << PIPE_BLENDFACTOR_ONE) |
		(1u << PIPE_BLENDFACTOR_SRC_COLOR) |
		(1u << PIPE_BLENDFACTOR_SRC_ALPHA) |
		(1u << PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE) |
		(1u << PIPE_BLENDFACTOR_CONST_COLOR) |
		(1u << PIPE_BLENDFACTOR_CONST_ALPHA) |
		(1u << PIPE_BLENDFACTOR_SRC1_COLOR) |
		(1u << PIPE_BLENDFACTOR_SRC1_ALPHA) |
		(1u << PIPE_BLENDFACTOR_ZERO) |
		(1u << PIPE_BLENDFACTOR_INV_SRC_COLOR) |
		(1u << PIPE_BLENDFACTOR_INV_SRC_ALPHA) |
		(1u << PIPE_BLENDFACTOR_INV_CONST_COLOR) |
		(1u << PIPE_BLENDFACTOR_INV_CONST_ALPHA) |
		(1u << PIPE_BLENDFACTOR_INV_SRC1_COLOR) |
		(1u << PIPE_BLENDFACTOR_INV_SRC1_ALPHA);

	if (dst == PIPE_BLENDFACTOR_ONE &&
	    (src_allowed & (1u << src))) {
		/* Addition is commutative, but floating point addition isn't
		 * associative: subtle changes can be introduced via different
		 * rounding.
		 *
		 * Out-of-order is also non-deterministic, which means that
		 * this breaks OpenGL invariance requirements. So only enable
		 * out-of-order additive blending if explicitly allowed by a
		 * setting.
		 */
		if (func == PIPE_BLEND_MAX || func == PIPE_BLEND_MIN ||
		    (func == PIPE_BLEND_ADD && sscreen->commutative_blend_add))
			blend->commutative_4bit |= chanmask;
	}
}

/**
 * Get rid of DST in the blend factors by commuting the operands:
 *    func(src * DST, dst * 0) ---> func(src * 0, dst * SRC)
 */
static void si_blend_remove_dst(unsigned *func, unsigned *src_factor,
				unsigned *dst_factor, unsigned expected_dst,
				unsigned replacement_src)
{
	if (*src_factor == expected_dst &&
	    *dst_factor == PIPE_BLENDFACTOR_ZERO) {
		*src_factor = PIPE_BLENDFACTOR_ZERO;
		*dst_factor = replacement_src;

		/* Commuting the operands requires reversing subtractions. */
		if (*func == PIPE_BLEND_SUBTRACT)
			*func = PIPE_BLEND_REVERSE_SUBTRACT;
		else if (*func == PIPE_BLEND_REVERSE_SUBTRACT)
			*func = PIPE_BLEND_SUBTRACT;
	}
}

static bool si_blend_factor_uses_dst(unsigned factor)
{
	return factor == PIPE_BLENDFACTOR_DST_COLOR ||
		factor == PIPE_BLENDFACTOR_DST_ALPHA ||
		factor == PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE ||
		factor == PIPE_BLENDFACTOR_INV_DST_ALPHA ||
		factor == PIPE_BLENDFACTOR_INV_DST_COLOR;
}

static void *si_create_blend_state_mode(struct pipe_context *ctx,
					const struct pipe_blend_state *state,
					unsigned mode)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct si_state_blend *blend = CALLOC_STRUCT(si_state_blend);
	struct si_pm4_state *pm4 = &blend->pm4;
	uint32_t sx_mrt_blend_opt[8] = {0};
	uint32_t color_control = 0;

	if (!blend)
		return NULL;

	blend->alpha_to_coverage = state->alpha_to_coverage;
	blend->alpha_to_one = state->alpha_to_one;
	blend->dual_src_blend = util_blend_state_is_dual(state, 0);
	blend->logicop_enable = state->logicop_enable;

	if (state->logicop_enable) {
		color_control |= S_028808_ROP3(state->logicop_func | (state->logicop_func << 4));
	} else {
		color_control |= S_028808_ROP3(0xcc);
	}

	si_pm4_set_reg(pm4, R_028B70_DB_ALPHA_TO_MASK,
		       S_028B70_ALPHA_TO_MASK_ENABLE(state->alpha_to_coverage) |
		       S_028B70_ALPHA_TO_MASK_OFFSET0(2) |
		       S_028B70_ALPHA_TO_MASK_OFFSET1(2) |
		       S_028B70_ALPHA_TO_MASK_OFFSET2(2) |
		       S_028B70_ALPHA_TO_MASK_OFFSET3(2));

	if (state->alpha_to_coverage)
		blend->need_src_alpha_4bit |= 0xf;

	blend->cb_target_mask = 0;
	blend->cb_target_enabled_4bit = 0;

	for (int i = 0; i < 8; i++) {
		/* state->rt entries > 0 only written if independent blending */
		const int j = state->independent_blend_enable ? i : 0;

		unsigned eqRGB = state->rt[j].rgb_func;
		unsigned srcRGB = state->rt[j].rgb_src_factor;
		unsigned dstRGB = state->rt[j].rgb_dst_factor;
		unsigned eqA = state->rt[j].alpha_func;
		unsigned srcA = state->rt[j].alpha_src_factor;
		unsigned dstA = state->rt[j].alpha_dst_factor;

		unsigned srcRGB_opt, dstRGB_opt, srcA_opt, dstA_opt;
		unsigned blend_cntl = 0;

		sx_mrt_blend_opt[i] =
			S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED) |
			S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED);

		/* Only set dual source blending for MRT0 to avoid a hang. */
		if (i >= 1 && blend->dual_src_blend) {
			/* Vulkan does this for dual source blending. */
			if (i == 1)
				blend_cntl |= S_028780_ENABLE(1);

			si_pm4_set_reg(pm4, R_028780_CB_BLEND0_CONTROL + i * 4, blend_cntl);
			continue;
		}

		/* Only addition and subtraction equations are supported with
		 * dual source blending.
		 */
		if (blend->dual_src_blend &&
		    (eqRGB == PIPE_BLEND_MIN || eqRGB == PIPE_BLEND_MAX ||
		     eqA == PIPE_BLEND_MIN || eqA == PIPE_BLEND_MAX)) {
			assert(!"Unsupported equation for dual source blending");
			si_pm4_set_reg(pm4, R_028780_CB_BLEND0_CONTROL + i * 4, blend_cntl);
			continue;
		}

		/* cb_render_state will disable unused ones */
		blend->cb_target_mask |= (unsigned)state->rt[j].colormask << (4 * i);
		if (state->rt[j].colormask)
			blend->cb_target_enabled_4bit |= 0xf << (4 * i);

		if (!state->rt[j].colormask || !state->rt[j].blend_enable) {
			si_pm4_set_reg(pm4, R_028780_CB_BLEND0_CONTROL + i * 4, blend_cntl);
			continue;
		}

		si_blend_check_commutativity(sctx->screen, blend,
					     eqRGB, srcRGB, dstRGB, 0x7 << (4 * i));
		si_blend_check_commutativity(sctx->screen, blend,
					     eqA, srcA, dstA, 0x8 << (4 * i));

		/* Blending optimizations for RB+.
		 * These transformations don't change the behavior.
		 *
		 * First, get rid of DST in the blend factors:
		 *    func(src * DST, dst * 0) ---> func(src * 0, dst * SRC)
		 */
		si_blend_remove_dst(&eqRGB, &srcRGB, &dstRGB,
				    PIPE_BLENDFACTOR_DST_COLOR,
				    PIPE_BLENDFACTOR_SRC_COLOR);
		si_blend_remove_dst(&eqA, &srcA, &dstA,
				    PIPE_BLENDFACTOR_DST_COLOR,
				    PIPE_BLENDFACTOR_SRC_COLOR);
		si_blend_remove_dst(&eqA, &srcA, &dstA,
				    PIPE_BLENDFACTOR_DST_ALPHA,
				    PIPE_BLENDFACTOR_SRC_ALPHA);

		/* Look up the ideal settings from tables. */
		srcRGB_opt = si_translate_blend_opt_factor(srcRGB, false);
		dstRGB_opt = si_translate_blend_opt_factor(dstRGB, false);
		srcA_opt = si_translate_blend_opt_factor(srcA, true);
		dstA_opt = si_translate_blend_opt_factor(dstA, true);

		/* Handle interdependencies. */
		if (si_blend_factor_uses_dst(srcRGB))
			dstRGB_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;
		if (si_blend_factor_uses_dst(srcA))
			dstA_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;

		if (srcRGB == PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE &&
		    (dstRGB == PIPE_BLENDFACTOR_ZERO ||
		     dstRGB == PIPE_BLENDFACTOR_SRC_ALPHA ||
		     dstRGB == PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE))
			dstRGB_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_A0;

		/* Set the final value. */
		sx_mrt_blend_opt[i] =
			S_028760_COLOR_SRC_OPT(srcRGB_opt) |
			S_028760_COLOR_DST_OPT(dstRGB_opt) |
			S_028760_COLOR_COMB_FCN(si_translate_blend_opt_function(eqRGB)) |
			S_028760_ALPHA_SRC_OPT(srcA_opt) |
			S_028760_ALPHA_DST_OPT(dstA_opt) |
			S_028760_ALPHA_COMB_FCN(si_translate_blend_opt_function(eqA));

		/* Set blend state. */
		blend_cntl |= S_028780_ENABLE(1);
		blend_cntl |= S_028780_COLOR_COMB_FCN(si_translate_blend_function(eqRGB));
		blend_cntl |= S_028780_COLOR_SRCBLEND(si_translate_blend_factor(srcRGB));
		blend_cntl |= S_028780_COLOR_DESTBLEND(si_translate_blend_factor(dstRGB));

		if (srcA != srcRGB || dstA != dstRGB || eqA != eqRGB) {
			blend_cntl |= S_028780_SEPARATE_ALPHA_BLEND(1);
			blend_cntl |= S_028780_ALPHA_COMB_FCN(si_translate_blend_function(eqA));
			blend_cntl |= S_028780_ALPHA_SRCBLEND(si_translate_blend_factor(srcA));
			blend_cntl |= S_028780_ALPHA_DESTBLEND(si_translate_blend_factor(dstA));
		}
		si_pm4_set_reg(pm4, R_028780_CB_BLEND0_CONTROL + i * 4, blend_cntl);

		blend->blend_enable_4bit |= 0xfu << (i * 4);

		/* This is only important for formats without alpha. */
		if (srcRGB == PIPE_BLENDFACTOR_SRC_ALPHA ||
		    dstRGB == PIPE_BLENDFACTOR_SRC_ALPHA ||
		    srcRGB == PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE ||
		    dstRGB == PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE ||
		    srcRGB == PIPE_BLENDFACTOR_INV_SRC_ALPHA ||
		    dstRGB == PIPE_BLENDFACTOR_INV_SRC_ALPHA)
			blend->need_src_alpha_4bit |= 0xfu << (i * 4);
	}

	if (blend->cb_target_mask) {
		color_control |= S_028808_MODE(mode);
	} else {
		color_control |= S_028808_MODE(V_028808_CB_DISABLE);
	}

	if (sctx->screen->rbplus_allowed) {
		/* Disable RB+ blend optimizations for dual source blending.
		 * Vulkan does this.
		 */
		if (blend->dual_src_blend) {
			for (int i = 0; i < 8; i++) {
				sx_mrt_blend_opt[i] =
					S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_NONE) |
					S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_NONE);
			}
		}

		for (int i = 0; i < 8; i++)
			si_pm4_set_reg(pm4, R_028760_SX_MRT0_BLEND_OPT + i * 4,
				       sx_mrt_blend_opt[i]);

		/* RB+ doesn't work with dual source blending, logic op, and RESOLVE. */
		if (blend->dual_src_blend || state->logicop_enable ||
		    mode == V_028808_CB_RESOLVE)
			color_control |= S_028808_DISABLE_DUAL_QUAD(1);
	}

	si_pm4_set_reg(pm4, R_028808_CB_COLOR_CONTROL, color_control);
	return blend;
}

static void *si_create_blend_state(struct pipe_context *ctx,
				   const struct pipe_blend_state *state)
{
	return si_create_blend_state_mode(ctx, state, V_028808_CB_NORMAL);
}

static void si_bind_blend_state(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_state_blend *old_blend = sctx->queued.named.blend;
	struct si_state_blend *blend = (struct si_state_blend *)state;

	if (!state)
		return;

	si_pm4_bind_state(sctx, blend, state);

	if (!old_blend ||
	    old_blend->cb_target_mask != blend->cb_target_mask ||
	    old_blend->dual_src_blend != blend->dual_src_blend ||
	    (old_blend->blend_enable_4bit != blend->blend_enable_4bit &&
	     sctx->framebuffer.nr_samples >= 2 &&
	     sctx->screen->dcc_msaa_allowed))
		si_mark_atom_dirty(sctx, &sctx->cb_render_state);

	if (!old_blend ||
	    old_blend->cb_target_mask != blend->cb_target_mask ||
	    old_blend->alpha_to_coverage != blend->alpha_to_coverage ||
	    old_blend->alpha_to_one != blend->alpha_to_one ||
	    old_blend->dual_src_blend != blend->dual_src_blend ||
	    old_blend->blend_enable_4bit != blend->blend_enable_4bit ||
	    old_blend->need_src_alpha_4bit != blend->need_src_alpha_4bit)
		sctx->do_update_shaders = true;

	if (sctx->screen->dpbb_allowed &&
	    (!old_blend ||
	     old_blend->alpha_to_coverage != blend->alpha_to_coverage ||
	     old_blend->blend_enable_4bit != blend->blend_enable_4bit ||
	     old_blend->cb_target_enabled_4bit != blend->cb_target_enabled_4bit))
		si_mark_atom_dirty(sctx, &sctx->dpbb_state);

	if (sctx->screen->has_out_of_order_rast &&
	    (!old_blend ||
	     (old_blend->blend_enable_4bit != blend->blend_enable_4bit ||
	      old_blend->cb_target_enabled_4bit != blend->cb_target_enabled_4bit ||
	      old_blend->commutative_4bit != blend->commutative_4bit ||
	      old_blend->logicop_enable != blend->logicop_enable)))
		si_mark_atom_dirty(sctx, &sctx->msaa_config);
}

static void si_delete_blend_state(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	si_pm4_delete_state(sctx, blend, (struct si_state_blend *)state);
}

static void si_set_blend_color(struct pipe_context *ctx,
			       const struct pipe_blend_color *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	static const struct pipe_blend_color zeros;

	sctx->blend_color.state = *state;
	sctx->blend_color.any_nonzeros = memcmp(state, &zeros, sizeof(*state)) != 0;
	si_mark_atom_dirty(sctx, &sctx->blend_color.atom);
}

static void si_emit_blend_color(struct si_context *sctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = sctx->gfx_cs;

	radeon_set_context_reg_seq(cs, R_028414_CB_BLEND_RED, 4);
	radeon_emit_array(cs, (uint32_t*)sctx->blend_color.state.color, 4);
}

/*
 * Clipping
 */

static void si_set_clip_state(struct pipe_context *ctx,
			      const struct pipe_clip_state *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct pipe_constant_buffer cb;
	static const struct pipe_clip_state zeros;

	if (memcmp(&sctx->clip_state.state, state, sizeof(*state)) == 0)
		return;

	sctx->clip_state.state = *state;
	sctx->clip_state.any_nonzeros = memcmp(state, &zeros, sizeof(*state)) != 0;
	si_mark_atom_dirty(sctx, &sctx->clip_state.atom);

	cb.buffer = NULL;
	cb.user_buffer = state->ucp;
	cb.buffer_offset = 0;
	cb.buffer_size = 4*4*8;
	si_set_rw_buffer(sctx, SI_VS_CONST_CLIP_PLANES, &cb);
	pipe_resource_reference(&cb.buffer, NULL);
}

static void si_emit_clip_state(struct si_context *sctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = sctx->gfx_cs;

	radeon_set_context_reg_seq(cs, R_0285BC_PA_CL_UCP_0_X, 6*4);
	radeon_emit_array(cs, (uint32_t*)sctx->clip_state.state.ucp, 6*4);
}

static void si_emit_clip_regs(struct si_context *sctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = sctx->gfx_cs;
	struct si_shader *vs = si_get_vs_state(sctx);
	struct si_shader_selector *vs_sel = vs->selector;
	struct tgsi_shader_info *info = &vs_sel->info;
	struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
	unsigned window_space =
	   info->properties[TGSI_PROPERTY_VS_WINDOW_SPACE_POSITION];
	unsigned clipdist_mask = vs_sel->clipdist_mask;
	unsigned ucp_mask = clipdist_mask ? 0 : rs->clip_plane_enable & SIX_BITS;
	unsigned culldist_mask = vs_sel->culldist_mask;
	unsigned total_mask;

	if (vs->key.opt.clip_disable) {
		assert(!info->culldist_writemask);
		clipdist_mask = 0;
		culldist_mask = 0;
	}
	total_mask = clipdist_mask | culldist_mask;

	/* Clip distances on points have no effect, so need to be implemented
	 * as cull distances. This applies for the clipvertex case as well.
	 *
	 * Setting this for primitives other than points should have no adverse
	 * effects.
	 */
	clipdist_mask &= rs->clip_plane_enable;
	culldist_mask |= clipdist_mask;

	radeon_set_context_reg(cs, R_02881C_PA_CL_VS_OUT_CNTL,
		vs_sel->pa_cl_vs_out_cntl |
		S_02881C_VS_OUT_CCDIST0_VEC_ENA((total_mask & 0x0F) != 0) |
		S_02881C_VS_OUT_CCDIST1_VEC_ENA((total_mask & 0xF0) != 0) |
		clipdist_mask | (culldist_mask << 8));
	radeon_set_context_reg(cs, R_028810_PA_CL_CLIP_CNTL,
		rs->pa_cl_clip_cntl |
		ucp_mask |
		S_028810_CLIP_DISABLE(window_space));
}

/*
 * inferred state between framebuffer and rasterizer
 */
static void si_update_poly_offset_state(struct si_context *sctx)
{
	struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;

	if (!rs || !rs->uses_poly_offset || !sctx->framebuffer.state.zsbuf) {
		si_pm4_bind_state(sctx, poly_offset, NULL);
		return;
	}

	/* Use the user format, not db_render_format, so that the polygon
	 * offset behaves as expected by applications.
	 */
	switch (sctx->framebuffer.state.zsbuf->texture->format) {
	case PIPE_FORMAT_Z16_UNORM:
		si_pm4_bind_state(sctx, poly_offset, &rs->pm4_poly_offset[0]);
		break;
	default: /* 24-bit */
		si_pm4_bind_state(sctx, poly_offset, &rs->pm4_poly_offset[1]);
		break;
	case PIPE_FORMAT_Z32_FLOAT:
	case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
		si_pm4_bind_state(sctx, poly_offset, &rs->pm4_poly_offset[2]);
		break;
	}
}

/*
 * Rasterizer
 */

static uint32_t si_translate_fill(uint32_t func)
{
	switch(func) {
	case PIPE_POLYGON_MODE_FILL:
		return V_028814_X_DRAW_TRIANGLES;
	case PIPE_POLYGON_MODE_LINE:
		return V_028814_X_DRAW_LINES;
	case PIPE_POLYGON_MODE_POINT:
		return V_028814_X_DRAW_POINTS;
	default:
		assert(0);
		return V_028814_X_DRAW_POINTS;
	}
}

static void *si_create_rs_state(struct pipe_context *ctx,
				const struct pipe_rasterizer_state *state)
{
	struct si_screen *sscreen = ((struct si_context *)ctx)->screen;
	struct si_state_rasterizer *rs = CALLOC_STRUCT(si_state_rasterizer);
	struct si_pm4_state *pm4 = &rs->pm4;
	unsigned tmp, i;
	float psize_min, psize_max;

	if (!rs) {
		return NULL;
	}

	rs->scissor_enable = state->scissor;
	rs->clip_halfz = state->clip_halfz;
	rs->two_side = state->light_twoside;
	rs->multisample_enable = state->multisample;
	rs->force_persample_interp = state->force_persample_interp;
	rs->clip_plane_enable = state->clip_plane_enable;
	rs->line_stipple_enable = state->line_stipple_enable;
	rs->poly_stipple_enable = state->poly_stipple_enable;
	rs->line_smooth = state->line_smooth;
	rs->line_width = state->line_width;
	rs->poly_smooth = state->poly_smooth;
	rs->uses_poly_offset = state->offset_point || state->offset_line ||
			       state->offset_tri;
	rs->clamp_fragment_color = state->clamp_fragment_color;
	rs->clamp_vertex_color = state->clamp_vertex_color;
	rs->flatshade = state->flatshade;
	rs->sprite_coord_enable = state->sprite_coord_enable;
	rs->rasterizer_discard = state->rasterizer_discard;
	rs->pa_sc_line_stipple = state->line_stipple_enable ?
				S_028A0C_LINE_PATTERN(state->line_stipple_pattern) |
				S_028A0C_REPEAT_COUNT(state->line_stipple_factor) : 0;
	rs->pa_cl_clip_cntl =
		S_028810_DX_CLIP_SPACE_DEF(state->clip_halfz) |
		S_028810_ZCLIP_NEAR_DISABLE(!state->depth_clip) |
		S_028810_ZCLIP_FAR_DISABLE(!state->depth_clip) |
		S_028810_DX_RASTERIZATION_KILL(state->rasterizer_discard) |
		S_028810_DX_LINEAR_ATTR_CLIP_ENA(1);

	si_pm4_set_reg(pm4, R_0286D4_SPI_INTERP_CONTROL_0,
		S_0286D4_FLAT_SHADE_ENA(1) |
		S_0286D4_PNT_SPRITE_ENA(state->point_quad_rasterization) |
		S_0286D4_PNT_SPRITE_OVRD_X(V_0286D4_SPI_PNT_SPRITE_SEL_S) |
		S_0286D4_PNT_SPRITE_OVRD_Y(V_0286D4_SPI_PNT_SPRITE_SEL_T) |
		S_0286D4_PNT_SPRITE_OVRD_Z(V_0286D4_SPI_PNT_SPRITE_SEL_0) |
		S_0286D4_PNT_SPRITE_OVRD_W(V_0286D4_SPI_PNT_SPRITE_SEL_1) |
		S_0286D4_PNT_SPRITE_TOP_1(state->sprite_coord_mode != PIPE_SPRITE_COORD_UPPER_LEFT));

	/* point size 12.4 fixed point */
	tmp = (unsigned)(state->point_size * 8.0);
	si_pm4_set_reg(pm4, R_028A00_PA_SU_POINT_SIZE, S_028A00_HEIGHT(tmp) | S_028A00_WIDTH(tmp));

	if (state->point_size_per_vertex) {
		psize_min = util_get_min_point_size(state);
		psize_max = 8192;
	} else {
		/* Force the point size to be as if the vertex output was disabled. */
		psize_min = state->point_size;
		psize_max = state->point_size;
	}
	rs->max_point_size = psize_max;

	/* Divide by two, because 0.5 = 1 pixel. */
	si_pm4_set_reg(pm4, R_028A04_PA_SU_POINT_MINMAX,
			S_028A04_MIN_SIZE(si_pack_float_12p4(psize_min/2)) |
			S_028A04_MAX_SIZE(si_pack_float_12p4(psize_max/2)));

	si_pm4_set_reg(pm4, R_028A08_PA_SU_LINE_CNTL,
		       S_028A08_WIDTH(si_pack_float_12p4(state->line_width/2)));
	si_pm4_set_reg(pm4, R_028A48_PA_SC_MODE_CNTL_0,
		       S_028A48_LINE_STIPPLE_ENABLE(state->line_stipple_enable) |
		       S_028A48_MSAA_ENABLE(state->multisample ||
					    state->poly_smooth ||
					    state->line_smooth) |
		       S_028A48_VPORT_SCISSOR_ENABLE(1) |
		       S_028A48_ALTERNATE_RBS_PER_TILE(sscreen->info.chip_class >= GFX9));

	si_pm4_set_reg(pm4, R_028BE4_PA_SU_VTX_CNTL,
		       S_028BE4_PIX_CENTER(state->half_pixel_center) |
		       S_028BE4_QUANT_MODE(V_028BE4_X_16_8_FIXED_POINT_1_256TH));

	si_pm4_set_reg(pm4, R_028B7C_PA_SU_POLY_OFFSET_CLAMP, fui(state->offset_clamp));
	si_pm4_set_reg(pm4, R_028814_PA_SU_SC_MODE_CNTL,
		S_028814_PROVOKING_VTX_LAST(!state->flatshade_first) |
		S_028814_CULL_FRONT((state->cull_face & PIPE_FACE_FRONT) ? 1 : 0) |
		S_028814_CULL_BACK((state->cull_face & PIPE_FACE_BACK) ? 1 : 0) |
		S_028814_FACE(!state->front_ccw) |
		S_028814_POLY_OFFSET_FRONT_ENABLE(util_get_offset(state, state->fill_front)) |
		S_028814_POLY_OFFSET_BACK_ENABLE(util_get_offset(state, state->fill_back)) |
		S_028814_POLY_OFFSET_PARA_ENABLE(state->offset_point || state->offset_line) |
		S_028814_POLY_MODE(state->fill_front != PIPE_POLYGON_MODE_FILL ||
				   state->fill_back != PIPE_POLYGON_MODE_FILL) |
		S_028814_POLYMODE_FRONT_PTYPE(si_translate_fill(state->fill_front)) |
		S_028814_POLYMODE_BACK_PTYPE(si_translate_fill(state->fill_back)));

	if (!rs->uses_poly_offset)
		return rs;

	rs->pm4_poly_offset = CALLOC(3, sizeof(struct si_pm4_state));
	if (!rs->pm4_poly_offset) {
		FREE(rs);
		return NULL;
	}

	/* Precalculate polygon offset states for 16-bit, 24-bit, and 32-bit zbuffers. */
	for (i = 0; i < 3; i++) {
		struct si_pm4_state *pm4 = &rs->pm4_poly_offset[i];
		float offset_units = state->offset_units;
		float offset_scale = state->offset_scale * 16.0f;
		uint32_t pa_su_poly_offset_db_fmt_cntl = 0;

		if (!state->offset_units_unscaled) {
			switch (i) {
			case 0: /* 16-bit zbuffer */
				offset_units *= 4.0f;
				pa_su_poly_offset_db_fmt_cntl =
					S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-16);
				break;
			case 1: /* 24-bit zbuffer */
				offset_units *= 2.0f;
				pa_su_poly_offset_db_fmt_cntl =
					S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-24);
				break;
			case 2: /* 32-bit zbuffer */
				offset_units *= 1.0f;
				pa_su_poly_offset_db_fmt_cntl = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-23) |
								S_028B78_POLY_OFFSET_DB_IS_FLOAT_FMT(1);
				break;
			}
		}

		si_pm4_set_reg(pm4, R_028B80_PA_SU_POLY_OFFSET_FRONT_SCALE,
			       fui(offset_scale));
		si_pm4_set_reg(pm4, R_028B84_PA_SU_POLY_OFFSET_FRONT_OFFSET,
			       fui(offset_units));
		si_pm4_set_reg(pm4, R_028B88_PA_SU_POLY_OFFSET_BACK_SCALE,
			       fui(offset_scale));
		si_pm4_set_reg(pm4, R_028B8C_PA_SU_POLY_OFFSET_BACK_OFFSET,
			       fui(offset_units));
		si_pm4_set_reg(pm4, R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL,
			       pa_su_poly_offset_db_fmt_cntl);
	}

	return rs;
}

static void si_bind_rs_state(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_state_rasterizer *old_rs =
		(struct si_state_rasterizer*)sctx->queued.named.rasterizer;
	struct si_state_rasterizer *rs = (struct si_state_rasterizer *)state;

	if (!state)
		return;

	if (!old_rs || old_rs->multisample_enable != rs->multisample_enable) {
		si_mark_atom_dirty(sctx, &sctx->db_render_state);

		/* Update the small primitive filter workaround if necessary. */
		if (sctx->screen->has_msaa_sample_loc_bug &&
		    sctx->framebuffer.nr_samples > 1)
			si_mark_atom_dirty(sctx, &sctx->msaa_sample_locs.atom);
	}

	sctx->current_vs_state &= C_VS_STATE_CLAMP_VERTEX_COLOR;
	sctx->current_vs_state |= S_VS_STATE_CLAMP_VERTEX_COLOR(rs->clamp_vertex_color);

	si_pm4_bind_state(sctx, rasterizer, rs);
	si_update_poly_offset_state(sctx);

	if (!old_rs ||
	    (old_rs->scissor_enable != rs->scissor_enable ||
	     old_rs->line_width != rs->line_width ||
	     old_rs->max_point_size != rs->max_point_size)) {
		sctx->scissors.dirty_mask = (1 << SI_MAX_VIEWPORTS) - 1;
		si_mark_atom_dirty(sctx, &sctx->scissors.atom);
	}

	if (!old_rs ||
	    old_rs->clip_halfz != rs->clip_halfz) {
		sctx->viewports.depth_range_dirty_mask = (1 << SI_MAX_VIEWPORTS) - 1;
		si_mark_atom_dirty(sctx, &sctx->viewports.atom);
	}

	if (!old_rs ||
	    old_rs->clip_plane_enable != rs->clip_plane_enable ||
	    old_rs->pa_cl_clip_cntl != rs->pa_cl_clip_cntl)
		si_mark_atom_dirty(sctx, &sctx->clip_regs);

	sctx->ia_multi_vgt_param_key.u.line_stipple_enabled =
		rs->line_stipple_enable;

	if (!old_rs ||
	    old_rs->clip_plane_enable != rs->clip_plane_enable ||
	    old_rs->rasterizer_discard != rs->rasterizer_discard ||
	    old_rs->sprite_coord_enable != rs->sprite_coord_enable ||
	    old_rs->flatshade != rs->flatshade ||
	    old_rs->two_side != rs->two_side ||
	    old_rs->multisample_enable != rs->multisample_enable ||
	    old_rs->poly_stipple_enable != rs->poly_stipple_enable ||
	    old_rs->poly_smooth != rs->poly_smooth ||
	    old_rs->line_smooth != rs->line_smooth ||
	    old_rs->clamp_fragment_color != rs->clamp_fragment_color ||
	    old_rs->force_persample_interp != rs->force_persample_interp)
		sctx->do_update_shaders = true;
}

static void si_delete_rs_state(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_state_rasterizer *rs = (struct si_state_rasterizer *)state;

	if (sctx->queued.named.rasterizer == state)
		si_pm4_bind_state(sctx, poly_offset, NULL);

	FREE(rs->pm4_poly_offset);
	si_pm4_delete_state(sctx, rasterizer, rs);
}

/*
 * infeered state between dsa and stencil ref
 */
static void si_emit_stencil_ref(struct si_context *sctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = sctx->gfx_cs;
	struct pipe_stencil_ref *ref = &sctx->stencil_ref.state;
	struct si_dsa_stencil_ref_part *dsa = &sctx->stencil_ref.dsa_part;

	radeon_set_context_reg_seq(cs, R_028430_DB_STENCILREFMASK, 2);
	radeon_emit(cs, S_028430_STENCILTESTVAL(ref->ref_value[0]) |
			S_028430_STENCILMASK(dsa->valuemask[0]) |
			S_028430_STENCILWRITEMASK(dsa->writemask[0]) |
			S_028430_STENCILOPVAL(1));
	radeon_emit(cs, S_028434_STENCILTESTVAL_BF(ref->ref_value[1]) |
			S_028434_STENCILMASK_BF(dsa->valuemask[1]) |
			S_028434_STENCILWRITEMASK_BF(dsa->writemask[1]) |
			S_028434_STENCILOPVAL_BF(1));
}

static void si_set_stencil_ref(struct pipe_context *ctx,
			       const struct pipe_stencil_ref *state)
{
        struct si_context *sctx = (struct si_context *)ctx;

	if (memcmp(&sctx->stencil_ref.state, state, sizeof(*state)) == 0)
		return;

	sctx->stencil_ref.state = *state;
	si_mark_atom_dirty(sctx, &sctx->stencil_ref.atom);
}


/*
 * DSA
 */

static uint32_t si_translate_stencil_op(int s_op)
{
	switch (s_op) {
	case PIPE_STENCIL_OP_KEEP:
		return V_02842C_STENCIL_KEEP;
	case PIPE_STENCIL_OP_ZERO:
		return V_02842C_STENCIL_ZERO;
	case PIPE_STENCIL_OP_REPLACE:
		return V_02842C_STENCIL_REPLACE_TEST;
	case PIPE_STENCIL_OP_INCR:
		return V_02842C_STENCIL_ADD_CLAMP;
	case PIPE_STENCIL_OP_DECR:
		return V_02842C_STENCIL_SUB_CLAMP;
	case PIPE_STENCIL_OP_INCR_WRAP:
		return V_02842C_STENCIL_ADD_WRAP;
	case PIPE_STENCIL_OP_DECR_WRAP:
		return V_02842C_STENCIL_SUB_WRAP;
	case PIPE_STENCIL_OP_INVERT:
		return V_02842C_STENCIL_INVERT;
	default:
		PRINT_ERR("Unknown stencil op %d", s_op);
		assert(0);
		break;
	}
	return 0;
}

static bool si_dsa_writes_stencil(const struct pipe_stencil_state *s)
{
	return s->enabled && s->writemask &&
	       (s->fail_op  != PIPE_STENCIL_OP_KEEP ||
		s->zfail_op != PIPE_STENCIL_OP_KEEP ||
		s->zpass_op != PIPE_STENCIL_OP_KEEP);
}

static bool si_order_invariant_stencil_op(enum pipe_stencil_op op)
{
	/* REPLACE is normally order invariant, except when the stencil
	 * reference value is written by the fragment shader. Tracking this
	 * interaction does not seem worth the effort, so be conservative. */
	return op != PIPE_STENCIL_OP_INCR &&
	       op != PIPE_STENCIL_OP_DECR &&
	       op != PIPE_STENCIL_OP_REPLACE;
}

/* Compute whether, assuming Z writes are disabled, this stencil state is order
 * invariant in the sense that the set of passing fragments as well as the
 * final stencil buffer result does not depend on the order of fragments. */
static bool si_order_invariant_stencil_state(const struct pipe_stencil_state *state)
{
	return !state->enabled || !state->writemask ||
	       /* The following assumes that Z writes are disabled. */
	       (state->func == PIPE_FUNC_ALWAYS &&
	        si_order_invariant_stencil_op(state->zpass_op) &&
	        si_order_invariant_stencil_op(state->zfail_op)) ||
	       (state->func == PIPE_FUNC_NEVER &&
	        si_order_invariant_stencil_op(state->fail_op));
}

static void *si_create_dsa_state(struct pipe_context *ctx,
				 const struct pipe_depth_stencil_alpha_state *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_state_dsa *dsa = CALLOC_STRUCT(si_state_dsa);
	struct si_pm4_state *pm4 = &dsa->pm4;
	unsigned db_depth_control;
	uint32_t db_stencil_control = 0;

	if (!dsa) {
		return NULL;
	}

	dsa->stencil_ref.valuemask[0] = state->stencil[0].valuemask;
	dsa->stencil_ref.valuemask[1] = state->stencil[1].valuemask;
	dsa->stencil_ref.writemask[0] = state->stencil[0].writemask;
	dsa->stencil_ref.writemask[1] = state->stencil[1].writemask;

	db_depth_control = S_028800_Z_ENABLE(state->depth.enabled) |
		S_028800_Z_WRITE_ENABLE(state->depth.writemask) |
		S_028800_ZFUNC(state->depth.func) |
		S_028800_DEPTH_BOUNDS_ENABLE(state->depth.bounds_test);

	/* stencil */
	if (state->stencil[0].enabled) {
		db_depth_control |= S_028800_STENCIL_ENABLE(1);
		db_depth_control |= S_028800_STENCILFUNC(state->stencil[0].func);
		db_stencil_control |= S_02842C_STENCILFAIL(si_translate_stencil_op(state->stencil[0].fail_op));
		db_stencil_control |= S_02842C_STENCILZPASS(si_translate_stencil_op(state->stencil[0].zpass_op));
		db_stencil_control |= S_02842C_STENCILZFAIL(si_translate_stencil_op(state->stencil[0].zfail_op));

		if (state->stencil[1].enabled) {
			db_depth_control |= S_028800_BACKFACE_ENABLE(1);
			db_depth_control |= S_028800_STENCILFUNC_BF(state->stencil[1].func);
			db_stencil_control |= S_02842C_STENCILFAIL_BF(si_translate_stencil_op(state->stencil[1].fail_op));
			db_stencil_control |= S_02842C_STENCILZPASS_BF(si_translate_stencil_op(state->stencil[1].zpass_op));
			db_stencil_control |= S_02842C_STENCILZFAIL_BF(si_translate_stencil_op(state->stencil[1].zfail_op));
		}
	}

	/* alpha */
	if (state->alpha.enabled) {
		dsa->alpha_func = state->alpha.func;

		si_pm4_set_reg(pm4, R_00B030_SPI_SHADER_USER_DATA_PS_0 +
		               SI_SGPR_ALPHA_REF * 4, fui(state->alpha.ref_value));
	} else {
		dsa->alpha_func = PIPE_FUNC_ALWAYS;
	}

	si_pm4_set_reg(pm4, R_028800_DB_DEPTH_CONTROL, db_depth_control);
	if (state->stencil[0].enabled)
		si_pm4_set_reg(pm4, R_02842C_DB_STENCIL_CONTROL, db_stencil_control);
	if (state->depth.bounds_test) {
		si_pm4_set_reg(pm4, R_028020_DB_DEPTH_BOUNDS_MIN, fui(state->depth.bounds_min));
		si_pm4_set_reg(pm4, R_028024_DB_DEPTH_BOUNDS_MAX, fui(state->depth.bounds_max));
	}

	dsa->depth_enabled = state->depth.enabled;
	dsa->depth_write_enabled = state->depth.enabled &&
				   state->depth.writemask;
	dsa->stencil_enabled = state->stencil[0].enabled;
	dsa->stencil_write_enabled = state->stencil[0].enabled &&
				     (si_dsa_writes_stencil(&state->stencil[0]) ||
				      si_dsa_writes_stencil(&state->stencil[1]));
	dsa->db_can_write = dsa->depth_write_enabled ||
			    dsa->stencil_write_enabled;

	bool zfunc_is_ordered =
		state->depth.func == PIPE_FUNC_NEVER ||
		state->depth.func == PIPE_FUNC_LESS ||
		state->depth.func == PIPE_FUNC_LEQUAL ||
		state->depth.func == PIPE_FUNC_GREATER ||
		state->depth.func == PIPE_FUNC_GEQUAL;

	bool nozwrite_and_order_invariant_stencil =
		!dsa->db_can_write ||
		(!dsa->depth_write_enabled &&
		 si_order_invariant_stencil_state(&state->stencil[0]) &&
		 si_order_invariant_stencil_state(&state->stencil[1]));

	dsa->order_invariance[1].zs =
		nozwrite_and_order_invariant_stencil ||
		(!dsa->stencil_write_enabled && zfunc_is_ordered);
	dsa->order_invariance[0].zs = !dsa->depth_write_enabled || zfunc_is_ordered;

	dsa->order_invariance[1].pass_set =
		nozwrite_and_order_invariant_stencil ||
		(!dsa->stencil_write_enabled &&
		 (state->depth.func == PIPE_FUNC_ALWAYS ||
		  state->depth.func == PIPE_FUNC_NEVER));
	dsa->order_invariance[0].pass_set =
		!dsa->depth_write_enabled ||
		(state->depth.func == PIPE_FUNC_ALWAYS ||
		 state->depth.func == PIPE_FUNC_NEVER);

	dsa->order_invariance[1].pass_last =
		sctx->screen->assume_no_z_fights &&
		!dsa->stencil_write_enabled &&
		dsa->depth_write_enabled && zfunc_is_ordered;
	dsa->order_invariance[0].pass_last =
		sctx->screen->assume_no_z_fights &&
		dsa->depth_write_enabled && zfunc_is_ordered;

	return dsa;
}

static void si_bind_dsa_state(struct pipe_context *ctx, void *state)
{
        struct si_context *sctx = (struct si_context *)ctx;
	struct si_state_dsa *old_dsa = sctx->queued.named.dsa;
        struct si_state_dsa *dsa = state;

        if (!state)
                return;

	si_pm4_bind_state(sctx, dsa, dsa);

	if (memcmp(&dsa->stencil_ref, &sctx->stencil_ref.dsa_part,
		   sizeof(struct si_dsa_stencil_ref_part)) != 0) {
		sctx->stencil_ref.dsa_part = dsa->stencil_ref;
		si_mark_atom_dirty(sctx, &sctx->stencil_ref.atom);
	}

	if (!old_dsa || old_dsa->alpha_func != dsa->alpha_func)
		sctx->do_update_shaders = true;

	if (sctx->screen->dpbb_allowed &&
	    (!old_dsa ||
	     (old_dsa->depth_enabled != dsa->depth_enabled ||
	      old_dsa->stencil_enabled != dsa->stencil_enabled ||
	      old_dsa->db_can_write != dsa->db_can_write)))
		si_mark_atom_dirty(sctx, &sctx->dpbb_state);

	if (sctx->screen->has_out_of_order_rast &&
	    (!old_dsa ||
	     memcmp(old_dsa->order_invariance, dsa->order_invariance,
		    sizeof(old_dsa->order_invariance))))
		si_mark_atom_dirty(sctx, &sctx->msaa_config);
}

static void si_delete_dsa_state(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	si_pm4_delete_state(sctx, dsa, (struct si_state_dsa *)state);
}

static void *si_create_db_flush_dsa(struct si_context *sctx)
{
	struct pipe_depth_stencil_alpha_state dsa = {};

	return sctx->b.create_depth_stencil_alpha_state(&sctx->b, &dsa);
}

/* DB RENDER STATE */

static void si_set_active_query_state(struct pipe_context *ctx, boolean enable)
{
	struct si_context *sctx = (struct si_context*)ctx;

	/* Pipeline stat & streamout queries. */
	if (enable) {
		sctx->flags &= ~SI_CONTEXT_STOP_PIPELINE_STATS;
		sctx->flags |= SI_CONTEXT_START_PIPELINE_STATS;
	} else {
		sctx->flags &= ~SI_CONTEXT_START_PIPELINE_STATS;
		sctx->flags |= SI_CONTEXT_STOP_PIPELINE_STATS;
	}

	/* Occlusion queries. */
	if (sctx->occlusion_queries_disabled != !enable) {
		sctx->occlusion_queries_disabled = !enable;
		si_mark_atom_dirty(sctx, &sctx->db_render_state);
	}
}

void si_set_occlusion_query_state(struct si_context *sctx,
				  bool old_perfect_enable)
{
	si_mark_atom_dirty(sctx, &sctx->db_render_state);

	bool perfect_enable = sctx->num_perfect_occlusion_queries != 0;

	if (perfect_enable != old_perfect_enable)
		si_mark_atom_dirty(sctx, &sctx->msaa_config);
}

void si_save_qbo_state(struct si_context *sctx, struct si_qbo_state *st)
{
	st->saved_compute = sctx->cs_shader_state.program;

	si_get_pipe_constant_buffer(sctx, PIPE_SHADER_COMPUTE, 0, &st->saved_const0);
	si_get_shader_buffers(sctx, PIPE_SHADER_COMPUTE, 0, 3, st->saved_ssbo);
}

static void si_emit_db_render_state(struct si_context *sctx, struct r600_atom *state)
{
	struct radeon_winsys_cs *cs = sctx->gfx_cs;
	struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
	unsigned db_shader_control;

	radeon_set_context_reg_seq(cs, R_028000_DB_RENDER_CONTROL, 2);

	/* DB_RENDER_CONTROL */
	if (sctx->dbcb_depth_copy_enabled ||
	    sctx->dbcb_stencil_copy_enabled) {
		radeon_emit(cs,
			    S_028000_DEPTH_COPY(sctx->dbcb_depth_copy_enabled) |
			    S_028000_STENCIL_COPY(sctx->dbcb_stencil_copy_enabled) |
			    S_028000_COPY_CENTROID(1) |
			    S_028000_COPY_SAMPLE(sctx->dbcb_copy_sample));
	} else if (sctx->db_flush_depth_inplace || sctx->db_flush_stencil_inplace) {
		radeon_emit(cs,
			    S_028000_DEPTH_COMPRESS_DISABLE(sctx->db_flush_depth_inplace) |
			    S_028000_STENCIL_COMPRESS_DISABLE(sctx->db_flush_stencil_inplace));
	} else {
		radeon_emit(cs,
			    S_028000_DEPTH_CLEAR_ENABLE(sctx->db_depth_clear) |
			    S_028000_STENCIL_CLEAR_ENABLE(sctx->db_stencil_clear));
	}

	/* DB_COUNT_CONTROL (occlusion queries) */
	if (sctx->num_occlusion_queries > 0 &&
	    !sctx->occlusion_queries_disabled) {
		bool perfect = sctx->num_perfect_occlusion_queries > 0;

		if (sctx->chip_class >= CIK) {
			radeon_emit(cs,
				    S_028004_PERFECT_ZPASS_COUNTS(perfect) |
				    S_028004_SAMPLE_RATE(sctx->framebuffer.log_samples) |
				    S_028004_ZPASS_ENABLE(1) |
				    S_028004_SLICE_EVEN_ENABLE(1) |
				    S_028004_SLICE_ODD_ENABLE(1));
		} else {
			radeon_emit(cs,
				    S_028004_PERFECT_ZPASS_COUNTS(perfect) |
				    S_028004_SAMPLE_RATE(sctx->framebuffer.log_samples));
		}
	} else {
		/* Disable occlusion queries. */
		if (sctx->chip_class >= CIK) {
			radeon_emit(cs, 0);
		} else {
			radeon_emit(cs, S_028004_ZPASS_INCREMENT_DISABLE(1));
		}
	}

	/* DB_RENDER_OVERRIDE2 */
	radeon_set_context_reg(cs, R_028010_DB_RENDER_OVERRIDE2,
		S_028010_DISABLE_ZMASK_EXPCLEAR_OPTIMIZATION(sctx->db_depth_disable_expclear) |
		S_028010_DISABLE_SMEM_EXPCLEAR_OPTIMIZATION(sctx->db_stencil_disable_expclear) |
		S_028010_DECOMPRESS_Z_ON_FLUSH(sctx->framebuffer.nr_samples >= 4));

	db_shader_control = sctx->ps_db_shader_control;

	/* Bug workaround for smoothing (overrasterization) on SI. */
	if (sctx->chip_class == SI && sctx->smoothing_enabled) {
		db_shader_control &= C_02880C_Z_ORDER;
		db_shader_control |= S_02880C_Z_ORDER(V_02880C_LATE_Z);
	}

	/* Disable the gl_SampleMask fragment shader output if MSAA is disabled. */
	if (!rs || !rs->multisample_enable)
		db_shader_control &= C_02880C_MASK_EXPORT_ENABLE;

	if (sctx->screen->has_rbplus &&
	    !sctx->screen->rbplus_allowed)
		db_shader_control |= S_02880C_DUAL_QUAD_DISABLE(1);

	radeon_set_context_reg(cs, R_02880C_DB_SHADER_CONTROL,
			       db_shader_control);
}

/*
 * format translation
 */
static uint32_t si_translate_colorformat(enum pipe_format format)
{
	const struct util_format_description *desc = util_format_description(format);
	if (!desc)
		return V_028C70_COLOR_INVALID;

#define HAS_SIZE(x,y,z,w) \
	(desc->channel[0].size == (x) && desc->channel[1].size == (y) && \
         desc->channel[2].size == (z) && desc->channel[3].size == (w))

	if (format == PIPE_FORMAT_R11G11B10_FLOAT) /* isn't plain */
		return V_028C70_COLOR_10_11_11;

	if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
		return V_028C70_COLOR_INVALID;

	/* hw cannot support mixed formats (except depth/stencil, since
	 * stencil is not written to). */
	if (desc->is_mixed && desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
		return V_028C70_COLOR_INVALID;

	switch (desc->nr_channels) {
	case 1:
		switch (desc->channel[0].size) {
		case 8:
			return V_028C70_COLOR_8;
		case 16:
			return V_028C70_COLOR_16;
		case 32:
			return V_028C70_COLOR_32;
		}
		break;
	case 2:
		if (desc->channel[0].size == desc->channel[1].size) {
			switch (desc->channel[0].size) {
			case 8:
				return V_028C70_COLOR_8_8;
			case 16:
				return V_028C70_COLOR_16_16;
			case 32:
				return V_028C70_COLOR_32_32;
			}
		} else if (HAS_SIZE(8,24,0,0)) {
			return V_028C70_COLOR_24_8;
		} else if (HAS_SIZE(24,8,0,0)) {
			return V_028C70_COLOR_8_24;
		}
		break;
	case 3:
		if (HAS_SIZE(5,6,5,0)) {
			return V_028C70_COLOR_5_6_5;
		} else if (HAS_SIZE(32,8,24,0)) {
			return V_028C70_COLOR_X24_8_32_FLOAT;
		}
		break;
	case 4:
		if (desc->channel[0].size == desc->channel[1].size &&
		    desc->channel[0].size == desc->channel[2].size &&
		    desc->channel[0].size == desc->channel[3].size) {
			switch (desc->channel[0].size) {
			case 4:
				return V_028C70_COLOR_4_4_4_4;
			case 8:
				return V_028C70_COLOR_8_8_8_8;
			case 16:
				return V_028C70_COLOR_16_16_16_16;
			case 32:
				return V_028C70_COLOR_32_32_32_32;
			}
		} else if (HAS_SIZE(5,5,5,1)) {
			return V_028C70_COLOR_1_5_5_5;
		} else if (HAS_SIZE(1,5,5,5)) {
			return V_028C70_COLOR_5_5_5_1;
		} else if (HAS_SIZE(10,10,10,2)) {
			return V_028C70_COLOR_2_10_10_10;
		}
		break;
	}
	return V_028C70_COLOR_INVALID;
}

static uint32_t si_colorformat_endian_swap(uint32_t colorformat)
{
	if (SI_BIG_ENDIAN) {
		switch(colorformat) {
		/* 8-bit buffers. */
		case V_028C70_COLOR_8:
			return V_028C70_ENDIAN_NONE;

		/* 16-bit buffers. */
		case V_028C70_COLOR_5_6_5:
		case V_028C70_COLOR_1_5_5_5:
		case V_028C70_COLOR_4_4_4_4:
		case V_028C70_COLOR_16:
		case V_028C70_COLOR_8_8:
			return V_028C70_ENDIAN_8IN16;

		/* 32-bit buffers. */
		case V_028C70_COLOR_8_8_8_8:
		case V_028C70_COLOR_2_10_10_10:
		case V_028C70_COLOR_8_24:
		case V_028C70_COLOR_24_8:
		case V_028C70_COLOR_16_16:
			return V_028C70_ENDIAN_8IN32;

		/* 64-bit buffers. */
		case V_028C70_COLOR_16_16_16_16:
			return V_028C70_ENDIAN_8IN16;

		case V_028C70_COLOR_32_32:
			return V_028C70_ENDIAN_8IN32;

		/* 128-bit buffers. */
		case V_028C70_COLOR_32_32_32_32:
			return V_028C70_ENDIAN_8IN32;
		default:
			return V_028C70_ENDIAN_NONE; /* Unsupported. */
		}
	} else {
		return V_028C70_ENDIAN_NONE;
	}
}

static uint32_t si_translate_dbformat(enum pipe_format format)
{
	switch (format) {
	case PIPE_FORMAT_Z16_UNORM:
		return V_028040_Z_16;
	case PIPE_FORMAT_S8_UINT_Z24_UNORM:
	case PIPE_FORMAT_X8Z24_UNORM:
	case PIPE_FORMAT_Z24X8_UNORM:
	case PIPE_FORMAT_Z24_UNORM_S8_UINT:
		return V_028040_Z_24; /* deprecated on SI */
	case PIPE_FORMAT_Z32_FLOAT:
	case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
		return V_028040_Z_32_FLOAT;
	default:
		return V_028040_Z_INVALID;
	}
}

/*
 * Texture translation
 */

static uint32_t si_translate_texformat(struct pipe_screen *screen,
				       enum pipe_format format,
				       const struct util_format_description *desc,
				       int first_non_void)
{
	struct si_screen *sscreen = (struct si_screen*)screen;
	bool enable_compressed_formats = (sscreen->info.drm_major == 2 &&
					  sscreen->info.drm_minor >= 31) ||
					 sscreen->info.drm_major == 3;
	bool uniform = true;
	int i;

	/* Colorspace (return non-RGB formats directly). */
	switch (desc->colorspace) {
	/* Depth stencil formats */
	case UTIL_FORMAT_COLORSPACE_ZS:
		switch (format) {
		case PIPE_FORMAT_Z16_UNORM:
			return V_008F14_IMG_DATA_FORMAT_16;
		case PIPE_FORMAT_X24S8_UINT:
		case PIPE_FORMAT_S8X24_UINT:
			/*
			 * Implemented as an 8_8_8_8 data format to fix texture
			 * gathers in stencil sampling. This affects at least
			 * GL45-CTS.texture_cube_map_array.sampling on VI.
			 */
			if (sscreen->info.chip_class <= VI)
				return V_008F14_IMG_DATA_FORMAT_8_8_8_8;

			if (format == PIPE_FORMAT_X24S8_UINT)
				return V_008F14_IMG_DATA_FORMAT_8_24;
			else
				return V_008F14_IMG_DATA_FORMAT_24_8;
		case PIPE_FORMAT_Z24X8_UNORM:
		case PIPE_FORMAT_Z24_UNORM_S8_UINT:
			return V_008F14_IMG_DATA_FORMAT_8_24;
		case PIPE_FORMAT_X8Z24_UNORM:
		case PIPE_FORMAT_S8_UINT_Z24_UNORM:
			return V_008F14_IMG_DATA_FORMAT_24_8;
		case PIPE_FORMAT_S8_UINT:
			return V_008F14_IMG_DATA_FORMAT_8;
		case PIPE_FORMAT_Z32_FLOAT:
			return V_008F14_IMG_DATA_FORMAT_32;
		case PIPE_FORMAT_X32_S8X24_UINT:
		case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
			return V_008F14_IMG_DATA_FORMAT_X24_8_32;
		default:
			goto out_unknown;
		}

	case UTIL_FORMAT_COLORSPACE_YUV:
		goto out_unknown; /* TODO */

	case UTIL_FORMAT_COLORSPACE_SRGB:
		if (desc->nr_channels != 4 && desc->nr_channels != 1)
			goto out_unknown;
		break;

	default:
		break;
	}

	if (desc->layout == UTIL_FORMAT_LAYOUT_RGTC) {
		if (!enable_compressed_formats)
			goto out_unknown;

		switch (format) {
		case PIPE_FORMAT_RGTC1_SNORM:
		case PIPE_FORMAT_LATC1_SNORM:
		case PIPE_FORMAT_RGTC1_UNORM:
		case PIPE_FORMAT_LATC1_UNORM:
			return V_008F14_IMG_DATA_FORMAT_BC4;
		case PIPE_FORMAT_RGTC2_SNORM:
		case PIPE_FORMAT_LATC2_SNORM:
		case PIPE_FORMAT_RGTC2_UNORM:
		case PIPE_FORMAT_LATC2_UNORM:
			return V_008F14_IMG_DATA_FORMAT_BC5;
		default:
			goto out_unknown;
		}
	}

	if (desc->layout == UTIL_FORMAT_LAYOUT_ETC &&
	    (sscreen->info.family == CHIP_STONEY ||
	     sscreen->info.family == CHIP_VEGA10 ||
	     sscreen->info.family == CHIP_RAVEN)) {
		switch (format) {
		case PIPE_FORMAT_ETC1_RGB8:
		case PIPE_FORMAT_ETC2_RGB8:
		case PIPE_FORMAT_ETC2_SRGB8:
			return V_008F14_IMG_DATA_FORMAT_ETC2_RGB;
		case PIPE_FORMAT_ETC2_RGB8A1:
		case PIPE_FORMAT_ETC2_SRGB8A1:
			return V_008F14_IMG_DATA_FORMAT_ETC2_RGBA1;
		case PIPE_FORMAT_ETC2_RGBA8:
		case PIPE_FORMAT_ETC2_SRGBA8:
			return V_008F14_IMG_DATA_FORMAT_ETC2_RGBA;
		case PIPE_FORMAT_ETC2_R11_UNORM:
		case PIPE_FORMAT_ETC2_R11_SNORM:
			return V_008F14_IMG_DATA_FORMAT_ETC2_R;
		case PIPE_FORMAT_ETC2_RG11_UNORM:
		case PIPE_FORMAT_ETC2_RG11_SNORM:
			return V_008F14_IMG_DATA_FORMAT_ETC2_RG;
		default:
			goto out_unknown;
		}
	}

	if (desc->layout == UTIL_FORMAT_LAYOUT_BPTC) {
		if (!enable_compressed_formats)
			goto out_unknown;

		switch (format) {
		case PIPE_FORMAT_BPTC_RGBA_UNORM:
		case PIPE_FORMAT_BPTC_SRGBA:
			return V_008F14_IMG_DATA_FORMAT_BC7;
		case PIPE_FORMAT_BPTC_RGB_FLOAT:
		case PIPE_FORMAT_BPTC_RGB_UFLOAT:
			return V_008F14_IMG_DATA_FORMAT_BC6;
		default:
			goto out_unknown;
		}
	}

	if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
		switch (format) {
		case PIPE_FORMAT_R8G8_B8G8_UNORM:
		case PIPE_FORMAT_G8R8_B8R8_UNORM:
			return V_008F14_IMG_DATA_FORMAT_GB_GR;
		case PIPE_FORMAT_G8R8_G8B8_UNORM:
		case PIPE_FORMAT_R8G8_R8B8_UNORM:
			return V_008F14_IMG_DATA_FORMAT_BG_RG;
		default:
			goto out_unknown;
		}
	}

	if (desc->layout == UTIL_FORMAT_LAYOUT_S3TC) {
		if (!enable_compressed_formats)
			goto out_unknown;

		switch (format) {
		case PIPE_FORMAT_DXT1_RGB:
		case PIPE_FORMAT_DXT1_RGBA:
		case PIPE_FORMAT_DXT1_SRGB:
		case PIPE_FORMAT_DXT1_SRGBA:
			return V_008F14_IMG_DATA_FORMAT_BC1;
		case PIPE_FORMAT_DXT3_RGBA:
		case PIPE_FORMAT_DXT3_SRGBA:
			return V_008F14_IMG_DATA_FORMAT_BC2;
		case PIPE_FORMAT_DXT5_RGBA:
		case PIPE_FORMAT_DXT5_SRGBA:
			return V_008F14_IMG_DATA_FORMAT_BC3;
		default:
			goto out_unknown;
		}
	}

	if (format == PIPE_FORMAT_R9G9B9E5_FLOAT) {
		return V_008F14_IMG_DATA_FORMAT_5_9_9_9;
	} else if (format == PIPE_FORMAT_R11G11B10_FLOAT) {
		return V_008F14_IMG_DATA_FORMAT_10_11_11;
	}

	/* R8G8Bx_SNORM - TODO CxV8U8 */

	/* hw cannot support mixed formats (except depth/stencil, since only
	 * depth is read).*/
	if (desc->is_mixed && desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
		goto out_unknown;

	/* See whether the components are of the same size. */
	for (i = 1; i < desc->nr_channels; i++) {
		uniform = uniform && desc->channel[0].size == desc->channel[i].size;
	}

	/* Non-uniform formats. */
	if (!uniform) {
		switch(desc->nr_channels) {
		case 3:
			if (desc->channel[0].size == 5 &&
			    desc->channel[1].size == 6 &&
			    desc->channel[2].size == 5) {
				return V_008F14_IMG_DATA_FORMAT_5_6_5;
			}
			goto out_unknown;
		case 4:
			if (desc->channel[0].size == 5 &&
			    desc->channel[1].size == 5 &&
			    desc->channel[2].size == 5 &&
			    desc->channel[3].size == 1) {
				return V_008F14_IMG_DATA_FORMAT_1_5_5_5;
			}
			if (desc->channel[0].size == 1 &&
			    desc->channel[1].size == 5 &&
			    desc->channel[2].size == 5 &&
			    desc->channel[3].size == 5) {
				return V_008F14_IMG_DATA_FORMAT_5_5_5_1;
			}
			if (desc->channel[0].size == 10 &&
			    desc->channel[1].size == 10 &&
			    desc->channel[2].size == 10 &&
			    desc->channel[3].size == 2) {
				return V_008F14_IMG_DATA_FORMAT_2_10_10_10;
			}
			goto out_unknown;
		}
		goto out_unknown;
	}

	if (first_non_void < 0 || first_non_void > 3)
		goto out_unknown;

	/* uniform formats */
	switch (desc->channel[first_non_void].size) {
	case 4:
		switch (desc->nr_channels) {
#if 0 /* Not supported for render targets */
		case 2:
			return V_008F14_IMG_DATA_FORMAT_4_4;
#endif
		case 4:
			return V_008F14_IMG_DATA_FORMAT_4_4_4_4;
		}
		break;
	case 8:
		switch (desc->nr_channels) {
		case 1:
			return V_008F14_IMG_DATA_FORMAT_8;
		case 2:
			return V_008F14_IMG_DATA_FORMAT_8_8;
		case 4:
			return V_008F14_IMG_DATA_FORMAT_8_8_8_8;
		}
		break;
	case 16:
		switch (desc->nr_channels) {
		case 1:
			return V_008F14_IMG_DATA_FORMAT_16;
		case 2:
			return V_008F14_IMG_DATA_FORMAT_16_16;
		case 4:
			return V_008F14_IMG_DATA_FORMAT_16_16_16_16;
		}
		break;
	case 32:
		switch (desc->nr_channels) {
		case 1:
			return V_008F14_IMG_DATA_FORMAT_32;
		case 2:
			return V_008F14_IMG_DATA_FORMAT_32_32;
#if 0 /* Not supported for render targets */
		case 3:
			return V_008F14_IMG_DATA_FORMAT_32_32_32;
#endif
		case 4:
			return V_008F14_IMG_DATA_FORMAT_32_32_32_32;
		}
	}

out_unknown:
	return ~0;
}

static unsigned si_tex_wrap(unsigned wrap)
{
	switch (wrap) {
	default:
	case PIPE_TEX_WRAP_REPEAT:
		return V_008F30_SQ_TEX_WRAP;
	case PIPE_TEX_WRAP_CLAMP:
		return V_008F30_SQ_TEX_CLAMP_HALF_BORDER;
	case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
		return V_008F30_SQ_TEX_CLAMP_LAST_TEXEL;
	case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
		return V_008F30_SQ_TEX_CLAMP_BORDER;
	case PIPE_TEX_WRAP_MIRROR_REPEAT:
		return V_008F30_SQ_TEX_MIRROR;
	case PIPE_TEX_WRAP_MIRROR_CLAMP:
		return V_008F30_SQ_TEX_MIRROR_ONCE_HALF_BORDER;
	case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
		return V_008F30_SQ_TEX_MIRROR_ONCE_LAST_TEXEL;
	case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER:
		return V_008F30_SQ_TEX_MIRROR_ONCE_BORDER;
	}
}

static unsigned si_tex_mipfilter(unsigned filter)
{
	switch (filter) {
	case PIPE_TEX_MIPFILTER_NEAREST:
		return V_008F38_SQ_TEX_Z_FILTER_POINT;
	case PIPE_TEX_MIPFILTER_LINEAR:
		return V_008F38_SQ_TEX_Z_FILTER_LINEAR;
	default:
	case PIPE_TEX_MIPFILTER_NONE:
		return V_008F38_SQ_TEX_Z_FILTER_NONE;
	}
}

static unsigned si_tex_compare(unsigned compare)
{
	switch (compare) {
	default:
	case PIPE_FUNC_NEVER:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_NEVER;
	case PIPE_FUNC_LESS:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_LESS;
	case PIPE_FUNC_EQUAL:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_EQUAL;
	case PIPE_FUNC_LEQUAL:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_LESSEQUAL;
	case PIPE_FUNC_GREATER:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_GREATER;
	case PIPE_FUNC_NOTEQUAL:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_NOTEQUAL;
	case PIPE_FUNC_GEQUAL:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_GREATEREQUAL;
	case PIPE_FUNC_ALWAYS:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_ALWAYS;
	}
}

static unsigned si_tex_dim(struct si_screen *sscreen, struct r600_texture *rtex,
			   unsigned view_target, unsigned nr_samples)
{
	unsigned res_target = rtex->resource.b.b.target;

	if (view_target == PIPE_TEXTURE_CUBE ||
	    view_target == PIPE_TEXTURE_CUBE_ARRAY)
		res_target = view_target;
	/* If interpreting cubemaps as something else, set 2D_ARRAY. */
	else if (res_target == PIPE_TEXTURE_CUBE ||
		 res_target == PIPE_TEXTURE_CUBE_ARRAY)
		res_target = PIPE_TEXTURE_2D_ARRAY;

	/* GFX9 allocates 1D textures as 2D. */
	if ((res_target == PIPE_TEXTURE_1D ||
	     res_target == PIPE_TEXTURE_1D_ARRAY) &&
	    sscreen->info.chip_class >= GFX9 &&
	    rtex->surface.u.gfx9.resource_type == RADEON_RESOURCE_2D) {
		if (res_target == PIPE_TEXTURE_1D)
			res_target = PIPE_TEXTURE_2D;
		else
			res_target = PIPE_TEXTURE_2D_ARRAY;
	}

	switch (res_target) {
	default:
	case PIPE_TEXTURE_1D:
		return V_008F1C_SQ_RSRC_IMG_1D;
	case PIPE_TEXTURE_1D_ARRAY:
		return V_008F1C_SQ_RSRC_IMG_1D_ARRAY;
	case PIPE_TEXTURE_2D:
	case PIPE_TEXTURE_RECT:
		return nr_samples > 1 ? V_008F1C_SQ_RSRC_IMG_2D_MSAA :
					V_008F1C_SQ_RSRC_IMG_2D;
	case PIPE_TEXTURE_2D_ARRAY:
		return nr_samples > 1 ? V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY :
					V_008F1C_SQ_RSRC_IMG_2D_ARRAY;
	case PIPE_TEXTURE_3D:
		return V_008F1C_SQ_RSRC_IMG_3D;
	case PIPE_TEXTURE_CUBE:
	case PIPE_TEXTURE_CUBE_ARRAY:
		return V_008F1C_SQ_RSRC_IMG_CUBE;
	}
}

/*
 * Format support testing
 */

static bool si_is_sampler_format_supported(struct pipe_screen *screen, enum pipe_format format)
{
	const struct util_format_description *desc = util_format_description(format);
	if (!desc)
		return false;

	return si_translate_texformat(screen, format, desc,
				      util_format_get_first_non_void_channel(format)) != ~0U;
}

static uint32_t si_translate_buffer_dataformat(struct pipe_screen *screen,
					       const struct util_format_description *desc,
					       int first_non_void)
{
	int i;

	if (desc->format == PIPE_FORMAT_R11G11B10_FLOAT)
		return V_008F0C_BUF_DATA_FORMAT_10_11_11;

	assert(first_non_void >= 0);

	if (desc->nr_channels == 4 &&
	    desc->channel[0].size == 10 &&
	    desc->channel[1].size == 10 &&
	    desc->channel[2].size == 10 &&
	    desc->channel[3].size == 2)
		return V_008F0C_BUF_DATA_FORMAT_2_10_10_10;

	/* See whether the components are of the same size. */
	for (i = 0; i < desc->nr_channels; i++) {
		if (desc->channel[first_non_void].size != desc->channel[i].size)
			return V_008F0C_BUF_DATA_FORMAT_INVALID;
	}

	switch (desc->channel[first_non_void].size) {
	case 8:
		switch (desc->nr_channels) {
		case 1:
		case 3: /* 3 loads */
			return V_008F0C_BUF_DATA_FORMAT_8;
		case 2:
			return V_008F0C_BUF_DATA_FORMAT_8_8;
		case 4:
			return V_008F0C_BUF_DATA_FORMAT_8_8_8_8;
		}
		break;
	case 16:
		switch (desc->nr_channels) {
		case 1:
		case 3: /* 3 loads */
			return V_008F0C_BUF_DATA_FORMAT_16;
		case 2:
			return V_008F0C_BUF_DATA_FORMAT_16_16;
		case 4:
			return V_008F0C_BUF_DATA_FORMAT_16_16_16_16;
		}
		break;
	case 32:
		switch (desc->nr_channels) {
		case 1:
			return V_008F0C_BUF_DATA_FORMAT_32;
		case 2:
			return V_008F0C_BUF_DATA_FORMAT_32_32;
		case 3:
			return V_008F0C_BUF_DATA_FORMAT_32_32_32;
		case 4:
			return V_008F0C_BUF_DATA_FORMAT_32_32_32_32;
		}
		break;
	case 64:
		/* Legacy double formats. */
		switch (desc->nr_channels) {
		case 1: /* 1 load */
			return V_008F0C_BUF_DATA_FORMAT_32_32;
		case 2: /* 1 load */
			return V_008F0C_BUF_DATA_FORMAT_32_32_32_32;
		case 3: /* 3 loads */
			return V_008F0C_BUF_DATA_FORMAT_32_32;
		case 4: /* 2 loads */
			return V_008F0C_BUF_DATA_FORMAT_32_32_32_32;
		}
		break;
	}

	return V_008F0C_BUF_DATA_FORMAT_INVALID;
}

static uint32_t si_translate_buffer_numformat(struct pipe_screen *screen,
					      const struct util_format_description *desc,
					      int first_non_void)
{
	if (desc->format == PIPE_FORMAT_R11G11B10_FLOAT)
		return V_008F0C_BUF_NUM_FORMAT_FLOAT;

	assert(first_non_void >= 0);

	switch (desc->channel[first_non_void].type) {
	case UTIL_FORMAT_TYPE_SIGNED:
	case UTIL_FORMAT_TYPE_FIXED:
		if (desc->channel[first_non_void].size >= 32 ||
		    desc->channel[first_non_void].pure_integer)
			return V_008F0C_BUF_NUM_FORMAT_SINT;
		else if (desc->channel[first_non_void].normalized)
			return V_008F0C_BUF_NUM_FORMAT_SNORM;
		else
			return V_008F0C_BUF_NUM_FORMAT_SSCALED;
		break;
	case UTIL_FORMAT_TYPE_UNSIGNED:
		if (desc->channel[first_non_void].size >= 32 ||
		    desc->channel[first_non_void].pure_integer)
			return V_008F0C_BUF_NUM_FORMAT_UINT;
		else if (desc->channel[first_non_void].normalized)
			return V_008F0C_BUF_NUM_FORMAT_UNORM;
		else
			return V_008F0C_BUF_NUM_FORMAT_USCALED;
		break;
	case UTIL_FORMAT_TYPE_FLOAT:
	default:
		return V_008F0C_BUF_NUM_FORMAT_FLOAT;
	}
}

static unsigned si_is_vertex_format_supported(struct pipe_screen *screen,
					      enum pipe_format format,
					      unsigned usage)
{
	const struct util_format_description *desc;
	int first_non_void;
	unsigned data_format;

	assert((usage & ~(PIPE_BIND_SHADER_IMAGE |
			  PIPE_BIND_SAMPLER_VIEW |
			  PIPE_BIND_VERTEX_BUFFER)) == 0);

	desc = util_format_description(format);
	if (!desc)
		return 0;

	/* There are no native 8_8_8 or 16_16_16 data formats, and we currently
	 * select 8_8_8_8 and 16_16_16_16 instead. This works reasonably well
	 * for read-only access (with caveats surrounding bounds checks), but
	 * obviously fails for write access which we have to implement for
	 * shader images. Luckily, OpenGL doesn't expect this to be supported
	 * anyway, and so the only impact is on PBO uploads / downloads, which
	 * shouldn't be expected to be fast for GL_RGB anyway.
	 */
	if (desc->block.bits == 3 * 8 ||
	    desc->block.bits == 3 * 16) {
		if (usage & (PIPE_BIND_SHADER_IMAGE | PIPE_BIND_SAMPLER_VIEW)) {
		    usage &= ~(PIPE_BIND_SHADER_IMAGE | PIPE_BIND_SAMPLER_VIEW);
			if (!usage)
				return 0;
		}
	}

	first_non_void = util_format_get_first_non_void_channel(format);
	data_format = si_translate_buffer_dataformat(screen, desc, first_non_void);
	if (data_format == V_008F0C_BUF_DATA_FORMAT_INVALID)
		return 0;

	return usage;
}

static bool si_is_colorbuffer_format_supported(enum pipe_format format)
{
	return si_translate_colorformat(format) != V_028C70_COLOR_INVALID &&
		si_translate_colorswap(format, false) != ~0U;
}

static bool si_is_zs_format_supported(enum pipe_format format)
{
	return si_translate_dbformat(format) != V_028040_Z_INVALID;
}

static boolean si_is_format_supported(struct pipe_screen *screen,
				      enum pipe_format format,
				      enum pipe_texture_target target,
				      unsigned sample_count,
				      unsigned usage)
{
	unsigned retval = 0;

	if (target >= PIPE_MAX_TEXTURE_TYPES) {
		PRINT_ERR("r600: unsupported texture type %d\n", target);
		return false;
	}

	if (!util_format_is_supported(format, usage))
		return false;

	if (sample_count > 1) {
		if (!screen->get_param(screen, PIPE_CAP_TEXTURE_MULTISAMPLE))
			return false;

		if (usage & PIPE_BIND_SHADER_IMAGE)
			return false;

		switch (sample_count) {
		case 2:
		case 4:
		case 8:
			break;
		case 16:
			if (format == PIPE_FORMAT_NONE)
				return true;
			else
				return false;
		default:
			return false;
		}
	}

	if (usage & (PIPE_BIND_SAMPLER_VIEW |
		     PIPE_BIND_SHADER_IMAGE)) {
		if (target == PIPE_BUFFER) {
			retval |= si_is_vertex_format_supported(
				screen, format, usage & (PIPE_BIND_SAMPLER_VIEW |
						         PIPE_BIND_SHADER_IMAGE));
		} else {
			if (si_is_sampler_format_supported(screen, format))
				retval |= usage & (PIPE_BIND_SAMPLER_VIEW |
						   PIPE_BIND_SHADER_IMAGE);
		}
	}

	if ((usage & (PIPE_BIND_RENDER_TARGET |
		      PIPE_BIND_DISPLAY_TARGET |
		      PIPE_BIND_SCANOUT |
		      PIPE_BIND_SHARED |
		      PIPE_BIND_BLENDABLE)) &&
	    si_is_colorbuffer_format_supported(format)) {
		retval |= usage &
			  (PIPE_BIND_RENDER_TARGET |
			   PIPE_BIND_DISPLAY_TARGET |
			   PIPE_BIND_SCANOUT |
			   PIPE_BIND_SHARED);
		if (!util_format_is_pure_integer(format) &&
		    !util_format_is_depth_or_stencil(format))
			retval |= usage & PIPE_BIND_BLENDABLE;
	}

	if ((usage & PIPE_BIND_DEPTH_STENCIL) &&
	    si_is_zs_format_supported(format)) {
		retval |= PIPE_BIND_DEPTH_STENCIL;
	}

	if (usage & PIPE_BIND_VERTEX_BUFFER) {
		retval |= si_is_vertex_format_supported(screen, format,
							PIPE_BIND_VERTEX_BUFFER);
	}

	if ((usage & PIPE_BIND_LINEAR) &&
	    !util_format_is_compressed(format) &&
	    !(usage & PIPE_BIND_DEPTH_STENCIL))
		retval |= PIPE_BIND_LINEAR;

	return retval == usage;
}

/*
 * framebuffer handling
 */

static void si_choose_spi_color_formats(struct r600_surface *surf,
					unsigned format, unsigned swap,
					unsigned ntype, bool is_depth)
{
	/* Alpha is needed for alpha-to-coverage.
	 * Blending may be with or without alpha.
	 */
	unsigned normal = 0; /* most optimal, may not support blending or export alpha */
	unsigned alpha = 0; /* exports alpha, but may not support blending */
	unsigned blend = 0; /* supports blending, but may not export alpha */
	unsigned blend_alpha = 0; /* least optimal, supports blending and exports alpha */

	/* Choose the SPI color formats. These are required values for RB+.
	 * Other chips have multiple choices, though they are not necessarily better.
	 */
	switch (format) {
	case V_028C70_COLOR_5_6_5:
	case V_028C70_COLOR_1_5_5_5:
	case V_028C70_COLOR_5_5_5_1:
	case V_028C70_COLOR_4_4_4_4:
	case V_028C70_COLOR_10_11_11:
	case V_028C70_COLOR_11_11_10:
	case V_028C70_COLOR_8:
	case V_028C70_COLOR_8_8:
	case V_028C70_COLOR_8_8_8_8:
	case V_028C70_COLOR_10_10_10_2:
	case V_028C70_COLOR_2_10_10_10:
		if (ntype == V_028C70_NUMBER_UINT)
			alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_UINT16_ABGR;
		else if (ntype == V_028C70_NUMBER_SINT)
			alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_SINT16_ABGR;
		else
			alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_FP16_ABGR;
		break;

	case V_028C70_COLOR_16:
	case V_028C70_COLOR_16_16:
	case V_028C70_COLOR_16_16_16_16:
		if (ntype == V_028C70_NUMBER_UNORM ||
		    ntype == V_028C70_NUMBER_SNORM) {
			/* UNORM16 and SNORM16 don't support blending */
			if (ntype == V_028C70_NUMBER_UNORM)
				normal = alpha = V_028714_SPI_SHADER_UNORM16_ABGR;
			else
				normal = alpha = V_028714_SPI_SHADER_SNORM16_ABGR;

			/* Use 32 bits per channel for blending. */
			if (format == V_028C70_COLOR_16) {
				if (swap == V_028C70_SWAP_STD) { /* R */
					blend = V_028714_SPI_SHADER_32_R;
					blend_alpha = V_028714_SPI_SHADER_32_AR;
				} else if (swap == V_028C70_SWAP_ALT_REV) /* A */
					blend = blend_alpha = V_028714_SPI_SHADER_32_AR;
				else
					assert(0);
			} else if (format == V_028C70_COLOR_16_16) {
				if (swap == V_028C70_SWAP_STD) { /* RG */
					blend = V_028714_SPI_SHADER_32_GR;
					blend_alpha = V_028714_SPI_SHADER_32_ABGR;
				} else if (swap == V_028C70_SWAP_ALT) /* RA */
					blend = blend_alpha = V_028714_SPI_SHADER_32_AR;
				else
					assert(0);
			} else /* 16_16_16_16 */
				blend = blend_alpha = V_028714_SPI_SHADER_32_ABGR;
		} else if (ntype == V_028C70_NUMBER_UINT)
			alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_UINT16_ABGR;
		else if (ntype == V_028C70_NUMBER_SINT)
			alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_SINT16_ABGR;
		else if (ntype == V_028C70_NUMBER_FLOAT)
			alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_FP16_ABGR;
		else
			assert(0);
		break;

	case V_028C70_COLOR_32:
		if (swap == V_028C70_SWAP_STD) { /* R */
			blend = normal = V_028714_SPI_SHADER_32_R;
			alpha = blend_alpha = V_028714_SPI_SHADER_32_AR;
		} else if (swap == V_028C70_SWAP_ALT_REV) /* A */
			alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_32_AR;
		else
			assert(0);
		break;

	case V_028C70_COLOR_32_32:
		if (swap == V_028C70_SWAP_STD) { /* RG */
			blend = normal = V_028714_SPI_SHADER_32_GR;
			alpha = blend_alpha = V_028714_SPI_SHADER_32_ABGR;
		} else if (swap == V_028C70_SWAP_ALT) /* RA */
			alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_32_AR;
		else
			assert(0);
		break;

	case V_028C70_COLOR_32_32_32_32:
	case V_028C70_COLOR_8_24:
	case V_028C70_COLOR_24_8:
	case V_028C70_COLOR_X24_8_32_FLOAT:
		alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_32_ABGR;
		break;

	default:
		assert(0);
		return;
	}

	/* The DB->CB copy needs 32_ABGR. */
	if (is_depth)
		alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_32_ABGR;

	surf->spi_shader_col_format = normal;
	surf->spi_shader_col_format_alpha = alpha;
	surf->spi_shader_col_format_blend = blend;
	surf->spi_shader_col_format_blend_alpha = blend_alpha;
}

static void si_initialize_color_surface(struct si_context *sctx,
					struct r600_surface *surf)
{
	struct r600_texture *rtex = (struct r600_texture*)surf->base.texture;
	unsigned color_info, color_attrib;
	unsigned format, swap, ntype, endian;
	const struct util_format_description *desc;
	int firstchan;
	unsigned blend_clamp = 0, blend_bypass = 0;

	desc = util_format_description(surf->base.format);
	for (firstchan = 0; firstchan < 4; firstchan++) {
		if (desc->channel[firstchan].type != UTIL_FORMAT_TYPE_VOID) {
			break;
		}
	}
	if (firstchan == 4 || desc->channel[firstchan].type == UTIL_FORMAT_TYPE_FLOAT) {
		ntype = V_028C70_NUMBER_FLOAT;
	} else {
		ntype = V_028C70_NUMBER_UNORM;
		if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB)
			ntype = V_028C70_NUMBER_SRGB;
		else if (desc->channel[firstchan].type == UTIL_FORMAT_TYPE_SIGNED) {
			if (desc->channel[firstchan].pure_integer) {
				ntype = V_028C70_NUMBER_SINT;
			} else {
				assert(desc->channel[firstchan].normalized);
				ntype = V_028C70_NUMBER_SNORM;
			}
		} else if (desc->channel[firstchan].type == UTIL_FORMAT_TYPE_UNSIGNED) {
			if (desc->channel[firstchan].pure_integer) {
				ntype = V_028C70_NUMBER_UINT;
			} else {
				assert(desc->channel[firstchan].normalized);
				ntype = V_028C70_NUMBER_UNORM;
			}
		}
	}

	format = si_translate_colorformat(surf->base.format);
	if (format == V_028C70_COLOR_INVALID) {
		PRINT_ERR("Invalid CB format: %d, disabling CB.\n", surf->base.format);
	}
	assert(format != V_028C70_COLOR_INVALID);
	swap = si_translate_colorswap(surf->base.format, false);
	endian = si_colorformat_endian_swap(format);

	/* blend clamp should be set for all NORM/SRGB types */
	if (ntype == V_028C70_NUMBER_UNORM ||
	    ntype == V_028C70_NUMBER_SNORM ||
	    ntype == V_028C70_NUMBER_SRGB)
		blend_clamp = 1;

	/* set blend bypass according to docs if SINT/UINT or
	   8/24 COLOR variants */
	if (ntype == V_028C70_NUMBER_UINT || ntype == V_028C70_NUMBER_SINT ||
	    format == V_028C70_COLOR_8_24 || format == V_028C70_COLOR_24_8 ||
	    format == V_028C70_COLOR_X24_8_32_FLOAT) {
		blend_clamp = 0;
		blend_bypass = 1;
	}

	if (ntype == V_028C70_NUMBER_UINT || ntype == V_028C70_NUMBER_SINT) {
		if (format == V_028C70_COLOR_8 ||
		    format == V_028C70_COLOR_8_8 ||
		    format == V_028C70_COLOR_8_8_8_8)
			surf->color_is_int8 = true;
		else if (format == V_028C70_COLOR_10_10_10_2 ||
			 format == V_028C70_COLOR_2_10_10_10)
			surf->color_is_int10 = true;
	}

	color_info = S_028C70_FORMAT(format) |
		S_028C70_COMP_SWAP(swap) |
		S_028C70_BLEND_CLAMP(blend_clamp) |
		S_028C70_BLEND_BYPASS(blend_bypass) |
		S_028C70_SIMPLE_FLOAT(1) |
		S_028C70_ROUND_MODE(ntype != V_028C70_NUMBER_UNORM &&
				    ntype != V_028C70_NUMBER_SNORM &&
				    ntype != V_028C70_NUMBER_SRGB &&
				    format != V_028C70_COLOR_8_24 &&
				    format != V_028C70_COLOR_24_8) |
		S_028C70_NUMBER_TYPE(ntype) |
		S_028C70_ENDIAN(endian);

	/* Intensity is implemented as Red, so treat it that way. */
	color_attrib = S_028C74_FORCE_DST_ALPHA_1(desc->swizzle[3] == PIPE_SWIZZLE_1 ||
						  util_format_is_intensity(surf->base.format));

	if (rtex->resource.b.b.nr_samples > 1) {
		unsigned log_samples = util_logbase2(rtex->resource.b.b.nr_samples);

		color_attrib |= S_028C74_NUM_SAMPLES(log_samples) |
				S_028C74_NUM_FRAGMENTS(log_samples);

		if (rtex->fmask.size) {
			color_info |= S_028C70_COMPRESSION(1);
			unsigned fmask_bankh = util_logbase2(rtex->fmask.bank_height);

			if (sctx->chip_class == SI) {
				/* due to a hw bug, FMASK_BANK_HEIGHT must be set on SI too */
				color_attrib |= S_028C74_FMASK_BANK_HEIGHT(fmask_bankh);
			}
		}
	}

	if (sctx->chip_class >= VI) {
		unsigned max_uncompressed_block_size = V_028C78_MAX_BLOCK_SIZE_256B;
		unsigned min_compressed_block_size = V_028C78_MIN_BLOCK_SIZE_32B;

		/* amdvlk: [min-compressed-block-size] should be set to 32 for dGPU and
		   64 for APU because all of our APUs to date use DIMMs which have
		   a request granularity size of 64B while all other chips have a
		   32B request size */
		if (!sctx->screen->info.has_dedicated_vram)
			min_compressed_block_size = V_028C78_MIN_BLOCK_SIZE_64B;

		if (rtex->resource.b.b.nr_samples > 1) {
			if (rtex->surface.bpe == 1)
				max_uncompressed_block_size = V_028C78_MAX_BLOCK_SIZE_64B;
			else if (rtex->surface.bpe == 2)
				max_uncompressed_block_size = V_028C78_MAX_BLOCK_SIZE_128B;
		}

		surf->cb_dcc_control = S_028C78_MAX_UNCOMPRESSED_BLOCK_SIZE(max_uncompressed_block_size) |
				       S_028C78_MIN_COMPRESSED_BLOCK_SIZE(min_compressed_block_size) |
		                       S_028C78_INDEPENDENT_64B_BLOCKS(1);
	}

	/* This must be set for fast clear to work without FMASK. */
	if (!rtex->fmask.size && sctx->chip_class == SI) {
		unsigned bankh = util_logbase2(rtex->surface.u.legacy.bankh);
		color_attrib |= S_028C74_FMASK_BANK_HEIGHT(bankh);
	}

	unsigned color_view = S_028C6C_SLICE_START(surf->base.u.tex.first_layer) |
			      S_028C6C_SLICE_MAX(surf->base.u.tex.last_layer);

	if (sctx->chip_class >= GFX9) {
		unsigned mip0_depth = util_max_layer(&rtex->resource.b.b, 0);

		color_view |= S_028C6C_MIP_LEVEL(surf->base.u.tex.level);
		color_attrib |= S_028C74_MIP0_DEPTH(mip0_depth) |
				S_028C74_RESOURCE_TYPE(rtex->surface.u.gfx9.resource_type);
		surf->cb_color_attrib2 = S_028C68_MIP0_WIDTH(surf->width0 - 1) |
					 S_028C68_MIP0_HEIGHT(surf->height0 - 1) |
					 S_028C68_MAX_MIP(rtex->resource.b.b.last_level);
	}

	surf->cb_color_view = color_view;
	surf->cb_color_info = color_info;
	surf->cb_color_attrib = color_attrib;

	/* Determine pixel shader export format */
	si_choose_spi_color_formats(surf, format, swap, ntype, rtex->is_depth);

	surf->color_initialized = true;
}

static void si_init_depth_surface(struct si_context *sctx,
				  struct r600_surface *surf)
{
	struct r600_texture *rtex = (struct r600_texture*)surf->base.texture;
	unsigned level = surf->base.u.tex.level;
	unsigned format, stencil_format;
	uint32_t z_info, s_info;

	format = si_translate_dbformat(rtex->db_render_format);
	stencil_format = rtex->surface.has_stencil ?
				 V_028044_STENCIL_8 : V_028044_STENCIL_INVALID;

	assert(format != V_028040_Z_INVALID);
	if (format == V_028040_Z_INVALID)
		PRINT_ERR("Invalid DB format: %d, disabling DB.\n", rtex->resource.b.b.format);

	surf->db_depth_view = S_028008_SLICE_START(surf->base.u.tex.first_layer) |
			      S_028008_SLICE_MAX(surf->base.u.tex.last_layer);
	surf->db_htile_data_base = 0;
	surf->db_htile_surface = 0;

	if (sctx->chip_class >= GFX9) {
		assert(rtex->surface.u.gfx9.surf_offset == 0);
		surf->db_depth_base = rtex->resource.gpu_address >> 8;
		surf->db_stencil_base = (rtex->resource.gpu_address +
					 rtex->surface.u.gfx9.stencil_offset) >> 8;
		z_info = S_028038_FORMAT(format) |
			 S_028038_NUM_SAMPLES(util_logbase2(rtex->resource.b.b.nr_samples)) |
			 S_028038_SW_MODE(rtex->surface.u.gfx9.surf.swizzle_mode) |
			 S_028038_MAXMIP(rtex->resource.b.b.last_level);
		s_info = S_02803C_FORMAT(stencil_format) |
			 S_02803C_SW_MODE(rtex->surface.u.gfx9.stencil.swizzle_mode);
		surf->db_z_info2 = S_028068_EPITCH(rtex->surface.u.gfx9.surf.epitch);
		surf->db_stencil_info2 = S_02806C_EPITCH(rtex->surface.u.gfx9.stencil.epitch);
		surf->db_depth_view |= S_028008_MIPID(level);
		surf->db_depth_size = S_02801C_X_MAX(rtex->resource.b.b.width0 - 1) |
				      S_02801C_Y_MAX(rtex->resource.b.b.height0 - 1);

		if (si_htile_enabled(rtex, level)) {
			z_info |= S_028038_TILE_SURFACE_ENABLE(1) |
				  S_028038_ALLOW_EXPCLEAR(1);

			if (rtex->tc_compatible_htile) {
				unsigned max_zplanes = 4;

				if (rtex->db_render_format == PIPE_FORMAT_Z16_UNORM &&
				    rtex->resource.b.b.nr_samples > 1)
					max_zplanes = 2;

				z_info |= S_028038_DECOMPRESS_ON_N_ZPLANES(max_zplanes + 1) |
					  S_028038_ITERATE_FLUSH(1);
				s_info |= S_02803C_ITERATE_FLUSH(1);
			}

			if (rtex->surface.has_stencil) {
				/* Stencil buffer workaround ported from the SI-CI-VI code.
				 * See that for explanation.
				 */
				s_info |= S_02803C_ALLOW_EXPCLEAR(rtex->resource.b.b.nr_samples <= 1);
			} else {
				/* Use all HTILE for depth if there's no stencil. */
				s_info |= S_02803C_TILE_STENCIL_DISABLE(1);
			}

			surf->db_htile_data_base = (rtex->resource.gpu_address +
						    rtex->htile_offset) >> 8;
			surf->db_htile_surface = S_028ABC_FULL_CACHE(1) |
						 S_028ABC_PIPE_ALIGNED(rtex->surface.u.gfx9.htile.pipe_aligned) |
						 S_028ABC_RB_ALIGNED(rtex->surface.u.gfx9.htile.rb_aligned);
		}
	} else {
		/* SI-CI-VI */
		struct legacy_surf_level *levelinfo = &rtex->surface.u.legacy.level[level];

		assert(levelinfo->nblk_x % 8 == 0 && levelinfo->nblk_y % 8 == 0);

		surf->db_depth_base = (rtex->resource.gpu_address +
				       rtex->surface.u.legacy.level[level].offset) >> 8;
		surf->db_stencil_base = (rtex->resource.gpu_address +
					 rtex->surface.u.legacy.stencil_level[level].offset) >> 8;

		z_info = S_028040_FORMAT(format) |
			 S_028040_NUM_SAMPLES(util_logbase2(rtex->resource.b.b.nr_samples));
		s_info = S_028044_FORMAT(stencil_format);
		surf->db_depth_info = S_02803C_ADDR5_SWIZZLE_MASK(!rtex->tc_compatible_htile);

		if (sctx->chip_class >= CIK) {
			struct radeon_info *info = &sctx->screen->info;
			unsigned index = rtex->surface.u.legacy.tiling_index[level];
			unsigned stencil_index = rtex->surface.u.legacy.stencil_tiling_index[level];
			unsigned macro_index = rtex->surface.u.legacy.macro_tile_index;
			unsigned tile_mode = info->si_tile_mode_array[index];
			unsigned stencil_tile_mode = info->si_tile_mode_array[stencil_index];
			unsigned macro_mode = info->cik_macrotile_mode_array[macro_index];

			surf->db_depth_info |=
				S_02803C_ARRAY_MODE(G_009910_ARRAY_MODE(tile_mode)) |
				S_02803C_PIPE_CONFIG(G_009910_PIPE_CONFIG(tile_mode)) |
				S_02803C_BANK_WIDTH(G_009990_BANK_WIDTH(macro_mode)) |
				S_02803C_BANK_HEIGHT(G_009990_BANK_HEIGHT(macro_mode)) |
				S_02803C_MACRO_TILE_ASPECT(G_009990_MACRO_TILE_ASPECT(macro_mode)) |
				S_02803C_NUM_BANKS(G_009990_NUM_BANKS(macro_mode));
			z_info |= S_028040_TILE_SPLIT(G_009910_TILE_SPLIT(tile_mode));
			s_info |= S_028044_TILE_SPLIT(G_009910_TILE_SPLIT(stencil_tile_mode));
		} else {
			unsigned tile_mode_index = si_tile_mode_index(rtex, level, false);
			z_info |= S_028040_TILE_MODE_INDEX(tile_mode_index);
			tile_mode_index = si_tile_mode_index(rtex, level, true);
			s_info |= S_028044_TILE_MODE_INDEX(tile_mode_index);
		}

		surf->db_depth_size = S_028058_PITCH_TILE_MAX((levelinfo->nblk_x / 8) - 1) |
				      S_028058_HEIGHT_TILE_MAX((levelinfo->nblk_y / 8) - 1);
		surf->db_depth_slice = S_02805C_SLICE_TILE_MAX((levelinfo->nblk_x *
								levelinfo->nblk_y) / 64 - 1);

		if (si_htile_enabled(rtex, level)) {
			z_info |= S_028040_TILE_SURFACE_ENABLE(1) |
				  S_028040_ALLOW_EXPCLEAR(1);

			if (rtex->surface.has_stencil) {
				/* Workaround: For a not yet understood reason, the
				 * combination of MSAA, fast stencil clear and stencil
				 * decompress messes with subsequent stencil buffer
				 * uses. Problem was reproduced on Verde, Bonaire,
				 * Tonga, and Carrizo.
				 *
				 * Disabling EXPCLEAR works around the problem.
				 *
				 * Check piglit's arb_texture_multisample-stencil-clear
				 * test if you want to try changing this.
				 */
				if (rtex->resource.b.b.nr_samples <= 1)
					s_info |= S_028044_ALLOW_EXPCLEAR(1);
			} else if (!rtex->tc_compatible_htile) {
				/* Use all of the htile_buffer for depth if there's no stencil.
				 * This must not be set when TC-compatible HTILE is enabled
				 * due to a hw bug.
				 */
				s_info |= S_028044_TILE_STENCIL_DISABLE(1);
			}

			surf->db_htile_data_base = (rtex->resource.gpu_address +
						    rtex->htile_offset) >> 8;
			surf->db_htile_surface = S_028ABC_FULL_CACHE(1);

			if (rtex->tc_compatible_htile) {
				surf->db_htile_surface |= S_028ABC_TC_COMPATIBLE(1);

				if (rtex->resource.b.b.nr_samples <= 1)
					z_info |= S_028040_DECOMPRESS_ON_N_ZPLANES(5);
				else if (rtex->resource.b.b.nr_samples <= 4)
					z_info |= S_028040_DECOMPRESS_ON_N_ZPLANES(3);
				else
					z_info |= S_028040_DECOMPRESS_ON_N_ZPLANES(2);
			}
		}
	}

	surf->db_z_info = z_info;
	surf->db_stencil_info = s_info;

	surf->depth_initialized = true;
}

void si_update_fb_dirtiness_after_rendering(struct si_context *sctx)
{
	if (sctx->decompression_enabled)
		return;

	if (sctx->framebuffer.state.zsbuf) {
		struct pipe_surface *surf = sctx->framebuffer.state.zsbuf;
		struct r600_texture *rtex = (struct r600_texture *)surf->texture;

		rtex->dirty_level_mask |= 1 << surf->u.tex.level;

		if (rtex->surface.has_stencil)
			rtex->stencil_dirty_level_mask |= 1 << surf->u.tex.level;
	}

	unsigned compressed_cb_mask = sctx->framebuffer.compressed_cb_mask;
	while (compressed_cb_mask) {
		unsigned i = u_bit_scan(&compressed_cb_mask);
		struct pipe_surface *surf = sctx->framebuffer.state.cbufs[i];
		struct r600_texture *rtex = (struct r600_texture*)surf->texture;

		if (rtex->fmask.size)
			rtex->dirty_level_mask |= 1 << surf->u.tex.level;
		if (rtex->dcc_gather_statistics)
			rtex->separate_dcc_dirty = true;
	}
}

static void si_dec_framebuffer_counters(const struct pipe_framebuffer_state *state)
{
	for (int i = 0; i < state->nr_cbufs; ++i) {
		struct r600_surface *surf = NULL;
		struct r600_texture *rtex;

		if (!state->cbufs[i])
			continue;
		surf = (struct r600_surface*)state->cbufs[i];
		rtex = (struct r600_texture*)surf->base.texture;

		p_atomic_dec(&rtex->framebuffers_bound);
	}
}

static void si_set_framebuffer_state(struct pipe_context *ctx,
				     const struct pipe_framebuffer_state *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct pipe_constant_buffer constbuf = {0};
	struct r600_surface *surf = NULL;
	struct r600_texture *rtex;
	bool old_any_dst_linear = sctx->framebuffer.any_dst_linear;
	unsigned old_nr_samples = sctx->framebuffer.nr_samples;
	unsigned old_colorbuf_enabled_4bit = sctx->framebuffer.colorbuf_enabled_4bit;
	bool old_has_zsbuf = !!sctx->framebuffer.state.zsbuf;
	bool old_has_stencil =
		old_has_zsbuf &&
		((struct r600_texture*)sctx->framebuffer.state.zsbuf->texture)->surface.has_stencil;
	bool unbound = false;
	int i;

	si_update_fb_dirtiness_after_rendering(sctx);

	for (i = 0; i < sctx->framebuffer.state.nr_cbufs; i++) {
		if (!sctx->framebuffer.state.cbufs[i])
			continue;

		rtex = (struct r600_texture*)sctx->framebuffer.state.cbufs[i]->texture;
		if (rtex->dcc_gather_statistics)
			vi_separate_dcc_stop_query(sctx, rtex);
	}

	/* Disable DCC if the formats are incompatible. */
	for (i = 0; i < state->nr_cbufs; i++) {
		if (!state->cbufs[i])
			continue;

		surf = (struct r600_surface*)state->cbufs[i];
		rtex = (struct r600_texture*)surf->base.texture;

		if (!surf->dcc_incompatible)
			continue;

		/* Since the DCC decompression calls back into set_framebuffer-
		 * _state, we need to unbind the framebuffer, so that
		 * vi_separate_dcc_stop_query isn't called twice with the same
		 * color buffer.
		 */
		if (!unbound) {
			util_copy_framebuffer_state(&sctx->framebuffer.state, NULL);
			unbound = true;
		}

		if (vi_dcc_enabled(rtex, surf->base.u.tex.level))
			if (!si_texture_disable_dcc(sctx, rtex))
				si_decompress_dcc(sctx, rtex);

		surf->dcc_incompatible = false;
	}

	/* Only flush TC when changing the framebuffer state, because
	 * the only client not using TC that can change textures is
	 * the framebuffer.
	 *
	 * Wait for compute shaders because of possible transitions:
	 * - FB write -> shader read
	 * - shader write -> FB read
	 *
	 * DB caches are flushed on demand (using si_decompress_textures).
	 *
	 * When MSAA is enabled, CB and TC caches are flushed on demand
	 * (after FMASK decompression). Shader write -> FB read transitions
	 * cannot happen for MSAA textures, because MSAA shader images are
	 * not supported.
	 *
	 * Only flush and wait for CB if there is actually a bound color buffer.
	 */
	if (sctx->framebuffer.uncompressed_cb_mask)
		si_make_CB_shader_coherent(sctx, sctx->framebuffer.nr_samples,
					   sctx->framebuffer.CB_has_shader_readable_metadata);

	sctx->flags |= SI_CONTEXT_CS_PARTIAL_FLUSH;

	/* u_blitter doesn't invoke depth decompression when it does multiple
	 * blits in a row, but the only case when it matters for DB is when
	 * doing generate_mipmap. So here we flush DB manually between
	 * individual generate_mipmap blits.
	 * Note that lower mipmap levels aren't compressed.
	 */
	if (sctx->generate_mipmap_for_depth) {
		si_make_DB_shader_coherent(sctx, 1, false,
					   sctx->framebuffer.DB_has_shader_readable_metadata);
	} else if (sctx->chip_class == GFX9) {
		/* It appears that DB metadata "leaks" in a sequence of:
		 *  - depth clear
		 *  - DCC decompress for shader image writes (with DB disabled)
		 *  - render with DEPTH_BEFORE_SHADER=1
		 * Flushing DB metadata works around the problem.
		 */
		sctx->flags |= SI_CONTEXT_FLUSH_AND_INV_DB_META;
	}

	/* Take the maximum of the old and new count. If the new count is lower,
	 * dirtying is needed to disable the unbound colorbuffers.
	 */
	sctx->framebuffer.dirty_cbufs |=
		(1 << MAX2(sctx->framebuffer.state.nr_cbufs, state->nr_cbufs)) - 1;
	sctx->framebuffer.dirty_zsbuf |= sctx->framebuffer.state.zsbuf != state->zsbuf;

	si_dec_framebuffer_counters(&sctx->framebuffer.state);
	util_copy_framebuffer_state(&sctx->framebuffer.state, state);

	sctx->framebuffer.colorbuf_enabled_4bit = 0;
	sctx->framebuffer.spi_shader_col_format = 0;
	sctx->framebuffer.spi_shader_col_format_alpha = 0;
	sctx->framebuffer.spi_shader_col_format_blend = 0;
	sctx->framebuffer.spi_shader_col_format_blend_alpha = 0;
	sctx->framebuffer.color_is_int8 = 0;
	sctx->framebuffer.color_is_int10 = 0;

	sctx->framebuffer.compressed_cb_mask = 0;
	sctx->framebuffer.uncompressed_cb_mask = 0;
	sctx->framebuffer.nr_samples = util_framebuffer_get_num_samples(state);
	sctx->framebuffer.log_samples = util_logbase2(sctx->framebuffer.nr_samples);
	sctx->framebuffer.any_dst_linear = false;
	sctx->framebuffer.CB_has_shader_readable_metadata = false;
	sctx->framebuffer.DB_has_shader_readable_metadata = false;

	for (i = 0; i < state->nr_cbufs; i++) {
		if (!state->cbufs[i])
			continue;

		surf = (struct r600_surface*)state->cbufs[i];
		rtex = (struct r600_texture*)surf->base.texture;

		if (!surf->color_initialized) {
			si_initialize_color_surface(sctx, surf);
		}

		sctx->framebuffer.colorbuf_enabled_4bit |= 0xf << (i * 4);
		sctx->framebuffer.spi_shader_col_format |=
			surf->spi_shader_col_format << (i * 4);
		sctx->framebuffer.spi_shader_col_format_alpha |=
			surf->spi_shader_col_format_alpha << (i * 4);
		sctx->framebuffer.spi_shader_col_format_blend |=
			surf->spi_shader_col_format_blend << (i * 4);
		sctx->framebuffer.spi_shader_col_format_blend_alpha |=
			surf->spi_shader_col_format_blend_alpha << (i * 4);

		if (surf->color_is_int8)
			sctx->framebuffer.color_is_int8 |= 1 << i;
		if (surf->color_is_int10)
			sctx->framebuffer.color_is_int10 |= 1 << i;

		if (rtex->fmask.size)
			sctx->framebuffer.compressed_cb_mask |= 1 << i;
		else
			sctx->framebuffer.uncompressed_cb_mask |= 1 << i;

		if (rtex->surface.is_linear)
			sctx->framebuffer.any_dst_linear = true;

		if (vi_dcc_enabled(rtex, surf->base.u.tex.level))
			sctx->framebuffer.CB_has_shader_readable_metadata = true;

		si_context_add_resource_size(sctx, surf->base.texture);

		p_atomic_inc(&rtex->framebuffers_bound);

		if (rtex->dcc_gather_statistics) {
			/* Dirty tracking must be enabled for DCC usage analysis. */
			sctx->framebuffer.compressed_cb_mask |= 1 << i;
			vi_separate_dcc_start_query(sctx, rtex);
		}
	}

	struct r600_texture *zstex = NULL;

	if (state->zsbuf) {
		surf = (struct r600_surface*)state->zsbuf;
		zstex = (struct r600_texture*)surf->base.texture;

		if (!surf->depth_initialized) {
			si_init_depth_surface(sctx, surf);
		}

		if (vi_tc_compat_htile_enabled(zstex, surf->base.u.tex.level))
			sctx->framebuffer.DB_has_shader_readable_metadata = true;

		si_context_add_resource_size(sctx, surf->base.texture);
	}

	si_update_ps_colorbuf0_slot(sctx);
	si_update_poly_offset_state(sctx);
	si_mark_atom_dirty(sctx, &sctx->cb_render_state);
	si_mark_atom_dirty(sctx, &sctx->framebuffer.atom);

	if (sctx->screen->dpbb_allowed)
		si_mark_atom_dirty(sctx, &sctx->dpbb_state);

	if (sctx->framebuffer.any_dst_linear != old_any_dst_linear)
		si_mark_atom_dirty(sctx, &sctx->msaa_config);

	if (sctx->screen->has_out_of_order_rast &&
	    (sctx->framebuffer.colorbuf_enabled_4bit != old_colorbuf_enabled_4bit ||
	     !!sctx->framebuffer.state.zsbuf != old_has_zsbuf ||
	     (zstex && zstex->surface.has_stencil != old_has_stencil)))
		si_mark_atom_dirty(sctx, &sctx->msaa_config);

	if (sctx->framebuffer.nr_samples != old_nr_samples) {
		si_mark_atom_dirty(sctx, &sctx->msaa_config);
		si_mark_atom_dirty(sctx, &sctx->db_render_state);

		/* Set sample locations as fragment shader constants. */
		switch (sctx->framebuffer.nr_samples) {
		case 1:
			constbuf.user_buffer = sctx->sample_locations_1x;
			break;
		case 2:
			constbuf.user_buffer = sctx->sample_locations_2x;
			break;
		case 4:
			constbuf.user_buffer = sctx->sample_locations_4x;
			break;
		case 8:
			constbuf.user_buffer = sctx->sample_locations_8x;
			break;
		case 16:
			constbuf.user_buffer = sctx->sample_locations_16x;
			break;
		default:
			PRINT_ERR("Requested an invalid number of samples %i.\n",
				 sctx->framebuffer.nr_samples);
			assert(0);
		}
		constbuf.buffer_size = sctx->framebuffer.nr_samples * 2 * 4;
		si_set_rw_buffer(sctx, SI_PS_CONST_SAMPLE_POSITIONS, &constbuf);

		si_mark_atom_dirty(sctx, &sctx->msaa_sample_locs.atom);
	}

	sctx->do_update_shaders = true;

	if (!sctx->decompression_enabled) {
		/* Prevent textures decompression when the framebuffer state
		 * changes come from the decompression passes themselves.
		 */
		sctx->need_check_render_feedback = true;
	}
}

static void si_emit_framebuffer_state(struct si_context *sctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = sctx->gfx_cs;
	struct pipe_framebuffer_state *state = &sctx->framebuffer.state;
	unsigned i, nr_cbufs = state->nr_cbufs;
	struct r600_texture *tex = NULL;
	struct r600_surface *cb = NULL;
	unsigned cb_color_info = 0;

	/* Colorbuffers. */
	for (i = 0; i < nr_cbufs; i++) {
		uint64_t cb_color_base, cb_color_fmask, cb_dcc_base;
		unsigned cb_color_attrib;

		if (!(sctx->framebuffer.dirty_cbufs & (1 << i)))
			continue;

		cb = (struct r600_surface*)state->cbufs[i];
		if (!cb) {
			radeon_set_context_reg(cs, R_028C70_CB_COLOR0_INFO + i * 0x3C,
					       S_028C70_FORMAT(V_028C70_COLOR_INVALID));
			continue;
		}

		tex = (struct r600_texture *)cb->base.texture;
		radeon_add_to_buffer_list(sctx, sctx->gfx_cs,
				      &tex->resource, RADEON_USAGE_READWRITE,
				      tex->resource.b.b.nr_samples > 1 ?
					      RADEON_PRIO_COLOR_BUFFER_MSAA :
					      RADEON_PRIO_COLOR_BUFFER);

		if (tex->cmask_buffer && tex->cmask_buffer != &tex->resource) {
			radeon_add_to_buffer_list(sctx, sctx->gfx_cs,
				tex->cmask_buffer, RADEON_USAGE_READWRITE,
				RADEON_PRIO_CMASK);
		}

		if (tex->dcc_separate_buffer)
			radeon_add_to_buffer_list(sctx, sctx->gfx_cs,
						  tex->dcc_separate_buffer,
						  RADEON_USAGE_READWRITE,
						  RADEON_PRIO_DCC);

		/* Compute mutable surface parameters. */
		cb_color_base = tex->resource.gpu_address >> 8;
		cb_color_fmask = 0;
		cb_dcc_base = 0;
		cb_color_info = cb->cb_color_info | tex->cb_color_info;
		cb_color_attrib = cb->cb_color_attrib;

		if (tex->fmask.size) {
			cb_color_fmask = (tex->resource.gpu_address + tex->fmask.offset) >> 8;
			cb_color_fmask |= tex->fmask.tile_swizzle;
		}

		/* Set up DCC. */
		if (vi_dcc_enabled(tex, cb->base.u.tex.level)) {
			bool is_msaa_resolve_dst = state->cbufs[0] &&
						   state->cbufs[0]->texture->nr_samples > 1 &&
						   state->cbufs[1] == &cb->base &&
						   state->cbufs[1]->texture->nr_samples <= 1;

			if (!is_msaa_resolve_dst)
				cb_color_info |= S_028C70_DCC_ENABLE(1);

			cb_dcc_base = ((!tex->dcc_separate_buffer ? tex->resource.gpu_address : 0) +
				       tex->dcc_offset) >> 8;
			cb_dcc_base |= tex->surface.tile_swizzle;
		}

		if (sctx->chip_class >= GFX9) {
			struct gfx9_surf_meta_flags meta;

			if (tex->dcc_offset)
				meta = tex->surface.u.gfx9.dcc;
			else
				meta = tex->surface.u.gfx9.cmask;

			/* Set mutable surface parameters. */
			cb_color_base += tex->surface.u.gfx9.surf_offset >> 8;
			cb_color_base |= tex->surface.tile_swizzle;
			if (!tex->fmask.size)
				cb_color_fmask = cb_color_base;
			cb_color_attrib |= S_028C74_COLOR_SW_MODE(tex->surface.u.gfx9.surf.swizzle_mode) |
					   S_028C74_FMASK_SW_MODE(tex->surface.u.gfx9.fmask.swizzle_mode) |
					   S_028C74_RB_ALIGNED(meta.rb_aligned) |
					   S_028C74_PIPE_ALIGNED(meta.pipe_aligned);

			radeon_set_context_reg_seq(cs, R_028C60_CB_COLOR0_BASE + i * 0x3C, 15);
			radeon_emit(cs, cb_color_base);		/* CB_COLOR0_BASE */
			radeon_emit(cs, S_028C64_BASE_256B(cb_color_base >> 32)); /* CB_COLOR0_BASE_EXT */
			radeon_emit(cs, cb->cb_color_attrib2);	/* CB_COLOR0_ATTRIB2 */
			radeon_emit(cs, cb->cb_color_view);	/* CB_COLOR0_VIEW */
			radeon_emit(cs, cb_color_info);		/* CB_COLOR0_INFO */
			radeon_emit(cs, cb_color_attrib);	/* CB_COLOR0_ATTRIB */
			radeon_emit(cs, cb->cb_dcc_control);	/* CB_COLOR0_DCC_CONTROL */
			radeon_emit(cs, tex->cmask.base_address_reg); /* CB_COLOR0_CMASK */
			radeon_emit(cs, S_028C80_BASE_256B(tex->cmask.base_address_reg >> 32)); /* CB_COLOR0_CMASK_BASE_EXT */
			radeon_emit(cs, cb_color_fmask);	/* CB_COLOR0_FMASK */
			radeon_emit(cs, S_028C88_BASE_256B(cb_color_fmask >> 32)); /* CB_COLOR0_FMASK_BASE_EXT */
			radeon_emit(cs, tex->color_clear_value[0]); /* CB_COLOR0_CLEAR_WORD0 */
			radeon_emit(cs, tex->color_clear_value[1]); /* CB_COLOR0_CLEAR_WORD1 */
			radeon_emit(cs, cb_dcc_base);		/* CB_COLOR0_DCC_BASE */
			radeon_emit(cs, S_028C98_BASE_256B(cb_dcc_base >> 32)); /* CB_COLOR0_DCC_BASE_EXT */

			radeon_set_context_reg(cs, R_0287A0_CB_MRT0_EPITCH + i * 4,
					       S_0287A0_EPITCH(tex->surface.u.gfx9.surf.epitch));
		} else {
			/* Compute mutable surface parameters (SI-CI-VI). */
			const struct legacy_surf_level *level_info =
				&tex->surface.u.legacy.level[cb->base.u.tex.level];
			unsigned pitch_tile_max, slice_tile_max, tile_mode_index;
			unsigned cb_color_pitch, cb_color_slice, cb_color_fmask_slice;

			cb_color_base += level_info->offset >> 8;
			/* Only macrotiled modes can set tile swizzle. */
			if (level_info->mode == RADEON_SURF_MODE_2D)
				cb_color_base |= tex->surface.tile_swizzle;

			if (!tex->fmask.size)
				cb_color_fmask = cb_color_base;
			if (cb_dcc_base)
				cb_dcc_base += level_info->dcc_offset >> 8;

			pitch_tile_max = level_info->nblk_x / 8 - 1;
			slice_tile_max = level_info->nblk_x *
					 level_info->nblk_y / 64 - 1;
			tile_mode_index = si_tile_mode_index(tex, cb->base.u.tex.level, false);

			cb_color_attrib |= S_028C74_TILE_MODE_INDEX(tile_mode_index);
			cb_color_pitch = S_028C64_TILE_MAX(pitch_tile_max);
			cb_color_slice = S_028C68_TILE_MAX(slice_tile_max);

			if (tex->fmask.size) {
				if (sctx->chip_class >= CIK)
					cb_color_pitch |= S_028C64_FMASK_TILE_MAX(tex->fmask.pitch_in_pixels / 8 - 1);
				cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(tex->fmask.tile_mode_index);
				cb_color_fmask_slice = S_028C88_TILE_MAX(tex->fmask.slice_tile_max);
			} else {
				/* This must be set for fast clear to work without FMASK. */
				if (sctx->chip_class >= CIK)
					cb_color_pitch |= S_028C64_FMASK_TILE_MAX(pitch_tile_max);
				cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(tile_mode_index);
				cb_color_fmask_slice = S_028C88_TILE_MAX(slice_tile_max);
			}

			radeon_set_context_reg_seq(cs, R_028C60_CB_COLOR0_BASE + i * 0x3C,
						   sctx->chip_class >= VI ? 14 : 13);
			radeon_emit(cs, cb_color_base);		/* CB_COLOR0_BASE */
			radeon_emit(cs, cb_color_pitch);	/* CB_COLOR0_PITCH */
			radeon_emit(cs, cb_color_slice);	/* CB_COLOR0_SLICE */
			radeon_emit(cs, cb->cb_color_view);	/* CB_COLOR0_VIEW */
			radeon_emit(cs, cb_color_info);		/* CB_COLOR0_INFO */
			radeon_emit(cs, cb_color_attrib);	/* CB_COLOR0_ATTRIB */
			radeon_emit(cs, cb->cb_dcc_control);	/* CB_COLOR0_DCC_CONTROL */
			radeon_emit(cs, tex->cmask.base_address_reg);	/* CB_COLOR0_CMASK */
			radeon_emit(cs, tex->cmask.slice_tile_max);	/* CB_COLOR0_CMASK_SLICE */
			radeon_emit(cs, cb_color_fmask);		/* CB_COLOR0_FMASK */
			radeon_emit(cs, cb_color_fmask_slice);		/* CB_COLOR0_FMASK_SLICE */
			radeon_emit(cs, tex->color_clear_value[0]);	/* CB_COLOR0_CLEAR_WORD0 */
			radeon_emit(cs, tex->color_clear_value[1]);	/* CB_COLOR0_CLEAR_WORD1 */

			if (sctx->chip_class >= VI) /* R_028C94_CB_COLOR0_DCC_BASE */
				radeon_emit(cs, cb_dcc_base);
		}
	}
	for (; i < 8 ; i++)
		if (sctx->framebuffer.dirty_cbufs & (1 << i))
			radeon_set_context_reg(cs, R_028C70_CB_COLOR0_INFO + i * 0x3C, 0);

	/* ZS buffer. */
	if (state->zsbuf && sctx->framebuffer.dirty_zsbuf) {
		struct r600_surface *zb = (struct r600_surface*)state->zsbuf;
		struct r600_texture *rtex = (struct r600_texture*)zb->base.texture;

		radeon_add_to_buffer_list(sctx, sctx->gfx_cs,
				      &rtex->resource, RADEON_USAGE_READWRITE,
				      zb->base.texture->nr_samples > 1 ?
					      RADEON_PRIO_DEPTH_BUFFER_MSAA :
					      RADEON_PRIO_DEPTH_BUFFER);

		if (sctx->chip_class >= GFX9) {
			radeon_set_context_reg_seq(cs, R_028014_DB_HTILE_DATA_BASE, 3);
			radeon_emit(cs, zb->db_htile_data_base);	/* DB_HTILE_DATA_BASE */
			radeon_emit(cs, S_028018_BASE_HI(zb->db_htile_data_base >> 32)); /* DB_HTILE_DATA_BASE_HI */
			radeon_emit(cs, zb->db_depth_size);		/* DB_DEPTH_SIZE */

			radeon_set_context_reg_seq(cs, R_028038_DB_Z_INFO, 10);
			radeon_emit(cs, zb->db_z_info |			/* DB_Z_INFO */
				    S_028038_ZRANGE_PRECISION(rtex->depth_clear_value != 0));
			radeon_emit(cs, zb->db_stencil_info);		/* DB_STENCIL_INFO */
			radeon_emit(cs, zb->db_depth_base);		/* DB_Z_READ_BASE */
			radeon_emit(cs, S_028044_BASE_HI(zb->db_depth_base >> 32)); /* DB_Z_READ_BASE_HI */
			radeon_emit(cs, zb->db_stencil_base);		/* DB_STENCIL_READ_BASE */
			radeon_emit(cs, S_02804C_BASE_HI(zb->db_stencil_base >> 32)); /* DB_STENCIL_READ_BASE_HI */
			radeon_emit(cs, zb->db_depth_base);		/* DB_Z_WRITE_BASE */
			radeon_emit(cs, S_028054_BASE_HI(zb->db_depth_base >> 32)); /* DB_Z_WRITE_BASE_HI */
			radeon_emit(cs, zb->db_stencil_base);		/* DB_STENCIL_WRITE_BASE */
			radeon_emit(cs, S_02805C_BASE_HI(zb->db_stencil_base >> 32)); /* DB_STENCIL_WRITE_BASE_HI */

			radeon_set_context_reg_seq(cs, R_028068_DB_Z_INFO2, 2);
			radeon_emit(cs, zb->db_z_info2);	/* DB_Z_INFO2 */
			radeon_emit(cs, zb->db_stencil_info2);	/* DB_STENCIL_INFO2 */
		} else {
			radeon_set_context_reg(cs, R_028014_DB_HTILE_DATA_BASE, zb->db_htile_data_base);

			radeon_set_context_reg_seq(cs, R_02803C_DB_DEPTH_INFO, 9);
			radeon_emit(cs, zb->db_depth_info);	/* DB_DEPTH_INFO */
			radeon_emit(cs, zb->db_z_info |		/* DB_Z_INFO */
				    S_028040_ZRANGE_PRECISION(rtex->depth_clear_value != 0));
			radeon_emit(cs, zb->db_stencil_info);	/* DB_STENCIL_INFO */
			radeon_emit(cs, zb->db_depth_base);	/* DB_Z_READ_BASE */
			radeon_emit(cs, zb->db_stencil_base);	/* DB_STENCIL_READ_BASE */
			radeon_emit(cs, zb->db_depth_base);	/* DB_Z_WRITE_BASE */
			radeon_emit(cs, zb->db_stencil_base);	/* DB_STENCIL_WRITE_BASE */
			radeon_emit(cs, zb->db_depth_size);	/* DB_DEPTH_SIZE */
			radeon_emit(cs, zb->db_depth_slice);	/* DB_DEPTH_SLICE */
		}

		radeon_set_context_reg_seq(cs, R_028028_DB_STENCIL_CLEAR, 2);
		radeon_emit(cs, rtex->stencil_clear_value); /* R_028028_DB_STENCIL_CLEAR */
		radeon_emit(cs, fui(rtex->depth_clear_value)); /* R_02802C_DB_DEPTH_CLEAR */

		radeon_set_context_reg(cs, R_028008_DB_DEPTH_VIEW, zb->db_depth_view);
		radeon_set_context_reg(cs, R_028ABC_DB_HTILE_SURFACE, zb->db_htile_surface);
	} else if (sctx->framebuffer.dirty_zsbuf) {
		if (sctx->chip_class >= GFX9)
			radeon_set_context_reg_seq(cs, R_028038_DB_Z_INFO, 2);
		else
			radeon_set_context_reg_seq(cs, R_028040_DB_Z_INFO, 2);

		radeon_emit(cs, S_028040_FORMAT(V_028040_Z_INVALID)); /* DB_Z_INFO */
		radeon_emit(cs, S_028044_FORMAT(V_028044_STENCIL_INVALID)); /* DB_STENCIL_INFO */
	}

	/* Framebuffer dimensions. */
        /* PA_SC_WINDOW_SCISSOR_TL is set in si_init_config() */
	radeon_set_context_reg(cs, R_028208_PA_SC_WINDOW_SCISSOR_BR,
			       S_028208_BR_X(state->width) | S_028208_BR_Y(state->height));

	if (sctx->screen->dfsm_allowed) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_BREAK_BATCH) | EVENT_INDEX(0));
	}

	sctx->framebuffer.dirty_cbufs = 0;
	sctx->framebuffer.dirty_zsbuf = false;
}

static void si_emit_msaa_sample_locs(struct si_context *sctx,
				     struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = sctx->gfx_cs;
	unsigned nr_samples = sctx->framebuffer.nr_samples;
	bool has_msaa_sample_loc_bug = sctx->screen->has_msaa_sample_loc_bug;

	/* Smoothing (only possible with nr_samples == 1) uses the same
	 * sample locations as the MSAA it simulates.
	 */
	if (nr_samples <= 1 && sctx->smoothing_enabled)
		nr_samples = SI_NUM_SMOOTH_AA_SAMPLES;

	/* On Polaris, the small primitive filter uses the sample locations
	 * even when MSAA is off, so we need to make sure they're set to 0.
	 */
	if (has_msaa_sample_loc_bug)
		nr_samples = MAX2(nr_samples, 1);

	if (nr_samples != sctx->msaa_sample_locs.nr_samples) {
		sctx->msaa_sample_locs.nr_samples = nr_samples;
		si_emit_sample_locations(cs, nr_samples);
	}

	if (sctx->family >= CHIP_POLARIS10) {
		struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
		unsigned small_prim_filter_cntl =
			S_028830_SMALL_PRIM_FILTER_ENABLE(1) |
			/* line bug */
			S_028830_LINE_FILTER_DISABLE(sctx->family <= CHIP_POLARIS12);

		/* The alternative of setting sample locations to 0 would
		 * require a DB flush to avoid Z errors, see
		 * https://bugs.freedesktop.org/show_bug.cgi?id=96908
		 */
		if (has_msaa_sample_loc_bug &&
		    sctx->framebuffer.nr_samples > 1 &&
		    rs && !rs->multisample_enable)
			small_prim_filter_cntl &= C_028830_SMALL_PRIM_FILTER_ENABLE;

		radeon_set_context_reg(cs, R_028830_PA_SU_SMALL_PRIM_FILTER_CNTL,
				       small_prim_filter_cntl);
	}
}

static bool si_out_of_order_rasterization(struct si_context *sctx)
{
	struct si_state_blend *blend = sctx->queued.named.blend;
	struct si_state_dsa *dsa = sctx->queued.named.dsa;

	if (!sctx->screen->has_out_of_order_rast)
		return false;

	unsigned colormask = sctx->framebuffer.colorbuf_enabled_4bit;

	if (blend) {
		colormask &= blend->cb_target_enabled_4bit;
	} else {
		colormask = 0;
	}

	/* Conservative: No logic op. */
	if (colormask && blend->logicop_enable)
		return false;

	struct si_dsa_order_invariance dsa_order_invariant = {
		.zs = true, .pass_set = true, .pass_last = false
	};

	if (sctx->framebuffer.state.zsbuf) {
		struct r600_texture *zstex =
			(struct r600_texture*)sctx->framebuffer.state.zsbuf->texture;
		bool has_stencil = zstex->surface.has_stencil;
		dsa_order_invariant = dsa->order_invariance[has_stencil];
		if (!dsa_order_invariant.zs)
			return false;

		/* The set of PS invocations is always order invariant,
		 * except when early Z/S tests are requested. */
		if (sctx->ps_shader.cso &&
		    sctx->ps_shader.cso->info.writes_memory &&
		    sctx->ps_shader.cso->info.properties[TGSI_PROPERTY_FS_EARLY_DEPTH_STENCIL] &&
		    !dsa_order_invariant.pass_set)
			return false;

		if (sctx->num_perfect_occlusion_queries != 0 &&
		    !dsa_order_invariant.pass_set)
			return false;
	}

	if (!colormask)
		return true;

	unsigned blendmask = colormask & blend->blend_enable_4bit;

	if (blendmask) {
		/* Only commutative blending. */
		if (blendmask & ~blend->commutative_4bit)
			return false;

		if (!dsa_order_invariant.pass_set)
			return false;
	}

	if (colormask & ~blendmask) {
		if (!dsa_order_invariant.pass_last)
			return false;
	}

	return true;
}

static void si_emit_msaa_config(struct si_context *sctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = sctx->gfx_cs;
	unsigned num_tile_pipes = sctx->screen->info.num_tile_pipes;
	/* 33% faster rendering to linear color buffers */
	bool dst_is_linear = sctx->framebuffer.any_dst_linear;
	bool out_of_order_rast = si_out_of_order_rasterization(sctx);
	unsigned sc_mode_cntl_1 =
		S_028A4C_WALK_SIZE(dst_is_linear) |
		S_028A4C_WALK_FENCE_ENABLE(!dst_is_linear) |
		S_028A4C_WALK_FENCE_SIZE(num_tile_pipes == 2 ? 2 : 3) |
		S_028A4C_OUT_OF_ORDER_PRIMITIVE_ENABLE(out_of_order_rast) |
		S_028A4C_OUT_OF_ORDER_WATER_MARK(0x7) |
		/* always 1: */
		S_028A4C_WALK_ALIGN8_PRIM_FITS_ST(1) |
		S_028A4C_SUPERTILE_WALK_ORDER_ENABLE(1) |
		S_028A4C_TILE_WALK_ORDER_ENABLE(1) |
		S_028A4C_MULTI_SHADER_ENGINE_PRIM_DISCARD_ENABLE(1) |
		S_028A4C_FORCE_EOV_CNTDWN_ENABLE(1) |
		S_028A4C_FORCE_EOV_REZ_ENABLE(1);

	int setup_samples = sctx->framebuffer.nr_samples > 1 ? sctx->framebuffer.nr_samples :
			    sctx->smoothing_enabled ? SI_NUM_SMOOTH_AA_SAMPLES : 0;

	/* Required by OpenGL line rasterization.
	 *
	 * TODO: We should also enable perpendicular endcaps for AA lines,
	 *       but that requires implementing line stippling in the pixel
	 *       shader. SC can only do line stippling with axis-aligned
	 *       endcaps.
	 */
	unsigned sc_line_cntl = S_028BDC_DX10_DIAMOND_TEST_ENA(1);

	if (setup_samples > 1) {
		/* distance from the pixel center, indexed by log2(nr_samples) */
		static unsigned max_dist[] = {
			0, /* unused */
			4, /* 2x MSAA */
			6, /* 4x MSAA */
			7, /* 8x MSAA */
			8, /* 16x MSAA */
		};
		unsigned log_samples = util_logbase2(setup_samples);
		unsigned ps_iter_samples = si_get_ps_iter_samples(sctx);
		unsigned log_ps_iter_samples =
			util_logbase2(util_next_power_of_two(ps_iter_samples));

		radeon_set_context_reg_seq(cs, R_028BDC_PA_SC_LINE_CNTL, 2);
		radeon_emit(cs, sc_line_cntl |
			    S_028BDC_EXPAND_LINE_WIDTH(1)); /* CM_R_028BDC_PA_SC_LINE_CNTL */
		radeon_emit(cs, S_028BE0_MSAA_NUM_SAMPLES(log_samples) |
			    S_028BE0_MAX_SAMPLE_DIST(max_dist[log_samples]) |
			    S_028BE0_MSAA_EXPOSED_SAMPLES(log_samples)); /* CM_R_028BE0_PA_SC_AA_CONFIG */

		if (sctx->framebuffer.nr_samples > 1) {
			radeon_set_context_reg(cs, R_028804_DB_EQAA,
					       S_028804_MAX_ANCHOR_SAMPLES(log_samples) |
					       S_028804_PS_ITER_SAMPLES(log_ps_iter_samples) |
					       S_028804_MASK_EXPORT_NUM_SAMPLES(log_samples) |
					       S_028804_ALPHA_TO_MASK_NUM_SAMPLES(log_samples) |
					       S_028804_HIGH_QUALITY_INTERSECTIONS(1) |
					       S_028804_STATIC_ANCHOR_ASSOCIATIONS(1));
			radeon_set_context_reg(cs, R_028A4C_PA_SC_MODE_CNTL_1,
					       S_028A4C_PS_ITER_SAMPLE(ps_iter_samples > 1) |
					       sc_mode_cntl_1);
		} else if (sctx->smoothing_enabled) {
			radeon_set_context_reg(cs, R_028804_DB_EQAA,
					       S_028804_HIGH_QUALITY_INTERSECTIONS(1) |
					       S_028804_STATIC_ANCHOR_ASSOCIATIONS(1) |
					       S_028804_OVERRASTERIZATION_AMOUNT(log_samples));
			radeon_set_context_reg(cs, R_028A4C_PA_SC_MODE_CNTL_1,
					       sc_mode_cntl_1);
		}
	} else {
		radeon_set_context_reg_seq(cs, R_028BDC_PA_SC_LINE_CNTL, 2);
		radeon_emit(cs, sc_line_cntl); /* CM_R_028BDC_PA_SC_LINE_CNTL */
		radeon_emit(cs, 0); /* CM_R_028BE0_PA_SC_AA_CONFIG */

		radeon_set_context_reg(cs, R_028804_DB_EQAA,
				       S_028804_HIGH_QUALITY_INTERSECTIONS(1) |
				       S_028804_STATIC_ANCHOR_ASSOCIATIONS(1));
		radeon_set_context_reg(cs, R_028A4C_PA_SC_MODE_CNTL_1,
				       sc_mode_cntl_1);
	}

	/* GFX9: Flush DFSM when the AA mode changes. */
	if (sctx->screen->dfsm_allowed) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_FLUSH_DFSM) | EVENT_INDEX(0));
	}
}

void si_update_ps_iter_samples(struct si_context *sctx)
{
	if (sctx->framebuffer.nr_samples > 1)
		si_mark_atom_dirty(sctx, &sctx->msaa_config);
	if (sctx->screen->dpbb_allowed)
		si_mark_atom_dirty(sctx, &sctx->dpbb_state);
}

static void si_set_min_samples(struct pipe_context *ctx, unsigned min_samples)
{
	struct si_context *sctx = (struct si_context *)ctx;

	if (sctx->ps_iter_samples == min_samples)
		return;

	sctx->ps_iter_samples = min_samples;
	sctx->do_update_shaders = true;

	si_update_ps_iter_samples(sctx);
}

/*
 * Samplers
 */

/**
 * Build the sampler view descriptor for a buffer texture.
 * @param state 256-bit descriptor; only the high 128 bits are filled in
 */
void
si_make_buffer_descriptor(struct si_screen *screen, struct r600_resource *buf,
			  enum pipe_format format,
			  unsigned offset, unsigned size,
			  uint32_t *state)
{
	const struct util_format_description *desc;
	int first_non_void;
	unsigned stride;
	unsigned num_records;
	unsigned num_format, data_format;

	desc = util_format_description(format);
	first_non_void = util_format_get_first_non_void_channel(format);
	stride = desc->block.bits / 8;
	num_format = si_translate_buffer_numformat(&screen->b, desc, first_non_void);
	data_format = si_translate_buffer_dataformat(&screen->b, desc, first_non_void);

	num_records = size / stride;
	num_records = MIN2(num_records, (buf->b.b.width0 - offset) / stride);

	/* The NUM_RECORDS field has a different meaning depending on the chip,
	 * instruction type, STRIDE, and SWIZZLE_ENABLE.
	 *
	 * SI-CIK:
	 * - If STRIDE == 0, it's in byte units.
	 * - If STRIDE != 0, it's in units of STRIDE, used with inst.IDXEN.
	 *
	 * VI:
	 * - For SMEM and STRIDE == 0, it's in byte units.
	 * - For SMEM and STRIDE != 0, it's in units of STRIDE.
	 * - For VMEM and STRIDE == 0 or SWIZZLE_ENABLE == 0, it's in byte units.
	 * - For VMEM and STRIDE != 0 and SWIZZLE_ENABLE == 1, it's in units of STRIDE.
	 * NOTE: There is incompatibility between VMEM and SMEM opcodes due to SWIZZLE_-
	 *       ENABLE. The workaround is to set STRIDE = 0 if SWIZZLE_ENABLE == 0 when
	 *       using SMEM. This can be done in the shader by clearing STRIDE with s_and.
	 *       That way the same descriptor can be used by both SMEM and VMEM.
	 *
	 * GFX9:
	 * - For SMEM and STRIDE == 0, it's in byte units.
	 * - For SMEM and STRIDE != 0, it's in units of STRIDE.
	 * - For VMEM and inst.IDXEN == 0 or STRIDE == 0, it's in byte units.
	 * - For VMEM and inst.IDXEN == 1 and STRIDE != 0, it's in units of STRIDE.
	 */
	if (screen->info.chip_class >= GFX9)
		/* When vindex == 0, LLVM sets IDXEN = 0, thus changing units
		 * from STRIDE to bytes. This works around it by setting
		 * NUM_RECORDS to at least the size of one element, so that
		 * the first element is readable when IDXEN == 0.
		 *
		 * TODO: Fix this in LLVM, but do we need a new intrinsic where
		 *       IDXEN is enforced?
		 */
		num_records = num_records ? MAX2(num_records, stride) : 0;
	else if (screen->info.chip_class == VI)
		num_records *= stride;

	state[4] = 0;
	state[5] = S_008F04_STRIDE(stride);
	state[6] = num_records;
	state[7] = S_008F0C_DST_SEL_X(si_map_swizzle(desc->swizzle[0])) |
		   S_008F0C_DST_SEL_Y(si_map_swizzle(desc->swizzle[1])) |
		   S_008F0C_DST_SEL_Z(si_map_swizzle(desc->swizzle[2])) |
		   S_008F0C_DST_SEL_W(si_map_swizzle(desc->swizzle[3])) |
		   S_008F0C_NUM_FORMAT(num_format) |
		   S_008F0C_DATA_FORMAT(data_format);
}

static unsigned gfx9_border_color_swizzle(const unsigned char swizzle[4])
{
	unsigned bc_swizzle = V_008F20_BC_SWIZZLE_XYZW;

	if (swizzle[3] == PIPE_SWIZZLE_X) {
		/* For the pre-defined border color values (white, opaque
		 * black, transparent black), the only thing that matters is
		 * that the alpha channel winds up in the correct place
		 * (because the RGB channels are all the same) so either of
		 * these enumerations will work.
		 */
		if (swizzle[2] == PIPE_SWIZZLE_Y)
			bc_swizzle = V_008F20_BC_SWIZZLE_WZYX;
		else
			bc_swizzle = V_008F20_BC_SWIZZLE_WXYZ;
	} else if (swizzle[0] == PIPE_SWIZZLE_X) {
		if (swizzle[1] == PIPE_SWIZZLE_Y)
			bc_swizzle = V_008F20_BC_SWIZZLE_XYZW;
		else
			bc_swizzle = V_008F20_BC_SWIZZLE_XWYZ;
	} else if (swizzle[1] == PIPE_SWIZZLE_X) {
		bc_swizzle = V_008F20_BC_SWIZZLE_YXWZ;
	} else if (swizzle[2] == PIPE_SWIZZLE_X) {
		bc_swizzle = V_008F20_BC_SWIZZLE_ZYXW;
	}

	return bc_swizzle;
}

/**
 * Build the sampler view descriptor for a texture.
 */
void
si_make_texture_descriptor(struct si_screen *screen,
			   struct r600_texture *tex,
			   bool sampler,
			   enum pipe_texture_target target,
			   enum pipe_format pipe_format,
			   const unsigned char state_swizzle[4],
			   unsigned first_level, unsigned last_level,
			   unsigned first_layer, unsigned last_layer,
			   unsigned width, unsigned height, unsigned depth,
			   uint32_t *state,
			   uint32_t *fmask_state)
{
	struct pipe_resource *res = &tex->resource.b.b;
	const struct util_format_description *desc;
	unsigned char swizzle[4];
	int first_non_void;
	unsigned num_format, data_format, type;
	uint64_t va;

	desc = util_format_description(pipe_format);

	if (desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS) {
		const unsigned char swizzle_xxxx[4] = {0, 0, 0, 0};
		const unsigned char swizzle_yyyy[4] = {1, 1, 1, 1};
		const unsigned char swizzle_wwww[4] = {3, 3, 3, 3};

		switch (pipe_format) {
		case PIPE_FORMAT_S8_UINT_Z24_UNORM:
		case PIPE_FORMAT_X32_S8X24_UINT:
		case PIPE_FORMAT_X8Z24_UNORM:
			util_format_compose_swizzles(swizzle_yyyy, state_swizzle, swizzle);
			break;
		case PIPE_FORMAT_X24S8_UINT:
			/*
			 * X24S8 is implemented as an 8_8_8_8 data format, to
			 * fix texture gathers. This affects at least
			 * GL45-CTS.texture_cube_map_array.sampling on VI.
			 */
			if (screen->info.chip_class <= VI)
				util_format_compose_swizzles(swizzle_wwww, state_swizzle, swizzle);
			else
				util_format_compose_swizzles(swizzle_yyyy, state_swizzle, swizzle);
			break;
		default:
			util_format_compose_swizzles(swizzle_xxxx, state_swizzle, swizzle);
		}
	} else {
		util_format_compose_swizzles(desc->swizzle, state_swizzle, swizzle);
	}

	first_non_void = util_format_get_first_non_void_channel(pipe_format);

	switch (pipe_format) {
	case PIPE_FORMAT_S8_UINT_Z24_UNORM:
		num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
		break;
	default:
		if (first_non_void < 0) {
			if (util_format_is_compressed(pipe_format)) {
				switch (pipe_format) {
				case PIPE_FORMAT_DXT1_SRGB:
				case PIPE_FORMAT_DXT1_SRGBA:
				case PIPE_FORMAT_DXT3_SRGBA:
				case PIPE_FORMAT_DXT5_SRGBA:
				case PIPE_FORMAT_BPTC_SRGBA:
				case PIPE_FORMAT_ETC2_SRGB8:
				case PIPE_FORMAT_ETC2_SRGB8A1:
				case PIPE_FORMAT_ETC2_SRGBA8:
					num_format = V_008F14_IMG_NUM_FORMAT_SRGB;
					break;
				case PIPE_FORMAT_RGTC1_SNORM:
				case PIPE_FORMAT_LATC1_SNORM:
				case PIPE_FORMAT_RGTC2_SNORM:
				case PIPE_FORMAT_LATC2_SNORM:
				case PIPE_FORMAT_ETC2_R11_SNORM:
				case PIPE_FORMAT_ETC2_RG11_SNORM:
				/* implies float, so use SNORM/UNORM to determine
				   whether data is signed or not */
				case PIPE_FORMAT_BPTC_RGB_FLOAT:
					num_format = V_008F14_IMG_NUM_FORMAT_SNORM;
					break;
				default:
					num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
					break;
				}
			} else if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
				num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
			} else {
				num_format = V_008F14_IMG_NUM_FORMAT_FLOAT;
			}
		} else if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
			num_format = V_008F14_IMG_NUM_FORMAT_SRGB;
		} else {
			num_format = V_008F14_IMG_NUM_FORMAT_UNORM;

			switch (desc->channel[first_non_void].type) {
			case UTIL_FORMAT_TYPE_FLOAT:
				num_format = V_008F14_IMG_NUM_FORMAT_FLOAT;
				break;
			case UTIL_FORMAT_TYPE_SIGNED:
				if (desc->channel[first_non_void].normalized)
					num_format = V_008F14_IMG_NUM_FORMAT_SNORM;
				else if (desc->channel[first_non_void].pure_integer)
					num_format = V_008F14_IMG_NUM_FORMAT_SINT;
				else
					num_format = V_008F14_IMG_NUM_FORMAT_SSCALED;
				break;
			case UTIL_FORMAT_TYPE_UNSIGNED:
				if (desc->channel[first_non_void].normalized)
					num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
				else if (desc->channel[first_non_void].pure_integer)
					num_format = V_008F14_IMG_NUM_FORMAT_UINT;
				else
					num_format = V_008F14_IMG_NUM_FORMAT_USCALED;
			}
		}
	}

	data_format = si_translate_texformat(&screen->b, pipe_format, desc, first_non_void);
	if (data_format == ~0) {
		data_format = 0;
	}

	/* S8 with Z32 HTILE needs a special format. */
	if (screen->info.chip_class >= GFX9 &&
	    pipe_format == PIPE_FORMAT_S8_UINT &&
	    tex->tc_compatible_htile)
		data_format = V_008F14_IMG_DATA_FORMAT_S8_32;

	if (!sampler &&
	    (res->target == PIPE_TEXTURE_CUBE ||
	     res->target == PIPE_TEXTURE_CUBE_ARRAY ||
	     (screen->info.chip_class <= VI &&
	      res->target == PIPE_TEXTURE_3D))) {
		/* For the purpose of shader images, treat cube maps and 3D
		 * textures as 2D arrays. For 3D textures, the address
		 * calculations for mipmaps are different, so we rely on the
		 * caller to effectively disable mipmaps.
		 */
		type = V_008F1C_SQ_RSRC_IMG_2D_ARRAY;

		assert(res->target != PIPE_TEXTURE_3D || (first_level == 0 && last_level == 0));
	} else {
		type = si_tex_dim(screen, tex, target, res->nr_samples);
	}

	if (type == V_008F1C_SQ_RSRC_IMG_1D_ARRAY) {
	        height = 1;
		depth = res->array_size;
	} else if (type == V_008F1C_SQ_RSRC_IMG_2D_ARRAY ||
		   type == V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY) {
		if (sampler || res->target != PIPE_TEXTURE_3D)
			depth = res->array_size;
	} else if (type == V_008F1C_SQ_RSRC_IMG_CUBE)
		depth = res->array_size / 6;

	state[0] = 0;
	state[1] = (S_008F14_DATA_FORMAT_GFX6(data_format) |
		    S_008F14_NUM_FORMAT_GFX6(num_format));
	state[2] = (S_008F18_WIDTH(width - 1) |
		    S_008F18_HEIGHT(height - 1) |
		    S_008F18_PERF_MOD(4));
	state[3] = (S_008F1C_DST_SEL_X(si_map_swizzle(swizzle[0])) |
		    S_008F1C_DST_SEL_Y(si_map_swizzle(swizzle[1])) |
		    S_008F1C_DST_SEL_Z(si_map_swizzle(swizzle[2])) |
		    S_008F1C_DST_SEL_W(si_map_swizzle(swizzle[3])) |
		    S_008F1C_BASE_LEVEL(res->nr_samples > 1 ?
					0 : first_level) |
		    S_008F1C_LAST_LEVEL(res->nr_samples > 1 ?
					util_logbase2(res->nr_samples) :
					last_level) |
		    S_008F1C_TYPE(type));
	state[4] = 0;
	state[5] = S_008F24_BASE_ARRAY(first_layer);
	state[6] = 0;
	state[7] = 0;

	if (screen->info.chip_class >= GFX9) {
		unsigned bc_swizzle = gfx9_border_color_swizzle(desc->swizzle);

		/* Depth is the the last accessible layer on Gfx9.
		 * The hw doesn't need to know the total number of layers.
		 */
		if (type == V_008F1C_SQ_RSRC_IMG_3D)
			state[4] |= S_008F20_DEPTH(depth - 1);
		else
			state[4] |= S_008F20_DEPTH(last_layer);

		state[4] |= S_008F20_BC_SWIZZLE(bc_swizzle);
		state[5] |= S_008F24_MAX_MIP(res->nr_samples > 1 ?
					     util_logbase2(res->nr_samples) :
					     tex->resource.b.b.last_level);
	} else {
		state[3] |= S_008F1C_POW2_PAD(res->last_level > 0);
		state[4] |= S_008F20_DEPTH(depth - 1);
		state[5] |= S_008F24_LAST_ARRAY(last_layer);
	}

	if (tex->dcc_offset) {
		unsigned swap = si_translate_colorswap(pipe_format, false);

		state[6] = S_008F28_ALPHA_IS_ON_MSB(swap <= 1);
	} else {
		/* The last dword is unused by hw. The shader uses it to clear
		 * bits in the first dword of sampler state.
		 */
		if (screen->info.chip_class <= CIK && res->nr_samples <= 1) {
			if (first_level == last_level)
				state[7] = C_008F30_MAX_ANISO_RATIO;
			else
				state[7] = 0xffffffff;
		}
	}

	/* Initialize the sampler view for FMASK. */
	if (tex->fmask.size) {
		uint32_t data_format, num_format;

		va = tex->resource.gpu_address + tex->fmask.offset;

		if (screen->info.chip_class >= GFX9) {
			data_format = V_008F14_IMG_DATA_FORMAT_FMASK;
			switch (res->nr_samples) {
			case 2:
				num_format = V_008F14_IMG_FMASK_8_2_2;
				break;
			case 4:
				num_format = V_008F14_IMG_FMASK_8_4_4;
				break;
			case 8:
				num_format = V_008F14_IMG_FMASK_32_8_8;
				break;
			default:
				unreachable("invalid nr_samples");
			}
		} else {
			switch (res->nr_samples) {
			case 2:
				data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S2_F2;
				break;
			case 4:
				data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S4_F4;
				break;
			case 8:
				data_format = V_008F14_IMG_DATA_FORMAT_FMASK32_S8_F8;
				break;
			default:
				unreachable("invalid nr_samples");
			}
			num_format = V_008F14_IMG_NUM_FORMAT_UINT;
		}

		fmask_state[0] = (va >> 8) | tex->fmask.tile_swizzle;
		fmask_state[1] = S_008F14_BASE_ADDRESS_HI(va >> 40) |
				 S_008F14_DATA_FORMAT_GFX6(data_format) |
				 S_008F14_NUM_FORMAT_GFX6(num_format);
		fmask_state[2] = S_008F18_WIDTH(width - 1) |
				 S_008F18_HEIGHT(height - 1);
		fmask_state[3] = S_008F1C_DST_SEL_X(V_008F1C_SQ_SEL_X) |
				 S_008F1C_DST_SEL_Y(V_008F1C_SQ_SEL_X) |
				 S_008F1C_DST_SEL_Z(V_008F1C_SQ_SEL_X) |
				 S_008F1C_DST_SEL_W(V_008F1C_SQ_SEL_X) |
				 S_008F1C_TYPE(si_tex_dim(screen, tex, target, 0));
		fmask_state[4] = 0;
		fmask_state[5] = S_008F24_BASE_ARRAY(first_layer);
		fmask_state[6] = 0;
		fmask_state[7] = 0;

		if (screen->info.chip_class >= GFX9) {
			fmask_state[3] |= S_008F1C_SW_MODE(tex->surface.u.gfx9.fmask.swizzle_mode);
			fmask_state[4] |= S_008F20_DEPTH(last_layer) |
					  S_008F20_PITCH_GFX9(tex->surface.u.gfx9.fmask.epitch);
			fmask_state[5] |= S_008F24_META_PIPE_ALIGNED(tex->surface.u.gfx9.cmask.pipe_aligned) |
					  S_008F24_META_RB_ALIGNED(tex->surface.u.gfx9.cmask.rb_aligned);
		} else {
			fmask_state[3] |= S_008F1C_TILING_INDEX(tex->fmask.tile_mode_index);
			fmask_state[4] |= S_008F20_DEPTH(depth - 1) |
					  S_008F20_PITCH_GFX6(tex->fmask.pitch_in_pixels - 1);
			fmask_state[5] |= S_008F24_LAST_ARRAY(last_layer);
		}
	}
}

/**
 * Create a sampler view.
 *
 * @param ctx		context
 * @param texture	texture
 * @param state		sampler view template
 * @param width0	width0 override (for compressed textures as int)
 * @param height0	height0 override (for compressed textures as int)
 * @param force_level   set the base address to the level (for compressed textures)
 */
struct pipe_sampler_view *
si_create_sampler_view_custom(struct pipe_context *ctx,
			      struct pipe_resource *texture,
			      const struct pipe_sampler_view *state,
			      unsigned width0, unsigned height0,
			      unsigned force_level)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct si_sampler_view *view = CALLOC_STRUCT(si_sampler_view);
	struct r600_texture *tmp = (struct r600_texture*)texture;
	unsigned base_level, first_level, last_level;
	unsigned char state_swizzle[4];
	unsigned height, depth, width;
	unsigned last_layer = state->u.tex.last_layer;
	enum pipe_format pipe_format;
	const struct legacy_surf_level *surflevel;

	if (!view)
		return NULL;

	/* initialize base object */
	view->base = *state;
	view->base.texture = NULL;
	view->base.reference.count = 1;
	view->base.context = ctx;

	assert(texture);
	pipe_resource_reference(&view->base.texture, texture);

	if (state->format == PIPE_FORMAT_X24S8_UINT ||
	    state->format == PIPE_FORMAT_S8X24_UINT ||
	    state->format == PIPE_FORMAT_X32_S8X24_UINT ||
	    state->format == PIPE_FORMAT_S8_UINT)
		view->is_stencil_sampler = true;

	/* Buffer resource. */
	if (texture->target == PIPE_BUFFER) {
		si_make_buffer_descriptor(sctx->screen,
					  (struct r600_resource *)texture,
					  state->format,
					  state->u.buf.offset,
					  state->u.buf.size,
					  view->state);
		return &view->base;
	}

	state_swizzle[0] = state->swizzle_r;
	state_swizzle[1] = state->swizzle_g;
	state_swizzle[2] = state->swizzle_b;
	state_swizzle[3] = state->swizzle_a;

	base_level = 0;
	first_level = state->u.tex.first_level;
	last_level = state->u.tex.last_level;
	width = width0;
	height = height0;
	depth = texture->depth0;

	if (sctx->chip_class <= VI && force_level) {
		assert(force_level == first_level &&
		       force_level == last_level);
		base_level = force_level;
		first_level = 0;
		last_level = 0;
		width = u_minify(width, force_level);
		height = u_minify(height, force_level);
		depth = u_minify(depth, force_level);
	}

	/* This is not needed if state trackers set last_layer correctly. */
	if (state->target == PIPE_TEXTURE_1D ||
	    state->target == PIPE_TEXTURE_2D ||
	    state->target == PIPE_TEXTURE_RECT ||
	    state->target == PIPE_TEXTURE_CUBE)
		last_layer = state->u.tex.first_layer;

	/* Texturing with separate depth and stencil. */
	pipe_format = state->format;

	/* Depth/stencil texturing sometimes needs separate texture. */
	if (tmp->is_depth && !si_can_sample_zs(tmp, view->is_stencil_sampler)) {
		if (!tmp->flushed_depth_texture &&
		    !si_init_flushed_depth_texture(ctx, texture, NULL)) {
			pipe_resource_reference(&view->base.texture, NULL);
			FREE(view);
			return NULL;
		}

		assert(tmp->flushed_depth_texture);

		/* Override format for the case where the flushed texture
		 * contains only Z or only S.
		 */
		if (tmp->flushed_depth_texture->resource.b.b.format != tmp->resource.b.b.format)
			pipe_format = tmp->flushed_depth_texture->resource.b.b.format;

		tmp = tmp->flushed_depth_texture;
	}

	surflevel = tmp->surface.u.legacy.level;

	if (tmp->db_compatible) {
		if (!view->is_stencil_sampler)
			pipe_format = tmp->db_render_format;

		switch (pipe_format) {
		case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
			pipe_format = PIPE_FORMAT_Z32_FLOAT;
			break;
		case PIPE_FORMAT_X8Z24_UNORM:
		case PIPE_FORMAT_S8_UINT_Z24_UNORM:
			/* Z24 is always stored like this for DB
			 * compatibility.
			 */
			pipe_format = PIPE_FORMAT_Z24X8_UNORM;
			break;
		case PIPE_FORMAT_X24S8_UINT:
		case PIPE_FORMAT_S8X24_UINT:
		case PIPE_FORMAT_X32_S8X24_UINT:
			pipe_format = PIPE_FORMAT_S8_UINT;
			surflevel = tmp->surface.u.legacy.stencil_level;
			break;
		default:;
		}
	}

	view->dcc_incompatible =
		vi_dcc_formats_are_incompatible(texture,
						state->u.tex.first_level,
						state->format);

	si_make_texture_descriptor(sctx->screen, tmp, true,
				   state->target, pipe_format, state_swizzle,
				   first_level, last_level,
				   state->u.tex.first_layer, last_layer,
				   width, height, depth,
				   view->state, view->fmask_state);

	unsigned num_format = G_008F14_NUM_FORMAT_GFX6(view->state[1]);
	view->is_integer =
		num_format == V_008F14_IMG_NUM_FORMAT_USCALED ||
		num_format == V_008F14_IMG_NUM_FORMAT_SSCALED ||
		num_format == V_008F14_IMG_NUM_FORMAT_UINT ||
		num_format == V_008F14_IMG_NUM_FORMAT_SINT;
	view->base_level_info = &surflevel[base_level];
	view->base_level = base_level;
	view->block_width = util_format_get_blockwidth(pipe_format);
	return &view->base;
}

static struct pipe_sampler_view *
si_create_sampler_view(struct pipe_context *ctx,
		       struct pipe_resource *texture,
		       const struct pipe_sampler_view *state)
{
	return si_create_sampler_view_custom(ctx, texture, state,
					     texture ? texture->width0 : 0,
					     texture ? texture->height0 : 0, 0);
}

static void si_sampler_view_destroy(struct pipe_context *ctx,
				    struct pipe_sampler_view *state)
{
	struct si_sampler_view *view = (struct si_sampler_view *)state;

	pipe_resource_reference(&state->texture, NULL);
	FREE(view);
}

static bool wrap_mode_uses_border_color(unsigned wrap, bool linear_filter)
{
	return wrap == PIPE_TEX_WRAP_CLAMP_TO_BORDER ||
	       wrap == PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER ||
	       (linear_filter &&
	        (wrap == PIPE_TEX_WRAP_CLAMP ||
		 wrap == PIPE_TEX_WRAP_MIRROR_CLAMP));
}

static uint32_t si_translate_border_color(struct si_context *sctx,
					  const struct pipe_sampler_state *state,
					  const union pipe_color_union *color,
					  bool is_integer)
{
	bool linear_filter = state->min_img_filter != PIPE_TEX_FILTER_NEAREST ||
			     state->mag_img_filter != PIPE_TEX_FILTER_NEAREST;

	if (!wrap_mode_uses_border_color(state->wrap_s, linear_filter) &&
	    !wrap_mode_uses_border_color(state->wrap_t, linear_filter) &&
	    !wrap_mode_uses_border_color(state->wrap_r, linear_filter))
		return S_008F3C_BORDER_COLOR_TYPE(V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK);

#define simple_border_types(elt) \
do { \
	if (color->elt[0] == 0 && color->elt[1] == 0 &&                         \
	    color->elt[2] == 0 && color->elt[3] == 0)                           \
		return S_008F3C_BORDER_COLOR_TYPE(V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK); \
	if (color->elt[0] == 0 && color->elt[1] == 0 &&                         \
	    color->elt[2] == 0 && color->elt[3] == 1)                           \
		return S_008F3C_BORDER_COLOR_TYPE(V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_BLACK); \
	if (color->elt[0] == 1 && color->elt[1] == 1 &&                         \
	    color->elt[2] == 1 && color->elt[3] == 1)                           \
		return S_008F3C_BORDER_COLOR_TYPE(V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_WHITE); \
} while (false)

	if (is_integer)
		simple_border_types(ui);
	else
		simple_border_types(f);

#undef simple_border_types

	int i;

	/* Check if the border has been uploaded already. */
	for (i = 0; i < sctx->border_color_count; i++)
		if (memcmp(&sctx->border_color_table[i], color,
			   sizeof(*color)) == 0)
			break;

	if (i >= SI_MAX_BORDER_COLORS) {
		/* Getting 4096 unique border colors is very unlikely. */
		fprintf(stderr, "radeonsi: The border color table is full. "
			"Any new border colors will be just black. "
			"Please file a bug.\n");
		return S_008F3C_BORDER_COLOR_TYPE(V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK);
	}

	if (i == sctx->border_color_count) {
		/* Upload a new border color. */
		memcpy(&sctx->border_color_table[i], color,
		       sizeof(*color));
		util_memcpy_cpu_to_le32(&sctx->border_color_map[i],
					color, sizeof(*color));
		sctx->border_color_count++;
	}

	return S_008F3C_BORDER_COLOR_PTR(i) |
	       S_008F3C_BORDER_COLOR_TYPE(V_008F3C_SQ_TEX_BORDER_COLOR_REGISTER);
}

static inline int S_FIXED(float value, unsigned frac_bits)
{
	return value * (1 << frac_bits);
}

static inline unsigned si_tex_filter(unsigned filter, unsigned max_aniso)
{
	if (filter == PIPE_TEX_FILTER_LINEAR)
		return max_aniso > 1 ? V_008F38_SQ_TEX_XY_FILTER_ANISO_BILINEAR
				     : V_008F38_SQ_TEX_XY_FILTER_BILINEAR;
	else
		return max_aniso > 1 ? V_008F38_SQ_TEX_XY_FILTER_ANISO_POINT
				     : V_008F38_SQ_TEX_XY_FILTER_POINT;
}

static inline unsigned si_tex_aniso_filter(unsigned filter)
{
	if (filter < 2)
		return 0;
	if (filter < 4)
		return 1;
	if (filter < 8)
		return 2;
	if (filter < 16)
		return 3;
	return 4;
}

static void *si_create_sampler_state(struct pipe_context *ctx,
				     const struct pipe_sampler_state *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_screen *sscreen = sctx->screen;
	struct si_sampler_state *rstate = CALLOC_STRUCT(si_sampler_state);
	unsigned max_aniso = sscreen->force_aniso >= 0 ? sscreen->force_aniso
						       : state->max_anisotropy;
	unsigned max_aniso_ratio = si_tex_aniso_filter(max_aniso);
	union pipe_color_union clamped_border_color;

	if (!rstate) {
		return NULL;
	}

#ifdef DEBUG
	rstate->magic = SI_SAMPLER_STATE_MAGIC;
#endif
	rstate->val[0] = (S_008F30_CLAMP_X(si_tex_wrap(state->wrap_s)) |
			  S_008F30_CLAMP_Y(si_tex_wrap(state->wrap_t)) |
			  S_008F30_CLAMP_Z(si_tex_wrap(state->wrap_r)) |
			  S_008F30_MAX_ANISO_RATIO(max_aniso_ratio) |
			  S_008F30_DEPTH_COMPARE_FUNC(si_tex_compare(state->compare_func)) |
			  S_008F30_FORCE_UNNORMALIZED(!state->normalized_coords) |
			  S_008F30_ANISO_THRESHOLD(max_aniso_ratio >> 1) |
			  S_008F30_ANISO_BIAS(max_aniso_ratio) |
			  S_008F30_DISABLE_CUBE_WRAP(!state->seamless_cube_map) |
			  S_008F30_COMPAT_MODE(sctx->chip_class >= VI));
	rstate->val[1] = (S_008F34_MIN_LOD(S_FIXED(CLAMP(state->min_lod, 0, 15), 8)) |
			  S_008F34_MAX_LOD(S_FIXED(CLAMP(state->max_lod, 0, 15), 8)) |
			  S_008F34_PERF_MIP(max_aniso_ratio ? max_aniso_ratio + 6 : 0));
	rstate->val[2] = (S_008F38_LOD_BIAS(S_FIXED(CLAMP(state->lod_bias, -16, 16), 8)) |
			  S_008F38_XY_MAG_FILTER(si_tex_filter(state->mag_img_filter, max_aniso)) |
			  S_008F38_XY_MIN_FILTER(si_tex_filter(state->min_img_filter, max_aniso)) |
			  S_008F38_MIP_FILTER(si_tex_mipfilter(state->min_mip_filter)) |
			  S_008F38_MIP_POINT_PRECLAMP(0) |
			  S_008F38_DISABLE_LSB_CEIL(sctx->chip_class <= VI) |
			  S_008F38_FILTER_PREC_FIX(1) |
			  S_008F38_ANISO_OVERRIDE(sctx->chip_class >= VI));
	rstate->val[3] = si_translate_border_color(sctx, state, &state->border_color, false);

	/* Create sampler resource for integer textures. */
	memcpy(rstate->integer_val, rstate->val, sizeof(rstate->val));
	rstate->integer_val[3] = si_translate_border_color(sctx, state, &state->border_color, true);

	/* Create sampler resource for upgraded depth textures. */
	memcpy(rstate->upgraded_depth_val, rstate->val, sizeof(rstate->val));

	for (unsigned i = 0; i < 4; ++i) {
		/* Use channel 0 on purpose, so that we can use OPAQUE_WHITE
		 * when the border color is 1.0. */
		clamped_border_color.f[i] = CLAMP(state->border_color.f[0], 0, 1);
	}

	if (memcmp(&state->border_color, &clamped_border_color, sizeof(clamped_border_color)) == 0)
		rstate->upgraded_depth_val[3] |= S_008F3C_UPGRADED_DEPTH(1);
	else
		rstate->upgraded_depth_val[3] =
			si_translate_border_color(sctx, state, &clamped_border_color, false) |
			S_008F3C_UPGRADED_DEPTH(1);

	return rstate;
}

static void si_set_sample_mask(struct pipe_context *ctx, unsigned sample_mask)
{
	struct si_context *sctx = (struct si_context *)ctx;

	if (sctx->sample_mask.sample_mask == (uint16_t)sample_mask)
		return;

	sctx->sample_mask.sample_mask = sample_mask;
	si_mark_atom_dirty(sctx, &sctx->sample_mask.atom);
}

static void si_emit_sample_mask(struct si_context *sctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = sctx->gfx_cs;
	unsigned mask = sctx->sample_mask.sample_mask;

	/* Needed for line and polygon smoothing as well as for the Polaris
	 * small primitive filter. We expect the state tracker to take care of
	 * this for us.
	 */
	assert(mask == 0xffff || sctx->framebuffer.nr_samples > 1 ||
	       (mask & 1 && sctx->blitter->running));

	radeon_set_context_reg_seq(cs, R_028C38_PA_SC_AA_MASK_X0Y0_X1Y0, 2);
	radeon_emit(cs, mask | (mask << 16));
	radeon_emit(cs, mask | (mask << 16));
}

static void si_delete_sampler_state(struct pipe_context *ctx, void *state)
{
#ifdef DEBUG
	struct si_sampler_state *s = state;

	assert(s->magic == SI_SAMPLER_STATE_MAGIC);
	s->magic = 0;
#endif
	free(state);
}

/*
 * Vertex elements & buffers
 */

static void *si_create_vertex_elements(struct pipe_context *ctx,
				       unsigned count,
				       const struct pipe_vertex_element *elements)
{
	struct si_screen *sscreen = (struct si_screen*)ctx->screen;
	struct si_vertex_elements *v = CALLOC_STRUCT(si_vertex_elements);
	bool used[SI_NUM_VERTEX_BUFFERS] = {};
	int i;

	assert(count <= SI_MAX_ATTRIBS);
	if (!v)
		return NULL;

	v->count = count;
	v->desc_list_byte_size = align(count * 16, SI_CPDMA_ALIGNMENT);

	for (i = 0; i < count; ++i) {
		const struct util_format_description *desc;
		const struct util_format_channel_description *channel;
		unsigned data_format, num_format;
		int first_non_void;
		unsigned vbo_index = elements[i].vertex_buffer_index;
		unsigned char swizzle[4];

		if (vbo_index >= SI_NUM_VERTEX_BUFFERS) {
			FREE(v);
			return NULL;
		}

		if (elements[i].instance_divisor) {
			v->uses_instance_divisors = true;
			v->instance_divisors[i] = elements[i].instance_divisor;

			if (v->instance_divisors[i] == 1)
				v->instance_divisor_is_one |= 1u << i;
			else
				v->instance_divisor_is_fetched |= 1u << i;
		}

		if (!used[vbo_index]) {
			v->first_vb_use_mask |= 1 << i;
			used[vbo_index] = true;
		}

		desc = util_format_description(elements[i].src_format);
		first_non_void = util_format_get_first_non_void_channel(elements[i].src_format);
		data_format = si_translate_buffer_dataformat(ctx->screen, desc, first_non_void);
		num_format = si_translate_buffer_numformat(ctx->screen, desc, first_non_void);
		channel = first_non_void >= 0 ? &desc->channel[first_non_void] : NULL;
		memcpy(swizzle, desc->swizzle, sizeof(swizzle));

		v->format_size[i] = desc->block.bits / 8;
		v->src_offset[i] = elements[i].src_offset;
		v->vertex_buffer_index[i] = vbo_index;

		/* The hardware always treats the 2-bit alpha channel as
		 * unsigned, so a shader workaround is needed. The affected
		 * chips are VI and older except Stoney (GFX8.1).
		 */
		if (data_format == V_008F0C_BUF_DATA_FORMAT_2_10_10_10 &&
		    sscreen->info.chip_class <= VI &&
		    sscreen->info.family != CHIP_STONEY) {
			if (num_format == V_008F0C_BUF_NUM_FORMAT_SNORM) {
				v->fix_fetch[i] = SI_FIX_FETCH_A2_SNORM;
			} else if (num_format == V_008F0C_BUF_NUM_FORMAT_SSCALED) {
				v->fix_fetch[i] = SI_FIX_FETCH_A2_SSCALED;
			} else if (num_format == V_008F0C_BUF_NUM_FORMAT_SINT) {
				/* This isn't actually used in OpenGL. */
				v->fix_fetch[i] = SI_FIX_FETCH_A2_SINT;
			}
		} else if (channel && channel->type == UTIL_FORMAT_TYPE_FIXED) {
			if (desc->swizzle[3] == PIPE_SWIZZLE_1)
				v->fix_fetch[i] = SI_FIX_FETCH_RGBX_32_FIXED;
			else
				v->fix_fetch[i] = SI_FIX_FETCH_RGBA_32_FIXED;
		} else if (channel && channel->size == 32 && !channel->pure_integer) {
			if (channel->type == UTIL_FORMAT_TYPE_SIGNED) {
				if (channel->normalized) {
					if (desc->swizzle[3] == PIPE_SWIZZLE_1)
						v->fix_fetch[i] = SI_FIX_FETCH_RGBX_32_SNORM;
					else
						v->fix_fetch[i] = SI_FIX_FETCH_RGBA_32_SNORM;
				} else {
					v->fix_fetch[i] = SI_FIX_FETCH_RGBA_32_SSCALED;
				}
			} else if (channel->type == UTIL_FORMAT_TYPE_UNSIGNED) {
				if (channel->normalized) {
					if (desc->swizzle[3] == PIPE_SWIZZLE_1)
						v->fix_fetch[i] = SI_FIX_FETCH_RGBX_32_UNORM;
					else
						v->fix_fetch[i] = SI_FIX_FETCH_RGBA_32_UNORM;
				} else {
					v->fix_fetch[i] = SI_FIX_FETCH_RGBA_32_USCALED;
				}
			}
		} else if (channel && channel->size == 64 &&
			   channel->type == UTIL_FORMAT_TYPE_FLOAT) {
			switch (desc->nr_channels) {
			case 1:
			case 2:
				v->fix_fetch[i] = SI_FIX_FETCH_RG_64_FLOAT;
				swizzle[0] = PIPE_SWIZZLE_X;
				swizzle[1] = PIPE_SWIZZLE_Y;
				swizzle[2] = desc->nr_channels == 2 ? PIPE_SWIZZLE_Z : PIPE_SWIZZLE_0;
				swizzle[3] = desc->nr_channels == 2 ? PIPE_SWIZZLE_W : PIPE_SWIZZLE_0;
				break;
			case 3:
				v->fix_fetch[i] = SI_FIX_FETCH_RGB_64_FLOAT;
				swizzle[0] = PIPE_SWIZZLE_X; /* 3 loads */
				swizzle[1] = PIPE_SWIZZLE_Y;
				swizzle[2] = PIPE_SWIZZLE_0;
				swizzle[3] = PIPE_SWIZZLE_0;
				break;
			case 4:
				v->fix_fetch[i] = SI_FIX_FETCH_RGBA_64_FLOAT;
				swizzle[0] = PIPE_SWIZZLE_X; /* 2 loads */
				swizzle[1] = PIPE_SWIZZLE_Y;
				swizzle[2] = PIPE_SWIZZLE_Z;
				swizzle[3] = PIPE_SWIZZLE_W;
				break;
			default:
				assert(0);
			}
		} else if (channel && desc->nr_channels == 3) {
			assert(desc->swizzle[0] == PIPE_SWIZZLE_X);

			if (channel->size == 8) {
				if (channel->pure_integer)
					v->fix_fetch[i] = SI_FIX_FETCH_RGB_8_INT;
				else
					v->fix_fetch[i] = SI_FIX_FETCH_RGB_8;
			} else if (channel->size == 16) {
				if (channel->pure_integer)
					v->fix_fetch[i] = SI_FIX_FETCH_RGB_16_INT;
				else
					v->fix_fetch[i] = SI_FIX_FETCH_RGB_16;
			}
		}

		v->rsrc_word3[i] = S_008F0C_DST_SEL_X(si_map_swizzle(swizzle[0])) |
				   S_008F0C_DST_SEL_Y(si_map_swizzle(swizzle[1])) |
				   S_008F0C_DST_SEL_Z(si_map_swizzle(swizzle[2])) |
				   S_008F0C_DST_SEL_W(si_map_swizzle(swizzle[3])) |
				   S_008F0C_NUM_FORMAT(num_format) |
				   S_008F0C_DATA_FORMAT(data_format);
	}
	return v;
}

static void si_bind_vertex_elements(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_vertex_elements *old = sctx->vertex_elements;
	struct si_vertex_elements *v = (struct si_vertex_elements*)state;

	sctx->vertex_elements = v;
	sctx->vertex_buffers_dirty = true;

	if (v &&
	    (!old ||
	     old->count != v->count ||
	     old->uses_instance_divisors != v->uses_instance_divisors ||
	     v->uses_instance_divisors || /* we don't check which divisors changed */
	     memcmp(old->fix_fetch, v->fix_fetch, sizeof(v->fix_fetch[0]) * v->count)))
		sctx->do_update_shaders = true;

	if (v && v->instance_divisor_is_fetched) {
		struct pipe_constant_buffer cb;

		cb.buffer = NULL;
		cb.user_buffer = v->instance_divisors;
		cb.buffer_offset = 0;
		cb.buffer_size = sizeof(uint32_t) * v->count;
		si_set_rw_buffer(sctx, SI_VS_CONST_INSTANCE_DIVISORS, &cb);
	}
}

static void si_delete_vertex_element(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;

	if (sctx->vertex_elements == state)
		sctx->vertex_elements = NULL;
	FREE(state);
}

static void si_set_vertex_buffers(struct pipe_context *ctx,
				  unsigned start_slot, unsigned count,
				  const struct pipe_vertex_buffer *buffers)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct pipe_vertex_buffer *dst = sctx->vertex_buffer + start_slot;
	int i;

	assert(start_slot + count <= ARRAY_SIZE(sctx->vertex_buffer));

	if (buffers) {
		for (i = 0; i < count; i++) {
			const struct pipe_vertex_buffer *src = buffers + i;
			struct pipe_vertex_buffer *dsti = dst + i;
			struct pipe_resource *buf = src->buffer.resource;

			pipe_resource_reference(&dsti->buffer.resource, buf);
			dsti->buffer_offset = src->buffer_offset;
			dsti->stride = src->stride;
			si_context_add_resource_size(sctx, buf);
			if (buf)
				r600_resource(buf)->bind_history |= PIPE_BIND_VERTEX_BUFFER;
		}
	} else {
		for (i = 0; i < count; i++) {
			pipe_resource_reference(&dst[i].buffer.resource, NULL);
		}
	}
	sctx->vertex_buffers_dirty = true;
}

/*
 * Misc
 */

static void si_set_tess_state(struct pipe_context *ctx,
			      const float default_outer_level[4],
			      const float default_inner_level[2])
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct pipe_constant_buffer cb;
	float array[8];

	memcpy(array, default_outer_level, sizeof(float) * 4);
	memcpy(array+4, default_inner_level, sizeof(float) * 2);

	cb.buffer = NULL;
	cb.user_buffer = NULL;
	cb.buffer_size = sizeof(array);

	si_upload_const_buffer(sctx, (struct r600_resource**)&cb.buffer,
			       (void*)array, sizeof(array),
			       &cb.buffer_offset);

	si_set_rw_buffer(sctx, SI_HS_CONST_DEFAULT_TESS_LEVELS, &cb);
	pipe_resource_reference(&cb.buffer, NULL);
}

static void si_texture_barrier(struct pipe_context *ctx, unsigned flags)
{
	struct si_context *sctx = (struct si_context *)ctx;

	si_update_fb_dirtiness_after_rendering(sctx);

	/* Multisample surfaces are flushed in si_decompress_textures. */
	if (sctx->framebuffer.uncompressed_cb_mask)
		si_make_CB_shader_coherent(sctx, sctx->framebuffer.nr_samples,
					   sctx->framebuffer.CB_has_shader_readable_metadata);
}

/* This only ensures coherency for shader image/buffer stores. */
static void si_memory_barrier(struct pipe_context *ctx, unsigned flags)
{
	struct si_context *sctx = (struct si_context *)ctx;

	/* Subsequent commands must wait for all shader invocations to
	 * complete. */
	sctx->flags |= SI_CONTEXT_PS_PARTIAL_FLUSH |
	                 SI_CONTEXT_CS_PARTIAL_FLUSH;

	if (flags & PIPE_BARRIER_CONSTANT_BUFFER)
		sctx->flags |= SI_CONTEXT_INV_SMEM_L1 |
				 SI_CONTEXT_INV_VMEM_L1;

	if (flags & (PIPE_BARRIER_VERTEX_BUFFER |
		     PIPE_BARRIER_SHADER_BUFFER |
		     PIPE_BARRIER_TEXTURE |
		     PIPE_BARRIER_IMAGE |
		     PIPE_BARRIER_STREAMOUT_BUFFER |
		     PIPE_BARRIER_GLOBAL_BUFFER)) {
		/* As far as I can tell, L1 contents are written back to L2
		 * automatically at end of shader, but the contents of other
		 * L1 caches might still be stale. */
		sctx->flags |= SI_CONTEXT_INV_VMEM_L1;
	}

	if (flags & PIPE_BARRIER_INDEX_BUFFER) {
		/* Indices are read through TC L2 since VI.
		 * L1 isn't used.
		 */
		if (sctx->screen->info.chip_class <= CIK)
			sctx->flags |= SI_CONTEXT_WRITEBACK_GLOBAL_L2;
	}

	/* MSAA color, any depth and any stencil are flushed in
	 * si_decompress_textures when needed.
	 */
	if (flags & PIPE_BARRIER_FRAMEBUFFER &&
	    sctx->framebuffer.uncompressed_cb_mask) {
		sctx->flags |= SI_CONTEXT_FLUSH_AND_INV_CB;

		if (sctx->chip_class <= VI)
			sctx->flags |= SI_CONTEXT_WRITEBACK_GLOBAL_L2;
	}

	/* Indirect buffers use TC L2 on GFX9, but not older hw. */
	if (sctx->screen->info.chip_class <= VI &&
	    flags & PIPE_BARRIER_INDIRECT_BUFFER)
		sctx->flags |= SI_CONTEXT_WRITEBACK_GLOBAL_L2;
}

static void *si_create_blend_custom(struct si_context *sctx, unsigned mode)
{
	struct pipe_blend_state blend;

	memset(&blend, 0, sizeof(blend));
	blend.independent_blend_enable = true;
	blend.rt[0].colormask = 0xf;
	return si_create_blend_state_mode(&sctx->b, &blend, mode);
}

static void si_init_config(struct si_context *sctx);

void si_init_state_functions(struct si_context *sctx)
{
	si_init_external_atom(sctx, &sctx->render_cond_atom, &sctx->atoms.s.render_cond);
	si_init_external_atom(sctx, &sctx->streamout.begin_atom, &sctx->atoms.s.streamout_begin);
	si_init_external_atom(sctx, &sctx->streamout.enable_atom, &sctx->atoms.s.streamout_enable);
	si_init_external_atom(sctx, &sctx->scissors.atom, &sctx->atoms.s.scissors);
	si_init_external_atom(sctx, &sctx->viewports.atom, &sctx->atoms.s.viewports);

	si_init_atom(sctx, &sctx->framebuffer.atom, &sctx->atoms.s.framebuffer, si_emit_framebuffer_state);
	si_init_atom(sctx, &sctx->msaa_sample_locs.atom, &sctx->atoms.s.msaa_sample_locs, si_emit_msaa_sample_locs);
	si_init_atom(sctx, &sctx->db_render_state, &sctx->atoms.s.db_render_state, si_emit_db_render_state);
	si_init_atom(sctx, &sctx->dpbb_state, &sctx->atoms.s.dpbb_state, si_emit_dpbb_state);
	si_init_atom(sctx, &sctx->msaa_config, &sctx->atoms.s.msaa_config, si_emit_msaa_config);
	si_init_atom(sctx, &sctx->sample_mask.atom, &sctx->atoms.s.sample_mask, si_emit_sample_mask);
	si_init_atom(sctx, &sctx->cb_render_state, &sctx->atoms.s.cb_render_state, si_emit_cb_render_state);
	si_init_atom(sctx, &sctx->blend_color.atom, &sctx->atoms.s.blend_color, si_emit_blend_color);
	si_init_atom(sctx, &sctx->clip_regs, &sctx->atoms.s.clip_regs, si_emit_clip_regs);
	si_init_atom(sctx, &sctx->clip_state.atom, &sctx->atoms.s.clip_state, si_emit_clip_state);
	si_init_atom(sctx, &sctx->stencil_ref.atom, &sctx->atoms.s.stencil_ref, si_emit_stencil_ref);

	sctx->b.create_blend_state = si_create_blend_state;
	sctx->b.bind_blend_state = si_bind_blend_state;
	sctx->b.delete_blend_state = si_delete_blend_state;
	sctx->b.set_blend_color = si_set_blend_color;

	sctx->b.create_rasterizer_state = si_create_rs_state;
	sctx->b.bind_rasterizer_state = si_bind_rs_state;
	sctx->b.delete_rasterizer_state = si_delete_rs_state;

	sctx->b.create_depth_stencil_alpha_state = si_create_dsa_state;
	sctx->b.bind_depth_stencil_alpha_state = si_bind_dsa_state;
	sctx->b.delete_depth_stencil_alpha_state = si_delete_dsa_state;

	sctx->custom_dsa_flush = si_create_db_flush_dsa(sctx);
	sctx->custom_blend_resolve = si_create_blend_custom(sctx, V_028808_CB_RESOLVE);
	sctx->custom_blend_fmask_decompress = si_create_blend_custom(sctx, V_028808_CB_FMASK_DECOMPRESS);
	sctx->custom_blend_eliminate_fastclear = si_create_blend_custom(sctx, V_028808_CB_ELIMINATE_FAST_CLEAR);
	sctx->custom_blend_dcc_decompress = si_create_blend_custom(sctx, V_028808_CB_DCC_DECOMPRESS);

	sctx->b.set_clip_state = si_set_clip_state;
	sctx->b.set_stencil_ref = si_set_stencil_ref;

	sctx->b.set_framebuffer_state = si_set_framebuffer_state;

	sctx->b.create_sampler_state = si_create_sampler_state;
	sctx->b.delete_sampler_state = si_delete_sampler_state;

	sctx->b.create_sampler_view = si_create_sampler_view;
	sctx->b.sampler_view_destroy = si_sampler_view_destroy;

	sctx->b.set_sample_mask = si_set_sample_mask;

	sctx->b.create_vertex_elements_state = si_create_vertex_elements;
	sctx->b.bind_vertex_elements_state = si_bind_vertex_elements;
	sctx->b.delete_vertex_elements_state = si_delete_vertex_element;
	sctx->b.set_vertex_buffers = si_set_vertex_buffers;

	sctx->b.texture_barrier = si_texture_barrier;
	sctx->b.memory_barrier = si_memory_barrier;
	sctx->b.set_min_samples = si_set_min_samples;
	sctx->b.set_tess_state = si_set_tess_state;

	sctx->b.set_active_query_state = si_set_active_query_state;

	sctx->b.draw_vbo = si_draw_vbo;

	si_init_config(sctx);
}

void si_init_screen_state_functions(struct si_screen *sscreen)
{
	sscreen->b.is_format_supported = si_is_format_supported;
}

static void si_set_grbm_gfx_index(struct si_context *sctx,
				  struct si_pm4_state *pm4,  unsigned value)
{
	unsigned reg = sctx->chip_class >= CIK ? R_030800_GRBM_GFX_INDEX :
						   R_00802C_GRBM_GFX_INDEX;
	si_pm4_set_reg(pm4, reg, value);
}

static void si_set_grbm_gfx_index_se(struct si_context *sctx,
				     struct si_pm4_state *pm4, unsigned se)
{
	assert(se == ~0 || se < sctx->screen->info.max_se);
	si_set_grbm_gfx_index(sctx, pm4,
			      (se == ~0 ? S_030800_SE_BROADCAST_WRITES(1) :
					  S_030800_SE_INDEX(se)) |
			      S_030800_SH_BROADCAST_WRITES(1) |
			      S_030800_INSTANCE_BROADCAST_WRITES(1));
}

static void
si_write_harvested_raster_configs(struct si_context *sctx,
				  struct si_pm4_state *pm4,
				  unsigned raster_config,
				  unsigned raster_config_1)
{
	unsigned sh_per_se = MAX2(sctx->screen->info.max_sh_per_se, 1);
	unsigned num_se = MAX2(sctx->screen->info.max_se, 1);
	unsigned rb_mask = sctx->screen->info.enabled_rb_mask;
	unsigned num_rb = MIN2(sctx->screen->info.num_render_backends, 16);
	unsigned rb_per_pkr = MIN2(num_rb / num_se / sh_per_se, 2);
	unsigned rb_per_se = num_rb / num_se;
	unsigned se_mask[4];
	unsigned se;

	se_mask[0] = ((1 << rb_per_se) - 1);
	se_mask[1] = (se_mask[0] << rb_per_se);
	se_mask[2] = (se_mask[1] << rb_per_se);
	se_mask[3] = (se_mask[2] << rb_per_se);

	se_mask[0] &= rb_mask;
	se_mask[1] &= rb_mask;
	se_mask[2] &= rb_mask;
	se_mask[3] &= rb_mask;

	assert(num_se == 1 || num_se == 2 || num_se == 4);
	assert(sh_per_se == 1 || sh_per_se == 2);
	assert(rb_per_pkr == 1 || rb_per_pkr == 2);

	/* XXX: I can't figure out what the *_XSEL and *_YSEL
	 * fields are for, so I'm leaving them as their default
	 * values. */

	for (se = 0; se < num_se; se++) {
		unsigned raster_config_se = raster_config;
		unsigned pkr0_mask = ((1 << rb_per_pkr) - 1) << (se * rb_per_se);
		unsigned pkr1_mask = pkr0_mask << rb_per_pkr;
		int idx = (se / 2) * 2;

		if ((num_se > 1) && (!se_mask[idx] || !se_mask[idx + 1])) {
			raster_config_se &= C_028350_SE_MAP;

			if (!se_mask[idx]) {
				raster_config_se |=
					S_028350_SE_MAP(V_028350_RASTER_CONFIG_SE_MAP_3);
			} else {
				raster_config_se |=
					S_028350_SE_MAP(V_028350_RASTER_CONFIG_SE_MAP_0);
			}
		}

		pkr0_mask &= rb_mask;
		pkr1_mask &= rb_mask;
		if (rb_per_se > 2 && (!pkr0_mask || !pkr1_mask)) {
			raster_config_se &= C_028350_PKR_MAP;

			if (!pkr0_mask) {
				raster_config_se |=
					S_028350_PKR_MAP(V_028350_RASTER_CONFIG_PKR_MAP_3);
			} else {
				raster_config_se |=
					S_028350_PKR_MAP(V_028350_RASTER_CONFIG_PKR_MAP_0);
			}
		}

		if (rb_per_se >= 2) {
			unsigned rb0_mask = 1 << (se * rb_per_se);
			unsigned rb1_mask = rb0_mask << 1;

			rb0_mask &= rb_mask;
			rb1_mask &= rb_mask;
			if (!rb0_mask || !rb1_mask) {
				raster_config_se &= C_028350_RB_MAP_PKR0;

				if (!rb0_mask) {
					raster_config_se |=
						S_028350_RB_MAP_PKR0(V_028350_RASTER_CONFIG_RB_MAP_3);
				} else {
					raster_config_se |=
						S_028350_RB_MAP_PKR0(V_028350_RASTER_CONFIG_RB_MAP_0);
				}
			}

			if (rb_per_se > 2) {
				rb0_mask = 1 << (se * rb_per_se + rb_per_pkr);
				rb1_mask = rb0_mask << 1;
				rb0_mask &= rb_mask;
				rb1_mask &= rb_mask;
				if (!rb0_mask || !rb1_mask) {
					raster_config_se &= C_028350_RB_MAP_PKR1;

					if (!rb0_mask) {
						raster_config_se |=
							S_028350_RB_MAP_PKR1(V_028350_RASTER_CONFIG_RB_MAP_3);
					} else {
						raster_config_se |=
							S_028350_RB_MAP_PKR1(V_028350_RASTER_CONFIG_RB_MAP_0);
					}
				}
			}
		}

		si_set_grbm_gfx_index_se(sctx, pm4, se);
		si_pm4_set_reg(pm4, R_028350_PA_SC_RASTER_CONFIG, raster_config_se);
	}
	si_set_grbm_gfx_index(sctx, pm4, ~0);

	if (sctx->chip_class >= CIK) {
		if ((num_se > 2) && ((!se_mask[0] && !se_mask[1]) ||
		                     (!se_mask[2] && !se_mask[3]))) {
			raster_config_1 &= C_028354_SE_PAIR_MAP;

			if (!se_mask[0] && !se_mask[1]) {
				raster_config_1 |=
					S_028354_SE_PAIR_MAP(V_028354_RASTER_CONFIG_SE_PAIR_MAP_3);
			} else {
				raster_config_1 |=
					S_028354_SE_PAIR_MAP(V_028354_RASTER_CONFIG_SE_PAIR_MAP_0);
			}
		}

		si_pm4_set_reg(pm4, R_028354_PA_SC_RASTER_CONFIG_1, raster_config_1);
	}
}

static void si_set_raster_config(struct si_context *sctx, struct si_pm4_state *pm4)
{
	struct si_screen *sscreen = sctx->screen;
	unsigned num_rb = MIN2(sctx->screen->info.num_render_backends, 16);
	unsigned rb_mask = sctx->screen->info.enabled_rb_mask;
	unsigned raster_config, raster_config_1;

	switch (sctx->family) {
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
		raster_config = 0x2a00126a;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_VERDE:
		raster_config = 0x0000124a;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_OLAND:
		raster_config = 0x00000082;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_HAINAN:
		raster_config = 0x00000000;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_BONAIRE:
		raster_config = 0x16000012;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_HAWAII:
		raster_config = 0x3a00161a;
		raster_config_1 = 0x0000002e;
		break;
	case CHIP_FIJI:
		if (sscreen->info.cik_macrotile_mode_array[0] == 0x000000e8) {
			/* old kernels with old tiling config */
			raster_config = 0x16000012;
			raster_config_1 = 0x0000002a;
		} else {
			raster_config = 0x3a00161a;
			raster_config_1 = 0x0000002e;
		}
		break;
	case CHIP_VEGAM:
		raster_config = 0x3a00161a;
		raster_config_1 = 0x0000002e;
		break;
	case CHIP_POLARIS10:
		raster_config = 0x16000012;
		raster_config_1 = 0x0000002a;
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		raster_config = 0x16000012;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_TONGA:
		raster_config = 0x16000012;
		raster_config_1 = 0x0000002a;
		break;
	case CHIP_ICELAND:
		if (num_rb == 1)
			raster_config = 0x00000000;
		else
			raster_config = 0x00000002;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_CARRIZO:
		raster_config = 0x00000002;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_KAVERI:
		/* KV should be 0x00000002, but that causes problems with radeon */
		raster_config = 0x00000000; /* 0x00000002 */
		raster_config_1 = 0x00000000;
		break;
	case CHIP_KABINI:
	case CHIP_MULLINS:
	case CHIP_STONEY:
		raster_config = 0x00000000;
		raster_config_1 = 0x00000000;
		break;
	default:
		fprintf(stderr,
			"radeonsi: Unknown GPU, using 0 for raster_config\n");
		raster_config = 0x00000000;
		raster_config_1 = 0x00000000;
	}

	if (!rb_mask || util_bitcount(rb_mask) >= num_rb) {
		/* Always use the default config when all backends are enabled
		 * (or when we failed to determine the enabled backends).
		 */
		si_pm4_set_reg(pm4, R_028350_PA_SC_RASTER_CONFIG,
			       raster_config);
		if (sctx->chip_class >= CIK)
			si_pm4_set_reg(pm4, R_028354_PA_SC_RASTER_CONFIG_1,
				       raster_config_1);
	} else {
		si_write_harvested_raster_configs(sctx, pm4, raster_config, raster_config_1);
	}
}

static void si_init_config(struct si_context *sctx)
{
	struct si_screen *sscreen = sctx->screen;
	uint64_t border_color_va = sctx->border_color_buffer->gpu_address;
	bool has_clear_state = sscreen->has_clear_state;
	struct si_pm4_state *pm4 = CALLOC_STRUCT(si_pm4_state);

	/* Only SI can disable CLEAR_STATE for now. */
	assert(has_clear_state || sscreen->info.chip_class == SI);

	if (!pm4)
		return;

	si_pm4_cmd_begin(pm4, PKT3_CONTEXT_CONTROL);
	si_pm4_cmd_add(pm4, CONTEXT_CONTROL_LOAD_ENABLE(1));
	si_pm4_cmd_add(pm4, CONTEXT_CONTROL_SHADOW_ENABLE(1));
	si_pm4_cmd_end(pm4, false);

	if (has_clear_state) {
		si_pm4_cmd_begin(pm4, PKT3_CLEAR_STATE);
		si_pm4_cmd_add(pm4, 0);
		si_pm4_cmd_end(pm4, false);
	}

	if (sctx->chip_class <= VI)
		si_set_raster_config(sctx, pm4);

	si_pm4_set_reg(pm4, R_028A18_VGT_HOS_MAX_TESS_LEVEL, fui(64));
	if (!has_clear_state)
		si_pm4_set_reg(pm4, R_028A1C_VGT_HOS_MIN_TESS_LEVEL, fui(0));

	/* FIXME calculate these values somehow ??? */
	if (sctx->chip_class <= VI) {
		si_pm4_set_reg(pm4, R_028A54_VGT_GS_PER_ES, SI_GS_PER_ES);
		si_pm4_set_reg(pm4, R_028A58_VGT_ES_PER_GS, 0x40);
	}

	if (!has_clear_state) {
		si_pm4_set_reg(pm4, R_028A5C_VGT_GS_PER_VS, 0x2);
		si_pm4_set_reg(pm4, R_028A8C_VGT_PRIMITIVEID_RESET, 0x0);
		si_pm4_set_reg(pm4, R_028B98_VGT_STRMOUT_BUFFER_CONFIG, 0x0);
	}

	si_pm4_set_reg(pm4, R_028AA0_VGT_INSTANCE_STEP_RATE_0, 1);
	if (!has_clear_state)
		si_pm4_set_reg(pm4, R_028AB8_VGT_VTX_CNT_EN, 0x0);
	if (sctx->chip_class < CIK)
		si_pm4_set_reg(pm4, R_008A14_PA_CL_ENHANCE, S_008A14_NUM_CLIP_SEQ(3) |
			       S_008A14_CLIP_VTX_REORDER_ENA(1));

	si_pm4_set_reg(pm4, R_028BD4_PA_SC_CENTROID_PRIORITY_0, 0x76543210);
	si_pm4_set_reg(pm4, R_028BD8_PA_SC_CENTROID_PRIORITY_1, 0xfedcba98);

	if (!has_clear_state)
		si_pm4_set_reg(pm4, R_02882C_PA_SU_PRIM_FILTER_CNTL, 0);

	/* CLEAR_STATE doesn't clear these correctly on certain generations.
	 * I don't know why. Deduced by trial and error.
	 */
	if (sctx->chip_class <= CIK) {
		si_pm4_set_reg(pm4, R_028B28_VGT_STRMOUT_DRAW_OPAQUE_OFFSET, 0);
		si_pm4_set_reg(pm4, R_028204_PA_SC_WINDOW_SCISSOR_TL, S_028204_WINDOW_OFFSET_DISABLE(1));
		si_pm4_set_reg(pm4, R_028240_PA_SC_GENERIC_SCISSOR_TL, S_028240_WINDOW_OFFSET_DISABLE(1));
		si_pm4_set_reg(pm4, R_028244_PA_SC_GENERIC_SCISSOR_BR,
			       S_028244_BR_X(16384) | S_028244_BR_Y(16384));
		si_pm4_set_reg(pm4, R_028030_PA_SC_SCREEN_SCISSOR_TL, 0);
		si_pm4_set_reg(pm4, R_028034_PA_SC_SCREEN_SCISSOR_BR,
			       S_028034_BR_X(16384) | S_028034_BR_Y(16384));
	}

	if (!has_clear_state) {
		si_pm4_set_reg(pm4, R_02820C_PA_SC_CLIPRECT_RULE, 0xFFFF);
		si_pm4_set_reg(pm4, R_028230_PA_SC_EDGERULE,
			       S_028230_ER_TRI(0xA) |
			       S_028230_ER_POINT(0xA) |
			       S_028230_ER_RECT(0xA) |
			       /* Required by DX10_DIAMOND_TEST_ENA: */
			       S_028230_ER_LINE_LR(0x1A) |
			       S_028230_ER_LINE_RL(0x26) |
			       S_028230_ER_LINE_TB(0xA) |
			       S_028230_ER_LINE_BT(0xA));
		/* PA_SU_HARDWARE_SCREEN_OFFSET must be 0 due to hw bug on SI */
		si_pm4_set_reg(pm4, R_028234_PA_SU_HARDWARE_SCREEN_OFFSET, 0);
		si_pm4_set_reg(pm4, R_028820_PA_CL_NANINF_CNTL, 0);
		si_pm4_set_reg(pm4, R_028AC0_DB_SRESULTS_COMPARE_STATE0, 0x0);
		si_pm4_set_reg(pm4, R_028AC4_DB_SRESULTS_COMPARE_STATE1, 0x0);
		si_pm4_set_reg(pm4, R_028AC8_DB_PRELOAD_CONTROL, 0x0);
		si_pm4_set_reg(pm4, R_02800C_DB_RENDER_OVERRIDE, 0);
	}

	if (sctx->chip_class >= GFX9) {
		si_pm4_set_reg(pm4, R_030920_VGT_MAX_VTX_INDX, ~0);
		si_pm4_set_reg(pm4, R_030924_VGT_MIN_VTX_INDX, 0);
		si_pm4_set_reg(pm4, R_030928_VGT_INDX_OFFSET, 0);
	} else {
		/* These registers, when written, also overwrite the CLEAR_STATE
		 * context, so we can't rely on CLEAR_STATE setting them.
		 * It would be an issue if there was another UMD changing them.
		 */
		si_pm4_set_reg(pm4, R_028400_VGT_MAX_VTX_INDX, ~0);
		si_pm4_set_reg(pm4, R_028404_VGT_MIN_VTX_INDX, 0);
		si_pm4_set_reg(pm4, R_028408_VGT_INDX_OFFSET, 0);
	}

	if (sctx->chip_class >= CIK) {
		if (sctx->chip_class >= GFX9) {
			si_pm4_set_reg(pm4, R_00B41C_SPI_SHADER_PGM_RSRC3_HS,
				       S_00B41C_CU_EN(0xffff) | S_00B41C_WAVE_LIMIT(0x3F));
		} else {
			si_pm4_set_reg(pm4, R_00B51C_SPI_SHADER_PGM_RSRC3_LS,
				       S_00B51C_CU_EN(0xffff) | S_00B51C_WAVE_LIMIT(0x3F));
			si_pm4_set_reg(pm4, R_00B41C_SPI_SHADER_PGM_RSRC3_HS,
				       S_00B41C_WAVE_LIMIT(0x3F));
			si_pm4_set_reg(pm4, R_00B31C_SPI_SHADER_PGM_RSRC3_ES,
				       S_00B31C_CU_EN(0xffff) | S_00B31C_WAVE_LIMIT(0x3F));

			/* If this is 0, Bonaire can hang even if GS isn't being used.
			 * Other chips are unaffected. These are suboptimal values,
			 * but we don't use on-chip GS.
			 */
			si_pm4_set_reg(pm4, R_028A44_VGT_GS_ONCHIP_CNTL,
				       S_028A44_ES_VERTS_PER_SUBGRP(64) |
				       S_028A44_GS_PRIMS_PER_SUBGRP(4));
		}
		si_pm4_set_reg(pm4, R_00B21C_SPI_SHADER_PGM_RSRC3_GS,
			       S_00B21C_CU_EN(0xffff) | S_00B21C_WAVE_LIMIT(0x3F));

		/* Compute LATE_ALLOC_VS.LIMIT. */
		unsigned num_cu_per_sh = sscreen->info.num_good_compute_units /
					 (sscreen->info.max_se *
					  sscreen->info.max_sh_per_se);
		unsigned late_alloc_limit; /* The limit is per SH. */

		if (sctx->family == CHIP_KABINI) {
			late_alloc_limit = 0; /* Potential hang on Kabini. */
		} else if (num_cu_per_sh <= 4) {
			/* Too few available compute units per SH. Disallowing
			 * VS to run on one CU could hurt us more than late VS
			 * allocation would help.
			 *
			 * 2 is the highest safe number that allows us to keep
			 * all CUs enabled.
			 */
			late_alloc_limit = 2;
		} else {
			/* This is a good initial value, allowing 1 late_alloc
			 * wave per SIMD on num_cu - 2.
			 */
			late_alloc_limit = (num_cu_per_sh - 2) * 4;

			/* The limit is 0-based, so 0 means 1. */
			assert(late_alloc_limit > 0 && late_alloc_limit <= 64);
			late_alloc_limit -= 1;
		}

		/* VS can't execute on one CU if the limit is > 2. */
		si_pm4_set_reg(pm4, R_00B118_SPI_SHADER_PGM_RSRC3_VS,
			       S_00B118_CU_EN(late_alloc_limit > 2 ? 0xfffe : 0xffff) |
			       S_00B118_WAVE_LIMIT(0x3F));
		si_pm4_set_reg(pm4, R_00B11C_SPI_SHADER_LATE_ALLOC_VS,
			       S_00B11C_LIMIT(late_alloc_limit));
		si_pm4_set_reg(pm4, R_00B01C_SPI_SHADER_PGM_RSRC3_PS,
			       S_00B01C_CU_EN(0xffff) | S_00B01C_WAVE_LIMIT(0x3F));
	}

	if (sctx->chip_class >= VI) {
		unsigned vgt_tess_distribution;

		vgt_tess_distribution =
			S_028B50_ACCUM_ISOLINE(32) |
			S_028B50_ACCUM_TRI(11) |
			S_028B50_ACCUM_QUAD(11) |
			S_028B50_DONUT_SPLIT(16);

		/* Testing with Unigine Heaven extreme tesselation yielded best results
		 * with TRAP_SPLIT = 3.
		 */
		if (sctx->family == CHIP_FIJI ||
		    sctx->family >= CHIP_POLARIS10)
			vgt_tess_distribution |= S_028B50_TRAP_SPLIT(3);

		si_pm4_set_reg(pm4, R_028B50_VGT_TESS_DISTRIBUTION, vgt_tess_distribution);
	} else if (!has_clear_state) {
		si_pm4_set_reg(pm4, R_028C58_VGT_VERTEX_REUSE_BLOCK_CNTL, 14);
		si_pm4_set_reg(pm4, R_028C5C_VGT_OUT_DEALLOC_CNTL, 16);
	}

	si_pm4_set_reg(pm4, R_028080_TA_BC_BASE_ADDR, border_color_va >> 8);
	if (sctx->chip_class >= CIK) {
		si_pm4_set_reg(pm4, R_028084_TA_BC_BASE_ADDR_HI,
			       S_028084_ADDRESS(border_color_va >> 40));
	}
	si_pm4_add_bo(pm4, sctx->border_color_buffer, RADEON_USAGE_READ,
		      RADEON_PRIO_BORDER_COLORS);

	if (sctx->chip_class >= GFX9) {
		unsigned num_se = sscreen->info.max_se;
		unsigned pc_lines = 0;

		switch (sctx->family) {
		case CHIP_VEGA10:
		case CHIP_VEGA12:
			pc_lines = 4096;
			break;
		case CHIP_RAVEN:
			pc_lines = 1024;
			break;
		default:
			assert(0);
		}

		si_pm4_set_reg(pm4, R_028C48_PA_SC_BINNER_CNTL_1,
			       S_028C48_MAX_ALLOC_COUNT(MIN2(128, pc_lines / (4 * num_se))) |
			       S_028C48_MAX_PRIM_PER_BATCH(1023));
		si_pm4_set_reg(pm4, R_028C4C_PA_SC_CONSERVATIVE_RASTERIZATION_CNTL,
			       S_028C4C_NULL_SQUAD_AA_MASK_ENABLE(1));
		si_pm4_set_reg(pm4, R_030968_VGT_INSTANCE_BASE_ID, 0);
	}

	si_pm4_upload_indirect_buffer(sctx, pm4);
	sctx->init_config = pm4;
}
