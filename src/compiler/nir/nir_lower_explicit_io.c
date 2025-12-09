/*
 * Copyright © 2014 Intel Corporation
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

static nir_intrinsic_op
ssbo_atomic_for_deref(nir_intrinsic_op deref_op)
{
   switch (deref_op) {
   case nir_intrinsic_deref_atomic:
      return nir_intrinsic_ssbo_atomic;
   case nir_intrinsic_deref_atomic_swap:
      return nir_intrinsic_ssbo_atomic_swap;
   default:
      UNREACHABLE("Invalid SSBO atomic");
   }
}

static nir_intrinsic_op
global_atomic_for_deref(nir_address_format addr_format,
                        nir_intrinsic_op deref_op)
{
   switch (deref_op) {
   case nir_intrinsic_deref_atomic:
      if (addr_format != nir_address_format_2x32bit_global)
         return nir_intrinsic_global_atomic;
      else
         return nir_intrinsic_global_atomic_2x32;

   case nir_intrinsic_deref_atomic_swap:
      if (addr_format != nir_address_format_2x32bit_global)
         return nir_intrinsic_global_atomic_swap;
      else
         return nir_intrinsic_global_atomic_swap_2x32;

   default:
      UNREACHABLE("Invalid SSBO atomic");
   }
}

static nir_intrinsic_op
shared_atomic_for_deref(nir_intrinsic_op deref_op)
{
   switch (deref_op) {
   case nir_intrinsic_deref_atomic:
      return nir_intrinsic_shared_atomic;
   case nir_intrinsic_deref_atomic_swap:
      return nir_intrinsic_shared_atomic_swap;
   default:
      UNREACHABLE("Invalid shared atomic");
   }
}

static nir_intrinsic_op
task_payload_atomic_for_deref(nir_intrinsic_op deref_op)
{
   switch (deref_op) {
   case nir_intrinsic_deref_atomic:
      return nir_intrinsic_task_payload_atomic;
   case nir_intrinsic_deref_atomic_swap:
      return nir_intrinsic_task_payload_atomic_swap;
   default:
      UNREACHABLE("Invalid task payload atomic");
   }
}

static unsigned
type_scalar_size_bytes(const struct glsl_type *type)
{
   assert(glsl_type_is_vector_or_scalar(type) ||
          glsl_type_is_matrix(type));
   return glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
}

static unsigned
addr_get_offset_bit_size(nir_def *addr, nir_address_format addr_format)
{
   if (addr_format == nir_address_format_32bit_offset_as_64bit ||
       addr_format == nir_address_format_32bit_index_offset_pack64)
      return 32;
   return addr->bit_size;
}

nir_def *
nir_build_addr_iadd_imm(nir_builder *b, nir_def *addr,
                        nir_address_format addr_format,
                        nir_variable_mode modes,
                        int64_t offset)
{
   if (!offset)
      return addr;

   return nir_build_addr_iadd(
      b, addr, addr_format, modes,
      nir_imm_intN_t(b, offset,
                     addr_get_offset_bit_size(addr, addr_format)));
}

static nir_def *
build_addr_ushr_imm(nir_builder *b, nir_def *addr,
                    nir_address_format addr_format, unsigned shift)
{
   switch (addr_format) {
   case nir_address_format_vec2_index_32bit_offset:
      assert(addr->num_components == 3);
      return nir_vector_insert_imm(
         b, addr, nir_ushr_imm(b, nir_channel(b, addr, 2), shift), 2);

   case nir_address_format_32bit_index_offset:
      assert(addr->num_components == 2);
      return nir_vector_insert_imm(
         b, addr, nir_ushr_imm(b, nir_channel(b, addr, 1), shift), 1);

   default:
      UNREACHABLE("Unsupported address format");
   }
}

static nir_def *
build_addr_for_var(nir_builder *b, nir_variable *var,
                   nir_address_format addr_format)
{
   assert(var->data.mode & (nir_var_uniform | nir_var_mem_shared |
                            nir_var_mem_task_payload |
                            nir_var_mem_global |
                            nir_var_shader_temp | nir_var_function_temp |
                            nir_var_mem_push_const | nir_var_mem_constant));

   const unsigned num_comps = nir_address_format_num_components(addr_format);
   const unsigned bit_size = nir_address_format_bit_size(addr_format);

   switch (addr_format) {
   case nir_address_format_2x32bit_global:
   case nir_address_format_32bit_global:
   case nir_address_format_64bit_global: {
      nir_def *base_addr;
      switch (var->data.mode) {
      case nir_var_shader_temp:
         base_addr = nir_load_scratch_base_ptr(b, num_comps, bit_size, 0);
         break;

      case nir_var_function_temp:
         base_addr = nir_load_scratch_base_ptr(b, num_comps, bit_size, 1);
         break;

      case nir_var_mem_constant:
         base_addr = nir_load_constant_base_ptr(b, num_comps, bit_size);
         break;

      case nir_var_mem_shared:
         base_addr = nir_load_shared_base_ptr(b, num_comps, bit_size);
         break;

      case nir_var_mem_global:
         base_addr = nir_load_global_base_ptr(b, num_comps, bit_size);
         break;

      default:
         UNREACHABLE("Unsupported variable mode");
      }

      return nir_build_addr_iadd_imm(b, base_addr, addr_format, var->data.mode,
                                     var->data.driver_location);
   }

   case nir_address_format_32bit_offset:
      assert(var->data.driver_location <= UINT32_MAX);
      return nir_imm_int(b, var->data.driver_location);

   case nir_address_format_32bit_offset_as_64bit:
      assert(var->data.driver_location <= UINT32_MAX);
      return nir_imm_int64(b, var->data.driver_location);

   case nir_address_format_62bit_generic:
      switch (var->data.mode) {
      case nir_var_shader_temp:
      case nir_var_function_temp:
         assert(var->data.driver_location <= UINT32_MAX);
         return nir_imm_intN_t(b, var->data.driver_location | 2ull << 62, 64);

      case nir_var_mem_shared:
         assert(var->data.driver_location <= UINT32_MAX);
         return nir_imm_intN_t(b, var->data.driver_location | 1ull << 62, 64);

      case nir_var_mem_global:
         return nir_iadd_imm(b, nir_load_global_base_ptr(b, num_comps, bit_size),
                             var->data.driver_location);

      default:
         UNREACHABLE("Unsupported variable mode");
      }

   default:
      UNREACHABLE("Unsupported address format");
   }
}

static nir_def *
build_runtime_addr_mode_check(nir_builder *b, nir_def *addr,
                              nir_address_format addr_format,
                              nir_variable_mode mode)
{
   /* The compile-time check failed; do a run-time check */
   switch (addr_format) {
   case nir_address_format_62bit_generic: {
      assert(addr->num_components == 1);
      assert(addr->bit_size == 64);
      nir_def *mode_enum = nir_ushr_imm(b, addr, 62);
      switch (mode) {
      case nir_var_function_temp:
      case nir_var_shader_temp:
         return nir_ieq_imm(b, mode_enum, 0x2);

      case nir_var_mem_shared:
         return nir_ieq_imm(b, mode_enum, 0x1);

      case nir_var_mem_global:
         return nir_ior(b, nir_ieq_imm(b, mode_enum, 0x0),
                        nir_ieq_imm(b, mode_enum, 0x3));

      default:
         UNREACHABLE("Invalid mode check intrinsic");
      }
   }

   default:
      UNREACHABLE("Unsupported address mode");
   }
}

unsigned
nir_address_format_bit_size(nir_address_format addr_format)
{
   switch (addr_format) {
   case nir_address_format_32bit_global:
      return 32;
   case nir_address_format_2x32bit_global:
      return 32;
   case nir_address_format_64bit_global:
      return 64;
   case nir_address_format_64bit_global_32bit_offset:
      return 32;
   case nir_address_format_64bit_bounded_global:
      return 32;
   case nir_address_format_32bit_index_offset:
      return 32;
   case nir_address_format_32bit_index_offset_pack64:
      return 64;
   case nir_address_format_vec2_index_32bit_offset:
      return 32;
   case nir_address_format_62bit_generic:
      return 64;
   case nir_address_format_32bit_offset:
      return 32;
   case nir_address_format_32bit_offset_as_64bit:
      return 64;
   case nir_address_format_logical:
      return 32;
   }
   UNREACHABLE("Invalid address format");
}

unsigned
nir_address_format_num_components(nir_address_format addr_format)
{
   switch (addr_format) {
   case nir_address_format_32bit_global:
      return 1;
   case nir_address_format_2x32bit_global:
      return 2;
   case nir_address_format_64bit_global:
      return 1;
   case nir_address_format_64bit_global_32bit_offset:
      return 4;
   case nir_address_format_64bit_bounded_global:
      return 4;
   case nir_address_format_32bit_index_offset:
      return 2;
   case nir_address_format_32bit_index_offset_pack64:
      return 1;
   case nir_address_format_vec2_index_32bit_offset:
      return 3;
   case nir_address_format_62bit_generic:
      return 1;
   case nir_address_format_32bit_offset:
      return 1;
   case nir_address_format_32bit_offset_as_64bit:
      return 1;
   case nir_address_format_logical:
      return 1;
   }
   UNREACHABLE("Invalid address format");
}

static nir_def *
addr_to_index(nir_builder *b, nir_def *addr,
              nir_address_format addr_format)
{
   switch (addr_format) {
   case nir_address_format_32bit_index_offset:
      assert(addr->num_components == 2);
      return nir_channel(b, addr, 0);
   case nir_address_format_32bit_index_offset_pack64:
      return nir_unpack_64_2x32_split_y(b, addr);
   case nir_address_format_vec2_index_32bit_offset:
      assert(addr->num_components == 3);
      return nir_trim_vector(b, addr, 2);
   default:
      UNREACHABLE("Invalid address format");
   }
}

static nir_def *
addr_to_offset(nir_builder *b, nir_def *addr,
               nir_address_format addr_format)
{
   switch (addr_format) {
   case nir_address_format_32bit_index_offset:
      assert(addr->num_components == 2);
      return nir_channel(b, addr, 1);
   case nir_address_format_32bit_index_offset_pack64:
      return nir_unpack_64_2x32_split_x(b, addr);
   case nir_address_format_vec2_index_32bit_offset:
      assert(addr->num_components == 3);
      return nir_channel(b, addr, 2);
   case nir_address_format_32bit_offset:
      return addr;
   case nir_address_format_32bit_offset_as_64bit:
   case nir_address_format_62bit_generic:
      return nir_u2u32(b, addr);
   default:
      UNREACHABLE("Invalid address format");
   }
}

