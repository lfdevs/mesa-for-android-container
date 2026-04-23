/*
 * Copyright (c) 2022 Amazon.com, Inc. or its affiliates.
 * Copyright (C) 2019-2022,2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "pan_nir.h"
#include "panfrost/model/pan_model.h"

enum pipe_format
pan_varying_format(nir_alu_type t, unsigned ncomps)
{
   assert(ncomps >= 1 && ncomps <= 4);

#define VARYING_FORMAT(ntype, nsz, ptype, psz)                                 \
   {                                                                           \
      .type = nir_type_##ntype##nsz, .formats = {                              \
         PIPE_FORMAT_R##psz##_##ptype,                                         \
         PIPE_FORMAT_R##psz##G##psz##_##ptype,                                 \
         PIPE_FORMAT_R##psz##G##psz##B##psz##_##ptype,                         \
         PIPE_FORMAT_R##psz##G##psz##B##psz##A##psz##_##ptype,                 \
      }                                                                        \
   }

   static const struct {
      nir_alu_type type;
      enum pipe_format formats[4];
   } conv[] = {
      VARYING_FORMAT(float, 32, FLOAT, 32),
      VARYING_FORMAT(uint, 32, UINT, 32),
      VARYING_FORMAT(int, 32, SINT, 32),
      VARYING_FORMAT(float, 16, FLOAT, 16),
      VARYING_FORMAT(uint, 16, UINT, 16),
      VARYING_FORMAT(int, 16, SINT, 16),
   };
#undef VARYING_FORMAT

   assert(ncomps > 0 && ncomps <= ARRAY_SIZE(conv[0].formats));

   for (unsigned i = 0; i < ARRAY_SIZE(conv); i++) {
      if (conv[i].type == t)
         return conv[i].formats[ncomps - 1];
   }

   UNREACHABLE("Invalid type");
}

struct slot_info {
   nir_alu_type type;
   bool any_highp;
   unsigned count;
   unsigned index;
};

struct walk_varyings_data {
   struct slot_info *slots;
   bool trust_varying_flat_highp_types;
};

static bool
walk_varyings(UNUSED nir_builder *b, nir_instr *instr, void *data)
{
   struct walk_varyings_data *wv_data = data;
   struct slot_info *slots = wv_data->slots;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   unsigned count;
   nir_alu_type type;
   bool is_store;

   /* Only consider intrinsics that access varyings */
   switch (intr->intrinsic) {
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_view_output:
      if (b->shader->info.stage != MESA_SHADER_VERTEX)
         return false;

      count = nir_src_num_components(intr->src[0]);
      type = nir_intrinsic_src_type(intr);
      is_store = true;
      break;

   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input:
      if (b->shader->info.stage != MESA_SHADER_FRAGMENT)
         return false;

      count = intr->def.num_components;
      type = nir_intrinsic_dest_type(intr);
      is_store = false;
      break;

   default:
      return false;
   }

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

   if (sem.no_varying)
      return false;

   nir_alu_type base_type = nir_alu_type_get_base_type(type);
   unsigned size = nir_alu_type_get_type_size(type);
   assert(base_type & (nir_type_int | nir_type_uint | nir_type_float));

   bool untrusted_type = !wv_data->trust_varying_flat_highp_types &&
                         sem.location >= VARYING_SLOT_VAR0 &&
                         !sem.medium_precision &&
                         !b->shader->info.separate_shader;
   if (untrusted_type) {
      /* Don't trust the type, varying_opts might have smashed everything
       * onto floats.  Replace all flat varyings with ints and smooth varyings
       * with floats, only exception is 16-bit flat varyings that should be
       * stored/loaded as ints as the hardware cannot encode 16-bit flat ints.
       * Read docs/drivers/panfrost/varyings.rst for details.
       */
      bool is_flat = intr->intrinsic != nir_intrinsic_load_interpolated_input;
      base_type = (is_flat && size == 32) ? nir_type_uint : nir_type_float;
      type = base_type | size;
      if (is_store)
         nir_intrinsic_set_src_type(intr, type);
      else
         nir_intrinsic_set_dest_type(intr, type);
   }

   /* Count currently contains the number of components accessed by this
    * intrinsics. However, we may be accessing a fractional location,
    * indicating by the NIR component. Add that in. The final value be the
    * maximum (component + count), an upper bound on the number of
    * components possibly used.
    */
   count += nir_intrinsic_component(intr);

   /* Consider each slot separately */
   for (unsigned offset = 0; offset < sem.num_slots; ++offset) {
      unsigned location = sem.location + offset;
      unsigned index = pan_res_handle_get_index(nir_intrinsic_base(intr)) + offset;

      if (slots[location].type) {
         assert(slots[location].type == type);
         assert(slots[location].index == index);
      } else {
         slots[location].type = type;
         slots[location].index = index;
      }

      if (size == 32 && !sem.medium_precision)
         slots[location].any_highp = true;

      slots[location].count = MAX2(slots[location].count, count);
   }

   return false;
}

