/*
 * Copyright (C) 2020,2026 Collabora Ltd.
 * Copyright (C) 2022 Alyssa Rosenzweig
 * Copyright (C) 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "panfrost/compiler/pan_compiler.h"
#include "util/u_qsort.h"

#include "bifrost/bi_debug.h"
#include "bifrost/disassemble.h"
#include "panfrost/lib/pan_props.h"
#include "valhall/disassemble.h"
#include "valhall/va_compiler.h"
#include "bi_builder.h"
#include "bi_quirks.h"
#include "bifrost_compile.h"
#include "bifrost_nir.h"
#include "pan_nir.h"
#include "compiler.h"

static void pan_stats_verbose(FILE *f, const char *prefix, bi_context *ctx,
                              const struct pan_stats *stats,
                              const struct pan_shader_info *info);


/* How many bytes are prefetched by the Bifrost shader core. From the final
 * clause of the shader, this range must be valid instructions or zero. */
#define BIFROST_SHADER_PREFETCH 128

static bi_block *emit_cf_list(bi_context *ctx, struct exec_list *list);

static bi_index
bi_preload(bi_builder *b, enum bi_preload val)
{
   unsigned reg = bi_preload_reg(val, b->shader->arch);
   if (bi_is_null(b->shader->preloaded[reg])) {
      /* Insert at the beginning of the shader */
      bi_builder b_ = *b;
      b_.cursor = bi_before_block(bi_start_block(&b->shader->blocks));

      /* Cache the result */
      b->shader->preloaded[reg] = bi_mov_i32(&b_, bi_register(reg));
   }

   return b->shader->preloaded[reg];
}

static bi_index
bi_coverage(bi_builder *b)
{
   if (bi_is_null(b->shader->coverage))
      b->shader->coverage = bi_preload(b, BI_PRELOAD_CUMULATIVE_COVERAGE);

   return b->shader->coverage;
}

/*
 * Vertex ID and Instance ID are preloaded registers. Where they are preloaded
 * changed from Bifrost to Valhall. Provide helpers that smooth over the
 * architectural difference.
 */
static inline bi_index
bi_vertex_id(bi_builder *b)
{
   return bi_preload(b, BI_PRELOAD_VERTEX_ID);
}

static inline bi_index
bi_instance_id(bi_builder *b)
{
   return bi_preload(b, BI_PRELOAD_INSTANCE_ID);
}

static inline bi_index
bi_draw_id(bi_builder *b)
{
   assert(b->shader->arch >= 9);
   return bi_preload(b, BI_PRELOAD_DRAW_ID);
}

static void
bi_emit_jump(bi_builder *b, nir_jump_instr *instr)
{
   bi_instr *branch = bi_jump(b, bi_zero());

   switch (instr->type) {
   case nir_jump_break:
      branch->branch_target = b->shader->break_block;
      break;
   case nir_jump_continue:
      branch->branch_target = b->shader->continue_block;
      break;
   default:
      UNREACHABLE("Unhandled jump type");
   }

   bi_block_add_successor(b->shader->current_block, branch->branch_target);
   b->shader->current_block->unconditional_jumps = true;
}

/* Builds a 64-bit hash table key for an index */
static uint64_t
bi_index_to_key(bi_index idx)
{
   static_assert(sizeof(idx) <= sizeof(uint64_t), "too much padding");

   uint64_t key = 0;
   memcpy(&key, &idx, sizeof(idx));
   return key;
}

/*
 * Extract a single channel out of a vector source. We split vectors with SPLIT
 * so we can use the split components directly, without emitting an extract.
 * This has advantages of RA, as the split can usually be optimized away.
 */
static bi_index
bi_extract(bi_builder *b, bi_index vec, unsigned channel)
{
   bi_index *components = _mesa_hash_table_u64_search(b->shader->allocated_vec,
                                                      bi_index_to_key(vec));

   /* No extract needed for scalars.
    *
    * This is a bit imprecise, but actual bugs (missing splits for vectors)
    * should be caught by the following assertion. It is too difficult to
    * ensure bi_extract is only called for real vectors.
    */
   if (components == NULL && channel == 0)
      return vec;

   assert(components != NULL && "missing bi_cache_collect()");
   return components[channel];
}

static void
bi_cache_collect(bi_builder *b, bi_index dst, bi_index *s, unsigned n)
{
   /* Lifetime of a hash table entry has to be at least as long as the table */
   bi_index *channels = ralloc_array(b->shader, bi_index, n);
   memcpy(channels, s, sizeof(bi_index) * n);

   _mesa_hash_table_u64_insert(b->shader->allocated_vec, bi_index_to_key(dst),
                               channels);
}

/*
 * Splits an n-component vector (vec) into n scalar destinations (dests) using a
 * split pseudo-instruction.
 *
 * Pre-condition: dests is filled with bi_null().
 */
static void
bi_emit_split_i32(bi_builder *b, bi_index dests[4], bi_index vec, unsigned n)
{
   assert(n <= 4);
   /* Setup the destinations */
   for (unsigned i = 0; i < n; ++i) {
      dests[i] = bi_temp(b->shader);
   }

   /* Emit the split */
   if (n == 1) {
      bi_mov_i32_to(b, dests[0], vec);
   } else {
      bi_instr *I = bi_split_i32_to(b, n, vec);

      bi_foreach_dest(I, j)
         I->dest[j] = dests[j];
   }
}

static void
bi_emit_cached_split_i32(bi_builder *b, bi_index vec, unsigned n)
{
   bi_index dests[4] = {bi_null(), bi_null(), bi_null(), bi_null()};
   bi_emit_split_i32(b, dests, vec, n);
   bi_cache_collect(b, vec, dests, n);
}

/*
 * Emit and cache a split for a vector of a given bitsize. The vector may not be
 * composed of 32-bit words, but it will be split at 32-bit word boundaries.
 */
static void
bi_emit_cached_split(bi_builder *b, bi_index vec, unsigned bits)
{
   bi_emit_cached_split_i32(b, vec, DIV_ROUND_UP(bits, 32));
}

static void
bi_split_def(bi_builder *b, nir_def *def)
{
   bi_emit_cached_split(b, bi_def_index(def),
                        def->bit_size * def->num_components);
}

static bi_instr *
bi_emit_collect_to(bi_builder *b, bi_index dst, bi_index *chan, unsigned n)
{
   /* Special case: COLLECT of a single value is a scalar move */
   if (n == 1)
      return bi_mov_i32_to(b, dst, chan[0]);

   bi_instr *I = bi_collect_i32_to(b, dst, n);

   bi_foreach_src(I, i)
      I->src[i] = chan[i];

   bi_cache_collect(b, dst, chan, n);
   return I;
}

static bi_instr *
bi_collect_v2i32_to(bi_builder *b, bi_index dst, bi_index s0, bi_index s1)
{
   return bi_emit_collect_to(b, dst, (bi_index[]){s0, s1}, 2);
}

static bi_instr *
bi_collect_v3i32_to(bi_builder *b, bi_index dst, bi_index s0, bi_index s1,
                    bi_index s2)
{
   return bi_emit_collect_to(b, dst, (bi_index[]){s0, s1, s2}, 3);
}

static bi_index
bi_collect_v2i32(bi_builder *b, bi_index s0, bi_index s1)
{
   bi_index dst = bi_temp(b->shader);
   bi_collect_v2i32_to(b, dst, s0, s1);
   return dst;
}

static inline bi_instr *
bi_f32_to_f16_to(bi_builder *b, bi_index dest, bi_index src)
{
   /* Use V2F32_TO_V2F16 on Bifrost, FADD otherwise */
   if (b->shader->arch < 9)
      return bi_v2f32_to_v2f16_to(b, dest, src, src);

   assert(dest.swizzle != BI_SWIZZLE_H01);

   /* FADD with -0 and force convertion to F16 on Valhall and later. Negative
    * zero is used to preserve signed zero, since 0 + -0 = 0 and -0 + -0 = -0.
    */
   bi_instr *I =  bi_fadd_f32_to(b, dest, src, bi_imm_f32(-0.0));

   /* The builder defaults to 32-bit rounding mode */
   I->round = bi_round_mode(b->shader, 16);

   return I;
}

static bi_index
bi_varying_src0_for_barycentric(bi_builder *b, nir_intrinsic_instr *intr)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_barycentric_centroid:
      return bi_preload(b, BI_PRELOAD_CENTROID_ID);
   case nir_intrinsic_load_barycentric_sample:
      return bi_preload(b, BI_PRELOAD_SAMPLE_ID);

   /* Need to put the sample ID in the top 16-bits */
   case nir_intrinsic_load_barycentric_at_sample:
      return bi_mkvec_v2i16(b, bi_half(bi_dontcare(b), false),
                            bi_half(bi_src_index(&intr->src[0]), false));

   /* Interpret as 8:8 signed fixed point positions in pixels along X and
    * Y axes respectively, relative to top-left of pixel. In NIR, (0, 0)
    * is the center of the pixel so we first fixup and then convert. For
    * fp16 input:
    *
    * f2i16(((x, y) + (0.5, 0.5)) * 2**8) =
    * f2i16((256 * (x, y)) + (128, 128)) =
    * V2F16_TO_V2S16(FMA.v2f16((x, y), #256, #128))
    *
    * For fp32 input, that lacks enough precision for MSAA 16x, but the
    * idea is the same. FIXME: still doesn't pass
    */
   case nir_intrinsic_load_barycentric_at_offset: {
      bi_index offset = bi_src_index(&intr->src[0]);
      bi_index f16 = bi_null();
      unsigned sz = nir_src_bit_size(intr->src[0]);

      if (sz == 16) {
         f16 = bi_fma_v2f16(b, offset, bi_imm_f16(256.0), bi_imm_f16(128.0));
      } else {
         assert(sz == 32);
         bi_index f[2];
         for (unsigned i = 0; i < 2; ++i) {
            f[i] =
               bi_fadd_rscale_f32(b, bi_extract(b, offset, i), bi_imm_f32(0.5),
                                  bi_imm_u32(8), BI_SPECIAL_NONE);
         }

         /* On v11+, V2F32_TO_V2F16 is gone */
         if (b->shader->arch >= 11) {
            bi_index tmp[2];

            for (int i = 0; i < 2; i++) {
               tmp[i] = bi_half(bi_temp(b->shader), false);
               bi_f32_to_f16_to(b, tmp[i], f[i]);
            }

            f16 = bi_mkvec_v2i16(b, tmp[0], tmp[1]);
         } else {
            f16 = bi_v2f32_to_v2f16(b, f[0], f[1]);
         }
      }

      /* v11 removed V2F16_TO_V2S16 */
      if (b->shader->arch >= 11) {
         bi_index f[2];

         for (int i = 0; i < 2; i++) {
            bi_index tmp = bi_half(f16, i == 1);
            tmp = bi_f16_to_f32(b, tmp);
            tmp = bi_f32_to_s32(b, tmp);
            f[i] = bi_half(tmp, false);
         }

         return bi_mkvec_v2i16(b, f[0], f[1]);
      } else {
         return bi_v2f16_to_v2s16(b, f16);
      }
   }

   case nir_intrinsic_load_barycentric_pixel:
   default:
      return b->shader->arch >= 9 ? bi_preload(b, BI_PRELOAD_CENTROID_ID)
                                  : bi_dontcare(b);
   }
}

static enum bi_sample
bi_interp_for_intrinsic(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_load_barycentric_centroid:
      return BI_SAMPLE_CENTROID;
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_at_sample:
      return BI_SAMPLE_SAMPLE;
   case nir_intrinsic_load_barycentric_at_offset:
      return BI_SAMPLE_EXPLICIT;
   case nir_intrinsic_load_barycentric_pixel:
   default:
      return BI_SAMPLE_CENTER;
   }
}

/* auto, 64-bit omitted */
static enum bi_register_format
bi_reg_fmt_for_nir(nir_alu_type T)
{
   switch (T) {
   case nir_type_float16:
      return BI_REGISTER_FORMAT_F16;
   case nir_type_float32:
      return BI_REGISTER_FORMAT_F32;
   case nir_type_int16:
      return BI_REGISTER_FORMAT_S16;
   case nir_type_uint16:
      return BI_REGISTER_FORMAT_U16;
   case nir_type_int32:
      return BI_REGISTER_FORMAT_S32;
   case nir_type_uint32:
      return BI_REGISTER_FORMAT_U32;
   default:
      UNREACHABLE("Invalid type for register format");
   }
}

static bool
va_is_valid_const_narrow_index(bi_index idx)
{
   if (idx.type != BI_INDEX_CONSTANT)
      return false;

   unsigned index = pan_res_handle_get_index(idx.value);
   unsigned table_index = pan_res_handle_get_table(idx.value);

   return index < 1024 && va_is_valid_const_table(table_index);
}

/* Checks if the _IMM variant of an intrinsic can be used, returning in imm the
 * immediate to be used (which applies even if _IMM can't be used) */

static bool
bi_is_intr_immediate(nir_intrinsic_instr *instr, unsigned *immediate,
                     unsigned max)
{
   nir_src *offset = nir_get_io_offset_src(instr);

   if (!nir_src_is_const(*offset))
      return false;

   *immediate = nir_intrinsic_base(instr) + nir_src_as_uint(*offset);
   return (*immediate) < max;
}

static bool
bi_is_imm_desc_handle(bi_builder *b, nir_intrinsic_instr *instr,
                      uint32_t *immediate, unsigned max)
{
   nir_src *offset = nir_get_io_offset_src(instr);

   if (!nir_src_is_const(*offset))
      return false;

   if (b->shader->arch >= 9) {
      uint32_t res_handle =
         nir_intrinsic_base(instr) + nir_src_as_uint(*offset);
      uint32_t table_index = pan_res_handle_get_table(res_handle);
      uint32_t res_index = pan_res_handle_get_index(res_handle);

      *immediate = res_handle;

      if (!va_is_valid_const_table(table_index) || res_index >= max)
         return false;

      return true;
   }

   return bi_is_intr_immediate(instr, immediate, max);
}

static bool
bi_is_imm_var_desc_handle(bi_builder *b, nir_intrinsic_instr *instr,
                          uint32_t *immediate)
{
   unsigned max = b->shader->arch >= 9 ? 256 : 20;

   return bi_is_imm_desc_handle(b, instr, immediate, max);
}

bi_instr *bi_make_vec_to(bi_builder *b, bi_index final_dst, bi_index *src,
                         unsigned *channel, unsigned count, unsigned bitsize);

/* Bifrost's load instructions lack a component offset despite operating in
 * terms of vec4 slots. Usually I/O vectorization avoids nonzero components,
 * but they may be unavoidable with separate shaders in use. To solve this, we
 * lower to a larger load and an explicit copy of the desired components. */

static void
bi_copy_component(bi_builder *b, nir_intrinsic_instr *instr, bi_index tmp)
{
   unsigned component = nir_intrinsic_component(instr);
   unsigned nr = instr->num_components;
   unsigned total = nr + component;
   unsigned bitsize = instr->def.bit_size;

   assert(total <= 4 && "should be vec4");
   bi_emit_cached_split(b, tmp, total * bitsize);

   if (component == 0)
      return;

   bi_index srcs[] = {tmp, tmp, tmp};
   unsigned channels[] = {component, component + 1, component + 2};

   bi_make_vec_to(b, bi_def_index(&instr->def), srcs, channels, nr,
                  instr->def.bit_size);
}

static void
bi_emit_load_attr(bi_builder *b, nir_intrinsic_instr *instr)
{
   bi_index vertex_id =
      instr->intrinsic == nir_intrinsic_load_attribute_pan ?
         bi_src_index(&instr->src[0]) :
         bi_vertex_id(b);
   bi_index instance_id =
      instr->intrinsic == nir_intrinsic_load_attribute_pan ?
         bi_src_index(&instr->src[1]) :
         bi_instance_id(b);

   /* Disregard the signedness of an integer, since loading 32-bits into a
    * 32-bit register should be bit exact so should not incur any clamping.
    *
    * If we are reading as a u32, then it must be paired with an integer (u32 or
    * s32) source, so use .auto32 to disregard.
    */
   nir_alu_type T = nir_intrinsic_dest_type(instr);
   enum bi_register_format regfmt = BI_REGISTER_FORMAT_AUTO;
   switch (T) {
      case nir_type_uint32:
      case nir_type_int32:
         regfmt = BI_REGISTER_FORMAT_AUTO;
         break;
      case nir_type_float32:
         regfmt = BI_REGISTER_FORMAT_F32;
         break;
      case nir_type_uint16:
         regfmt = BI_REGISTER_FORMAT_U16;
         break;
      case nir_type_int16:
         regfmt = BI_REGISTER_FORMAT_S16;
         break;
      case nir_type_float16:
         regfmt = BI_REGISTER_FORMAT_F16;
         break;
      default:
         assert("unsupported attribute type" && false);
   }

   nir_src *offset = nir_get_io_offset_src(instr);
   unsigned component = nir_intrinsic_component(instr);
   enum bi_vecsize vecsize = (instr->num_components + component - 1);
   unsigned imm_index = 0;
   unsigned base = nir_intrinsic_base(instr);
   bool constant = nir_src_is_const(*offset);
   bool immediate = bi_is_imm_desc_handle(b, instr, &imm_index, 16);
   bi_index dest =
      (component == 0) ? bi_def_index(&instr->def) : bi_temp(b->shader);
   bi_instr *I;

   if (immediate) {
      I = bi_ld_attr_imm_to(b, dest, vertex_id, instance_id, regfmt,
                            vecsize, pan_res_handle_get_index(imm_index));

      if (b->shader->arch >= 9)
         I->table = va_res_fold_table_idx(pan_res_handle_get_table(base));
   } else {
      bi_index idx = bi_src_index(&instr->src[0]);

      if (constant)
         idx = bi_imm_u32(imm_index);
      else if (base != 0)
         idx = bi_iadd_u32(b, idx, bi_imm_u32(base), false);

      I = bi_ld_attr_to(b, dest, vertex_id, instance_id, idx, regfmt, vecsize);
   }

   bi_copy_component(b, instr, dest);
}

static void
bi_emit_lea_attr(bi_builder *b, nir_intrinsic_instr *intr)
{
   assert(intr->intrinsic == nir_intrinsic_lea_attr_pan);
   const nir_alu_type src_fmt = nir_intrinsic_src_type(intr);

   if (b->shader->arch < 9 &&
       b->shader->idvs == BI_IDVS_POSITION &&
       nir_intrinsic_desc_set(intr) == BI_TABLE_NONE &&
       nir_src_is_zero(intr->src[0])) {
      /* Bifrost position shaders have a fast path */
      assert(nir_src_is_zero(intr->src[0]));
      assert(src_fmt == nir_type_float32);
      unsigned regfmt = BI_REGISTER_FORMAT_F32;
      unsigned identity = (b->shader->arch == 6) ? 0x688 : 0;
      unsigned snap4 = 0x5E;
      uint32_t format = identity | (snap4 << 12) | (regfmt << 24);
      bi_collect_v3i32_to(b, bi_def_index(&intr->def),
                          bi_preload(b, BI_PRELOAD_POS_RESULT_PTR_LO),
                          bi_preload(b, BI_PRELOAD_POS_RESULT_PTR_HI),
                          bi_imm_u32(format));
      return;
   }

   bi_index vertex_id = bi_src_index(&intr->src[1]);
   bi_index instance_id = bi_src_index(&intr->src[2]);
   enum bi_register_format regfmt =
      src_fmt == 32 ? BI_REGISTER_FORMAT_AUTO
                    : bi_reg_fmt_for_nir(src_fmt);

   /* Check if the index can fit in LEA_ATTR_IMM */
   uint32_t imm_res = 0;
   bool use_imm_form = false;
   if (nir_src_is_const(intr->src[0])) {
      imm_res = nir_src_as_uint(intr->src[0]);
      use_imm_form = pan_res_handle_get_index(imm_res) < 0x10;
   }

   bi_index address = bi_def_index(&intr->def);
   if (use_imm_form) {
      bi_instr *I = bi_lea_attr_imm_to(b, address, vertex_id, instance_id,
                                       regfmt,
                                       pan_res_handle_get_index(imm_res));
      if (b->shader->arch >= 9)
         I->table = va_res_fold_table_idx(pan_res_handle_get_table(imm_res));
      else
         I->table = nir_intrinsic_desc_set(intr);
   } else {
      bi_index res = bi_src_index(&intr->src[0]);
      bi_instr *I = bi_lea_attr_to(b, address, vertex_id, instance_id, res,
                                   regfmt);
      if (b->shader->arch < 9)
         I->table = nir_intrinsic_desc_set(intr);
   }
   bi_split_def(b, &intr->def);
}

static void
bi_emit_lea_buf(bi_builder *b, nir_intrinsic_instr *intr)
{
   assert(intr->intrinsic == nir_intrinsic_lea_buf_pan);
   assert(b->shader->arch >= 9);
   bi_index index = bi_src_index(&intr->src[1]);

   uint32_t imm_res;
   bool use_imm_form = false;
   if (nir_src_is_const(intr->src[0])) {
      imm_res = nir_src_as_uint(intr->src[0]);
      uint32_t table_index = pan_res_handle_get_table(imm_res);
      uint32_t res_index = pan_res_handle_get_index(imm_res);
      use_imm_form = va_is_valid_const_table(table_index) && res_index < 256;
   }

   bi_index address = bi_def_index(&intr->def);
   if (use_imm_form) {
      bi_instr *I = bi_lea_buf_imm_to(b, address, index);
      I->table = va_res_fold_table_idx(pan_res_handle_get_table(imm_res));
      I->index = pan_res_handle_get_index(imm_res);
   } else {
      bi_index res = bi_src_index(&intr->src[0]);
      bi_lea_buf_to(b, address, index, res);
   }
   bi_split_def(b, &intr->def);
}

