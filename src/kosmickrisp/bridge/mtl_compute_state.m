/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_compute_state.h"

#include <Metal/MTLComputePipeline.h>

mtl_compute_pipeline_state *
mtl_new_compute_pipeline_state(mtl_device *device, mtl_function *function,
                               uint64_t max_total_threads_per_threadgroup)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      id<MTLComputePipelineState> pipeline = NULL;
      
      MTLComputePipelineDescriptor *comp_desc = [[[MTLComputePipelineDescriptor alloc] init] autorelease];
      NSError *error;
      comp_desc.computeFunction = (id<MTLFunction>)function;
      comp_desc.maxTotalThreadsPerThreadgroup = max_total_threads_per_threadgroup;
      pipeline = [dev newComputePipelineStateWithDescriptor:comp_desc options:0 reflection:nil error:&error];

      /* TODO_KOSMICKRISP Error checking */

      return pipeline;
   }
}
