/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_trans_nir.c
 *
 * \brief NIR translation functions.
 */

#include "compiler/shader_enums.h"
#include "hwdef/rogue_hw_defs.h"
#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "util/bitset.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/ralloc.h"

#include <assert.h>
#include <stdio.h>

/** Translation context. */
typedef struct _trans_ctx {
   pco_ctx *pco_ctx; /** PCO compiler context. */
   pco_shader *shader; /** Current shader. */
   pco_func *func; /** Current function. */
   pco_builder b; /** Builder. */
   mesa_shader_stage stage; /** Shader stage. */
   enum pco_cf_node_flag flag; /** Implementation-defined control-flow flag. */
   bool ftz32b;
   bool olchk;

   BITSET_WORD *float_types; /** NIR SSA float vars. */
   BITSET_WORD *int_types; /** NIR SSA int vars. */
} trans_ctx;

/* Forward declarations. */
static pco_block *trans_cf_nodes(trans_ctx *tctx,
                                 pco_cf_node *parent_cf_node,
                                 struct list_head *cf_node_list,
                                 struct exec_list *nir_cf_node_list);

static inline void pco_fence(pco_builder *b)
{
   pco_flush_p0(b);
   pco_br_next(b, .exec_cnd = PCO_EXEC_CND_E1_Z1);
   pco_br_next(b, .exec_cnd = PCO_EXEC_CND_E1_Z0);
}

/**
 * \brief Splits a vector destination into scalar components.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] instr Instruction producing the vector destination.
 * \param[in] dest Instruction destination.
 */
static void split_dest_comps(trans_ctx *tctx, pco_instr *instr, pco_ref dest)
{
   unsigned chans = pco_ref_get_chans(dest);
   assert(chans > 1);

   pco_func *func = tctx->func;

   pco_vec_info *vec_info = rzalloc_size(func->vec_infos, sizeof(*vec_info));
   vec_info->instr = instr;
   vec_info->comps =
      rzalloc_array_size(vec_info, sizeof(*vec_info->comps), chans);

   for (unsigned u = 0; u < chans; ++u) {
      pco_ref comp = pco_ref_new_ssa(func, pco_ref_get_bits(dest), 1);
      vec_info->comps[u] = pco_comp(&tctx->b, comp, dest, pco_ref_val16(u));
   }

   _mesa_hash_table_u64_insert(func->vec_infos, dest.val, vec_info);
}

/**
 * \brief Translates a NIR def into a PCO reference.
 *
 * \param[in] def The nir def.
 * \return The PCO reference.
 */
static inline pco_ref pco_ref_nir_def(const nir_def *def)
{
   return pco_ref_ssa(def->index, def->bit_size, def->num_components);
}

/**
 * \brief Translates a NIR src into a PCO reference.
 *
 * \param[in] src The nir src.
 * \return The PCO reference.
 */
static inline pco_ref pco_ref_nir_src(const nir_src *src)
{
   return pco_ref_nir_def(src->ssa);
}

/**
 * \brief Translates a NIR def into a PCO reference with type information.
 *
 * \param[in] def The nir def.
 * \param[in] tctx Translation context.
 * \return The PCO reference.
 */
static inline pco_ref pco_ref_nir_def_t(const nir_def *def, trans_ctx *tctx)
{
   pco_ref ref = pco_ref_nir_def(def);

   bool is_float = BITSET_TEST(tctx->float_types, def->index);
   bool is_int = BITSET_TEST(tctx->int_types, def->index);

   if (is_float)
      ref.dtype = PCO_DTYPE_FLOAT;
   else if (is_int)
      ref.dtype = PCO_DTYPE_UNSIGNED;

   return ref;
}

/**
 * \brief Translates a NIR src into a PCO reference with type information.
 *
 * \param[in] src The nir src.
 * \param[in] tctx Translation context.
 * \return The PCO reference.
 */
static inline pco_ref pco_ref_nir_src_t(const nir_src *src, trans_ctx *tctx)
{
   return pco_ref_nir_def_t(src->ssa, tctx);
}

/**
 * \brief Translates a NIR alu src into a PCO reference with type information,
 *        extracting from and/or building new vectors as needed.
 *
 * \param[in] src The nir src.
 * \param[in,out] tctx Translation context.
 * \return The PCO reference.
 */
static inline pco_ref
pco_ref_nir_alu_src_t(const nir_alu_instr *alu, unsigned src, trans_ctx *tctx)
{
   const nir_alu_src *alu_src = &alu->src[src];
   /* unsigned chans = nir_src_num_components(alu_src->src); */
   unsigned chans = nir_ssa_alu_instr_src_components(alu, src);

   bool seq_comps =
      nir_is_sequential_comp_swizzle((uint8_t *)alu_src->swizzle, chans);
   pco_ref ref = pco_ref_nir_src_t(&alu_src->src, tctx);
   uint8_t swizzle0 = alu_src->swizzle[0];

   /* Multiple channels, but referencing the entire vector; return as-is. */
   if (!swizzle0 && seq_comps && chans == nir_src_num_components(alu_src->src))
      return ref;

   pco_vec_info *vec_info =
      _mesa_hash_table_u64_search(tctx->func->vec_infos, ref.val);
   ASSERTED bool replicated_scalar =
      nir_is_same_comp_swizzle((uint8_t *)alu_src->swizzle, chans) &&
      !swizzle0 && pco_ref_get_chans(ref) == 1;
   assert(!!vec_info != replicated_scalar);

   /* One channel; just return its component. */
   if (chans == 1)
      return vec_info ? vec_info->comps[swizzle0]->dest[0] : ref;

   /* Multiple channels, either a partial vec and/or swizzling; we need to build
    * a new vec for this.
    */
   pco_ref comps[NIR_MAX_VEC_COMPONENTS] = { 0 };
   for (unsigned u = 0; u < chans; ++u)
      comps[u] = vec_info ? vec_info->comps[alu_src->swizzle[u]]->dest[0] : ref;

   pco_ref vec = pco_ref_new_ssa(tctx->func, pco_ref_get_bits(ref), chans);
   pco_instr *instr = pco_vec(&tctx->b, vec, chans, comps);

   split_dest_comps(tctx, instr, vec);

   return vec;
}

/**
 * \brief Translates a NIR vs load_input intrinsic into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] intr load_input intrinsic.
 * \param[in] dest Instruction destination.
 * \return The translated PCO instruction.
 */
static pco_instr *
trans_load_input_vs(trans_ctx *tctx, nir_intrinsic_instr *intr, pco_ref dest)
{
   UNUSED unsigned base = nir_intrinsic_base(intr);

   /* TODO: f16 support. */

   ASSERTED const nir_src offset = intr->src[0];
   assert(nir_src_as_uint(offset) == 0);

   gl_vert_attrib location = nir_intrinsic_io_semantics(intr).location;
   unsigned component = nir_intrinsic_component(intr);
   unsigned chans = pco_ref_get_chans(dest);

   const pco_range *range = &tctx->shader->data.vs.attribs[location];
   assert(component + chans <= range->count);

   pco_ref src =
      pco_ref_hwreg_vec(range->start + component, PCO_REG_CLASS_VTXIN, chans);
   return pco_mov(&tctx->b, dest, src, .rpt = chans);
}

/**
 * \brief Translates a NIR vs store_output intrinsic into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] intr store_output intrinsic.
 * \param[in] src Instruction source.
 * \return The translated PCO instruction.
 */
static pco_instr *
trans_store_output_vs(trans_ctx *tctx, nir_intrinsic_instr *intr, pco_ref src)
{
   UNUSED unsigned base = nir_intrinsic_base(intr);
   ASSERTED nir_alu_type type = nir_intrinsic_src_type(intr);
   assert(type == nir_type_float32 || type == nir_type_uint32 ||
          type == nir_type_int32);
   /* TODO: f16 support. */

   ASSERTED const nir_src offset = intr->src[1];
   assert(nir_src_as_uint(offset) == 0);

   gl_varying_slot location = nir_intrinsic_io_semantics(intr).location;
   unsigned component = nir_intrinsic_component(intr);
   unsigned chans = pco_ref_get_chans(src);

   /* Only contiguous write masks supported. */
   ASSERTED unsigned write_mask = nir_intrinsic_write_mask(intr);
   assert(write_mask == BITFIELD_MASK(chans));

   const pco_range *range = &tctx->shader->data.vs.varyings[location];
   assert(component + chans <= range->count);

   pco_ref vtxout_addr = pco_ref_val8(range->start + component);
   return pco_uvsw_write(&tctx->b, src, vtxout_addr, .rpt = chans);
}

static pco_instr *trans_uvsw_write(trans_ctx *tctx,
                                   nir_intrinsic_instr *intr,
                                   UNUSED pco_ref offset_src,
                                   pco_ref data_src)
{
   unsigned chans = pco_ref_get_chans(data_src);

   nir_src *noffset_src = &intr->src[0];
   /* TODO: support non-immediate uvsw variant. */
   assert(nir_src_is_const(*noffset_src));
   pco_ref vtxout_addr = pco_ref_val8(nir_src_as_uint(*noffset_src));

   return pco_uvsw_write(&tctx->b, data_src, vtxout_addr, .rpt = chans);
}

static pco_instr *trans_load_reg(trans_ctx *tctx,
                                 nir_intrinsic_instr *intr,
                                 pco_ref dest,
                                 UNUSED pco_ref offset_src,
                                 enum pco_reg_class class)
{
   unsigned chans = pco_ref_get_chans(dest);

   nir_src *noffset_src = &intr->src[0];
   /* TODO: support indexed source offset. */
   assert(nir_src_is_const(*noffset_src));
   unsigned offset = nir_src_as_uint(*noffset_src);
   pco_ref src = pco_ref_hwreg_vec(offset, class, chans);

   return pco_mov(&tctx->b, dest, src, .rpt = chans);
}

static pco_instr *trans_load_tiled_offset(trans_ctx *tctx,
                                          nir_intrinsic_instr *intr,
                                          pco_ref dest)
{
   unsigned component = nir_intrinsic_component(intr);
   bool store = !!nir_intrinsic_flags(intr);

   unsigned base0 = store ? PCO_SR_TILED_ST_COMP0 : PCO_SR_TILED_LD_COMP0;
   unsigned base4 = store ? PCO_SR_TILED_ST_COMP4 : PCO_SR_TILED_LD_COMP4;

   unsigned sr_index = component < 4 ? component + base0
                                     : component + base4 - 4;

   pco_ref tiled_offset = pco_ref_hwreg(sr_index, PCO_REG_CLASS_SPEC);

   return pco_mov(&tctx->b, dest, tiled_offset, .olchk = tctx->olchk);
}

static inline pco_instr *build_itr(pco_builder *b,
                                   pco_ref dest,
                                   enum pco_drc drc,
                                   pco_ref coeffs,
                                   pco_ref wcoeffs,
                                   unsigned _itr_count,
                                   enum pco_itr_mode itr_mode,
                                   bool d)
{
   pco_func *func = pco_cursor_func(b->cursor);
   pco_ref itr_count = pco_ref_val16(_itr_count);
   bool p = !pco_ref_is_null(wcoeffs);
   pco_instr *instr = pco_instr_create(func, 1, 3 + p);

   instr->op = d ? (p ? PCO_OP_DITRP : PCO_OP_DITR)
                 : (p ? PCO_OP_FITRP : PCO_OP_FITR);

   instr->dest[0] = dest;
   instr->src[0] = pco_ref_drc(drc);
   instr->src[1] = coeffs;
   instr->src[2] = p ? wcoeffs : itr_count;
   if (p)
      instr->src[3] = itr_count;

   pco_instr_set_itr_mode(instr, itr_mode);

   if (d)
      pco_fence(b);

   pco_builder_insert_instr(b, instr);

   if (d)
      pco_fence(b);

   return instr;
}

static pco_ref fs_is_single_sampled(trans_ctx *tctx)
{
   assert(tctx->stage == MESA_SHADER_FRAGMENT);

   /* n samples = ...
    * 1 = 0b00000001
    * 2 = 0b00000011
    * 4 = 0b00001111
    * 8 = 0b11111111
    */
   pco_ref smp_rate_mask = pco_ref_new_ssa32(tctx->func);
   pco_pck(&tctx->b, smp_rate_mask, pco_fone, .pck_fmt = PCO_PCK_FMT_COV);

   /* n samples = ...
    * 1 = 0b00000000
    * 2 = 0b00000001
    * 4 = 0b00000111
    * 8 = 0b01111111
    */
   pco_ref smp_rate_mask_shr1 = pco_ref_new_ssa32(tctx->func);
   pco_shift(&tctx->b,
             smp_rate_mask_shr1,
             smp_rate_mask,
             pco_one,
             pco_ref_null(),
             .shiftop = PCO_SHIFTOP_SHR);

   pco_ref is_single_sampled = pco_ref_new_ssa32(tctx->func);
   pco_tstz(&tctx->b,
            is_single_sampled,
            pco_ref_null(),
            smp_rate_mask_shr1,
            .tst_type_main = PCO_TST_TYPE_MAIN_U32);

   return is_single_sampled;
}

/**
 * \brief Translates a NIR fs load_input intrinsic into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] intr load_input intrinsic.
 * \param[in] dest Instruction destination.
 * \return The translated PCO instruction.
 */