static void
bi_emit_load_var(bi_builder *b, nir_intrinsic_instr *intr)
{
   assert(intr->intrinsic == nir_intrinsic_load_var_pan ||
          intr->intrinsic == nir_intrinsic_load_var_flat_pan);
   const bool flat = intr->intrinsic == nir_intrinsic_load_var_flat_pan;

   enum bi_sample sample = BI_SAMPLE_CENTER;
   bi_index src0 = bi_null();

   const nir_alu_type dest_type = nir_intrinsic_dest_type(intr);
   ASSERTED const nir_alu_type base_type = nir_alu_type_get_base_type(dest_type);
   const unsigned sz = nir_alu_type_get_type_size(dest_type);
   assert(intr->def.bit_size == sz);
   enum bi_register_format regfmt;

   if (flat) {
      b->shader->info.bifrost->uses_flat_shading = true;

      /* Flat loading with i16/u16 is not encodable */
      assert(base_type == nir_type_float || sz == 32);
      regfmt = bi_reg_fmt_for_nir(dest_type);
   } else {
      nir_intrinsic_instr *bary = nir_src_as_intrinsic(intr->src[1]);
      sample = bi_interp_for_intrinsic(bary->intrinsic);
      src0 = bi_varying_src0_for_barycentric(b, bary);

      /* Smooth ints don't exist */
      assert(base_type == nir_type_float);
      regfmt = (sz == 16) ? BI_REGISTER_FORMAT_F16 : BI_REGISTER_FORMAT_F32;
   }

   const enum bi_vecsize vecsize = intr->num_components - 1;
   bi_index dest = bi_def_index(&intr->def);

   uint32_t imm_res = 0;
   bool use_imm_form = false;
   if (nir_src_is_const(intr->src[0])) {
      imm_res = nir_src_as_uint(intr->src[0]);
      if (b->shader->arch >= 9) {
         uint32_t table_index = pan_res_handle_get_table(imm_res);
         uint32_t res_index = pan_res_handle_get_index(imm_res);
         use_imm_form = va_is_valid_const_table(table_index) &&
                        res_index < 256;
      } else {
         use_imm_form = imm_res < 20;
      }
   }

   if (use_imm_form) {
      bi_instr *I;
      if (flat) {
         I = bi_ld_var_flat_imm_to(b, dest, BI_FUNCTION_NONE, regfmt, vecsize,
                                   pan_res_handle_get_index(imm_res));
      } else {
         I = bi_ld_var_imm_to(b, dest, src0, regfmt, sample, BI_UPDATE_STORE,
                              vecsize, pan_res_handle_get_index(imm_res));
      }

      if (b->shader->arch >= 9)
         I->table = va_res_fold_table_idx(pan_res_handle_get_table(imm_res));
   } else {
      bi_index res = bi_src_index(&intr->src[0]);
      if (flat) {
         bi_ld_var_flat_to(b, dest, res, BI_FUNCTION_NONE, regfmt, vecsize);
      } else {
         bi_ld_var_to(b, dest, src0, res, regfmt, sample,
                      BI_UPDATE_STORE, vecsize);
      }
   }
   bi_split_def(b, &intr->def);
}

static void
bi_emit_load_var_buf(bi_builder *b, nir_intrinsic_instr *intr)
{
   assert(intr->intrinsic == nir_intrinsic_load_var_buf_pan ||
          intr->intrinsic == nir_intrinsic_load_var_buf_flat_pan);

   /* These are only available on Valhall+ */
   assert(b->shader->arch >= 9);

   const bool flat = intr->intrinsic == nir_intrinsic_load_var_buf_flat_pan;
   const nir_alu_type src_type = nir_intrinsic_src_type(intr);
   ASSERTED const nir_alu_type base_type = nir_alu_type_get_base_type(src_type);
   const nir_alu_type src_sz = nir_alu_type_get_type_size(src_type);
   const unsigned sz = intr->def.bit_size;
   assert(src_sz == 16 || src_sz == 32);
   assert(sz == 16 || sz == 32);

   enum bi_sample sample = BI_SAMPLE_CENTER;
   bi_index src0 = bi_null();

   if (flat) {
      /* The source is not used for flat loads but we still need to encode
       * something.
       */
      src0 = bi_zero();

      /* Gather info as we go */
      b->shader->info.bifrost->uses_flat_shading = true;
   } else {
      nir_intrinsic_instr *bary = nir_src_as_intrinsic(intr->src[1]);
      sample = bi_interp_for_intrinsic(bary->intrinsic);
      src0 = bi_varying_src0_for_barycentric(b, bary);
   }

   enum bi_source_format source_format;
   enum bi_register_format regfmt;
   if (flat) {
      /* conversion MUST be a noop for int varyings to work correctly */
      assert(base_type == nir_type_float || src_sz == sz);
      /* integer regfmt are not supported by LD_VAR_BUF, but using float
       * src_types for integers is okay if the source_format is flat and uses
       * the same bit size.  The conversion is a no-op. */
      regfmt = (sz == 16) ? BI_REGISTER_FORMAT_F16 : BI_REGISTER_FORMAT_F32;
      source_format = (src_sz == 16) ? BI_SOURCE_FORMAT_FLAT16 :
                                       BI_SOURCE_FORMAT_FLAT32;
   } else {
      /* Smooth ints don't exist */
      assert(base_type == nir_type_float);
      regfmt = (sz == 16) ? BI_REGISTER_FORMAT_F16 : BI_REGISTER_FORMAT_F32;
      source_format = (src_sz == 16) ? BI_SOURCE_FORMAT_F16 :
                                       BI_SOURCE_FORMAT_F32;
   }

   const enum bi_vecsize vecsize = intr->num_components - 1;
   bi_index dest = bi_def_index(&intr->def);

   uint32_t imm_offset = 0;
   bool use_imm_form = false;
   if (nir_src_is_const(intr->src[0])) {
      imm_offset = nir_src_as_uint(intr->src[0]);
      assert(imm_offset < pan_ld_var_buf_off_size(b->shader->arch));

      use_imm_form = true;
   }

   if (use_imm_form) {
      bi_ld_var_buf_imm_to(b, sz, dest, src0, regfmt, sample, source_format,
                           BI_UPDATE_STORE, vecsize, imm_offset);
   } else {
      bi_index offset = bi_src_index(&intr->src[0]);
      bi_ld_var_buf_to(b, sz, dest, src0, offset, regfmt, sample,
                       source_format, BI_UPDATE_STORE, vecsize);
   }
   bi_split_def(b, &intr->def);
}

bi_instr *
bi_make_vec_to(bi_builder *b, bi_index dst, bi_index *src, unsigned *channel,
               unsigned count, unsigned bitsize)
{
   assert(DIV_ROUND_UP(count * bitsize, 32) <= BI_MAX_SRCS &&
          "unnecessarily large vector should have been lowered");

   assert(count <= BI_MAX_VEC);
   bi_index srcs[BI_MAX_VEC];

   if (bitsize == 64) {
      for (unsigned i = 0; i < count; i++) {
         const unsigned c = channel ? channel[i] : 0;
         srcs[i * 2 + 0] = bi_extract(b, src[i], c * 2 + 0);
         srcs[i * 2 + 1] = bi_extract(b, src[i], c * 2 + 1);
      }
      return bi_emit_collect_to(b, dst, srcs, count * 2);
   } else if (bitsize == 32) {
      for (unsigned i = 0; i < count; i++) {
         const unsigned c = channel ? channel[i] : 0;
         srcs[i] = bi_extract(b, src[i], c);
      }
      return bi_emit_collect_to(b, dst, srcs, count);
   } else if (bitsize == 16) {
      for (unsigned i = 0; i < count; i++) {
         const unsigned c = channel ? channel[i] : 0;
         srcs[i] = bi_half(bi_extract(b, src[i], c >> 1), c & 1);
      }

      for (unsigned i = count; i < align(count, 2); i++)
         srcs[i] = bi_imm_u16(0);

      for (unsigned i = 0; i < count; i += 2)
         srcs[i / 2] =  bi_mkvec_v2i16(b, srcs[i], srcs[i + 1]);

      return bi_emit_collect_to(b, dst, srcs, DIV_ROUND_UP(count, 2));
   } else if (bitsize == 8) {
      for (unsigned i = 0; i < count; i++) {
         const unsigned c = channel ? channel[i] : 0;
         srcs[i] = bi_byte(bi_extract(b, src[i], c >> 2), c & 3);
      }

      for (unsigned i = count; i < align(count, 4); i++)
         srcs[i] = bi_imm_u8(0);

      for (unsigned i = 0; i < count; i += 4) {
         srcs[i / 4] =  bi_mkvec_v4i8(b, srcs[i], srcs[i + 1],
                                      srcs[i + 2], srcs[i + 3]);
      }

      return bi_emit_collect_to(b, dst, srcs, DIV_ROUND_UP(count, 4));
   } else {
      UNREACHABLE("Unsupported bit size");
   }
}

static inline bi_instr *
bi_load_ubo_to(bi_builder *b, unsigned bitsize, bi_index dest0, bi_index src0,
               bi_index src1)
{
   bi_instr *I;

   if (b->shader->arch >= 9) {
      I = bi_ld_pka_to(b, bitsize, dest0, src0, src1);
      I->seg = BI_SEG_UBO;
   } else {
      I = bi_load_to(b, bitsize, dest0, src0, src1, BI_SEG_UBO, 0);
   }

   bi_emit_cached_split(b, dest0, bitsize);
   return I;
}

static void
bi_load_sample_id_to(bi_builder *b, bi_index dst)
{
   /* r61[16:23] contains the sampleID, mask it out. Upper bits
    * seem to read garbage (despite being architecturally defined
    * as zero), so use a 5-bit mask instead of 8-bits */

   bi_rshift_and_i32_to(b, dst, bi_preload(b, BI_PRELOAD_SAMPLE_ID),
                        bi_imm_u32(0x1f), bi_imm_u8(16), false);
}

static bi_index
bi_load_sample_id(bi_builder *b)
{
   bi_index sample_id = bi_temp(b->shader);
   bi_load_sample_id_to(b, sample_id);
   return sample_id;
}

static bi_index
bi_pixel_indices(bi_builder *b, unsigned rt, unsigned sample)
{
   /* We want to load the current pixel. */
   struct bifrost_pixel_indices pix = {
      .y = BIFROST_CURRENT_PIXEL,
      .rt = rt,
      .sample = sample,
   };

   uint32_t indices_u32 = 0;
   memcpy(&indices_u32, &pix, sizeof(indices_u32));
   return bi_imm_u32(indices_u32);
}

/* Source color is passed through r0-r3, or r4-r7 for the second source when
 * dual-source blending. Preload the corresponding vector.
 */
static void
bi_emit_load_blend_input(bi_builder *b, nir_intrinsic_instr *instr)
{
   nir_io_semantics sem = nir_intrinsic_io_semantics(instr);
   unsigned size = nir_alu_type_get_type_size(nir_intrinsic_dest_type(instr));
   assert(size == 16 || size == 32);

   bi_index srcs[4];
   switch (sem.dual_source_blend_index) {
   case 0:
      srcs[0] = bi_preload(b, BI_PRELOAD_BLEND_SRC0_C0);
      srcs[1] = bi_preload(b, BI_PRELOAD_BLEND_SRC0_C1);
      srcs[2] = bi_preload(b, BI_PRELOAD_BLEND_SRC0_C2);
      srcs[3] = bi_preload(b, BI_PRELOAD_BLEND_SRC0_C3);
      break;
   case 1:
      srcs[0] = bi_preload(b, BI_PRELOAD_BLEND_SRC1_C0);
      srcs[1] = bi_preload(b, BI_PRELOAD_BLEND_SRC1_C1);
      srcs[2] = bi_preload(b, BI_PRELOAD_BLEND_SRC1_C2);
      srcs[3] = bi_preload(b, BI_PRELOAD_BLEND_SRC1_C3);
      break;
   }

   bi_emit_collect_to(b, bi_def_index(&instr->def), srcs, size == 32 ? 4 : 2);
}

static bi_index
bi_src_color_vec4(bi_builder *b, nir_src *src, nir_alu_type T)
{
   unsigned num_components = nir_src_num_components(*src);
   bi_index base = bi_src_index(src);

   /* short-circuit the common case */
   if (num_components == 4)
      return base;

   unsigned size = nir_alu_type_get_type_size(T);
   assert(size == 16 || size == 32);

   bi_index src_vals[4];

   unsigned i;
   for (i = 0; i < num_components; i++)
      src_vals[i] = bi_extract(b, base, i);

   for (; i < 3; i++)
      src_vals[i] = (size == 16) ? bi_imm_f16(0.0) : bi_imm_f32(0.0);
   src_vals[3] = (size == 16) ? bi_imm_f16(1.0) : bi_imm_f32(1.0);
   bi_index temp = bi_temp(b->shader);
   bi_make_vec_to(b, temp, src_vals, NULL, 4, size);
   return temp;
}

static unsigned
bi_is_zs(unsigned location)
{
   return location == FRAG_RESULT_DEPTH || location == FRAG_RESULT_STENCIL;
}

static void
bi_emit_load_ubo(bi_builder *b, nir_intrinsic_instr *instr)
{
   nir_src *offset = nir_get_io_offset_src(instr);

   bool offset_is_const = nir_src_is_const(*offset);
   bi_index dyn_offset = bi_src_index(offset);
   uint32_t const_offset = offset_is_const ? nir_src_as_uint(*offset) : 0;

   bi_load_ubo_to(b, instr->num_components * instr->def.bit_size,
                  bi_def_index(&instr->def),
                  offset_is_const ? bi_imm_u32(const_offset) : dyn_offset,
                  bi_src_index(&instr->src[0]));
}

static void
bi_emit_load_push_constant(bi_builder *b, nir_intrinsic_instr *instr)
{
   assert(!b->shader->inputs->pushable_ubos && "can't mix push constant forms");

   nir_src *offset = &instr->src[0];
   assert(!nir_intrinsic_base(instr) && "base must be zero");
   assert(!nir_intrinsic_range(instr) && "range must be zero");
   assert(nir_src_is_const(*offset) && "no indirect push constants");
   uint32_t base = nir_src_as_uint(*offset);
   assert((base & 3) == 0 && "unaligned push constants");

   unsigned bits = instr->def.bit_size * instr->def.num_components;

   unsigned n = DIV_ROUND_UP(bits, 32);
   assert(n <= 4);
   bi_index channels[4] = {bi_null()};

   for (unsigned i = 0; i < n; ++i) {
      unsigned word = (base >> 2) + i;

      channels[i] = bi_fau(BIR_FAU_UNIFORM | (word >> 1), word & 1);
   }

   bi_emit_collect_to(b, bi_def_index(&instr->def), channels, n);

   /* Update push->count to report the highest push constant word being accessed
    * by this shader.
    */
   b->shader->info.push->count =
      MAX2((base / 4) + n, b->shader->info.push->count);
}

/* Split a 32/64-bit address into low and high parts. 64-bit addresses are
 * checked to be split and cached. 64-bit address happens on `store_scratch`
 * produced by `nir_lower_vars_to_scratch`
 */

static void
bi_addr_split(bi_builder *b, nir_src *src, bi_index *addr_lo, bi_index *addr_hi)
{
   bi_index addr = bi_src_index(src);
   if (nir_src_bit_size(*src) == 32) {
      *addr_lo = addr;
      *addr_hi = bi_zero();
      return;
   }
   bi_index *components = _mesa_hash_table_u64_search(b->shader->allocated_vec,
                                                      bi_index_to_key(addr));
   if (components == NULL) {
      bi_emit_cached_split(b, addr, 64);
   }
   *addr_lo = bi_extract(b, addr, 0);
   *addr_hi = bi_extract(b, addr, 1);
}

static void
bi_handle_segment(bi_builder *b, bi_index *addr_lo, bi_index *addr_hi,
                  enum bi_seg seg, int16_t *offset)
{
   /* Not needed on Bifrost or for global accesses */
   if (b->shader->arch < 9 || seg == BI_SEG_NONE)
      return;

   /* There is no segment modifier on Valhall. Instead, we need to
    * emit the arithmetic ourselves. We do have an offset
    * available, which saves an instruction for constant offsets.
    */
   bool wls = (seg == BI_SEG_WLS);
   assert(wls || (seg == BI_SEG_TL));

   enum bir_fau fau = wls ? BIR_FAU_WLS_PTR : BIR_FAU_TLS_PTR;

   bi_index base_lo = bi_fau(fau, false);

   if (offset && addr_lo->type == BI_INDEX_CONSTANT &&
       addr_lo->value == (int16_t)addr_lo->value) {
      *offset = addr_lo->value;
      *addr_lo = base_lo;
   } else {
      *addr_lo = bi_iadd_u32(b, base_lo, *addr_lo, false);
   }

   /* Do not allow overflow for WLS or TLS */
   *addr_hi = bi_fau(fau, true);
}

static void
bi_emit_load(bi_builder *b, nir_intrinsic_instr *instr, enum bi_seg seg,
             enum va_memory_access mem_access)
{
   int16_t offset = 0;
   unsigned bits = instr->num_components * instr->def.bit_size;
   bi_index dest = bi_def_index(&instr->def);
   bi_index addr_lo, addr_hi;
   bi_addr_split(b, &instr->src[0], &addr_lo, &addr_hi);

   bi_handle_segment(b, &addr_lo, &addr_hi, seg, &offset);

   bi_instr *I = bi_load_to(b, bits, dest, addr_lo, addr_hi, seg, offset);
   I->mem_access = mem_access;
   bi_emit_cached_split(b, dest, bits);
}

static bi_instr *
bi_emit_store(bi_builder *b, nir_intrinsic_instr *instr, enum bi_seg seg,
              enum va_memory_access mem_access)
{
   /* Require contiguous masks, gauranteed by nir_lower_wrmasks */
   assert(nir_intrinsic_write_mask(instr) ==
          BITFIELD_MASK(instr->num_components));

   int16_t offset = 0;
   bi_index addr_lo, addr_hi;
   bi_addr_split(b, &instr->src[1], &addr_lo, &addr_hi);

   bi_handle_segment(b, &addr_lo, &addr_hi, seg, &offset);

   bi_instr *I =
      bi_store(b, instr->num_components * nir_src_bit_size(instr->src[0]),
               bi_src_index(&instr->src[0]), addr_lo, addr_hi, seg, offset);
   I->mem_access = mem_access;
   return I;
}

/* Exchanges the staging register with memory */

static void
bi_emit_axchg_to(bi_builder *b, bi_index dst, bi_index addr, nir_src *arg,
                 enum bi_seg seg)
{
   assert(seg == BI_SEG_NONE || seg == BI_SEG_WLS);

   unsigned sz = nir_src_bit_size(*arg);
   assert(sz == 32 || sz == 64);

   bi_index data = bi_src_index(arg);

   bi_index addr_hi = (seg == BI_SEG_WLS) ? bi_zero() : bi_extract(b, addr, 1);

   if (b->shader->arch >= 9)
      bi_handle_segment(b, &addr, &addr_hi, seg, NULL);
   else if (seg == BI_SEG_WLS)
      addr_hi = bi_zero();

   bi_axchg_to(b, sz, dst, data, bi_extract(b, addr, 0), addr_hi, seg);
}

/* Exchanges the second staging register with memory if comparison with first
 * staging register passes */

static void
bi_emit_acmpxchg_to(bi_builder *b, bi_index dst, bi_index addr, nir_src *arg_1,
                    nir_src *arg_2, enum bi_seg seg)
{
   assert(seg == BI_SEG_NONE || seg == BI_SEG_WLS);

   /* hardware is swapped from NIR */
   bi_index src0 = bi_src_index(arg_2);
   bi_index src1 = bi_src_index(arg_1);

   unsigned sz = nir_src_bit_size(*arg_1);
   assert(sz == 32 || sz == 64);

   bi_index data_words[] = {
      bi_extract(b, src0, 0),
      sz == 32 ? bi_extract(b, src1, 0) : bi_extract(b, src0, 1),

      /* 64-bit */
      bi_extract(b, src1, 0),
      sz == 32 ? bi_extract(b, src1, 0) : bi_extract(b, src1, 1),
   };

   bi_index in = bi_temp(b->shader);
   bi_emit_collect_to(b, in, data_words, 2 * (sz / 32));
   bi_index addr_hi = (seg == BI_SEG_WLS) ? bi_zero() : bi_extract(b, addr, 1);

   if (b->shader->arch >= 9)
      bi_handle_segment(b, &addr, &addr_hi, seg, NULL);
   else if (seg == BI_SEG_WLS)
      addr_hi = bi_zero();

   bi_index out = bi_acmpxchg(b, sz, in, bi_extract(b, addr, 0), addr_hi, seg);
   bi_emit_cached_split(b, out, sz);

   bi_index inout_words[] = {bi_extract(b, out, 0),
                             sz == 64 ? bi_extract(b, out, 1) : bi_null()};

   bi_make_vec_to(b, dst, inout_words, NULL, sz / 32, 32);
}

static enum bi_atom_opc
bi_atom_opc_for_nir(nir_atomic_op op)
{
   /* clang-format off */
   switch (op) {
   case nir_atomic_op_iadd: return BI_ATOM_OPC_AADD;
   case nir_atomic_op_imin: return BI_ATOM_OPC_ASMIN;
   case nir_atomic_op_umin: return BI_ATOM_OPC_AUMIN;
   case nir_atomic_op_imax: return BI_ATOM_OPC_ASMAX;
   case nir_atomic_op_umax: return BI_ATOM_OPC_AUMAX;
   case nir_atomic_op_iand: return BI_ATOM_OPC_AAND;
   case nir_atomic_op_ior:  return BI_ATOM_OPC_AOR;
   case nir_atomic_op_ixor: return BI_ATOM_OPC_AXOR;
   case nir_atomic_op_xchg: return BI_ATOM_OPC_AXCHG;
   case nir_atomic_op_cmpxchg: return BI_ATOM_OPC_AXCHG;
   default: UNREACHABLE("Unexpected computational atomic");
   }
   /* clang-format on */
}

/* Optimized unary atomics are available with an implied #1 argument */

static bool
bi_promote_atom_c1(enum bi_atom_opc op, bi_index arg, enum bi_atom_opc *out)
{
   /* Check we have a compatible constant */
   if (arg.type != BI_INDEX_CONSTANT)
      return false;

   if (!(arg.value == 1 || (arg.value == -1 && op == BI_ATOM_OPC_AADD)))
      return false;

   /* Check for a compatible operation */
   switch (op) {
   case BI_ATOM_OPC_AADD:
      *out = (arg.value == 1) ? BI_ATOM_OPC_AINC : BI_ATOM_OPC_ADEC;
      return true;
   case BI_ATOM_OPC_ASMAX:
      *out = BI_ATOM_OPC_ASMAX1;
      return true;
   case BI_ATOM_OPC_AUMAX:
      *out = BI_ATOM_OPC_AUMAX1;
      return true;
   case BI_ATOM_OPC_AOR:
      *out = BI_ATOM_OPC_AOR1;
      return true;
   default:
      return false;
   }
}

/*
 * Coordinates are 16-bit integers in Bifrost but 32-bit in NIR. We need to
 * translate between these forms (with MKVEC.v2i16).
 *
 * Aditionally on Valhall, cube maps in the attribute pipe are treated as 2D
 * arrays.  For uniform handling, we also treat 3D textures like 2D arrays.
 *
 * Our indexing needs to reflects this. Since Valhall and Bifrost are quite
 * different, we provide separate functions for these.
 */
static bi_index
bi_emit_image_coord(bi_builder *b, bi_index coord, unsigned src_idx,
                    unsigned coord_comps, bool is_array, bool is_msaa)
{
   assert(coord_comps > 0 && coord_comps <= 3);

   /* MSAA load store should have been lowered */
   assert(!is_msaa);
   if (src_idx == 0) {
      if (coord_comps == 1 || (coord_comps == 2 && is_array))
         return bi_extract(b, coord, 0);
      else
         return bi_mkvec_v2i16(b, bi_half(bi_extract(b, coord, 0), false),
                               bi_half(bi_extract(b, coord, 1), false));
   } else {
      if (coord_comps == 3)
         return bi_extract(b, coord, 2);
      else if (coord_comps == 2 && is_array)
         return bi_extract(b, coord, 1);
      else
         return bi_zero();
   }
}

