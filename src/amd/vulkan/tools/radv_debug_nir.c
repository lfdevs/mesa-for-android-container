/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_debug_nir.h"
#include "radv_debug.h"
#include "radv_device.h"
#include "radv_physical_device.h"

#include "util/strndup.h"
#include "util/u_printf.h"

#include "nir.h"
#include "nir_builder.h"

VkResult
radv_printf_data_init(struct radv_device *device)
{
   struct radv_printf_data *printf = &device->debug_nir.printf;

   printf->formats = UTIL_DYNARRAY_INIT;

   printf->buffer_size = debug_get_num_option("RADV_PRINTF_BUFFER_SIZE", 0);
   if (printf->buffer_size < sizeof(struct radv_printf_buffer_header))
      return VK_SUCCESS;

   VkResult result =
      radv_backed_buffer_init(device, &printf->buffer, printf->buffer_size, radv_memory_type_visible_vram,
                              VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT, true);
   if (result != VK_SUCCESS)
      return result;

   printf->buffer_addr = radv_backed_buffer_get_va(device, &printf->buffer);

   struct radv_printf_buffer_header *header = printf->buffer.map;
   header->offset = sizeof(struct radv_printf_buffer_header);
   header->size = printf->buffer_size;

   return VK_SUCCESS;
}

void
radv_printf_data_finish(struct radv_device *device)
{
   struct radv_printf_data *printf = &device->debug_nir.printf;

   radv_backed_buffer_finish(device, &printf->buffer);

   util_dynarray_foreach (&printf->formats, struct radv_printf_format, format)
      free(format->string);

   util_dynarray_fini(&printf->formats);
}

void
radv_build_printf_args(struct radv_debug_nir *debug_nir, nir_builder *b, const char *format_string, uint32_t argc,
                       nir_def **in_args)
{
   struct radv_printf_data *printf = &debug_nir->printf;

   if (!printf->buffer_addr)
      return;

   struct radv_printf_format format = {0};
   format.string = strdup(format_string);
   if (!format.string)
      return;

   uint32_t format_index = util_dynarray_num_elements(&printf->formats, struct radv_printf_format);

   if (b->shader->info.stage == MESA_SHADER_FRAGMENT)
      nir_push_if(b, nir_inot(b, nir_is_helper_invocation(b, 1)));

   nir_def *size = nir_imm_int(b, 4);

   nir_def **args = malloc(argc * sizeof(nir_def *));
   nir_def **strides = malloc(argc * sizeof(nir_def *));

   nir_def *ballot = nir_ballot(b, 1, 64, nir_imm_true(b));
   nir_def *active_invocation_count = nir_bit_count(b, ballot);

   for (uint32_t i = 0; i < argc; i++) {
      nir_def *arg = in_args[i];
      bool divergent = arg->divergent;

      if (arg->bit_size == 1)
         arg = nir_b2i32(b, arg);

      args[i] = arg;

      uint32_t arg_size = arg->bit_size == 1 ? 32 : arg->bit_size / 8;
      format.element_sizes[i] = arg_size;

      if (divergent) {
         strides[i] = nir_imul_imm(b, active_invocation_count, arg_size);
         format.divergence_mask |= BITFIELD_BIT(i);
      } else {
         strides[i] = nir_imm_int(b, arg_size);
      }

      size = nir_iadd(b, size, strides[i]);
   }

   nir_def *offset;
   nir_def *undef;

   nir_push_if(b, nir_elect(b, 1));
   {
      offset = nir_global_atomic(
         b, 32, nir_imm_int64(b, printf->buffer_addr + offsetof(struct radv_printf_buffer_header, offset)), size,
         .atomic_op = nir_atomic_op_iadd);
   }
   nir_push_else(b, NULL);
   {
      undef = nir_undef(b, 1, 32);
   }
   nir_pop_if(b, NULL);

   offset = nir_read_first_invocation(b, nir_if_phi(b, offset, undef));

   nir_def *buffer_size = nir_load_global(
      b, 1, 32, nir_imm_int64(b, printf->buffer_addr + offsetof(struct radv_printf_buffer_header, size)));

   nir_push_if(b, nir_ige(b, buffer_size, nir_iadd(b, offset, size)));
   {
      nir_def *addr = nir_iadd_imm(b, nir_u2u64(b, offset), printf->buffer_addr);

      /* header */
      nir_store_global(b, nir_ior_imm(b, active_invocation_count, format_index << 16), addr);
      addr = nir_iadd_imm(b, addr, 4);

      for (uint32_t i = 0; i < argc; i++) {
         nir_def *arg = args[i];

         if (arg->divergent) {
            nir_def *invocation_index = nir_mbcnt_amd(b, ballot, nir_imm_int(b, 0));
            nir_store_global(
               b, arg, nir_iadd(b, addr, nir_u2u64(b, nir_imul_imm(b, invocation_index, format.element_sizes[i]))));
         } else {
            nir_store_global(b, arg, addr, );
         }

         addr = nir_iadd(b, addr, nir_u2u64(b, strides[i]));
      }
   }
   nir_pop_if(b, NULL);

   if (b->shader->info.stage == MESA_SHADER_FRAGMENT)
      nir_pop_if(b, NULL);

   free(args);
   free(strides);

   util_dynarray_append(&printf->formats, format);
}

