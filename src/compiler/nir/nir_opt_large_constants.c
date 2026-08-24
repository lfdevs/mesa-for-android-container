/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_deref.h"

#include "util/u_math.h"

static void
read_const_values(nir_const_value *dst, const void *src,
                  unsigned num_components, unsigned bit_size,
                  unsigned stride)
{
   memset(dst, 0, num_components * sizeof(*dst));

   for (unsigned i = 0; i < num_components; i++) {
      switch (bit_size) {
      case 1:
         /* Booleans are special-cased to be 32-bit */
         assert(util_ptr_is_aligned(src, 4));
         dst[i].b = *(uint32_t *)src != 0;
         break;

      case 8:
         dst[i].u8 = *(uint8_t *)src;
         break;

      case 16:
         assert(util_ptr_is_aligned(src, 2));
         dst[i].u16 = *(uint16_t *)src;
         break;

      case 32:
         assert(util_ptr_is_aligned(src, 4));
         dst[i].u32 = *(uint32_t *)src;
         break;

      case 64:
         assert(util_ptr_is_aligned(src, 8));
         dst[i].u64 = *(uint64_t *)src;
         break;

      default:
         UNREACHABLE("Invalid bit size");
      }

      src = (void *)((uintptr_t)src + stride);
   }
}

static void
write_const_value(void *dst, const nir_const_value src,
                  unsigned index, unsigned bit_size)
{
   switch (bit_size) {
   case 1:
      /* Booleans are special-cased to be 32-bit */
      assert(util_ptr_is_aligned(dst, 4));
      ((uint32_t *)dst)[index] = -(int)src.b;
      break;

   case 8:
      ((uint8_t *)dst)[index] = src.u8;
      break;

   case 16:
      assert(util_ptr_is_aligned(dst, 2));
      ((uint16_t *)dst)[index] = src.u16;
      break;

   case 32:
      assert(util_ptr_is_aligned(dst, 4));
      ((uint32_t *)dst)[index] = src.u32;
      break;

   case 64:
      assert(util_ptr_is_aligned(dst, 8));
      ((uint64_t *)dst)[index] = src.u64;
      break;

   default:
      UNREACHABLE("Invalid bit size");
   }
}

typedef enum small_constant_encoding {
   SMALL_CONST_INT,
   SMALL_CONST_FLOAT,
   SMALL_CONST_BCSEL,
} small_constant_encoding;

struct small_constant {
   uint64_t data;
   uint32_t bit_stride;
   uint32_t bit_size;

   small_constant_encoding encoding;

   union {
      /* int/float */
      struct {
         int64_t min;
         uint32_t denom;
      };
      /* bcsel */
      struct {
         uint64_t sel_true;
         uint64_t sel_false;
      };
   };
};

struct var_info {
   nir_variable *var;

   bool is_constant;
   bool is_small;
   bool found_read;
   bool duplicate;

   /* Block that has all the variable stores.  All the blocks with reads
    * should be dominated by this block.
    */
   nir_block *block;

   /* If is_constant, hold the collected constant data for this var. */
   uint32_t constant_data_size;
   void *constant_data;

   uint32_t num_components;
   struct small_constant small_constant[NIR_MAX_VEC_COMPONENTS];
};

static int
var_info_cmp(const void *_a, const void *_b)
{
   const struct var_info *a = _a;
   const struct var_info *b = _b;
   uint32_t a_size = a->constant_data_size;
   uint32_t b_size = b->constant_data_size;

   if (a->is_constant != b->is_constant) {
      return (int)a->is_constant - (int)b->is_constant;
   } else if (a_size < b_size) {
      return -1;
   } else if (a_size > b_size) {
      return 1;
   } else if (a_size == 0) {
      /* Don't call memcmp with invalid pointers. */
      return 0;
   } else {
      return memcmp(a->constant_data, b->constant_data, a_size);
   }
}