static bi_index
va_emit_image_coord(bi_builder *b, bi_index coord, bi_index sample_index,
                    unsigned src_idx, unsigned coord_comps, bool is_array,
                    bool is_msaa)
{
   assert(coord_comps > 0 && coord_comps <= 3);
   if (src_idx == 0) {
      if (coord_comps == 1 || (coord_comps == 2 && is_array))
         return bi_extract(b, coord, 0);
      else
         return bi_mkvec_v2i16(b, bi_half(bi_extract(b, coord, 0), false),
                               bi_half(bi_extract(b, coord, 1), false));
   } else if (is_msaa) {
      bi_index array_idx = bi_extract(b, sample_index, 0);
      if (coord_comps == 3)
         return bi_mkvec_v2i16(b, bi_half(array_idx, false),
                               bi_half(bi_extract(b, coord, 2), false));
      else if (coord_comps == 2)
         return array_idx;
   } else if (coord_comps == 3 && is_array) {
      return bi_mkvec_v2i16(b, bi_imm_u16(0),
                            bi_half(bi_extract(b, coord, 2), false));
   } else if (coord_comps == 3 && !is_array) {
      return bi_mkvec_v2i16(b, bi_half(bi_extract(b, coord, 2), false),
                            bi_imm_u16(0));
   } else if (coord_comps == 2 && is_array) {
      return bi_mkvec_v2i16(b, bi_imm_u16(0),
                            bi_half(bi_extract(b, coord, 1), false));
   }
   return bi_zero();
}

static void
bi_emit_image_load(bi_builder *b, nir_intrinsic_instr *instr)
{
   enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   assert((dim != GLSL_SAMPLER_DIM_BUF) &&
          "Texel buffers should already have been lowered");
   unsigned coord_comps = nir_image_intrinsic_coord_components(instr);
   bool array =
      nir_intrinsic_image_array(instr) || dim == GLSL_SAMPLER_DIM_CUBE;

   bi_index coords = bi_src_index(&instr->src[1]);
   bi_index indexvar = bi_src_index(&instr->src[2]);
   bi_index xy, zw;
   bool is_ms = (dim == GLSL_SAMPLER_DIM_MS);
   if (b->shader->arch < 9) {
      xy = bi_emit_image_coord(b, coords, 0, coord_comps, array, is_ms);
      zw = bi_emit_image_coord(b, coords, 1, coord_comps, array, is_ms);
   } else {
      xy =
         va_emit_image_coord(b, coords, indexvar, 0, coord_comps, array, is_ms);
      zw =
         va_emit_image_coord(b, coords, indexvar, 1, coord_comps, array, is_ms);
   }
   bi_index dest = bi_def_index(&instr->def);
   enum bi_register_format regfmt =
      bi_reg_fmt_for_nir(nir_intrinsic_dest_type(instr));
   enum bi_vecsize vecsize = instr->num_components - 1;

   if (b->shader->arch >= 9 && nir_src_is_const(instr->src[0])) {
      const unsigned raw_value = nir_src_as_uint(instr->src[0]);
      const unsigned table_index = pan_res_handle_get_table(raw_value);
      const unsigned texture_index = pan_res_handle_get_index(raw_value);

      if (texture_index < 16 && va_is_valid_const_table(table_index)) {
         bi_instr *I =
            bi_ld_tex_imm_to(b, dest, xy, zw, regfmt, vecsize, texture_index);
         I->table = va_res_fold_table_idx(table_index);
      } else {
         bi_ld_tex_to(b, dest, xy, zw, bi_src_index(&instr->src[0]), regfmt,
                      vecsize);
      }
   } else if (b->shader->arch >= 9) {
      bi_ld_tex_to(b, dest, xy, zw, bi_src_index(&instr->src[0]), regfmt,
                   vecsize);
   } else {
      bi_ld_attr_tex_to(b, dest, xy, zw, bi_src_index(&instr->src[0]), regfmt,
                        vecsize);
   }

   bi_split_def(b, &instr->def);
}

static void
bi_emit_lea_image_to(bi_builder *b, bi_index dest, nir_intrinsic_instr *instr)
{
   enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   assert((dim != GLSL_SAMPLER_DIM_BUF) &&
          "Texel buffers should already have been lowered");
   bool array =
      nir_intrinsic_image_array(instr) || dim == GLSL_SAMPLER_DIM_CUBE;
   unsigned coord_comps = nir_image_intrinsic_coord_components(instr);

   enum bi_register_format type =
      (instr->intrinsic == nir_intrinsic_image_store)
         ? bi_reg_fmt_for_nir(nir_intrinsic_src_type(instr))
         : BI_REGISTER_FORMAT_AUTO;

   bi_index coords = bi_src_index(&instr->src[1]);
   bi_index indices = bi_src_index(&instr->src[2]);
   bi_index xy, zw;
   bool is_ms = dim == GLSL_SAMPLER_DIM_MS;
   if (b->shader->arch < 9) {
      xy = bi_emit_image_coord(b, coords, 0, coord_comps, array, is_ms);
      zw = bi_emit_image_coord(b, coords, 1, coord_comps, array, is_ms);
   } else {
      xy =
         va_emit_image_coord(b, coords, indices, 0, coord_comps, array, is_ms);
      zw =
         va_emit_image_coord(b, coords, indices, 1, coord_comps, array, is_ms);
   }

   if (b->shader->arch >= 9 && nir_src_is_const(instr->src[0])) {
      const unsigned raw_value = nir_src_as_uint(instr->src[0]);
      unsigned table_index = pan_res_handle_get_table(raw_value);
      unsigned texture_index = pan_res_handle_get_index(raw_value);

      if (texture_index < 16 && va_is_valid_const_table(table_index)) {
         bi_instr *I = bi_lea_tex_imm_to(b, dest, xy, zw, false, texture_index);
         I->table = va_res_fold_table_idx(table_index);
      } else {
         bi_lea_tex_to(b, dest, xy, zw, bi_src_index(&instr->src[0]), false);
      }
   } else if (b->shader->arch >= 9) {
      bi_lea_tex_to(b, dest, xy, zw, bi_src_index(&instr->src[0]), false);
   } else {
      bi_instr *I = bi_lea_attr_tex_to(b, dest, xy, zw,
                                       bi_src_index(&instr->src[0]), type);

      /* LEA_ATTR_TEX defaults to the secondary attribute table, but
       * our ABI has all images in the primary attribute table
       */
      I->table = BI_TABLE_ATTRIBUTE_1;
   }

   bi_emit_cached_split(b, dest, 3 * 32);
}

static bi_index
bi_emit_lea_image(bi_builder *b, nir_intrinsic_instr *instr)
{
   bi_index dest = bi_temp(b->shader);
   bi_emit_lea_image_to(b, dest, instr);
   return dest;
}

static void
bi_emit_image_store(bi_builder *b, nir_intrinsic_instr *instr)
{
   bi_index a[4] = {bi_null()};
   bi_emit_split_i32(b, a, bi_emit_lea_image(b, instr), 3);

   /* Due to SPIR-V limitations, the source type is not fully reliable: it
    * reports uint32 even for write_imagei. This causes an incorrect
    * u32->s32->u32 roundtrip which incurs an unwanted clamping. Use auto32
    * instead, which will match per the OpenCL spec. Of course this does
    * not work for 16-bit stores, but those are not available in OpenCL.
    */
   ASSERTED nir_alu_type T = nir_intrinsic_src_type(instr);
   assert(nir_alu_type_get_type_size(T) == 32);

   bi_st_cvt(b, bi_src_index(&instr->src[3]), a[0], a[1], a[2],
             BI_REGISTER_FORMAT_AUTO, instr->num_components - 1);
}

static void
bi_emit_load_cvt(bi_builder *b, bi_index dst, nir_intrinsic_instr *instr,
                 enum va_memory_access mem_access)
{
   bi_index addr = bi_src_index(&instr->src[0]);
   bi_index icd = bi_src_index(&instr->src[1]);

   bi_instr *I =
      bi_ld_cvt_to(b, dst, bi_extract(b, addr, 0), bi_extract(b, addr, 1), icd,
                   bi_reg_fmt_for_nir(nir_intrinsic_dest_type(instr)),
                   instr->def.num_components - 1);
   I->mem_access = mem_access;
   bi_emit_cached_split_i32(b, dst, instr->def.num_components);
}

static void
bi_emit_store_cvt(bi_builder *b, nir_intrinsic_instr *instr,
                  enum va_memory_access mem_access)
{
   bi_index value = bi_src_index(&instr->src[0]);
   bi_index addr = bi_src_index(&instr->src[1]);
   bi_index icd = bi_src_index(&instr->src[2]);

   const nir_alu_type src_type = nir_intrinsic_src_type(instr);
   enum bi_register_format regfmt;
   if (src_type == 32) {
      assert(nir_src_bit_size(instr->src[0]) == 32);
      regfmt = BI_REGISTER_FORMAT_AUTO;
   } else {
      assert(nir_src_bit_size(instr->src[0]) ==
             nir_alu_type_get_type_size(src_type));
      regfmt = bi_reg_fmt_for_nir(src_type);
   }

   bi_instr *I =
      bi_st_cvt(b, value, bi_extract(b, addr, 0), bi_extract(b, addr, 1), icd,
                regfmt, instr->num_components - 1);
   I->mem_access = mem_access;
}

static void
bi_emit_atomic_i64_to(bi_builder *b, bi_index dst, bi_index addr, bi_index arg,
                      nir_atomic_op op)
{
   enum bi_atom_opc opc = bi_atom_opc_for_nir(op);
   enum bi_atom_opc post_opc = opc;
   bool bifrost = b->shader->arch <= 8;

   /* ATOM_C.i64 takes a vector with {arg, coalesced}, ATOM_C1.i64 doesn't
    * take any vector but can still output in RETURN mode */
   bi_index tmp_dest = bifrost ? bi_temp(b->shader) : dst;
   unsigned sr_count = bifrost ? 4 : 2;

   /* Generate either ATOM or ATOM1 as required */
   if (bi_promote_atom_c1(opc, arg, &opc)) {
      bi_atom1_return_i64_to(b, tmp_dest, bi_extract(b, addr, 0),
                             bi_extract(b, addr, 1), opc, sr_count);
   } else {
      bi_atom_return_i64_to(b, tmp_dest, arg, bi_extract(b, addr, 0),
                            bi_extract(b, addr, 1), opc, sr_count);
   }

   if (bifrost) {
      /* Post-process it */
      bi_emit_cached_split(b, tmp_dest, 64 * 2);
      bi_atom_post_i64_to(b, dst, bi_extract(b, tmp_dest, 0),
                          bi_extract(b, tmp_dest, 2), post_opc);
      bi_emit_cached_split(b, dst, 64);
   }
}
static void
bi_emit_atomic_i32_to(bi_builder *b, bi_index dst, bi_index addr, bi_index arg,
                      nir_atomic_op op)
{
   enum bi_atom_opc opc = bi_atom_opc_for_nir(op);
   enum bi_atom_opc post_opc = opc;
   bool bifrost = b->shader->arch <= 8;

   /* ATOM_C.i32 takes a vector with {arg, coalesced}, ATOM_C1.i32 doesn't
    * take any vector but can still output in RETURN mode */
   bi_index tmp_dest = bifrost ? bi_temp(b->shader) : dst;
   unsigned sr_count = bifrost ? 2 : 1;

   /* Generate either ATOM or ATOM1 as required */
   if (bi_promote_atom_c1(opc, arg, &opc)) {
      bi_atom1_return_i32_to(b, tmp_dest, bi_extract(b, addr, 0),
                             bi_extract(b, addr, 1), opc, sr_count);
   } else {
      bi_atom_return_i32_to(b, tmp_dest, arg, bi_extract(b, addr, 0),
                            bi_extract(b, addr, 1), opc, sr_count);
   }

   if (bifrost) {
      /* Post-process it */
      bi_emit_cached_split_i32(b, tmp_dest, 2);
      bi_atom_post_i32_to(b, dst, bi_extract(b, tmp_dest, 0),
                          bi_extract(b, tmp_dest, 1), post_opc);
   }
}

static void
bi_emit_load_var_special_pan(bi_builder *b, nir_intrinsic_instr *instr)
{
   bi_index dst = bi_def_index(&instr->def);
   enum bi_varying_name var_name = nir_intrinsic_flags(instr);
   nir_intrinsic_instr *bary = nir_src_as_intrinsic(instr->src[0]);

   enum bi_sample sample = bi_interp_for_intrinsic(bary->intrinsic);
   bi_index src0 = bi_varying_src0_for_barycentric(b, bary);
   unsigned nr = instr->num_components;
   assert(instr->def.bit_size == 32);


   /* .explicit is not supported with frag_z */
   if (var_name == BI_VARYING_NAME_FRAG_Z)
      assert(sample != BI_SAMPLE_EXPLICIT);

   /* TODO: v10 adds BI_UPDATE_NONE and v14 introduces store/retrieve updates
    * and the barycentric coordinate special varying */
   bi_ld_var_special_to(
      b, dst, src0, BI_REGISTER_FORMAT_F32, sample, BI_UPDATE_CLOBBER,
      (enum bi_varying_name) var_name, nr - 1);
   bi_emit_cached_split_i32(b, dst, nr);
}

static void
bi_emit_ld_tile(bi_builder *b, nir_intrinsic_instr *instr)
{
   bi_index dest = bi_def_index(&instr->def);
   nir_alu_type T = nir_intrinsic_dest_type(instr);
   nir_io_semantics sem = nir_intrinsic_io_semantics(instr);
   enum bi_register_format regfmt = bi_reg_fmt_for_nir(T);
   unsigned size = instr->def.bit_size;
   unsigned nr = instr->num_components;

   bi_index pi = bi_src_index(&instr->src[0]);
   bi_index coverage = bi_src_index(&instr->src[1]);
   bi_index conversion = bi_src_index(&instr->src[2]);

   bi_instr *I = bi_ld_tile_to(b, dest, pi, coverage, conversion,
                               regfmt, nr - 1);
   I->z_stencil = bi_is_zs(sem.location);

   if (instr->intrinsic == nir_intrinsic_load_tile_res_pan)
      I->wait_resource = true;

   bi_emit_cached_split(b, dest, size * nr);
}

static void
bi_emit_st_tile(bi_builder *b, nir_intrinsic_instr *instr)
{
   nir_alu_type T = nir_intrinsic_src_type(instr);
   assert(!bi_is_zs(nir_intrinsic_io_semantics(instr).location));
   enum bi_register_format regfmt = bi_reg_fmt_for_nir(T);
   unsigned nr = instr->num_components;

   bi_index rgba = bi_src_index(&instr->src[0]);
   bi_index pi = bi_src_index(&instr->src[1]);
   bi_index coverage = bi_src_index(&instr->src[2]);
   bi_index conversion = bi_src_index(&instr->src[3]);

   bi_st_tile(b, rgba, pi, coverage, conversion, regfmt, nr - 1);
}

/*
 * Older Bifrost hardware has a limited CLPER instruction. Add a safe helper
 * that uses the hardware functionality if available and lowers otherwise.
 */
static bi_index
bi_clper(bi_builder *b, bi_index s0, bi_index s1, enum bi_lane_op lop)
{
   if (b->shader->quirks & BIFROST_LIMITED_CLPER) {
      if (lop == BI_LANE_OP_XOR) {
         bi_index lane_id = bi_fau(BIR_FAU_LANE_ID, false);
         s1 = bi_lshift_xor_i32(b, lane_id, s1, bi_imm_u8(0));
      } else {
         assert(lop == BI_LANE_OP_NONE);
      }

      return bi_clper_old_i32(b, s0, s1);
   } else {
      return bi_clper_i32(b, s0, s1, BI_INACTIVE_RESULT_ZERO, lop,
                          BI_SUBGROUP_SUBGROUP4);
   }
}

static void
bi_emit_derivative(bi_builder *b, bi_index dst, nir_intrinsic_instr *instr,
                   unsigned axis, bool coarse)
{
   assert(axis == 1 || axis == 2);

   bi_index left, right;
   bi_index s0 = bi_src_index(&instr->src[0]);

   unsigned sz = instr->def.bit_size;

   /* If all uses are fabs, the sign of the derivative doesn't matter. This is
    * inherently based on fine derivatives so we can't do it for coarse.
    */
   if (coarse) {
      left = bi_clper(b, s0, bi_imm_u8(0), BI_LANE_OP_NONE);
      right = bi_clper(b, s0, bi_imm_u8(axis), BI_LANE_OP_NONE);
   } else {
      left = s0;
      right = bi_clper(b, s0, bi_imm_u8(axis), BI_LANE_OP_XOR);
   }

   if (!coarse && !nir_def_all_uses_ignore_sign_bit(&instr->def)) {
      /* If the user cares about the sign, we need to take into account the fact
       * left/right (or top/bottom) might be inverted. Instead of using a couple
       * CSEL, we just invert the sign bit with
       *
       *    sign_bit = XOR(sign_bit, axis_bit(lane_id)).
       */
      bi_index res = bi_fadd(b, sz, right, bi_neg(left));
      bi_index lane = bi_fau(BIR_FAU_LANE_ID, false);
      bi_index lane_shift = bi_imm_u8(sz - ffs(axis));

      /* Clear the low bit before the shift if this is the Y-axis we want.
       * We skip it on the X-axis, because the lshift-by-31 will get us a
       * clean mask. */
      if (axis == 2)
         lane = bi_lshift_and_i32(b, lane, bi_imm_u32(2), bi_imm_u8(0));

      if (sz == 16)
         lane = bi_half(lane, false);

      /* And here comes the final XOR on the sign bit. */
      bi_lshift_xor_to(b, sz, dst, lane, res, lane_shift);
   } else {
      bi_fadd_to(b, sz, dst, right, bi_neg(left));
   }
}

static enum bi_subgroup
bi_subgroup_from_cluster_size(unsigned cluster_size)
{
   switch (cluster_size) {
   case 2: return BI_SUBGROUP_SUBGROUP2;
   case 4: return BI_SUBGROUP_SUBGROUP4;
   case 8: return BI_SUBGROUP_SUBGROUP8;
   case 16: return BI_SUBGROUP_SUBGROUP16;
   default: UNREACHABLE("Unsupported cluster size");
   }
}

static enum va_memory_access
va_memory_access_from_nir(const nir_intrinsic_instr *intr)
{
   const enum gl_access_qualifier access = nir_intrinsic_access(intr);
   if (access & ACCESS_ISTREAM_PAN)
      return VA_MEMORY_ACCESS_ISTREAM;
   if (access & ACCESS_ESTREAM_PAN)
      return VA_MEMORY_ACCESS_ESTREAM;
   return VA_MEMORY_ACCESS_NONE;
}