static pco_instr *trans_load_input_fs(trans_ctx *tctx,
                                      nir_intrinsic_instr *intr,
                                      pco_ref dest,
                                      bool interp)
{
   pco_fs_data *fs_data = &tctx->shader->data.fs;
   UNUSED unsigned base = nir_intrinsic_base(intr);

   unsigned component = nir_intrinsic_component(intr);
   unsigned chans = pco_ref_get_chans(dest);

   const nir_src offset = interp ? intr->src[1] : intr->src[0];
   assert(nir_src_as_uint(offset) == 0);

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   gl_varying_slot location = io_semantics.location;

   nir_variable *var = nir_find_variable_with_location(tctx->shader->nir,
                                                       nir_var_shader_in,
                                                       location);

   enum glsl_interp_mode interp_mode = var->data.interpolation;

   /* Setup iteration mode. */
   enum pco_itr_mode itr_mode = PCO_ITR_MODE_PIXEL;
   assert(!(var->data.sample && var->data.centroid));
   if (var->data.sample)
      itr_mode = PCO_ITR_MODE_SAMPLE;
   else if (var->data.centroid)
      itr_mode = PCO_ITR_MODE_CENTROID;

   /* Override if load_input_interpolated. */
   if (interp) {
      nir_intrinsic_instr *bary = nir_src_as_intrinsic(intr->src[0]);
      assert(bary);

      interp_mode = nir_intrinsic_interp_mode(bary);

      switch (bary->intrinsic) {
      case nir_intrinsic_load_barycentric_pixel:
         itr_mode = PCO_ITR_MODE_PIXEL;
         break;

      case nir_intrinsic_load_barycentric_sample:
         itr_mode = PCO_ITR_MODE_SAMPLE;
         break;

      case nir_intrinsic_load_barycentric_centroid:
         itr_mode = PCO_ITR_MODE_CENTROID;
         break;

      default:
         UNREACHABLE("");
      }
   }

   if (location == VARYING_SLOT_POS) {
      /* Only scalar supported for now. */
      /* TODO: support vector for zw. */
      assert(chans == 1);

      /* TODO: support packing/partial vars. */
      assert(!var->data.location_frac);

      assert(interp_mode == INTERP_MODE_NOPERSPECTIVE);

      /* Special case: x and y are loaded from special registers. */
      switch (component) {
      case 0: /* x */
      case 1: /* y */ {
         pco_ref xy_s[] = { pco_ref_hwreg(PCO_SR_X_S, PCO_REG_CLASS_SPEC),
                            pco_ref_hwreg(PCO_SR_Y_S, PCO_REG_CLASS_SPEC) };
         pco_ref xy_p[] = { pco_ref_hwreg(PCO_SR_X_P, PCO_REG_CLASS_SPEC),
                            pco_ref_hwreg(PCO_SR_Y_P, PCO_REG_CLASS_SPEC) };

         return pco_csel(&tctx->b,
                         dest,
                         fs_is_single_sampled(tctx),
                         xy_p[component],
                         xy_s[component],
                         .tst_op_main = PCO_TST_OP_MAIN_GZERO,
                         .tst_type_main = PCO_TST_TYPE_MAIN_U32);
      }

      case 2:
         assert(fs_data->uses.z);
         component = 0;
         break;

      case 3:
         assert(fs_data->uses.w);
         component = fs_data->uses.z ? 1 : 0;
         break;

      default:
         UNREACHABLE("");
      }
   }

   const pco_range *range = &fs_data->varyings[location];
   assert(component + (ROGUE_USC_COEFFICIENT_SET_SIZE * chans) <= range->count);

   unsigned coeffs_index =
      range->start + (ROGUE_USC_COEFFICIENT_SET_SIZE * component);

   pco_ref coeffs = pco_ref_hwreg_vec(coeffs_index,
                                      PCO_REG_CLASS_COEFF,
                                      ROGUE_USC_COEFFICIENT_SET_SIZE * chans);

   bool usc_itrsmp_enhanced =
      PVR_HAS_FEATURE(tctx->pco_ctx->dev_info, usc_itrsmp_enhanced);

   switch (interp_mode) {
   case INTERP_MODE_SMOOTH: {
      assert(fs_data->uses.w);

      unsigned wcoeffs_index = fs_data->uses.z ? ROGUE_USC_COEFFICIENT_SET_SIZE
                                               : 0;

      pco_ref wcoeffs = pco_ref_hwreg_vec(wcoeffs_index,
                                          PCO_REG_CLASS_COEFF,
                                          ROGUE_USC_COEFFICIENT_SET_SIZE);

      return build_itr(&tctx->b,
                       dest,
                       PCO_DRC_0,
                       coeffs,
                       wcoeffs,
                       chans,
                       itr_mode,
                       usc_itrsmp_enhanced);
   }

   case INTERP_MODE_FLAT: {
      pco_ref coeff_c =
         pco_ref_hwreg(coeffs_index + ROGUE_USC_COEFFICIENT_SET_C,
                       PCO_REG_CLASS_COEFF);

      assert(chans == 1);
      return pco_mov(&tctx->b, dest, coeff_c);
   }

   case INTERP_MODE_NOPERSPECTIVE:
      return build_itr(&tctx->b,
                       dest,
                       PCO_DRC_0,
                       coeffs,
                       pco_ref_null(),
                       chans,
                       itr_mode,
                       usc_itrsmp_enhanced);

   default:
      /* Should have been previously lowered. */
      UNREACHABLE("");
   }
}

static pco_instr *
trans_load_fs_coeffs(trans_ctx *tctx, nir_intrinsic_instr *intr, pco_ref dest)
{
   pco_fs_data *fs_data = &tctx->shader->data.fs;

   unsigned component = nir_intrinsic_component(intr);
   unsigned chans = pco_ref_get_chans(dest);

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   gl_varying_slot location = io_semantics.location;

   /* Special case, get z/w coeffs. */
   if (location == VARYING_SLOT_POS) {
      if (component == 2) {
         assert(fs_data->uses.z);

         pco_ref zcoeffs = pco_ref_hwreg_vec(0, PCO_REG_CLASS_COEFF, chans);

         return pco_mov(&tctx->b, dest, zcoeffs, .rpt = chans);
      } else if (component == 3) {
         assert(fs_data->uses.w);

         unsigned wcoeffs_index =
            fs_data->uses.z ? ROGUE_USC_COEFFICIENT_SET_SIZE : 0;

         pco_ref wcoeffs =
            pco_ref_hwreg_vec(wcoeffs_index, PCO_REG_CLASS_COEFF, chans);

         return pco_mov(&tctx->b, dest, wcoeffs, .rpt = chans);
      }

      UNREACHABLE("");
   }

   const pco_range *range = &fs_data->varyings[location];
   assert(component + chans <= range->count);

   unsigned coeffs_index =
      range->start + (ROGUE_USC_COEFFICIENT_SET_SIZE * component);

   pco_ref coeffs = pco_ref_hwreg_vec(coeffs_index, PCO_REG_CLASS_COEFF, chans);

   return pco_mov(&tctx->b, dest, coeffs, .rpt = chans);
}

/**
 * \brief Translates a NIR fs store_output intrinsic into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] intr store_output intrinsic.
 * \param[in] src Instruction source.
 * \return The translated PCO instruction.
 */
static pco_instr *
trans_store_output_fs(trans_ctx *tctx, nir_intrinsic_instr *intr, pco_ref src)
{
   pco_fs_data *fs_data = &tctx->shader->data.fs;
   ASSERTED unsigned base = nir_intrinsic_base(intr);
   assert(!base);

   assert(pco_ref_is_scalar(src));
   unsigned component = nir_intrinsic_component(intr);

   ASSERTED const nir_src offset = intr->src[1];
   assert(nir_src_as_uint(offset) == 0);

   gl_frag_result location = nir_intrinsic_io_semantics(intr).location;

   const pco_range *range = &fs_data->outputs[location];
   assert(component < range->count);

   unsigned idx = location - FRAG_RESULT_DATA0;
   bool tile_buffer = fs_data->output_tile_buffers & BITFIELD_BIT(idx);
   if (!tile_buffer) {
      pco_ref dest =
         pco_ref_hwreg(range->start + component, PCO_REG_CLASS_PIXOUT);
      return pco_mov(&tctx->b, dest, src, .olchk = tctx->olchk);
   }

   unsigned tile_buffer_id = range->start;
   pco_range *tile_buffers = &fs_data->tile_buffers;
   assert(tile_buffer_id < (tile_buffers->count / tile_buffers->stride));
   unsigned sh_index =
      tile_buffers->start + tile_buffer_id * tile_buffers->stride;

   pco_ref base_addr[2];
   pco_ref_hwreg_addr_comps(sh_index, PCO_REG_CLASS_SHARED, base_addr);

   pco_ref addr_data_comps[3] = {
      [2] = src,
   };
   pco_ref_new_ssa_addr_comps(tctx->func, addr_data_comps);

   component += range->offset;
   assert(component < 8);

   unsigned sr_index = component < 4 ? component + PCO_SR_TILED_ST_COMP0
                                     : component + PCO_SR_TILED_ST_COMP4 - 4;
   pco_ref tiled_offset = pco_ref_hwreg(sr_index, PCO_REG_CLASS_SPEC);

   pco_add64_32(&tctx->b,
                addr_data_comps[0],
                addr_data_comps[1],
                base_addr[0],
                base_addr[1],
                tiled_offset,
                pco_ref_null(),
                .olchk = tctx->olchk,
                .s = true);

   unsigned chans = pco_ref_get_chans(src);
   pco_ref addr_data = pco_ref_new_ssa_addr_data(tctx->func, chans);
   pco_vec(&tctx->b, addr_data, ARRAY_SIZE(addr_data_comps), addr_data_comps);

   pco_ref data_comp =
      pco_ref_new_ssa(tctx->func, pco_ref_get_bits(src), chans);
   pco_comp(&tctx->b, data_comp, addr_data, pco_ref_val16(2));

   pco_ref cov_mask = pco_ref_new_ssa32(tctx->func);
   pco_ref sample_id = pco_ref_hwreg(PCO_SR_SAMP_NUM, PCO_REG_CLASS_SPEC);
   pco_shift(&tctx->b,
             cov_mask,
             pco_one,
             sample_id,
             pco_ref_null(),
             .shiftop = PCO_SHIFTOP_LSL);

   return pco_st_tiled(&tctx->b,
                       data_comp,
                       pco_ref_imm8(PCO_DSIZE_32BIT),
                       pco_ref_drc(PCO_DRC_0),
                       pco_ref_imm8(chans),
                       addr_data,
                       cov_mask,
                       .olchk = tctx->olchk);
}

static pco_instr *trans_flush_tile_buffer(trans_ctx *tctx,
                                          nir_intrinsic_instr *intr,
                                          pco_ref src_addr_lo,
                                          pco_ref src_addr_hi)
{
   pco_ref addr_comps[2];
   pco_ref_new_ssa_addr_comps(tctx->func, addr_comps);

   pco_ref tiled_offset =
      pco_ref_hwreg(PCO_SR_TILED_LD_COMP0, PCO_REG_CLASS_SPEC);

   pco_add64_32(&tctx->b,
                addr_comps[0],
                addr_comps[1],
                src_addr_lo,
                src_addr_hi,
                tiled_offset,
                pco_ref_null(),
                .olchk = tctx->olchk,
                .s = true);

   pco_ref addr = pco_ref_new_ssa_addr(tctx->func);
   pco_vec(&tctx->b, addr, ARRAY_SIZE(addr_comps), addr_comps);

   unsigned idx_reg_num = 0;
   pco_ref idx_reg =
      pco_ref_hwreg_idx(idx_reg_num, idx_reg_num, PCO_REG_CLASS_INDEX);

   unsigned base = nir_intrinsic_base(intr);
   pco_movi32(&tctx->b, idx_reg, pco_ref_imm32(base));

   pco_ref burst_len = pco_ref_new_ssa32(tctx->func);
   unsigned range = nir_intrinsic_range(intr);
   assert(range <= 1024);
   if (range == 1024)
      range = 0;

   pco_movi32(&tctx->b, burst_len, pco_ref_imm32(range));

   pco_ref dest = pco_ref_hwreg(0, PCO_REG_CLASS_PIXOUT);
   dest = pco_ref_hwreg_idx_from(idx_reg_num, dest);

   return pco_ld_regbl(&tctx->b, dest, pco_ref_drc(PCO_DRC_0), burst_len, addr);
}

static unsigned fetch_resource_base_reg(const pco_common_data *common,
                                        unsigned desc_set,
                                        unsigned binding,
                                        unsigned elem,
                                        unsigned *stride,
                                        bool *is_img_smp)
{
   const pco_range *range;
   if (desc_set == PCO_POINT_SAMPLER && binding == PCO_POINT_SAMPLER) {
      assert(common->uses.point_sampler);
      range = &common->point_sampler;

      if (is_img_smp)
         *is_img_smp = false;
   } else if (desc_set == PCO_IA_SAMPLER && binding == PCO_IA_SAMPLER) {
      assert(common->uses.ia_sampler);
      range = &common->ia_sampler;

      if (is_img_smp)
         *is_img_smp = false;
   } else {
      assert(desc_set < ARRAY_SIZE(common->desc_sets));
      const pco_descriptor_set_data *desc_set_data =
         &common->desc_sets[desc_set];
      assert(desc_set_data->used);
      assert(desc_set_data->bindings && binding < desc_set_data->binding_count);

      const pco_binding_data *binding_data = &desc_set_data->bindings[binding];
      assert(binding_data->used);

      range = &binding_data->range;

      if (is_img_smp)
         *is_img_smp = binding_data->is_img_smp;
   }

   if (stride)
      *stride = range->stride;

   unsigned reg_offset = elem * range->stride;
   assert(reg_offset < range->count);

   unsigned reg_index = range->start + reg_offset;
   return reg_index;
}

static unsigned fetch_resource_base_reg_packed(const pco_common_data *common,
                                               uint32_t packed_desc,
                                               unsigned elem,
                                               unsigned *stride,
                                               bool *is_img_smp)
{
   unsigned desc_set;
   unsigned binding;
   pco_unpack_desc(packed_desc, &desc_set, &binding);

   return fetch_resource_base_reg(common,
                                  desc_set,
                                  binding,
                                  elem,
                                  stride,
                                  is_img_smp);
}

/**
 * \brief Translates a NIR fs load_output intrinsic into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] intr load_output intrinsic.
 * \return The translated PCO instruction.
 */
static pco_instr *
trans_load_output_fs(trans_ctx *tctx, nir_intrinsic_instr *intr, pco_ref dest)
{
   pco_fs_data *fs_data = &tctx->shader->data.fs;
   unsigned base = nir_intrinsic_base(intr);

   assert(pco_ref_is_scalar(dest));
   unsigned component = nir_intrinsic_component(intr);

   ASSERTED const nir_src offset = intr->src[0];
   assert(nir_src_as_uint(offset) == 0);

   gl_frag_result location = nir_intrinsic_io_semantics(intr).location;

   const pco_range *range;
   bool tile_buffer;
   if (location >= FRAG_RESULT_DATA0) {
      assert(!base);
      range = &tctx->shader->data.fs.outputs[location];
      unsigned idx = location - FRAG_RESULT_DATA0;
      tile_buffer = fs_data->output_tile_buffers & BITFIELD_BIT(idx);
   } else if (location == FRAG_RESULT_COLOR) {
      /* Special case for on-chip input attachments. */
      assert(base < ARRAY_SIZE(tctx->shader->data.fs.ias_onchip));
      range = &tctx->shader->data.fs.ias_onchip[base];
      tile_buffer = fs_data->ia_tile_buffers & BITFIELD_BIT(base);
   } else {
      UNREACHABLE("");
   }

   assert(component < range->count);

   if (!tile_buffer) {
      pco_ref src =
         pco_ref_hwreg(range->start + component, PCO_REG_CLASS_PIXOUT);
      return pco_mov(&tctx->b, dest, src, .olchk = tctx->olchk);
   }

   unsigned tile_buffer_id = range->start;
   pco_range *tile_buffers = &fs_data->tile_buffers;
   assert(tile_buffer_id < (tile_buffers->count / tile_buffers->stride));
   unsigned sh_index =
      tile_buffers->start + tile_buffer_id * tile_buffers->stride;

   pco_ref base_addr[2];
   pco_ref_hwreg_addr_comps(sh_index, PCO_REG_CLASS_SHARED, base_addr);

   pco_ref addr_comps[2];
   pco_ref_new_ssa_addr_comps(tctx->func, addr_comps);

   component += range->offset;
   assert(component < 8);

   unsigned sr_index = component < 4 ? component + PCO_SR_TILED_LD_COMP0
                                     : component + PCO_SR_TILED_LD_COMP4 - 4;
   pco_ref tiled_offset = pco_ref_hwreg(sr_index, PCO_REG_CLASS_SPEC);

   pco_add64_32(&tctx->b,
                addr_comps[0],
                addr_comps[1],
                base_addr[0],
                base_addr[1],
                tiled_offset,
                pco_ref_null(),
                .olchk = tctx->olchk,
                .s = true);

   pco_ref addr = pco_ref_new_ssa_addr(tctx->func);
   pco_vec(&tctx->b, addr, ARRAY_SIZE(addr_comps), addr_comps);

   unsigned chans = pco_ref_get_chans(dest);
   return pco_ld(&tctx->b,
                 dest,
                 pco_ref_drc(PCO_DRC_0),
                 pco_ref_imm8(chans),
                 addr);
}

static pco_instr *trans_load_common_store(trans_ctx *tctx,
                                          nir_intrinsic_instr *intr,
                                          pco_ref dest,
                                          pco_ref offset_src,
                                          bool coeffs,
                                          pco_range *range)
{
   nir_src *noffset_src = &intr->src[0];
   enum pco_reg_class reg_class = coeffs ? PCO_REG_CLASS_COEFF
                                         : PCO_REG_CLASS_SHARED;

   unsigned chans = pco_ref_get_chans(dest);
   ASSERTED unsigned bits = pco_ref_get_bits(dest);
   assert(bits == 32);

   assert(range->count > 0);

   if (pco_ref_is_null(offset_src) || nir_src_is_const(*noffset_src)) {
      unsigned offset =
         pco_ref_is_null(offset_src) ? 0 : nir_src_as_uint(*noffset_src);
      assert(offset < range->count);

      pco_ref src = pco_ref_hwreg_vec(range->start + offset, reg_class, chans);
      return pco_mbyp(&tctx->b, dest, src, .rpt = chans);
   }

   pco_ref src_base = pco_ref_hwreg_vec(range->start, reg_class, chans);
   return pco_mov_offset(&tctx->b,
                         dest,
                         src_base,
                         offset_src,
                         .offset_sd = PCO_OFFSET_SD_SRC,
                         .rpt = chans);
}