/** Returns true if the given address format resolves to a global address */
static bool
addr_format_is_global(nir_address_format addr_format,
                      nir_variable_mode mode)
{
   if (addr_format == nir_address_format_62bit_generic)
      return mode == nir_var_mem_global;

   return addr_format == nir_address_format_32bit_global ||
          addr_format == nir_address_format_2x32bit_global ||
          addr_format == nir_address_format_64bit_global ||
          addr_format == nir_address_format_64bit_global_32bit_offset ||
          addr_format == nir_address_format_64bit_bounded_global;
}

static bool
addr_format_is_offset(nir_address_format addr_format,
                      nir_variable_mode mode)
{
   if (addr_format == nir_address_format_62bit_generic)
      return mode != nir_var_mem_global;

   return addr_format == nir_address_format_32bit_offset ||
          addr_format == nir_address_format_32bit_offset_as_64bit;
}

static nir_def *
addr_to_global(nir_builder *b, nir_def *addr,
               nir_address_format addr_format)
{
   switch (addr_format) {
   case nir_address_format_32bit_global:
   case nir_address_format_64bit_global:
   case nir_address_format_62bit_generic:
      assert(addr->num_components == 1);
      return addr;

   case nir_address_format_2x32bit_global:
      assert(addr->num_components == 2);
      return addr;

   case nir_address_format_64bit_global_32bit_offset:
   case nir_address_format_64bit_bounded_global:
      assert(addr->num_components == 4);
      return nir_iadd(b, nir_pack_64_2x32(b, nir_trim_vector(b, addr, 2)),
                      nir_u2u64(b, nir_channel(b, addr, 3)));

   case nir_address_format_32bit_index_offset:
   case nir_address_format_32bit_index_offset_pack64:
   case nir_address_format_vec2_index_32bit_offset:
   case nir_address_format_32bit_offset:
   case nir_address_format_32bit_offset_as_64bit:
   case nir_address_format_logical:
      UNREACHABLE("Cannot get a 64-bit address with this address format");
   }

   UNREACHABLE("Invalid address format");
}

static bool
addr_format_needs_bounds_check(nir_address_format addr_format)
{
   return addr_format == nir_address_format_64bit_bounded_global;
}

static nir_def *
addr_is_in_bounds(nir_builder *b, nir_def *addr,
                  nir_address_format addr_format, unsigned size)
{
   assert(addr_format == nir_address_format_64bit_bounded_global);
   assert(addr->num_components == 4);
   assert(size > 0);
   return nir_ult(b, nir_iadd_imm(b, nir_channel(b, addr, 3), size - 1),
                  nir_channel(b, addr, 2));
}

static void
nir_get_explicit_deref_range(nir_deref_instr *deref,
                             nir_address_format addr_format,
                             uint32_t *out_base,
                             uint32_t *out_range)
{
   uint32_t base = 0;
   uint32_t range = glsl_get_explicit_size(deref->type, false);

   while (true) {
      nir_deref_instr *parent = nir_deref_instr_parent(deref);

      switch (deref->deref_type) {
      case nir_deref_type_array:
      case nir_deref_type_array_wildcard:
      case nir_deref_type_ptr_as_array: {
         const unsigned stride = nir_deref_instr_array_stride(deref);
         if (stride == 0)
            goto fail;

         if (!parent)
            goto fail;

         if (deref->deref_type != nir_deref_type_array_wildcard &&
             nir_src_is_const(deref->arr.index)) {
            base += stride * nir_src_as_uint(deref->arr.index);
         } else {
            if (glsl_get_length(parent->type) == 0)
               goto fail;
            range += stride * (glsl_get_length(parent->type) - 1);
         }
         break;
      }

      case nir_deref_type_struct: {
         if (!parent)
            goto fail;

         base += glsl_get_struct_field_offset(parent->type, deref->strct.index);
         break;
      }

      case nir_deref_type_cast: {
         nir_instr *parent_instr = nir_def_instr(deref->parent.ssa);

         switch (parent_instr->type) {
         case nir_instr_type_load_const: {
            nir_load_const_instr *load = nir_instr_as_load_const(parent_instr);

            switch (addr_format) {
            case nir_address_format_32bit_offset:
               base += load->value[1].u32;
               break;
            case nir_address_format_32bit_index_offset:
               base += load->value[1].u32;
               break;
            case nir_address_format_vec2_index_32bit_offset:
               base += load->value[2].u32;
               break;
            default:
               goto fail;
            }

            *out_base = base;
            *out_range = range;
            return;
         }

         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(parent_instr);
            switch (intr->intrinsic) {
            case nir_intrinsic_load_vulkan_descriptor:
               /* Assume that a load_vulkan_descriptor won't contribute to an
                * offset within the resource.
                */
               break;
            default:
               goto fail;
            }

            *out_base = base;
            *out_range = range;
            return;
         }

         default:
            goto fail;
         }
      }

      default:
         goto fail;
      }

      deref = parent;
   }

fail:
   *out_base = 0;
   *out_range = ~0;
}

static nir_variable_mode
canonicalize_generic_modes(nir_variable_mode modes)
{
   assert(modes != 0);
   if (util_bitcount(modes) == 1)
      return modes;

   assert(!(modes & ~(nir_var_function_temp | nir_var_shader_temp |
                      nir_var_mem_shared | nir_var_mem_global)));

   /* Canonicalize by converting shader_temp to function_temp */
   if (modes & nir_var_shader_temp) {
      modes &= ~nir_var_shader_temp;
      modes |= nir_var_function_temp;
   }

   return modes;
}

static nir_intrinsic_op
get_store_global_op_from_addr_format(nir_address_format addr_format)
{
   if (addr_format != nir_address_format_2x32bit_global)
      return nir_intrinsic_store_global;
   else
      return nir_intrinsic_store_global_2x32;
}

static nir_intrinsic_op
get_load_global_op_from_addr_format(nir_address_format addr_format)
{
   if (addr_format != nir_address_format_2x32bit_global)
      return nir_intrinsic_load_global;
   else
      return nir_intrinsic_load_global_2x32;
}

static nir_intrinsic_op
get_load_global_constant_op_from_addr_format(nir_address_format addr_format)
{
   if (addr_format != nir_address_format_2x32bit_global)
      return nir_intrinsic_load_global_constant;
   else
      return nir_intrinsic_load_global_2x32; /* no dedicated op, fallback */
}