static void
bi_emit_intrinsic(bi_builder *b, nir_intrinsic_instr *instr)
{
   bi_index dst = nir_intrinsic_infos[instr->intrinsic].has_dest
                     ? bi_def_index(&instr->def)
                     : bi_null();
   mesa_shader_stage stage = b->shader->stage;

   switch (instr->intrinsic) {
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_at_sample:
   case nir_intrinsic_load_barycentric_at_offset:
      /* handled later via load_fs_input */
      break;
   case nir_intrinsic_load_attribute_pan:
      assert(stage == MESA_SHADER_VERTEX);
      bi_emit_load_attr(b, instr);
      break;

   case nir_intrinsic_load_blend_input_pan:
      bi_emit_load_blend_input(b, instr);
      break;

   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_load_input:
      assert(!b->shader->inputs->is_blend);
      if (stage == MESA_SHADER_VERTEX)
         bi_emit_load_attr(b, instr);
      else
         UNREACHABLE("Unsupported shader stage");
      break;

   case nir_intrinsic_load_var_pan:
   case nir_intrinsic_load_var_flat_pan:
      bi_emit_load_var(b, instr);
      break;

   case nir_intrinsic_load_var_buf_pan:
   case nir_intrinsic_load_var_buf_flat_pan:
      bi_emit_load_var_buf(b, instr);
      break;

   case nir_intrinsic_load_cumulative_coverage_pan:
      bi_mov_i32_to(b, dst, bi_preload(b, BI_PRELOAD_CUMULATIVE_COVERAGE));
      break;

   case nir_intrinsic_load_blend_descriptor_pan: {
      unsigned rt = nir_intrinsic_base(instr);
      bi_collect_v2i32_to(b, dst, bi_fau(BIR_FAU_BLEND_0 + rt, false),
                                  bi_fau(BIR_FAU_BLEND_0 + rt, true));
      break;
   }

   case nir_intrinsic_atest_pan: {
      unsigned alpha_bit_size = instr->src[1].ssa->bit_size;
      assert(alpha_bit_size == 16 || alpha_bit_size == 32);

      bi_index coverage = bi_src_index(&instr->src[0]);
      bi_index alpha = bi_src_index(&instr->src[1]);

      if (alpha_bit_size == 16)
         alpha = bi_half(alpha, false);

      bi_atest_to(b, dst, coverage, alpha, bi_fau(BIR_FAU_ATEST_PARAM, false));
      break;
   }

   case nir_intrinsic_zs_emit_pan: {
      bi_index coverage = bi_src_index(&instr->src[0]);
      unsigned flags = nir_intrinsic_flags(instr);
      uint32_t has_z = flags & BITFIELD_BIT(FRAG_RESULT_DEPTH);
      uint32_t has_s = flags & BITFIELD_BIT(FRAG_RESULT_STENCIL);
      bi_index z = has_z ? bi_src_index(&instr->src[1]) : bi_dontcare(b);
      bi_index s = has_s ? bi_src_index(&instr->src[2]) : bi_dontcare(b);
      bi_zs_emit_to(b, dst, z, s, coverage, has_s, has_z);
      break;
   }

   case nir_intrinsic_blend_pan:
   case nir_intrinsic_blend2_pan: {
      bi_index coverage = bi_src_index(&instr->src[0]);
      bi_index desc = bi_src_index(&instr->src[1]);
      bi_index rgba = bi_src_index(&instr->src[2]);
      nir_alu_type T = nir_intrinsic_src_type(instr);

      bi_index rgba2 = bi_null();
      nir_alu_type T2 = nir_type_invalid;
      if (instr->intrinsic == nir_intrinsic_blend2_pan) {
         rgba2 = bi_src_index(&instr->src[3]);
         T2 = nir_intrinsic_dest_type(instr);
      }

      unsigned size = nir_alu_type_get_type_size(T);
      unsigned size_2 = nir_alu_type_get_type_size(T2);
      unsigned sr_count = (size <= 16) ? 2 : 4;
      unsigned sr_count_2 = (size_2 <= 16) ? 2 : 4;

      enum bi_register_format regfmt = bi_reg_fmt_for_nir(T);
      if (b->shader->nir->info.fs.untyped_color_outputs)
         regfmt = BI_REGISTER_FORMAT_AUTO;

      /* If we have more than one color target, explicit copy since BLEND
       * inputs are precoloured to R0-R3.
       *
       * TODO: maybe schedule around this or implement in RA as a spill
       */
      if (b->shader->nir->info.outputs_written >> FRAG_RESULT_DATA1) {
         bi_index srcs[4] = {rgba, rgba, rgba, rgba};
         unsigned channels[4] = {0, 1, 2, 3};
         rgba = bi_temp(b->shader);
         bi_make_vec_to(b, rgba, srcs, channels, 4, size);
      }

      nir_io_semantics io = nir_intrinsic_io_semantics(instr);
      assert(io.location >= FRAG_RESULT_DATA0);
      assert(io.location <= FRAG_RESULT_DATA7);
      unsigned rt = io.location - FRAG_RESULT_DATA0;

      bi_instr *I = bi_blend_to(b, bi_temp(b->shader), rgba, coverage,
                                bi_extract(b, desc, 0), bi_extract(b, desc, 1),
                                rgba2, regfmt, sr_count, sr_count_2);
      I->blend_target = rt;

      b->shader->info.bifrost->blend[rt].type = T;
      if (instr->intrinsic == nir_intrinsic_blend2_pan) {
         assert(b->shader->info.bifrost->blend_src1_type == nir_type_invalid);
         b->shader->info.bifrost->blend_src1_type = T2;
      }
      break;
   }

   case nir_intrinsic_blend_return_pan:
      /* Jump back to the fragment shader. On Valhall, only jump if the address
       * is nonzero. The check is free there and it implements the "jump to 0
       * terminates the blend shader" that's automatic on Bifrost.
       */
      if (b->shader->arch >= 9)
         bi_branchzi(b, bi_preload(b, BI_PRELOAD_BLEND_LINK),
                     bi_preload(b, BI_PRELOAD_BLEND_LINK), BI_CMPF_NE);
      else
         bi_jump(b, bi_preload(b, BI_PRELOAD_BLEND_LINK));
      break;

   case nir_intrinsic_load_ubo:
      bi_emit_load_ubo(b, instr);
      break;

   case nir_intrinsic_load_push_constant:
      bi_emit_load_push_constant(b, instr);
      break;

   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
      bi_emit_load(b, instr, BI_SEG_NONE, va_memory_access_from_nir(instr));
      break;

   case nir_intrinsic_store_global:
   case nir_intrinsic_store_global_psiz_pan: {
      bi_instr *I = bi_emit_store(b, instr, BI_SEG_NONE,
                                  va_memory_access_from_nir(instr));
      I->is_psiz_write = instr->intrinsic == nir_intrinsic_store_global_psiz_pan;
      break;
   }

   case nir_intrinsic_load_scratch:
      bi_emit_load(b, instr, BI_SEG_TL, VA_MEMORY_ACCESS_FORCE);
      break;

   case nir_intrinsic_store_scratch:
      bi_emit_store(b, instr, BI_SEG_TL, VA_MEMORY_ACCESS_FORCE);
      break;

   case nir_intrinsic_load_shared:
      bi_emit_load(b, instr, BI_SEG_WLS, VA_MEMORY_ACCESS_NONE);
      break;

   case nir_intrinsic_store_shared:
      bi_emit_store(b, instr, BI_SEG_WLS, VA_MEMORY_ACCESS_NONE);
      break;

   case nir_intrinsic_barrier:
      switch (nir_intrinsic_execution_scope(instr)) {
      case SCOPE_NONE:
      case SCOPE_SUBGROUP:
         /*
          * To implement none and subgroup barriers, we only need to prevent
          * the scheduler from reordering operations with side-effects around
          * the barrier. Avail and vis are trivially established.
          */
         bi_nop(b)->scheduling_barrier = true;
         break;

      case SCOPE_WORKGROUP:
         assert(mesa_shader_stage_is_compute(b->shader->stage));
         bi_barrier(b);
         /*
          * Blob doesn't seem to do anything for memory barriers, so no need to
          * check nir_intrinsic_memory_scope().
          */
         break;

      default:
         UNREACHABLE("Unsupported barrier scope");
      }

      break;

   case nir_intrinsic_shared_atomic: {
      nir_atomic_op op = nir_intrinsic_atomic_op(instr);

      bi_index addr = bi_src_index(&instr->src[0]);
      bi_index addr_hi;

      if (b->shader->arch >= 9) {
         bi_handle_segment(b, &addr, &addr_hi, BI_SEG_WLS, NULL);
         addr = bi_collect_v2i32(b, addr, addr_hi);

         if (nir_src_bit_size(instr->src[1]) == 32) {
            bi_emit_atomic_i32_to(b, dst, addr, bi_src_index(&instr->src[1]), op);
         } else {
            bi_emit_atomic_i64_to(b, dst, addr, bi_src_index(&instr->src[1]), op);
         }
      } else {
         if (op == nir_atomic_op_xchg) {
            bi_emit_axchg_to(b, dst, addr, &instr->src[1],
                             BI_SEG_WLS);
         } else {
            addr = bi_seg_add_i64(b, addr, bi_zero(), false, BI_SEG_WLS);
            bi_emit_cached_split(b, addr, 64);

            if (nir_src_bit_size(instr->src[1]) == 32) {
               bi_emit_atomic_i32_to(b, dst, addr, bi_src_index(&instr->src[1]), op);
            } else {
               bi_emit_atomic_i64_to(b, dst, addr, bi_src_index(&instr->src[1]), op);
            }
         }
      }

      bi_split_def(b, &instr->def);
      break;
   }

   case nir_intrinsic_global_atomic: {
      nir_atomic_op op = nir_intrinsic_atomic_op(instr);

      if (b->shader->arch >= 9) {
         if (nir_src_bit_size(instr->src[1]) == 32) {
            bi_emit_atomic_i32_to(b, dst, bi_src_index(&instr->src[0]),
                                  bi_src_index(&instr->src[1]), op);
         } else {
            bi_emit_atomic_i64_to(b, dst, bi_src_index(&instr->src[0]),
                                  bi_src_index(&instr->src[1]), op);
         }
      } else {

         if (op == nir_atomic_op_xchg) {
            bi_emit_axchg_to(b, dst, bi_src_index(&instr->src[0]), &instr->src[1], BI_SEG_NONE);
         } else {
            if (nir_src_bit_size(instr->src[1]) == 32) {
               bi_emit_atomic_i32_to(b, dst, bi_src_index(&instr->src[0]),
                                     bi_src_index(&instr->src[1]), op);
            } else {
               bi_emit_atomic_i64_to(b, dst, bi_src_index(&instr->src[0]),
                                     bi_src_index(&instr->src[1]), op);
            }
         }
      }

      bi_split_def(b, &instr->def);
      break;
   }

   case nir_intrinsic_image_texel_address:
      bi_emit_lea_image_to(b, dst, instr);
      break;

   case nir_intrinsic_image_load:
      bi_emit_image_load(b, instr);
      break;

   case nir_intrinsic_image_store:
      bi_emit_image_store(b, instr);
      break;

   case nir_intrinsic_global_atomic_swap:
      bi_emit_acmpxchg_to(b, dst, bi_src_index(&instr->src[0]), &instr->src[1],
                          &instr->src[2], BI_SEG_NONE);
      bi_split_def(b, &instr->def);
      break;

   case nir_intrinsic_shared_atomic_swap:
      bi_emit_acmpxchg_to(b, dst, bi_src_index(&instr->src[0]), &instr->src[1],
                          &instr->src[2], BI_SEG_WLS);
      bi_split_def(b, &instr->def);
      break;

   case nir_intrinsic_texs_2d_pan: {
      struct pan_bi_tex_flags flags =
         nir_intrinsic_pan_bi_tex_flags(instr);

      bi_instr *I = bi_texs_2d_to(b, instr->def.bit_size,
                                  bi_def_index(&instr->def),
                                  bi_src_index(&instr->src[0]),
                                  bi_src_index(&instr->src[1]),
                                  flags.explicit_lod,
                                  flags.sampler_idx,
                                  flags.texture_idx);
      I->skip = flags.skip;

      bi_split_def(b, &instr->def);
      break;
   }

   case nir_intrinsic_texs_cube_pan: {
      struct pan_bi_tex_flags flags =
         nir_intrinsic_pan_bi_tex_flags(instr);
      assert(!flags.explicit_lod);

      bi_instr *I = bi_texs_cube_to(b, instr->def.bit_size,
                                    bi_def_index(&instr->def),
                                    bi_src_index(&instr->src[0]),
                                    bi_src_index(&instr->src[1]),
                                    bi_src_index(&instr->src[2]),
                                    flags.sampler_idx,
                                    flags.texture_idx);
      I->skip = flags.skip;

      bi_split_def(b, &instr->def);
      break;
   }

   case nir_intrinsic_texc0_pan:
   case nir_intrinsic_texc1_pan:
   case nir_intrinsic_texc2_pan: {
      bi_index cx = bi_src_index(&instr->src[0]);
      bi_index cy = bi_src_index(&instr->src[1]);
      bi_index desc = bi_src_index(&instr->src[2]);
      struct pan_bi_tex_flags flags =
         nir_intrinsic_pan_bi_tex_flags(instr);

      bi_index sr_comps[8];
      unsigned sr_count = 0;

      if (instr->intrinsic != nir_intrinsic_texc0_pan) {
         bi_index sr0 = bi_src_index(&instr->src[3]);
         for (unsigned i = 0; i < nir_src_num_components(instr->src[3]); i++)
            sr_comps[sr_count++] = bi_extract(b, sr0, i);
      }
      if (instr->intrinsic == nir_intrinsic_texc2_pan) {
         bi_index sr1 = bi_src_index(&instr->src[4]);
         for (unsigned i = 0; i < nir_src_num_components(instr->src[4]); i++)
            sr_comps[sr_count++] = bi_extract(b, sr1, i);
      }

      bi_index sr = bi_null();
      if (sr_count > 0) {
         sr = bi_temp(b->shader);
         bi_emit_collect_to(b, sr, sr_comps, sr_count);
      }

      bi_instr *I = bi_texc_to(b, bi_def_index(&instr->def), sr, cx, cy,
                               desc, flags.explicit_lod, sr_count, 0);
      I->register_format = bi_reg_fmt_for_nir(nir_intrinsic_dest_type(instr));
      I->skip = flags.skip;

      bi_split_def(b, &instr->def);
      break;
   }

   case nir_intrinsic_load_pixel_coord:
      /* Vectorized load of the preloaded i16vec2 */
      bi_mov_i32_to(b, dst, bi_preload(b, BI_PRELOAD_POSITION_XY));
      break;

   case nir_intrinsic_load_global_cvt_pan:
      bi_emit_load_cvt(b, dst, instr, va_memory_access_from_nir(instr));
      break;

   case nir_intrinsic_store_global_cvt_pan:
      bi_emit_store_cvt(b, instr, va_memory_access_from_nir(instr));
      break;

   case nir_intrinsic_load_idvs_output_buf_index_pan:
      bi_mov_i32_to(b, dst, bi_preload(b, BI_PRELOAD_INTERNAL_ID));
      break;

   case nir_intrinsic_lea_attr_pan:
      bi_emit_lea_attr(b, instr);
      break;

   case nir_intrinsic_lea_buf_pan:
      bi_emit_lea_buf(b, instr);
      break;

   case nir_intrinsic_load_tile_pan:
   case nir_intrinsic_load_tile_res_pan:
      bi_emit_ld_tile(b, instr);
      break;

   case nir_intrinsic_store_tile_pan:
      bi_emit_st_tile(b, instr);
      break;

   case nir_intrinsic_demote_if:
      bi_discard_b32(b, bi_src_index(&instr->src[0]));
      break;

   case nir_intrinsic_demote:
      bi_discard_f32(b, bi_zero(), bi_zero(), BI_CMPF_EQ);
      break;

   case nir_intrinsic_load_sample_positions_pan:
      bi_collect_v2i32_to(b, dst, bi_fau(BIR_FAU_SAMPLE_POS_ARRAY, false),
                          bi_fau(BIR_FAU_SAMPLE_POS_ARRAY, true));
      break;

   case nir_intrinsic_load_sample_mask_in:
      /* [0:15] contains the coverage bitmap */
      bi_u16_to_u32_to(
         b, dst, bi_half(bi_preload(b, BI_PRELOAD_RASTERIZER_COVERAGE), false));
      break;

   case nir_intrinsic_load_sample_mask:
      bi_mov_i32_to(b, dst, bi_coverage(b));
      break;

   case nir_intrinsic_load_sample_id:
      bi_load_sample_id_to(b, dst);
      break;

   case nir_intrinsic_load_primitive_id:
      bi_mov_i32_to(b, dst, bi_preload(b, BI_PRELOAD_PRIMITIVE_ID));
      break;

   case nir_intrinsic_load_front_face: {
      /* (primitive_flags & 1) == 0 means primitive is front facing */
      bi_index primitive_facing = bi_preload(b, BI_PRELOAD_PRIMITIVE_FLAGS);

      /* Starting with v11, there is more fields defined in the primitive flags */
      if (b->shader->arch >= 11)
         primitive_facing =
            bi_lshift_and_i32(b, primitive_facing, bi_imm_u32(1), bi_imm_u8(0));

      bi_icmp_i32_to(b, dst, primitive_facing, bi_zero(), BI_CMPF_EQ,
                     BI_RESULT_TYPE_M1);
      break;
   }

   case nir_intrinsic_load_var_special_pan:
      bi_emit_load_var_special_pan(b, instr);
      break;

   /* It appears vertex_id is zero-based with Bifrost geometry flows, but
    * not with Valhall's memory-allocation IDVS geometry flow. We only support
    * the new flow on Valhall so this is lowered in NIR.
    */
   case nir_intrinsic_load_vertex_id:
      assert(b->shader->malloc_idvs);
      bi_mov_i32_to(b, dst, bi_vertex_id(b));
      break;

   case nir_intrinsic_load_raw_vertex_id_pan:
      assert(!b->shader->malloc_idvs);
      bi_mov_i32_to(b, dst, bi_vertex_id(b));
      break;

   case nir_intrinsic_load_instance_id:
      bi_mov_i32_to(b, dst, bi_instance_id(b));
      break;

   case nir_intrinsic_load_draw_id:
      bi_mov_i32_to(b, dst, bi_draw_id(b));
      break;

   case nir_intrinsic_load_subgroup_invocation:
      bi_mov_i32_to(b, dst, bi_fau(BIR_FAU_LANE_ID, false));
      break;

   case nir_intrinsic_ballot:
   case nir_intrinsic_ballot_relaxed: {
      assert(instr->src[0].ssa->bit_size == 32);
      enum bi_subgroup subgroup =
         bi_subgroup_from_cluster_size(pan_subgroup_size(b->shader->arch));
      bi_wmask_to(b, dst, bi_src_index(&instr->src[0]), subgroup, 0);
      break;
   }

   case nir_intrinsic_read_invocation: {
      assert(instr->src[0].ssa->bit_size <= 32);
      enum bi_inactive_result inactive_result = BI_INACTIVE_RESULT_ZERO;
      enum bi_lane_op lane_op = BI_LANE_OP_NONE;
      enum bi_subgroup subgroup =
         bi_subgroup_from_cluster_size(pan_subgroup_size(b->shader->arch));
      bi_clper_i32_to(b, dst,
                      bi_src_index(&instr->src[0]),
                      bi_byte(bi_src_index(&instr->src[1]), 0),
                      inactive_result, lane_op, subgroup);
      break;
   }

   case nir_intrinsic_load_local_invocation_id:
      bi_collect_v3i32_to(
         b, dst,
         bi_u16_to_u32(b, bi_half(bi_preload(b, BI_PRELOAD_LOCAL_ID_0), 0)),
         bi_u16_to_u32(b, bi_half(bi_preload(b, BI_PRELOAD_LOCAL_ID_1), 1)),
         bi_u16_to_u32(b, bi_half(bi_preload(b, BI_PRELOAD_LOCAL_ID_2), 0)));
      break;

   case nir_intrinsic_load_workgroup_id:
      bi_collect_v3i32_to(b, dst, bi_preload(b, BI_PRELOAD_WORKGROUP_ID_0),
                          bi_preload(b, BI_PRELOAD_WORKGROUP_ID_1),
                          bi_preload(b, BI_PRELOAD_WORKGROUP_ID_2));
      break;

   case nir_intrinsic_load_global_invocation_id:
      bi_collect_v3i32_to(b, dst, bi_preload(b, BI_PRELOAD_GLOBAL_ID_0),
                          bi_preload(b, BI_PRELOAD_GLOBAL_ID_1),
                          bi_preload(b, BI_PRELOAD_GLOBAL_ID_2));
      break;

   case nir_intrinsic_shader_clock:
      bi_ld_gclk_u64_to(b, dst, BI_SOURCE_CYCLE_COUNTER);
      bi_split_def(b, &instr->def);
      b->shader->info.has_ld_gclk_instr = true;
      break;

   case nir_intrinsic_cubeface_pan: {
      bi_index x = bi_src_index(&instr->src[0]);
      bi_index y = bi_src_index(&instr->src[1]);
      bi_index z = bi_src_index(&instr->src[2]);

      bi_index max_xyz = bi_temp(b->shader);
      bi_index inf_face = bi_temp(b->shader);

      /* Use a pseudo op on Bifrost due to tuple restrictions */
      if (b->shader->arch <= 8) {
         bi_cubeface_to(b, max_xyz, inf_face, x, y, z);
      } else {
         bi_cubeface1_to(b, max_xyz, x, y, z);
         bi_cubeface2_v9_to(b, inf_face, x, y, z);
      }
      bi_collect_v2i32_to(b, dst, max_xyz, inf_face);
      break;
   }

   case nir_intrinsic_cube_ssel_pan:
      bi_cube_ssel_to(b, dst, bi_src_index(&instr->src[0]),
                              bi_src_index(&instr->src[1]),
                              bi_src_index(&instr->src[2]));
      break;

   case nir_intrinsic_cube_tsel_pan:
      bi_cube_tsel_to(b, dst, bi_src_index(&instr->src[0]),
                              bi_src_index(&instr->src[1]),
                              bi_src_index(&instr->src[2]));
      break;

   case nir_intrinsic_ddx:
   case nir_intrinsic_ddx_fine:
      bi_emit_derivative(b, dst, instr, 1, false);
      break;
   case nir_intrinsic_ddx_coarse:
      bi_emit_derivative(b, dst, instr, 1, true);
      break;
   case nir_intrinsic_ddy:
   case nir_intrinsic_ddy_fine:
      bi_emit_derivative(b, dst, instr, 2, false);
      break;
   case nir_intrinsic_ddy_coarse:
      bi_emit_derivative(b, dst, instr, 2, true);
      break;

   case nir_intrinsic_load_view_index:
   case nir_intrinsic_load_layer_id:
      assert(b->shader->arch >= 9);
      bi_mov_i32_to(
         b, dst,
         bi_u8_to_u32(b, bi_byte(bi_preload(b, BI_PRELOAD_FRAME_ARG), 0)));
      break;

   case nir_intrinsic_load_ssbo_address:
      assert(b->shader->arch >= 9);
      bi_lea_pka_to(b, dst, bi_src_index(&instr->src[1]),
                    bi_src_index(&instr->src[0]));
      bi_emit_cached_split(b, dst, 64);
      break;

   case nir_intrinsic_load_ssbo: {
      assert(b->shader->arch >= 9);
      unsigned dst_bits = instr->num_components * instr->def.bit_size;
      bi_ld_pka_to(b, dst_bits, dst, bi_src_index(&instr->src[1]),
                   bi_src_index(&instr->src[0]));
      bi_emit_cached_split(b, dst, dst_bits);
      break;
   }

   case nir_intrinsic_as_uniform:
      /*
       * We don't have uniform registers (registers shared by all threads in the
       * warp) like some other hardware does so this is just a simple mov for
       * us.
       */
      bi_mov_i32_to(b, dst, bi_src_index(&instr->src[0]));
      break;

   case nir_intrinsic_load_shader_output_pan:
      assert(b->shader->arch >= 12 && "load_shader_output_pan should have been lowered!");
      bi_mov_i32_to(b, dst, bi_fau(BIR_FAU_SHADER_OUTPUT, false));
      break;

   case nir_intrinsic_load_core_id:
      assert(b->shader->arch >= 9);
      bi_mov_i32_to(b, dst, bi_fau(BIR_FAU_CORE_ID, false));
      break;

   case nir_intrinsic_load_core_count_arm:
      assert(b->shader->arch >= 9);
      bi_mov_i32_to(b, dst, bi_iadd_u32(b, bi_fau(BIR_FAU_CORE_ID, true), bi_imm_u32(1), false));
      break;

   case nir_intrinsic_load_warp_id_arm:
      assert(b->shader->arch >= 9);
      bi_mov_i32_to(b, dst, bi_fau(BIR_FAU_WARP_ID, false));
      break;

   case nir_intrinsic_load_warp_max_id_arm:
      assert(b->shader->arch >= 9);
      bi_mov_i32_to(b, dst, bi_fau(BIR_FAU_WARP_ID, true));
      break;

   default:
      fprintf(stderr, "Unhandled intrinsic %s\n",
              nir_intrinsic_infos[instr->intrinsic].name);
      assert(0);
   }
}

static void
bi_emit_load_const(bi_builder *b, nir_load_const_instr *instr)
{
   const unsigned sz = instr->def.bit_size;
   const unsigned comps = instr->def.num_components;
   bi_index temp[BI_MAX_VEC];

   if (sz == 64) {
      for (unsigned i = 0; i < comps; ++i) {
         uint64_t v = nir_const_value_as_uint(instr->value[i], 64);
         temp[i * 2 + 0] = bi_mov_i32(b, bi_imm_u32(v));
         temp[i * 2 + 1] = bi_mov_i32(b, bi_imm_u32(v >> 32));
      }
      bi_emit_collect_to(b, bi_get_index(instr->def.index), temp, comps * 2);
   } else {
      assert(sz == 8 || sz == 16 || sz == 32);
      const unsigned comps_per_word = 32 / sz;
      const unsigned words = DIV_ROUND_UP(comps, comps_per_word);
      for (unsigned w = 0; w < words; w++) {
         uint32_t acc = 0;
         for (unsigned i = 0; i < comps_per_word; i++) {
            unsigned c = w * comps_per_word + i;
            if (c >= comps)
               break;

            uint32_t v = nir_const_value_as_uint(instr->value[c], sz);
            acc |= v << (i * sz);
         }
         temp[w] = bi_mov_i32(b, bi_imm_u32(acc));
      }
      bi_emit_collect_to(b, bi_get_index(instr->def.index), temp, words);
   }
}

