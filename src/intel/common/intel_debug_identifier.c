/**
 * \file intel_debug_identifier.c
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "git_sha1.h"

#include "common/intel_debug_identifier.h"
#include "dev/intel_debug.h"
#include "util/macros.h"
#include "util/u_math.h"

static uint64_t debug_identifier[4] = {
   0xffeeddccbbaa9988,
   0x7766554433221100,
   0xffeeddccbbaa9988,
   0x7766554433221100,
};

void *
intel_debug_identifier(void)
{
   return debug_identifier;
}

uint32_t
intel_debug_identifier_size(void)
{
   return sizeof(debug_identifier);
}

uint32_t
intel_debug_write_identifiers(void *_output,
                              uint32_t output_size,
                              const char *driver_name)
{
   void *output = _output, *output_end = _output + output_size;

   assert(output_size > intel_debug_identifier_size());

   memcpy(output, intel_debug_identifier(), intel_debug_identifier_size());
   output += intel_debug_identifier_size();

   for (uint32_t id = INTEL_DEBUG_BLOCK_TYPE_DRIVER; id < INTEL_DEBUG_BLOCK_TYPE_MAX; id++) {
      switch (id) {
      case INTEL_DEBUG_BLOCK_TYPE_DRIVER: {
         struct intel_debug_block_driver driver_desc = {
            .base = {
               .type = id,
            },
         };
         int len = snprintf(output + sizeof(driver_desc),
                            output_end - (output + sizeof(driver_desc)),
                            "%s " PACKAGE_VERSION " build " MESA_GIT_SHA1,
                            driver_name);
         driver_desc.base.length = sizeof(driver_desc) + len + 1;
         memcpy(output, &driver_desc, sizeof(driver_desc));
         output += driver_desc.base.length;
         break;
      }

      case INTEL_DEBUG_BLOCK_TYPE_FRAME: {
         struct intel_debug_block_frame frame_desc = {
            .base = {
               .type = INTEL_DEBUG_BLOCK_TYPE_FRAME,
               .length = sizeof(frame_desc),
            },
         };
         memcpy(output, &frame_desc, sizeof(frame_desc));
         output += sizeof(frame_desc);
         break;
      }

      default:
         UNREACHABLE("Missing identifier write");
      }

      assert(output < output_end);
   }

   struct intel_debug_block_base end = {
      .type = INTEL_DEBUG_BLOCK_TYPE_END,
      .length = sizeof(end),
   };
   memcpy(output, &end, sizeof(end));
   output += sizeof(end);

   assert(output < output_end);

   /* Add at least a full aligned uint64_t of zero padding at the end
    * to make the identifiers easier to spot.
    */
   const unsigned unpadded_len = output - _output;
   const unsigned padding = align(unpadded_len + 8, 8) - unpadded_len;
   memset(output, 0, padding);
   output += padding;

   assert(output < output_end);

   /* Return the how many bytes where written, so that the rest of the buffer
    * can be used for other things.
    */
   return output - _output;
}

void *
intel_debug_get_identifier_block(void *_buffer,
                                 uint32_t buffer_size,
                                 enum intel_debug_block_type type)
{
   void *buffer = _buffer + intel_debug_identifier_size(),
      *end_buffer = _buffer + buffer_size;

   while (buffer < end_buffer) {
      struct intel_debug_block_base item;
      memcpy(&item, buffer, sizeof(item));

      if (item.type == type)
         return buffer;
      if (item.type == INTEL_DEBUG_BLOCK_TYPE_END)
         return NULL;

      buffer += item.length;
   }

   return NULL;
}

/**
 * Check if in valid frame range for batch dumping
 */
bool
intel_debug_batch_in_range(uint64_t frame_id)
{
   return frame_id >= intel_debug_batch_frame_start &&
          frame_id < intel_debug_batch_frame_stop;
}
