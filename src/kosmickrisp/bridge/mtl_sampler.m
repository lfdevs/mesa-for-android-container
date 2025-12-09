/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_sampler.h"

#include <Metal/MTLSampler.h>

mtl_sampler_descriptor *
mtl_new_sampler_descriptor()
{
   @autoreleasepool {
      MTLSamplerDescriptor *descriptor = [MTLSamplerDescriptor new];
      /* Set common variables we don't expose */
      descriptor.lodAverage = false;
      descriptor.supportArgumentBuffers = true;
      return descriptor;
   }
}

void
mtl_sampler_descriptor_set_normalized_coordinates(mtl_sampler_descriptor *descriptor, bool normalized_coordinates)
{
   @autoreleasepool {
      MTLSamplerDescriptor *desc = (MTLSamplerDescriptor *)descriptor;
      desc.normalizedCoordinates = normalized_coordinates;
   }
}

void
mtl_sampler_descriptor_set_address_mode(mtl_sampler_descriptor *descriptor,
                                        enum mtl_sampler_address_mode address_mode_u,
                                        enum mtl_sampler_address_mode address_mode_v,
                                        enum mtl_sampler_address_mode address_mode_w)
{
   @autoreleasepool {
      MTLSamplerDescriptor *desc = (MTLSamplerDescriptor *)descriptor;
      desc.sAddressMode = (MTLSamplerAddressMode)address_mode_u;
      desc.tAddressMode = (MTLSamplerAddressMode)address_mode_v;
      desc.rAddressMode = (MTLSamplerAddressMode)address_mode_w;
   }
}

void
mtl_sampler_descriptor_set_border_color(mtl_sampler_descriptor *descriptor, enum mtl_sampler_border_color color)
{
   @autoreleasepool {
      MTLSamplerDescriptor *desc = (MTLSamplerDescriptor *)descriptor;
      desc.borderColor = (MTLSamplerBorderColor)color;
   }
}

void
mtl_sampler_descriptor_set_filters(mtl_sampler_descriptor *descriptor,
                                   enum mtl_sampler_min_mag_filter min_filter,
                                   enum mtl_sampler_min_mag_filter mag_filter,
                                   enum mtl_sampler_mip_filter mip_filter)
{
   @autoreleasepool {
      MTLSamplerDescriptor *desc = (MTLSamplerDescriptor *)descriptor;
      desc.minFilter = (MTLSamplerMinMagFilter)min_filter;
      desc.magFilter = (MTLSamplerMinMagFilter)mag_filter;
      desc.mipFilter = (MTLSamplerMipFilter)mip_filter;
   }
}

void
mtl_sampler_descriptor_set_lod_clamp(mtl_sampler_descriptor *descriptor,
                                     float min,
                                     float max)
{
   @autoreleasepool {
      MTLSamplerDescriptor *desc = (MTLSamplerDescriptor *)descriptor;
      desc.lodMinClamp = min;
      desc.lodMaxClamp = max;
   }
}

void
mtl_sampler_descriptor_set_max_anisotropy(mtl_sampler_descriptor *descriptor,
                                          uint64_t max)
{
   @autoreleasepool {
      MTLSamplerDescriptor *desc = (MTLSamplerDescriptor *)descriptor;
      desc.maxAnisotropy = max ? max : 1u; /* Metal requires a non-zero value */
   }
}

void
mtl_sampler_descriptor_set_compare_function(mtl_sampler_descriptor *descriptor,
                                            enum mtl_compare_function function)
{
   @autoreleasepool {
      MTLSamplerDescriptor *desc = (MTLSamplerDescriptor *)descriptor;
      desc.compareFunction = (MTLCompareFunction)function;
   }
}

mtl_sampler *
mtl_new_sampler(mtl_device *device, mtl_sampler_descriptor *descriptor)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      MTLSamplerDescriptor *desc = (MTLSamplerDescriptor *)descriptor;
      return [dev newSamplerStateWithDescriptor:desc];
   }
}

uint64_t
mtl_sampler_get_gpu_resource_id(mtl_sampler *sampler)
{
   @autoreleasepool {
      id<MTLSamplerState> samp = (id<MTLSamplerState>)sampler;
      return [samp gpuResourceID]._impl;
   }
}