static bi_index
bi_alu_src_index(bi_builder *b, nir_alu_src src, unsigned comps)
{
   unsigned bitsize = nir_src_bit_size(src.src);

   if (b->shader->arch >= 9 && bitsize == 64) {
      /* For Valhall, 64-bit instructions only encode one register but will read
       * the adjacent register that comes right after as well. Therefore we
       * don't need to extract a single register here.
       */
      assert(comps == 1);
      return bi_src_index(&src.src);
   }

   /* the bi_index carries the 32-bit (word) offset separate from the
    * subword swizzle, first handle the offset */

   unsigned offset = 0;

   assert(bitsize == 8 || bitsize == 16 || bitsize == 32 || bitsize == 64);
   for (unsigned i = 0; i < comps; ++i) {
      unsigned new_offset = (src.swizzle[i] * bitsize) / 32;

      if (i > 0)
         assert(offset == new_offset && "wrong vectorization");

      offset = new_offset;
   }

   bi_index idx = bi_extract(b, bi_src_index(&src.src), offset);

   /* Compose the subword swizzle with existing (identity) swizzle */
   assert(idx.swizzle == BI_SWIZZLE_H01);

   if (bitsize == 16) {
      unsigned c0 = src.swizzle[0] & 1;
      unsigned c1 = (comps > 1) ? src.swizzle[1] & 1 : c0;
      idx.swizzle = BI_SWIZZLE_H00 + c1 + (c0 << 1);
   } else if (bitsize == 8 && comps == 1) {
      idx.swizzle = BI_SWIZZLE_B0000 + (src.swizzle[0] & 3);
   } else if (bitsize == 8) {
      bool has_swizzle = false;
      enum bi_swizzle swizzle = BI_SWIZZLE_H01;
      if (comps == 3) {
         unsigned c[4];
         for (unsigned i = 0; i < 3; ++i)
            c[i] = src.swizzle[i] & 3;

         /* Try to find a swizzle that starts with the given v3i8 swizzle */
         for (unsigned i = 0; i < 4; i++) {
            c[3] = i;
            if (bi_swizzle_from_byte_channels(c, &swizzle)) {
               has_swizzle = true;
               break;
            }
         }
      } else {
         /* For 1 and 2-component, repeat the swizzle to increase the chances
          * that it's a valid bi_swizzle.
          */
         unsigned c[4];
         for (unsigned i = 0; i < 4; ++i)
            c[i] = src.swizzle[i % comps] & 3;
         has_swizzle = bi_swizzle_from_byte_channels(c, &swizzle);
      }

      if (has_swizzle) {
         idx.swizzle = swizzle;
         return idx;
      }

      bi_index v4_srcs[4];
      for (unsigned i = 0; i < comps; i++) {
         v4_srcs[i] = idx;
         v4_srcs[i].swizzle = BI_SWIZZLE_B0 + src.swizzle[i];
      }
      for (unsigned i = comps; i < 4; i++)
         v4_srcs[i] = bi_imm_u8(0);

      bi_index temp = bi_mkvec_v4i8(b, v4_srcs[0], v4_srcs[1],
                                    v4_srcs[2], v4_srcs[3]);

      static const enum bi_swizzle swizzle_lut[] = {
         BI_SWIZZLE_B0000, BI_SWIZZLE_B0101, BI_SWIZZLE_B0123, BI_SWIZZLE_B0123
      };
      assert(comps - 1 < ARRAY_SIZE(swizzle_lut));

      /* Assign a coherent swizzle for the vector */
      temp.swizzle = swizzle_lut[comps - 1];

      return temp;
   }

   return idx;
}

static bi_index
bi_swiz_b01(bi_index idx)
{
   enum bi_swizzle swizzle;
   bool valid = bi_try_compose_swizzles(&swizzle, BI_SWIZZLE_B01, idx.swizzle);
   assert(valid);

   idx.swizzle = swizzle;
   return idx;
}

static enum bi_round
bi_nir_round(nir_op op)
{
   switch (op) {
   case nir_op_fround_even:
      return BI_ROUND_NONE;
   case nir_op_ftrunc:
      return BI_ROUND_RTZ;
   case nir_op_fceil:
      return BI_ROUND_RTP;
   case nir_op_ffloor:
      return BI_ROUND_RTN;
   default:
      UNREACHABLE("invalid nir round op");
   }
}

/* Convenience for lowered transcendentals */

static bi_index
bi_fmul_f32(bi_builder *b, bi_index s0, bi_index s1)
{
   return bi_fma_f32(b, s0, s1, bi_imm_f32(-0.0f));
}

/* Approximate with FRCP_APPROX.f32 and apply a single iteration of
 * Newton-Raphson to improve precision */

static void
bi_lower_frcp_32(bi_builder *b, bi_index dst, bi_index s0)
{
   bi_index x1 = bi_frcp_approx_f32(b, s0);
   bi_index m = bi_frexpm_f32(b, s0, false, false);
   bi_index e = bi_frexpe_f32(b, bi_neg(s0), false, false);
   bi_index t1 = bi_fma_rscale_f32(b, m, bi_neg(x1), bi_imm_f32(1.0), bi_zero(),
                                   BI_SPECIAL_N);
   bi_fma_rscale_f32_to(b, dst, t1, x1, x1, e, BI_SPECIAL_NONE);
}

static void
bi_lower_frsq_32(bi_builder *b, bi_index dst, bi_index s0)
{
   bi_index x1 = bi_frsq_approx_f32(b, s0);
   bi_index m = bi_frexpm_f32(b, s0, false, true);
   bi_index e = bi_frexpe_f32(b, bi_neg(s0), false, true);
   bi_index t1 = bi_fmul_f32(b, x1, x1);
   bi_index t2 = bi_fma_rscale_f32(b, m, bi_neg(t1), bi_imm_f32(1.0),
                                   bi_imm_u32(-1), BI_SPECIAL_N);
   bi_fma_rscale_f32_to(b, dst, t2, x1, x1, e, BI_SPECIAL_N);
}

/* More complex transcendentals, see
 * https://gitlab.freedesktop.org/panfrost/mali-isa-docs/-/blob/master/Bifrost.adoc
 * for documentation */

static void
bi_lower_fexp2_32(bi_builder *b, bi_index dst, bi_index s0)
{
   bi_index t1 = bi_temp(b->shader);
   bi_instr *t1_instr = bi_fadd_f32_to(b, t1, s0, bi_imm_u32(0x49400000));
   t1_instr->clamp = BI_CLAMP_CLAMP_0_INF;

   bi_index t2 = bi_fadd_f32(b, t1, bi_imm_u32(0xc9400000));

   bi_instr *a2 = bi_fadd_f32_to(b, bi_temp(b->shader), s0, bi_neg(t2));
   a2->clamp = BI_CLAMP_CLAMP_M1_1;

   bi_index a1t = bi_fexp_table_u4(b, t1, BI_ADJ_NONE);
   bi_index t3 = bi_isub_u32(b, t1, bi_imm_u32(0x49400000), false);
   bi_index a1i = bi_arshift_i32(b, t3, bi_null(), bi_imm_u8(4));
   bi_index p1 = bi_fma_f32(b, a2->dest[0], bi_imm_u32(0x3d635635),
                            bi_imm_u32(0x3e75fffa));
   bi_index p2 = bi_fma_f32(b, p1, a2->dest[0], bi_imm_u32(0x3f317218));
   bi_index p3 = bi_fmul_f32(b, a2->dest[0], p2);
   bi_instr *x = bi_fma_rscale_f32_to(b, bi_temp(b->shader), p3, a1t, a1t, a1i,
                                      BI_SPECIAL_NONE);
   x->clamp = BI_CLAMP_CLAMP_0_INF;

   bi_instr *max = bi_fmax_f32_to(b, dst, x->dest[0], s0);
   max->sem = BI_SEM_NAN_PROPAGATE;
}

static void
bi_fexp_32(bi_builder *b, bi_index dst, bi_index s0, bi_index log2_base)
{
   /* Scale by base, Multiply by 2*24 and convert to integer to get a 8:24
    * fixed-point input */
   bi_index scale = bi_fma_rscale_f32(b, s0, log2_base, bi_negzero(),
                                      bi_imm_u32(24), BI_SPECIAL_NONE);
   bi_instr *fixed_pt = bi_f32_to_s32_to(b, bi_temp(b->shader), scale);
   fixed_pt->round = BI_ROUND_NONE; // XXX

   /* Compute the result for the fixed-point input, but pass along
    * the floating-point scale for correct NaN propagation */
   bi_fexp_f32_to(b, dst, fixed_pt->dest[0], scale);
}

static void
bi_lower_flog2_32(bi_builder *b, bi_index dst, bi_index s0)
{
   /* s0 = a1 * 2^e, with a1 in [0.75, 1.5) */
   bi_index a1 = bi_frexpm_f32(b, s0, true, false);
   bi_index ei = bi_frexpe_f32(b, s0, true, false);
   bi_index ef = bi_s32_to_f32(b, ei);

   /* xt estimates -log(r1), a coarse approximation of log(a1) */
   bi_index r1 = bi_flog_table_f32(b, s0, BI_MODE_RED, BI_PRECISION_NONE);
   bi_index xt = bi_flog_table_f32(b, s0, BI_MODE_BASE2, BI_PRECISION_NONE);

   /* log(s0) = log(a1 * 2^e) = e + log(a1) = e + log(a1 * r1) -
    * log(r1), so let x1 = e - log(r1) ~= e + xt and x2 = log(a1 * r1),
    * and then log(s0) = x1 + x2 */
   bi_index x1 = bi_fadd_f32(b, ef, xt);

   /* Since a1 * r1 is close to 1, x2 = log(a1 * r1) may be computed by
    * polynomial approximation around 1. The series is expressed around
    * 1, so set y = (a1 * r1) - 1.0 */
   bi_index y = bi_fma_f32(b, a1, r1, bi_imm_f32(-1.0));

   /* x2 = log_2(1 + y) = log_e(1 + y) * (1/log_e(2)), so approximate
    * log_e(1 + y) by the Taylor series (lower precision than the blob):
    * y - y^2/2 + O(y^3) = y(1 - y/2) + O(y^3) */
   bi_index loge =
      bi_fmul_f32(b, y, bi_fma_f32(b, y, bi_imm_f32(-0.5), bi_imm_f32(1.0)));

   bi_index x2 = bi_fmul_f32(b, loge, bi_imm_f32(1.0 / logf(2.0)));

   /* log(s0) = x1 + x2 */
   bi_fadd_f32_to(b, dst, x1, x2);
}

static void
bi_flog2_32(bi_builder *b, bi_index dst, bi_index s0)
{
   bi_index frexp = bi_frexpe_f32(b, s0, true, false);
   bi_index frexpi = bi_s32_to_f32(b, frexp);
   bi_index add = bi_fadd_lscale_f32(b, bi_imm_f32(-1.0f), s0);
   bi_fma_f32_to(b, dst, bi_flogd_f32(b, s0), add, frexpi);
}

static void
bi_lower_fpow_32(bi_builder *b, bi_index dst, bi_index base, bi_index exp)
{
   bi_index log2_base = bi_null();

   if (base.type == BI_INDEX_CONSTANT) {
      log2_base = bi_imm_f32(log2f(uif(base.value)));
   } else {
      log2_base = bi_temp(b->shader);
      bi_lower_flog2_32(b, log2_base, base);
   }

   return bi_lower_fexp2_32(b, dst, bi_fmul_f32(b, exp, log2_base));
}

static void
bi_fpow_32(bi_builder *b, bi_index dst, bi_index base, bi_index exp)
{
   bi_index log2_base = bi_null();

   if (base.type == BI_INDEX_CONSTANT) {
      log2_base = bi_imm_f32(log2f(uif(base.value)));
   } else {
      log2_base = bi_temp(b->shader);
      bi_flog2_32(b, log2_base, base);
   }

   return bi_fexp_32(b, dst, exp, log2_base);
}

/* Bifrost has extremely coarse tables for approximating sin/cos, accessible as
 * FSIN/COS_TABLE.u6, which multiplies the bottom 6-bits by pi/32 and
 * calculates the results. We use them to calculate sin/cos via a Taylor
 * approximation:
 *
 * f(x + e) = f(x) + e f'(x) + (e^2)/2 f''(x)
 * sin(x + e) = sin(x) + e cos(x) - (e^2)/2 sin(x)
 * cos(x + e) = cos(x) - e sin(x) - (e^2)/2 cos(x)
 */

#define TWO_OVER_PI  bi_imm_f32(2.0f / 3.14159f)
#define MPI_OVER_TWO bi_imm_f32(-3.14159f / 2.0)
#define SINCOS_BIAS  bi_imm_u32(0x49400000)

static void
bi_lower_fsincos_32(bi_builder *b, bi_index dst, bi_index s0, bool cos)
{
   /* bottom 6-bits of result times pi/32 approximately s0 mod 2pi */
   bi_index x_u6 = bi_fma_f32(b, s0, TWO_OVER_PI, SINCOS_BIAS);

   /* Approximate domain error (small) */
   bi_index e = bi_fma_f32(b, bi_fadd_f32(b, x_u6, bi_neg(SINCOS_BIAS)),
                           MPI_OVER_TWO, s0);

   /* Lookup sin(x), cos(x) */
   bi_index sinx = bi_fsin_table_u6(b, x_u6, false);
   bi_index cosx = bi_fcos_table_u6(b, x_u6, false);

   /* e^2 / 2 */
   bi_index e2_over_2 =
      bi_fma_rscale_f32(b, e, e, bi_negzero(), bi_imm_u32(-1), BI_SPECIAL_NONE);

   /* (-e^2)/2 f''(x) */
   bi_index quadratic =
      bi_fma_f32(b, bi_neg(e2_over_2), cos ? cosx : sinx, bi_negzero());

   /* e f'(x) - (e^2/2) f''(x) */
   bi_instr *I = bi_fma_f32_to(b, bi_temp(b->shader), e,
                               cos ? bi_neg(sinx) : cosx, quadratic);
   I->clamp = BI_CLAMP_CLAMP_M1_1;

   /* f(x) + e f'(x) - (e^2/2) f''(x) */
   bi_fadd_f32_to(b, dst, I->dest[0], cos ? cosx : sinx);
}

static enum bi_cmpf
bi_translate_cmpf(nir_op op)
{
   switch (op) {
   case nir_op_ieq8:
   case nir_op_ieq16:
   case nir_op_ieq32:
   case nir_op_feq16:
   case nir_op_feq32:
      return BI_CMPF_EQ;

   case nir_op_ine8:
   case nir_op_ine16:
   case nir_op_ine32:
   case nir_op_fneu16:
   case nir_op_fneu32:
      return BI_CMPF_NE;

   case nir_op_ilt8:
   case nir_op_ilt16:
   case nir_op_ilt32:
   case nir_op_flt16:
   case nir_op_flt32:
   case nir_op_ult8:
   case nir_op_ult16:
   case nir_op_ult32:
      return BI_CMPF_LT;

   case nir_op_ige8:
   case nir_op_ige16:
   case nir_op_ige32:
   case nir_op_fge16:
   case nir_op_fge32:
   case nir_op_uge8:
   case nir_op_uge16:
   case nir_op_uge32:
      return BI_CMPF_GE;

   default:
      UNREACHABLE("invalid comparison");
   }
}

static bool
bi_nir_is_replicated(nir_alu_src *src)
{
   for (unsigned i = 1; i < nir_src_num_components(src->src); ++i) {
      if (src->swizzle[0] != src->swizzle[i])
         return false;
   }

   return true;
}

