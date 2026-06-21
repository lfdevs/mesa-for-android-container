/*
 * Copyright © 2025 Autumn Ashton
 * SPDX-License-Identifier: MIT
 */

#ifndef NVK_CUBIN_H
#define NVK_CUBIN_H 1

#include "nvk_private.h"

struct nv_cubin_module;
struct nv_cubin_function;

struct nvk_cubin_module {
   struct vk_object_base base;

   struct nv_cubin_module *info;
};

struct nvk_cubin_function {
   struct vk_object_base base;

   struct nvk_cubin_module *module;
   const struct nv_cubin_function *info;

   uint32_t upload_size;
   uint64_t upload_addr;

   uint32_t max_warps_per_sm;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_cubin_module, base, VkCuModuleNVX,
                               VK_OBJECT_TYPE_CU_MODULE_NVX)

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_cubin_function, base, VkCuFunctionNVX,
                               VK_OBJECT_TYPE_CU_FUNCTION_NVX)

#endif
