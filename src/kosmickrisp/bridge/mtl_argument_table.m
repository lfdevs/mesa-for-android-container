/*
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_argument_table.h"

#include <Metal/MTL4ArgumentTable.h>
#include <Metal/MTLDevice.h>

mtl_argument_table_descriptor *
mtl_new_argument_table_descriptor()
{
   @autoreleasepool {
      return [[MTL4ArgumentTableDescriptor new] init];
   }
}

void
mtl_set_max_buffer_binding_count(mtl_argument_table_descriptor *descriptor,
                                 uint32_t count)
{
   @autoreleasepool {
      MTL4ArgumentTableDescriptor *desc = (MTL4ArgumentTableDescriptor *)descriptor;
      desc.maxBufferBindCount = count;
   }
}

/* MTLArgumentTable */
mtl_argument_table *
mtl_new_argument_table(mtl_device *device,
                       mtl_argument_table_descriptor *descriptor)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      MTL4ArgumentTableDescriptor *desc = (MTL4ArgumentTableDescriptor *)descriptor;
      return [dev newArgumentTableWithDescriptor:desc error:NULL];
   }
}

void
mtl_set_address(mtl_argument_table *table, uint64_t address, uint32_t binding)
{
   @autoreleasepool {
      id<MTL4ArgumentTable> t = (id<MTL4ArgumentTable>)table;
      [t setAddress:address atIndex:binding];
   }
}