void
radv_build_printf(nir_builder *b, nir_def *cond, const char *format_string, ...)
{
   va_list arg_list;
   va_start(arg_list, format_string);

   uint32_t num_args = 0;
   for (uint32_t i = 0; i < strlen(format_string); i++)
      if (format_string[i] == '%')
         num_args++;

   nir_def **args = malloc(num_args * sizeof(nir_def *));

   for (uint32_t i = 0; i < num_args; i++)
      args[i] = va_arg(arg_list, nir_def *);

   va_end(arg_list);

   b->shader->info.uses_printf = true;
   b->shader->printf_info_count++;
   b->shader->printf_info = reralloc(b->shader, b->shader->printf_info, u_printf_info, b->shader->printf_info_count);

   u_printf_info *info = &b->shader->printf_info[b->shader->printf_info_count - 1];

   *info = (u_printf_info){
      .arg_sizes = ralloc_array(b->shader, unsigned, num_args),
      .num_args = num_args,
      .strings = ralloc_strdup(b->shader, format_string),
      .string_size = strlen(format_string) + 1,
   };

   uint32_t info_index = b->shader->printf_info_count;

   glsl_struct_field *fields = NULL;
   nir_def *printf_src;

   if (num_args) {
      fields = calloc(num_args, sizeof(glsl_struct_field));

      for (uint32_t i = 0; i < num_args; i++) {
         nir_def *arg = args[i];

         fields[i].type = glsl_intN_t_type(arg->bit_size);
         fields[i].name = "";

         info->arg_sizes[i] = arg->bit_size / 8;
      }

      nir_variable *packed_args =
         nir_local_variable_create(b->impl, glsl_struct_type(fields, num_args, "packed_args", false), "packed_args");
      nir_deref_instr *var_deref = nir_build_deref_var(b, packed_args);

      for (uint32_t i = 0; i < num_args; i++) {
         nir_def *arg = args[i];
         nir_deref_instr *arg_deref = nir_build_deref_struct(b, var_deref, i);
         nir_store_deref(b, arg_deref, arg, BITFIELD_MASK(NIR_MAX_VEC_COMPONENTS));
      }

      printf_src = &var_deref->def;
   } else {
      printf_src = nir_undef(b, 1, 32);
   }

   if (cond)
      nir_push_if(b, cond);

   nir_printf(b, printf_src, .fmt_idx = info_index);

   if (cond)
      nir_pop_if(b, NULL);

   free(fields);
   free(args);
}

void
radv_dump_printf_data(struct radv_device *device, FILE *out, bool wait_idle)
{
   struct radv_printf_data *printf = &device->debug_nir.printf;

   if (!printf->buffer.map)
      return;

   if (wait_idle)
      device->vk.dispatch_table.DeviceWaitIdle(radv_device_to_handle(device));

   struct radv_printf_buffer_header *header = printf->buffer.map;
   uint8_t *data = printf->buffer.map;

   for (uint32_t offset = sizeof(struct radv_printf_buffer_header); offset < header->offset;) {
      uint32_t printf_header = *(uint32_t *)&data[offset];
      offset += sizeof(uint32_t);

      uint32_t format_index = printf_header >> 16;
      struct radv_printf_format *printf_format =
         util_dynarray_element(&printf->formats, struct radv_printf_format, format_index);

      uint32_t invocation_count = printf_header & 0xFFFF;

      uint32_t num_args = 0;
      for (uint32_t i = 0; i < strlen(printf_format->string); i++)
         if (printf_format->string[i] == '%')
            num_args++;

      char *format = printf_format->string;

      for (uint32_t i = 0; i <= num_args; i++) {
         size_t spec_pos = util_printf_next_spec_pos(format, 0);

         if (spec_pos == -1) {
            fprintf(out, "%s", format);
            continue;
         }

         const char *token = util_printf_prev_tok(&format[spec_pos]);
         char *next_format = &format[spec_pos + 1];

         /* print the part before the format token */
         if (token != format)
            fwrite(format, token - format, 1, out);

         char *print_str = strndup(token, next_format - token);
         /* rebase spec_pos so we can use it with print_str */
         spec_pos += format - token;

         size_t element_size = printf_format->element_sizes[i];
         bool is_float = strpbrk(print_str, "fFeEgGaA") != NULL;

         uint32_t lane_count = (printf_format->divergence_mask & BITFIELD_BIT(i)) ? invocation_count : 1;
         for (uint32_t lane = 0; lane < lane_count; lane++) {
            switch (element_size) {
            case 1: {
               uint8_t v;
               memcpy(&v, &data[offset], element_size);
               fprintf(out, print_str, v);
               break;
            }
            case 2: {
               uint16_t v;
               memcpy(&v, &data[offset], element_size);
               fprintf(out, print_str, v);
               break;
            }
            case 4: {
               if (is_float) {
                  float v;
                  memcpy(&v, &data[offset], element_size);
                  fprintf(out, print_str, v);
               } else {
                  uint32_t v;
                  memcpy(&v, &data[offset], element_size);
                  fprintf(out, print_str, v);
               }
               break;
            }
            case 8: {
               if (is_float) {
                  double v;
                  memcpy(&v, &data[offset], element_size);
                  fprintf(out, print_str, v);
               } else {
                  uint64_t v;
                  memcpy(&v, &data[offset], element_size);
                  fprintf(out, print_str, v);
               }
               break;
            }
            default:
               UNREACHABLE("Unsupported data type");
            }

            if (lane != lane_count - 1)
               fprintf(out, " ");

            offset += element_size;
         }

         /* rebase format */
         format = next_format;
         free(print_str);
      }
   }

   fflush(out);

   header->offset = sizeof(struct radv_printf_buffer_header);
}

