/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_heap.h"

/* TODO_KOSMICKRISP Remove */
#include "kk_private.h"
#include "kk_image_layout.h"

#include <Metal/MTLHeap.h>

/* Creation */
mtl_heap *
mtl_new_heap(mtl_device *device, uint64_t size,
             enum mtl_resource_options resource_options)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      MTLHeapDescriptor *descriptor = [[MTLHeapDescriptor new] autorelease];
      descriptor.type = MTLHeapTypePlacement;
      descriptor.resourceOptions = (MTLResourceOptions)resource_options;
      descriptor.size = size;
      descriptor.sparsePageSize = MTLSparsePageSize16;
      return [dev newHeapWithDescriptor:descriptor];
   }
}

/* Utils */
uint64_t
mtl_heap_get_size(mtl_heap *heap)
{
   @autoreleasepool {
      id<MTLHeap> hp = (id<MTLHeap>)heap;
      return hp.size;
   }
}

static MTLTextureDescriptor *
mtl_new_texture_descriptor(const struct kk_image_layout *layout)
{
   @autoreleasepool {
      MTLTextureDescriptor *descriptor = [MTLTextureDescriptor new];
      descriptor.textureType = (MTLTextureType)layout->type;
      descriptor.pixelFormat = layout->format.mtl;
      descriptor.width = layout->width_px;
      descriptor.height = layout->height_px;
      descriptor.depth = layout->depth_px;
      descriptor.mipmapLevelCount = layout->levels;
      descriptor.sampleCount = layout->sample_count_sa;
      descriptor.arrayLength = layout->layers;
      descriptor.allowGPUOptimizedContents = layout->optimized_layout;
      descriptor.usage = (MTLTextureUsage)layout->usage;
      /* We don't set the swizzle because Metal complains when the usage has store or render target with swizzle... */
      
      return descriptor;
   }
}

/* Allocation from heap */
mtl_buffer *
mtl_new_buffer_with_length(mtl_heap *heap, uint64_t size_B, uint64_t offset_B)
{
   @autoreleasepool {
      id<MTLHeap> hp = (id<MTLHeap>)heap;
      return (mtl_buffer *)[hp newBufferWithLength:size_B options:KK_MTL_RESOURCE_OPTIONS offset:offset_B];
   }
}

mtl_texture *
mtl_new_texture_with_descriptor(mtl_heap *heap,
                                const struct kk_image_layout *layout,
                                uint64_t offset)
{
   @autoreleasepool {
      id<MTLHeap> hp = (id<MTLHeap>)heap;
      MTLTextureDescriptor *descriptor = [mtl_new_texture_descriptor(layout) autorelease];
      descriptor.resourceOptions = hp.resourceOptions;
      return [hp newTextureWithDescriptor:descriptor offset:offset];
   }
}