static nir_def *
build_explicit_io_load(nir_builder *b, nir_intrinsic_instr *intrin,
                       nir_io_offset addr_shift, nir_address_format addr_format,
                       nir_variable_mode modes,
                       uint32_t align_mul, uint32_t align_offset,
                       unsigned num_components)
{
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   modes = canonicalize_generic_modes(modes);
   nir_def *addr = addr_shift.def;

   if (util_bitcount(modes) > 1) {
      if (addr_format_is_global(addr_format, modes)) {
         return build_explicit_io_load(b, intrin, addr_shift, addr_format,
                                       nir_var_mem_global,
                                       align_mul, align_offset,
                                       num_components);
      } else if (modes & nir_var_function_temp) {
         nir_push_if(b, build_runtime_addr_mode_check(b, addr, addr_format,
                                                      nir_var_function_temp));
         nir_def *res1 =
            build_explicit_io_load(b, intrin, addr_shift, addr_format,
                                   nir_var_function_temp,
                                   align_mul, align_offset,
                                   num_components);
         nir_push_else(b, NULL);
         nir_def *res2 =
            build_explicit_io_load(b, intrin, addr_shift, addr_format,
                                   modes & ~nir_var_function_temp,
                                   align_mul, align_offset,
                                   num_components);
         nir_pop_if(b, NULL);
         return nir_if_phi(b, res1, res2);
      } else {
         nir_push_if(b, build_runtime_addr_mode_check(b, addr, addr_format,
                                                      nir_var_mem_shared));
         assert(modes & nir_var_mem_shared);
         nir_def *res1 =
            build_explicit_io_load(b, intrin, addr_shift, addr_format,
                                   nir_var_mem_shared,
                                   align_mul, align_offset,
                                   num_components);
         nir_push_else(b, NULL);
         assert(modes & nir_var_mem_global);
         nir_def *res2 =
            build_explicit_io_load(b, intrin, addr_shift, addr_format,
                                   nir_var_mem_global,
                                   align_mul, align_offset,
                                   num_components);
         nir_pop_if(b, NULL);
         return nir_if_phi(b, res1, res2);
      }
   }

   assert(util_bitcount(modes) == 1);
   const nir_variable_mode mode = modes;

   nir_intrinsic_op op;
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_deref:
      switch (mode) {
      case nir_var_mem_ubo:
         if (addr_format == nir_address_format_64bit_global_32bit_offset)
            op = nir_intrinsic_load_global_constant_offset;
         else if (addr_format == nir_address_format_64bit_bounded_global)
            op = nir_intrinsic_load_global_constant_bounded;
         else if (addr_format_is_global(addr_format, mode))
            op = nir_intrinsic_load_global_constant;
         else
            op = nir_intrinsic_load_ubo;
         break;
      case nir_var_mem_ssbo:
         if (addr_format == nir_address_format_64bit_bounded_global &&
             b->shader->options->has_load_global_bounded)
            op = nir_intrinsic_load_global_bounded;
         else if (addr_format_is_global(addr_format, mode))
            op = nir_intrinsic_load_global;
         else
            op = nir_intrinsic_load_ssbo;
         break;
      case nir_var_mem_global:
         assert(addr_format_is_global(addr_format, mode));

         if (nir_intrinsic_has_access(intrin) &&
             (nir_intrinsic_access(intrin) & ACCESS_CAN_REORDER))
            op = get_load_global_constant_op_from_addr_format(addr_format);
         else
            op = get_load_global_op_from_addr_format(addr_format);
         break;
      case nir_var_uniform:
         assert(addr_format_is_offset(addr_format, mode));
         assert(b->shader->info.stage == MESA_SHADER_KERNEL);
         op = nir_intrinsic_load_kernel_input;
         break;
      case nir_var_mem_shared:
         assert(addr_format_is_offset(addr_format, mode));
         op = nir_intrinsic_load_shared;
         break;
      case nir_var_mem_task_payload:
         assert(addr_format_is_offset(addr_format, mode));
         op = nir_intrinsic_load_task_payload;
         break;
      case nir_var_shader_temp:
      case nir_var_function_temp:
         if (addr_format_is_offset(addr_format, mode)) {
            op = nir_intrinsic_load_scratch;
         } else {
            assert(addr_format_is_global(addr_format, mode));
            op = get_load_global_op_from_addr_format(addr_format);
         }
         break;
      case nir_var_mem_push_const:
         assert(addr_format == nir_address_format_32bit_offset);
         op = nir_intrinsic_load_push_constant;
         break;
      case nir_var_mem_constant:
         if (addr_format_is_offset(addr_format, mode)) {
            op = nir_intrinsic_load_constant;
         } else {
            assert(addr_format_is_global(addr_format, mode));
            op = get_load_global_constant_op_from_addr_format(addr_format);
         }
         break;
      default:
         UNREACHABLE("Unsupported explicit IO variable mode");
      }
      break;

   case nir_intrinsic_load_deref_block_intel:
      switch (mode) {
      case nir_var_mem_ssbo:
         if (addr_format_is_global(addr_format, mode))
            op = nir_intrinsic_load_global_block_intel;
         else
            op = nir_intrinsic_load_ssbo_block_intel;
         break;
      case nir_var_mem_global:
         op = nir_intrinsic_load_global_block_intel;
         break;
      case nir_var_mem_shared:
         op = nir_intrinsic_load_shared_block_intel;
         break;
      default:
         UNREACHABLE("Unsupported explicit IO variable mode");
      }
      break;

   default:
      UNREACHABLE("Invalid intrinsic");
   }

   nir_intrinsic_instr *load = nir_intrinsic_instr_create(b->shader, op);

   if (op == nir_intrinsic_load_global_constant_offset) {
      assert(addr_format == nir_address_format_64bit_global_32bit_offset);
      load->src[0] = nir_src_for_ssa(
         nir_pack_64_2x32(b, nir_trim_vector(b, addr, 2)));
      load->src[1] = nir_src_for_ssa(nir_channel(b, addr, 3));
   } else if (op == nir_intrinsic_load_global_bounded ||
              op == nir_intrinsic_load_global_constant_bounded) {
      assert(addr_format == nir_address_format_64bit_bounded_global);
      load->src[0] = nir_src_for_ssa(
         nir_pack_64_2x32(b, nir_trim_vector(b, addr, 2)));
      load->src[1] = nir_src_for_ssa(nir_channel(b, addr, 3));
      load->src[2] = nir_src_for_ssa(nir_channel(b, addr, 2));
   } else if (addr_format_is_global(addr_format, mode)) {
      load->src[0] = nir_src_for_ssa(addr_to_global(b, addr, addr_format));
   } else if (addr_format_is_offset(addr_format, mode)) {
      assert(addr->num_components == 1);
      load->src[0] = nir_src_for_ssa(addr_to_offset(b, addr, addr_format));
   } else {
      load->src[0] = nir_src_for_ssa(addr_to_index(b, addr, addr_format));
      load->src[1] = nir_src_for_ssa(addr_to_offset(b, addr, addr_format));
   }

   if (nir_intrinsic_has_access(load))
      nir_intrinsic_set_access(load, nir_intrinsic_access(intrin));

   if (op == nir_intrinsic_load_constant) {
      nir_intrinsic_set_base(load, 0);
      nir_intrinsic_set_range(load, b->shader->constant_data_size);
   } else if (op == nir_intrinsic_load_kernel_input) {
      nir_intrinsic_set_base(load, 0);
      nir_intrinsic_set_range(load, b->shader->num_uniforms);
   } else if (mode == nir_var_mem_push_const) {
      /* Push constants are required to be able to be chased back to the
       * variable so we can provide a base/range.
       */
      nir_variable *var = nir_deref_instr_get_variable(deref);
      nir_intrinsic_set_base(load, 0);
      nir_intrinsic_set_range(load, glsl_get_explicit_size(var->type, false));
   }

   unsigned bit_size = intrin->def.bit_size;
   if (bit_size == 1) {
      /* TODO: Make the native bool bit_size an option. */
      bit_size = 32;
   }

   if (nir_intrinsic_has_align(load))
      nir_intrinsic_set_align(load, align_mul, align_offset);

   if (nir_intrinsic_has_range_base(load)) {
      unsigned base, range;
      nir_get_explicit_deref_range(deref, addr_format, &base, &range);
      nir_intrinsic_set_range_base(load, base);
      nir_intrinsic_set_range(load, range);
   }

   if (addr_shift.shift)
      nir_intrinsic_set_offset_shift(load, addr_shift.shift);

   load->num_components = num_components;
   nir_def_init(&load->instr, &load->def, num_components, bit_size);

   assert(bit_size % 8 == 0);

   nir_def *result;
   if (addr_format_needs_bounds_check(addr_format) &&
       op != nir_intrinsic_load_global_constant_bounded &&
       op != nir_intrinsic_load_global_bounded) {
      /* We don't need to bounds-check global_(constant_)bounded because bounds
       * checking is handled by the intrinsic itself.
       *
       * The Vulkan spec for robustBufferAccess gives us quite a few options
       * as to what we can do with an OOB read.  Unfortunately, returning
       * undefined values isn't one of them so we return an actual zero.
       */
      nir_def *zero = nir_imm_zero(b, load->num_components, bit_size);

      /* TODO: Better handle block_intel. */
      assert(load->num_components == 1);
      const unsigned load_size = bit_size / 8;
      nir_push_if(b, addr_is_in_bounds(b, addr, addr_format, load_size));

      nir_builder_instr_insert(b, &load->instr);

      nir_pop_if(b, NULL);

      result = nir_if_phi(b, &load->def, zero);
   } else {
      nir_builder_instr_insert(b, &load->instr);
      result = &load->def;
   }

   if (intrin->def.bit_size == 1) {
      /* For shared, we can go ahead and use NIR's and/or the back-end's
       * standard encoding for booleans rather than forcing a 0/1 boolean.
       * This should save an instruction or two.
       */
      if (mode == nir_var_mem_shared ||
          mode == nir_var_shader_temp ||
          mode == nir_var_function_temp)
         result = nir_b2b1(b, result);
      else
         result = nir_i2b(b, result);
   }

   return result;
}

static void
build_explicit_io_store(nir_builder *b, nir_intrinsic_instr *intrin,
                        nir_io_offset addr_shift, nir_address_format addr_format,
                        nir_variable_mode modes,
                        uint32_t align_mul, uint32_t align_offset,
                        nir_def *value, nir_component_mask_t write_mask)
{
   modes = canonicalize_generic_modes(modes);
   nir_def *addr = addr_shift.def;

   if (util_bitcount(modes) > 1) {
      if (addr_format_is_global(addr_format, modes)) {
         build_explicit_io_store(b, intrin, addr_shift, addr_format,
                                 nir_var_mem_global,
                                 align_mul, align_offset,
                                 value, write_mask);
      } else if (modes & nir_var_function_temp) {
         nir_push_if(b, build_runtime_addr_mode_check(b, addr, addr_format,
                                                      nir_var_function_temp));
         build_explicit_io_store(b, intrin, addr_shift, addr_format,
                                 nir_var_function_temp,
                                 align_mul, align_offset,
                                 value, write_mask);
         nir_push_else(b, NULL);
         build_explicit_io_store(b, intrin, addr_shift, addr_format,
                                 modes & ~nir_var_function_temp,
                                 align_mul, align_offset,
                                 value, write_mask);
         nir_pop_if(b, NULL);
      } else {
         nir_push_if(b, build_runtime_addr_mode_check(b, addr, addr_format,
                                                      nir_var_mem_shared));
         assert(modes & nir_var_mem_shared);
         build_explicit_io_store(b, intrin, addr_shift, addr_format,
                                 nir_var_mem_shared,
                                 align_mul, align_offset,
                                 value, write_mask);
         nir_push_else(b, NULL);
         assert(modes & nir_var_mem_global);
         build_explicit_io_store(b, intrin, addr_shift, addr_format,
                                 nir_var_mem_global,
                                 align_mul, align_offset,
                                 value, write_mask);
         nir_pop_if(b, NULL);
      }
      return;
   }

   assert(util_bitcount(modes) == 1);
   const nir_variable_mode mode = modes;

   nir_intrinsic_op op;
   switch (intrin->intrinsic) {
   case nir_intrinsic_store_deref:
      assert(write_mask != 0);

      switch (mode) {
      case nir_var_mem_ssbo:
         if (addr_format_is_global(addr_format, mode))
            op = get_store_global_op_from_addr_format(addr_format);
         else
            op = nir_intrinsic_store_ssbo;
         break;
      case nir_var_mem_global:
         assert(addr_format_is_global(addr_format, mode));
         op = get_store_global_op_from_addr_format(addr_format);
         break;
      case nir_var_mem_shared:
         assert(addr_format_is_offset(addr_format, mode));
         op = nir_intrinsic_store_shared;
         break;
      case nir_var_mem_task_payload:
         assert(addr_format_is_offset(addr_format, mode));
         op = nir_intrinsic_store_task_payload;
         break;
      case nir_var_shader_temp:
      case nir_var_function_temp:
         if (addr_format_is_offset(addr_format, mode)) {
            op = nir_intrinsic_store_scratch;
         } else {
            assert(addr_format_is_global(addr_format, mode));
            op = get_store_global_op_from_addr_format(addr_format);
         }
         break;
      default:
         UNREACHABLE("Unsupported explicit IO variable mode");
      }
      break;

   case nir_intrinsic_store_deref_block_intel:
      assert(write_mask == 0);

      switch (mode) {
      case nir_var_mem_ssbo:
         if (addr_format_is_global(addr_format, mode))
            op = nir_intrinsic_store_global_block_intel;
         else
            op = nir_intrinsic_store_ssbo_block_intel;
         break;
      case nir_var_mem_global:
         op = nir_intrinsic_store_global_block_intel;
         break;
      case nir_var_mem_shared:
         op = nir_intrinsic_store_shared_block_intel;
         break;
      default:
         UNREACHABLE("Unsupported explicit IO variable mode");
      }
      break;

   default:
      UNREACHABLE("Invalid intrinsic");
   }

   nir_intrinsic_instr *store = nir_intrinsic_instr_create(b->shader, op);

   if (value->bit_size == 1) {
      /* For shared, we can go ahead and use NIR's and/or the back-end's
       * standard encoding for booleans rather than forcing a 0/1 boolean.
       * This should save an instruction or two.
       *
       * TODO: Make the native bool bit_size an option.
       */
      if (mode == nir_var_mem_shared ||
          mode == nir_var_shader_temp ||
          mode == nir_var_function_temp)
         value = nir_b2b32(b, value);
      else
         value = nir_b2iN(b, value, 32);
   }

   store->src[0] = nir_src_for_ssa(value);
   if (addr_format_is_global(addr_format, mode)) {
      store->src[1] = nir_src_for_ssa(addr_to_global(b, addr, addr_format));
   } else if (addr_format_is_offset(addr_format, mode)) {
      assert(addr->num_components == 1);
      store->src[1] = nir_src_for_ssa(addr_to_offset(b, addr, addr_format));
   } else {
      store->src[1] = nir_src_for_ssa(addr_to_index(b, addr, addr_format));
      store->src[2] = nir_src_for_ssa(addr_to_offset(b, addr, addr_format));
   }

   nir_intrinsic_set_write_mask(store, write_mask);

   if (nir_intrinsic_has_access(store))
      nir_intrinsic_set_access(store, nir_intrinsic_access(intrin));

   nir_intrinsic_set_align(store, align_mul, align_offset);

   if (addr_shift.shift)
      nir_intrinsic_set_offset_shift(store, addr_shift.shift);

   assert(value->num_components == 1 ||
          value->num_components == intrin->num_components);
   store->num_components = value->num_components;

   assert(value->bit_size % 8 == 0);

   if (addr_format_needs_bounds_check(addr_format)) {
      /* TODO: Better handle block_intel. */
      assert(store->num_components == 1);
      const unsigned store_size = value->bit_size / 8;
      nir_push_if(b, addr_is_in_bounds(b, addr, addr_format, store_size));

      nir_builder_instr_insert(b, &store->instr);

      nir_pop_if(b, NULL);
   } else {
      nir_builder_instr_insert(b, &store->instr);
   }
}

