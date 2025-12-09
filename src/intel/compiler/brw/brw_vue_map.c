/*
 * Copyright © 2011 Intel Corporation
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

/**
 * @file
 *
 * This file computes the "VUE map" for a (non-fragment) shader stage, which
 * describes the layout of its output varyings.  The VUE map is used to match
 * outputs from one stage with the inputs of the next.
 *
 * Largely, varyings can be placed however we like - producers/consumers simply
 * have to agree on the layout.  However, there is also a "VUE Header" that
 * prescribes a fixed-layout for items that interact with fixed function
 * hardware, such as the clipper and rasterizer.
 *
 * Authors:
 *   Paul Berry <stereotype441@gmail.com>
 *   Chris Forbes <chrisf@ijw.co.nz>
 *   Eric Anholt <eric@anholt.net>
 */


#include "brw_compiler.h"
#include "dev/intel_debug.h"
#include "brw_nir.h"

static unsigned
get_var_slots(mesa_shader_stage stage, const nir_variable *var)
{
   const struct glsl_type *type = var->type;

   if (nir_is_arrayed_io(var, stage)) {
      assert(glsl_type_is_array(type));
      type = glsl_get_array_element(type);
   }

   /* EXT_mesh_shader: PRIMITIVE_INDICES is a flat array, not a proper arrayed
    * output, as opposed to D3D-style mesh shaders where it's addressed by the
    * primitive index. Prevent assigning several slots to primitive indices,
    * to avoid some issues.
    */
   if (stage == MESA_SHADER_MESH &&
       var->data.location == VARYING_SLOT_PRIMITIVE_INDICES &&
       !nir_is_arrayed_io(var, stage))
      return 1;

   return glsl_count_vec4_slots(type, false, var->data.bindless);
}

void
brw_compute_per_primitive_map(int *out_per_primitive_map,
                              uint32_t *out_per_primitive_stride,
                              uint32_t *out_first_offset,
                              uint32_t base_offset,
                              nir_shader *nir,
                              uint32_t variables_mode,
                              uint64_t slots_valid,
                              bool separate_shader)
{
   memset(out_per_primitive_map, -1, sizeof(*out_per_primitive_map) * VARYING_SLOT_MAX);
   *out_per_primitive_stride = base_offset;
   *out_first_offset = UINT32_MAX;

   const uint64_t count_indices_bits =
      VARYING_BIT_PRIMITIVE_COUNT |
      VARYING_BIT_PRIMITIVE_INDICES;
   const uint64_t per_primitive_header_bits =
      VARYING_BIT_PRIMITIVE_SHADING_RATE |
      VARYING_BIT_LAYER |
      VARYING_BIT_VIEWPORT |
      VARYING_BIT_CULL_PRIMITIVE;
   const uint64_t per_primitive_outputs_written =
      slots_valid & ~(count_indices_bits | per_primitive_header_bits);

   *out_first_offset = base_offset;

   /* We put each variable in its own 16B slot. Technically we could do a lot
    * better by allocating the space needed for the variable since the data is
    * constant and not interpolated for the fragment shader. Unfortunately the
    * backend treats those values similarly to vertex attributes and making
    * that change would require a pretty large change in the backend. Let's do
    * this later.
    */

   /* Lay out builtins first */
   const uint64_t builtins =
      per_primitive_outputs_written & BITFIELD64_MASK(VARYING_SLOT_VAR0);
   u_foreach_bit64(location, builtins) {
      assert(out_per_primitive_map[location] == -1);

      out_per_primitive_map[location] = *out_per_primitive_stride;
      *out_per_primitive_stride += 16;
   }

   uint32_t generics_offset = *out_per_primitive_stride;

   /* Lay out generics */
   const uint64_t generics =
      per_primitive_outputs_written & ~BITFIELD64_MASK(VARYING_SLOT_VAR0);
   const int first_generic_output = ffsll(generics) - 1;
   u_foreach_bit64(location, generics) {
      assert(out_per_primitive_map[location] == -1);
      if (!separate_shader) {
         /* Just append the location at the back */
         out_per_primitive_map[location] = *out_per_primitive_stride;
      } else {
         assert(location >= VARYING_SLOT_VAR0);
         /* Each location has its fixed spot */
         out_per_primitive_map[location] = generics_offset +
            16 * (location - first_generic_output);
      }

      *out_per_primitive_stride =
         MAX2(out_per_primitive_map[location] + 16,
              *out_per_primitive_stride);

      *out_first_offset = MIN2(out_per_primitive_map[location],
                               *out_first_offset);
   }

   *out_first_offset = *out_first_offset == UINT32_MAX ? 0 :
      ROUND_DOWN_TO(*out_first_offset, 32);
}