static bool
collect_noperspective_varyings_fs(UNUSED nir_builder *b,
                                  nir_intrinsic_instr *intr,
                                  void *data)
{
   uint32_t *noperspective_varyings = data;

   if (intr->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   if (sem.location < VARYING_SLOT_VAR0)
      return false;

   nir_intrinsic_instr *bary_instr = nir_src_as_intrinsic(intr->src[0]);
   assert(bary_instr);
   if (nir_intrinsic_interp_mode(bary_instr) == INTERP_MODE_NOPERSPECTIVE) {
      unsigned loc = sem.location - VARYING_SLOT_VAR0;
      *noperspective_varyings |= BITFIELD_RANGE(loc, sem.num_slots);
   }

   return false;
}

uint32_t
pan_nir_collect_noperspective_varyings_fs(nir_shader *s)
{
   assert(s->info.stage == MESA_SHADER_FRAGMENT);

   uint32_t noperspective_varyings = 0;

   /* Collect from variables */
   nir_foreach_shader_in_variable(var, s) {
      if (var->data.location < VARYING_SLOT_VAR0)
         continue;

      if (var->data.interpolation != INTERP_MODE_NOPERSPECTIVE)
         continue;

      unsigned loc = var->data.location - VARYING_SLOT_VAR0;
      unsigned slots = glsl_count_attribute_slots(var->type, false);
      noperspective_varyings |= BITFIELD_RANGE(loc, slots);
   }

   /* And collect from load_interpolated_input intrinsics */
   nir_shader_intrinsics_pass(s, collect_noperspective_varyings_fs,
                              nir_metadata_all,
                              (void *)&noperspective_varyings);

   return noperspective_varyings;
}

static const struct pan_varying_slot hw_varying_slots[] = {{
   .location = VARYING_SLOT_POS,
   .alu_type = nir_type_float32,
   .ncomps = 4,
   .section = PAN_VARYING_SECTION_POSITION,
   .offset = 0,
}, {
   .location = VARYING_SLOT_PSIZ,
   .alu_type = nir_type_float16,
   .ncomps = 1,
   .section = PAN_VARYING_SECTION_ATTRIBS,
   .offset = 0,
}, {
   .location = VARYING_SLOT_LAYER,
   .alu_type = nir_type_uint8,
   .ncomps = 1,
   .section = PAN_VARYING_SECTION_ATTRIBS,
   .offset = 2,
}, {
   .location = VARYING_SLOT_VIEWPORT,
   .alu_type = nir_type_uint8,
   .ncomps = 1,
   .section = PAN_VARYING_SECTION_ATTRIBS,
   .offset = 2,
}, {
   .location = VARYING_SLOT_PRIMITIVE_ID,
   .alu_type = nir_type_uint32,
   .ncomps = 1,
   .section = PAN_VARYING_SECTION_ATTRIBS,
   .offset = 12,
}};

/* On Midgard some attributes are computed on-the-fly from the drawing state,
 * those are called special and require a custom descriptor definition.
 * From v6 onwards those use the LD_VAR_SPECIAL instruction.
 * Also on Midgard, VARYING_SLOT_TEX* might be point coordinates depending on
 * the rasterizer state, if they are they should be theoretically in the special
 * section.  Since we don't know this yet we "misplace" them in the generic
 * section anyway, they won't end up in the memory layout and they'll be handled
 * by the descriptor emitter code.
 * It's not a mistake, just a "happy little accident".
 */
static const struct pan_varying_slot special_varying_slots[] = {{
   .location = VARYING_SLOT_POS,
   .alu_type = nir_type_float32,
   .ncomps = 4,
   .section = PAN_VARYING_SECTION_SPECIAL,
   .offset = 0,
}, {
   .location = VARYING_SLOT_PNTC,
   .alu_type = nir_type_float32,
   .ncomps = 1,
   .section = PAN_VARYING_SECTION_SPECIAL,
   .offset = 0,
}, {
   .location = VARYING_SLOT_FACE,
   .alu_type = nir_type_uint32,
   .ncomps = 1,
   .section = PAN_VARYING_SECTION_SPECIAL,
   .offset = 0,
}};

static struct pan_varying_slot
hw_varying_slot(unsigned arch, mesa_shader_stage stage, gl_varying_slot slot)
{
   bool vs_pos = slot == VARYING_SLOT_POS && stage == MESA_SHADER_VERTEX;
   /* pos is only special in fragment shader input, not vertex shader output */
   if (arch < 6 && !vs_pos) {
      for (unsigned i = 0; i < ARRAY_SIZE(special_varying_slots); i++) {
         if (special_varying_slots[i].location == slot)
            return special_varying_slots[i];
      }
   }
   for (unsigned i = 0; i < ARRAY_SIZE(hw_varying_slots); i++) {
      if (hw_varying_slots[i].location == slot)
         return hw_varying_slots[i];
   }
   UNREACHABLE("Invalid HW varying slot");
}

void
pan_varying_collect_formats(struct pan_varying_layout *layout, nir_shader *nir,
                            uint64_t gpu_id,
                            bool trust_varying_flat_highp_types,
                            bool lower_mediump)
{
   assert(nir->info.stage == MESA_SHADER_VERTEX ||
          nir->info.stage == MESA_SHADER_FRAGMENT);
   memset(layout, 0, sizeof(*layout));

   struct slot_info slots[64] = {0};
   struct walk_varyings_data wv_data = {
      .slots = slots,
      .trust_varying_flat_highp_types = trust_varying_flat_highp_types,
   };

   nir_shader_instructions_pass(nir, walk_varyings, nir_metadata_all, &wv_data);

   const unsigned gpu_arch = pan_arch(gpu_id);
   unsigned count = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(slots); i++) {
      if (!slots[i].type)
         continue;

      /* It's possible that something has been dead code eliminated between
       * when the driver locations were set on variables and here.  Don't
       * trust our compaction to match the driver.  Just copy over the index
       * and accept that there's a hole in the mapping.
       */
      unsigned idx = slots[i].index;
      count = MAX2(count, idx + 1);
      assert(count <= ARRAY_SIZE(layout->slots));
      assert(layout->slots[idx].alu_type == nir_type_invalid);

      if (BITFIELD64_BIT(i) & PAN_HARDWARE_VARYING_BITS) {
         layout->slots[idx] = hw_varying_slot(gpu_arch, nir->info.stage, i);
      } else {
         nir_alu_type type = nir_alu_type_get_base_type(slots[i].type);
         unsigned bit_size = nir_alu_type_get_type_size(slots[i].type);

         /* The Vulkan spec requires types to match across all uses of a
          * location but doesn't actually require RelaxedPrecision to match
          * for the whole location.  So we can only apply mediump if every use
          * of the location is mediump.
          * Don't lower mediump integers, it has no measured impact and causes
          * lots of bugs due to gallium shenanigans.
          * Also allow the client to remove mediump lowering and keep the
          * original types
          */
         bool can_lower_size = lower_mediump &&
                               bit_size == 32 &&
                               type == nir_type_float &&
                               !slots[i].any_highp;
         if (can_lower_size)
            bit_size = 16;

         layout->slots[idx] = (struct pan_varying_slot){
            .location = i,
            .alu_type = type | bit_size,
            .ncomps = slots[i].count,
            .section = PAN_VARYING_SECTION_GENERIC,
            /* Don't know the offset yet */
            .offset = -1,
         };
      }
   }
   layout->count = count;
   layout->generic_size_B = 0;
   layout->known |= PAN_VARYING_FORMAT_KNOWN;
}