static nir_def *
build_explicit_io_atomic(nir_builder *b, nir_intrinsic_instr *intrin,
                         nir_io_offset addr_shift,
                         nir_address_format addr_format,
                         nir_variable_mode modes)
{
   modes = canonicalize_generic_modes(modes);
   nir_def *addr = addr_shift.def;

   if (util_bitcount(modes) > 1) {
      if (addr_format_is_global(addr_format, modes)) {
         return build_explicit_io_atomic(b, intrin, addr_shift, addr_format,
                                         nir_var_mem_global);
      } else if (modes & nir_var_function_temp) {
         nir_push_if(b, build_runtime_addr_mode_check(b, addr, addr_format,
                                                      nir_var_function_temp));
         nir_def *res1 =
            build_explicit_io_atomic(b, intrin, addr_shift, addr_format,
                                     nir_var_function_temp);
         nir_push_else(b, NULL);
         nir_def *res2 =
            build_explicit_io_atomic(b, intrin, addr_shift, addr_format,
                                     modes & ~nir_var_function_temp);
         nir_pop_if(b, NULL);
         return nir_if_phi(b, res1, res2);
      } else {
         nir_push_if(b, build_runtime_addr_mode_check(b, addr, addr_format,
                                                      nir_var_mem_shared));
         assert(modes & nir_var_mem_shared);
         nir_def *res1 =
            build_explicit_io_atomic(b, intrin, addr_shift, addr_format,
                                     nir_var_mem_shared);
         nir_push_else(b, NULL);
         assert(modes & nir_var_mem_global);
         nir_def *res2 =
            build_explicit_io_atomic(b, intrin, addr_shift, addr_format,
                                     nir_var_mem_global);
         nir_pop_if(b, NULL);
         return nir_if_phi(b, res1, res2);
      }
   }

   assert(util_bitcount(modes) == 1);
   const nir_variable_mode mode = modes;

   const unsigned num_data_srcs =
      nir_intrinsic_infos[intrin->intrinsic].num_srcs - 1;

   nir_intrinsic_op op;
   switch (mode) {
   case nir_var_mem_ssbo:
      if (addr_format_is_global(addr_format, mode))
         op = global_atomic_for_deref(addr_format, intrin->intrinsic);
      else
         op = ssbo_atomic_for_deref(intrin->intrinsic);
      break;
   case nir_var_mem_global:
      assert(addr_format_is_global(addr_format, mode));
      op = global_atomic_for_deref(addr_format, intrin->intrinsic);
      break;
   case nir_var_mem_shared:
      assert(addr_format_is_offset(addr_format, mode));
      op = shared_atomic_for_deref(intrin->intrinsic);
      break;
   case nir_var_mem_task_payload:
      assert(addr_format_is_offset(addr_format, mode));
      op = task_payload_atomic_for_deref(intrin->intrinsic);
      break;
   default:
      UNREACHABLE("Unsupported explicit IO variable mode");
   }

   nir_intrinsic_instr *atomic = nir_intrinsic_instr_create(b->shader, op);
   nir_intrinsic_set_atomic_op(atomic, nir_intrinsic_atomic_op(intrin));

   unsigned src = 0;
   if (addr_format_is_global(addr_format, mode)) {
      atomic->src[src++] = nir_src_for_ssa(addr_to_global(b, addr, addr_format));
   } else if (addr_format_is_offset(addr_format, mode)) {
      assert(addr->num_components == 1);
      atomic->src[src++] = nir_src_for_ssa(addr_to_offset(b, addr, addr_format));
   } else {
      atomic->src[src++] = nir_src_for_ssa(addr_to_index(b, addr, addr_format));
      atomic->src[src++] = nir_src_for_ssa(addr_to_offset(b, addr, addr_format));
   }
   for (unsigned i = 0; i < num_data_srcs; i++) {
      atomic->src[src++] = nir_src_for_ssa(intrin->src[1 + i].ssa);
   }

   /* Global atomics don't have access flags because they assume that the
    * address may be non-uniform.
    */
   if (nir_intrinsic_has_access(atomic))
      nir_intrinsic_set_access(atomic, nir_intrinsic_access(intrin));

   if (addr_shift.shift)
      nir_intrinsic_set_offset_shift(atomic, addr_shift.shift);

   assert(intrin->def.num_components == 1);
   nir_def_init(&atomic->instr, &atomic->def, 1,
                intrin->def.bit_size);

   assert(atomic->def.bit_size % 8 == 0);

   if (addr_format_needs_bounds_check(addr_format)) {
      const unsigned atomic_size = atomic->def.bit_size / 8;
      nir_push_if(b, addr_is_in_bounds(b, addr, addr_format, atomic_size));

      nir_builder_instr_insert(b, &atomic->instr);

      nir_pop_if(b, NULL);
      return nir_if_phi(b, &atomic->def,
                        nir_undef(b, 1, atomic->def.bit_size));
   } else {
      nir_builder_instr_insert(b, &atomic->instr);
      return &atomic->def;
   }
}

/* The resulting address will be right-shifted by `shift` and any bits that
 * got shifted-out by that will be put in `leftover`. So the final byte
 * address will be: (ret << shift) + *leftover.
 */
static nir_def *
explicit_io_offset_from_deref(nir_builder *b, nir_deref_instr *deref,
                              unsigned offset_bit_size, unsigned shift,
                              unsigned *leftover)
{
   switch (deref->deref_type) {
   case nir_deref_type_var:
      return NULL;

   case nir_deref_type_ptr_as_array:
   case nir_deref_type_array: {
      unsigned stride = nir_deref_instr_array_stride(deref);
      assert(stride > 0);

      nir_def *index = deref->arr.index.ssa;
      nir_def *offset;

      unsigned max_stride_shift = ffs(stride) - 1;
      unsigned stride_shift = MIN2(shift, max_stride_shift);

      if (stride_shift < shift) {
         /* The stride isn't aligned enough to fully shift right. Try to apply
          * the leftover shift to the index. We can only do this (without
          * losing precision) if the index is constant.
          */
         assert(nir_src_is_const(deref->arr.index));

         unsigned index_shift = shift - stride_shift;
         int64_t const_index = nir_src_as_int(deref->arr.index);

         if (!util_is_aligned(const_index, (uintmax_t)1 << index_shift)) {
            assert(leftover);

            /* The index isn't aligned enough either. Just put the full offset
             * in `leftover` and return zero.
             */
            *leftover = stride * const_index;
            return nir_imm_intN_t(b, 0, deref->arr.index.ssa->bit_size);
         }

         index = nir_imm_intN_t(b, const_index >> index_shift,
                                deref->arr.index.ssa->bit_size);
      }

      stride >>= stride_shift;

      /* If the access chain has been declared in-bounds, then we know it doesn't
       * overflow the type.  For nir_deref_type_array, this implies it cannot be
       * negative. Also, since types in NIR have a maximum 32-bit size, we know the
       * final result will fit in a 32-bit value so we can convert the index to
       * 32-bit before multiplying and save ourselves from a 64-bit multiply.
       */
      if (deref->arr.in_bounds && deref->deref_type == nir_deref_type_array) {
         index = nir_u2u32(b, index);
         offset = nir_u2uN(b, nir_amul_imm(b, index, stride), offset_bit_size);
      } else {
         index = nir_i2iN(b, index, offset_bit_size);
         offset = nir_amul_imm(b, index, stride);
      }

      return offset;
   }

   case nir_deref_type_array_wildcard:
      UNREACHABLE("Wildcards should be lowered by now");
      break;

   case nir_deref_type_struct: {
      nir_deref_instr *parent = nir_deref_instr_parent(deref);
      int offset = glsl_get_struct_field_offset(parent->type,
                                                deref->strct.index);
      assert(offset >= 0);

      if (!util_is_aligned(offset, (uintmax_t)1 << shift)) {
         assert(leftover);

         /* The offset isn't aligned enough to fully shift right. Just put it
          * in leftover and return zero.
          */
         *leftover = offset;
         return nir_imm_intN_t(b, 0, offset_bit_size);
      }

      offset >>= shift;
      return nir_imm_intN_t(b, offset, offset_bit_size);
   }

   case nir_deref_type_cast:
      /* Nothing to do here */
      return NULL;
   }

   UNREACHABLE("Invalid NIR deref type");
}

nir_def *
nir_explicit_io_address_from_deref(nir_builder *b, nir_deref_instr *deref,
                                   nir_def *base_addr,
                                   nir_address_format addr_format)
{
   if (deref->deref_type == nir_deref_type_var) {
      return build_addr_for_var(b, deref->var, addr_format);
   }

   nir_def *offset = explicit_io_offset_from_deref(
      b, deref, addr_get_offset_bit_size(base_addr, addr_format), 0, NULL);
   return offset ? nir_build_addr_iadd(b, base_addr, addr_format, deref->modes,
                                       offset)
                 : base_addr;
}

