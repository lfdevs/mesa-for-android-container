/*
 * Copyright © 2025 Autumn Ashton
 * SPDX-License-Identifier: MIT
 */
#include "nvk_cubin.h"

#include "nvk_cmd_buffer.h"
#include "nvk_device.h"
#include "nvk_entrypoints.h"
#include "nvk_instance.h"

#include "nouveau/cubin/nv_cubin.h"
#include "nouveau/cubin/nv_fatbin.h"

#include "util/u_process.h"

static void
nvk_cubin_dump(const char *extension, const void *data, size_t size)
{
   static int dump_idx = 0;

   char path[2048];
   snprintf(path, sizeof(path), "/tmp/%s.%d.%s", util_get_process_name(),
            dump_idx++, extension);
   FILE *dump_file = fopen(path, "wb");
   if (!dump_file) {
      mesa_loge("nvk_cubin_dump: Failed to dump to: %s\n", path);
      return;
   }

   fwrite(data, 1, size, dump_file);
   fclose(dump_file);
}

/* nvk_cubin_module */

static VkResult
nvk_cubin_module_init(struct nvk_device *dev, struct nvk_cubin_module *module,
                      const VkCuModuleCreateInfoNVX *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   const struct nvk_instance *instance = nvk_physical_device_instance(pdev);
   const void *data;
   size_t size;
   bool backwards_compat =
      instance->experimental_flags & NVK_EXPERIMENTAL_DLSS_BACK_COMPAT;

   struct nv_fatbin fatbin;
   if (nv_fatbin_init(&fatbin, pCreateInfo->pData, pCreateInfo->dataSize)) {
      /* Must be a fatbin. */
#ifndef NDEBUG
      if (debug_get_bool_option("NVK_CUBIN_DUMP", false))
         nvk_cubin_dump("fatbin", pCreateInfo->pData, pCreateInfo->dataSize);
#endif

      if (!nv_fatbin_get_bytecode(&fatbin, pdev->info.sm,
                                  backwards_compat, &data, &size))
         return vk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                         "Fatbin does not have compatible bytecode");
   } else {
      /* Must be a cubin. */
      data = pCreateInfo->pData;
      size = pCreateInfo->dataSize;
   }

#ifndef NDEBUG
   if (debug_get_bool_option("NVK_CUBIN_DUMP", false))
      nvk_cubin_dump("cubin", data, size);
#endif

   module->info =
      vk_zalloc2(&dev->vk.alloc, pAllocator, sizeof(struct nv_cubin_module), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!nv_cubin_module_init(module->info, data, size))
      return VK_ERROR_INITIALIZATION_FAILED;

   if (!nv_is_sm_compatible(pdev->info.sm, module->info->sm, backwards_compat))
         return vk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                          "Incompatible cubin module: pdev sm_%u vs cubin sm_%u",
                          pdev->info.sm, module->info->sm);

   return VK_SUCCESS;
}

