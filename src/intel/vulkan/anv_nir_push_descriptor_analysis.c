/*
 * Copyright © 2022 Intel Corporation
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

#include "anv_nir.h"

const struct anv_descriptor_set_layout *
anv_pipeline_layout_get_push_set(const struct anv_pipeline_layout *layout,
                                 uint8_t *set_idx)
{
   for (unsigned s = 0; s < ARRAY_SIZE(layout->set); s++) {
      struct anv_descriptor_set_layout *set_layout = layout->set[s].layout;

      if (!set_layout ||
          !(set_layout->flags &
            VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR))
         continue;

      if (set_idx)
         *set_idx = s;

      return set_layout;
   }

   return NULL;
}

/* This function returns a bitfield of used descriptors in the push descriptor
 * set. You can only call this function before calling
 * anv_nir_apply_pipeline_layout() as information required is lost after
 * applying the pipeline layout.
 */
uint32_t
anv_nir_compute_used_push_descriptors(nir_shader *shader,
                                      const struct anv_pipeline_layout *layout)
{
   uint8_t push_set;
   const struct anv_descriptor_set_layout *push_set_layout =
      anv_pipeline_layout_get_push_set(layout, &push_set);
   if (push_set_layout == NULL)
      return 0;

   uint32_t used_push_bindings = 0;
   nir_foreach_variable_with_modes(var, shader,
                                   nir_var_uniform |
                                   nir_var_image |
                                   nir_var_mem_ubo |
                                   nir_var_mem_ssbo) {
      if (var->data.descriptor_set == push_set) {
         uint32_t desc_idx =
            push_set_layout->binding[var->data.binding].descriptor_index;
         assert(desc_idx < MAX_PUSH_DESCRIPTORS);
         used_push_bindings |= BITFIELD_BIT(desc_idx);
      }
   }

   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_vulkan_resource_index)
               continue;

            uint8_t set = nir_intrinsic_desc_set(intrin);
            if (set != push_set)
               continue;

            uint32_t binding = nir_intrinsic_binding(intrin);
            uint32_t desc_idx =
               push_set_layout->binding[binding].descriptor_index;
            assert(desc_idx < MAX_PUSH_DESCRIPTORS);

            used_push_bindings |= BITFIELD_BIT(desc_idx);
         }
      }
   }

   return used_push_bindings;
}

/* This function checks whether the shader accesses the push descriptor
 * buffer. This function must be called after anv_nir_compute_push_layout().
 */
bool
anv_nir_loads_push_desc_buffer(nir_shader *nir,
                               const struct anv_pipeline_layout *layout,
                               const struct anv_pipeline_bind_map *bind_map)
{
   uint8_t push_set;
   const struct anv_descriptor_set_layout *push_set_layout =
      anv_pipeline_layout_get_push_set(layout, &push_set);
   if (push_set_layout == NULL)
      return false;

   nir_foreach_function(function, nir) {
      if (!function->impl)
         continue;

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_ubo)
               continue;

            const nir_const_value *const_bt_idx =
               nir_src_as_const_value(intrin->src[0]);
            if (const_bt_idx == NULL)
               continue;

            const unsigned bt_idx = const_bt_idx[0].u32;

            const struct anv_pipeline_binding *binding =
               &bind_map->surface_to_descriptor[bt_idx];
            if (binding->set == ANV_DESCRIPTOR_SET_DESCRIPTORS &&
                binding->index == push_set)
               return true;
         }
      }
   }

   return false;
}

/* This function computes a bitfield of all the UBOs bindings in the push
 * descriptor set that are fully promoted to push constants. If a binding's
 * bit in the field is set, the corresponding binding table entry will not be
 * accessed by the shader. This function must be called after
 * anv_nir_compute_push_layout().
 */
uint32_t
anv_nir_push_desc_ubo_fully_promoted(nir_shader *nir,
                                     const struct anv_pipeline_layout *layout,
                                     const struct anv_pipeline_bind_map *bind_map)
{
   uint8_t push_set;
   const struct anv_descriptor_set_layout *push_set_layout =
      anv_pipeline_layout_get_push_set(layout, &push_set);
   if (push_set_layout == NULL)
      return 0;

   uint32_t ubos_fully_promoted = 0;
   for (uint32_t b = 0; b < push_set_layout->binding_count; b++) {
      const struct anv_descriptor_set_binding_layout *bind_layout =
         &push_set_layout->binding[b];
      if (bind_layout->type == -1)
         continue;

      assert(bind_layout->descriptor_index < MAX_PUSH_DESCRIPTORS);
      if (bind_layout->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
         ubos_fully_promoted |= BITFIELD_BIT(bind_layout->descriptor_index);
   }

   nir_foreach_function(function, nir) {
      if (!function->impl)
         continue;

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_ubo)
               continue;

            const nir_const_value *const_bt_idx =
               nir_src_as_const_value(intrin->src[0]);
            if (const_bt_idx == NULL)
               continue;

            const unsigned bt_idx = const_bt_idx[0].u32;

            /* Skip if this isn't a load from push descriptor buffer. */
            const struct anv_pipeline_binding *binding =
               &bind_map->surface_to_descriptor[bt_idx];
            if (binding->set != push_set)
               continue;

            const uint32_t desc_idx =
               push_set_layout->binding[binding->binding].descriptor_index;
            assert(desc_idx < MAX_PUSH_DESCRIPTORS);

            bool promoted = false;

            /* If the offset in the entry is dynamic, we can't tell if
             * promoted or not.
             */
            const nir_const_value *const_load_offset =
               nir_src_as_const_value(intrin->src[1]);
            if (const_load_offset != NULL) {
               /* Check if the load was promoted to a push constant. */
               const unsigned load_offset = const_load_offset[0].u32;
               const int load_bytes = nir_intrinsic_dest_components(intrin) *
                  (nir_dest_bit_size(intrin->dest) / 8);

               for (unsigned i = 0; i < ARRAY_SIZE(bind_map->push_ranges); i++) {
                  if (bind_map->push_ranges[i].set == binding->set &&
                      bind_map->push_ranges[i].index == desc_idx &&
                      bind_map->push_ranges[i].start * 32 <= load_offset &&
                      (bind_map->push_ranges[i].start +
                       bind_map->push_ranges[i].length) * 32 >=
                      (load_offset + load_bytes)) {
                     promoted = true;
                     break;
                  }
               }
            }

            if (!promoted)
               ubos_fully_promoted &= ~BITFIELD_BIT(desc_idx);
         }
      }
   }

   return ubos_fully_promoted;
}