static pco_instr *trans_store_common_store(trans_ctx *tctx,
                                           nir_intrinsic_instr *intr,
                                           pco_ref data,
                                           pco_ref offset_src,
                                           bool coeffs,
                                           pco_range *range)
{
   nir_src *noffset_src = &intr->src[1];
   enum pco_reg_class reg_class = coeffs ? PCO_REG_CLASS_COEFF
                                         : PCO_REG_CLASS_SHARED;

   unsigned chans = pco_ref_get_chans(data);
   ASSERTED unsigned bits = pco_ref_get_bits(data);
   assert(bits == 32);

   assert(range->count > 0);

   if (nir_src_is_const(*noffset_src)) {
      unsigned offset = nir_src_as_uint(*noffset_src);
      assert(offset < range->count);

      pco_ref dest = pco_ref_hwreg_vec(range->start + offset, reg_class, chans);
      return pco_mbyp(&tctx->b, dest, data, .rpt = chans);
   }

   pco_ref dest_base = pco_ref_hwreg_vec(range->start, reg_class, chans);
   return pco_mov_offset(&tctx->b,
                         dest_base,
                         data,
                         offset_src,
                         .offset_sd = PCO_OFFSET_SD_DEST,
                         .rpt = chans);
}

static inline enum pco_atom_op to_atom_op(nir_atomic_op op)
{
   switch (op) {
   case nir_atomic_op_iadd:
      return PCO_ATOM_OP_ADD;

   case nir_atomic_op_xchg:
      return PCO_ATOM_OP_XCHG;

   case nir_atomic_op_cmpxchg:
      return PCO_ATOM_OP_CMPXCHG;

   case nir_atomic_op_umin:
      return PCO_ATOM_OP_UMIN;

   case nir_atomic_op_imin:
      return PCO_ATOM_OP_IMIN;

   case nir_atomic_op_umax:
      return PCO_ATOM_OP_UMAX;

   case nir_atomic_op_imax:
      return PCO_ATOM_OP_IMAX;

   case nir_atomic_op_iand:
      return PCO_ATOM_OP_AND;

   case nir_atomic_op_ior:
      return PCO_ATOM_OP_OR;

   case nir_atomic_op_ixor:
      return PCO_ATOM_OP_XOR;

   default:
      break;
   }

   UNREACHABLE("");
}

static pco_instr *trans_atomic_shared(trans_ctx *tctx,
                                      nir_intrinsic_instr *intr,
                                      pco_ref dest,
                                      pco_ref offset_src,
                                      pco_ref value,
                                      pco_ref value_swap)
{
   nir_src *noffset_src = &intr->src[0];

   unsigned chans = pco_ref_get_chans(dest);
   ASSERTED unsigned bits = pco_ref_get_bits(dest);

   assert(bits == 32);
   assert(chans == 1);

   assert(tctx->shader->data.cs.shmem.count > 0);

   pco_ref shmem_ref = pco_ref_hwreg_vec(tctx->shader->data.cs.shmem.start,
                                         PCO_REG_CLASS_COEFF,
                                         chans);

   bool const_offset = nir_src_is_const(*noffset_src);
   if (const_offset) {
      unsigned offset = nir_src_as_uint(*noffset_src);
      assert(offset < tctx->shader->data.cs.shmem.count);
      shmem_ref = pco_ref_offset(shmem_ref, offset);
   } else {
      enum pco_atom_op atom_op = to_atom_op(nir_intrinsic_atomic_op(intr));
      return pco_op_atomic_offset(&tctx->b,
                                  dest,
                                  shmem_ref,
                                  shmem_ref,
                                  value,
                                  value_swap,
                                  offset_src,
                                  .atom_op = atom_op,
                                  .rpt = chans);
   }

   pco_instr *instr;
   switch (nir_intrinsic_atomic_op(intr)) {
   case nir_atomic_op_iadd:
      instr = pco_iadd32_atomic(&tctx->b,
                                dest,
                                shmem_ref,
                                shmem_ref,
                                value,
                                pco_ref_null(),
                                .s = true);
      break;

   case nir_atomic_op_xchg:
      instr = pco_xchg_atomic(&tctx->b, dest, shmem_ref, shmem_ref, value);
      break;

   case nir_atomic_op_umin:
      instr = pco_min_atomic(&tctx->b,
                             dest,
                             shmem_ref,
                             shmem_ref,
                             value,
                             .tst_type_main = PCO_TST_TYPE_MAIN_U32);
      break;

   case nir_atomic_op_imin:
      instr = pco_min_atomic(&tctx->b,
                             dest,
                             shmem_ref,
                             shmem_ref,
                             value,
                             .tst_type_main = PCO_TST_TYPE_MAIN_S32);
      break;

   case nir_atomic_op_umax:
      instr = pco_max_atomic(&tctx->b,
                             dest,
                             shmem_ref,
                             shmem_ref,
                             value,
                             .tst_type_main = PCO_TST_TYPE_MAIN_U32);
      break;

   case nir_atomic_op_imax:
      instr = pco_max_atomic(&tctx->b,
                             dest,
                             shmem_ref,
                             shmem_ref,
                             value,
                             .tst_type_main = PCO_TST_TYPE_MAIN_S32);
      break;

   case nir_atomic_op_iand:
      instr = pco_logical_atomic(&tctx->b,
                                 dest,
                                 shmem_ref,
                                 shmem_ref,
                                 value,
                                 .logiop = PCO_LOGIOP_AND);
      break;

   case nir_atomic_op_ior:
      instr = pco_logical_atomic(&tctx->b,
                                 dest,
                                 shmem_ref,
                                 shmem_ref,
                                 value,
                                 .logiop = PCO_LOGIOP_OR);
      break;

   case nir_atomic_op_ixor:
      instr = pco_logical_atomic(&tctx->b,
                                 dest,
                                 shmem_ref,
                                 shmem_ref,
                                 value,
                                 .logiop = PCO_LOGIOP_XOR);
      break;

   case nir_atomic_op_cmpxchg:
      instr = pco_cmpxchg_atomic(&tctx->b,
                                 dest,
                                 shmem_ref,
                                 shmem_ref,
                                 value,
                                 value_swap,
                                 .tst_type_main = PCO_TST_TYPE_MAIN_U32);
      break;

   default:
      UNREACHABLE("");
   }

   pco_instr_set_rpt(instr, chans);

   return instr;
}

static pco_instr *trans_load_buffer(trans_ctx *tctx,
                                    nir_intrinsic_instr *intr,
                                    pco_ref dest,
                                    pco_ref offset_src)
{
   const pco_common_data *common = &tctx->shader->data.common;

   unsigned chans = pco_ref_get_chans(dest);
   ASSERTED unsigned bits = pco_ref_get_bits(dest);
   assert(bits == 32);

   uint32_t packed_desc = nir_src_comp_as_uint(intr->src[0], 0);
   unsigned elem = nir_src_comp_as_uint(intr->src[0], 1);
   unsigned sh_index =
      fetch_resource_base_reg_packed(common, packed_desc, elem, NULL, NULL);

   pco_ref base_addr[2];
   pco_ref_hwreg_addr_comps(sh_index, PCO_REG_CLASS_SHARED, base_addr);

   pco_ref addr_comps[2];
   pco_ref_new_ssa_addr_comps(tctx->func, addr_comps);

   pco_add64_32(&tctx->b,
                addr_comps[0],
                addr_comps[1],
                base_addr[0],
                base_addr[1],
                offset_src,
                pco_ref_null(),
                .s = true);

   pco_ref addr_comps_dyn_off[2];
   pco_ref_new_ssa_addr_comps(tctx->func, addr_comps_dyn_off);

   pco_ref dyn_off_reg = pco_ref_hwreg(sh_index, PCO_REG_CLASS_SHARED);
   dyn_off_reg = pco_ref_offset(dyn_off_reg, 3);

   pco_add64_32(&tctx->b,
                addr_comps_dyn_off[0],
                addr_comps_dyn_off[1],
                addr_comps[0],
                addr_comps[1],
                dyn_off_reg,
                pco_ref_null(),
                .s = true);

   pco_ref addr = pco_ref_new_ssa_addr(tctx->func);
   pco_vec(&tctx->b, addr, ARRAY_SIZE(addr_comps_dyn_off), addr_comps_dyn_off);

   return pco_ld(&tctx->b,
                 dest,
                 pco_ref_drc(PCO_DRC_0),
                 pco_ref_imm8(chans),
                 addr);
}

static pco_instr *trans_load_global(trans_ctx *tctx,
                                    nir_intrinsic_instr *intr,
                                    pco_ref dest,
                                    pco_ref addr)
{
   unsigned chans = pco_ref_get_chans(dest);
   ASSERTED unsigned bits = pco_ref_get_bits(dest);
   assert(bits == 32);

   return pco_ld(&tctx->b,
                 dest,
                 pco_ref_drc(PCO_DRC_0),
                 pco_ref_imm8(chans),
                 addr);
}

static pco_instr *
trans_store_global(trans_ctx *tctx, pco_ref data_src, pco_ref addr_src)
{
   unsigned chans = pco_ref_get_chans(data_src);
   unsigned bits = pco_ref_get_bits(data_src);

   pco_ref addr_data_comps[3] = {
      [2] = data_src,
   };

   pco_ref_new_ssa_addr_comps(tctx->func, addr_data_comps);
   pco_comp(&tctx->b, addr_data_comps[0], addr_src, pco_ref_val16(0));
   pco_comp(&tctx->b, addr_data_comps[1], addr_src, pco_ref_val16(1));

   pco_ref addr_data = pco_ref_new_ssa_addr_data(tctx->func, chans);
   pco_vec(&tctx->b, addr_data, ARRAY_SIZE(addr_data_comps), addr_data_comps);

   pco_ref data_comp = pco_ref_new_ssa(tctx->func, bits, chans);
   pco_comp(&tctx->b, data_comp, addr_data, pco_ref_val16(2));

   switch (bits) {
   case 32:
      return pco_st32(&tctx->b,
                      data_comp,
                      pco_ref_drc(PCO_DRC_0),
                      pco_ref_imm8(chans),
                      addr_data,
                      pco_ref_null());

   default:
      break;
   }

   UNREACHABLE("");
}

static pco_instr *
trans_get_buffer_size(trans_ctx *tctx, nir_intrinsic_instr *intr, pco_ref dest)
{
   const pco_common_data *common = &tctx->shader->data.common;

   ASSERTED unsigned chans = pco_ref_get_chans(dest);
   ASSERTED unsigned bits = pco_ref_get_bits(dest);

   assert(chans == 1);
   assert(bits == 32);

   uint32_t packed_desc = nir_src_comp_as_uint(intr->src[0], 0);
   unsigned elem = nir_src_comp_as_uint(intr->src[0], 1);
   unsigned sh_index =
      fetch_resource_base_reg_packed(common, packed_desc, elem, NULL, NULL);

   pco_ref size_reg = pco_ref_hwreg(sh_index, PCO_REG_CLASS_SHARED);
   size_reg = pco_ref_offset(size_reg, 2);

   return pco_mov(&tctx->b, dest, size_reg);
}

static pco_instr *trans_store_buffer(trans_ctx *tctx,
                                     nir_intrinsic_instr *intr,
                                     pco_ref data_src,
                                     pco_ref offset_src)
{
   const pco_common_data *common = &tctx->shader->data.common;

   unsigned chans = pco_ref_get_chans(data_src);
   unsigned bits = pco_ref_get_bits(data_src);

   uint32_t packed_desc = nir_src_comp_as_uint(intr->src[1], 0);
   unsigned elem = nir_src_comp_as_uint(intr->src[1], 1);
   unsigned sh_index =
      fetch_resource_base_reg_packed(common, packed_desc, elem, NULL, NULL);

   pco_ref base_addr[2];
   pco_ref_hwreg_addr_comps(sh_index, PCO_REG_CLASS_SHARED, base_addr);

   pco_ref addr_comps[2];
   pco_ref_new_ssa_addr_comps(tctx->func, addr_comps);

   pco_add64_32(&tctx->b,
                addr_comps[0],
                addr_comps[1],
                base_addr[0],
                base_addr[1],
                offset_src,
                pco_ref_null(),
                .s = true);

   pco_ref addr_data_comps_dyn_off[3] = {
      [2] = data_src,
   };
   pco_ref_new_ssa_addr_comps(tctx->func, addr_data_comps_dyn_off);

   pco_ref dyn_off_reg = pco_ref_hwreg(sh_index, PCO_REG_CLASS_SHARED);
   dyn_off_reg = pco_ref_offset(dyn_off_reg, 3);

   pco_add64_32(&tctx->b,
                addr_data_comps_dyn_off[0],
                addr_data_comps_dyn_off[1],
                addr_comps[0],
                addr_comps[1],
                dyn_off_reg,
                pco_ref_null(),
                .s = true);

   pco_ref addr_data = pco_ref_new_ssa_addr_data(tctx->func, chans);
   pco_vec(&tctx->b,
           addr_data,
           ARRAY_SIZE(addr_data_comps_dyn_off),
           addr_data_comps_dyn_off);

   pco_ref data_comp = pco_ref_new_ssa(tctx->func,
                                       pco_ref_get_bits(data_src),
                                       pco_ref_get_chans(data_src));
   pco_comp(&tctx->b, data_comp, addr_data, pco_ref_val16(2));

   switch (bits) {
   case 32:
      return pco_st32(&tctx->b,
                      data_comp,
                      pco_ref_drc(PCO_DRC_0),
                      pco_ref_imm8(chans),
                      addr_data,
                      pco_ref_null());

   default:
      break;
   }

   UNREACHABLE("");
}

static pco_instr *trans_atomic_buffer(trans_ctx *tctx,
                                      nir_intrinsic_instr *intr,
                                      pco_ref dest,
                                      pco_ref offset_src,
                                      pco_ref data_src)
{
   const pco_common_data *common = &tctx->shader->data.common;

   enum pco_atom_op atom_op = to_atom_op(nir_intrinsic_atomic_op(intr));
   /* Should have been lowered. */
   assert(atom_op != PCO_ATOM_OP_CMPXCHG);

   unsigned chans = pco_ref_get_chans(dest);
   unsigned bits = pco_ref_get_bits(dest);

   uint32_t packed_desc = nir_src_comp_as_uint(intr->src[0], 0);
   unsigned elem = nir_src_comp_as_uint(intr->src[0], 1);
   unsigned sh_index =
      fetch_resource_base_reg_packed(common, packed_desc, elem, NULL, NULL);

   pco_ref base_addr[2];
   pco_ref_hwreg_addr_comps(sh_index, PCO_REG_CLASS_SHARED, base_addr);

   pco_ref addr_comps[2];
   pco_ref_new_ssa_addr_comps(tctx->func, addr_comps);

   pco_add64_32(&tctx->b,
                addr_comps[0],
                addr_comps[1],
                base_addr[0],
                base_addr[1],
                offset_src,
                pco_ref_null(),
                .s = true);

   pco_ref addr_data_comps_dyn_off[3] = {
      [2] = data_src,
   };
   pco_ref_new_ssa_addr_comps(tctx->func, addr_data_comps_dyn_off);

   pco_ref dyn_off_reg = pco_ref_hwreg(sh_index, PCO_REG_CLASS_SHARED);
   dyn_off_reg = pco_ref_offset(dyn_off_reg, 3);

   pco_add64_32(&tctx->b,
                addr_data_comps_dyn_off[0],
                addr_data_comps_dyn_off[1],
                addr_comps[0],
                addr_comps[1],
                dyn_off_reg,
                pco_ref_null(),
                .s = true);

   pco_ref addr_data = pco_ref_new_ssa_addr_data(tctx->func, chans);
   pco_vec(&tctx->b,
           addr_data,
           ARRAY_SIZE(addr_data_comps_dyn_off),
           addr_data_comps_dyn_off);

   switch (bits) {
   case 32:
      return pco_atomic(&tctx->b,
                        dest,
                        pco_ref_drc(PCO_DRC_0),
                        addr_data,
                        .atom_op = atom_op);

   default:
      break;
   }

   UNREACHABLE("");
}

