/*
 * Copyright © 2015 Intel Corporation
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

#include "anv_private.h"

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"

VkResult
genX(compute_pipeline_create)(
    VkDevice                                    _device,
    struct anv_pipeline_cache *                 cache,
    const VkComputePipelineCreateInfo*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipeline)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_pipeline *pipeline;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);

   pipeline = anv_alloc2(&device->alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   pipeline->device = device;
   pipeline->layout = anv_pipeline_layout_from_handle(pCreateInfo->layout);

   pipeline->blend_state.map = NULL;

   result = anv_reloc_list_init(&pipeline->batch_relocs,
                                pAllocator ? pAllocator : &device->alloc);
   if (result != VK_SUCCESS) {
      anv_free2(&device->alloc, pAllocator, pipeline);
      return result;
   }
   pipeline->batch.next = pipeline->batch.start = pipeline->batch_data;
   pipeline->batch.end = pipeline->batch.start + sizeof(pipeline->batch_data);
   pipeline->batch.relocs = &pipeline->batch_relocs;

   /* When we free the pipeline, we detect stages based on the NULL status
    * of various prog_data pointers.  Make them NULL by default.
    */
   memset(pipeline->prog_data, 0, sizeof(pipeline->prog_data));
   memset(pipeline->bindings, 0, sizeof(pipeline->bindings));

   pipeline->vs_simd8 = NO_KERNEL;
   pipeline->vs_vec4 = NO_KERNEL;
   pipeline->gs_kernel = NO_KERNEL;

   pipeline->active_stages = 0;

   pipeline->needs_data_cache = false;

   assert(pCreateInfo->stage.stage == VK_SHADER_STAGE_COMPUTE_BIT);
   ANV_FROM_HANDLE(anv_shader_module, module,  pCreateInfo->stage.module);
   anv_pipeline_compile_cs(pipeline, cache, pCreateInfo, module,
                           pCreateInfo->stage.pName,
                           pCreateInfo->stage.pSpecializationInfo);

   pipeline->use_repclear = false;

   anv_setup_pipeline_l3_config(pipeline);

   const struct brw_cs_prog_data *cs_prog_data = get_cs_prog_data(pipeline);

   uint32_t group_size = cs_prog_data->local_size[0] *
      cs_prog_data->local_size[1] * cs_prog_data->local_size[2];
   uint32_t remainder = group_size & (cs_prog_data->simd_size - 1);

   if (remainder > 0)
      pipeline->cs_right_mask = ~0u >> (32 - remainder);
   else
      pipeline->cs_right_mask = ~0u >> (32 - cs_prog_data->simd_size);

   const uint32_t vfe_curbe_allocation =
      ALIGN(cs_prog_data->push.per_thread.regs * cs_prog_data->threads +
            cs_prog_data->push.cross_thread.regs, 2);

   anv_batch_emit(&pipeline->batch, GENX(MEDIA_VFE_STATE), vfe) {
      vfe.ScratchSpaceBasePointer = (struct anv_address) {
         .bo = anv_scratch_pool_alloc(device, &device->scratch_pool,
                                      MESA_SHADER_COMPUTE,
                                      cs_prog_data->base.total_scratch),
         .offset = 0,
      };
      vfe.PerThreadScratchSpace  = ffs(cs_prog_data->base.total_scratch / 2048);
#if GEN_GEN > 7
      vfe.StackSize              = 0;
#else
      vfe.GPGPUMode              = true;
#endif
      vfe.MaximumNumberofThreads = device->info.max_cs_threads - 1;
      vfe.NumberofURBEntries     = GEN_GEN <= 7 ? 0 : 2;
      vfe.ResetGatewayTimer      = true;
#if GEN_GEN <= 8
      vfe.BypassGatewayControl   = true;
#endif
      vfe.URBEntryAllocationSize = GEN_GEN <= 7 ? 0 : 2;
      vfe.CURBEAllocationSize    = vfe_curbe_allocation;
   }

   *pPipeline = anv_pipeline_to_handle(pipeline);

   return VK_SUCCESS;
}