static inline void
assign_vue_slot(struct intel_vue_map *vue_map, int varying, int slot)
{
   /* Make sure this varying hasn't been assigned a slot already */
   assert (vue_map->varying_to_slot[varying] == -1);

   vue_map->varying_to_slot[varying] = slot;
   vue_map->slot_to_varying[slot] = varying;
}

/**
 * Compute the VUE map for a shader stage.
 */
void
brw_compute_vue_map(const struct intel_device_info *devinfo,
                    struct intel_vue_map *vue_map,
                    uint64_t slots_valid,
                    enum intel_vue_layout layout,
                    uint32_t pos_slots)
{
   vue_map->slots_valid = slots_valid;
   vue_map->layout = layout;

   if (layout != INTEL_VUE_LAYOUT_FIXED) {
      /* In SSO mode, we don't know whether the adjacent stage will
       * read/write gl_ClipDistance, which has a fixed slot location.
       * We have to assume the worst and reserve a slot for it, or else
       * the rest of our varyings will be off by a slot.
       *
       * Note that we don't have to worry about COL/BFC, as those built-in
       * variables only exist in legacy GL, which only supports VS and FS.
       */
      slots_valid |= VARYING_BIT_CLIP_DIST0;
      slots_valid |= VARYING_BIT_CLIP_DIST1;
   }

   /* gl_Layer, gl_ViewportIndex & gl_PrimitiveShadingRateEXT don't get their
    * own varying slots -- they are stored in the first VUE slot
    * (VARYING_SLOT_PSIZ).
    */
   slots_valid &= ~(VARYING_BIT_LAYER | VARYING_BIT_VIEWPORT | VARYING_BIT_PRIMITIVE_SHADING_RATE);

   /* gl_FrontFace is provided somewhere else in the FS thread payload, it's
    * never in the VUE.
    */
   slots_valid &= ~VARYING_BIT_FACE;

   /* Make sure that the values we store in vue_map->varying_to_slot and
    * vue_map->slot_to_varying won't overflow the signed chars that are used
    * to store them.  Note that since vue_map->slot_to_varying sometimes holds
    * values equal to BRW_VARYING_SLOT_COUNT, we need to ensure that
    * BRW_VARYING_SLOT_COUNT is <= 127, not 128.
    */
   STATIC_ASSERT(BRW_VARYING_SLOT_COUNT <= 127);

   for (int i = 0; i < BRW_VARYING_SLOT_COUNT; ++i) {
      vue_map->varying_to_slot[i] = -1;
      vue_map->slot_to_varying[i] = BRW_VARYING_SLOT_PAD;
   }

   int slot = 0;

   /* VUE header: format depends on chip generation and whether clipping is
    * enabled.
    *
    * See the Sandybridge PRM, Volume 2 Part 1, section 1.5.1 (page 30),
    * "Vertex URB Entry (VUE) Formats" which describes the VUE header layout.
    *
    * There are 8 or 16 DWs (D0-D15) in VUE header on Sandybridge:
    * dword 0-3 of the header is shading rate, indices, point width, clip flags.
    * dword 4-7 is the 4D space position
    * dword 8-15 of the vertex header is the user clip distance if
    * enabled.
    * dword 8-11 or 16-19 is the first vertex element data we fill.
    */
   assign_vue_slot(vue_map, VARYING_SLOT_PSIZ, slot++);
   assign_vue_slot(vue_map, VARYING_SLOT_POS, slot++);

   /* When using Primitive Replication, multiple slots are used for storing
    * positions for each view.
    */
   assert(pos_slots >= 1);
   if (pos_slots > 1) {
      for (int i = 1; i < pos_slots; i++) {
         vue_map->slot_to_varying[slot++] = VARYING_SLOT_POS;
      }
   }

   if (slots_valid & VARYING_BIT_CLIP_DIST0)
      assign_vue_slot(vue_map, VARYING_SLOT_CLIP_DIST0, slot++);
   if (slots_valid & VARYING_BIT_CLIP_DIST1)
      assign_vue_slot(vue_map, VARYING_SLOT_CLIP_DIST1, slot++);

   /* Vertex URB Formats table says: "Vertex Header shall be padded at the
    * end so that the header ends on a 32-byte boundary".
    */
   slot += slot % 2;

   /* front and back colors need to be consecutive so that we can use
    * ATTRIBUTE_SWIZZLE_INPUTATTR_FACING to swizzle them when doing
    * two-sided color.
    */
   if (slots_valid & VARYING_BIT_COL0)
      assign_vue_slot(vue_map, VARYING_SLOT_COL0, slot++);
   if (slots_valid & VARYING_BIT_BFC0)
      assign_vue_slot(vue_map, VARYING_SLOT_BFC0, slot++);
   if (slots_valid & VARYING_BIT_COL1)
      assign_vue_slot(vue_map, VARYING_SLOT_COL1, slot++);
   if (slots_valid & VARYING_BIT_BFC1)
      assign_vue_slot(vue_map, VARYING_SLOT_BFC1, slot++);

   /* The hardware doesn't care about the rest of the vertex outputs, so we
    * can assign them however we like.  For normal programs, we simply assign
    * them contiguously.
    *
    * We generally don't need to assign a slot for VARYING_SLOT_CLIP_VERTEX,
    * since it's encoded as the clip distances by emit_clip_distances().
    * However, it may be output by transform feedback, and we'd rather not
    * recompute state when TF changes, so we just always include it.
    */
   if (layout != INTEL_VUE_LAYOUT_SEPARATE_MESH) {
      vue_map->builtins_slot_offset = slot;
      const uint64_t builtins = slots_valid & BITFIELD64_MASK(VARYING_SLOT_VAR0);
      u_foreach_bit64(varying, builtins) {
         /* Already assigned above? */
         if (vue_map->varying_to_slot[varying] != -1)
         continue;
         assign_vue_slot(vue_map, varying, slot++);
      }
   }

   const int first_generic_slot = slot;
   const uint64_t generics =
      (layout != INTEL_VUE_LAYOUT_FIXED ?
       BITFIELD64_MASK(util_last_bit64(slots_valid)) :
       slots_valid) &
      ~BITFIELD64_MASK(VARYING_SLOT_VAR0);
   u_foreach_bit64(varying, generics) {
      if (layout != INTEL_VUE_LAYOUT_FIXED) {
         slot = first_generic_slot + varying - VARYING_SLOT_VAR0;
      }
      assign_vue_slot(vue_map, varying, slot++);
   }

   if (layout == INTEL_VUE_LAYOUT_SEPARATE_MESH) {
      vue_map->builtins_slot_offset = slot;
      const uint64_t builtins = slots_valid & BITFIELD64_MASK(VARYING_SLOT_VAR0);
      u_foreach_bit64(varying, builtins) {
         /* Already assigned above? */
         if (vue_map->varying_to_slot[varying] != -1)
            continue;
         assign_vue_slot(vue_map, varying, slot++);
      }
   }

   vue_map->num_slots = slot;
   vue_map->num_pos_slots = pos_slots;
   vue_map->num_per_vertex_slots = 0;
   vue_map->num_per_patch_slots = 0;
}