static pco_instr *trans_global_atomic_buffer(trans_ctx *tctx,
                                             nir_intrinsic_instr *intr,
                                             pco_ref dest,
                                             pco_ref addr_data)
{
   enum pco_atom_op atom_op = to_atom_op(nir_intrinsic_atomic_op(intr));
   /* Should have been lowered. */
   assert(atom_op != PCO_ATOM_OP_CMPXCHG);

   ASSERTED unsigned chans = pco_ref_get_chans(dest);
   unsigned bits = pco_ref_get_bits(dest);

   assert(chans == 1);

   switch (bits) {
   case 32:
      return pco_atomic(&tctx->b,
                        dest,
                        pco_ref_drc(PCO_DRC_0),
                        addr_data,
                        .atom_op = atom_op);

   default:
      break;
   }

   UNREACHABLE("");
}

static pco_instr *trans_scratch(trans_ctx *tctx,
                                pco_ref dest,
                                pco_ref offset_src,
                                pco_ref data_src)
{
   const pco_common_data *common = &tctx->shader->data.common;

   assert(common->scratch_info.count > 0);
   unsigned base_addr_idx = common->scratch_info.start;
   unsigned block_size_idx = common->scratch_info.start + 2;

   pco_ref base_addr[2];
   pco_ref_hwreg_addr_comps(base_addr_idx, PCO_REG_CLASS_SHARED, base_addr);

   pco_ref block_size = pco_ref_hwreg(block_size_idx, PCO_REG_CLASS_SHARED);
   pco_ref local_addr_inst_num =
      pco_ref_hwreg(PCO_SR_LOCAL_ADDR_INST_NUM, PCO_REG_CLASS_SPEC);

   pco_ref inst_base_addr[2];
   pco_ref_new_ssa_addr_comps(tctx->func, inst_base_addr);

   pco_imadd64(&tctx->b,
               inst_base_addr[0],
               inst_base_addr[1],
               block_size,
               local_addr_inst_num,
               base_addr[0],
               base_addr[1],
               pco_ref_null());

   pco_ref addr_data_comps[3];
   pco_ref_new_ssa_addr_comps(tctx->func, addr_data_comps);

   pco_add64_32(&tctx->b,
                addr_data_comps[0],
                addr_data_comps[1],
                inst_base_addr[0],
                inst_base_addr[1],
                offset_src,
                pco_ref_null(),
                .s = true);

   bool is_load = pco_ref_is_null(data_src);
   if (is_load) {
      ASSERTED unsigned bits = pco_ref_get_bits(dest);

      /* TODO: 8/16-bit support via masking. */
      assert(bits == 32);

      pco_ref addr = pco_ref_new_ssa_addr(tctx->func);
      pco_vec(&tctx->b, addr, 2, addr_data_comps);

      unsigned chans = pco_ref_get_chans(dest);
      return pco_ld(&tctx->b,
                    dest,
                    pco_ref_drc(PCO_DRC_0),
                    pco_ref_imm8(chans),
                    addr);
   }

   unsigned chans = pco_ref_get_chans(data_src);
   pco_ref addr_data = pco_ref_new_ssa_addr_data(tctx->func, chans);

   addr_data_comps[2] = data_src;
   pco_vec(&tctx->b, addr_data, ARRAY_SIZE(addr_data_comps), addr_data_comps);

   pco_ref data_comp = pco_ref_new_ssa(tctx->func,
                                       pco_ref_get_bits(data_src),
                                       pco_ref_get_chans(data_src));
   pco_comp(&tctx->b, data_comp, addr_data, pco_ref_val16(2));

   return pco_st32(&tctx->b,
                   data_comp,
                   pco_ref_drc(PCO_DRC_0),
                   pco_ref_imm8(chans),
                   addr_data,
                   pco_ref_null());
}

static inline enum pco_reg_class sys_val_to_reg_class(gl_system_value sys_val,
                                                      mesa_shader_stage stage)
{
   switch (stage) {
   case MESA_SHADER_FRAGMENT:
      return PCO_REG_CLASS_SHARED;

   case MESA_SHADER_VERTEX:
      return PCO_REG_CLASS_VTXIN;

   case MESA_SHADER_COMPUTE:
      switch (sys_val) {
      case SYSTEM_VALUE_LOCAL_INVOCATION_INDEX:
         return PCO_REG_CLASS_VTXIN;

      case SYSTEM_VALUE_WORKGROUP_ID:
      case SYSTEM_VALUE_NUM_WORKGROUPS:
         return PCO_REG_CLASS_COEFF;

      default:
         break;
      }
      break;

   default:
      break;
   }

   UNREACHABLE("");
}

/**
 * \brief Translates a NIR load system value intrinsic into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] intr System value intrinsic.
 * \param[in] dest Instruction destination.
 * \return The translated PCO instruction.
 */
static pco_instr *
trans_load_sysval(trans_ctx *tctx, nir_intrinsic_instr *intr, pco_ref dest)
{
   gl_system_value sys_val;
   /* Overrides. */
   switch (intr->intrinsic) {
   case nir_intrinsic_load_front_face_op_pco:
      sys_val = SYSTEM_VALUE_FRONT_FACE;
      break;

   default:
      sys_val = nir_system_value_from_intrinsic(intr->intrinsic);
      break;
   }

   const pco_range *range = &tctx->shader->data.common.sys_vals[sys_val];

   unsigned chans = pco_ref_get_chans(dest);
   assert(chans == range->count);

   pco_ref src = pco_ref_hwreg_vec(range->start,
                                   sys_val_to_reg_class(sys_val, tctx->stage),
                                   chans);
   return pco_mov(&tctx->b, dest, src, .rpt = chans);
}

static pco_instr *trans_load_packed_sample_location(trans_ctx *tctx,
                                                    nir_intrinsic_instr *intr,
                                                    pco_ref dest,
                                                    pco_ref offset_src)
{
   unsigned idx_reg_num = 0;
   pco_ref idx_reg =
      pco_ref_hwreg_idx(idx_reg_num, idx_reg_num, PCO_REG_CLASS_INDEX);

   pco_mov(&tctx->b, idx_reg, offset_src);

   const pco_range *range = &tctx->shader->data.fs.sample_locations;
   assert(range->count > 0);

   /* TODO: add the start onto the offset src instead? */
   assert(range->start < 256);

   pco_ref src = pco_ref_hwreg(range->start, PCO_REG_CLASS_SHARED);
   src = pco_ref_hwreg_idx_from(idx_reg_num, src);

   return pco_mov(&tctx->b, dest, src);
}

static bool desc_set_binding_is_comb_img_smp(unsigned desc_set,
                                             unsigned binding,
                                             const pco_common_data *common)
{
   const pco_descriptor_set_data *desc_set_data = &common->desc_sets[desc_set];
   assert(desc_set_data->used);
   assert(desc_set_data->bindings && binding < desc_set_data->binding_count);

   const pco_binding_data *binding_data = &desc_set_data->bindings[binding];
   assert(binding_data->used);

   return binding_data->is_img_smp;
}

static pco_instr *lower_load_tex_smp_state(trans_ctx *tctx,
                                           nir_intrinsic_instr *intr,
                                           pco_ref dest,
                                           bool smp)
{
   unsigned desc_set = nir_intrinsic_desc_set(intr);
   unsigned binding = nir_intrinsic_binding(intr);
   unsigned start_comp = nir_intrinsic_component(intr);
   unsigned chans = pco_ref_get_chans(dest);
   assert(start_comp + chans <= ROGUE_NUM_TEXSTATE_DWORDS);
   unsigned elem = nir_src_as_uint(intr->src[0]);

   const pco_common_data *common = &tctx->shader->data.common;
   bool is_img_smp;
   unsigned sh_index = fetch_resource_base_reg(common,
                                               desc_set,
                                               binding,
                                               elem,
                                               NULL,
                                               &is_img_smp);
   pco_ref state_words =
      pco_ref_hwreg_vec(sh_index, PCO_REG_CLASS_SHARED, chans);

   /* Sampler state comes after image state and metadata in combined image
    * samplers.
    */
   if (smp && is_img_smp) {
      state_words = pco_ref_offset(state_words, ROGUE_NUM_TEXSTATE_DWORDS);
      state_words = pco_ref_offset(state_words, PCO_IMAGE_META_COUNT);
   }

   /* Gather sampler words come after standard sampler state and metadata. */
   if (smp && nir_intrinsic_flags(intr)) {
      state_words = pco_ref_offset(state_words, ROGUE_NUM_TEXSTATE_DWORDS);
      state_words = pco_ref_offset(state_words, PCO_SAMPLER_META_COUNT);
   }

   state_words = pco_ref_offset(state_words, start_comp);

   return pco_mov(&tctx->b, dest, state_words, .rpt = chans);
}

static pco_instr *lower_load_tex_smp_meta(trans_ctx *tctx,
                                          nir_intrinsic_instr *intr,
                                          pco_ref dest,
                                          bool smp)
{
   unsigned desc_set = nir_intrinsic_desc_set(intr);
   unsigned binding = nir_intrinsic_binding(intr);
   unsigned start_comp = nir_intrinsic_component(intr);
   unsigned chans = pco_ref_get_chans(dest);
   unsigned elem = nir_src_as_uint(intr->src[0]);

   const pco_common_data *common = &tctx->shader->data.common;
   bool is_img_smp;
   unsigned sh_index = fetch_resource_base_reg(common,
                                               desc_set,
                                               binding,
                                               elem,
                                               NULL,
                                               &is_img_smp);
   pco_ref state_words =
      pco_ref_hwreg_vec(sh_index, PCO_REG_CLASS_SHARED, chans);

   assert(start_comp + chans <=
          (smp ? PCO_SAMPLER_META_COUNT : PCO_IMAGE_META_COUNT));

   state_words = pco_ref_offset(state_words, ROGUE_NUM_TEXSTATE_DWORDS);

   if (smp && is_img_smp) {
      state_words = pco_ref_offset(state_words, PCO_IMAGE_META_COUNT);
      state_words = pco_ref_offset(state_words, ROGUE_NUM_TEXSTATE_DWORDS);
   }

   state_words = pco_ref_offset(state_words, start_comp);

   return pco_mov(&tctx->b, dest, state_words, .rpt = chans);
}

static pco_instr *lower_smp(trans_ctx *tctx,
                            nir_intrinsic_instr *intr,
                            pco_ref *dest,
                            pco_ref data,
                            pco_ref tex_state,
                            pco_ref smp_state)
{
   pco_smp_flags smp_flags = { ._ = nir_intrinsic_smp_flags_pco(intr) };
   unsigned data_comps = nir_intrinsic_range(intr);

   data = pco_ref_chans(data, data_comps);

   unsigned chans = pco_ref_get_chans(*dest);
   enum pco_sb_mode sb_mode = PCO_SB_MODE_NONE;
   switch (intr->intrinsic) {
   case nir_intrinsic_smp_coeffs_pco:
      /* Shrink the destination to its actual size. */
      *dest = pco_ref_chans(*dest, ROGUE_SMP_COEFF_COUNT);
      chans = 1; /* Chans must be 1 for coeff mode. */

      sb_mode = PCO_SB_MODE_COEFFS;
      break;

   case nir_intrinsic_smp_raw_pco:
      chans = 4;
      sb_mode = PCO_SB_MODE_RAWDATA;
      break;

   case nir_intrinsic_smp_pco:
      /* Destination and chans should be correct. */
      break;

   case nir_intrinsic_smp_write_pco:
      chans = 4;
      break;

   default:
      UNREACHABLE("");
   }

   pco_ref shared_lod = pco_ref_null();

   pco_instr *smp = pco_smp(&tctx->b,
                            *dest,
                            pco_ref_drc(PCO_DRC_0),
                            tex_state,
                            data,
                            smp_state,
                            shared_lod,
                            pco_ref_imm8(chans),
                            .dim = smp_flags.dim,
                            .proj = smp_flags.proj,
                            .fcnorm = smp_flags.fcnorm,
                            .nncoords = smp_flags.nncoords,
                            .lod_mode = smp_flags.lod_mode,
                            .pplod = smp_flags.pplod,
                            .tao = smp_flags.tao,
                            .soo = smp_flags.soo,
                            .sno = smp_flags.sno,
                            .sb_mode = sb_mode,
                            .array = smp_flags.array,
                            .integer = smp_flags.integer,
                            .wrt = smp_flags.wrt);

   return smp;
}

static pco_instr *lower_alphatst(trans_ctx *tctx,
                                 pco_ref dest,
                                 pco_ref src0,
                                 pco_ref src1,
                                 pco_ref src2)
{
   pco_alphatst(&tctx->b,
                pco_ref_pred(PCO_PRED_P0),
                pco_ref_drc(PCO_DRC_0),
                src0,
                src1,
                src2);

   return pco_psel(&tctx->b,
                   dest,
                   pco_ref_pred(PCO_PRED_P0),
                   pco_fone,
                   pco_zero);
}

static inline unsigned lookup_reg_bits(nir_intrinsic_instr *intr)
{
   nir_def *reg;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_reg:
      reg = intr->src[0].ssa;
      break;

   case nir_intrinsic_store_reg:
      reg = intr->src[1].ssa;
      break;

   default:
      UNREACHABLE("");
   }

   nir_intrinsic_instr *reg_decl = nir_reg_get_decl(reg);
   return nir_intrinsic_bit_size(reg_decl);
}

static pco_instr *trans_reg_intr(trans_ctx *tctx,
                                 nir_intrinsic_instr *intr,
                                 pco_ref dest,
                                 pco_ref src0,
                                 pco_ref src1)
{
   pco_func *func = tctx->func;

   /* Special case; consume and reserve. */
   if (intr->intrinsic == nir_intrinsic_decl_reg) {
      assert(nir_intrinsic_num_components(intr) == 1);
      assert(!nir_intrinsic_num_array_elems(intr));

      /* nir_trivialize_registers puts decl_regs into the start block. */
      assert(func->next_vreg == dest.val);
      ++func->next_vreg;

      return NULL;
   }

   unsigned reg_bits = lookup_reg_bits(intr);
   switch (intr->intrinsic) {
   case nir_intrinsic_load_reg:
      assert(!nir_intrinsic_base(intr));
      assert(!nir_intrinsic_legacy_fabs(intr));
      assert(!nir_intrinsic_legacy_fneg(intr));

      return pco_mbyp(&tctx->b, dest, pco_ref_ssa_vreg(func, src0, reg_bits));

   case nir_intrinsic_store_reg:
      assert(!nir_intrinsic_base(intr));
      assert(nir_intrinsic_write_mask(intr) == 1);
      assert(!nir_intrinsic_legacy_fsat(intr));

      return pco_mbyp(&tctx->b, pco_ref_ssa_vreg(func, src1, reg_bits), src0);

   default:
      break;
   }

   UNREACHABLE("");
}

static enum pco_pck_fmt pco_pck_format_from_pipe_format(enum pipe_format fmt)
{
   const struct util_format_description *desc = util_format_description(fmt);
   int c = util_format_get_largest_non_void_channel(fmt);
   assert(c >= 0);
   const struct util_format_channel_description *chan = &desc->channel[c];