static void
nvk_cubin_module_destroy(struct nvk_device *dev,
                         struct nvk_cubin_module *module,
                         const VkAllocationCallbacks *pAllocator)
{
   if (module->info) {
      nv_cubin_module_fini(module->info);
      vk_free2(&dev->vk.alloc, pAllocator, module->info);
   }
   vk_object_free(&dev->vk, pAllocator, module);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateCuModuleNVX(VkDevice _device,
                      const VkCuModuleCreateInfoNVX *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkCuModuleNVX *pModule)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   struct nvk_cubin_module *module;
   VkResult result;

   module = vk_object_zalloc(&dev->vk, pAllocator, sizeof(*module),
                             VK_OBJECT_TYPE_CU_MODULE_NVX);
   if (!module)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = nvk_cubin_module_init(dev, module, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      nvk_cubin_module_destroy(dev, module, pAllocator);
      return result;
   }

   *pModule = nvk_cubin_module_to_handle(module);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyCuModuleNVX(VkDevice _device, VkCuModuleNVX _module,
                       const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VK_FROM_HANDLE(nvk_cubin_module, module, _module);

   if (!module)
      return;

   nvk_cubin_module_destroy(dev, module, pAllocator);
}

/* nvk_cubin_function */

static VkResult
nvk_cubin_function_init(struct nvk_device *dev,
                        struct nvk_cubin_function *function,
                        const VkCuFunctionCreateInfoNVX *pCreateInfo)
{
   VK_FROM_HANDLE(nvk_cubin_module, module, pCreateInfo->module);

   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   const struct nak_compiler *nak = pdev->nak;

   function->module = module;
   function->info =
      nv_cubin_module_find_function(module->info, pCreateInfo->pName);
   if (!function->info)
      return VK_ERROR_INITIALIZATION_FAILED;

   const int alignment = 0x80; /* maxwell+ */

   VkResult result = nvk_heap_upload(
      dev, &dev->shader_heap, function->info->code_ptr,
      function->info->code_size, alignment, &function->upload_addr);
   if (result != VK_SUCCESS)
      return result;

   function->upload_size = function->info->code_size;
   function->max_warps_per_sm = nak_max_warps_per_sm(function->info->gpr_count, nak);

   return VK_SUCCESS;
}

static void
nvk_cubin_function_destroy(struct nvk_device *dev,
                           struct nvk_cubin_function *function,
                           const VkAllocationCallbacks *pAllocator)
{
   if (function->upload_size) {
      nvk_heap_free(dev, &dev->shader_heap, function->upload_addr,
                    function->upload_size);
   }

   vk_object_free(&dev->vk, pAllocator, function);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateCuFunctionNVX(VkDevice _device,
                        const VkCuFunctionCreateInfoNVX *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkCuFunctionNVX *pFunction)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   struct nvk_cubin_function *function;
   VkResult result;

   function = vk_object_zalloc(&dev->vk, pAllocator, sizeof(*function),
                               VK_OBJECT_TYPE_CU_FUNCTION_NVX);
   if (!function)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = nvk_cubin_function_init(dev, function, pCreateInfo);
   if (result != VK_SUCCESS) {
      nvk_cubin_function_destroy(dev, function, pAllocator);
      return result;
   }

   *pFunction = nvk_cubin_function_to_handle(function);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyCuFunctionNVX(VkDevice _device, VkCuFunctionNVX _function,
                         const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VK_FROM_HANDLE(nvk_cubin_function, function, _function);

   if (!function)
      return;

   nvk_cubin_function_destroy(dev, function, pAllocator);
}

/* kernel launch */

struct nvk_cubin_root_descriptor {
   uint32_t block_dim[3];
   uint32_t grid_dim[3];
   uint8_t data[4096 - (6 * sizeof(uint32_t))];
};

/* extra launch parameters taken from vkd3d-proton */
#define CU_LAUNCH_PARAM_BUFFER_POINTER ((const void *)0x01)
#define CU_LAUNCH_PARAM_BUFFER_SIZE    ((const void *)0x02)
#define CU_LAUNCH_PARAM_END            ((const void *)0x00)

VKAPI_ATTR void VKAPI_CALL
nvk_CmdCuLaunchKernelNVX(VkCommandBuffer commandBuffer,
                         const VkCuLaunchInfoNVX *pLaunchInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_cubin_function, function, pLaunchInfo->function);
   const struct nv_cubin_function *function_info = function->info;

   /*
    * Use a root descriptor on the stack if its < 4kb,
    * otherwise, allocate one big enough for us to hold
    * the kernel parameters.
    */
   struct nvk_cubin_root_descriptor stack_root;
   struct nvk_cubin_root_descriptor *root = NULL;
   const size_t root_size =
      align(MAX2(sizeof(struct nvk_cubin_root_descriptor),
                 function_info->params_offset + function_info->params_size),
            0x100);
   if (root_size <= 4096) {
      root = &stack_root;
   } else {
      root = malloc(root_size);
   }
   memset(root, 0, root_size);
   root->block_dim[0] = pLaunchInfo->blockDimX;
   root->block_dim[1] = pLaunchInfo->blockDimY;
   root->block_dim[2] = pLaunchInfo->blockDimZ;
   root->grid_dim[0] = pLaunchInfo->gridDimX;
   root->grid_dim[1] = pLaunchInfo->gridDimY;
   root->grid_dim[2] = pLaunchInfo->gridDimZ;

   const void *param_buffer = NULL;
   const uint32_t *param_buffer_size = NULL;
   if (pLaunchInfo->extraCount) {
      /* pLaunchInfo->extraCount is erroneous, pExtras is not extraCount sized.
       * It's a stream of options. Just use extraCount to key off presence. */
      const void *const *extra = pLaunchInfo->pExtras;

      while (*extra != CU_LAUNCH_PARAM_END) {
         if (*extra == CU_LAUNCH_PARAM_BUFFER_POINTER) {
            extra++;
            param_buffer = *extra;
         }
         if (*extra == CU_LAUNCH_PARAM_BUFFER_SIZE) {
            extra++;
            param_buffer_size = *extra;
         }
         extra++;
      }
   }

   if (param_buffer && param_buffer_size) {
      /* CU_LAUNCH_PARAM_BUFFER_POINTER contains ALL parameters. */
      assert(pLaunchInfo->paramCount == 0);

      const struct nv_cubin_function_param_info *param_info =
         nv_cubin_function_get_param_info(function_info, 0);
      assert(param_info != NULL);

      const size_t param_offset =
         function_info->params_offset + param_info->offset;

      void *param_root_dst = ((uint8_t *)root) + param_offset;
      memcpy(param_root_dst, param_buffer, *param_buffer_size);
   } else {
      for (uint32_t i = 0; i < pLaunchInfo->paramCount; i++) {
         const void *param_data = pLaunchInfo->pParams[i];

         const struct nv_cubin_function_param_info *param_info =
            nv_cubin_function_get_param_info(function_info, i);
         assert(param_info != NULL);

         const size_t param_offset =
            function_info->params_offset + param_info->offset;

         void *param_root_dst = ((uint8_t *)root) + param_offset;
         memcpy(param_root_dst, param_data, param_info->size);
      }
   }

   struct nvk_shader shader;
   memset(&shader, 0, sizeof(shader));
   shader.upload_size = function->upload_size;
   shader.upload_addr = function->upload_addr;
   shader.hdr_addr = function->upload_addr;
   shader.data_addr = function->upload_addr + function_info->func_offset;
   shader.info.stage = MESA_SHADER_COMPUTE;
   shader.info.sm = function->module->info->sm;
   shader.info.num_gprs = function_info->gpr_count;
   shader.info.num_control_barriers = function_info->num_control_barriers;
   shader.info.max_warps_per_sm = function->max_warps_per_sm;
   shader.info.slm_size = function_info->slm_size;
   shader.info.crs_size = function_info->crs_size;
   shader.info.cs.local_size[0] = pLaunchInfo->blockDimX;
   shader.info.cs.local_size[1] = pLaunchInfo->blockDimY;
   shader.info.cs.local_size[2] = pLaunchInfo->blockDimZ;
   shader.info.cs.smem_size =
      function_info->static_smem_size + pLaunchInfo->sharedMemBytes;
   shader.cbuf_map.cbuf_count = 1;
   shader.cbuf_map.cbufs[0].type = NVK_CBUF_TYPE_ROOT_DESC;

   nvk_cmd_dispatch_with_root(cmd, &shader, root, root_size, pLaunchInfo->gridDimX,
                              pLaunchInfo->gridDimY, pLaunchInfo->gridDimZ);

   if (root != &stack_root)
      free(root);
}
