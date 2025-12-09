/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_BRIDGE_H
#define KK_BRIDGE_H 1

/* C wrappers for Metal. May not be complete. If you find something you need
 * feel free to add them where they belong. As a rule of thumb, member functions
 * go in the objects' .h/.m/.c Naming convention for wrappers is:
 * object_type* mtl_new_object_type(params...);
 * void mtl_member_function(object_type* ptr, params...);
 * void mtl_object_set_member(object_type* ptr, member_type value);
 * member_type mtl_object_get_member(object_type* ptr);
 *
 * Functions that have new in the name require to release the returned object
 * via mtl_release(object);
 * */

#include "mtl_types.h"

#include "mtl_buffer.h"
#include "mtl_command_buffer.h"
#include "mtl_command_queue.h"
#include "mtl_compute_state.h"
#include "mtl_device.h"
#include "mtl_encoder.h"
#include "mtl_format.h"
#include "mtl_heap.h"
#include "mtl_library.h"
#include "mtl_render_state.h"
#include "mtl_residency_set.h"
#include "mtl_sampler.h"
#include "mtl_sync.h"
#include "mtl_texture.h"

mtl_texture *mtl_drawable_get_texture(void *drawable_ptr);

void *mtl_retain(void *handle);
void mtl_release(void *handle);

#endif /* KK_BRIDGE_H */