/* Walk the full deref chain and return the resulting address. The resulting
 * address will be right-shifted by `shift` and any bits that got shifted-out
 * by that will be put in `leftover`. So the final byte address will be:
 *    (ret << shift) + *leftover.
 *
 * Note on `shift` and `leftover`: we try to shift-right the resulting address
 * by shifting-right the array strides and struct offsets. As long as `shift`
 * isn't larger than the alignment, this should generally work without
 * requiring the `leftover` value (i.e., all intermediary strides and offsets
 * should be properly aligned). However, `leftover` is necessary in cases like
 * this:
 *
 * struct {
 *    uint16_t a;
 *    uint8_t b;
 *    uint8_t c[3];
 * } s;
 * use s.c[1];
 *
 * s.c[1] is 2-byte aligned but neither s.c[]'s base offset (3) nor its stride
 * (1) are. In this case, the returned address will be zero while `leftover`
 * is set to 4.
 */
static nir_def *
explicit_io_address_from_deref_chain_aux(nir_builder *b, nir_deref_instr *deref,
                                         nir_address_format addr_format,
                                         nir_variable_mode modes,
                                         unsigned shift, unsigned *leftover)
{
   unsigned offset_bit_size =
      addr_get_offset_bit_size(&deref->def, addr_format);

   switch (deref->deref_type) {
   case nir_deref_type_var:
      UNREACHABLE("Unsupported deref type");
      return NULL;

   case nir_deref_type_cast:
      if (nir_src_is_deref(deref->parent)) {
         return explicit_io_address_from_deref_chain_aux(
            b, nir_deref_instr_parent(deref), addr_format, modes, shift,
            leftover);
      } else {
         /* For casts of non-deref instructions, there's nothing we can do
          * besides simply right-shifting the result. As long as the cast's
          * alignment is large enough, this should be fine. This is ensured in
          * build_deref_addr by taking the alignment information into account.
          * Note that it's correct to use a logical shift as the cast has to
          * be in-bounds which means its result cannot be negative.
          */
         return build_addr_ushr_imm(b, deref->parent.ssa, addr_format, shift);
      }

   default: {
      unsigned offset_leftover = 0;
      nir_def *offset = explicit_io_offset_from_deref(b, deref, offset_bit_size,
                                                      shift, &offset_leftover);
      assert(offset);

      unsigned parent_leftover = 0;
      nir_def *parent_addr = explicit_io_address_from_deref_chain_aux(
         b, nir_deref_instr_parent(deref), addr_format, modes, shift,
         &parent_leftover);

      if (offset_leftover || parent_leftover) {
         assert(leftover && *leftover == 0);
         *leftover = offset_leftover + parent_leftover;
      }

      return nir_build_addr_iadd(b, parent_addr, addr_format, modes, offset);
   }
   }
}

static nir_def *
explicit_io_address_from_deref_chain(nir_builder *b, nir_deref_instr *deref,
                                     nir_address_format addr_format,
                                     nir_variable_mode modes, unsigned shift,
                                     unsigned comp_offset)
{
   unsigned leftover = 0;
   nir_def *addr = explicit_io_address_from_deref_chain_aux(
      b, deref, addr_format, modes, shift, &leftover);

   unsigned extra_offset = comp_offset + leftover;
   assert(util_is_aligned(extra_offset, (uintmax_t)1 << shift));

   return nir_build_addr_iadd_imm(b, addr, addr_format, modes,
                                  extra_offset >> shift);
}

static unsigned
get_max_shift(nir_intrinsic_instr *intrin,
              const nir_shader_compiler_options *options)
{
   if (options->max_offset_shift) {
      return options->max_offset_shift(intrin, options->cb_data);
   }

   return 0;
}

static nir_io_offset
build_addr(nir_builder *b, nir_intrinsic_instr *intrin, nir_def *base_addr,
           nir_address_format addr_format, unsigned comp_offset,
           uint32_t align_mul, uint32_t align_offset)
{
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   assert(deref);

   nir_io_offset addr;
   unsigned max_shift = get_max_shift(intrin, b->shader->options);

   if (max_shift) {
      /* In order to calculate a shifted address, we have to walk the full
       * deref chain. In that case, providing a base_addr that's not the
       * actual deref probably won't produce the desired result.
       */
      assert(base_addr == &deref->def);

      /* Don't try to shift more than the alignment would allow. This ensures
       * we can just right-shift casts.
       */
      unsigned align = nir_combined_align(align_mul, align_offset);
      addr.shift = MIN2(max_shift, util_logbase2(align));
      addr.def = explicit_io_address_from_deref_chain(
         b, deref, addr_format, deref->modes, addr.shift, comp_offset);
   } else {
      addr.def = nir_build_addr_iadd_imm(b, base_addr, addr_format,
                                         deref->modes, comp_offset);
      addr.shift = 0;
   }

   return addr;
}

void
nir_lower_explicit_io_instr(nir_builder *b,
                            nir_intrinsic_instr *intrin,
                            nir_def *base_addr,
                            nir_address_format addr_format)
{
   b->cursor = nir_after_instr(&intrin->instr);

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   unsigned vec_stride = glsl_get_explicit_stride(deref->type);
   unsigned scalar_size = type_scalar_size_bytes(deref->type);
   if (vec_stride == 0) {
      vec_stride = scalar_size;
   } else {
      assert(glsl_type_is_vector(deref->type));
      assert(vec_stride >= scalar_size);
   }

   uint32_t align_mul, align_offset;
   if (!nir_get_explicit_deref_align(deref, true, &align_mul, &align_offset)) {
      /* If we don't have an alignment from the deref, assume scalar */
      align_mul = scalar_size;
      align_offset = 0;
   }

   /* In order for bounds checking to be correct as per the Vulkan spec,
    * we need to check at the individual component granularity.  Prior to
    * robustness2, we're technically allowed to be sloppy by 16B.  Even with
    * robustness2, UBO loads are allowed to have a granularity as high as 256B
    * depending on hardware limits.  However, we have none of that information
    * here.  Short of adding new address formats, the easiest way to do that
    * is to just split any loads and stores into individual components here.
    *
    * TODO: At some point in the future we may want to add more ops similar to
    * nir_intrinsic_load_global_(constant_)bounded and make bouds checking the
    * back-end's problem.  Another option would be to somehow plumb more of
    * that information through to nir_lower_explicit_io.  For now, however,
    * scalarizing is at least correct.
    */
   bool scalarize = vec_stride > scalar_size ||
                    addr_format_needs_bounds_check(addr_format);

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_deref: {
      nir_def *value;
      if (scalarize) {
         nir_def *comps[NIR_MAX_VEC_COMPONENTS] = {
            NULL,
         };
         for (unsigned i = 0; i < intrin->num_components; i++) {
            unsigned comp_offset = i * vec_stride;
            nir_io_offset comp_addr =
               build_addr(b, intrin, base_addr, addr_format, comp_offset,
                          align_mul, align_offset);
            comps[i] = build_explicit_io_load(b, intrin, comp_addr,
                                              addr_format, deref->modes,
                                              align_mul,
                                              (align_offset + comp_offset) %
                                                 align_mul,
                                              1);
         }
         value = nir_vec(b, comps, intrin->num_components);
      } else {
         nir_io_offset addr = build_addr(b, intrin, base_addr, addr_format, 0,
                                         align_mul, align_offset);
         value = build_explicit_io_load(b, intrin, addr, addr_format,
                                        deref->modes, align_mul, align_offset,
                                        intrin->num_components);
      }
      nir_def_rewrite_uses(&intrin->def, value);
      break;
   }

   case nir_intrinsic_store_deref: {
      nir_def *value = intrin->src[1].ssa;
      nir_component_mask_t write_mask = nir_intrinsic_write_mask(intrin);
      if (scalarize) {
         for (unsigned i = 0; i < intrin->num_components; i++) {
            if (!(write_mask & (1 << i)))
               continue;

            unsigned comp_offset = i * vec_stride;
            nir_io_offset comp_addr =
               build_addr(b, intrin, base_addr, addr_format, comp_offset,
                          align_mul, align_offset);
            build_explicit_io_store(b, intrin, comp_addr, addr_format,
                                    deref->modes, align_mul,
                                    (align_offset + comp_offset) % align_mul,
                                    nir_channel(b, value, i), 1);
         }
      } else {
         nir_io_offset addr = build_addr(b, intrin, base_addr, addr_format, 0,
                                         align_mul, align_offset);
         build_explicit_io_store(b, intrin, addr, addr_format,
                                 deref->modes, align_mul, align_offset,
                                 value, write_mask);
      }
      break;
   }

   case nir_intrinsic_load_deref_block_intel: {
      nir_io_offset addr = build_addr(b, intrin, base_addr, addr_format, 0,
                                      align_mul, align_offset);
      nir_def *value = build_explicit_io_load(b, intrin, addr, addr_format,
                                              deref->modes,
                                              align_mul, align_offset,
                                              intrin->num_components);
      nir_def_rewrite_uses(&intrin->def, value);
      break;
   }

   case nir_intrinsic_store_deref_block_intel: {
      nir_io_offset addr = build_addr(b, intrin, base_addr, addr_format, 0,
                                      align_mul, align_offset);
      nir_def *value = intrin->src[1].ssa;
      const nir_component_mask_t write_mask = 0;
      build_explicit_io_store(b, intrin, addr, addr_format,
                              deref->modes, align_mul, align_offset,
                              value, write_mask);
      break;
   }

   default: {
      nir_io_offset addr = build_addr(b, intrin, base_addr, addr_format, 0,
                                      align_mul, align_offset);
      nir_def *value =
         build_explicit_io_atomic(b, intrin, addr, addr_format, deref->modes);
      nir_def_rewrite_uses(&intrin->def, value);
      break;
   }
   }

   nir_instr_remove(&intrin->instr);
}

