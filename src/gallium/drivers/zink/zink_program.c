/*
 * Copyright 2018 Collabora Ltd.
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

#include "zink_program.h"

#include "zink_compiler.h"
#include "zink_context.h"
#include "zink_render_pass.h"
#include "zink_screen.h"

#include "util/hash_table.h"
#include "util/set.h"
#include "util/u_debug.h"
#include "util/u_memory.h"

static VkDescriptorSetLayout
create_desc_set_layout(VkDevice dev,
                       struct zink_shader *stages[PIPE_SHADER_TYPES - 1],
                       unsigned *num_descriptors)
{
   VkDescriptorSetLayoutBinding bindings[PIPE_SHADER_TYPES * PIPE_MAX_CONSTANT_BUFFERS];
   int num_bindings = 0;

   for (int i = 0; i < PIPE_SHADER_TYPES - 1; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;

      VkShaderStageFlagBits stage_flags = zink_shader_stage(i);
      for (int j = 0; j < shader->num_bindings; j++) {
         assert(num_bindings < ARRAY_SIZE(bindings));
         bindings[num_bindings].binding = shader->bindings[j].binding;
         bindings[num_bindings].descriptorType = shader->bindings[j].type;
         bindings[num_bindings].descriptorCount = 1;
         bindings[num_bindings].stageFlags = stage_flags;
         bindings[num_bindings].pImmutableSamplers = NULL;
         ++num_bindings;
      }
   }

   VkDescriptorSetLayoutCreateInfo dcslci = {};
   dcslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   dcslci.pNext = NULL;
   dcslci.flags = 0;
   dcslci.bindingCount = num_bindings;
   dcslci.pBindings = bindings;

   VkDescriptorSetLayout dsl;
   if (vkCreateDescriptorSetLayout(dev, &dcslci, 0, &dsl) != VK_SUCCESS) {
      debug_printf("vkCreateDescriptorSetLayout failed\n");
      return VK_NULL_HANDLE;
   }

   *num_descriptors = num_bindings;
   return dsl;
}

static VkPipelineLayout
create_pipeline_layout(VkDevice dev, VkDescriptorSetLayout dsl)
{
   assert(dsl != VK_NULL_HANDLE);

   VkPipelineLayoutCreateInfo plci = {};
   plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

   plci.pSetLayouts = &dsl;
   plci.setLayoutCount = 1;

   VkPipelineLayout layout;
   if (vkCreatePipelineLayout(dev, &plci, NULL, &layout) != VK_SUCCESS) {
      debug_printf("vkCreatePipelineLayout failed!\n");
      return VK_NULL_HANDLE;
   }

   return layout;
}

static uint32_t
hash_gfx_pipeline_state(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct zink_gfx_pipeline_state));
}

static bool
equals_gfx_pipeline_state(const void *a, const void *b)
{
   return memcmp(a, b, sizeof(struct zink_gfx_pipeline_state)) == 0;
}

struct zink_gfx_program *
zink_create_gfx_program(struct zink_screen *screen,
                        struct zink_shader *stages[PIPE_SHADER_TYPES - 1])
{
   struct zink_gfx_program *prog = CALLOC_STRUCT(zink_gfx_program);
   if (!prog)
      goto fail;

   for (int i = 0; i < ARRAY_SIZE(prog->pipelines); ++i) {
      prog->pipelines[i] = _mesa_hash_table_create(NULL,
                                                   hash_gfx_pipeline_state,
                                                   equals_gfx_pipeline_state);
      if (!prog->pipelines[i])
         goto fail;
   }

   for (int i = 0; i < PIPE_SHADER_TYPES - 1; ++i)
      prog->stages[i] = stages[i];

   prog->dsl = create_desc_set_layout(screen->dev, stages,
                                      &prog->num_descriptors);
   if (!prog->dsl)
      goto fail;

   prog->layout = create_pipeline_layout(screen->dev, prog->dsl);
   if (!prog->layout)
      goto fail;

   prog->render_passes = _mesa_set_create(NULL, _mesa_hash_pointer,
                                          _mesa_key_pointer_equal);
   if (!prog->render_passes)
      goto fail;

   return prog;

fail:
   if (prog)
      zink_destroy_gfx_program(screen, prog);
   return NULL;
}

void
zink_destroy_gfx_program(struct zink_screen *screen,
                         struct zink_gfx_program *prog)
{
   if (prog->layout)
      vkDestroyPipelineLayout(screen->dev, prog->layout, NULL);

   if (prog->dsl)
      vkDestroyDescriptorSetLayout(screen->dev, prog->dsl, NULL);

   /* unref all used render-passes */
   if (prog->render_passes) {
      set_foreach(prog->render_passes, entry) {
         struct zink_render_pass *pres = (struct zink_render_pass *)entry->key;
         zink_render_pass_reference(screen, &pres, NULL);
      }
      _mesa_set_destroy(prog->render_passes, NULL);
   }

   FREE(prog);
}

struct pipeline_cache_entry {
   struct zink_gfx_pipeline_state state;
   VkPipeline pipeline;
};

static VkPrimitiveTopology
primitive_topology(enum pipe_prim_type mode)
{
   switch (mode) {
   case PIPE_PRIM_POINTS:
      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

   case PIPE_PRIM_LINES:
      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

   case PIPE_PRIM_LINE_STRIP:
      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;

   case PIPE_PRIM_TRIANGLES:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

   case PIPE_PRIM_TRIANGLE_STRIP:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

   case PIPE_PRIM_TRIANGLE_FAN:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;

   default:
      unreachable("unexpected enum pipe_prim_type");
   }
}

static void
reference_render_pass(struct zink_screen *screen,
                      struct zink_gfx_program *prog,
                      struct zink_render_pass *render_pass)
{
   struct set_entry *entry = _mesa_set_search(prog->render_passes,
                                              render_pass);
   if (!entry) {
      struct zink_render_pass *tmp = NULL;
      entry = _mesa_set_add(prog->render_passes, render_pass);
      zink_render_pass_reference(screen, &tmp, render_pass);
   }
}

VkPipeline
zink_get_gfx_pipeline(struct zink_screen *screen,
                      struct zink_gfx_program *prog,
                      struct zink_gfx_pipeline_state *state,
                      enum pipe_prim_type mode)
{
   assert(mode <= ARRAY_SIZE(prog->pipelines));

   /* TODO: use pre-hashed versions to save some time (can re-hash only when
      state changes) */
   struct hash_entry *entry = _mesa_hash_table_search(prog->pipelines[mode], state);
   if (!entry) {
      VkPrimitiveTopology vkmode = primitive_topology(mode);
      VkPipeline pipeline = zink_create_gfx_pipeline(screen, prog,
                                                     state, vkmode);
      if (pipeline == VK_NULL_HANDLE)
         return VK_NULL_HANDLE;

      struct pipeline_cache_entry *pc_entry = CALLOC_STRUCT(pipeline_cache_entry);
      if (!pc_entry)
         return VK_NULL_HANDLE;

      memcpy(&pc_entry->state, state, sizeof(*state));
      pc_entry->pipeline = pipeline;

      entry = _mesa_hash_table_insert(prog->pipelines[mode], &pc_entry->state, pc_entry);
      assert(entry);

      reference_render_pass(screen, prog, state->render_pass);
   }

   return ((struct pipeline_cache_entry *)(entry->data))->pipeline;
}
