/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "radix_sort_u64.h"
#include <assert.h>

static const uint32_t init_spv[] = {
#include "radix_sort/shaders/u64_init.spv.h"
};

static const uint32_t fill_spv[] = {
#include "radix_sort/shaders/fill.spv.h"
};

static const uint32_t histogram_spv[] = {
#include "radix_sort/shaders/u64_histogram.spv.h"
};

static const uint32_t prefix_spv[] = {
#include "radix_sort/shaders/u64_prefix.spv.h"
};

static const uint32_t scatter_0_even_spv[] = {
#include "radix_sort/shaders/u64_scatter_0_even.spv.h"
};

static const uint32_t scatter_0_odd_spv[] = {
#include "radix_sort/shaders/u64_scatter_0_odd.spv.h"
};

static const uint32_t scatter_1_even_spv[] = {
#include "radix_sort/shaders/u64_scatter_1_even.spv.h"
};

static const uint32_t scatter_1_odd_spv[] = {
#include "radix_sort/shaders/u64_scatter_1_odd.spv.h"
};


radix_sort_vk_t *
vk_create_radix_sort_u64(VkDevice device, VkAllocationCallbacks const *ac,
                         VkPipelineCache pc,
                         struct radix_sort_vk_target_config config)
{
   assert(config.keyval_dwords == 2);

   const uint32_t *spv[8] = {
      init_spv,           fill_spv,          histogram_spv,      prefix_spv,
      scatter_0_even_spv, scatter_0_odd_spv, scatter_1_even_spv, scatter_1_odd_spv,
   };
   const uint32_t spv_sizes[8] = {
      sizeof(init_spv),           sizeof(fill_spv),          sizeof(histogram_spv),      sizeof(prefix_spv),
      sizeof(scatter_0_even_spv), sizeof(scatter_0_odd_spv), sizeof(scatter_1_even_spv), sizeof(scatter_1_odd_spv),
   };
   return radix_sort_vk_create(device, ac, pc, spv, spv_sizes, config);
}