bool
nir_get_explicit_deref_align(nir_deref_instr *deref,
                             bool default_to_type_align,
                             uint32_t *align_mul,
                             uint32_t *align_offset)
{
   if (deref->deref_type == nir_deref_type_var) {
      /* If we see a variable, align_mul is effectively infinite because we
       * know the offset exactly (up to the offset of the base pointer for the
       * given variable mode).   We have to pick something so we choose 256B
       * as an arbitrary alignment which seems high enough for any reasonable
       * wide-load use-case.  Back-ends should clamp alignments down if 256B
       * is too large for some reason.
       */
      *align_mul = 256;
      *align_offset = deref->var->data.driver_location % 256;
      return true;
   }

   /* If we're a cast deref that has an alignment, use that. */
   if (deref->deref_type == nir_deref_type_cast && deref->cast.align_mul > 0) {
      *align_mul = deref->cast.align_mul;
      *align_offset = deref->cast.align_offset;
      return true;
   }

   /* Otherwise, we need to compute the alignment based on the parent */
   nir_deref_instr *parent = nir_deref_instr_parent(deref);
   if (parent == NULL) {
      assert(deref->deref_type == nir_deref_type_cast);
      if (default_to_type_align) {
         /* If we don't have a parent, assume the type's alignment, if any. */
         unsigned type_align = glsl_get_explicit_alignment(deref->type);
         if (type_align == 0)
            return false;

         *align_mul = type_align;
         *align_offset = 0;
         return true;
      } else {
         return false;
      }
   }

   uint32_t parent_mul, parent_offset;
   if (!nir_get_explicit_deref_align(parent, default_to_type_align,
                                     &parent_mul, &parent_offset))
      return false;

   switch (deref->deref_type) {
   case nir_deref_type_var:
      UNREACHABLE("Handled above");

   case nir_deref_type_array:
   case nir_deref_type_array_wildcard:
   case nir_deref_type_ptr_as_array: {
      const unsigned stride = nir_deref_instr_array_stride(deref);
      if (stride == 0)
         return false;

      if (deref->deref_type != nir_deref_type_array_wildcard &&
          nir_src_is_const(deref->arr.index)) {
         unsigned offset = nir_src_as_uint(deref->arr.index) * stride;
         *align_mul = parent_mul;
         *align_offset = (parent_offset + offset) % parent_mul;
      } else {
         /* If this is a wildcard or an indirect deref, we have to go with the
          * power-of-two gcd.
          */
         *align_mul = MIN2(parent_mul, 1 << (ffs(stride) - 1));
         *align_offset = parent_offset % *align_mul;
      }
      return true;
   }

   case nir_deref_type_struct: {
      const int offset = glsl_get_struct_field_offset(parent->type,
                                                      deref->strct.index);
      if (offset < 0)
         return false;

      *align_mul = parent_mul;
      *align_offset = (parent_offset + offset) % parent_mul;
      return true;
   }

   case nir_deref_type_cast:
      /* We handled the explicit alignment case above. */
      assert(deref->cast.align_mul == 0);
      *align_mul = parent_mul;
      *align_offset = parent_offset;
      return true;
   }

   UNREACHABLE("Invalid deref_instr_type");
}

static void
lower_explicit_io_deref(nir_builder *b, nir_deref_instr *deref,
                        nir_address_format addr_format)
{
   /* Ignore samplers/textures, because they are handled by other passes like `nir_lower_samplers`.
    * Also do it only for those being uniforms, otherwise it will break GL bindless textures handles
    * stored in UBOs.
    */
   if (nir_deref_mode_is_in_set(deref, nir_var_uniform) &&
       (glsl_type_is_sampler(deref->type) ||
        glsl_type_is_texture(deref->type)))
      return;

   /* Just delete the deref if it's not used.  We can't use
    * nir_deref_instr_remove_if_unused here because it may remove more than
    * one deref which could break our list walking since we walk the list
    * backwards.
    */
   if (nir_def_is_unused(&deref->def)) {
      nir_instr_remove(&deref->instr);
      return;
   }

   b->cursor = nir_after_instr(&deref->instr);

   nir_def *base_addr = NULL;
   if (deref->deref_type != nir_deref_type_var) {
      base_addr = deref->parent.ssa;
   }

   nir_def *addr = nir_explicit_io_address_from_deref(b, deref, base_addr,
                                                      addr_format);
   assert(addr->bit_size == deref->def.bit_size);
   assert(addr->num_components == deref->def.num_components);

   nir_instr_remove(&deref->instr);
   nir_def_rewrite_uses(&deref->def, addr);
}

static void
lower_explicit_io_access(nir_builder *b, nir_intrinsic_instr *intrin,
                         nir_address_format addr_format)
{
   nir_lower_explicit_io_instr(b, intrin, intrin->src[0].ssa, addr_format);
}

static void
lower_explicit_io_array_length(nir_builder *b, nir_intrinsic_instr *intrin,
                               nir_address_format addr_format)
{
   b->cursor = nir_after_instr(&intrin->instr);

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

   assert(glsl_type_is_array(deref->type));
   assert(glsl_get_length(deref->type) == 0);
   assert(nir_deref_mode_is(deref, nir_var_mem_ssbo));
   unsigned stride = glsl_get_explicit_stride(deref->type);
   assert(stride > 0);

   nir_def *addr = &deref->def;

   nir_def *offset, *size;
   switch (addr_format) {
   case nir_address_format_64bit_global_32bit_offset:
   case nir_address_format_64bit_bounded_global:
      offset = nir_channel(b, addr, 3);
      size = nir_channel(b, addr, 2);
      break;

   case nir_address_format_32bit_index_offset:
   case nir_address_format_32bit_index_offset_pack64:
   case nir_address_format_vec2_index_32bit_offset: {
      offset = addr_to_offset(b, addr, addr_format);
      nir_def *index = addr_to_index(b, addr, addr_format);
      unsigned access = nir_intrinsic_access(intrin);
      size = nir_get_ssbo_size(b, index, .access = access);
      break;
   }

   default:
      UNREACHABLE("Cannot determine SSBO size");
   }

   nir_def *remaining = nir_usub_sat(b, size, offset);
   nir_def *arr_size = nir_udiv_imm(b, remaining, stride);

   nir_def_replace(&intrin->def, arr_size);
}

static void
lower_explicit_io_mode_check(nir_builder *b, nir_intrinsic_instr *intrin,
                             nir_address_format addr_format)
{
   if (addr_format_is_global(addr_format, 0)) {
      /* If the address format is always global, then the driver can use
       * global addresses regardless of the mode.  In that case, don't create
       * a check, just whack the intrinsic to addr_mode_is and delegate to the
       * driver lowering.
       */
      intrin->intrinsic = nir_intrinsic_addr_mode_is;
      return;
   }

   nir_def *addr = intrin->src[0].ssa;

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_def *is_mode =
      build_runtime_addr_mode_check(b, addr, addr_format,
                                    nir_intrinsic_memory_modes(intrin));

   nir_def_rewrite_uses(&intrin->def, is_mode);
}

static bool
nir_lower_explicit_io_impl(nir_function_impl *impl, nir_variable_mode modes,
                           nir_address_format addr_format)
{
   bool progress = false;

   nir_builder b = nir_builder_create(impl);

   /* Walk in reverse order so that we can see the full deref chain when we
    * lower the access operations.  We lower them assuming that the derefs
    * will be turned into address calculations later.
    */
   nir_foreach_block_reverse(block, impl) {
      nir_foreach_instr_reverse_safe(instr, block) {
         switch (instr->type) {
         case nir_instr_type_deref: {
            nir_deref_instr *deref = nir_instr_as_deref(instr);
            if (nir_deref_mode_is_in_set(deref, modes)) {
               lower_explicit_io_deref(&b, deref, addr_format);
               progress = true;
            }
            break;
         }

         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            switch (intrin->intrinsic) {
            case nir_intrinsic_load_deref:
            case nir_intrinsic_store_deref:
            case nir_intrinsic_load_deref_block_intel:
            case nir_intrinsic_store_deref_block_intel:
            case nir_intrinsic_deref_atomic:
            case nir_intrinsic_deref_atomic_swap: {
               nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
               if (nir_deref_mode_is_in_set(deref, modes)) {
                  lower_explicit_io_access(&b, intrin, addr_format);
                  progress = true;
               }
               break;
            }

            case nir_intrinsic_deref_buffer_array_length: {
               nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
               if (nir_deref_mode_is_in_set(deref, modes)) {
                  lower_explicit_io_array_length(&b, intrin, addr_format);
                  progress = true;
               }
               break;
            }

            case nir_intrinsic_deref_mode_is: {
               nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
               if (nir_deref_mode_is_in_set(deref, modes)) {
                  lower_explicit_io_mode_check(&b, intrin, addr_format);
                  progress = true;
               }
               break;
            }

            case nir_intrinsic_launch_mesh_workgroups_with_payload_deref: {
               if (modes & nir_var_mem_task_payload) {
                  /* Get address and size of the payload variable. */
                  nir_deref_instr *deref = nir_src_as_deref(intrin->src[1]);
                  assert(deref->deref_type == nir_deref_type_var);
                  unsigned base = deref->var->data.explicit_location;
                  unsigned size = glsl_get_explicit_size(deref->var->type, false);

                  /* Replace the current instruction with the explicit intrinsic. */
                  nir_def *dispatch_3d = intrin->src[0].ssa;
                  b.cursor = nir_instr_remove(instr);
                  nir_launch_mesh_workgroups(&b, dispatch_3d, .base = base, .range = size);
                  progress = true;
               }

               break;
            }

            default:
               break;
            }
            break;
         }

         default:
            /* Nothing to do */
            break;
         }
      }
   }

   return nir_progress(progress, impl, nir_metadata_none);
}

/** Lower explicitly laid out I/O access to byte offset/address intrinsics
 *
 * This pass is intended to be used for any I/O which touches memory external
 * to the shader or which is directly visible to the client.  It requires that
 * all data types in the given modes have a explicit stride/offset decorations
 * to tell it exactly how to calculate the offset/address for the given load,
 * store, or atomic operation.  If the offset/stride information does not come
 * from the client explicitly (as with shared variables in GL or Vulkan),
 * nir_lower_vars_to_explicit_types() can be used to add them.
 *
 * Unlike nir_lower_io, this pass is fully capable of handling incomplete
 * pointer chains which may contain cast derefs.  It does so by walking the
 * deref chain backwards and simply replacing each deref, one at a time, with
 * the appropriate address calculation.  The pass takes a nir_address_format
 * parameter which describes how the offset or address is to be represented
 * during calculations.  By ensuring that the address is always in a
 * consistent format, pointers can safely be conjured from thin air by the
 * driver, stored to variables, passed through phis, etc.
 *
 * One exception to the simple algorithm described above is for handling
 * row-major matrices in which case we may look down one additional level of
 * the deref chain.
 *
 * Another exception is when lowering accesses using offset_shift. The goal
 * here is to generate addresses that are a right-shifted version of the
 * actual byte address and record the shift amount in the offset_shift index.
 * While we could just insert a ushr at the end of deref chains, this will
 * prevent the shift to be optimized away in many cases. Instead, we try to
 * extract the shift from the array strides and struct offsets that make up
 * the deref chain, and only insert a ushr when absolutely necessary (i.e.,
 * for casts). This means we have to walk the entire deref chain at once for
 * accesses that support offset_shift and we don't use the algorithm described
 * above.
 *
 * This pass is also capable of handling OpenCL generic pointers.  If the
 * address mode is global, it will lower any ambiguous (more than one mode)
 * access to global and pass through the deref_mode_is run-time checks as
 * addr_mode_is.  This assumes the driver has somehow mapped shared and
 * scratch memory to the global address space.  For other modes such as
 * 62bit_generic, there is an enum embedded in the address and we lower
 * ambiguous access to an if-ladder and deref_mode_is to a check against the
 * embedded enum.  If nir_lower_explicit_io is called on any shader that
 * contains generic pointers, it must either be used on all of the generic
 * modes or none.
 */