static void
bi_emit_alu(bi_builder *b, nir_alu_instr *instr)
{
   bi_index dst = bi_def_index(&instr->def);
   unsigned srcs = nir_op_infos[instr->op].num_inputs;
   unsigned sz = instr->def.bit_size;
   unsigned comps = instr->def.num_components;
   unsigned src_sz = srcs > 0 ? nir_src_bit_size(instr->src[0].src) : 0;

   /* Indicate scalarness */
   if (sz == 16 && comps == 1)
      dst.swizzle = BI_SWIZZLE_H00;

   /* First, match against the various moves in NIR. These are
    * special-cased because they can operate on vectors even after
    * lowering ALU to scalar. For Bifrost, bi_alu_src_index assumes the
    * instruction is no "bigger" than SIMD-within-a-register. These moves
    * are the exceptions that need to handle swizzles specially. */

   switch (instr->op) {
   case nir_op_mov: {
      bi_index idx = bi_src_index(&instr->src[0].src);
      bi_index unoffset_srcs[4] = {idx, idx, idx, idx};

      unsigned channels[4] = {
         comps > 0 ? instr->src[0].swizzle[0] : 0,
         comps > 1 ? instr->src[0].swizzle[1] : 0,
         comps > 2 ? instr->src[0].swizzle[2] : 0,
         comps > 3 ? instr->src[0].swizzle[3] : 0,
      };

      bi_make_vec_to(b, dst, unoffset_srcs, channels, comps, src_sz);
      return;
   }

   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_vec8:
   case nir_op_vec16:
   case nir_op_pack_32_2x16_split:
   case nir_op_pack_32_4x8_split:
   case nir_op_pack_64_2x32_split: {
      bi_index unoffset_srcs[16] = {bi_null()};
      unsigned channels[16] = {0};

      for (unsigned i = 0; i < srcs; ++i) {
         assert(nir_src_bit_size(instr->src[i].src) == src_sz);
         unoffset_srcs[i] = bi_src_index(&instr->src[i].src);
         channels[i] = instr->src[i].swizzle[0];
      }

      bi_make_vec_to(b, dst, unoffset_srcs, channels, srcs, src_sz);
      return;
   }

   case nir_op_pack_32_2x16:
   case nir_op_pack_32_4x8:
   case nir_op_pack_64_2x32:
   case nir_op_pack_64_4x16: {
      bi_index idx = bi_src_index(&instr->src[0].src);
      bi_index unoffset_srcs[4] = { idx, idx, idx, idx };

      unsigned src_comps = nir_op_infos[instr->op].input_sizes[0];
      assert(src_sz * src_comps == sz);

      unsigned channels[4] = { 0 };
      for (uint32_t i = 0; i < src_comps; i++)
         channels[i] = instr->src[0].swizzle[i];

      bi_make_vec_to(b, dst, unoffset_srcs, channels, src_comps, src_sz);
      return;
   }

   case nir_op_unpack_32_2x16:
   case nir_op_unpack_32_4x8:
   case nir_op_unpack_64_2x32:
   case nir_op_unpack_64_4x16: {
      assert(comps * sz == src_sz);
      bi_index idx = bi_src_index(&instr->src[0].src);
      unsigned chan = instr->src[0].swizzle[0];
      bi_make_vec_to(b, dst, &idx, &chan, 1, src_sz);
      return;
   }

   case nir_op_unpack_32_2x16_split_x:
   case nir_op_unpack_32_2x16_split_y: {
      assert(comps <= 2);

      bi_index idx = bi_src_index(&instr->src[0].src);
      bi_index srcs[2] = {idx, idx};

      unsigned offset = instr->op == nir_op_unpack_32_2x16_split_x ? 0 : 1;
      unsigned channels[2] = {
         comps > 0 ? instr->src[0].swizzle[0] * 2 + offset : 0,
         comps > 1 ? instr->src[0].swizzle[1] * 2 + offset : 0,
      };

      bi_make_vec_to(b, dst, srcs, channels, comps, 16);
      return;
   }

   case nir_op_unpack_64_2x32_split_x:
   case nir_op_unpack_64_2x32_split_y: {
      bi_index idx = bi_src_index(&instr->src[0].src);

      unsigned offset = instr->op == nir_op_unpack_64_2x32_split_x ? 0 : 1;
      unsigned chan = instr->src[0].swizzle[0] * 2 + offset;

      bi_mov_i32_to(b, dst, bi_extract(b, idx, chan));
      return;
   }

   case nir_op_pack_uvec2_to_uint: {
      bi_index src = bi_src_index(&instr->src[0].src);

      assert(sz == 32 && src_sz == 32);
      bi_mkvec_v2i16_to(
         b, dst, bi_half(bi_extract(b, src, instr->src[0].swizzle[0]), false),
         bi_half(bi_extract(b, src, instr->src[0].swizzle[1]), false));
      return;
   }

   case nir_op_pack_uvec4_to_uint: {
      bi_index src = bi_src_index(&instr->src[0].src);

      assert(sz == 32 && src_sz == 32);

      bi_index srcs[4] = {
         bi_extract(b, src, instr->src[0].swizzle[0]),
         bi_extract(b, src, instr->src[0].swizzle[1]),
         bi_extract(b, src, instr->src[0].swizzle[2]),
         bi_extract(b, src, instr->src[0].swizzle[3]),
      };
      unsigned channels[4] = {0};
      bi_make_vec_to(b, dst, srcs, channels, 4, 8);
      return;
   }

   case nir_op_f2f16:
   case nir_op_f2f16_rtz:
   case nir_op_f2f16_rtne: {
      /* Starting with v11, we don't have V2XXX_TO_V2F16, this should have been
       * lowered before if there is more than one components */
      assert(b->shader->arch < 11 || comps == 1);
      assert(src_sz == 32);
      bi_index idx = bi_src_index(&instr->src[0].src);
      bi_index s0 = bi_extract(b, idx, instr->src[0].swizzle[0]);

      bi_instr *I;

      /* Use V2F32_TO_V2F16 if vectorized */
      if (comps == 2) {
         /* Starting with v11, we don't have V2F32_TO_V2F16, this should have
          * been lowered before if there is more than one components */
         assert(b->shader->arch < 11);
         bi_index s1 = bi_extract(b, idx, instr->src[0].swizzle[1]);
         I = bi_v2f32_to_v2f16_to(b, dst, s0, s1);
      } else {
         assert(comps == 1);
         I = bi_f32_to_f16_to(b, dst, s0);
      }

      /* Override rounding if explicitly requested. Otherwise, the
       * default rounding mode is selected by the builder. Depending
       * on the float controls required by the shader, the default
       * mode may not be nearest-even.
       */
      if (instr->op == nir_op_f2f16_rtz)
         I->round = BI_ROUND_RTZ;
      else if (instr->op == nir_op_f2f16_rtne)
         I->round = BI_ROUND_NONE; /* Nearest even */

      return;
   }

   /* Vectorized downcasts */
   case nir_op_u2u16:
   case nir_op_i2i16: {
      if (!(src_sz == 32 && comps == 2))
         break;

      bi_index idx = bi_src_index(&instr->src[0].src);
      bi_index s0 = bi_extract(b, idx, instr->src[0].swizzle[0]);
      bi_index s1 = bi_extract(b, idx, instr->src[0].swizzle[1]);

      bi_mkvec_v2i16_to(b, dst, bi_half(s0, false), bi_half(s1, false));
      return;
   }

   /* Pre-v11, we can get vector i2f32 by lowering 32-bit vector i2f16 to
    * i2f32 + f2f16 in bifrost_nir_lower_algebraic_late, which runs after
    * nir_opt_vectorize. We don't scalarize i2f32 earlier because we have
    * vector V2F16_TO_V2F32. */
   case nir_op_i2f32:
   case nir_op_u2f32: {
      if (!(src_sz == 32 && comps == 2))
         break;

      assert(b->shader->arch < 11);

      nir_alu_src *src = &instr->src[0];
      bi_index idx = bi_src_index(&src->src);
      bi_index s0 = bi_extract(b, idx, src->swizzle[0]);
      bi_index s1 = bi_extract(b, idx, src->swizzle[1]);

      bi_index d0, d1;
      if (instr->op == nir_op_i2f32) {
         d0 = bi_s32_to_f32(b, s0);
         d1 = bi_s32_to_f32(b, s1);
      } else {
         d0 = bi_u32_to_f32(b, s0);
         d1 = bi_u32_to_f32(b, s1);
      }

      bi_collect_v2i32_to(b, dst, d0, d1);

      return;
   }

   case nir_op_i2i8:
   case nir_op_u2u8: {
      /* Acts like an 8-bit swizzle */
      bi_index idx = bi_src_index(&instr->src[0].src);
      unsigned factor = src_sz / 8;
      unsigned chan[4] = {0};
      bi_index idxs[4];

      for (unsigned i = 0; i < comps; ++i) {
         idxs[i] = idx;
         chan[i] = instr->src[0].swizzle[i] * factor;
      }

      bi_make_vec_to(b, dst, idxs, chan, comps, 8);
      return;
   }

   default:
      break;
   }

   bi_index s0 =
      srcs > 0 ? bi_alu_src_index(b, instr->src[0], comps) : bi_null();
   bi_index s1 =
      srcs > 1 ? bi_alu_src_index(b, instr->src[1], comps) : bi_null();
   bi_index s2 =
      srcs > 2 ? bi_alu_src_index(b, instr->src[2], comps) : bi_null();

   switch (instr->op) {
   case nir_op_ffma:
      bi_fma_to(b, sz, dst, s0, s1, s2);
      break;

   case nir_op_fmul:
      bi_fma_to(b, sz, dst, s0, s1, bi_negzero());
      break;

   case nir_op_fadd:
      bi_fadd_to(b, sz, dst, s0, s1);
      break;

   case nir_op_fsat: {
      bi_instr *I = bi_fclamp_to(b, sz, dst, s0);
      I->clamp = BI_CLAMP_CLAMP_0_1;
      break;
   }

   case nir_op_fsat_signed: {
      bi_instr *I = bi_fclamp_to(b, sz, dst, s0);
      I->clamp = BI_CLAMP_CLAMP_M1_1;
      break;
   }

   case nir_op_fclamp_pos: {
      bi_instr *I = bi_fclamp_to(b, sz, dst, s0);
      I->clamp = BI_CLAMP_CLAMP_0_INF;
      break;
   }

   case nir_op_fneg:
      bi_fabsneg_to(b, sz, dst, bi_neg(s0));
      break;

   case nir_op_fabs:
      bi_fabsneg_to(b, sz, dst, bi_abs(s0));
      break;

   case nir_op_fsin:
      bi_lower_fsincos_32(b, dst, s0, false);
      break;

   case nir_op_fcos:
      bi_lower_fsincos_32(b, dst, s0, true);
      break;

   case nir_op_fexp2:
      assert(sz == 32); /* should've been lowered */

      if (b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
         bi_lower_fexp2_32(b, dst, s0);
      else
         bi_fexp_32(b, dst, s0, bi_imm_f32(1.0f));

      break;

   case nir_op_flog2:
      assert(sz == 32); /* should've been lowered */

      if (b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
         bi_lower_flog2_32(b, dst, s0);
      else
         bi_flog2_32(b, dst, s0);

      break;

   case nir_op_fpow:
      assert(sz == 32); /* should've been lowered */

      if (b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
         bi_lower_fpow_32(b, dst, s0, s1);
      else
         bi_fpow_32(b, dst, s0, s1);

      break;

   case nir_op_frexp_exp:
      /* v11 removed FREXPE.v2f16 */
      assert(src_sz == 32 || (b->shader->arch < 11 && src_sz == 16));
      bi_frexpe_to(b, sz, dst, s0, false, false);
      break;

   case nir_op_frexp_sig:
      /* v11 removed FREXPM.v2f16 */
      assert(src_sz == 32 || (b->shader->arch < 11 && src_sz == 16));
      bi_frexpm_to(b, sz, dst, s0, false, false);
      break;

   case nir_op_ldexp:
      assert(sz == 32 && "should be lowered");
      bi_ldexp_to(b, sz, dst, s0, s1);
      break;

   case nir_op_ldexp16_pan:
      assert(sz == 16);
      bi_ldexp_to(b, sz, dst, s0, s1);
      break;

   case nir_op_b8csel:
      assert(sz == 8);
      bi_mux_v4i8_to(b, dst, s2, s1, s0, BI_MUX_INT_ZERO);
      break;

   case nir_op_b16csel:
      assert(sz == 16);
      bi_mux_v2i16_to(b, dst, s2, s1, s0, BI_MUX_INT_ZERO);
      break;

   case nir_op_b32csel:
      assert(sz == 32);
      bi_mux_i32_to(b, dst, s2, s1, s0, BI_MUX_INT_ZERO);
      break;

   case nir_op_extract_u8:
   case nir_op_extract_i8: {
      if (sz == 32) {
         assert(comps == 1 && "should be scalarized");
         unsigned byte = nir_alu_src_as_uint(instr->src[1]);
         if (instr->op == nir_op_extract_i8)
            bi_s8_to_s32_to(b, dst, bi_byte(s0, byte));
         else
            bi_u8_to_u32_to(b, dst, bi_byte(s0, byte));
      } else if (sz == 16) {
         assert(comps <= 2);
         const unsigned byte_x = nir_alu_src_comp_as_uint(instr->src[1], 0);
         const unsigned byte_y = comps == 1 ? byte_x :
                                 nir_alu_src_comp_as_uint(instr->src[1], 1);
         assert(byte_x < 2 && byte_y < 2);

         /* Construct the swizzle for the select.  We know it's valid because
          * it's guaranteed to be a BXXYY swizzle, even when composed with
          * s0.swizzle.
          */
         enum bi_swizzle swizzle = BI_SWIZZLE_H01;
         const unsigned bytes[4] = { byte_x, byte_x, 2 + byte_y, 2 + byte_y };
         bool valid = bi_swizzle_from_byte_channels(bytes, &swizzle);
         assert(valid);

         valid = bi_try_compose_swizzles(&swizzle, swizzle, s0.swizzle);
         assert(valid);

         bi_index src = s0;
         src.swizzle = swizzle;

         if (instr->op == nir_op_extract_i8)
            bi_v2s8_to_v2s16_to(b, dst, src);
         else
            bi_v2u8_to_v2u16_to(b, dst, src);
      } else {
         UNREACHABLE("should be lowered");
      }
      break;
   }

   case nir_op_extract_u16:
   case nir_op_extract_i16: {
      assert(comps == 1 && "should be scalarized");
      assert(src_sz == 32 && "should be lowered");
      unsigned half = nir_alu_src_as_uint(instr->src[1]);
      assert(half == 0 || half == 1);

      if (instr->op == nir_op_extract_i16)
         bi_s16_to_s32_to(b, dst, bi_half(s0, half));
      else
         bi_u16_to_u32_to(b, dst, bi_half(s0, half));
      break;
   }

   case nir_op_insert_u16: {
      assert(comps == 1 && "should be scalarized");
      unsigned half = nir_alu_src_as_uint(instr->src[1]);
      assert(half == 0 || half == 1);

      if (half == 0)
         bi_u16_to_u32_to(b, dst, bi_half(s0, 0));
      else
         bi_mkvec_v2i16_to(b, dst, bi_imm_u16(0), bi_half(s0, 0));
      break;
   }

   case nir_op_pack_half_2x16_split:
      /* On v11+, V2F32_TO_V2F16 is gone. This should be lowered in
       * bifrost_nir_lower_algebraic_late. */
      assert(b->shader->arch < 11);

      bi_v2f32_to_v2f16_to(b, dst, s0, s1);
      break;

   case nir_op_ishl:
      bi_lshift_or_to(b, sz, dst, s0, bi_zero(), bi_byte(s1, 0));
      break;
   case nir_op_ushr:
      bi_rshift_or_to(b, sz, dst, s0, bi_zero(), bi_byte(s1, 0), false);
      break;

   case nir_op_ishr:
      if (b->shader->arch >= 9)
         bi_rshift_or_to(b, sz, dst, s0, bi_zero(), bi_byte(s1, 0), true);
      else
         bi_arshift_to(b, sz, dst, s0, bi_null(), bi_byte(s1, 0));
      break;

   case nir_op_imin:
   case nir_op_umin:
      bi_csel_to(b, nir_op_infos[instr->op].input_types[0], sz, dst, s0, s1, s0,
                 s1, BI_CMPF_LT);
      break;

   case nir_op_imax:
   case nir_op_umax:
      bi_csel_to(b, nir_op_infos[instr->op].input_types[0], sz, dst, s0, s1, s0,
                 s1, BI_CMPF_GT);
      break;

   case nir_op_f2f32:
      bi_f16_to_f32_to(b, dst, s0);
      break;

   case nir_op_fquantize2f16: {
      bi_instr *f16 =
         bi_f32_to_f16_to(b, bi_half(bi_temp(b->shader), false), s0);

      if (b->shader->arch < 9) {
         /* Bifrost has psuedo-ftz on conversions, that is lowered to an ftz
          * flag in the clause header */
         f16->ftz = true;
      } else {
         /* Valhall doesn't have clauses, and uses a separate flush
          * instruction */
         f16 = bi_flush_to(b, 16, bi_half(bi_temp(b->shader), false), f16->dest[0]);
         f16->ftz = true;
      }

      bi_instr *f32 = bi_f16_to_f32_to(b, dst, f16->dest[0]);

      if (b->shader->arch < 9)
         f32->ftz = true;

      break;
   }

   case nir_op_f2i32:
   case nir_op_f2i32_rtne: {
      /* v11 removed F16_TO_S32 */
      assert(src_sz == 32 || (b->shader->arch < 11 && src_sz == 16));

      bi_instr *I;
      if (src_sz == 32)
         I = bi_f32_to_s32_to(b, dst, s0);
      else
         I = bi_f16_to_s32_to(b, dst, s0);

      if (instr->op == nir_op_f2i32_rtne)
         I->round = BI_ROUND_NONE;
      break;
   }

   /* Note 32-bit sources => no vectorization, so 32-bit works */
   case nir_op_f2u16:
      /* v11 removed V2F16_TO_V2U16 */
      assert(src_sz == 32 || (b->shader->arch < 11 && src_sz == 16));

      if (src_sz == 32)
         bi_f32_to_u32_to(b, dst, s0);
      else
         bi_v2f16_to_v2u16_to(b, dst, s0);
      break;

   case nir_op_f2i16:
      /* v11 removed V2F16_TO_V2S16 */
      assert(src_sz == 32 || (b->shader->arch < 11 && src_sz == 16));

      if (src_sz == 32)
         bi_f32_to_s32_to(b, dst, s0);
      else
         bi_v2f16_to_v2s16_to(b, dst, s0);
      break;

   case nir_op_f2u32:
   case nir_op_f2u32_rtne: {
      /* v11 removed F16_TO_U32 */
      assert(src_sz == 32 || (b->shader->arch < 11 && src_sz == 16));

      bi_instr *I;
      if (src_sz == 32)
         I = bi_f32_to_u32_to(b, dst, s0);
      else
         I = bi_f16_to_u32_to(b, dst, s0);

      if (instr->op == nir_op_f2u32_rtne)
         I->round = BI_ROUND_NONE;
      break;
   }

   case nir_op_u2f16:
      /* Starting with v11, we don't have V2XXX_TO_V2F16, this should have been
       * lowered before by algebraic. */
      assert(b->shader->arch < 11);

      /* V2I32_TO_V2F16 does not exist */
      assert((src_sz == 16 || src_sz == 8) && "should be lowered");

      if (src_sz == 16)
         bi_v2u16_to_v2f16_to(b, dst, s0);
      else if (src_sz == 8)
         bi_v2u8_to_v2f16_to(b, dst, bi_swiz_b01(s0));
      break;

   case nir_op_u2f32:
      /* v11 removed U16_TO_F32 and U8_TO_F32 */
      assert(src_sz == 32 || (b->shader->arch < 11 && (src_sz == 16 || src_sz == 8)));

      if (src_sz == 32)
         bi_u32_to_f32_to(b, dst, s0);
      else if (src_sz == 16)
         bi_u16_to_f32_to(b, dst, s0);
      else
         bi_u8_to_f32_to(b, dst, s0);
      break;

   case nir_op_i2f16:
      /* Starting with v11, we don't have V2XXX_TO_V2F16, this should have been
       * lowered before by algebraic. */
      assert(b->shader->arch < 11);

      /* V2I32_TO_V2F16 does not exist */
      assert((src_sz == 16 || src_sz == 8) && "should be lowered");

      if (src_sz == 16)
         bi_v2s16_to_v2f16_to(b, dst, s0);
      else if (src_sz == 8)
         bi_v2s8_to_v2f16_to(b, dst, bi_swiz_b01(s0));
      break;

   case nir_op_i2f32:
      /* v11 removed S16_TO_F32 and S8_TO_F32 */
      assert(src_sz == 32 || (b->shader->arch < 11 && (src_sz == 16 || src_sz == 8)));

      if (src_sz == 32)
         bi_s32_to_f32_to(b, dst, s0);
      else if (src_sz == 16)
         bi_s16_to_f32_to(b, dst, s0);
      else if (src_sz == 8)
         bi_s8_to_f32_to(b, dst, s0);
      break;

   case nir_op_i2i32:
      assert(src_sz == 32 || src_sz == 16 || src_sz == 8);

      if (src_sz == 32)
         bi_mov_i32_to(b, dst, s0);
      else if (src_sz == 16)
         bi_s16_to_s32_to(b, dst, s0);
      else if (src_sz == 8)
         bi_s8_to_s32_to(b, dst, s0);
      break;

   case nir_op_u2u32:
      assert(src_sz == 32 || src_sz == 16 || src_sz == 8);

      if (src_sz == 32)
         bi_mov_i32_to(b, dst, s0);
      else if (src_sz == 16)
         bi_u16_to_u32_to(b, dst, s0);
      else if (src_sz == 8)
         bi_u8_to_u32_to(b, dst, s0);

      break;

   case nir_op_i2i16:
      assert(src_sz == 8 || src_sz == 32);

      if (src_sz == 8)
         bi_v2s8_to_v2s16_to(b, dst, bi_swiz_b01(s0));
      else
         bi_mov_i32_to(b, dst, s0);
      break;

   case nir_op_u2u16:
      assert(src_sz == 8 || src_sz == 32);

      if (src_sz == 8)
         bi_v2u8_to_v2u16_to(b, dst, bi_swiz_b01(s0));
      else
         bi_mov_i32_to(b, dst, s0);
      break;

   case nir_op_b2i8:
   case nir_op_b2i16:
   case nir_op_b2i32:
      bi_mux_to(b, sz, dst, bi_imm_u8(0), bi_imm_uintN(1, sz), s0,
                BI_MUX_INT_ZERO);
      break;

   case nir_op_ieq8:
   case nir_op_ine8:
   case nir_op_ilt8:
   case nir_op_ige8:
   case nir_op_ieq16:
   case nir_op_ine16:
   case nir_op_ilt16:
   case nir_op_ige16:
   case nir_op_ieq32:
   case nir_op_ine32:
   case nir_op_ilt32:
   case nir_op_ige32:
      bi_icmp_to(b, nir_type_int, sz, dst, s0, s1, bi_translate_cmpf(instr->op),
                 BI_RESULT_TYPE_M1);
      break;

   case nir_op_ult8:
   case nir_op_uge8:
   case nir_op_ult16:
   case nir_op_uge16:
   case nir_op_ult32:
   case nir_op_uge32:
      bi_icmp_to(b, nir_type_uint, sz, dst, s0, s1,
                 bi_translate_cmpf(instr->op), BI_RESULT_TYPE_M1);
      break;

   case nir_op_feq32:
   case nir_op_feq16:
   case nir_op_flt32:
   case nir_op_flt16:
   case nir_op_fge32:
   case nir_op_fge16:
   case nir_op_fneu32:
   case nir_op_fneu16:
      bi_fcmp_to(b, sz, dst, s0, s1, bi_translate_cmpf(instr->op),
                 BI_RESULT_TYPE_M1);
      break;

   case nir_op_fround_even:
   case nir_op_fceil:
   case nir_op_ffloor:
   case nir_op_ftrunc: {
      /* On v11+, FROUND.v2s16 is gone, we lower this in nir_lower_bit_size */
      assert(sz != 16 || b->shader->arch < 11);

      enum bi_round round = bi_nir_round(instr->op);

      /* On v11+, FROUND does not flush subnormals to zero even when configured
       * in the shader program header */
      if (b->shader->arch >= 11 &&
          (round == BI_ROUND_RTP || round == BI_ROUND_RTN) &&
          b->shader->ftz_fp32) {
         bi_instr *flush = bi_flush_to(b, 32, bi_temp(b->shader), s0);
         flush->ftz = true;
         s0 = flush->dest[0];
      }

      bi_fround_to(b, sz, dst, s0, round);
      break;
   }

   case nir_op_fmin:
      bi_fmin_to(b, sz, dst, s0, s1);
      break;

   case nir_op_fmax:
      bi_fmax_to(b, sz, dst, s0, s1);
      break;

   case nir_op_iadd:
      if (sz == 64) {
         assert(b->shader->arch >= 9);
         bi_shaddx_s64_to(b, dst, s0, s1, 0);
         bi_index dsts[4] = {bi_null(), bi_null(), bi_null(), bi_null()};
         bi_emit_split_i32(b, dsts, dst, 2);
         bi_cache_collect(b, dst, dsts, 2);
      } else
         bi_iadd_to(b, nir_type_int, sz, dst, s0, s1, false);
      break;

   case nir_op_iadd_sat:
      bi_iadd_to(b, nir_type_int, sz, dst, s0, s1, true);
      break;

   case nir_op_uadd_sat:
      bi_iadd_to(b, nir_type_uint, sz, dst, s0, s1, true);
      break;

   case nir_op_ihadd:
      /* v11 removed HADD */
      assert(b->shader->arch < 11);
      bi_hadd_to(b, nir_type_int, sz, dst, s0, s1, BI_ROUND_RTN);
      break;

   case nir_op_irhadd:
      /* v11 removed HADD */
      assert(b->shader->arch < 11);
      bi_hadd_to(b, nir_type_int, sz, dst, s0, s1, BI_ROUND_RTP);
      break;

   case nir_op_uhadd:
      /* v11 removed HADD */
      assert(b->shader->arch < 11);
      bi_hadd_to(b, nir_type_uint, sz, dst, s0, s1, BI_ROUND_RTN);
      break;

   case nir_op_urhadd:
      /* v11 removed HADD */
      assert(b->shader->arch < 11);
      bi_hadd_to(b, nir_type_uint, sz, dst, s0, s1, BI_ROUND_RTP);
      break;

   case nir_op_ineg:
      bi_isub_to(b, nir_type_int, sz, dst, bi_zero(), s0, false);
      break;

   case nir_op_isub:
      bi_isub_to(b, nir_type_int, sz, dst, s0, s1, false);
      break;

   case nir_op_isub_sat:
      bi_isub_to(b, nir_type_int, sz, dst, s0, s1, true);
      break;

   case nir_op_usub_sat:
      bi_isub_to(b, nir_type_uint, sz, dst, s0, s1, true);
      break;

   case nir_op_imul:
      bi_imul_to(b, sz, dst, s0, s1);
      break;

   case nir_op_iabs:
      bi_iabs_to(b, sz, dst, s0);
      break;

   case nir_op_iand:
      bi_lshift_and_to(b, sz, dst, s0, s1, bi_imm_u8(0));
      break;

   case nir_op_ior:
      bi_lshift_or_to(b, sz, dst, s0, s1, bi_imm_u8(0));
      break;

   case nir_op_ixor:
      bi_lshift_xor_to(b, sz, dst, s0, s1, bi_imm_u8(0));
      break;

   case nir_op_inot:
      bi_lshift_or_to(b, sz, dst, bi_zero(), bi_not(s0), bi_imm_u8(0));
      break;

   case nir_op_frsq:
      if (sz == 32 && b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
         bi_lower_frsq_32(b, dst, s0);
      else
         bi_frsq_to(b, sz, dst, s0);
      break;

   case nir_op_frcp:
      if (sz == 32 && b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
         bi_lower_frcp_32(b, dst, s0);
      else
         bi_frcp_to(b, sz, dst, s0);
      break;

   case nir_op_uclz:
      bi_clz_to(b, sz, dst, s0, false);
      break;

   case nir_op_bit_count:
      assert(sz == 32 && src_sz == 32 && "should've been lowered");
      bi_popcount_i32_to(b, dst, s0);
      break;

   case nir_op_bitfield_reverse:
      assert(sz == 32 && src_sz == 32 && "should've been lowered");
      bi_bitrev_i32_to(b, dst, s0);
      break;

   case nir_op_bitfield_select:
      /* Bifrost doesn't have MUX.v4i8.bit but that's okay because MUX.v4i8
       * can't swizzle on Bifrost anyway so we can just use MUX.i32.
       */
      if (b->shader->arch < 9 && sz == 8)
         bi_mux_i32_to(b, dst, s1, s2, s0, BI_MUX_BIT);
      else
         bi_mux_to(b, sz, dst, s1, s2, s0, BI_MUX_BIT);
      break;

   case nir_op_ufind_msb: {
      bi_index clz = bi_clz(b, src_sz, s0, false);

      if (sz == 8)
         clz = bi_byte(clz, 0);
      else if (sz == 16)
         clz = bi_half(clz, false);

      bi_isub_u32_to(b, dst, bi_imm_u32(src_sz - 1), clz, false);
      break;
   }
   case nir_op_udot_4x8_uadd_sat:
   case nir_op_udot_4x8_uadd: {
      assert(b->shader->arch >= 9);
      bi_idpadd_v4u8_to(b, dst, s0, s1, s2,
                        instr->op == nir_op_udot_4x8_uadd_sat);
      break;
   }
   case nir_op_sdot_4x8_iadd_sat:
   case nir_op_sdot_4x8_iadd: {
      assert(b->shader->arch >= 9);
      bi_idpadd_v4s8_to(b, dst, s0, s1, s2,
                        instr->op == nir_op_sdot_4x8_iadd_sat);
      break;
   }

   default:
      fprintf(stderr, "Unhandled ALU op %s\n", nir_op_infos[instr->op].name);
      UNREACHABLE("Unknown ALU op");
   }
}

static enum bi_dimension
valhall_tex_dimension(enum glsl_sampler_dim dim)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_1D:
   case GLSL_SAMPLER_DIM_BUF:
      return BI_DIMENSION_1D;

   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_MS:
   case GLSL_SAMPLER_DIM_EXTERNAL:
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_SUBPASS:
   case GLSL_SAMPLER_DIM_SUBPASS_MS:
      return BI_DIMENSION_2D;

   case GLSL_SAMPLER_DIM_3D:
      return BI_DIMENSION_3D;

   case GLSL_SAMPLER_DIM_CUBE:
      return BI_DIMENSION_CUBE;

   default:
      UNREACHABLE("Unknown sampler dim type");
   }
}

static void
bi_emit_tex_valhall(bi_builder *b, nir_tex_instr *tex)
{
   nir_def *tex_h = NULL, *sr0 = NULL, *sr1 = NULL;
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_texture_handle: tex_h =  tex->src[i].src.ssa; break;
      case nir_tex_src_backend1:       sr0 =    tex->src[i].src.ssa; break;
      case nir_tex_src_backend2:       sr1 =    tex->src[i].src.ssa; break;
      default: UNREACHABLE("Unknown texture source");
      }
   }

   struct pan_va_tex_flags flags;
   STATIC_ASSERT(sizeof(tex->backend_flags) == sizeof(flags));
   memcpy(&flags, &tex->backend_flags, sizeof(flags));

   bi_index src0 = bi_extract(b, bi_def_index(tex_h), 0);
   bi_index src1 = bi_extract(b, bi_def_index(tex_h), 1);

   unsigned sr_count = 0;
   bi_index sr_comps[8];
   for (unsigned i = 0; i < sr0->num_components; i++)
      sr_comps[sr_count++] = bi_extract(b, bi_def_index(sr0), i);
   if (sr1 != NULL) {
      for (unsigned i = 0; i < sr1->num_components; i++)
         sr_comps[sr_count++] = bi_extract(b, bi_def_index(sr1), i);
   }

   bi_index sr = bi_temp(b->shader);
   bi_emit_collect_to(b, sr, sr_comps, sr_count);

   const enum bi_dimension dim = valhall_tex_dimension(tex->sampler_dim);
   const enum bi_register_format regfmt = bi_reg_fmt_for_nir(tex->dest_type);
   const unsigned mask = nir_def_components_read(&tex->def);
   bi_index dest = bi_temp(b->shader);

   switch (tex->op) {
   case nir_texop_tex:
   case nir_texop_txb:
   case nir_texop_txl:
   case nir_texop_txd:
      bi_tex_single_to(b, dest, sr, src0, src1, flags.array_enable, dim,
                       regfmt, flags.compare_enable, flags.texel_offset,
                       flags.lod_mode, flags.wide_indices,
                       mask, sr_count);
      break;
   case nir_texop_txf:
   case nir_texop_txf_ms:
      bi_tex_fetch_to(b, dest, sr, src0, src1, flags.array_enable, dim,
                      regfmt, flags.texel_offset, flags.wide_indices,
                      mask, sr_count);
      break;
   case nir_texop_tg4:
      bi_tex_gather_to(b, dest, sr, src0, src1, flags.array_enable, dim,
                       tex->component, false, regfmt, flags.compare_enable,
                       flags.texel_offset, flags.wide_indices,
                       mask, sr_count);
      break;
   case nir_texop_gradient_pan: {
      bi_instr *I =
         bi_tex_gradient_to(b, dest, sr, src0, src1, dim,
                            flags.wide_indices,
                            mask, sr_count);
      I->force_delta_enable = flags.force_delta_enable;
      I->derivative_enable = flags.derivative_enable;
      I->lod_clamp_disable = flags.lod_clamp_disable;
      I->lod_bias_disable = flags.lod_bias_disable;
      I->register_format = BI_REGISTER_FORMAT_U32;
      break;
   }
   default:
      UNREACHABLE("Unhandled Valhall texture op");
   }

   bi_emit_cached_split(b, dest, util_bitcount(mask) * tex->def.bit_size);
   bi_index dest4[4] = { dest, dest, dest, dest };

   unsigned channel[4] = { };
   for (unsigned i = 0; i < tex->def.num_components; i++) {
      if (mask & BITFIELD_BIT(i))
         channel[i] = util_bitcount(mask & BITFIELD_MASK(i));
   }

   bi_make_vec_to(b, bi_def_index(&tex->def), dest4, channel,
                  tex->def.num_components, tex->def.bit_size);
}

static void
bi_emit_phi(bi_builder *b, nir_phi_instr *instr)
{
   unsigned nr_srcs = exec_list_length(&instr->srcs);
   bi_instr *I = bi_phi_to(b, bi_def_index(&instr->def), nr_srcs);

   /* Deferred */
   I->phi = instr;
}

/* Look up the AGX block corresponding to a given NIR block. Used when
 * translating phi nodes after emitting all blocks.
 */
static bi_block *
bi_from_nir_block(bi_context *ctx, nir_block *block)
{
   return ctx->indexed_nir_blocks[block->index];
}

static void
bi_emit_phi_deferred(bi_context *ctx, bi_block *block, bi_instr *I)
{
   nir_phi_instr *phi = I->phi;

   /* Guaranteed by lower_phis_to_scalar */
   assert(phi->def.bit_size * phi->def.num_components <= 32);

   nir_foreach_phi_src(src, phi) {
      bi_block *pred = bi_from_nir_block(ctx, src->pred);
      unsigned i = bi_predecessor_index(block, pred);
      assert(i < I->nr_srcs);

      I->src[i] = bi_src_index(&src->src);
   }

   I->phi = NULL;
}

static void
bi_emit_phis_deferred(bi_context *ctx)
{
   bi_foreach_block(ctx, block) {
      bi_foreach_instr_in_block(block, I) {
         if (I->op == BI_OPCODE_PHI)
            bi_emit_phi_deferred(ctx, block, I);
      }
   }
}

static void
bi_emit_instr(bi_builder *b, struct nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_load_const:
      bi_emit_load_const(b, nir_instr_as_load_const(instr));
      break;

   case nir_instr_type_intrinsic:
      bi_emit_intrinsic(b, nir_instr_as_intrinsic(instr));
      break;

   case nir_instr_type_alu:
      bi_emit_alu(b, nir_instr_as_alu(instr));
      break;

   case nir_instr_type_tex:
      assert(b->shader->arch >= 9);
      bi_emit_tex_valhall(b, nir_instr_as_tex(instr));
      break;

   case nir_instr_type_jump:
      bi_emit_jump(b, nir_instr_as_jump(instr));
      break;

   case nir_instr_type_phi:
      bi_emit_phi(b, nir_instr_as_phi(instr));
      break;

   default:
      UNREACHABLE("should've been lowered");
   }
}