   switch (chan->size) {
   case 8:
      if (chan->type == UTIL_FORMAT_TYPE_UNSIGNED)
         return PCO_PCK_FMT_U8888;
      else if (chan->type == UTIL_FORMAT_TYPE_SIGNED)
         return PCO_PCK_FMT_S8888;
      break;

   case 10:
      if (chan->type == UTIL_FORMAT_TYPE_UNSIGNED)
         return PCO_PCK_FMT_U1010102;
      else if (chan->type == UTIL_FORMAT_TYPE_SIGNED)
         return PCO_PCK_FMT_S1010102;
      break;

   case 11:
      if (chan->type == UTIL_FORMAT_TYPE_FLOAT)
         return PCO_PCK_FMT_F111110;
      break;

   case 16:
      if (chan->type == UTIL_FORMAT_TYPE_UNSIGNED)
         return PCO_PCK_FMT_U1616;
      else if (chan->type == UTIL_FORMAT_TYPE_SIGNED)
         return PCO_PCK_FMT_S1616;
      else if (chan->type == UTIL_FORMAT_TYPE_FLOAT)
         return PCO_PCK_FMT_F16F16;
      break;

   default:
      break;
   }

   UNREACHABLE("Unsupported format.");
}

/**
 * \brief Translates a NIR intrinsic instruction into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] intr The nir intrinsic instruction.
 * \return The PCO instruction.
 */
static pco_instr *trans_intr(trans_ctx *tctx, nir_intrinsic_instr *intr)
{
   const nir_intrinsic_info *info = &nir_intrinsic_infos[intr->intrinsic];

   pco_ref dest = info->has_dest ? pco_ref_nir_def_t(&intr->def, tctx)
                                 : pco_ref_null();

   pco_ref src[NIR_MAX_VEC_COMPONENTS] = { 0 };
   for (unsigned s = 0; s < info->num_srcs; ++s)
      src[s] = pco_ref_nir_src_t(&intr->src[s], tctx);

   pco_instr *instr;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_input:
      if (tctx->stage == MESA_SHADER_VERTEX)
         instr = trans_load_input_vs(tctx, intr, dest);
      else if (tctx->stage == MESA_SHADER_FRAGMENT)
         instr = trans_load_input_fs(tctx, intr, dest, false);
      else
         UNREACHABLE("Unsupported stage for \"nir_intrinsic_load_input\".");
      break;

   case nir_intrinsic_load_interpolated_input:
      assert(tctx->stage == MESA_SHADER_FRAGMENT);
      instr = trans_load_input_fs(tctx, intr, dest, true);
      break;

   /* Just consume. */
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_centroid:
      return NULL;

   case nir_intrinsic_store_output:
      if (tctx->stage == MESA_SHADER_VERTEX)
         instr = trans_store_output_vs(tctx, intr, src[0]);
      else if (tctx->stage == MESA_SHADER_FRAGMENT)
         instr = trans_store_output_fs(tctx, intr, src[0]);
      else
         UNREACHABLE("Unsupported stage for \"nir_intrinsic_store_output\".");
      break;

   case nir_intrinsic_load_fs_coeffs_pco:
      instr = trans_load_fs_coeffs(tctx, intr, dest);
      break;

   case nir_intrinsic_uvsw_write_pco:
      instr = trans_uvsw_write(tctx, intr, src[0], src[1]);
      break;

   case nir_intrinsic_load_vtxin_pco:
      instr = trans_load_reg(tctx, intr, dest, src[0], PCO_REG_CLASS_VTXIN);
      break;

   case nir_intrinsic_load_coeff_pco:
      instr = trans_load_reg(tctx, intr, dest, src[0], PCO_REG_CLASS_COEFF);
      break;

   case nir_intrinsic_load_tiled_offset_pco:
      instr = trans_load_tiled_offset(tctx, intr, dest);
      break;

   case nir_intrinsic_frag_store_pco: {
      unsigned base = nir_intrinsic_base(intr);

      pco_ref dest = pco_ref_hwreg(base, PCO_REG_CLASS_PIXOUT);
      instr = pco_mov(&tctx->b, dest, src[0], .olchk = tctx->olchk);
      break;
   }

   case nir_intrinsic_load_output:
      assert(tctx->stage == MESA_SHADER_FRAGMENT);
      instr = trans_load_output_fs(tctx, intr, dest);
      break;

   case nir_intrinsic_dummy_load_store_pco: {
      assert(tctx->stage == MESA_SHADER_FRAGMENT);
      pco_ref pixout =
         pco_ref_hwreg(nir_intrinsic_base(intr), PCO_REG_CLASS_PIXOUT);
      return pco_mov(&tctx->b, pixout, pixout, .olchk = tctx->olchk);
      break;
   }

   case nir_intrinsic_flush_tile_buffer_pco:
      assert(tctx->stage == MESA_SHADER_FRAGMENT);
      instr = trans_flush_tile_buffer(tctx, intr, src[0], src[1]);
      break;

   case nir_intrinsic_load_preamble:
      instr = pco_mov(&tctx->b,
                      dest,
                      pco_ref_hwreg_vec(nir_intrinsic_base(intr),
                                        PCO_REG_CLASS_SHARED,
                                        pco_ref_get_chans(dest)),
                      .rpt = pco_ref_get_chans(dest));
      break;

   case nir_intrinsic_store_preamble:
      instr = pco_mov(&tctx->b,
                      pco_ref_hwreg_vec(nir_intrinsic_base(intr),
                                        PCO_REG_CLASS_SHARED,
                                        pco_ref_get_chans(src[0])),
                      src[0],
                      .rpt = pco_ref_get_chans(src[0]));
      break;

   case nir_intrinsic_store_preamble_dynamic: {
      unsigned idx_reg_num = 0;
      pco_ref idx_reg =
         pco_ref_hwreg_idx(idx_reg_num, idx_reg_num, PCO_REG_CLASS_INDEX);

      pco_mov(&tctx->b, idx_reg, src[1]);

      pco_ref dest = pco_ref_hwreg(0, PCO_REG_CLASS_SHARED);
      dest = pco_ref_hwreg_idx_from(idx_reg_num, dest);

      instr = pco_mov(&tctx->b, dest, src[0], .rpt = pco_ref_get_chans(src[0]));
      break;
   }

   case nir_intrinsic_load_push_constant:
      instr =
         trans_load_common_store(tctx,
                                 intr,
                                 dest,
                                 src[0],
                                 false,
                                 &tctx->shader->data.common.push_consts.range);
      break;

   case nir_intrinsic_load_blend_const_color_rgba:
      assert(tctx->stage == MESA_SHADER_FRAGMENT);
      instr = trans_load_common_store(tctx,
                                      intr,
                                      dest,
                                      pco_ref_null(),
                                      false,
                                      &tctx->shader->data.fs.blend_consts);
      break;

   case nir_intrinsic_load_packed_sample_location_pco:
      assert(tctx->stage == MESA_SHADER_FRAGMENT);
      instr = trans_load_packed_sample_location(tctx, intr, dest, src[0]);
      break;

   case nir_intrinsic_load_shared:
      assert(tctx->stage == MESA_SHADER_COMPUTE);
      instr = trans_load_common_store(tctx,
                                      intr,
                                      dest,
                                      src[0],
                                      true,
                                      &tctx->shader->data.cs.shmem);
      break;

   case nir_intrinsic_store_shared:
      assert(tctx->stage == MESA_SHADER_COMPUTE);
      instr = trans_store_common_store(tctx,
                                       intr,
                                       src[0],
                                       src[1],
                                       true,
                                       &tctx->shader->data.cs.shmem);
      break;

   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
      instr = trans_atomic_shared(tctx, intr, dest, src[0], src[1], src[2]);
      break;

   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
      instr = trans_load_buffer(tctx, intr, dest, src[1]);
      break;

   case nir_intrinsic_load_global_2x32:
      instr = trans_load_global(tctx, intr, dest, src[0]);
      break;

   case nir_intrinsic_store_global_2x32:
      instr = trans_store_global(tctx, src[0], src[1]);
      break;

   case nir_intrinsic_get_ubo_size:
   case nir_intrinsic_get_ssbo_size:
      instr = trans_get_buffer_size(tctx, intr, dest);
      break;

   case nir_intrinsic_store_ssbo:
      instr = trans_store_buffer(tctx, intr, src[0], src[2]);
      break;

   case nir_intrinsic_ssbo_atomic:
      instr = trans_atomic_buffer(tctx, intr, dest, src[1], src[2]);
      break;

   case nir_intrinsic_global_atomic_pco:
      instr = trans_global_atomic_buffer(tctx, intr, dest, src[0]);
      break;

   case nir_intrinsic_is_null_descriptor: {
      const pco_common_data *common = &tctx->shader->data.common;
      uint32_t packed_desc = nir_src_comp_as_uint(intr->src[0], 0);
      unsigned elem = nir_src_comp_as_uint(intr->src[0], 1);
      unsigned stride;
      bool is_img_smp;
      unsigned sh_index = fetch_resource_base_reg_packed(common,
                                                         packed_desc,
                                                         elem,
                                                         &stride,
                                                         &is_img_smp);

      if (is_img_smp)
         stride = ROGUE_NUM_TEXSTATE_DWORDS;

      pco_ref all_words_zero;
      for (unsigned u = 0; u < stride; ++u) {
         pco_ref word = pco_ref_hwreg(sh_index + u, PCO_REG_CLASS_SHARED);
         pco_ref word_is_zero =
            pco_ref_new_ssa(tctx->func, pco_ref_get_bits(dest), 1);

         pco_tstz(&tctx->b,
                  word_is_zero,
                  pco_ref_null(),
                  word,
                  .tst_type_main = PCO_TST_TYPE_MAIN_U32);

         if (!u) {
            all_words_zero = word_is_zero;
            continue;
         }

         pco_ref _all_words_zero =
            pco_ref_new_ssa(tctx->func, pco_ref_get_bits(dest), 1);
         pco_logical(&tctx->b,
                     _all_words_zero,
                     pco_ref_null(),
                     all_words_zero,
                     pco_ref_null(),
                     word_is_zero,
                     .logiop = PCO_LOGIOP_AND);

         all_words_zero = _all_words_zero;
      }

      instr = pco_mov(&tctx->b, dest, all_words_zero);

      break;
   }

   case nir_intrinsic_load_scratch:
      instr = trans_scratch(tctx, dest, src[0], src[1]);
      break;

   case nir_intrinsic_store_scratch:
      instr = trans_scratch(tctx, dest, src[1], src[0]);
      break;

   case nir_intrinsic_dma_ld_pco: {
      unsigned chans = pco_ref_get_chans(dest);

      instr = pco_ld(&tctx->b,
                     dest,
                     pco_ref_drc(PCO_DRC_0),
                     pco_ref_imm8(chans),
                     src[0]);

      break;
   }

   case nir_intrinsic_dma_st_pco: {
      unsigned chans = pco_ref_get_chans(src[0]) - 2;
      unsigned flags = nir_intrinsic_flags(intr);

      pco_ref data_comp =
         pco_ref_new_ssa(tctx->func, pco_ref_get_bits(src[0]), chans);
      pco_comp(&tctx->b, data_comp, src[0], pco_ref_val16(2));

      instr = pco_st32(&tctx->b,
                       data_comp,
                       pco_ref_drc(PCO_DRC_0),
                       pco_ref_imm8(chans),
                       src[0],
                       pco_ref_null(),
                       .idf = !!flags);

      break;
   }

   case nir_intrinsic_dma_ld_shregs_pco: {
      pco_ref addr = src[0];
      pco_ref burst_len = src[1];
      pco_ref dynsrc_offset = src[2];

      unsigned idx_reg_num = 0;
      pco_ref idx_reg =
         pco_ref_hwreg_idx(idx_reg_num, idx_reg_num, PCO_REG_CLASS_INDEX);

      pco_mov(&tctx->b, idx_reg, dynsrc_offset);

      pco_ref dest = pco_ref_hwreg(0, PCO_REG_CLASS_SHARED);
      dest = pco_ref_hwreg_idx_from(idx_reg_num, dest);

      instr =
         pco_ld_regbl(&tctx->b, dest, pco_ref_drc(PCO_DRC_0), burst_len, addr);

      break;
   }

   case nir_intrinsic_dma_st_shregs_pco: {
      pco_ref addr = src[0];
      pco_ref burst_len = src[1];
      pco_ref dynsrc_offset = src[2];
      unsigned flags = nir_intrinsic_flags(intr);

      unsigned idx_reg_num = 0;
      pco_ref idx_reg =
         pco_ref_hwreg_idx(idx_reg_num, idx_reg_num, PCO_REG_CLASS_INDEX);

      pco_mov(&tctx->b, idx_reg, dynsrc_offset);

      pco_ref data = pco_ref_hwreg(0, PCO_REG_CLASS_SHARED);
      data = pco_ref_hwreg_idx_from(idx_reg_num, data);

      instr = pco_st32_regbl(&tctx->b,
                             data,
                             pco_ref_drc(PCO_DRC_0),
                             burst_len,
                             addr,
                             pco_ref_null(),
                             .idf = !!flags);

      break;
   }

   case nir_intrinsic_dma_st_tiled_pco: {
      unsigned chans = pco_ref_get_chans(src[0]) - 2;

      pco_ref data_comp =
         pco_ref_new_ssa(tctx->func, pco_ref_get_bits(src[0]), chans);
      pco_comp(&tctx->b, data_comp, src[0], pco_ref_val16(2));

      instr = pco_st_tiled(&tctx->b,
                           data_comp,
                           pco_ref_imm8(PCO_DSIZE_32BIT),
                           pco_ref_drc(PCO_DRC_0),
                           pco_ref_imm8(chans),
                           src[0],
                           src[1],
                           .olchk = tctx->olchk);

      break;
   }

   /* Vertex sysvals. */
   case nir_intrinsic_load_vertex_id:
   case nir_intrinsic_load_instance_id:
   case nir_intrinsic_load_base_instance:
   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_draw_id:
   case nir_intrinsic_load_view_index:

   /* Compute sysvals. */
   case nir_intrinsic_load_local_invocation_index:
   case nir_intrinsic_load_workgroup_id:
   case nir_intrinsic_load_num_workgroups:

   /* Fragment sysvals. */
   case nir_intrinsic_load_front_face_op_pco:
      instr = trans_load_sysval(tctx, intr, dest);
      break;

   case nir_intrinsic_load_fs_meta_pco:
      assert(tctx->stage == MESA_SHADER_FRAGMENT);
      assert(tctx->shader->data.fs.meta.count > 0);

      return pco_mov(&tctx->b,
                     dest,
                     pco_ref_hwreg(tctx->shader->data.fs.meta.start,
                                   PCO_REG_CLASS_SHARED));
      break;

   case nir_intrinsic_ddx:
   case nir_intrinsic_ddx_fine:
   case nir_intrinsic_ddx_coarse:
      instr = intr->intrinsic == nir_intrinsic_ddx_fine
                 ? pco_fdsxf(&tctx->b, dest, src[0])
                 : pco_fdsx(&tctx->b, dest, src[0]);
      break;

   case nir_intrinsic_ddy:
   case nir_intrinsic_ddy_fine:
   case nir_intrinsic_ddy_coarse:
      instr = intr->intrinsic == nir_intrinsic_ddy_fine
                 ? pco_fdsyf(&tctx->b, dest, src[0])
                 : pco_fdsy(&tctx->b, dest, src[0]);
      break;

   /* Texture-related intrinsics. */
   case nir_intrinsic_load_tex_state_pco:
      instr = lower_load_tex_smp_state(tctx, intr, dest, false);
      break;

   case nir_intrinsic_load_smp_state_pco:
      instr = lower_load_tex_smp_state(tctx, intr, dest, true);
      break;

   case nir_intrinsic_load_tex_meta_pco:
      instr = lower_load_tex_smp_meta(tctx, intr, dest, false);
      break;

   case nir_intrinsic_load_smp_meta_pco:
      instr = lower_load_tex_smp_meta(tctx, intr, dest, true);
      break;

   case nir_intrinsic_smp_coeffs_pco:
   case nir_intrinsic_smp_raw_pco:
   case nir_intrinsic_smp_pco:
   case nir_intrinsic_smp_write_pco:
      instr = lower_smp(tctx, intr, &dest, src[0], src[1], src[2]);
      break;

   case nir_intrinsic_alphatst_pco:
      instr = lower_alphatst(tctx, dest, src[0], src[1], src[2]);
      break;

   case nir_intrinsic_isp_feedback_pco: {
      assert(tctx->stage == MESA_SHADER_FRAGMENT);
      bool does_discard = !nir_src_is_undef(intr->src[0]);
      bool does_depthf = !nir_src_is_undef(intr->src[1]);

      does_depthf &= (tctx->shader->data.fs.uses.depth_feedback &&
                      !tctx->shader->data.fs.uses.early_frag);

      if (does_discard) {
         pco_tstz(&tctx->b,
                  pco_ref_null(),
                  pco_ref_pred(PCO_PRED_P0),
                  src[0],
                  .tst_type_main = PCO_TST_TYPE_MAIN_U32);
      }

      instr = does_depthf ? pco_depthf(&tctx->b,
                                       pco_ref_drc(PCO_DRC_0),
                                       src[1],
                                       .olchk = tctx->olchk)
                          : pco_alphaf(&tctx->b,
                                       pco_ref_null(),
                                       pco_ref_drc(PCO_DRC_0),
                                       pco_zero,
                                       pco_zero,
                                       pco_7,
                                       .olchk = tctx->olchk);

      if (does_discard)
         pco_instr_set_exec_cnd(instr, PCO_EXEC_CND_E1_Z1);

      break;
   }

   case nir_intrinsic_alpha_to_coverage:
      assert(tctx->stage == MESA_SHADER_FRAGMENT);
      instr = pco_pck(&tctx->b,
                      pco_ref_bits(dest, 32),
                      src[0],
                      .pck_fmt = PCO_PCK_FMT_COV);
      break;

   case nir_intrinsic_mutex_pco:
      instr = pco_mutex(&tctx->b,
                        pco_ref_imm8(nir_intrinsic_mutex_id_pco(intr)),
                        .mutex_op = nir_intrinsic_mutex_op_pco(intr));
      break;

   case nir_intrinsic_load_instance_num_pco:
      instr = pco_mov(&tctx->b,
                      dest,
                      pco_ref_hwreg(PCO_SR_INST_NUM, PCO_REG_CLASS_SPEC));
      break;

   case nir_intrinsic_load_shared_reg_alloc_size_pco:
      instr = pco_mov(&tctx->b,
                      dest,
                      pco_ref_hwreg(PCO_SR_SH_ALLOC_SIZE, PCO_REG_CLASS_SPEC));
      break;

   case nir_intrinsic_load_sample_id:
      instr = pco_mov(&tctx->b,
                      dest,
                      pco_ref_hwreg(PCO_SR_SAMP_NUM, PCO_REG_CLASS_SPEC));
      break;

   case nir_intrinsic_load_layer_id:
      instr = pco_mov(&tctx->b,
                      dest,
                      pco_ref_hwreg(PCO_SR_RENDER_TGT_ID, PCO_REG_CLASS_SPEC));
      break;

   case nir_intrinsic_load_face_ccw_pco:
      instr = pco_mov(&tctx->b,
                      dest,
                      pco_ref_hwreg(PCO_SR_FACE_ORIENT, PCO_REG_CLASS_SPEC));
      break;

   case nir_intrinsic_load_tile_coord_pco: {
      assert(tctx->stage == MESA_SHADER_FRAGMENT);

      pco_ref xy[] = {
         pco_ref_hwreg(PCO_SR_TILE_X_P, PCO_REG_CLASS_SPEC),
         pco_ref_hwreg(PCO_SR_TILE_Y_P, PCO_REG_CLASS_SPEC),
      };

      unsigned component = nir_intrinsic_component(intr);
      unsigned chans = pco_ref_get_chans(dest);
      assert(component + chans <= ARRAY_SIZE(xy));

      instr = pco_vec(&tctx->b, dest, chans, &xy[component]);
      break;
   }

   case nir_intrinsic_load_savmsk_vm_pco:
      instr = pco_savmsk(&tctx->b,
                         dest,
                         pco_ref_null(),
                         .savmsk_mode = PCO_SAVMSK_MODE_VM);
      break;

   case nir_intrinsic_decl_reg:
   case nir_intrinsic_load_reg:
   case nir_intrinsic_store_reg:
      instr = trans_reg_intr(tctx, intr, dest, src[0], src[1]);
      break;

   case nir_intrinsic_emitpix_pco:
      instr = pco_emitpix(&tctx->b,
                          src[0],
                          src[1],
                          .freep = nir_intrinsic_freep(intr));
      break;

   case nir_intrinsic_wop_pco:
      instr = pco_wop(&tctx->b);
      break;

   case nir_intrinsic_pck_prog_pco:
      instr = pco_pck_prog(&tctx->b,
                           dest,
                           src[0],
                           src[1],
                           .scale = nir_intrinsic_scale(intr),
                           .roundzero = nir_intrinsic_roundzero(intr),
                           .rpt = pco_ref_get_chans(dest));
      break;

   case nir_intrinsic_pack_pco: {
      enum pipe_format fmt = nir_intrinsic_format(intr);
      enum pco_pck_fmt pck_fmt = pco_pck_format_from_pipe_format(fmt);
      bool scale = util_format_is_unorm(fmt) || util_format_is_snorm(fmt);
      unsigned chans = pco_ref_get_chans(src[0]);
      instr = pco_pck(&tctx->b,
                      dest,
                      src[0],
                      .rpt = chans,
                      .pck_fmt = pck_fmt,
                      .scale = scale);
      break;
   }

   case nir_intrinsic_unpack_pco: {
      enum pipe_format fmt = nir_intrinsic_format(intr);
      enum pco_pck_fmt pck_fmt = pco_pck_format_from_pipe_format(fmt);
      unsigned chans = pco_ref_get_chans(dest);
      bool scale = util_format_is_unorm(fmt) || util_format_is_snorm(fmt);
      instr = pco_unpck(&tctx->b,
                        dest,
                        pco_ref_elem(src[0], 0),
                        .rpt = chans,
                        .pck_fmt = pck_fmt,
                        .scale = scale);
      break;
   }

   default:
      printf("Unsupported intrinsic: \"");
      nir_print_instr(&intr->instr, stdout);
      printf("\"\n");
      UNREACHABLE("");
      break;
   }

   if (!pco_ref_is_scalar(dest))
      split_dest_comps(tctx, instr, dest);

   return instr;
}

