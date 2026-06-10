/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_compiler.h"
#include "mtl_format.h"

#include <Metal/MTL4ComputePipeline.h>
#include <Metal/MTL4LibraryDescriptor.h>
#include <Metal/MTL4LibraryFunctionDescriptor.h>
#include <Metal/MTL4RenderPipeline.h>
#include <Metal/MTLDevice.h>

/* Compiler */
mtl_compiler *
mtl_new_compiler(mtl_device *device)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;

      MTL4CompilerDescriptor *desc = [[MTL4CompilerDescriptor new] autorelease];

      NSError *error;
      id<MTL4Compiler> compiler = [dev newCompilerWithDescriptor:desc error:&error];

      if (compiler == nil) {
         fprintf(stderr, "Failed to create MTL4Compiler: %s\n", [error.localizedDescription UTF8String]);
      }

      return compiler;
   }
}

/* Library */
mtl_library *
mtl_new_library(mtl_compiler *compiler, const char *src)
{
   @autoreleasepool {
      id<MTL4Compiler> comp = (id<MTL4Compiler>)compiler;
      NSString *ns_src = [NSString stringWithCString:src encoding:NSASCIIStringEncoding];

      MTLCompileOptions *opts = [[MTLCompileOptions new] autorelease];
      /* TODO_KOSMICKRISP: MTLLanguageVersion4_0 causing vertex shader timeouts in
       * dEQP-VK.spirv_assembly.instruction.graphics.float16.logical.opisnan_vector_vert
       * on M1/2 */
      opts.languageVersion = MTLLanguageVersion3_2;
      opts.mathMode = MTLMathModeSafe;
      opts.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;

      MTL4LibraryDescriptor *desc = [[MTL4LibraryDescriptor new] autorelease];
      desc.source = ns_src;
      desc.options = opts;

      NSError *error;
      id<MTLLibrary> lib = [comp newLibraryWithDescriptor:desc error:&error];

      if (lib == nil) {
         fprintf(stderr, "Failed to create MTLLibrary: %s\n", [error.localizedDescription UTF8String]);
      }

      return lib;
   }
}

mtl_function_descriptor *
mtl_new_library_function_descriptor(mtl_library *library, const char *entry_point)
{
   @autoreleasepool {
      id<MTLLibrary> lib = (id<MTLLibrary>)library;
      NSString *ns_entry_point = [NSString stringWithCString:entry_point encoding:NSASCIIStringEncoding];

      MTL4LibraryFunctionDescriptor *desc = [MTL4LibraryFunctionDescriptor new];
      desc.name = ns_entry_point;
      desc.library = lib;
      return desc;
   }
}

/* Compute pipeline */
mtl_compute_pipeline_state *
mtl_new_compute_pipeline_state(mtl_compiler *compiler,
                               mtl_function_descriptor *function,
                               uint64_t max_total_threads_per_threadgroup)
{
   @autoreleasepool {
      id<MTL4Compiler> comp = (id<MTL4Compiler>)compiler;

      MTL4ComputePipelineDescriptor *desc = [[MTL4ComputePipelineDescriptor new] autorelease];
      desc.computeFunctionDescriptor = (MTL4FunctionDescriptor *)function;
      desc.maxTotalThreadsPerThreadgroup = max_total_threads_per_threadgroup;

      NSError *error;
      id<MTLComputePipelineState> pipeline = [comp newComputePipelineStateWithDescriptor:desc compilerTaskOptions:nil
                                                                                   error:&error];

      if (pipeline == nil) {
         fprintf(stderr, "Failed to create MTLComputePipelineState: %s\n", [error.localizedDescription UTF8String]);
      }

      return pipeline;
   }
}

/* Render pipeline descriptor */
mtl_render_pipeline_descriptor *
mtl_new_render_pipeline_descriptor()
{
   @autoreleasepool {
      return [MTL4RenderPipelineDescriptor new];
   }
}

void
mtl_render_pipeline_descriptor_set_vertex_shader(mtl_render_pipeline_descriptor *descriptor,
                                                 mtl_function_descriptor *shader)
{
   @autoreleasepool {
      MTL4RenderPipelineDescriptor *desc = (MTL4RenderPipelineDescriptor *)descriptor;
      desc.vertexFunctionDescriptor = (MTL4FunctionDescriptor *)shader;
   }
}