#define RADV_VA_VALIDATION_BITS              40
#define RADV_VA_VALIDATION_BIT_COUNT         (1ull << RADV_VA_VALIDATION_BITS)
#define RADV_VA_VALIDATION_GRANULARITY_BYTES 4096

VkResult
radv_init_va_validation(struct radv_device *device)
{
   struct radv_valid_va_data *valid_va = &device->debug_nir.valid_va;

   uint64_t size = RADV_VA_VALIDATION_BIT_COUNT / RADV_VA_VALIDATION_GRANULARITY_BYTES / 8;

   VkResult result =
      radv_backed_buffer_init(device, &valid_va->buffer, size, radv_memory_type_visible_vram,
                              VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT, true);
   if (result != VK_SUCCESS)
      return result;

   valid_va->vas = valid_va->buffer.map;
   memset(valid_va->vas, 0, size);

   valid_va->buffer_addr = radv_backed_buffer_get_va(device, &valid_va->buffer);

   return VK_SUCCESS;
}

void
radv_finish_va_validation(struct radv_device *device)
{
   struct radv_valid_va_data *valid_va = &device->debug_nir.valid_va;

   valid_va->vas = NULL;

   radv_backed_buffer_finish(device, &valid_va->buffer);
}

void
radv_va_validation_update_page(struct radv_device *device, uint64_t va, uint64_t size, bool valid)
{
   struct radv_valid_va_data *valid_va = &device->debug_nir.valid_va;

   if (!valid_va->vas)
      return;

   struct radv_physical_device *pdev = radv_device_physical(device);
   assert(!(((va >> 32) & ~pdev->info.address32_hi) >> (RADV_VA_VALIDATION_BITS - 32)));

   uint64_t start = (va & BITFIELD64_MASK(RADV_VA_VALIDATION_BITS)) / RADV_VA_VALIDATION_GRANULARITY_BYTES;
   uint64_t end = start + size / RADV_VA_VALIDATION_GRANULARITY_BYTES;
   assert(end > 0);
   assert(end <= RADV_VA_VALIDATION_BIT_COUNT);

   if (valid)
      BITSET_SET_RANGE(valid_va->vas, start, end - 1);
   else
      BITSET_CLEAR_RANGE(valid_va->vas, start, end - 1);
}

nir_def *
radv_build_is_valid_va(struct radv_debug_nir *debug_nir, nir_builder *b, nir_def *addr)
{
   struct radv_valid_va_data *valid_va = &debug_nir->valid_va;

   if (!valid_va->buffer_addr)
      return NULL;

   nir_def *masked_addr = nir_iand_imm(b, addr, BITFIELD64_MASK(RADV_VA_VALIDATION_BITS));
   nir_def *then_valid;
   nir_def *else_valid;
   nir_push_if(b, nir_ult_imm(b, masked_addr, RADV_VA_VALIDATION_BIT_COUNT * RADV_VA_VALIDATION_GRANULARITY_BYTES));
   {
      nir_def *index = nir_u2u32(b, nir_udiv_imm(b, masked_addr, RADV_VA_VALIDATION_GRANULARITY_BYTES));
      nir_def *offset = nir_imul_imm(b, nir_udiv_imm(b, index, 32), 4);
      nir_def *dword =
         nir_load_global(b, 1, 32, nir_iadd_imm(b, nir_u2u64(b, offset), valid_va->buffer_addr), .align_mul = 4);
      index = nir_umod_imm(b, index, 32);
      then_valid = nir_bitnz(b, dword, index);
   }
   nir_push_else(b, NULL);
   {
      else_valid = nir_imm_false(b);
   }
   nir_pop_if(b, NULL);
   nir_def *valid = nir_if_phi(b, then_valid, else_valid);

   radv_build_printf(b, nir_inot(b, valid), "radv: Invalid VA %lx\n", addr);

   return valid;
}