/**
 * \brief Attempts to collate a vector within a vector.
 *
 * If a vector references another vector in its entirety in order/without
 * swizzling, we try to store a reference to said vector rather than its
 * individual components.
 *
 * \param[in] src The source/vector channel to start checking from.
 * \param[in] from The instruction the vector components are from.
 * \param[in] vec The potential vector reference from the parent instruction.
 * \param[in] vec_chans The number of sources/vector channels.
 * \return The number of collated sources, or 0 if collation failed.
 */
static pco_ref
try_collate_vec(pco_ref *src, pco_instr *from, pco_ref vec, unsigned vec_chans)
{
   /* Skip the first one since it's our reference (and we already know its
    * component is 0.
    */
   for (unsigned s = 1; s < vec_chans; ++s) {
      pco_instr *parent_instr = find_parent_instr_from(src[s], from);
      assert(parent_instr);

      if (parent_instr->op != PCO_OP_COMP)
         return pco_ref_null();

      pco_ref comp_src = parent_instr->src[0];
      unsigned comp_idx = pco_ref_get_imm(parent_instr->src[1]);
      ASSERTED unsigned chans = pco_ref_get_chans(comp_src);

      /* TODO: can this be true? */
      if (!pco_refs_are_equal(comp_src, vec, false))
         return pco_ref_null();

      assert(chans == vec_chans);

      if (comp_idx != s)
         return pco_ref_null();
   }

   return vec;
}

/**
 * \brief Attempts to collate vector sources.
 *
 * \param[in] tctx Translation context.
 * \param[in] dest Instruction destination.
 * \param[in] num_srcs The number of sources/vector channels.
 * \param[in] src The sources/vector components.
 * \return The number of collated sources, or 0 if collation failed.
 */
static unsigned try_collate_vec_srcs(trans_ctx *tctx,
                                     unsigned num_srcs,
                                     pco_ref *src,
                                     pco_ref *collated_src)
{
   bool collated_vector = false;
   unsigned num_srcs_collated = 0;
   pco_instr *from = pco_cursor_instr(tctx->b.cursor);
   if (!from) {
      from = pco_last_instr(
         pco_prev_block_nonempty(pco_cursor_block(tctx->b.cursor)));
   }

   assert(from);

   for (unsigned s = 0; s < num_srcs; ++s) {
      pco_instr *parent_instr = find_parent_instr_from(src[s], from);
      assert(parent_instr);

      /* This is a purely scalar source; append it and continue. */
      if (parent_instr->op != PCO_OP_COMP) {
         collated_src[num_srcs_collated++] = src[s];
         continue;
      }

      pco_ref comp_src = parent_instr->src[0];
      unsigned comp_idx = pco_ref_get_imm(parent_instr->src[1]);
      unsigned chans = pco_ref_get_chans(comp_src);

      /* We have a vector source, but it either:
       * - doesn't start from the first element
       * - is bigger than the remaining channels of *this* vec
       * so it's impossible for it to be contained in its entirety;
       * append the component and continue.
       */
      if (comp_idx != 0 || chans > (num_srcs - s)) {
         collated_src[num_srcs_collated++] = src[s];
         continue;
      }

      /* We have a candidate for an entire vector to be inserted. */
      pco_ref collated_ref = try_collate_vec(&src[s], from, comp_src, chans);
      if (pco_ref_is_null(collated_ref)) {
         collated_src[num_srcs_collated++] = src[s];
         continue;
      }

      /* We were successful, record this and increment accordingly. */
      collated_src[num_srcs_collated++] = collated_ref;

      s += chans - 1;
      collated_vector = true;
   }

   return collated_vector ? num_srcs_collated : 0;
}

/**
 * \brief Translates a NIR vec instruction into PCO, attempting collation.
 *
 * \param[in] tctx Translation context.
 * \param[in] dest Instruction destination.
 * \param[in] num_srcs The number of sources/vector components.
 * \param[in] src The sources/vector components.
 * \return The PCO instruction.
 */
static pco_instr *pco_trans_nir_vec(trans_ctx *tctx,
                                    pco_ref dest,
                                    unsigned num_srcs,
                                    pco_ref *src)

{
   /* If a vec contains entire other vecs, try to reference them directly. */
   pco_ref collated_src[NIR_MAX_VEC_COMPONENTS] = { 0 };
   unsigned num_srcs_collated =
      try_collate_vec_srcs(tctx, num_srcs, src, collated_src);
   if (!num_srcs_collated)
      return pco_vec(&tctx->b, dest, num_srcs, src);

   pco_instr *instr = pco_vec(&tctx->b, dest, num_srcs_collated, collated_src);

   /* Record the collated vectors. */
   for (unsigned s = 0; s < num_srcs_collated; ++s) {
      if (pco_ref_is_scalar(collated_src[s]))
         continue;

      pco_vec_info *vec_info =
         _mesa_hash_table_u64_search(tctx->func->vec_infos,
                                     collated_src[s].val);
      assert(vec_info);

      /* Skip if there are multiple users. */
      vec_info->vec_user = vec_info->vec_user ? VEC_USER_MULTI : instr;
   }

   return instr;
}

static inline enum pco_tst_op_main to_tst_op_main(nir_op op)
{
   switch (op) {
   case nir_op_fcsel:
   case nir_op_icsel_eqz:
      return PCO_TST_OP_MAIN_ZERO;

   case nir_op_fcsel_gt:
   case nir_op_i32csel_gt:
      return PCO_TST_OP_MAIN_GZERO;

   case nir_op_fcsel_ge:
   case nir_op_i32csel_ge:
      return PCO_TST_OP_MAIN_GEZERO;

   case nir_op_slt:
   case nir_op_flt:
   case nir_op_ilt:
   case nir_op_ult:
      return PCO_TST_OP_MAIN_LESS;

   case nir_op_sge:
   case nir_op_fge:
   case nir_op_ige:
   case nir_op_uge:
      return PCO_TST_OP_MAIN_GEQUAL;

   case nir_op_seq:
   case nir_op_feq:
   case nir_op_ieq:
      return PCO_TST_OP_MAIN_EQUAL;

   case nir_op_sne:
   case nir_op_fneu:
   case nir_op_ine:
      return PCO_TST_OP_MAIN_NOTEQUAL;

   default:
      break;
   }

   UNREACHABLE("");
}

static inline enum pco_tst_type_main to_tst_type_main(nir_op op, pco_ref src)
{
   switch (op) {
   case nir_op_fcsel:
   case nir_op_fcsel_gt:
   case nir_op_fcsel_ge:

   case nir_op_slt:
   case nir_op_sge:
   case nir_op_seq:
   case nir_op_sne:

   case nir_op_flt:
   case nir_op_fge:
   case nir_op_feq:
   case nir_op_fneu:

   case nir_op_fmin:
   case nir_op_fmax:
      return PCO_TST_TYPE_MAIN_F32;

   case nir_op_icsel_eqz:
   case nir_op_i32csel_gt:
   case nir_op_i32csel_ge:

   case nir_op_ilt:
   case nir_op_ige:
   case nir_op_ieq:
   case nir_op_ine:

   case nir_op_imin:
   case nir_op_imax:
      return PCO_TST_TYPE_MAIN_S32;

   case nir_op_ult:
   case nir_op_uge:

   case nir_op_umin:
   case nir_op_umax:
      return PCO_TST_TYPE_MAIN_U32;

   default:
      break;
   }

   UNREACHABLE("");
}

static inline bool is_scmp(nir_op op)
{
   switch (op) {
   case nir_op_slt:
   case nir_op_sge:
   case nir_op_seq:
   case nir_op_sne:
      return true;

   default:
      break;
   }

   return false;
}

/**
 * \brief Translates a NIR comparison op into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] op The NIR op.
 * \param[in] dest Instruction destination.
 * \param[in] src0 First comparison source.
 * \param[in] src1 Second comparison source.
 * \return The translated PCO instruction.
 */
static pco_instr *
trans_cmp(trans_ctx *tctx, nir_op op, pco_ref dest, pco_ref src0, pco_ref src1)
{
   enum pco_tst_op_main tst_op_main = to_tst_op_main(op);
   enum pco_tst_type_main tst_type_main = to_tst_type_main(op, src0);

   return is_scmp(op)
             ? pco_scmp(&tctx->b, dest, src0, src1, .tst_op_main = tst_op_main)
             : pco_bcmp(&tctx->b,
                        dest,
                        src0,
                        src1,
                        .tst_op_main = tst_op_main,
                        .tst_type_main = tst_type_main);
}

/**
 * \brief Translates a NIR {i,f}csel op into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] op The NIR op.
 * \param[in] src Instruction source.
 * \return The translated PCO instruction.
 */
static pco_instr *trans_csel(trans_ctx *tctx,
                             nir_op op,
                             pco_ref dest,
                             pco_ref src0,
                             pco_ref src1,
                             pco_ref src2)
{
   enum pco_tst_op_main tst_op_main = to_tst_op_main(op);
   enum pco_tst_type_main tst_type_main = to_tst_type_main(op, src0);

   if (op == nir_op_fcsel) {
      pco_ref tmp = src2;
      src2 = src1;
      src1 = tmp;
   }

   return pco_csel(&tctx->b,
                   dest,
                   src0,
                   src1,
                   src2,
                   .tst_op_main = tst_op_main,
                   .tst_type_main = tst_type_main);
}

