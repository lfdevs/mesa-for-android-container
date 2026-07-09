/* Copyright © 2024 Intel Corporation 
 * SPDX-License-Identifier: MIT       
 */

#ifndef ANV_BVH_BUILD_HELPERS_H
#define ANV_BVH_BUILD_HELPERS_H

#include "vk_bvh_helpers.h"
#include "anv_bvh_defines.h"

#if ((VK_USED_BUILD_FLAGS & ANV_BUILD_FLAG_WRITE_LOOKUP_MAPS_FOR_UPDATE) != 0)
#define ANV_TEST_BUILD_FLAG_WRITE_LOOKUP_MAPS_FOR_UPDATE ((BUILD_FLAGS & ANV_BUILD_FLAG_WRITE_LOOKUP_MAPS_FOR_UPDATE) != 0)
#endif

#endif