static bi_block *
create_empty_block(bi_context *ctx)
{
   bi_block *blk = rzalloc(ctx, bi_block);

   util_dynarray_init(&blk->predecessors, blk);

   _mesa_pointer_set_init(&blk->dom_frontier, ctx);

   return blk;
}

static bi_block *
emit_block(bi_context *ctx, nir_block *block)
{
   if (ctx->after_block) {
      ctx->current_block = ctx->after_block;
      ctx->after_block = NULL;
   } else {
      ctx->current_block = create_empty_block(ctx);
   }

   list_addtail(&ctx->current_block->link, &ctx->blocks);
   list_inithead(&ctx->current_block->instructions);

   bi_builder _b = bi_init_builder(ctx, bi_after_block(ctx->current_block));

   ctx->indexed_nir_blocks[block->index] = ctx->current_block;

   nir_foreach_instr(instr, block) {
      _b.debug_info = instr->has_debug_info ? nir_instr_get_debug_info(instr) : NULL;
      bi_emit_instr(&_b, instr);
   }

   return ctx->current_block;
}

static void
emit_if(bi_context *ctx, nir_if *nif)
{
   bi_block *before_block = ctx->current_block;

   /* Speculatively emit the branch, but we can't fill it in until later */
   bi_builder _b = bi_init_builder(ctx, bi_after_block(ctx->current_block));
   bi_instr *then_branch =
      bi_branchz_i16(&_b, bi_half(bi_src_index(&nif->condition), false),
                     bi_zero(), BI_CMPF_EQ);

   /* Emit the two subblocks. */
   bi_block *then_block = emit_cf_list(ctx, &nif->then_list);
   bi_block *end_then_block = ctx->current_block;

   /* Emit second block */

   bi_block *else_block = emit_cf_list(ctx, &nif->else_list);
   bi_block *end_else_block = ctx->current_block;
   ctx->after_block = create_empty_block(ctx);

   /* Now that we have the subblocks emitted, fix up the branches */

   assert(then_block);
   assert(else_block);

   then_branch->branch_target = else_block;

   /* Emit a jump from the end of the then block to the end of the else */
   _b.cursor = bi_after_block(end_then_block);
   bi_instr *then_exit = bi_jump(&_b, bi_zero());
   then_exit->branch_target = ctx->after_block;

   bi_block_add_successor(end_then_block, then_exit->branch_target);
   bi_block_add_successor(end_else_block, ctx->after_block); /* fallthrough */

   bi_block_add_successor(before_block,
                          then_branch->branch_target); /* then_branch */
   bi_block_add_successor(before_block, then_block);   /* fallthrough */
}

static void
emit_loop(bi_context *ctx, nir_loop *nloop)
{
   assert(!nir_loop_has_continue_construct(nloop));

   /* Remember where we are */
   bi_block *start_block = ctx->current_block;

   bi_block *saved_break = ctx->break_block;
   bi_block *saved_continue = ctx->continue_block;

   ctx->continue_block = create_empty_block(ctx);
   ctx->break_block = create_empty_block(ctx);
   ctx->after_block = ctx->continue_block;
   ctx->after_block->loop_header = true;

   /* Emit the body itself */
   emit_cf_list(ctx, &nloop->body);

   /* Branch back to loop back */
   bi_builder _b = bi_init_builder(ctx, bi_after_block(ctx->current_block));
   bi_instr *I = bi_jump(&_b, bi_zero());
   I->branch_target = ctx->continue_block;
   bi_block_add_successor(start_block, ctx->continue_block);
   bi_block_add_successor(ctx->current_block, ctx->continue_block);

   ctx->after_block = ctx->break_block;

   /* Pop off */
   ctx->break_block = saved_break;
   ctx->continue_block = saved_continue;
   ++ctx->loop_count;
}

static bi_block *
emit_cf_list(bi_context *ctx, struct exec_list *list)
{
   bi_block *start_block = NULL;

   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block: {
         bi_block *block = emit_block(ctx, nir_cf_node_as_block(node));

         if (!start_block)
            start_block = block;

         break;
      }

      case nir_cf_node_if:
         emit_if(ctx, nir_cf_node_as_if(node));
         break;

      case nir_cf_node_loop:
         emit_loop(ctx, nir_cf_node_as_loop(node));
         break;

      default:
         UNREACHABLE("Unknown control flow");
      }
   }

   return start_block;
}

/* shader-db stuff */

struct bi_stats {
   unsigned nr_clauses, nr_tuples, nr_ins;
   unsigned nr_arith, nr_texture, nr_varying, nr_ldst;
   unsigned nr_fau_uniforms;
   uint64_t reg_mask;
};

static uint64_t
bi_tuple_regs_used(const bi_registers *regs)
{
   uint64_t mask = 0;

   /* note: the stats gathering runs before final packing,
    * so no need to handle the 63-x trick and similar issues
    */
   if (regs->enabled[0]) {
      mask |= BITFIELD64_BIT(regs->slot[0]);
   }
   if (regs->enabled[1]) {
      mask |= BITFIELD64_BIT(regs->slot[1]);
   }
   if (regs->slot23.slot2) {
      mask |= BITFIELD64_BIT(regs->slot[2]);
   }
   if (regs->slot23.slot3) {
      mask |= BITFIELD64_BIT(regs->slot[3]);
   }
   return mask;
}

static void
bi_count_tuple_stats(bi_clause *clause, bi_tuple *tuple, struct bi_stats *stats)
{
   /* check for FAU access */
   if (tuple->fau_idx & 0x80) {
      unsigned ureg = 1 + (tuple->fau_idx & 0x7f);
      stats->nr_fau_uniforms = MAX2(stats->nr_fau_uniforms, 2*ureg);
   }
   stats->reg_mask |= bi_tuple_regs_used(&tuple->regs);

   /* Count instructions */
   stats->nr_ins += (tuple->fma ? 1 : 0) + (tuple->add ? 1 : 0);

   /* Non-message passing tuples are always arithmetic */
   if (tuple->add != clause->message) {
      stats->nr_arith++;
      return;
   }

   /* Message + FMA we'll count as arithmetic _and_ message */
   if (tuple->fma)
      stats->nr_arith++;

   switch (clause->message_type) {
   case BIFROST_MESSAGE_VARYING:
      /* Check components interpolated */
      stats->nr_varying +=
         (clause->message->vecsize + 1) *
         (bi_is_regfmt_16(clause->message->register_format) ? 1 : 2);
      break;

   case BIFROST_MESSAGE_VARTEX:
      /* 2 coordinates, fp32 each */
      stats->nr_varying += (2 * 2);
      FALLTHROUGH;
   case BIFROST_MESSAGE_TEX:
      stats->nr_texture++;
      break;

   case BIFROST_MESSAGE_ATTRIBUTE:
   case BIFROST_MESSAGE_LOAD:
   case BIFROST_MESSAGE_STORE:
   case BIFROST_MESSAGE_ATOMIC:
      stats->nr_ldst++;
      break;

   case BIFROST_MESSAGE_NONE:
   case BIFROST_MESSAGE_BARRIER:
   case BIFROST_MESSAGE_BLEND:
   case BIFROST_MESSAGE_TILE:
   case BIFROST_MESSAGE_Z_STENCIL:
   case BIFROST_MESSAGE_ATEST:
   case BIFROST_MESSAGE_JOB:
   case BIFROST_MESSAGE_64BIT:
      /* Nothing to do */
      break;
   };
}

/*
 * v7 allows preloading LD_VAR or VAR_TEX messages that must complete before the
 * shader completes. These costs are not accounted for in the general cycle
 * counts, so this function calculates the effective cost of these messages, as
 * if they were executed by shader code.
 */
static unsigned
bi_count_preload_cost(bi_context *ctx)
{
   /* Units: 1/16 of a normalized cycle, assuming that we may interpolate
    * 16 fp16 varying components per cycle or fetch two texels per cycle.
    */
   unsigned cost = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(ctx->info.bifrost->messages); ++i) {
      struct bifrost_message_preload msg = ctx->info.bifrost->messages[i];

      if (msg.enabled && msg.texture) {
         /* 2 coordinate, 2 half-words each, plus texture */
         cost += 12;
      } else if (msg.enabled) {
         cost += (msg.num_components * (msg.fp16 ? 1 : 2));
      }
   }

   return cost;
}

static const char *
bi_shader_stage_name(bi_context *ctx)
{
   if (ctx->idvs == BI_IDVS_VARYING)
      return "MESA_SHADER_VARYING";
   else if (ctx->idvs == BI_IDVS_POSITION)
      return "MESA_SHADER_POSITION";
   else if (ctx->inputs->is_blend)
      return "MESA_SHADER_BLEND";
   else
      return mesa_shader_stage_name(ctx->stage);
}

static void
bi_gather_stats(bi_context *ctx, unsigned size, struct bifrost_stats *out)
{
   struct bi_stats counts = {0};

   /* Count instructions, clauses, and tuples. Also attempt to construct
    * normalized execution engine cycle counts, using the following ratio:
    *
    * 24 arith tuples/cycle
    * 2 texture messages/cycle
    * 16 x 16-bit varying channels interpolated/cycle
    * 1 load store message/cycle
    *
    * These numbers seem to match Arm Mobile Studio's heuristic. The real
    * cycle counts are surely more complicated.
    */

   bi_foreach_block(ctx, block) {
      bi_foreach_clause_in_block(block, clause) {
         counts.nr_clauses++;
         counts.nr_tuples += clause->tuple_count;

         for (unsigned i = 0; i < clause->tuple_count; ++i)
            bi_count_tuple_stats(clause, &clause->tuples[i], &counts);
      }
   }

   /* Thread count and register pressure are traded off only on v7 */
   bool full_threads = (ctx->arch == 7 && ctx->info.work_reg_count <= 32);

   *out = (struct bifrost_stats){
      .instrs = counts.nr_ins,
      .tuples = counts.nr_tuples,
      .clauses = counts.nr_clauses,
      .arith = ((float)counts.nr_arith) / 24.0,
      .t = ((float)counts.nr_texture) / 2.0,
      .v = ((float)counts.nr_varying) / 16.0,
      .ldst = ((float)counts.nr_ldst) / 1.0,
      .code_size = size,
      .preloads = ctx->arch == 7 ? bi_count_preload_cost(ctx) : 0,
      .threads = full_threads ? 2 : 1,
      .loops = ctx->loop_count,
      .spills = ctx->spills,
      .fills = ctx->fills,
      .spill_cost = ctx->spill_cost,
      .registers_used = util_bitcount64(counts.reg_mask),
      .uniforms_used = counts.nr_fau_uniforms,
   };

   out->cycles = MAX2(out->arith, MAX3(out->t, out->v, out->ldst));
}

static void
va_count_stats(bi_context *ctx, unsigned nr_ins, unsigned size,
               const struct va_stats *counts,
               struct valhall_stats *out)
{
   const struct pan_model *model =
      pan_get_model(ctx->inputs->gpu_id, ctx->inputs->gpu_variant);

   if (model == NULL) {
      /* Get G57 by default: */
      model = pan_get_model(((uint64_t)0x9001) << 16, 0);
      assert(model);
   }

   struct valhall_stats stats_abs = {
      .instrs = nr_ins,
      .code_size = size,
      .fma = ((float)counts->fma),
      .cvt = ((float)counts->cvt),
      .sfu = ((float)counts->sfu),
      .v = ((float)counts->v),
      .t = ((float)counts->t),
      .ls = ((float)counts->ls),
      .threads = (ctx->info.work_reg_count <= 32) ? 2 : 1,
      .loops = ctx->loop_count,
      .spills = ctx->spills,
      .fills = ctx->fills,
      .spill_cost = ctx->spill_cost,
      .registers_used = util_bitcount64(counts->reg_mask),
      .uniforms_used = counts->nr_fau_uniforms,
   };
   struct valhall_stats stats = stats_abs;
   stats.fma /= model->rates.fma;
   stats.cvt /= model->rates.fma;
   stats.sfu /= (model->rates.fma / 4.0);
   stats.v /= 16.0;
   stats.t /= model->rates.texel;
   stats.ls /= 1.0;

   const bool output_normalized = !(bifrost_debug & BIFROST_DBG_STATSABS);
   *out = output_normalized ? stats : stats_abs;

   /* Calculate the bound */
   out->cycles = MAX2(MAX3(stats.fma, stats.cvt, stats.sfu),
                      MAX3(stats.v, stats.t, stats.ls));
}

static unsigned
va_gather_stats_block(bi_block *block, struct va_stats *counts)
{
   unsigned nr_ins = 0;

   bi_foreach_instr_in_block(block, I) {
      nr_ins++;
      va_count_instr_stats(I, counts);
   }
   return nr_ins;
}

/*
 * Gather stats for a minimum length path through the shader.
 */
static unsigned
va_gather_min_path_stats(bi_block *block, struct va_stats *counts)
{
   struct va_stats min_counts;
   struct va_stats save_counts = *counts;
   unsigned min_ins = 0;
   unsigned nr_ins;

   bi_foreach_successor(block, next) {
      // if following a path leads to a loop, do not do it
      if (bi_block_dominates(next, block)) {
         continue;
      }
      nr_ins = va_gather_min_path_stats(next, counts);
      if (min_ins == 0 || nr_ins < min_ins) {
         min_ins = nr_ins;
         min_counts = *counts;
      }
      *counts = save_counts;
   }
   if (min_ins != 0) {
      *counts = min_counts;
   }
   nr_ins = min_ins + va_gather_stats_block(block, counts);
   return nr_ins;
}

/*
 * Gather stats for a maximum length path through the shader.
 * This is slightly tricky because we do want to count loops,
 * but at most once. If we see we've visited a block already,
 * bail out.
 */
static unsigned
va_gather_max_path_stats(bi_block *block, struct va_stats *counts, BITSET_WORD *visited)
{
   struct va_stats max_counts;
   struct va_stats save_counts = *counts;
   unsigned max_ins = 0;
   unsigned nr_ins;

   BITSET_SET(visited, block->index);
   bi_foreach_successor(block, next) {
      // if we've already visited this block, skip it
      if (BITSET_TEST(visited, next->index)) {
         continue;
      }
      nr_ins = va_gather_max_path_stats(next, counts, visited);
      if (nr_ins > max_ins) {
         max_ins = nr_ins;
         max_counts = *counts;
      }
      *counts = save_counts;
   }
   if (max_ins != 0) {
      *counts = max_counts;
   }
   nr_ins = max_ins + va_gather_stats_block(block, counts);
   return nr_ins;
}

enum gather_stats_mode {
   GATHER_STATS_FULL = 0,
   GATHER_STATS_MIN,
   GATHER_STATS_MAX
};

static void
va_gather_stats(bi_context *ctx, unsigned size, struct valhall_stats *out,
                enum gather_stats_mode mode)
{
   unsigned nr_ins = 0;
   struct va_stats counts = {0};
   bi_block *first_block = bi_start_block(&ctx->blocks);
   BITSET_WORD *visited;

   /* Count instructions */
   switch (mode) {
   case GATHER_STATS_FULL:
      bi_foreach_instr_global(ctx, I) {
         nr_ins++;
         va_count_instr_stats(I, &counts);
      }
      break;
   case GATHER_STATS_MIN:
      nr_ins = va_gather_min_path_stats(first_block, &counts);
      break;
   case GATHER_STATS_MAX:
      visited = BITSET_RZALLOC(NULL, ctx->num_blocks);
      nr_ins = va_gather_max_path_stats(first_block, &counts, visited);
      ralloc_free(visited);
      break;
   }
   /* convert to stats */
   va_count_stats(ctx, nr_ins, size, &counts, out);
}

static void
bi_opt_post_ra(bi_context *ctx)
{
   bi_foreach_instr_global_safe(ctx, ins) {
      if (ins->op == BI_OPCODE_MOV_I32 &&
          bi_is_equiv(ins->dest[0], ins->src[0]))
         bi_remove_instruction(ins);
   }
}

/* Dead code elimination for branches at the end of a block - only one branch
 * per block is legal semantically, but unreachable jumps can be generated.
 * Likewise on Bifrost we can generate jumps to the terminal block which need
 * to be lowered away to a jump to #0x0, which induces successful termination.
 * That trick doesn't work on Valhall, which needs a NOP inserted in the
 * terminal block instead.
 */
static void
bi_lower_branch(bi_context *ctx, bi_block *block)
{
   bool cull_terminal = (ctx->arch <= 8);
   bool branched = false;

   bi_foreach_instr_in_block_safe(block, ins) {
      if (!ins->branch_target)
         continue;

      if (branched) {
         bi_remove_instruction(ins);
         continue;
      }

      branched = true;

      if (!bi_is_terminal_block(ins->branch_target))
         continue;

      if (cull_terminal)
         ins->branch_target = NULL;
      else if (ins->branch_target)
         ins->branch_target->needs_nop = true;
   }
}

static void
bi_pack_clauses(bi_context *ctx, struct util_dynarray *binary, unsigned offset)
{
   unsigned final_clause = bi_pack(ctx, binary);

   /* If we need to wait for ATEST or BLEND in the first clause, pass the
    * corresponding bits through to the renderer state descriptor */
   bi_block *first_block = list_first_entry(&ctx->blocks, bi_block, link);
   bi_clause *first_clause = bi_next_clause(ctx, first_block, NULL);

   unsigned first_deps = first_clause ? first_clause->dependencies : 0;
   ctx->info.bifrost->wait_6 = (first_deps & (1 << 6));
   ctx->info.bifrost->wait_7 = (first_deps & (1 << 7));

   /* Pad the shader with enough zero bytes to trick the prefetcher,
    * unless we're compiling an empty shader (in which case we don't pad
    * so the size remains 0) */
   unsigned prefetch_size = BIFROST_SHADER_PREFETCH - final_clause;

   if (binary->size - offset) {
      memset(util_dynarray_grow(binary, uint8_t, prefetch_size), 0,
             prefetch_size);
   }
}

static int
compare_u32(const void* a, const void* b, void* _)
{
   const uint32_t va = (uintptr_t)a;
   const uint32_t vb = (uintptr_t)b;
   return va - vb;
}

static bi_context *
bi_compile_variant_nir(nir_shader *nir,
                       const struct pan_compile_inputs *inputs,
                       struct util_dynarray *binary, struct pan_shader_info *pinfo,
                       struct pan_stats *stats, enum bi_idvs_mode idvs)
{
   bi_context *ctx = rzalloc(NULL, bi_context);
   struct bi_shader_info info = {
      .push = &pinfo->push,
      .bifrost = &pinfo->bifrost,
      .tls_size = pinfo->tls_size,
      .push_offset = pinfo->push.count,
      .init_fau_consts_count = pinfo->fau_consts_count,
   };

   /* There may be another program in the dynarray, start at the end */
   unsigned offset = binary->size;