/**
 * \brief Translates a NIR bitwise logical op into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] op The NIR op.
 * \param[in] dest Instruction destination.
 * \param[in] src0 First logical operand.
 * \param[in] src1 Second logical operand.
 * \return The translated PCO instruction.
 */
static pco_instr *trans_logical(trans_ctx *tctx,
                                nir_op op,
                                pco_ref dest,
                                pco_ref src0,
                                pco_ref src1)
{
   ASSERTED unsigned bits = pco_ref_get_bits(dest);

   /* TODO: 8/16-bit support via masking. */
   assert(bits == 1 || bits == 32);

   enum pco_logiop logiop;
   switch (op) {
   case nir_op_iand:
      logiop = PCO_LOGIOP_AND;
      break;

   case nir_op_ior:
      logiop = PCO_LOGIOP_OR;
      break;

   case nir_op_ixor:
      logiop = PCO_LOGIOP_XOR;
      break;

   case nir_op_inot:
      logiop = PCO_LOGIOP_XNOR;
      src1 = pco_zero;
      break;

   default:
      UNREACHABLE("");
   }

   return pco_logical(&tctx->b,
                      dest,
                      pco_ref_null(),
                      src0,
                      pco_ref_null(),
                      src1,
                      .logiop = logiop);
}

/**
 * \brief Translates a NIR shift op into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] op The NIR op.
 * \param[in] src Instruction source.
 * \return The translated PCO instruction.
 */
static pco_instr *trans_shift(trans_ctx *tctx,
                              nir_op op,
                              pco_ref dest,
                              pco_ref src0,
                              pco_ref src1)
{
   ASSERTED unsigned bits = pco_ref_get_bits(dest);
   assert(bits == 32);

   enum pco_shiftop shiftop;
   switch (op) {
   case nir_op_ishl:
      shiftop = PCO_SHIFTOP_LSL;
      break;

   case nir_op_ishr:
      shiftop = PCO_SHIFTOP_ASR_TWB;
      break;

   case nir_op_ushr:
      shiftop = PCO_SHIFTOP_SHR;
      break;

   default:
      UNREACHABLE("");
   }

   return pco_shift(&tctx->b,
                    dest,
                    src0,
                    src1,
                    pco_ref_null(),
                    .shiftop = shiftop);
}

static inline bool is_min(nir_op op)
{
   switch (op) {
   case nir_op_fmin:
   case nir_op_imin:
   case nir_op_umin:
      return true;

   case nir_op_fmax:
   case nir_op_imax:
   case nir_op_umax:
      return false;

   default:
      break;
   }

   UNREACHABLE("");
}

/**
 * \brief Translates a NIR min/max op into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] op The NIR op.
 * \param[in] src Instruction source.
 * \return The translated PCO instruction.
 */
static pco_instr *trans_min_max(trans_ctx *tctx,
                                nir_op op,
                                pco_ref dest,
                                pco_ref src0,
                                pco_ref src1)
{
   enum pco_tst_type_main tst_type_main = to_tst_type_main(op, src0);

   return is_min(op) ? pco_min(&tctx->b,
                               dest,
                               src0,
                               src1,
                               .tst_type_main = tst_type_main)
                     : pco_max(&tctx->b,
                               dest,
                               src0,
                               src1,
                               .tst_type_main = tst_type_main);
}

/**
 * \brief Translates a NIR trigonometric op into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] op The NIR op.
 * \param[in] dest Instruction destination.
 * \param[in] src Instruction source.
 * \return The translated PCO instruction.
 */
static pco_instr *
trans_trig(trans_ctx *tctx, nir_op op, pco_ref dest, pco_ref src)
{
   assert(pco_ref_get_chans(dest) == 1);
   assert(pco_ref_get_bits(dest) == 32);

   enum pco_fred_type fred_type;
   switch (op) {
   case nir_op_fsin:
      fred_type = PCO_FRED_TYPE_SIN;
      break;

   case nir_op_fcos:
      fred_type = PCO_FRED_TYPE_COS;
      break;

   /* TODO: arctan, arctanc, sinc, cosc. */
   default:
      UNREACHABLE("");
   }

   pco_ref fred_dest_a = pco_ref_new_ssa32(tctx->func);
   pco_fred(&tctx->b,
            pco_ref_null(),
            fred_dest_a,
            pco_ref_null(),
            src,
            pco_ref_null(),
            pco_ref_imm8(0),
            .fred_type = fred_type,
            .fred_part = PCO_FRED_PART_A);

   pco_ref fred_dest_b = pco_ref_new_ssa32(tctx->func);
   pco_fred(&tctx->b,
            fred_dest_b,
            pco_ref_null(),
            pco_ref_null(),
            src,
            fred_dest_a,
            pco_ref_imm8(0),
            .fred_type = fred_type,
            .fred_part = PCO_FRED_PART_B);

   pco_ref trig_dest = pco_ref_new_ssa32(tctx->func);
   switch (op) {
   case nir_op_fsin:
   case nir_op_fcos:
      pco_fsinc(&tctx->b, trig_dest, pco_ref_pred(PCO_PRED_P0), fred_dest_b);
      break;

   default:
      UNREACHABLE("");
   }

   return pco_psel_trig(&tctx->b,
                        dest,
                        pco_ref_pred(PCO_PRED_P0),
                        trig_dest,
                        fred_dest_b);
}

static pco_instr *
trans_conv(trans_ctx *tctx, nir_op op, pco_ref dest, pco_ref src)
{
   bool roundzero;
   enum pco_pck_fmt pck_fmt;
   bool unpck;

   switch (op) {
   /* b2* ops are just sels. */
   case nir_op_b2f32:
   case nir_op_b2i32:
      return pco_bcsel(&tctx->b,
                       dest,
                       src,
                       op == nir_op_b2f32 ? pco_fone : pco_one,
                       pco_zero);

   case nir_op_f2f16:
   case nir_op_f2f16_rtne:
   case nir_op_f2f16_rtz:
      assert(pco_ref_get_bits(src) == 32);

      roundzero = (op != nir_op_f2f16_rtne);
      pck_fmt = PCO_PCK_FMT_F16F16;
      unpck = false;

      /* TODO: support non-32 bit vars. */
      dest = pco_ref_bits(dest, 32);
      break;

   case nir_op_f2f32:
      assert(pco_ref_get_bits(src) == 16);

      roundzero = false;
      pck_fmt = PCO_PCK_FMT_F16F16;
      unpck = true;

      /* TODO: support non-32 bit vars. */
      src = pco_ref_bits(src, 32);
      break;

   case nir_op_f2i32:
   case nir_op_f2i32_rtne:
   case nir_op_f2u32:
      roundzero = (op != nir_op_f2i32_rtne);
      pck_fmt = (op == nir_op_f2u32) ? PCO_PCK_FMT_U32 : PCO_PCK_FMT_S32;
      unpck = false;
      break;

   /* Just consume/treat as 32-bit for now. */
   /* TODO: support non-32 bit vars. */
   case nir_op_i2i16:
   case nir_op_i2i32:
   case nir_op_u2u16:
   case nir_op_u2u32:
      dest = pco_ref_bits(dest, 32);
      src = pco_ref_bits(src, 32);
      return pco_mov(&tctx->b, dest, src);

   case nir_op_i2f32:
   case nir_op_u2f32:
      roundzero = tctx->ftz32b;
      pck_fmt = (op == nir_op_u2f32) ? PCO_PCK_FMT_U32 : PCO_PCK_FMT_S32;
      unpck = true;
      break;

   default:
      UNREACHABLE("Unsupported conversion op.");
   }

   return unpck ? pco_unpck(&tctx->b,
                            dest,
                            pco_ref_elem(src, 0),
                            .pck_fmt = pck_fmt,
                            .roundzero = roundzero)
                : pco_pck(&tctx->b,
                          dest,
                          src,
                          .pck_fmt = pck_fmt,
                          .roundzero = roundzero);
}

/**
 * \brief Translates a NIR alu instruction into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] alu The nir alu instruction.
 * \return The PCO instruction.
 */
static pco_instr *trans_alu(trans_ctx *tctx, nir_alu_instr *alu)
{
   const nir_op_info *info = &nir_op_infos[alu->op];
   unsigned num_srcs = info->num_inputs;

   pco_ref dest = pco_ref_nir_def_t(&alu->def, tctx);

   pco_ref src[NIR_MAX_VEC_COMPONENTS] = { 0 };
   for (unsigned s = 0; s < num_srcs; ++s)
      src[s] = pco_ref_nir_alu_src_t(alu, s, tctx);

   pco_instr *instr;
   switch (alu->op) {
   case nir_op_fneg:
      instr = pco_fneg(&tctx->b, dest, src[0]);
      break;

   case nir_op_fabs:
      instr = pco_fabs(&tctx->b, dest, src[0]);
      break;

   case nir_op_ffloor:
      instr = pco_fflr(&tctx->b, dest, src[0]);
      break;

   case nir_op_fceil:
      instr = pco_fceil(&tctx->b, dest, src[0]);
      break;

   case nir_op_fadd:
      instr = pco_fadd(&tctx->b, dest, src[0], src[1]);
      break;

   case nir_op_fsub:
      instr = pco_fadd(&tctx->b, dest, pco_ref_neg(src[1]), src[0]);
      break;

   case nir_op_fmul:
      instr = pco_fmul(&tctx->b, dest, src[0], src[1]);
      break;

   case nir_op_ffma:
      instr = pco_fmad(&tctx->b, dest, src[0], src[1], src[2]);
      break;

   case nir_op_frcp:
      instr = pco_frcp(&tctx->b, dest, src[0]);
      break;

   case nir_op_frsq:
      instr = pco_frsq(&tctx->b, dest, src[0]);
      break;

   case nir_op_fexp2:
      instr = pco_fexp(&tctx->b, dest, src[0]);
      break;

   case nir_op_flog2:
      instr = pco_flog(&tctx->b, dest, src[0]);
      break;

   case nir_op_fsign:
      instr = pco_fsign(&tctx->b, dest, src[0]);
      break;

   case nir_op_fsat:
      instr = pco_fadd(&tctx->b, dest, src[0], pco_zero, .sat = true);
      break;

   case nir_op_fsin:
   case nir_op_fcos:
      instr = trans_trig(tctx, alu->op, dest, src[0]);
      break;

   case nir_op_isign:
      instr = pco_isign(&tctx->b, dest, src[0]);
      break;

   case nir_op_fcopysign_pco:
      instr = pco_copysign(&tctx->b, dest, src[0], src[1]);
      break;

   case nir_op_ineg:
      instr = pco_iadd32(&tctx->b,
                         dest,
                         pco_ref_neg(src[0]),
                         pco_zero,
                         pco_ref_null(),
                         .s = true);
      break;

   case nir_op_iabs:
      instr = pco_iadd32(&tctx->b,
                         dest,
                         pco_ref_abs(src[0]),
                         pco_zero,
                         pco_ref_null(),
                         .s = true);
      break;

   case nir_op_iadd:
      instr = pco_iadd32(&tctx->b, dest, src[0], src[1], pco_ref_null());
      break;

   case nir_op_uadd64_32: {
      pco_ref dest_comps[2] = {
         [0] = pco_ref_new_ssa32(tctx->func),
         [1] = pco_ref_new_ssa32(tctx->func),
      };

      pco_add64_32(&tctx->b,
                   dest_comps[0],
                   dest_comps[1],
                   src[0],
                   src[1],
                   src[2],
                   pco_ref_null());

      /* TODO: mark this vec as being non-contiguous,
       * add pass for expanding.
       */
      instr = pco_trans_nir_vec(tctx, dest, 2, dest_comps);
      break;
   }

   case nir_op_umad64_32: {
      pco_ref dest_comps[2] = {
         [0] = pco_ref_new_ssa32(tctx->func),
         [1] = pco_ref_new_ssa32(tctx->func),
      };

      pco_imadd64(&tctx->b,
                  dest_comps[0],
                  dest_comps[1],
                  src[0],
                  src[1],
                  src[2],
                  src[3],
                  pco_ref_null());

      /* TODO: mark this vec as being non-contiguous,
       * add pass for expanding.
       */
      instr = pco_trans_nir_vec(tctx, dest, 2, dest_comps);
      break;
   }

   case nir_op_imul:
      instr = pco_imul32(&tctx->b, dest, src[0], src[1], pco_ref_null());
      break;

   case nir_op_imul_high:
      instr = pco_imadd64(&tctx->b,
                          pco_ref_null(),
                          dest,
                          src[0],
                          src[1],
                          pco_zero,
                          pco_zero,
                          pco_ref_null(),
                          .s = true);
      break;

   case nir_op_umul_high:
      instr = pco_imadd64(&tctx->b,
                          pco_ref_null(),
                          dest,
                          src[0],
                          src[1],
                          pco_zero,
                          pco_zero,
                          pco_ref_null());
      break;

   case nir_op_umul_16x16:
      instr = pco_imadd64(&tctx->b,
                          dest,
                          pco_ref_null(),
                          src[0],
                          src[1],
                          pco_zero,
                          pco_zero,
                          pco_ref_null());
      break;

   case nir_op_imad:
      instr = pco_imadd32(&tctx->b,
                          dest,
                          src[0],
                          src[1],
                          src[2],
                          pco_ref_null(),
                          .s = true);
      break;

   /* Set-on (float) comparisons. */
   case nir_op_slt:
   case nir_op_sge:
   case nir_op_seq:
   case nir_op_sne:

   /* Float comparisons. */
   case nir_op_flt:
   case nir_op_fge:
   case nir_op_feq:
   case nir_op_fneu:

   /* Signed int comparisons. */
   case nir_op_ilt:
   case nir_op_ige:
   case nir_op_ieq:
   case nir_op_ine:

   /* Unsigned int comparisons. */
   case nir_op_ult:
   case nir_op_uge:
      instr = trans_cmp(tctx, alu->op, dest, src[0], src[1]);
      break;

   case nir_op_bcsel:
      instr = pco_bcsel(&tctx->b, dest, src[0], src[1], src[2]);
      break;

   case nir_op_fcsel:
   case nir_op_fcsel_gt:
   case nir_op_fcsel_ge:

   case nir_op_icsel_eqz:
   case nir_op_i32csel_gt:
   case nir_op_i32csel_ge:
      instr = trans_csel(tctx, alu->op, dest, src[0], src[1], src[2]);
      break;

   case nir_op_iand:
   case nir_op_ior:
   case nir_op_ixor:
   case nir_op_inot:
      instr = trans_logical(tctx, alu->op, dest, src[0], src[1]);
      break;

   case nir_op_ishl:
   case nir_op_ishr:
   case nir_op_ushr:
      instr = trans_shift(tctx, alu->op, dest, src[0], src[1]);
      break;

   case nir_op_bit_count:
      instr = pco_cbs(&tctx->b, dest, src[0]);
      break;

   case nir_op_ufind_msb:
      instr = pco_ftb(&tctx->b, dest, src[0]);
      break;

   case nir_op_ibitfield_extract: {
      pco_ref bfe = pco_ref_new_ssa32(tctx->func);
      pco_ibfe(&tctx->b, bfe, src[0], src[1], src[2]);
      instr = pco_bcsel(&tctx->b, dest, src[2], bfe, pco_zero);
      break;
   }

   case nir_op_ubitfield_extract: {
      pco_ref bfe = pco_ref_new_ssa32(tctx->func);
      pco_ubfe(&tctx->b, bfe, src[0], src[1], src[2]);
      instr = pco_bcsel(&tctx->b, dest, src[2], bfe, pco_zero);
      break;
   }

   case nir_op_bitfield_insert: {
      pco_ref bfi = pco_ref_new_ssa32(tctx->func);
      pco_bfi(&tctx->b, bfi, src[0], src[1], src[2], src[3]);
      instr = pco_bcsel(&tctx->b, dest, src[3], bfi, src[0]);
      break;
   }

   case nir_op_bitfield_reverse:
      instr = pco_rev(&tctx->b, dest, src[0]);
      break;

   case nir_op_interleave:
      instr = pco_shuffle(&tctx->b, dest, src[0], src[1]);
      break;

   case nir_op_b2f32:
   case nir_op_b2i32:
   case nir_op_f2f16:
   case nir_op_f2f16_rtne:
   case nir_op_f2f16_rtz:
   case nir_op_f2f32:
   case nir_op_f2i32:
   case nir_op_f2i32_rtne:
   case nir_op_f2u32:
   case nir_op_i2f32:
   case nir_op_i2i16:
   case nir_op_i2i32:
   case nir_op_u2f32:
   case nir_op_u2u16:
   case nir_op_u2u32:
      instr = trans_conv(tctx, alu->op, dest, src[0]);
      break;

   case nir_op_fmin:
   case nir_op_fmax:

   case nir_op_imin:
   case nir_op_imax:

   case nir_op_umin:
   case nir_op_umax:
      instr = trans_min_max(tctx, alu->op, dest, src[0], src[1]);
      break;

   case nir_op_pack_half_2x16:
      instr = pco_pck(&tctx->b,
                      dest,
                      src[0],
                      .rpt = 2,
                      .pck_fmt = PCO_PCK_FMT_F16F16);
      break;

   case nir_op_unpack_half_2x16:
      instr = pco_unpck(&tctx->b,
                        dest,
                        pco_ref_elem(src[0], 0),
                        .rpt = 2,
                        .pck_fmt = PCO_PCK_FMT_F16F16);
      break;

   case nir_op_pack_snorm_4x8:
      instr = pco_pck(&tctx->b,
                      dest,
                      src[0],
                      .rpt = 4,
                      .pck_fmt = PCO_PCK_FMT_S8888,
                      .scale = true);
      break;

   case nir_op_unpack_snorm_4x8:
      instr = pco_unpck(&tctx->b,
                        dest,
                        pco_ref_elem(src[0], 0),
                        .rpt = 4,
                        .pck_fmt = PCO_PCK_FMT_S8888,
                        .scale = true);
      break;

   case nir_op_pack_unorm_4x8:
      instr = pco_pck(&tctx->b,
                      dest,
                      src[0],
                      .rpt = 4,
                      .pck_fmt = PCO_PCK_FMT_U8888,
                      .scale = true);
      break;

   case nir_op_unpack_unorm_4x8:
      instr = pco_unpck(&tctx->b,
                        dest,
                        pco_ref_elem(src[0], 0),
                        .rpt = 4,
                        .pck_fmt = PCO_PCK_FMT_U8888,
                        .scale = true);
      break;

   case nir_op_pack_snorm_2x16:
      instr = pco_pck(&tctx->b,
                      dest,
                      src[0],
                      .rpt = 2,
                      .pck_fmt = PCO_PCK_FMT_S1616,
                      .scale = true);
      break;

   case nir_op_unpack_snorm_2x16:
      instr = pco_unpck(&tctx->b,
                        dest,
                        pco_ref_elem(src[0], 0),
                        .rpt = 2,
                        .pck_fmt = PCO_PCK_FMT_S1616,
                        .scale = true);
      break;

   case nir_op_pack_unorm_2x16:
      instr = pco_pck(&tctx->b,
                      dest,
                      src[0],
                      .rpt = 2,
                      .pck_fmt = PCO_PCK_FMT_U1616,
                      .scale = true);
      break;

   case nir_op_unpack_unorm_2x16:
      instr = pco_unpck(&tctx->b,
                        dest,
                        pco_ref_elem(src[0], 0),
                        .rpt = 2,
                        .pck_fmt = PCO_PCK_FMT_U1616,
                        .scale = true);
      break;

   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_vec5:
   case nir_op_vec8:
   case nir_op_vec16:
      instr = pco_trans_nir_vec(tctx, dest, num_srcs, src);
      break;

   case nir_op_mov:
      instr = pco_mov(&tctx->b, dest, src[0]);
      break;

   default:
      printf("Unsupported alu instruction: \"");
      nir_print_instr(&alu->instr, stdout);
      printf("\"\n");
      UNREACHABLE("");
   }

   if (!pco_ref_is_scalar(dest))
      split_dest_comps(tctx, instr, dest);

   return instr;
}