void
mtl_render_pipeline_descriptor_set_fragment_shader(mtl_render_pipeline_descriptor *descriptor,
                                                   mtl_function_descriptor *shader)
{
   @autoreleasepool {
      MTL4RenderPipelineDescriptor *desc = (MTL4RenderPipelineDescriptor *)descriptor;
      desc.fragmentFunctionDescriptor = (MTL4FunctionDescriptor *)shader;
   }
}

void
mtl_render_pipeline_descriptor_set_input_primitive_topology(mtl_render_pipeline_descriptor *descriptor,
                                                            enum mtl_primitive_topology_class class)
{
   @autoreleasepool {
      MTL4RenderPipelineDescriptor *desc = (MTL4RenderPipelineDescriptor *)descriptor;
      desc.inputPrimitiveTopology = (MTLPrimitiveTopologyClass)class;
   }
}

void
mtl_render_pipeline_descriptor_set_color_attachment_format(mtl_render_pipeline_descriptor *descriptor,
                                                           uint8_t index,
                                                           enum mtl_pixel_format format)
{
   @autoreleasepool {
      MTL4RenderPipelineDescriptor *desc = (MTL4RenderPipelineDescriptor *)descriptor;
      desc.colorAttachments[index].pixelFormat = (MTLPixelFormat)format;
   }
}

void
mtl_render_pipeline_descriptor_set_raster_sample_count(mtl_render_pipeline_descriptor *descriptor,
                                                       uint32_t sample_count)
{
   @autoreleasepool {
      MTL4RenderPipelineDescriptor *desc = (MTL4RenderPipelineDescriptor *)descriptor;
      desc.rasterSampleCount = sample_count;
   }
}

void
mtl_render_pipeline_descriptor_set_alpha_to_coverage(mtl_render_pipeline_descriptor *descriptor,
                                                     bool enabled)
{
   @autoreleasepool {
      MTL4RenderPipelineDescriptor *desc = (MTL4RenderPipelineDescriptor *)descriptor;
      desc.alphaToCoverageState = enabled ? MTL4AlphaToCoverageStateEnabled : MTL4AlphaToCoverageStateDisabled;
   }
}

void
mtl_render_pipeline_descriptor_set_alpha_to_one(mtl_render_pipeline_descriptor *descriptor,
                                                bool enabled)
{
   @autoreleasepool {
      MTL4RenderPipelineDescriptor *desc = (MTL4RenderPipelineDescriptor *)descriptor;
      desc.alphaToOneState = enabled ? MTL4AlphaToOneStateEnabled : MTL4AlphaToOneStateDisabled;
   }
}

void
mtl_render_pipeline_descriptor_set_rasterization_enabled(mtl_render_pipeline_descriptor *descriptor,
                                                         bool enabled)
{
   @autoreleasepool {
      MTL4RenderPipelineDescriptor *desc = (MTL4RenderPipelineDescriptor *)descriptor;
      desc.rasterizationEnabled = enabled;
   }
}

void
mtl_render_pipeline_descriptor_set_max_vertex_amplification_count(mtl_render_pipeline_descriptor *descriptor,
                                                                  uint32_t count)
{
   @autoreleasepool {
      MTL4RenderPipelineDescriptor *desc = (MTL4RenderPipelineDescriptor *)descriptor;
      desc.maxVertexAmplificationCount = count;
   }
}

/* Render pipeline */
mtl_render_pipeline_state *
mtl_new_render_pipeline(mtl_compiler *compiler, mtl_render_pass_descriptor *descriptor)
{
   @autoreleasepool {
      id<MTL4Compiler> comp = (id<MTL4Compiler>)compiler;
      MTL4RenderPipelineDescriptor *desc = (MTL4RenderPipelineDescriptor *)descriptor;

      NSError *error = nil;
      id<MTLRenderPipelineState> pipeline = [comp newRenderPipelineStateWithDescriptor:desc compilerTaskOptions:nil
                                                                                 error:&error];

      if (pipeline == nil) {
         fprintf(stderr, "Failed to create MTLRenderPipelineState: %s\n", [error.localizedDescription UTF8String]);
      }

      return pipeline;
   }
}
