/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_library.h"

#include <Metal/MTLDevice.h>

mtl_library *
mtl_new_library(mtl_device *device, const char *src)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      id<MTLLibrary> lib = NULL;
      NSString *nsstr = [NSString stringWithCString:src encoding:NSASCIIStringEncoding];
      NSError *error;
      MTLCompileOptions *comp_opts = [MTLCompileOptions new];
      comp_opts.languageVersion = MTLLanguageVersion3_2;
      comp_opts.mathMode = MTLMathModeSafe;
      comp_opts.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;
      lib = [dev newLibraryWithSource:nsstr options:comp_opts error:&error];

      if (error != nil) {
         fprintf(stderr, "Failed to create MTLLibrary: %s\n", [error.localizedDescription UTF8String]);
      }

      [comp_opts release];
      return lib;
   }
}

mtl_function *
mtl_new_function_with_name(mtl_library *lib, const char *entry_point)
{
   @autoreleasepool {
      id<MTLLibrary> mtl_lib = (id<MTLLibrary>)lib;
      NSString *ns_entry_point = [NSString stringWithCString:entry_point encoding:NSASCIIStringEncoding];
      return [mtl_lib newFunctionWithName:ns_entry_point];
   }
}