/**
 * \brief Translates a NIR load constant instruction into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] nconst The nir load constant instruction.
 * \return The PCO instruction.
 */
static pco_instr *trans_const(trans_ctx *tctx, nir_load_const_instr *nconst)
{
   unsigned num_bits = nconst->def.bit_size;
   unsigned chans = nconst->def.num_components;

   pco_ref dest = pco_ref_nir_def_t(&nconst->def, tctx);

   if (num_bits == 1) {
      assert(chans == 1);

      bool val = nir_const_value_as_bool(nconst->value[0], 1);
      pco_ref imm_reg = pco_ref_bits(val ? pco_true : pco_zero, 1);

      return pco_mov(&tctx->b, dest, imm_reg);
   }

   /* No 64-bit support yet. */
   assert(num_bits <= 32);

   if (pco_ref_is_scalar(dest)) {
      assert(chans == 1);

      uint64_t val = nir_const_value_as_uint(nconst->value[0], num_bits);
      pco_ref imm =
         pco_ref_imm(val, pco_bits(num_bits), pco_ref_get_dtype(dest));

      /* Override to 32-bit. */
      dest = pco_ref_bits(dest, 32);
      imm = pco_ref_bits(imm, 32);

      return pco_movi32(&tctx->b, dest, imm);
   }

   pco_ref comps[NIR_MAX_VEC_COMPONENTS] = { 0 };
   for (unsigned c = 0; c < chans; ++c) {
      comps[c] = pco_ref_new_ssa(tctx->func, pco_ref_get_bits(dest), 1);

      uint64_t val = nir_const_value_as_uint(nconst->value[c], num_bits);
      pco_ref imm =
         pco_ref_imm(val, pco_bits(num_bits), pco_ref_get_dtype(dest));

      pco_movi32(&tctx->b, comps[c], imm);
   }

   pco_instr *instr = pco_vec(&tctx->b, dest, chans, comps);

   split_dest_comps(tctx, instr, dest);

   return instr;
}

static pco_instr *trans_undef(trans_ctx *tctx, nir_undef_instr *undef)
{
   unsigned num_bits = undef->def.bit_size;
   unsigned chans = undef->def.num_components;

   pco_ref dest = pco_ref_nir_def_t(&undef->def, tctx);

   if (num_bits == 1) {
      assert(chans == 1);
      return pco_mov(&tctx->b, dest, pco_ref_bits(pco_zero, 1));
   }

   assert(num_bits == 32);

   if (pco_ref_is_scalar(dest)) {
      assert(chans == 1);

      pco_ref imm = pco_ref_imm(0, pco_bits(num_bits), pco_ref_get_dtype(dest));

      return pco_movi32(&tctx->b, dest, imm);
   }

   pco_ref comps[NIR_MAX_VEC_COMPONENTS];
   for (unsigned c = 0; c < chans; ++c)
      comps[c] = pco_zero;

   pco_instr *instr = pco_vec(&tctx->b, dest, chans, comps);

   split_dest_comps(tctx, instr, dest);

   return instr;
}

/**
 * \brief Translates a NIR jump instruction into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] jump The NIR jump instruction.
 * \return The PCO instruction.
 */
static pco_instr *trans_jump(trans_ctx *tctx, nir_jump_instr *jump)
{
   switch (jump->type) {
   case nir_jump_break:
      return pco_break(&tctx->b);

   case nir_jump_continue:
      return pco_continue(&tctx->b);

   default:
      break;
   }

   UNREACHABLE("");
}

/**
 * \brief Translates a NIR instruction into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] ninstr The nir instruction.
 * \return The PCO instruction.
 */
static pco_instr *trans_instr(trans_ctx *tctx, nir_instr *ninstr)
{
   switch (ninstr->type) {
   case nir_instr_type_intrinsic:
      return trans_intr(tctx, nir_instr_as_intrinsic(ninstr));

   case nir_instr_type_load_const:
      return trans_const(tctx, nir_instr_as_load_const(ninstr));

   case nir_instr_type_alu:
      return trans_alu(tctx, nir_instr_as_alu(ninstr));

   case nir_instr_type_jump:
      return trans_jump(tctx, nir_instr_as_jump(ninstr));

   case nir_instr_type_undef:
      return trans_undef(tctx, nir_instr_as_undef(ninstr));

   default:
      break;
   }

   UNREACHABLE("");
}

/**
 * \brief Translates a NIR block into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] parent_cf_node The parent cf node.
 * \param[in] cf_node_list The PCO cf node list.
 * \param[in] nblock The nir block.
 * \return The PCO block.
 */
static pco_block *trans_block(trans_ctx *tctx,
                              pco_cf_node *parent_cf_node,
                              struct list_head *cf_node_list,
                              nir_block *nblock)
{
   pco_block *block = pco_block_create(tctx->func);

   block->cf_node.flag = tctx->flag;
   block->cf_node.parent = parent_cf_node;
   list_addtail(&block->cf_node.link, cf_node_list);

   tctx->b = pco_builder_create(tctx->func, pco_cursor_after_block(block));

   nir_foreach_instr (ninstr, nblock) {
      trans_instr(tctx, ninstr);
   }

   return block;
}

/**
 * \brief Translates a NIR if into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] parent_cf_node The parent cf node.
 * \param[in] cf_node_list The PCO cf node list.
 * \param[in] nif The nir if.
 */
static void trans_if(trans_ctx *tctx,
                     pco_cf_node *parent_cf_node,
                     struct list_head *cf_node_list,
                     nir_if *nif)
{
   pco_if *pif = pco_if_create(tctx->func);

   pif->cf_node.flag = tctx->flag;
   pif->cf_node.parent = parent_cf_node;
   list_addtail(&pif->cf_node.link, cf_node_list);

   pif->cond = pco_ref_nir_src_t(&nif->condition, tctx);
   assert(pco_ref_is_scalar(pif->cond));

   bool has_then = !nir_cf_list_is_empty_block(&nif->then_list);
   bool has_else = !nir_cf_list_is_empty_block(&nif->else_list);
   assert(has_then || has_else);

   enum pco_cf_node_flag flag = tctx->flag;
   if (has_then) {
      tctx->flag = PCO_CF_NODE_FLAG_IF_THEN;
      trans_cf_nodes(tctx, &pif->cf_node, &pif->then_body, &nif->then_list);
   }

   if (has_else) {
      tctx->flag = PCO_CF_NODE_FLAG_IF_ELSE;
      trans_cf_nodes(tctx, &pif->cf_node, &pif->else_body, &nif->else_list);
   }

   tctx->flag = flag;
}

/**
 * \brief Translates a NIR loop into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] parent_cf_node The parent cf node.
 * \param[in] cf_node_list The PCO cf node list.
 * \param[in] nloop The nir loop.
 */
static void trans_loop(trans_ctx *tctx,
                       pco_cf_node *parent_cf_node,
                       struct list_head *cf_node_list,
                       nir_loop *nloop)
{
   pco_loop *loop = pco_loop_create(tctx->func);

   loop->cf_node.flag = tctx->flag;
   loop->cf_node.parent = parent_cf_node;
   list_addtail(&loop->cf_node.link, cf_node_list);

   assert(!nir_cf_list_is_empty_block(&nloop->body));
   assert(!nir_loop_has_continue_construct(nloop));

   enum pco_cf_node_flag flag = tctx->flag;
   tctx->flag = PCO_CF_NODE_FLAG_BODY;

   trans_cf_nodes(tctx, &loop->cf_node, &loop->body, &nloop->body);

   tctx->flag = flag;
}

/**
 * \brief Translates a NIR function into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] impl The nir function impl.
 * \return The PCO function.
 */
static pco_func *trans_func(trans_ctx *tctx, nir_function_impl *impl)
{
   nir_function *nfunc = impl->function;
   enum pco_func_type func_type = PCO_FUNC_TYPE_CALLABLE;

   if (nfunc->is_preamble)
      func_type = PCO_FUNC_TYPE_PREAMBLE;
   else if (nfunc->is_entrypoint)
      func_type = PCO_FUNC_TYPE_ENTRYPOINT;

   pco_func *func = pco_func_create(tctx->shader, func_type, nfunc->num_params);
   tctx->func = func;

   func->name = ralloc_strdup(func, nfunc->name);
   func->next_ssa = impl->ssa_alloc;

   /* TODO: Function parameter support. */
   assert(func->num_params == 0 && func->params == NULL);

   /* Gather types. */
   tctx->float_types = BITSET_RZALLOC(NULL, impl->ssa_alloc);
   tctx->int_types = BITSET_RZALLOC(NULL, impl->ssa_alloc);
   nir_gather_types(impl, tctx->float_types, tctx->int_types);

   tctx->flag = PCO_CF_NODE_FLAG_BODY;
   trans_cf_nodes(tctx, &func->cf_node, &func->body, &impl->body);

   ralloc_free(tctx->float_types);
   ralloc_free(tctx->int_types);

   return func;
}

/**
 * \brief Translates NIR control flow nodes into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] parent_cf_node The parent cf node.
 * \param[in] cf_node_list The PCO cf node list.
 * \param[in,out] nir_cf_node_list The NIR cf node list.
 * \return The first block from the cf nodes.
 */
static pco_block *trans_cf_nodes(trans_ctx *tctx,
                                 pco_cf_node *parent_cf_node,
                                 struct list_head *cf_node_list,
                                 struct exec_list *nir_cf_node_list)
{
   pco_block *start_block = NULL;

   foreach_list_typed (nir_cf_node, ncf_node, node, nir_cf_node_list) {
      switch (ncf_node->type) {
      case nir_cf_node_block: {
         pco_block *block = trans_block(tctx,
                                        parent_cf_node,
                                        cf_node_list,
                                        nir_cf_node_as_block(ncf_node));
         if (!start_block)
            start_block = block;
         break;
      }

      case nir_cf_node_if: {
         trans_if(tctx,
                  parent_cf_node,
                  cf_node_list,
                  nir_cf_node_as_if(ncf_node));
         break;
      }

      case nir_cf_node_loop: {
         trans_loop(tctx,
                    parent_cf_node,
                    cf_node_list,
                    nir_cf_node_as_loop(ncf_node));
         break;
      }

      default:
         UNREACHABLE("");
      }
   }

   return start_block;
}

/**
 * \brief Translates a NIR shader into a PCO shader.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in] nir NIR shader.
 * \param[in] data Shader-specific data.
 * \param[in] mem_ctx Ralloc memory allocation context.
 * \return The PCO shader.
 */
pco_shader *
pco_trans_nir(pco_ctx *ctx, nir_shader *nir, pco_data *data, void *mem_ctx)
{
   pco_shader *shader = pco_shader_create(ctx, nir, mem_ctx);

   if (data)
      memcpy(&shader->data, data, sizeof(*data));

   trans_ctx tctx = {
      .pco_ctx = ctx,
      .shader = shader,
      .stage = shader->stage,
      .ftz32b =
         nir_is_rounding_mode_rtz(nir->info.float_controls_execution_mode, 32),
   };

   if (shader->stage == MESA_SHADER_FRAGMENT)
      tctx.olchk = !data->fs.uses.olchk_skip;

   nir_foreach_function_with_impl (func, impl, nir) {
      trans_func(&tctx, impl);
   }

   if (pco_should_print_shader(shader))
      pco_print_shader(shader, stdout, "before passes");

   return shader;
}