bool
nir_lower_explicit_io(nir_shader *shader, nir_variable_mode modes,
                      nir_address_format addr_format)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      if (impl && nir_lower_explicit_io_impl(impl, modes, addr_format))
         progress = true;
   }

   return progress;
}

static bool
nir_lower_vars_to_explicit_types_impl(nir_function_impl *impl,
                                      nir_variable_mode modes,
                                      glsl_type_size_align_func type_info)
{
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_deref)
            continue;

         nir_deref_instr *deref = nir_instr_as_deref(instr);
         if (!nir_deref_mode_is_in_set(deref, modes))
            continue;

         unsigned size, alignment;
         const struct glsl_type *new_type =
            glsl_get_explicit_type_for_size_align(deref->type, type_info, &size, &alignment);
         if (new_type != deref->type) {
            progress = true;
            deref->type = new_type;
         }
         if (deref->deref_type == nir_deref_type_cast) {
            /* See also glsl_type::get_explicit_type_for_size_align() */
            unsigned new_stride = align(size, alignment);
            if (new_stride != deref->cast.ptr_stride) {
               deref->cast.ptr_stride = new_stride;
               progress = true;
            }
         }
      }
   }

   return nir_progress(progress, impl,
                       nir_metadata_control_flow | nir_metadata_live_defs | nir_metadata_loop_analysis);
}

static bool
lower_vars_to_explicit(nir_shader *shader,
                       struct exec_list *vars, nir_variable_mode mode,
                       glsl_type_size_align_func type_info)
{
   bool progress = false;
   unsigned offset;
   switch (mode) {
   case nir_var_uniform:
      assert(shader->info.stage == MESA_SHADER_KERNEL);
      offset = 0;
      break;
   case nir_var_function_temp:
   case nir_var_shader_temp:
      offset = shader->scratch_size;
      break;
   case nir_var_mem_shared:
      offset = shader->info.shared_size;
      break;
   case nir_var_mem_task_payload:
      offset = shader->info.task_payload_size;
      break;
   case nir_var_mem_node_payload:
      assert(!shader->info.cs.node_payloads_size);
      offset = 0;
      break;
   case nir_var_mem_global:
      offset = shader->global_mem_size;
      break;
   case nir_var_mem_constant:
      offset = shader->constant_data_size;
      break;
   case nir_var_shader_call_data:
   case nir_var_ray_hit_attrib:
   case nir_var_mem_node_payload_in:
      offset = 0;
      break;
   default:
      UNREACHABLE("Unsupported mode");
   }
   nir_foreach_variable_in_list(var, vars) {
      if (var->data.mode != mode)
         continue;

      unsigned size, alignment;
      const struct glsl_type *explicit_type =
         glsl_get_explicit_type_for_size_align(var->type, type_info,
                                               &size, &alignment);

      if (explicit_type != var->type)
         var->type = explicit_type;

      UNUSED bool is_empty_struct =
         glsl_type_is_struct_or_ifc(explicit_type) &&
         glsl_get_length(explicit_type) == 0;

      assert(util_is_power_of_two_nonzero(alignment) || is_empty_struct ||
             glsl_type_is_cmat(glsl_without_array(explicit_type)));
      assert(util_is_power_of_two_or_zero(var->data.alignment));
      alignment = MAX2(alignment, var->data.alignment);

      var->data.driver_location = ALIGN_POT(offset, alignment);
      offset = var->data.driver_location + size;
      progress = true;
   }

   switch (mode) {
   case nir_var_uniform:
      assert(shader->info.stage == MESA_SHADER_KERNEL);
      shader->num_uniforms = offset;
      break;
   case nir_var_shader_temp:
   case nir_var_function_temp:
      shader->scratch_size = offset;
      break;
   case nir_var_mem_shared:
      shader->info.shared_size = offset;
      break;
   case nir_var_mem_task_payload:
      shader->info.task_payload_size = offset;
      break;
   case nir_var_mem_node_payload:
      shader->info.cs.node_payloads_size = offset;
      break;
   case nir_var_mem_global:
      shader->global_mem_size = offset;
      break;
   case nir_var_mem_constant:
      shader->constant_data_size = offset;
      break;
   case nir_var_shader_call_data:
   case nir_var_ray_hit_attrib:
   case nir_var_mem_node_payload_in:
      break;
   default:
      UNREACHABLE("Unsupported mode");
   }

   return progress;
}

static unsigned
nir_calculate_alignment_from_explicit_layout(const glsl_type *type,
                                             glsl_type_size_align_func type_info)
{
   unsigned size, alignment;
   glsl_get_explicit_type_for_size_align(type, type_info,
                                         &size, &alignment);
   return alignment;
}

static void
nir_assign_shared_var_locations(nir_shader *shader, glsl_type_size_align_func type_info)
{
   assert(shader->info.shared_memory_explicit_layout);

   /* Calculate region for Aliased shared memory at the beginning. */
   unsigned aliased_size = 0;
   unsigned aliased_alignment = 0;
   nir_foreach_variable_with_modes(var, shader, nir_var_mem_shared) {
      /* Per SPV_KHR_workgroup_storage_explicit_layout, if one shared variable is
       * a Block, all of them will be and Blocks are explicitly laid out.
       */
      assert(glsl_type_is_interface(var->type));

      if (var->data.aliased_shared_memory) {
         const bool align_to_stride = false;
         aliased_size = MAX2(aliased_size, glsl_get_explicit_size(var->type, align_to_stride));
         aliased_alignment = MAX2(aliased_alignment,
                                  nir_calculate_alignment_from_explicit_layout(var->type, type_info));
      }
   }

   unsigned offset = shader->info.shared_size;

   unsigned aliased_location = UINT_MAX;
   if (aliased_size) {
      aliased_location = align(offset, aliased_alignment);
      offset = aliased_location + aliased_size;
   }

   /* Allocate Blocks either at the Aliased region or after it. */
   nir_foreach_variable_with_modes(var, shader, nir_var_mem_shared) {
      if (var->data.aliased_shared_memory) {
         assert(aliased_location != UINT_MAX);
         var->data.driver_location = aliased_location;
      } else {
         const bool align_to_stride = false;
         const unsigned size = glsl_get_explicit_size(var->type, align_to_stride);
         const unsigned alignment =
            MAX2(nir_calculate_alignment_from_explicit_layout(var->type, type_info),
                 var->data.alignment);
         var->data.driver_location = align(offset, alignment);
         offset = var->data.driver_location + size;
      }
   }

   shader->info.shared_size = offset;
}

/* If nir_lower_vars_to_explicit_types is called on any shader that contains
 * generic pointers, it must either be used on all of the generic modes or
 * none.
 */
bool
nir_lower_vars_to_explicit_types(nir_shader *shader,
                                 nir_variable_mode modes,
                                 glsl_type_size_align_func type_info)
{
   /* TODO: Situations which need to be handled to support more modes:
    * - row-major matrices
    * - compact shader inputs/outputs
    * - interface types
    */
   ASSERTED nir_variable_mode supported =
      nir_var_mem_shared | nir_var_mem_global | nir_var_mem_constant |
      nir_var_shader_temp | nir_var_function_temp | nir_var_uniform |
      nir_var_shader_call_data | nir_var_ray_hit_attrib |
      nir_var_mem_task_payload | nir_var_mem_node_payload |
      nir_var_mem_node_payload_in;
   assert(!(modes & ~supported) && "unsupported");

   bool progress = false;

   if (modes & nir_var_uniform)
      progress |= lower_vars_to_explicit(shader, &shader->variables, nir_var_uniform, type_info);
   if (modes & nir_var_mem_global)
      progress |= lower_vars_to_explicit(shader, &shader->variables, nir_var_mem_global, type_info);

   if (modes & nir_var_mem_shared) {
      if (shader->info.shared_memory_explicit_layout) {
         nir_assign_shared_var_locations(shader, type_info);
         /* Types don't change, so no further lowering is needed. */
         modes &= ~nir_var_mem_shared;
      } else {
         progress |= lower_vars_to_explicit(shader, &shader->variables, nir_var_mem_shared, type_info);
      }
   }

   if (modes & nir_var_shader_temp)
      progress |= lower_vars_to_explicit(shader, &shader->variables, nir_var_shader_temp, type_info);
   if (modes & nir_var_mem_constant)
      progress |= lower_vars_to_explicit(shader, &shader->variables, nir_var_mem_constant, type_info);
   if (modes & nir_var_shader_call_data)
      progress |= lower_vars_to_explicit(shader, &shader->variables, nir_var_shader_call_data, type_info);
   if (modes & nir_var_ray_hit_attrib)
      progress |= lower_vars_to_explicit(shader, &shader->variables, nir_var_ray_hit_attrib, type_info);
   if (modes & nir_var_mem_task_payload)
      progress |= lower_vars_to_explicit(shader, &shader->variables, nir_var_mem_task_payload, type_info);
   if (modes & nir_var_mem_node_payload)
      progress |= lower_vars_to_explicit(shader, &shader->variables, nir_var_mem_node_payload, type_info);
   if (modes & nir_var_mem_node_payload_in)
      progress |= lower_vars_to_explicit(shader, &shader->variables, nir_var_mem_node_payload_in, type_info);

   if (modes) {
      nir_foreach_function_impl(impl, shader) {
         if (modes & nir_var_function_temp)
            progress |= lower_vars_to_explicit(shader, &impl->locals, nir_var_function_temp, type_info);

         progress |= nir_lower_vars_to_explicit_types_impl(impl, modes, type_info);
      }
   }

   return progress;
}