/**
 * Compute the VUE map for tessellation control shader outputs and
 * tessellation evaluation shader inputs.
 */
void
brw_compute_tess_vue_map(struct intel_vue_map *vue_map,
                         uint64_t vertex_slots,
                         uint32_t patch_slots,
                         bool separate)
{
   /* I don't think anything actually uses this... */
   vue_map->slots_valid = vertex_slots;
   vue_map->layout = separate ? INTEL_VUE_LAYOUT_SEPARATE : INTEL_VUE_LAYOUT_FIXED;

   vertex_slots &= ~(VARYING_BIT_TESS_LEVEL_OUTER |
                     VARYING_BIT_TESS_LEVEL_INNER);

   if (separate) {
      vertex_slots |= VARYING_BIT_POS;
      vertex_slots |= VARYING_BIT_CLIP_DIST0;
      vertex_slots |= VARYING_BIT_CLIP_DIST1;
   }

   /* Make sure that the values we store in vue_map->varying_to_slot and
    * vue_map->slot_to_varying won't overflow the signed chars that are used
    * to store them.  Note that since vue_map->slot_to_varying sometimes holds
    * values equal to VARYING_SLOT_TESS_MAX , we need to ensure that
    * VARYING_SLOT_TESS_MAX is <= 127, not 128.
    */
   STATIC_ASSERT(VARYING_SLOT_TESS_MAX <= 127);

   for (int i = 0; i < VARYING_SLOT_TESS_MAX ; ++i) {
      vue_map->varying_to_slot[i] = -1;
      vue_map->slot_to_varying[i] = BRW_VARYING_SLOT_PAD;
   }

   int slot = 0;

   /* The first 8 DWords are reserved for the "Patch Header".
    *
    * VARYING_SLOT_TESS_LEVEL_OUTER / INNER live here, but the exact layout
    * depends on the domain type.  They might not be in slots 0 and 1 as
    * described here, but pretending they're separate allows us to uniquely
    * identify them by distinct slot locations.
    */
   assign_vue_slot(vue_map, VARYING_SLOT_TESS_LEVEL_OUTER, slot++);
   assign_vue_slot(vue_map, VARYING_SLOT_TESS_LEVEL_INNER, slot++);

   /* first assign per-patch varyings */
   const int first_patch_slot = slot;
   u_foreach_bit64(patch, patch_slots) {
      if (separate) {
         slot = first_patch_slot + patch;
         assign_vue_slot(vue_map, VARYING_SLOT_PATCH0 + patch, slot++);
      } else {
         assign_vue_slot(vue_map, VARYING_SLOT_PATCH0 + patch, slot++);
      }
   }

   /* apparently, including the patch header... */
   vue_map->num_per_patch_slots = slot;

   /* We have 2 blocks of varyings, generics for the app & builtins. Builtins
    * have to match according to the Vulkan spec, but generics don't have to.
    *
    * In fixed layout we can put all the builtins together at the beginning
    * and pack the generics after that.
    *
    * In separate layout we lay out the generics in a position independent way
    * at the beginning first then followed by the builtins. An offset will be
    * pushed into the tessellation evaluation shader to indicate where to find
    * the builtins.
    */
   const int first_vertex_slot = slot;

   const uint64_t generics =
      (separate ? BITFIELD64_MASK(util_last_bit64(vertex_slots)) : vertex_slots) &
      ~BITFIELD64_MASK(VARYING_SLOT_VAR0);
   const uint64_t builtins = vertex_slots & BITFIELD64_MASK(VARYING_SLOT_VAR0);

   /* TODO: consider only using a single layout (just allowing position
    * independent generics), since there should be no difference in VUE size.
    */
   if (separate) {
      const int first_generics_slot = slot;
      u_foreach_bit64(varying, generics) {
         slot = first_generics_slot + varying - VARYING_SLOT_VAR0;
         assign_vue_slot(vue_map, varying, slot++);
      }
      vue_map->builtins_slot_offset = slot;
      u_foreach_bit64(varying, builtins)
         assign_vue_slot(vue_map, varying, slot++);
   } else {
      vue_map->builtins_slot_offset = slot;
      u_foreach_bit64(varying, builtins) {
         assign_vue_slot(vue_map, varying, slot++);
      }
      u_foreach_bit64(varying, generics)
         assign_vue_slot(vue_map, varying, slot++);
   }

   vue_map->num_per_vertex_slots = slot - first_vertex_slot;
   vue_map->num_pos_slots = 0;
   vue_map->num_slots = slot;
}

