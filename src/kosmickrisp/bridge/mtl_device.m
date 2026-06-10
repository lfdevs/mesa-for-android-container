/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_device.h"

/* TODO_KOSMICKRISP Remove */
#include "kk_image_layout.h"
#include "kk_private.h"

#include <Metal/MTLDevice.h>
#include <Metal/MTLCaptureManager.h>

/* Device creation */
mtl_device *
mtl_device_create()
{
   mtl_device *device = nil;

   @autoreleasepool {
      NSArray<id<MTLDevice>> *devs = [MTLCopyAllDevices() autorelease];
      uint32_t device_count = [devs count];
      
      for (uint32_t i = 0u; i < device_count; ++i) {
         if ([devs[i] supportsFamily:MTLGPUFamilyMetal4]) {
            device = (mtl_device *)[devs[i] retain];
            break;
         }
      }
   }

   return device;
}

/* Device operations */
void
mtl_start_gpu_capture(mtl_device *mtl_dev_handle, const char *directory)
{
   @autoreleasepool {
      id<MTLDevice> mtl_dev = (id<MTLDevice>)mtl_dev_handle;
      MTLCaptureManager *captureMgr = [MTLCaptureManager sharedCaptureManager];

      MTLCaptureDescriptor *captureDesc = [[MTLCaptureDescriptor new] autorelease];
      captureDesc.captureObject = mtl_dev;
      captureDesc.destination = MTLCaptureDestinationDeveloperTools;

      if (directory && [captureMgr supportsDestination: MTLCaptureDestinationGPUTraceDocument]) {
         NSString *dir = [NSString stringWithUTF8String:directory];
         NSString *pname = [[NSProcessInfo processInfo] processName];
         NSString *capture_path = [NSString stringWithFormat:@"%@/%@.gputrace", dir, pname];
         captureDesc.destination = MTLCaptureDestinationGPUTraceDocument;
         captureDesc.outputURL = [NSURL fileURLWithPath: capture_path];
      }

      NSError *err = nil;
      if (![captureMgr startCaptureWithDescriptor:captureDesc error:&err]) {
         fprintf(stderr, "Failed to automatically start GPU capture session (Error code %li) using startCaptureWithDescriptor: %s\n",
                 (long)err.code, err.localizedDescription.UTF8String);
      }
   }
}

void
mtl_stop_gpu_capture()
{
   @autoreleasepool {
      [[MTLCaptureManager sharedCaptureManager] stopCapture];
   }
}

/* Device feature query */
void
mtl_device_get_name(mtl_device *dev, char buffer[256])
{
   @autoreleasepool {
      id<MTLDevice> device = (id<MTLDevice>)dev;
      [device.name getCString:buffer maxLength:(sizeof(char) * 256) encoding:NSUTF8StringEncoding];
   }
}

void
mtl_device_get_architecture_name(mtl_device *dev, char buffer[256])
{
   @autoreleasepool {
      id<MTLDevice> device = (id<MTLDevice>)dev;
      [device.architecture.name getCString:buffer maxLength:(sizeof(char) * 256) encoding:NSUTF8StringEncoding];
   }
}

uint64_t
mtl_device_get_registry_id(mtl_device *dev)
{
   @autoreleasepool {
      id<MTLDevice> device = (id<MTLDevice>)dev;
      return device.registryID;
   }
}

bool
mtl_device_supports_sample_count(mtl_device *dev, uint32_t sample_count)
{
   @autoreleasepool {
      id<MTLDevice> device = (id<MTLDevice>)dev;
      return [device supportsTextureSampleCount:sample_count];
   }
}

struct mtl_size
mtl_device_max_threads_per_threadgroup(mtl_device *dev)
{
   @autoreleasepool {
      id<MTLDevice> device = (id<MTLDevice>)dev;
      return (struct mtl_size){.x = device.maxThreadsPerThreadgroup.width,
                               .y = device.maxThreadsPerThreadgroup.height,
                               .z = device.maxThreadsPerThreadgroup.depth};
   }
}

uint32_t
mtl_device_max_threadgroup_memory_length(mtl_device *dev)
{
   @autoreleasepool {
      id<MTLDevice> device = (id<MTLDevice>)dev;
      return device.maxThreadgroupMemoryLength;
   }
}

