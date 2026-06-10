/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_device.h"

#include "kk_shader.h"

#include "kosmickrisp/clc/kk_precompiled_shader.h"

#include "libkk_shaders.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "nir/nir.h"
#include "nir/nir_builder.h"

static VkResult
build_precompiled_shaders(struct kk_device *dev)
{
   uint32_t i = 0u;
   for (; i < LIBKK_NUM_PROGRAMS; ++i) {
      const uint32_t *bin = libkk_AppleSilicon[i];
      const struct kk_precompiled_info *info = (void *)bin;
      const char *msl = (const char *)bin + sizeof(*info);

      mtl_library *library = mtl_new_library(dev->mtl_compiler_handle, msl);
      if (library == NULL)
         goto fail;

      struct kk_precompiled_shader *shader = &dev->precompiled_cache.shaders[i];

      /* TODO_KOSMICKRISP Do not hardcode the entrypoint */
      mtl_function_descriptor *function =
         mtl_new_library_function_descriptor(library, "main_entrypoint");
      uint32_t local_size_threads = info->workgroup_size[0] *
                                    info->workgroup_size[1] *
                                    info->workgroup_size[2];
      shader->pipeline = mtl_new_compute_pipeline_state(
         dev->mtl_compiler_handle, function, local_size_threads);
      mtl_release(function);
      mtl_release(library);

      if (!shader->pipeline)
         goto fail;

      memcpy(&shader->info, info, sizeof(*info));
   }

   return VK_SUCCESS;

fail:
   for (uint32_t j = 0u; j < i; ++j)
      mtl_release(dev->precompiled_cache.shaders[j].pipeline);
   return vk_error(dev, VK_ERROR_INVALID_SHADER_NV);
}

VkResult
kk_device_init_lib(struct kk_device *dev)
{
   return build_precompiled_shaders(dev);
}

void
kk_device_finish_lib(struct kk_device *dev)
{
   for (uint32_t i = 0; i < LIBKK_NUM_PROGRAMS; ++i)
      mtl_release(dev->precompiled_cache.shaders[i].pipeline);
}
