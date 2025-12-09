/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_device.h"

#include "kk_shader.h"

#include "kkcl.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "nir/nir.h"
#include "nir/nir_builder.h"

static nir_def *
load_struct_var(nir_builder *b, nir_variable *var, uint32_t field)
{
   nir_deref_instr *deref =
      nir_build_deref_struct(b, nir_build_deref_var(b, var), field);
   return nir_load_deref(b, deref);
}

static nir_shader *
create_imm_write_shader()
{
   nir_builder build = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL,
                                                      "kk-meta-imm-write-u64");
   nir_builder *b = &build;

   struct glsl_struct_field push_fields[] = {
      {.type = glsl_uint64_t_type(), .name = "buffer_address", .offset = 0},
   };
   const struct glsl_type *push_iface_type = glsl_interface_type(
      push_fields, ARRAY_SIZE(push_fields), GLSL_INTERFACE_PACKING_STD140,
      false /* row_major */, "push");
   nir_variable *push = nir_variable_create(b->shader, nir_var_mem_push_const,
                                            push_iface_type, "push");

   b->shader->info.workgroup_size[0] = 1;
   b->shader->info.workgroup_size[1] = 1;
   b->shader->info.workgroup_size[2] = 1;

   libkk_write_u64(b, load_struct_var(b, push, 0));

   return build.shader;
}

static nir_shader *
create_copy_query_shader()
{
   nir_builder build = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL,
                                                      "kk-meta-copy-queries");
   nir_builder *b = &build;

   struct glsl_struct_field push_fields[] = {
      {.type = glsl_uint64_t_type(), .name = "availability", .offset = 0},
      {.type = glsl_uint64_t_type(), .name = "results", .offset = 8},
      {.type = glsl_uint64_t_type(), .name = "indices", .offset = 16},
      {.type = glsl_uint64_t_type(), .name = "dst_addr", .offset = 24},
      {.type = glsl_uint64_t_type(), .name = "dst_stride", .offset = 32},
      {.type = glsl_uint_type(), .name = "first_query", .offset = 40},
      {.type = glsl_uint_type(), .name = "flags", .offset = 44},
      {.type = glsl_uint16_t_type(), .name = "reports_per_query", .offset = 48},
   };
   /* TODO_KOSMICKRISP Don't use push constants and directly bind the buffer to
    * the binding index. This requires compiler work first to remove the
    * hard-coded buffer0 value. Same applies to other creation functions.
    */
   const struct glsl_type *push_iface_type = glsl_interface_type(
      push_fields, ARRAY_SIZE(push_fields), GLSL_INTERFACE_PACKING_STD140,
      false /* row_major */, "push");
   nir_variable *push = nir_variable_create(b->shader, nir_var_mem_push_const,
                                            push_iface_type, "push");

   b->shader->info.workgroup_size[0] = 1;
   b->shader->info.workgroup_size[1] = 1;
   b->shader->info.workgroup_size[2] = 1;

   libkk_copy_queries(b, load_struct_var(b, push, 0),
                      load_struct_var(b, push, 1), load_struct_var(b, push, 2),
                      load_struct_var(b, push, 3), load_struct_var(b, push, 4),
                      load_struct_var(b, push, 5), load_struct_var(b, push, 6),
                      load_struct_var(b, push, 7));

   return build.shader;
}

static nir_shader *
create_triangle_fan_shader()
{
   nir_builder build = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, NULL, "kk-device-unroll-geomtry-and-restart");
   nir_builder *b = &build;

   struct glsl_struct_field push_fields[] = {
      {.type = glsl_uint64_t_type(), .name = "index_buffer", .offset = 0},
      {.type = glsl_uint64_t_type(), .name = "out_ptr", .offset = 8},
      {.type = glsl_uint64_t_type(), .name = "indirect_in", .offset = 16},
      {.type = glsl_uint64_t_type(), .name = "indirect_out", .offset = 24},
      {.type = glsl_uint_type(), .name = "restart_index", .offset = 32},
      {.type = glsl_uint_type(), .name = "index_buffer_size_el", .offset = 36},
      {.type = glsl_uint_type(), .name = "in_el_size_B,", .offset = 40},
      {.type = glsl_uint_type(), .name = "out_el_size_B,", .offset = 44},
      {.type = glsl_uint_type(), .name = "flatshade_first", .offset = 48},
      {.type = glsl_uint_type(), .name = "mode", .offset = 52},
   };
   const struct glsl_type *push_iface_type = glsl_interface_type(
      push_fields, ARRAY_SIZE(push_fields), GLSL_INTERFACE_PACKING_STD140,
      false /* row_major */, "push");
   nir_variable *push = nir_variable_create(b->shader, nir_var_mem_push_const,
                                            push_iface_type, "push");

   b->shader->info.workgroup_size[0] = 1;
   b->shader->info.workgroup_size[1] = 1;
   b->shader->info.workgroup_size[2] = 1;

   libkk_unroll_geometry_and_restart(
      b, load_struct_var(b, push, 0), load_struct_var(b, push, 1),
      load_struct_var(b, push, 2), load_struct_var(b, push, 3),
      load_struct_var(b, push, 4), load_struct_var(b, push, 5),
      load_struct_var(b, push, 6), load_struct_var(b, push, 7),
      load_struct_var(b, push, 8), load_struct_var(b, push, 9));

   return build.shader;
}

static struct {
   enum kk_device_lib_pipeline ndx;
   nir_shader *(*create_shader_fn)();
} lib_shaders[KK_LIB_COUNT] = {
   {KK_LIB_IMM_WRITE, create_imm_write_shader},
   {KK_LIB_COPY_QUERY, create_copy_query_shader},
   {KK_LIB_TRIANGLE_FAN, create_triangle_fan_shader},
};
static_assert(ARRAY_SIZE(lib_shaders) == KK_LIB_COUNT,
              "Device lib shader count and created shader count mismatch");

VkResult
kk_device_init_lib(struct kk_device *dev)
{
   VkResult result = VK_SUCCESS;
   uint32_t i = 0u;
   for (; i < KK_LIB_COUNT; ++i) {
      nir_shader *s = lib_shaders[i].create_shader_fn();
      if (!s)
         goto fail;

      struct kk_shader *shader = NULL;
      result = kk_compile_nir_shader(dev, s, &dev->vk.alloc, &shader);
      if (result != VK_SUCCESS)
         goto fail;

      mtl_library *library = mtl_new_library(dev->mtl_handle, shader->msl_code);
      if (library == NULL)
         goto fail;

      uint32_t local_size_threads = shader->info.cs.local_size.x *
                                    shader->info.cs.local_size.y *
                                    shader->info.cs.local_size.z;
      mtl_function *function =
         mtl_new_function_with_name(library, shader->entrypoint_name);
      dev->lib_pipelines[i] = mtl_new_compute_pipeline_state(
         dev->mtl_handle, function, local_size_threads);
      mtl_release(function);
      mtl_release(library);

      /* We no longer need the shader. Although it may be useful to keep it
       * alive for the info maybe? */
      shader->vk.ops->destroy(&dev->vk, &shader->vk, &dev->vk.alloc);

      if (!dev->lib_pipelines[i])
         goto fail;
   }

   return result;

fail:
   for (uint32_t j = 0u; j < i; ++j)
      mtl_release(dev->lib_pipelines[j]);
   return vk_error(dev, result);
}

void
kk_device_finish_lib(struct kk_device *dev)
{
   for (uint32_t i = 0; i < KK_LIB_COUNT; ++i)
      mtl_release(dev->lib_pipelines[i]);
}
