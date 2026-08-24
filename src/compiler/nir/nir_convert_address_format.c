/*
 * Copyright 2026 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"

#include "util/u_dynarray.h"

typedef struct {
   nir_variable_mode modes;
   nir_address_format from;
   nir_address_format to;
} convert_address_format_state;

static bool
has_address_format(nir_def *def, nir_address_format format)
{
   return def->bit_size == nir_address_format_bit_size(format) &&
          def->num_components == nir_address_format_num_components(format);
}

nir_def *
nir_build_convert_address_format(nir_builder *b, nir_def *addr,
                                 nir_address_format from, nir_address_format to)
{
   assert(has_address_format(addr, from));

   switch (from) {
   case nir_address_format_64bit_global:
      switch (to) {
      case nir_address_format_64bit_global_32bit_offset:
         return nir_vec4(b, nir_unpack_64_2x32_split_x(b, addr),
                         nir_unpack_64_2x32_split_y(b, addr), nir_imm_int(b, 0),
                         nir_imm_int(b, 0));

      default:
         UNREACHABLE("unsupported address format");
      }

   case nir_address_format_64bit_global_32bit_offset:
      switch (to) {
      case nir_address_format_64bit_global:
         return nir_iadd(b, nir_pack_64_2x32(b, nir_trim_vector(b, addr, 2)),
                         nir_u2u64(b, nir_channel(b, addr, 3)));

      default:
         UNREACHABLE("unsupported address format");
      }

   default:
      UNREACHABLE("unsupported address format");
   }
}

static void
convert_def(nir_builder *b, nir_def *def, convert_address_format_state *state)
{
   unsigned num_components = nir_address_format_num_components(state->to);
   unsigned bit_size = nir_address_format_bit_size(state->to);

   def->num_components = num_components;
   def->bit_size = bit_size;

   /* Check if all uses support the new address format. If not, convert them
    * back to the old format.
    *
    * This is done in two passes, so that we aren't creating a new use of
    * the def (ie. the address conversion) while we are still iterating the
    * original uses.
    */

   struct util_dynarray rewrites = UTIL_DYNARRAY_INIT;

   nir_foreach_use_safe(use, def) {
      nir_instr *instr = nir_src_use_instr(use);

      if (instr->type == nir_instr_type_deref) {
         /* Handled later. */
         continue;
      } else if (instr->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         switch (intrin->intrinsic) {
         case nir_intrinsic_store_deref:
         case nir_intrinsic_store_deref_block_intel:
         case nir_intrinsic_deref_atomic:
         case nir_intrinsic_deref_atomic_swap:
            /* These all take a deref as the first src, followed by one or
             * more non-deref args.  If a non-deref src is the use, it must
             * be converted back to the original address format.
             */
            if (&intrin->src[0] == use)
               continue;

            break;

         case nir_intrinsic_load_deref:
         case nir_intrinsic_load_deref_block_intel:
         case nir_intrinsic_deref_buffer_array_length:
         case nir_intrinsic_deref_buffer_address:
         case nir_intrinsic_deref_mode_is:
         case nir_intrinsic_launch_mesh_workgroups_with_payload_deref:
            /* These support any address format. */
            continue;

         default:
            break;
         }
      }

      util_dynarray_append(&rewrites, use);
   }

   nir_def *addr = NULL;

   /* Second pass, now that we've found all the original use's that need
    * conversion, we can safely create the address conversion (which
    * would have created a new use that _shouldn't_ be re-written if
    * we did this as part of the first loop), and re-write the needed
    * uses:
    */

   util_dynarray_foreach (&rewrites, nir_src *, usep) {
      if (!addr) {
         b->cursor = nir_after_instr(nir_def_instr(def));
         addr =
            nir_build_convert_address_format(b, def, state->to, state->from);
      }
      nir_src_rewrite(*usep, addr);
   }

   util_dynarray_fini(&rewrites);
}

static void
maybe_convert_src(nir_builder *b, nir_src *src,
                  convert_address_format_state *state)
{
   if (has_address_format(src->ssa, state->to)) {
      return;
   }

   b->cursor = nir_before_instr(nir_src_use_instr(src));
   nir_def *addr =
      nir_build_convert_address_format(b, src->ssa, state->from, state->to);
   nir_src_rewrite(src, addr);

   if (nir_src_use_instr(src)->type == nir_instr_type_alu) {
      nir_alu_src *alu_src = nir_src_as_alu_src(src);

      for (unsigned c = 0; c < addr->num_components; c++) {
         alu_src->swizzle[c] = c;
      }
   }
}

static bool
convert_deref(nir_builder *b, nir_deref_instr *deref, void *data)
{
   convert_address_format_state *state = data;

   if (!nir_deref_mode_is_in_set(deref, state->modes)) {
      return false;
   }

   if (deref->deref_type == nir_deref_type_cast) {
      maybe_convert_src(b, &deref->parent, state);
   }

   assert(deref->deref_type == nir_deref_type_var ||
          has_address_format(deref->parent.ssa, state->to));
   assert(has_address_format(&deref->def, state->from));

   unsigned from_bit_size = nir_address_format_bit_size(state->from);
   unsigned to_bit_size = nir_address_format_bit_size(state->to);

   /* Array index needs to match the pointer size so convert if necessary. */
   if ((deref->deref_type == nir_deref_type_array ||
        deref->deref_type == nir_deref_type_ptr_as_array) &&
       from_bit_size != to_bit_size) {
      b->cursor = nir_before_instr(&deref->instr);
      nir_def *index = NULL;

      if (to_bit_size == 32 && nir_src_is_const(deref->arr.index)) {
         int64_t const_index = nir_src_as_int(deref->arr.index);

         if (const_index >= INT32_MIN && const_index <= INT32_MAX) {
            index = nir_imm_int(b, const_index);
         }
      }

      if (!index) {
         index = nir_i2iN(b, deref->arr.index.ssa, to_bit_size);
      }

      nir_src_rewrite(&deref->arr.index, index);
   }

   convert_def(b, &deref->def, state);
   return true;
}

/* Convert derefs with the given modes from using the from format to using the
 * to format. All uses of converted derefs other than loads/stores will be
 * converted back to the from format.
 */
bool
nir_convert_address_format(nir_shader *shader, nir_variable_mode modes,
                           nir_address_format from, nir_address_format to)
{
   convert_address_format_state state = {
      .modes = modes,
      .from = from,
      .to = to,
   };

   return nir_shader_deref_pass(shader, convert_deref,
                                nir_metadata_control_flow, &state);
}