static nir_def *
build_constant_load(nir_builder *b, nir_deref_instr *deref,
                    glsl_type_size_align_func size_align)
{
   nir_variable *var = nir_deref_instr_get_variable(deref);

   const unsigned bit_size = glsl_get_bit_size(deref->type);
   const unsigned num_components = glsl_get_vector_elements(deref->type);

   UNUSED unsigned var_size, var_align;
   size_align(var->type, &var_size, &var_align);
   assert(var->data.location % var_align == 0);

   UNUSED unsigned deref_size, deref_align;
   size_align(deref->type, &deref_size, &deref_align);

   nir_def *src = nir_build_deref_offset(b, deref, size_align);
   nir_def *load =
      nir_load_constant(b, num_components, bit_size, src,
                        .base = var->data.location,
                        .range = var_size,
                        .align_mul = deref_align,
                        .align_offset = 0);

   if (load->bit_size < 8) {
      /* Booleans are special-cased to be 32-bit */
      assert(glsl_type_is_boolean(deref->type));
      assert(deref_size == num_components * 4);
      load->bit_size = 32;
      return nir_b2b1(b, load);
   } else {
      assert(deref_size == num_components * bit_size / 8);
      return load;
   }
}

static void
handle_constant_store(void *mem_ctx, struct var_info *info,
                      nir_deref_instr *deref, nir_def *val,
                      nir_component_mask_t write_mask,
                      glsl_type_size_align_func size_align)
{
   assert(!nir_deref_instr_has_indirect(deref));
   const unsigned bit_size = glsl_get_bit_size(deref->type);
   const unsigned num_components = glsl_get_vector_elements(deref->type);

   if (info->constant_data_size == 0) {
      unsigned var_size, var_align;
      size_align(info->var->type, &var_size, &var_align);
      info->constant_data_size = var_size;
      info->constant_data = rzalloc_size(mem_ctx, var_size);
   }

   const unsigned offset = nir_deref_instr_get_const_offset(deref, size_align);
   if (offset >= info->constant_data_size)
      return;

   write_mask &= nir_component_mask(num_components);

   u_foreach_bit(i, write_mask) {
      nir_const_value constant = nir_scalar_as_const_value(nir_scalar_resolved(val, i));
      write_const_value((char *)info->constant_data + offset,
                        constant, i, bit_size);
   }
}

#define NIR_SMALL_CONSTANT_MAX_ABS_VALUE 255

static bool
get_small_constant_bcsel(struct small_constant *info, uint32_t array_len,
                         uint32_t bit_size, nir_const_value *values)
{
   nir_const_value *other = NULL;
   uint64_t data = 0;
   for (unsigned i = 1; i < array_len; i++) {
      uint64_t val = nir_const_value_as_uint(values[i], bit_size);
      if (nir_const_value_as_uint(values[0], bit_size) == val)
         continue;
      if (other && nir_const_value_as_uint(*other, bit_size) != val)
         return false;
      other = &values[i];
      data |= BITFIELD64_BIT(i);
   }

   info->sel_false = nir_const_value_as_uint(values[0], bit_size);
   if (other)
      info->sel_true = nir_const_value_as_uint(*other, bit_size);
   else
      info->sel_true = info->sel_false;

   if (util_bitcount64(data) * 2 > array_len) {
      data = ~data & BITFIELD64_MASK(array_len);
      SWAP(info->sel_true, info->sel_false);
   }

   info->data = data;
   info->bit_size = array_len > 32 ? 64 : 32;
   info->encoding = SMALL_CONST_BCSEL;
   info->bit_stride = 1;

   return true;
}