static const char *
varying_name(brw_varying_slot slot, mesa_shader_stage stage)
{
   assume(slot < BRW_VARYING_SLOT_COUNT);

   if (slot < VARYING_SLOT_MAX)
      return gl_varying_slot_name_for_stage((gl_varying_slot)slot, stage);

   static const char *brw_names[] = {
      [BRW_VARYING_SLOT_PAD - VARYING_SLOT_MAX] = "BRW_VARYING_SLOT_PAD",
   };

   return brw_names[slot - VARYING_SLOT_MAX];
}

void
brw_print_vue_map(FILE *fp, const struct intel_vue_map *vue_map,
                  mesa_shader_stage stage)
{
   const char *layout_name =
      vue_map->layout == INTEL_VUE_LAYOUT_FIXED ? "fixed" :
      vue_map->layout == INTEL_VUE_LAYOUT_SEPARATE ? "separate" :
      "separate-mesh";

   if (vue_map->num_per_vertex_slots > 0 || vue_map->num_per_patch_slots > 0) {
      fprintf(fp, "PUE map (%d slots, %d/patch, %d/vertex, %s)\n",
              vue_map->num_slots,
              vue_map->num_per_patch_slots,
              vue_map->num_per_vertex_slots,
              layout_name);
      for (int i = 0; i < vue_map->num_slots; i++) {
         if (vue_map->slot_to_varying[i] >= VARYING_SLOT_PATCH0) {
            fprintf(fp, "  [%02d] VARYING_SLOT_PATCH%d\n", i,
                    vue_map->slot_to_varying[i] - VARYING_SLOT_PATCH0);
         } else {
            fprintf(fp, "  [%02d] %s\n", i,
                    varying_name(vue_map->slot_to_varying[i], stage));
         }
      }
   } else {
      fprintf(fp, "%s VUE map (%d slots, %s)\n",
              mesa_shader_stage_name(stage), vue_map->num_slots, layout_name);
      for (int i = 0; i < vue_map->num_slots; i++) {
         fprintf(fp, "  [%02d] %s\n", i,
                 varying_name(vue_map->slot_to_varying[i], stage));
      }
   }
   fprintf(fp, "\n");
}
