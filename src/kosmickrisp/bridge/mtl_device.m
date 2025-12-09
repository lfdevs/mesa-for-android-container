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
   mtl_device *device = 0u;

   @autoreleasepool {
      NSArray<id<MTLDevice>> *devs = [MTLCopyAllDevices() autorelease];
      uint32_t device_count = [devs count];
      
      for (uint32_t i = 0u; i < device_count; ++i) {
         if (@available(macOS 10.15, *)) {
            if (!device && [devs[i] supportsFamily:MTLGPUFamilyMetal3]) {
               device = (mtl_device *)[devs[i] retain];
               break;
            }
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
mtl_device_get_peer_group_id(mtl_device *dev)
{
   @autoreleasepool {
      id<MTLDevice> device = (id<MTLDevice>)dev;
      return device.peerGroupID;
   }
}

uint32_t
mtl_device_get_peer_index(mtl_device *dev)
{
   @autoreleasepool {
      id<MTLDevice> device = (id<MTLDevice>)dev;
      return device.peerIndex;
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

void
mtl_heap_texture_size_and_align_with_descriptor(mtl_device *device,
                                                struct kk_image_layout *layout)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      if (layout->optimized_layout) {
         MTLTextureDescriptor *descriptor = [mtl_new_texture_descriptor(layout) autorelease];
         descriptor.resourceOptions = KK_MTL_RESOURCE_OPTIONS;
         MTLSizeAndAlign size_align = [dev heapTextureSizeAndAlignWithDescriptor:descriptor];
         layout->size_B = size_align.size;
         layout->align_B = size_align.align;
      } else {
         /* Linear textures have different alignment since they are allocated on top of MTLBuffers */
         layout->align_B = [dev minimumLinearTextureAlignmentForPixelFormat:layout->format.mtl];
      }
   }
}