   ctx->inputs = inputs;
   ctx->nir = nir;
   ctx->stage = nir->info.stage;
   ctx->quirks = bifrost_get_quirks(inputs->gpu_id);
   ctx->arch = pan_arch(inputs->gpu_id);
   ctx->info = info;
   ctx->idvs = idvs;
   ctx->malloc_idvs = (ctx->arch >= 9) && !inputs->no_idvs;
   ctx->fau_consts_count = info.init_fau_consts_count;

   unsigned execution_mode = nir->info.float_controls_execution_mode;
   ctx->rtz_fp16 = nir_is_rounding_mode_rtz(execution_mode, 16);
   ctx->rtz_fp32 = nir_is_rounding_mode_rtz(execution_mode, 32);
   ctx->ftz_fp32 = nir_is_denorm_flush_to_zero(execution_mode, 32);

   if (idvs == BI_IDVS_POSITION || idvs == BI_IDVS_VARYING) {
      /* Specializing shaders for IDVS is destructive, so we need to
       * clone. However, the last (second) IDVS shader does not need
       * to be preserved so we can skip cloning that one.
       */
      if (offset == 0)
         ctx->nir = nir = nir_shader_clone(ctx, nir);

      uint64_t position = VA_SHADER_OUTPUT_POSITION_BIT |
                          VA_SHADER_OUTPUT_ATTRIB_BIT;

      NIR_PASS(_, nir, nir_inline_sysval, nir_intrinsic_load_shader_output_pan,
               (idvs == BI_IDVS_POSITION) ? position : VA_SHADER_OUTPUT_VARY_BIT);

      /* After specializing, clean up the mess */
      bool progress = true;

      while (progress) {
         progress = false;

         NIR_PASS(progress, nir, nir_opt_constant_folding);
         NIR_PASS(progress, nir, nir_opt_dce);
         NIR_PASS(progress, nir, nir_opt_dead_cf);
         NIR_PASS(progress, nir, nir_opt_cse);
      }
   }

   /* If nothing is pushed, all UBOs need to be uploaded */
   ctx->ubo_mask = ~0;

   list_inithead(&ctx->blocks);

   bool skip_internal = nir->info.internal;
   skip_internal &= !(bifrost_debug & BIFROST_DBG_INTERNAL);

   if (bifrost_debug & BIFROST_DBG_SHADERS && !skip_internal) {
      /* Assign lines. Must be done last. */
      if (bifrost_debug & BIFROST_DBG_DEBUGINFO)
         ralloc_free(nir_shader_gather_debug_info(nir, "", 1));

      nir_print_shader(nir, stderr);
   }

   ctx->allocated_vec = _mesa_hash_table_u64_create(ctx);

   nir_foreach_function_impl(impl, nir) {
      nir_index_blocks(impl);
      nir_index_ssa_defs(impl);

      ctx->indexed_nir_blocks =
         rzalloc_array(ctx, bi_block *, impl->num_blocks);

      ctx->ssa_alloc += impl->ssa_alloc;

      emit_cf_list(ctx, &impl->body);
      bi_emit_phis_deferred(ctx);
      break; /* TODO: Multi-function shaders */
   }

   /* Index blocks now that we're done emitting */
   bi_foreach_block(ctx, block) {
      block->index = ctx->num_blocks++;
   }

   bi_validate(ctx, "NIR -> BIR");

   bool optimize = !(bifrost_debug & BIFROST_DBG_NOOPT);

   /* Runs before constant folding */
   bi_lower_swizzle(ctx);
   bi_validate(ctx, "Early lowering");

   /* Runs before copy prop */
   if (optimize && ctx->inputs->pushable_ubos) {
      bi_opt_push_ubo(ctx);
   }

   if (likely(optimize)) {
      bi_opt_copy_prop(ctx);

      while (bi_opt_constant_fold(ctx))
         bi_opt_copy_prop(ctx);

      bi_opt_mod_prop_forward(ctx);
      bi_opt_mod_prop_backward(ctx);

      /* Push LD_VAR_IMM/VAR_TEX instructions. Must run after
       * mod_prop_backward to fuse VAR_TEX */
      if (ctx->arch == 7 && ctx->stage == MESA_SHADER_FRAGMENT &&
          !(bifrost_debug & BIFROST_DBG_NOPRELOAD)) {
         bi_opt_dce(ctx, false);
         bi_opt_message_preload(ctx);
         bi_opt_copy_prop(ctx);
      }

      bi_opt_dce(ctx, false);
      bi_opt_cse(ctx);
      bi_opt_dce(ctx, false);
      if (ctx->inputs->pushable_ubos)
         bi_opt_reorder_push(ctx);
      bi_validate(ctx, "Optimization passes");
   }

   bi_lower_opt_instructions(ctx);
   bi_lower_mkvec_swz(ctx);

   if (ctx->arch >= 9) {
      va_lower_isel(ctx);
      va_optimize(ctx);

      /* Count how often a specific constant appears. */
      struct hash_table_u64 *const_hist = _mesa_hash_table_u64_create(ctx);
      bi_foreach_instr_global_safe(ctx, I) {
         /* Phis become single moves so shouldn't be affected */
         if (I->op == BI_OPCODE_PHI)
            continue;

         va_count_constants(ctx, I, const_hist);
      }

      uint32_t const_amount = _mesa_hash_table_u64_num_entries(const_hist);
      uint32_t *sorted = rzalloc_array(ctx, uint32_t, const_amount);

      uint32_t idx = 0;
      hash_table_u64_foreach(const_hist, entry)
      {
         sorted[idx++] = (uintptr_t)entry.data;
      }

      util_qsort_r(sorted, const_amount, sizeof(uint32_t), compare_u32, NULL);
      uint32_t max_amount = MIN2(const_amount, ctx->inputs->fau_consts.max_amount);
      uint32_t min_count_for_fau = max_amount > 0 ? sorted[max_amount - 1] : 0;
      ralloc_free(sorted);

      bi_foreach_instr_global_safe(ctx, I) {
         /* Phis become single moves so shouldn't be affected */
         if (I->op == BI_OPCODE_PHI)
            continue;

         va_lower_constants(ctx, I, const_hist, min_count_for_fau);

         bi_builder b = bi_init_builder(ctx, bi_before_instr(I));
         va_repair_fau(&b, I);
      }

      _mesa_hash_table_u64_destroy(const_hist);

      /* We need to clean up after constant lowering */
      if (likely(optimize)) {
         bi_opt_cse(ctx);
         bi_opt_dce(ctx, false);
      }

      bi_validate(ctx, "Valhall passes");
   }

   bi_foreach_block(ctx, block) {
      bi_lower_branch(ctx, block);
   }

   if (bifrost_debug & BIFROST_DBG_SHADERS && !skip_internal)
      bi_print_shader(ctx, stderr);

   /* Analyze before register allocation to avoid false dependencies. The
    * skip bit is a function of only the data flow graph and is invariant
    * under valid scheduling. Helpers are only defined for fragment
    * shaders, so this analysis is only required in fragment shaders.
    */
   if (ctx->stage == MESA_SHADER_FRAGMENT) {
      bi_opt_dce(ctx, false);
      bi_analyze_helper_requirements(ctx);
   }

   /* Fuse TEXC after analyzing helper requirements so the analysis
    * doesn't have to know about dual textures */
   if (likely(optimize)) {
      bi_opt_fuse_dual_texture(ctx);
   }

   /* Lower FAU after fusing dual texture, because fusing dual texture
    * creates new immediates that themselves may need lowering.
    */
   if (ctx->arch <= 8) {
      bi_lower_fau(ctx);
   }

   /* Lowering FAU can create redundant moves. Run CSE+DCE to clean up. */
   if (likely(optimize)) {
      bi_opt_cse(ctx);
      bi_opt_dce(ctx, false);
   }

   bi_validate(ctx, "Late lowering");

   bi_iterator_schedule(ctx);

   if (likely(!(bifrost_debug & BIFROST_DBG_NOPSCHED))) {
      bi_pressure_schedule(ctx);
      bi_validate(ctx, "Pre-RA scheduling");
   }

   bi_register_allocate(ctx);

   if (likely(optimize))
      bi_opt_post_ra(ctx);

   if (bifrost_debug & BIFROST_DBG_SHADERS && !skip_internal)
      bi_print_shader(ctx, stderr);

   bi_opt_control_flow(ctx);

   if (ctx->arch >= 9) {
      va_assign_slots(ctx);
      va_insert_flow_control_nops(ctx);
      va_merge_flow(ctx);
      va_mark_last(ctx);
   } else {
      bi_schedule(ctx);
      bi_assign_scoreboard(ctx);

      /* Analyze after scheduling since we depend on instruction
       * order. Valhall calls as part of va_insert_flow_control_nops,
       * as the handling for clauses differs from instructions.
       */
      bi_analyze_helper_terminate(ctx);
      bi_mark_clauses_td(ctx);
   }

   if (bifrost_debug & BIFROST_DBG_SHADERS && !skip_internal)
      bi_print_shader(ctx, stderr);

   if (ctx->arch <= 8) {
      bi_pack_clauses(ctx, binary, offset);
   } else {
      bi_pack_valhall(ctx, binary);
   }

   if (bifrost_debug & BIFROST_DBG_SHADERS && !skip_internal) {
      if (ctx->arch <= 8) {
         disassemble_bifrost(stderr, binary->data + offset,
                             binary->size - offset,
                             bifrost_debug & BIFROST_DBG_VERBOSE);
      } else {
         disassemble_valhall(stderr, binary->data + offset,
                             binary->size - offset,
                             bifrost_debug & BIFROST_DBG_VERBOSE);
      }

      fflush(stderr);
   }

   /* gather instruction statistics */
   if (ctx->arch >= 9) {
      stats->isa = PAN_STAT_VALHALL;
      va_gather_stats(ctx, binary->size - offset, &stats->valhall, GATHER_STATS_FULL);
   } else {
      stats->isa = PAN_STAT_BIFROST;
      bi_gather_stats(ctx, binary->size - offset, &stats->bifrost);
   }

   if (ctx->arch >= 13)
      va_gather_hsr_info(ctx, pinfo);

   /* update info struct */
   pan_shader_update_info(pinfo, ctx->nir, inputs);

   if ((bifrost_debug & (BIFROST_DBG_SHADERDB|BIFROST_DBG_STATSFULL))
       && !skip_internal) {
      const char *prefix = bi_shader_stage_name(ctx);
      if (bifrost_debug & BIFROST_DBG_STATSFULL) {
         pan_stats_verbose(stderr, prefix, ctx, stats, pinfo);
      } else {
         pan_stats_fprintf(stderr, prefix, stats);
      }
   }

   return ctx;
}

void
bi_compile_variant(nir_shader *nir,
                   const struct pan_compile_inputs *inputs,
                   struct util_dynarray *binary, struct pan_shader_info *info,
                   enum bi_idvs_mode idvs)
{
   unsigned offset = binary->size;

   /* If there is no position shader (gl_Position is not written), then
    * there is no need to build a varying shader either. This case is hit
    * for transform feedback only vertex shaders which only make sense with
    * rasterizer discard.
    */
   if ((offset == 0) && (idvs == BI_IDVS_VARYING))
      return;

   /* Software invariant: Only a secondary shader can appear at a nonzero
    * offset, to keep the ABI simple. */
   assert((offset == 0) ^ (idvs == BI_IDVS_VARYING));

   struct pan_stats *stats =
      idvs == BI_IDVS_VARYING ? &info->stats_idvs_varying : &info->stats;

   bi_context *ctx =
      bi_compile_variant_nir(nir, inputs, binary, info, stats, idvs);

   info->fau_consts_count = ctx->fau_consts_count;

   /* A register is preloaded <==> it is live before the first block */
   bi_block *first_block = list_first_entry(&ctx->blocks, bi_block, link);
   uint64_t preload = first_block->reg_live_in;

   /* If multisampling is used with a blend shader, the blend shader needs
    * to access the sample coverage mask in r60 and the sample ID in r61.
    * Blend shaders run in the same context as fragment shaders, so if a
    * blend shader could run, we need to preload these registers
    * conservatively. There is believed to be little cost to doing so, so
    * do so always to avoid variants of the preload descriptor.
    *
    * We only do this on Valhall, as Bifrost has to update the RSD for
    * multisampling w/ blend shader anyway, so this is handled in the
    * driver. We could unify the paths if the cost is acceptable.
    */
   if (nir->info.stage == MESA_SHADER_FRAGMENT && ctx->arch >= 9)
      preload |= BITFIELD64_BIT(60) | BITFIELD64_BIT(61);

   info->ubo_mask |= ctx->ubo_mask;
   info->tls_size = MAX2(info->tls_size, ctx->info.tls_size);
   info->has_shader_clk_instr = ctx->info.has_ld_gclk_instr;

   if (idvs == BI_IDVS_VARYING) {
      info->vs.secondary_enable = (binary->size > offset);
      info->vs.secondary_offset = offset;
      info->vs.secondary_preload = preload;
      info->vs.secondary_work_reg_count = ctx->info.work_reg_count;
   } else {
      info->preload = preload;
      info->work_reg_count = ctx->info.work_reg_count;

      if (idvs == BI_IDVS_ALL) {
         /* Varying shader is only enabled if we can have any kind of varying
          * written (that mean not position, or an extended FIFO attribute) */
         info->vs.secondary_enable =
            (nir->info.outputs_written &
             ~(VARYING_BIT_POS | VALHAL_EX_FIFO_VARYING_BITS)) != 0;
      }
   }

   if ((idvs == BI_IDVS_POSITION || idvs == BI_IDVS_ALL) &&
       !nir->info.internal &&
       nir->info.outputs_written & VARYING_BIT_PSIZ) {
      /* Find the psiz write */
      bi_instr *write = NULL;

      bi_foreach_instr_global(ctx, I) {
         if (I->is_psiz_write) {
            write = I;
            break;
         }
      }

      assert(write != NULL);

      /* NOP it out, preserving its flow control. TODO: maybe DCE */
      if (write->flow) {
         bi_builder b = bi_init_builder(ctx, bi_before_instr(write));
         bi_instr *nop = bi_nop(&b);
         nop->flow = write->flow;
      }

      bi_remove_instruction(write);

      info->vs.no_psiz_offset = binary->size;
      bi_pack_valhall(ctx, binary);
   }

   ralloc_free(ctx);
}

static void
find_all_predecessors(const bi_context *ctx, bi_block *b, BITSET_WORD *out)
{
   assert(out);
   assert(b);

   BITSET_CLEAR_COUNT(out, 0, ctx->num_blocks);

   /* If the CFG was one long chain, we would require |blocks|-1 iters to
    * propagate the in_loop info all the way through.
    */
   for (uint32_t iter = 0; iter < ctx->num_blocks - 1; ++iter) {
      bi_foreach_block(ctx, block) {
         bi_foreach_successor(block, succ) {
            if (succ == b || BITSET_TEST(out, succ->index)) {
               BITSET_SET(out, block->index);
               break;
            }
         }
      }
   }
}

void
bi_find_loop_blocks(const bi_context *ctx, bi_block *header, BITSET_WORD *out)
{
   find_all_predecessors(ctx, header, out);

   BITSET_WORD *dominators = BITSET_RZALLOC(NULL, ctx->num_blocks);
   bi_foreach_block(ctx, b) {
      if (bi_block_dominates(header, b)) {
         BITSET_SET(dominators, b->index);
      }
   }

   /* If the header dominates b and is also the successor of b, then b
    * is in the loop of that header.
    */
   __bitset_and(out, out, dominators, BITSET_WORDS(ctx->num_blocks));

   /* The header is not a predecessor of itself, so add it separately. */
   BITSET_SET(out, header->index);

   ralloc_free(dominators);
}

/*
 * verbose stat printing
 * enable with BIFROST_MESA_DEBUG=statsfull
 */
static unsigned
percent_used(unsigned cur, unsigned max)
{
   return (unsigned)(0.5 + 100 * cur / (double)max);
}

static void
report_regs(FILE *f, unsigned registers_used, unsigned uniforms_used)
{
   fprintf(f, "Work registers:    %u ", registers_used);
   if (registers_used <= 32) {
      fprintf(f, "(%u%% used at 100%% occupancy)\n",
              percent_used(registers_used, 32));
   } else {
      fprintf(f, "(%u%% used at 50%% occupancy)\n",
              percent_used(registers_used, 64));
   }
   fprintf(f, "Uniform registers: %u (%u%% used)\n",
           uniforms_used,
           percent_used(uniforms_used, 128));
}

/*
 * This function prints the pipe statistics in statval[], with `prefix` as
 * a leading message (e.g. prefix can indicate total stats, max path, etc.).
 * As a special case, if prefix is empty, only the column headings are
 * printed.
 */
static void
do_report_pipes(FILE *f, const char *prefix, unsigned n, const char *statname[], float statval[])
{
   unsigned limit_idx = 0;
   float limit_val = statval[0];

   fprintf(f, "%-25s", prefix);
   if (*prefix == 0) {
      /* just print column headings, no stats */
      for (unsigned i = 0; i < n; i++) {
         fprintf(f, " %6s", statname[i]);
      }
      fprintf(f, " %6s\n", "Bound");
      return;
   }
   for (unsigned i = 0; i < n; i++) {
      fprintf(f, " %6.3f", statval[i]);
      if (statval[i] > limit_val) {
         limit_idx = i;
         limit_val = statval[i];
      }
   }
   /* print the first thing that matches the bound */
   char bound_str[256];
   unsigned max_str = sizeof(bound_str) - 1; /* leave room for trailing 0 */
   strncpy(bound_str, statname[limit_idx], max_str);
   /* now print any others that match */
   for (unsigned i = limit_idx + 1; i < n; i++) {
      if (statval[i] == limit_val) {
         strncat(bound_str, ", ", max_str);
         strncat(bound_str, statname[i], max_str);
      }
   }
   fprintf(f, " %6s\n", bound_str);
}
static void
bifrost_stats_verbose(FILE *f, bi_context *ctx, const struct bifrost_stats *stats,
                      const struct pan_shader_info *info)
{
   report_regs(f, stats->registers_used, stats->uniforms_used);
   fprintf(f, "Code size:         %u bytes\n", stats->code_size);
   fprintf(f, "Loops:             %u\n", stats->loops);
   fprintf(f, "Spills/fills:      %u/%u\n", stats->spills, stats->fills);
   fprintf(f, "Stack size:        %u bytes\n", ctx->info.tls_size);

   /* now print instruction statistics */
   static const char *statname[] = {
      "A", "LS", "V", "T"
   };
   float statval[] = {
      stats->arith, stats->ldst, stats->v, stats->t,
   };
   unsigned n = ARRAY_SIZE(statname);
   assert(n == ARRAY_SIZE(statval));

   /* special case, empty prefix prints column headings */
   do_report_pipes(f, "", n, statname, statval);
   do_report_pipes(f, "Total instruction cycles:", n, statname, statval);
   fprintf(f, "\nA = Arithmetic, LS = Load/Store, V = Varying, T = Texture\n");
}

static void
valhall_stats_verbose(FILE *f, bi_context *ctx, const struct valhall_stats *stats,
                      const struct pan_shader_info *info)
{
   struct valhall_stats min_stats = { 0 };
   struct valhall_stats max_stats = { 0 };

   report_regs(f, stats->registers_used, stats->uniforms_used);
   fprintf(f, "Code size:         %u bytes\n", stats->code_size);
   fprintf(f, "Loops:             %u\n", stats->loops);
   fprintf(f, "Spills/fills:      %u/%u\n", stats->spills, stats->fills);
   fprintf(f, "Stack size:        %u bytes\n", ctx->info.tls_size);

   va_gather_stats(ctx, stats->code_size, &min_stats, GATHER_STATS_MIN);
   va_gather_stats(ctx, stats->code_size, &max_stats, GATHER_STATS_MAX);

   /* now print instruction statistics */
   float arith = MAX3(stats->fma, stats->sfu, stats->cvt);
   float min_arith = MAX3(min_stats.fma, min_stats.sfu, min_stats.cvt);
   float max_arith = MAX3(max_stats.fma, max_stats.sfu, max_stats.cvt);
   static const char *statname[] = {
      "A", "FMA", "CVT", "SFU", "LS", "V", "T"
   };
   float statval[] = {
      arith, stats->fma, stats->cvt, stats->sfu, stats->ls, stats->v, stats->t,
   };
   float min_statval[] = {
      min_arith, min_stats.fma, min_stats.cvt, min_stats.sfu,
      min_stats.ls, min_stats.v, min_stats.t,
   };
   float max_statval[] = {
      max_arith, max_stats.fma, max_stats.cvt, max_stats.sfu,
      max_stats.ls, max_stats.v, max_stats.t,
   };
   unsigned n = ARRAY_SIZE(statval);
   assert(n == ARRAY_SIZE(statname));
   do_report_pipes(f, "", n, statname, statval);
   do_report_pipes(f, "Total instruction cycles:", n, statname, statval);
   do_report_pipes(f, "Shortest path cycles:", n, statname, min_statval);
   do_report_pipes(f, "Longest path cycles:", n, statname, max_statval);
   fprintf(f, "\nA = Arithmetic, FMA = Arith FMA, CVT = Arith CVT, SFU = Arith SFU\n");
   fprintf(f, "LS = Load/Store, V = Varying, T = Texture\n");
}

static const char *bool_str(bool x) {
   return x ? "true" : "false";
}

static void
pan_stats_verbose(FILE *f, const char *prefix, bi_context *ctx, const struct pan_stats *stats,
                  const struct pan_shader_info *info)
{
   const struct pan_model *model = pan_get_model(ctx->inputs->gpu_id, ctx->inputs->gpu_variant);
   unsigned arch = (ctx->arch > 12) ? 0 : ctx->arch;
   const char *archname[] = {
      "Unknown",              /* 0 must always be "Unknown" */
      "Lima", "Lima", "Lima", /* 1-3 */
      "Utgard", "Midgard", "Bifrost", "Bifrost", /* 4-7 */
      "Valhall", "Valhall", "Valhall", "Valhall", /* 8-11 */
      "Arm 5th Gen", /* 12 */
   };

   fprintf(f, "\n");
   fprintf(f, "Model: %s\n", model->name);
   fprintf(f, "Shader type: %s\n", prefix);
      fprintf(f, "Architecture: %s\n", archname[arch]);
   switch (stats->isa) {
   case PAN_STAT_VALHALL:
      valhall_stats_verbose(f, ctx, &stats->valhall, info);
      break;
   case PAN_STAT_BIFROST:
      bifrost_stats_verbose(f, ctx, &stats->bifrost, info);
      break;
   default:
      pan_stats_fprintf(f, prefix, stats);
      break;
   }
   fprintf(f, "\n");
   fprintf(f, "Shader properties\n");
   fprintf(f, "=================\n");
   fprintf(f, "Contains barrier: %s\n", bool_str(info->contains_barrier));
   switch (info->stage) {
   case MESA_SHADER_FRAGMENT:
      fprintf(f, "Has side-effects: %s\n", bool_str(info->fs.sidefx));
      fprintf(f, "Modifies coverage: %s\n", bool_str(info->fs.writes_coverage));
      fprintf(f, "Reads color buffer: %s\n", bool_str(info->fs.outputs_read != 0));
      break;
   default:
      break;
   }
   fprintf(f, "\n");
}