uint64_t
mtl_device_max_buffer_length(mtl_device *dev)
{
   @autoreleasepool {
      id<MTLDevice> device = (id<MTLDevice>)dev;
      return device.maxBufferLength;
   }
}

uint64_t
mtl_device_recommended_max_working_set_size(mtl_device *dev)
{
   @autoreleasepool {
      id<MTLDevice> device = (id<MTLDevice>)dev;
      return device.recommendedMaxWorkingSetSize;
   }
}

uint64_t
mtl_device_current_allocated_size(mtl_device *dev)
{
   @autoreleasepool {
      id<MTLDevice> device = (id<MTLDevice>)dev;
      return device.currentAllocatedSize;
   }
}

/* Timestamp query */
uint64_t
mtl_device_get_gpu_timestamp(mtl_device *dev)
{
   @autoreleasepool {
      id<MTLDevice> device = (id<MTLDevice>)dev;
      MTLTimestamp cpu_ts, gpu_ts;

      [device sampleTimestamps:&cpu_ts gpuTimestamp:&gpu_ts];

      return (uint64_t)gpu_ts;
   }
}

/* Resource queries */
/* TODO_KOSMICKRISP Return a struct */
void
mtl_heap_buffer_size_and_align_with_length(mtl_device *device, uint64_t *size_B,
                                           uint64_t *align_B)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      MTLSizeAndAlign size_align = [dev heapBufferSizeAndAlignWithLength:*size_B options:KK_MTL_RESOURCE_OPTIONS];
      *size_B = size_align.size;
      *align_B = size_align.align;
   }
}

/* TODO_KOSMICKRISP Remove */
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

uint64_t
mtl_minimum_linear_texture_alignment_for_pixel_format(
   mtl_device *device, enum mtl_pixel_format format)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      return [dev minimumLinearTextureAlignmentForPixelFormat:(MTLPixelFormat)format];
   }
}

void
mtl_heap_texture_size_and_align_with_descriptor(mtl_device *device,
                                                struct kk_image_layout *layout,
                                                uint64_t *size_B,
                                                uint64_t *align_B)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      MTLTextureDescriptor *descriptor = [mtl_new_texture_descriptor(layout) autorelease];
      descriptor.resourceOptions = KK_MTL_RESOURCE_OPTIONS;

      MTLSizeAndAlign size_align = [dev heapTextureSizeAndAlignWithDescriptor:descriptor];
      if (size_B)
         *size_B = size_align.size;
      if (align_B)
         *align_B = size_align.align;
   }
}

uint32_t
mtl_sparse_tile_size_in_bytes(mtl_device *device) {
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      return [dev sparseTileSizeInBytes];
   }
}

struct mtl_size
mtl_sparse_tile_size(mtl_device *device, struct kk_image_layout *layout) {
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      MTLSize tile_size = [dev sparseTileSizeWithTextureType:(MTLTextureType)layout->type
                                                 pixelFormat:layout->format.mtl
                                                 sampleCount:layout->sample_count_sa];
      return (struct mtl_size){tile_size.width, tile_size.height, tile_size.depth};
   }
}

struct mtl_size
mtl_sparse_tile_count(mtl_device *device, struct kk_image_layout *layout,
                      struct mtl_size tile_size) {
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      MTLRegion pixel_region = MTLRegionMake3D(
          0, 0, 0, layout->width_px, layout->height_px, layout->depth_px);
      MTLRegion tile_region;
      [dev convertSparsePixelRegions:&pixel_region
                       toTileRegions:&tile_region
                        withTileSize:MTLSizeMake(tile_size.x, tile_size.y, tile_size.z)
                       alignmentMode:MTLSparseTextureRegionAlignmentModeOutward
                          numRegions:1];

      return (struct mtl_size){tile_region.size.width, tile_region.size.height,
                               tile_region.size.depth};
   }
}

/* Resource creation */
mtl_buffer *
mtl_new_buffer_with_bytes_no_copy(mtl_device *device, void* ptr,
                                  uint64_t size_B)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      return [dev newBufferWithBytesNoCopy:ptr length:size_B options:KK_MTL_RESOURCE_OPTIONS deallocator:nil];
   }
}