static void
write_constant(void *dst, size_t dst_size,
               const nir_constant *c, const struct glsl_type *type)
{
   if (c->is_null_constant) {
      memset(dst, 0, dst_size);
      return;
   }

   if (glsl_type_is_vector_or_scalar(type)) {
      const unsigned num_components = glsl_get_vector_elements(type);
      const unsigned bit_size = glsl_get_bit_size(type);
      if (bit_size == 1) {
         /* Booleans are special-cased to be 32-bit
          *
          * TODO: Make the native bool bit_size an option.
          */
         assert(num_components * 4 <= dst_size);
         for (unsigned i = 0; i < num_components; i++) {
            int32_t b32 = -(int)c->values[i].b;
            memcpy((char *)dst + i * 4, &b32, 4);
         }
      } else {
         assert(bit_size >= 8 && bit_size % 8 == 0);
         const unsigned byte_size = bit_size / 8;
         assert(num_components * byte_size <= dst_size);
         for (unsigned i = 0; i < num_components; i++) {
            /* Annoyingly, thanks to packed structs, we can't make any
             * assumptions about the alignment of dst.  To avoid any strange
             * issues with unaligned writes, we always use memcpy.
             */
            memcpy((char *)dst + i * byte_size, &c->values[i], byte_size);
         }
      }
   } else if (glsl_type_is_array_or_matrix(type)) {
      const unsigned array_len = glsl_get_length(type);
      const unsigned stride = glsl_get_explicit_stride(type);
      assert(stride > 0);
      const struct glsl_type *elem_type = glsl_get_array_element(type);
      for (unsigned i = 0; i < array_len; i++) {
         unsigned elem_offset = i * stride;
         assert(elem_offset < dst_size);
         write_constant((char *)dst + elem_offset, dst_size - elem_offset,
                        c->elements[i], elem_type);
      }
   } else {
      assert(glsl_type_is_struct_or_ifc(type));
      const unsigned num_fields = glsl_get_length(type);
      for (unsigned i = 0; i < num_fields; i++) {
         const int field_offset = glsl_get_struct_field_offset(type, i);
         assert(field_offset >= 0 && field_offset < dst_size);
         const struct glsl_type *field_type = glsl_get_struct_field(type, i);
         write_constant((char *)dst + field_offset, dst_size - field_offset,
                        c->elements[i], field_type);
      }
   }
}

void
nir_gather_explicit_io_initializers(nir_shader *shader,
                                    void *dst, size_t dst_size,
                                    nir_variable_mode mode)
{
   /* It doesn't really make sense to gather initializers for more than one
    * mode at a time.  If this ever becomes well-defined, we can drop the
    * assert then.
    */
   assert(util_bitcount(mode) == 1);

   nir_foreach_variable_with_modes(var, shader, mode) {
      assert(var->data.driver_location < dst_size);
      write_constant((char *)dst + var->data.driver_location,
                     dst_size - var->data.driver_location,
                     var->constant_initializer, var->type);
   }
}

/**
 * Return the numeric constant that identify a NULL pointer for each address
 * format.
 */
const nir_const_value *
nir_address_format_null_value(nir_address_format addr_format)
{
   const static nir_const_value null_values[][NIR_MAX_VEC_COMPONENTS] = {
      [nir_address_format_32bit_global] = { { 0 } },
      [nir_address_format_2x32bit_global] = { { 0 } },
      [nir_address_format_64bit_global] = { { 0 } },
      [nir_address_format_64bit_global_32bit_offset] = { { 0 } },
      [nir_address_format_64bit_bounded_global] = { { 0 } },
      [nir_address_format_32bit_index_offset] = { { .u32 = ~0 }, { .u32 = ~0 } },
      [nir_address_format_32bit_index_offset_pack64] = { { .u64 = ~0ull } },
      [nir_address_format_vec2_index_32bit_offset] = { { .u32 = ~0 }, { .u32 = ~0 }, { .u32 = ~0 } },
      [nir_address_format_32bit_offset] = { { .u32 = ~0 } },
      [nir_address_format_32bit_offset_as_64bit] = { { .u64 = ~0ull } },
      [nir_address_format_62bit_generic] = { { .u64 = 0 } },
      [nir_address_format_logical] = { { .u32 = ~0 } },
   };

   assert(addr_format < ARRAY_SIZE(null_values));
   return null_values[addr_format];
}

nir_def *
nir_build_addr_ieq(nir_builder *b, nir_def *addr0, nir_def *addr1,
                   nir_address_format addr_format)
{
   switch (addr_format) {
   case nir_address_format_32bit_global:
   case nir_address_format_2x32bit_global:
   case nir_address_format_64bit_global:
   case nir_address_format_64bit_bounded_global:
   case nir_address_format_32bit_index_offset:
   case nir_address_format_vec2_index_32bit_offset:
   case nir_address_format_32bit_offset:
   case nir_address_format_62bit_generic:
      return nir_ball_iequal(b, addr0, addr1);

   case nir_address_format_64bit_global_32bit_offset:
      return nir_ball_iequal(b, nir_channels(b, addr0, 0xb),
                             nir_channels(b, addr1, 0xb));

   case nir_address_format_32bit_offset_as_64bit:
      assert(addr0->num_components == 1 && addr1->num_components == 1);
      return nir_ieq(b, nir_u2u32(b, addr0), nir_u2u32(b, addr1));

   case nir_address_format_32bit_index_offset_pack64:
      assert(addr0->num_components == 1 && addr1->num_components == 1);
      return nir_ball_iequal(b, nir_unpack_64_2x32(b, addr0), nir_unpack_64_2x32(b, addr1));

   case nir_address_format_logical:
      UNREACHABLE("Unsupported address format");
   }

   UNREACHABLE("Invalid address format");
}

nir_def *
nir_build_addr_isub(nir_builder *b, nir_def *addr0, nir_def *addr1,
                    nir_address_format addr_format)
{
   switch (addr_format) {
   case nir_address_format_32bit_global:
   case nir_address_format_64bit_global:
   case nir_address_format_32bit_offset:
   case nir_address_format_32bit_index_offset_pack64:
   case nir_address_format_62bit_generic:
      assert(addr0->num_components == 1);
      assert(addr1->num_components == 1);
      return nir_isub(b, addr0, addr1);

   case nir_address_format_2x32bit_global:
      return nir_isub(b, addr_to_global(b, addr0, addr_format),
                      addr_to_global(b, addr1, addr_format));

   case nir_address_format_32bit_offset_as_64bit:
      assert(addr0->num_components == 1);
      assert(addr1->num_components == 1);
      return nir_u2u64(b, nir_isub(b, nir_u2u32(b, addr0), nir_u2u32(b, addr1)));

   case nir_address_format_64bit_global_32bit_offset:
   case nir_address_format_64bit_bounded_global:
      return nir_isub(b, addr_to_global(b, addr0, addr_format),
                      addr_to_global(b, addr1, addr_format));

   case nir_address_format_32bit_index_offset:
      assert(addr0->num_components == 2);
      assert(addr1->num_components == 2);
      /* Assume the same buffer index. */
      return nir_isub(b, nir_channel(b, addr0, 1), nir_channel(b, addr1, 1));

   case nir_address_format_vec2_index_32bit_offset:
      assert(addr0->num_components == 3);
      assert(addr1->num_components == 3);
      /* Assume the same buffer index. */
      return nir_isub(b, nir_channel(b, addr0, 2), nir_channel(b, addr1, 2));

   case nir_address_format_logical:
      UNREACHABLE("Unsupported address format");
   }

   UNREACHABLE("Invalid address format");
}

nir_def *
nir_build_addr_iadd(nir_builder *b, nir_def *addr,
                    nir_address_format addr_format,
                    nir_variable_mode modes,
                    nir_def *offset)
{
   assert(offset->num_components == 1);

   switch (addr_format) {
   case nir_address_format_32bit_global:
   case nir_address_format_64bit_global:
   case nir_address_format_32bit_offset:
      assert(addr->bit_size == offset->bit_size);
      assert(addr->num_components == 1);
      return nir_iadd(b, addr, offset);

   case nir_address_format_2x32bit_global: {
      assert(addr->num_components == 2);
      nir_def *lo = nir_channel(b, addr, 0);
      nir_def *hi = nir_channel(b, addr, 1);
      nir_def *res_lo = nir_iadd(b, lo, offset);
      nir_def *carry = nir_b2i32(b, nir_ult(b, res_lo, lo));
      nir_def *res_hi = nir_iadd(b, hi, carry);
      return nir_vec2(b, res_lo, res_hi);
   }

   case nir_address_format_32bit_offset_as_64bit:
      assert(addr->num_components == 1);
      assert(offset->bit_size == 32);
      return nir_u2u64(b, nir_iadd(b, nir_u2u32(b, addr), offset));

   case nir_address_format_64bit_global_32bit_offset:
   case nir_address_format_64bit_bounded_global:
      assert(addr->num_components == 4);
      assert(addr->bit_size == offset->bit_size);
      return nir_vector_insert_imm(b, addr, nir_iadd(b, nir_channel(b, addr, 3), offset), 3);

   case nir_address_format_32bit_index_offset:
      assert(addr->num_components == 2);
      assert(addr->bit_size == offset->bit_size);
      return nir_vector_insert_imm(b, addr, nir_iadd(b, nir_channel(b, addr, 1), offset), 1);

   case nir_address_format_32bit_index_offset_pack64:
      assert(addr->num_components == 1);
      assert(offset->bit_size == 32);
      return nir_pack_64_2x32_split(b,
                                    nir_iadd(b, nir_unpack_64_2x32_split_x(b, addr), offset),
                                    nir_unpack_64_2x32_split_y(b, addr));

   case nir_address_format_vec2_index_32bit_offset:
      assert(addr->num_components == 3);
      assert(offset->bit_size == 32);
      return nir_vector_insert_imm(b, addr, nir_iadd(b, nir_channel(b, addr, 2), offset), 2);

   case nir_address_format_62bit_generic:
      assert(addr->num_components == 1);
      assert(addr->bit_size == 64);
      assert(offset->bit_size == 64);
      if (!(modes & ~(nir_var_function_temp |
                      nir_var_shader_temp |
                      nir_var_mem_shared))) {
         /* If we're sure it's one of these modes, we can do an easy 32-bit
          * addition and don't need to bother with 64-bit math.
          */
         nir_def *addr32 = nir_unpack_64_2x32_split_x(b, addr);
         nir_def *type = nir_unpack_64_2x32_split_y(b, addr);
         addr32 = nir_iadd(b, addr32, nir_u2u32(b, offset));
         return nir_pack_64_2x32_split(b, addr32, type);
      } else {
         return nir_iadd(b, addr, offset);
      }

   case nir_address_format_logical:
      UNREACHABLE("Unsupported address format");
   }
   UNREACHABLE("Invalid address format");
}