static bool
get_small_constant_component(const nir_shader_compiler_options *options,
                             struct small_constant *info, uint32_t array_len,
                             uint32_t bit_size, nir_const_value *values)
{
   if (get_small_constant_bcsel(info, array_len, bit_size, values))
      return true;

   int64_t min = INT64_MAX;

   bool is_float = true;
   uint32_t denom = 1;
   if (bit_size < 16 || (bit_size == 16 && !options->support_16bit_alu)) {
      is_float = false;
   } else {
      for (unsigned i = 0; i < array_len; i++) {
         double float_value = nir_const_value_as_float(values[i], bit_size);
         if (fabs(float_value) > NIR_SMALL_CONSTANT_MAX_ABS_VALUE) {
            is_float = false;
            break;
         }

         /* Try out small denominators. Handling large denominators is not worth it
          * because the numerators will be large in that case, making it unlikely that
          * they will fit into 64 bits.
          * Limit to power of two for now, to avoid any rounding issues.
          */
         uint32_t value_denom = 0;
         for (uint32_t candidate_denom = 1; candidate_denom <= 16; candidate_denom *= 2) {
            double expanded = float_value * candidate_denom;
            if (floor(expanded) * (1.0f / (float)candidate_denom) == float_value) {
               value_denom = candidate_denom;
               break;
            }
         }

         if (!value_denom) {
            denom = 0;
            break;
         } else {
            denom = MAX2(denom, value_denom);
         }
      }

      if (denom) {
         for (unsigned i = 0; i < array_len; i++) {
            double fp_val = nir_const_value_as_float(values[i], bit_size) * denom;
            /* quantize to target precision  */
            fp_val = nir_const_value_as_float(nir_const_value_for_float(fp_val, bit_size), bit_size);

            int64_t int_value = (int64_t)fp_val;

            nir_const_value fc = nir_const_value_for_float(int_value * (1.0f / (float)denom), bit_size);
            is_float &= !memcmp(&fc, &values[i], bit_size / 8);

            min = MIN2(min, int_value);
         }
      } else {
         is_float = false;
      }
   }

   if (!is_float) {
      min = INT64_MAX;
      for (unsigned i = 0; i < array_len; i++) {
         int64_t integer = nir_const_value_as_int(values[i], bit_size);
         min = MIN2(min, integer);
      }
   }

   uint32_t used_bits = 0;
   for (unsigned i = 0; i < array_len; i++) {
      int64_t i64_elem;

      if (is_float)
         i64_elem = nir_const_value_as_float(values[i], bit_size) * denom;
      else
         i64_elem = nir_const_value_as_int(values[i], bit_size);

      i64_elem -= min;
      if (!i64_elem)
         continue;

      uint32_t elem_bits = util_logbase2_64(i64_elem) + 1;
      used_bits = MAX2(used_bits, elem_bits);
   }

   /* Only use power-of-two numbers of bits so we end up with a shift
    * instead of a multiply on our index.
    */
   used_bits = util_next_power_of_two(used_bits);

   if (used_bits * array_len > 64)
      return false;

   for (unsigned i = 0; i < array_len; i++) {
      int64_t i64_elem;

      if (is_float)
         i64_elem = nir_const_value_as_float(values[i], bit_size) * denom;
      else
         i64_elem = nir_const_value_as_int(values[i], bit_size);

      i64_elem -= min;
      if (!i64_elem)
         continue;

      info->data |= ((uint64_t)i64_elem) << (i * used_bits);
   }

   /* Limit bit_size >= 32 to avoid unnecessary conversions.  */
   info->bit_size = MAX2(util_next_power_of_two(used_bits * array_len), 32);
   info->min = min;
   info->encoding = is_float ? SMALL_CONST_FLOAT : SMALL_CONST_INT;
   info->denom = denom;
   info->bit_stride = used_bits;
   return true;
}

static void
get_small_constant(const nir_shader_compiler_options *options, struct var_info *info,
                   glsl_type_size_align_func size_align)
{
   if (!glsl_type_is_array(info->var->type))
      return;

   const struct glsl_type *elem_type = glsl_get_array_element(info->var->type);
   if (!glsl_type_is_scalar(elem_type) && !glsl_type_is_vector(elem_type))
      return;

   uint32_t array_len = glsl_get_length(info->var->type);
   info->num_components = glsl_get_vector_elements(elem_type);
   uint32_t bit_size = glsl_get_bit_size(elem_type);

   /* If our array is large, don't even bother */
   if (array_len > 64)
      return;

   /* Skip cases that can be lowered to a bcsel ladder more efficiently. */
   if (array_len <= 3)
      return;

   uint32_t elem_size, elem_align;
   size_align(elem_type, &elem_size, &elem_align);
   const uint32_t stride = ALIGN_POT(elem_size, elem_align);

   const uint32_t scalar_stride = bit_size == 1 ? 4 : bit_size / 8;

   info->is_small = true;
   for (unsigned c = 0; c < info->num_components; c++) {
      nir_const_value values[64];
      const void *data = info->constant_data;
      data = (void *)(((uintptr_t)data) + scalar_stride * c);
      read_const_values(values, data, array_len, bit_size, stride);

      if (!get_small_constant_component(options, &info->small_constant[c],
                                        array_len, bit_size, values)) {
         info->is_small = false;
         break;
      }
   }
}

