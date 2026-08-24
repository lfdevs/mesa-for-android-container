/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_entrypoints.h"

static VkResult
prepare_push_uniforms(struct panvk_cmd_buffer *cmdbuf,
                      const struct panvk_shader_variant *shader,
                      struct pan_ptr *push_uniforms, uint32_t repeat_count,
                      uint64_t *sysvals)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   if (!shader->fau.total_count) {
      push_uniforms->cpu = NULL;
      push_uniforms->gpu = 0;
      return VK_SUCCESS;
   }

   *push_uniforms = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, shader->fau.total_count * sizeof(uint64_t) * repeat_count,
      sizeof(uint64_t));

   if (!push_uniforms->gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct panvk_common_sysvals_inner common_inner = {
      .printf_buffer_address = dev->printf.bo->addr.dev,
   };
   uint64_t *common = (uint64_t *)&common_inner;

   uint64_t *push_consts = cmdbuf->state.push_constants.data;
   uint64_t *faus = push_uniforms->cpu;
   const struct pan_fau_layout *fau = &shader->info.fau;
   uint32_t w;

   for (uint32_t r = 0; r < repeat_count; r++) {
      /* Each repeat owns its own contiguous block of total_count slots. */
      uint64_t *repeat_faus = faus + r * shader->fau.total_count;
      uint32_t fau_idx = 0;

      common_inner.push_uniforms =
         push_uniforms->gpu + r * shader->fau.total_count * sizeof(uint64_t);

      /* After packing, the sysvals come first, followed by the user push
       * constants. The ordering is encoded shader side, so don't re-order
       * these loops. */
      BITSET_FOREACH_SET(w, shader->fau.used_sysvals, MAX_SYSVAL_FAUS) {
         if (w >= SYSVALS_COMMON_START && w < SYSVALS_COMMON_END)
            repeat_faus[fau_idx++] = common[w - SYSVALS_COMMON_START];
         else
            repeat_faus[fau_idx++] = sysvals[w];
      }

      BITSET_FOREACH_SET(w, shader->fau.used_push_consts, MAX_PUSH_CONST_FAUS)
         repeat_faus[fau_idx++] = push_consts[w];
      assert(fau_idx <= shader->fau.total_count);

      pan_fau_foreach_imm(fau, i) {
         bool hi = (i & 1) != 0;
         unsigned idx = i / 2;
         assert(fau_idx <= idx && idx < shader->fau.total_count);

         repeat_faus[idx] =
            (repeat_faus[idx] & ((uint64_t)UINT32_MAX << (32 * !hi))) |
            ((uint64_t)fau->words[i].constant << (32 * hi));
      }
   }

   return VK_SUCCESS;
}

VkResult
panvk_per_arch(cmd_prepare_gfx_push_uniforms)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_shader_variant *shader,
   struct pan_ptr *push_uniforms, uint32_t repeat_count)
{
   return prepare_push_uniforms(cmdbuf, shader, push_uniforms, repeat_count,
                                (uint64_t *)&cmdbuf->state.gfx.sysvals);
}

VkResult
panvk_per_arch(cmd_prepare_compute_push_uniforms)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_shader_variant *shader,
   struct pan_ptr *push_uniforms)
{
   return prepare_push_uniforms(cmdbuf, shader, push_uniforms, 1,
                                (uint64_t *)&cmdbuf->state.compute.sysvals);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdPushConstants2KHR)(
   VkCommandBuffer commandBuffer,
   const VkPushConstantsInfoKHR *pPushConstantsInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_VERTEX_BIT)
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT)
      gfx_state_set_dirty(cmdbuf, FS_PUSH_UNIFORMS);

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT)
      compute_state_set_dirty(cmdbuf, PUSH_UNIFORMS);

   uint8_t *data =
      (uint8_t *)cmdbuf->state.push_constants.data + pPushConstantsInfo->offset;

   memcpy(data, pPushConstantsInfo->pValues, pPushConstantsInfo->size);
}
