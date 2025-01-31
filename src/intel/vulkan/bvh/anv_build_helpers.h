/* Copyright © 2024 Intel Corporation 
 * SPDX-License-Identifier: MIT       
 */

#ifndef ANV_BVH_BUILD_HELPERS_H
#define ANV_BVH_BUILD_HELPERS_H

#include "vk_build_helpers.h"
#include "anv_bvh.h"

TYPE(anv_accel_struct_header, 8);
TYPE(anv_quad_leaf_node, 4);
TYPE(anv_procedural_leaf_node, 4);
TYPE(anv_internal_node, 4);
TYPE(anv_instance_leaf, 8);
TYPE(anv_prim_leaf_desc, 4);
TYPE(child_data, 1);
TYPE(instance_leaf_part0, 8);
TYPE(instance_leaf_part1, 8);

#endif