static nir_def *
build_small_constant_load(nir_builder *b, nir_deref_instr *deref,
                          struct var_info *info, glsl_type_size_align_func size_align)
{
   assert(deref->deref_type == nir_deref_type_array);
   nir_def *index = nir_u2u32(b, deref->arr.index.ssa);

   nir_def *ret[NIR_MAX_VEC_COMPONENTS];

   const unsigned bit_size = glsl_get_bit_size(deref->type);

   for (unsigned c = 0; c < info->num_components; c++) {
      const struct small_constant *constant = &info->small_constant[c];

      if (constant->encoding == SMALL_CONST_BCSEL) {
         assert(constant->bit_stride == 1);

         if (util_is_power_of_two_nonzero64(constant->data)) {
            ret[c] = nir_ieq_imm(b, index, ffsll(constant->data) - 1);
         } else {
            nir_def *imm = nir_imm_intN_t(b, constant->data, constant->bit_size);
            ret[c] = nir_ushr(b, imm, index);
            ret[c] = nir_test_mask(b, ret[c], 0x1);
         }

         nir_def *sel_true = nir_imm_intN_t(b, constant->sel_true, bit_size);
         nir_def *sel_false = nir_imm_intN_t(b, constant->sel_false, bit_size);

         ret[c] = nir_bcsel(b, ret[c], sel_true, sel_false);
         continue;
      }

      nir_def *imm = nir_imm_intN_t(b, constant->data, constant->bit_size);

      nir_def *shift = nir_imul_imm(b, index, constant->bit_stride);

      ret[c] = nir_ushr(b, imm, shift);
      ret[c] = nir_iand_imm(b, ret[c], BITFIELD64_MASK(constant->bit_stride));

      assert(constant->bit_stride <= 32);
      if (ret[c]->bit_size == 64)
         ret[c] = nir_unpack_64_2x32_split_x(b, ret[c]);

      if (bit_size == 64 && constant->encoding == SMALL_CONST_INT)
         ret[c] = nir_u2u64(b, ret[c]);

      ret[c] = nir_iadd_imm(b, ret[c], constant->min);

      if (constant->encoding == SMALL_CONST_FLOAT) {
         if (constant->min >= 0)
            ret[c] = nir_u2fN(b, ret[c], bit_size);
         else
            ret[c] = nir_i2fN(b, ret[c], bit_size);

         if (constant->denom != 1)
            ret[c] = nir_fmul_imm(b, ret[c], 1.0f / (float)constant->denom);
      } else {
         ret[c] = nir_u2uN(b, ret[c], bit_size);
      }
   }

   if (info->num_components == 1)
      return ret[0];
   return nir_vec(b, ret, info->num_components);
}

/** Lower large constant variables to shader constant data
 *
 * This pass looks for large (type_size(var->type) > threshold) variables
 * which are statically constant and moves them into shader constant data.
 * This is especially useful when large tables are baked into the shader
 * source code because they can be moved into a UBO by the driver to reduce
 * register pressure and make indirect access cheaper.
 */