void
pan_build_varying_layout_compact(struct pan_varying_layout *layout,
                                 nir_shader *nir, uint64_t gpu_id)
{
   pan_varying_layout_require_format(layout);

   const unsigned gpu_arch = pan_arch(gpu_id);
   unsigned generic_size_B = 0;
   for (unsigned i = 0; i < layout->count; i++) {
      struct pan_varying_slot *slot = &layout->slots[i];
      if (pan_varying_slot_is_empty(slot))
         continue;

      if (slot->section != PAN_VARYING_SECTION_GENERIC) {
         ASSERTED const struct pan_varying_slot hw_slot =
            hw_varying_slot(gpu_arch, nir->info.stage, slot->location);

         assert(memcmp(slot, &hw_slot, sizeof(*slot)) == 0);
      } else {
         unsigned bit_size = nir_alu_type_get_type_size(slot->alu_type);

         unsigned size = slot->ncomps * (bit_size / 8);
         unsigned alignment = util_next_power_of_two(size);
         unsigned offset = align(generic_size_B, alignment);
         generic_size_B = offset + size;

         assert(slot->offset == -1);
         assert(offset < 4096);
         slot->offset = offset;
      }
   }
   layout->generic_size_B = generic_size_B;
   layout->known |= PAN_VARYING_LAYOUT_KNOWN;
}