bool
nir_opt_large_constants(nir_shader *shader,
                        glsl_type_size_align_func size_align,
                        unsigned threshold)
{
   /* Default to a natural alignment if none is provided */
   if (size_align == NULL)
      size_align = glsl_get_natural_size_align_bytes;

   /* This only works with a single entrypoint */
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   unsigned num_locals = nir_function_impl_index_vars(impl);

   if (num_locals == 0) {
      nir_shader_preserve_all_metadata(shader);
      return false;
   }

   struct var_info *var_infos = ralloc_array(NULL, struct var_info, num_locals);
   nir_foreach_function_temp_variable(var, impl) {
      var_infos[var->index] = (struct var_info){
         .var = var,
         .is_constant = true,
         .found_read = false,
      };
   }

   nir_metadata_require(impl, nir_metadata_dominance);

   /* First, walk through the shader and figure out what variables we can
    * lower to the constant blob.
    */
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_deref) {
            /* If we ever see a complex use of a deref_var, we have to assume
             * that variable is non-constant because we can't guarantee we
             * will find all of the writers of that variable.
             */
            nir_deref_instr *deref = nir_instr_as_deref(instr);
            if (deref->deref_type == nir_deref_type_var &&
                deref->var->data.mode == nir_var_function_temp &&
                nir_deref_instr_has_complex_use(deref, 0))
               var_infos[deref->var->index].is_constant = false;
            continue;
         }

         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         bool src_is_const = false;
         nir_deref_instr *src_deref = NULL, *dst_deref = NULL;
         nir_component_mask_t write_mask = 0;
         switch (intrin->intrinsic) {
         case nir_intrinsic_store_deref:
            dst_deref = nir_src_as_deref(intrin->src[0]);
            write_mask = nir_intrinsic_write_mask(intrin);
            src_is_const = true;
            u_foreach_bit(i, write_mask)
               src_is_const &= nir_scalar_is_const(nir_scalar_resolved(intrin->src[1].ssa, i));
            break;

         case nir_intrinsic_load_deref:
            src_deref = nir_src_as_deref(intrin->src[0]);
            break;

         case nir_intrinsic_copy_deref:
            assert(!"Lowering of copy_deref with large constants is prohibited");
            break;

         default:
            continue;
         }

         if (dst_deref && nir_deref_mode_must_be(dst_deref, nir_var_function_temp)) {
            nir_variable *var = nir_deref_instr_get_variable(dst_deref);
            if (var == NULL)
               continue;

            assert(var->data.mode == nir_var_function_temp);

            struct var_info *info = &var_infos[var->index];
            if (!info->is_constant)
               continue;

            if (!info->block)
               info->block = block;

            /* We only consider variables constant if they only have constant
             * stores, all the stores come before any reads, and all stores
             * come from the same block.  We also can't handle indirect stores.
             */
            if (!src_is_const || info->found_read || block != info->block ||
                nir_deref_instr_has_indirect(dst_deref)) {
               info->is_constant = false;
            } else {
               handle_constant_store(var_infos, info, dst_deref, intrin->src[1].ssa,
                                     write_mask, size_align);
            }
         }

         if (src_deref && nir_deref_mode_must_be(src_deref, nir_var_function_temp)) {
            nir_variable *var = nir_deref_instr_get_variable(src_deref);
            if (var == NULL)
               continue;

            assert(var->data.mode == nir_var_function_temp);

            /* We only consider variables constant if all the reads are
             * dominated by the block that writes to it.
             */
            struct var_info *info = &var_infos[var->index];
            if (!info->is_constant)
               continue;

            if (!info->block || !nir_block_dominates(info->block, block))
               info->is_constant = false;

            info->found_read = true;
         }
      }
   }

   bool has_constant = false;

   /* Allocate constant data space for each variable that just has constant
    * data.  We sort them by size and content so we can easily find
    * duplicates.
    */
   const unsigned old_constant_data_size = shader->constant_data_size;
   qsort(var_infos, num_locals, sizeof(struct var_info), var_info_cmp);
   for (int i = 0; i < num_locals; i++) {
      struct var_info *info = &var_infos[i];

      /* Fix up indices after we sorted. */
      info->var->index = i;

      /* Don't bother with dead variables. */
      if (info->constant_data_size == 0)
         info->is_constant = false;

      if (!info->is_constant)
         continue;

      get_small_constant(shader->options, info, size_align);

      unsigned var_size, var_align;
      size_align(info->var->type, &var_size, &var_align);
      if ((var_size <= threshold && !info->is_small) || !info->found_read) {
         /* Don't bother lowering small stuff or data that's never read */
         info->is_constant = false;
         continue;
      }

      if (!info->is_small) {
         if (i > 0 && var_info_cmp(info, &var_infos[i - 1]) == 0) {
            info->var->data.location = var_infos[i - 1].var->data.location;
            info->duplicate = true;
         } else {
            info->var->data.location = ALIGN_POT(shader->constant_data_size, var_align);
            shader->constant_data_size = info->var->data.location + var_size;
         }
      }

      has_constant |= info->is_constant;
   }

   if (!has_constant) {
      nir_shader_preserve_all_metadata(shader);
      ralloc_free(var_infos);
      return false;
   }

   if (shader->constant_data_size != old_constant_data_size) {
      assert(shader->constant_data_size > old_constant_data_size);
      shader->constant_data = rerzalloc_size(shader, shader->constant_data,
                                             old_constant_data_size,
                                             shader->constant_data_size);
      for (int i = 0; i < num_locals; i++) {
         struct var_info *info = &var_infos[i];
         if (!info->duplicate && info->is_constant && !info->is_small) {
            memcpy((char *)shader->constant_data + info->var->data.location,
                   info->constant_data, info->constant_data_size);
         }
      }
   }

   nir_builder b = nir_builder_create(impl);

   /* We must preserve signed zero and disallow e.g. fma fusion
    * when we create new floating point instructions because it's not
    * even clear the result is only used as float.
    */
   b.fp_math_ctrl = nir_fp_exact | nir_fp_preserve_signed_zero;

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type == nir_instr_type_deref) {
            /* Ensure all derefs accessing the lowered arrays get removed. */
            nir_deref_instr *deref = nir_instr_as_deref(instr);
            if (!nir_deref_mode_is(deref, nir_var_function_temp))
               continue;

            nir_variable *var = nir_deref_instr_get_variable(deref);
            if (var && var_infos[var->index].is_constant)
               nir_deref_instr_remove_if_unused(deref);
         }

         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         switch (intrin->intrinsic) {
         case nir_intrinsic_load_deref: {
            nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
            if (!nir_deref_mode_is(deref, nir_var_function_temp))
               continue;

            nir_variable *var = nir_deref_instr_get_variable(deref);
            if (var == NULL)
               continue;

            struct var_info *info = &var_infos[var->index];
            if (info->is_small) {
               b.cursor = nir_after_instr(&intrin->instr);
               nir_def *val = build_small_constant_load(&b, deref, info, size_align);
               nir_def_replace(&intrin->def, val);
               nir_deref_instr_remove_if_unused(deref);
            } else if (info->is_constant) {
               b.cursor = nir_after_instr(&intrin->instr);
               nir_def *val = build_constant_load(&b, deref, size_align);
               nir_def_replace(&intrin->def, val);
               nir_deref_instr_remove_if_unused(deref);
            }
            break;
         }

         case nir_intrinsic_store_deref: {
            nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
            if (!nir_deref_mode_is(deref, nir_var_function_temp))
               continue;

            nir_variable *var = nir_deref_instr_get_variable(deref);
            if (var == NULL)
               continue;

            struct var_info *info = &var_infos[var->index];
            if (info->is_constant) {
               nir_instr_remove(&intrin->instr);
               nir_deref_instr_remove_if_unused(deref);
            }
            break;
         }
         case nir_intrinsic_copy_deref:
         default:
            continue;
         }
      }
   }

   /* Clean up the now unused variables */
   for (int i = 0; i < num_locals; i++) {
      struct var_info *info = &var_infos[i];
      if (info->is_constant)
         exec_node_remove(&info->var->node);
   }

   ralloc_free(var_infos);

   return nir_progress(true, impl, nir_metadata_control_flow);
}
