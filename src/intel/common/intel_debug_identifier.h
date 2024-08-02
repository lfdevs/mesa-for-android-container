#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/**
 * \file intel_debug_identifier.h
 *
 * Debug identifier to put into the driver debug buffers. Helps figure out
 * information about the driver that generated a hang.
 */

/* Below is a list of structure located in the identifier buffer. The driver
 * can fill those in for debug purposes.
 */

enum intel_debug_block_type {
   /* End of the debug blocks */
   INTEL_DEBUG_BLOCK_TYPE_END = 1,

   /* Driver identifier (struct intel_debug_block_driver) */
   INTEL_DEBUG_BLOCK_TYPE_DRIVER,

   /* Frame identifier (struct intel_debug_block_frame) */
   INTEL_DEBUG_BLOCK_TYPE_FRAME,

   /* Internal, never to be written out */
   INTEL_DEBUG_BLOCK_TYPE_MAX,
};

struct intel_debug_block_base {
   uint32_t type; /* enum intel_debug_block_type */
   uint32_t length; /* inclusive of this structure size */
};

struct intel_debug_block_driver {
   struct intel_debug_block_base base;
   uint8_t description[];
};

struct intel_debug_block_frame {
   struct intel_debug_block_base base;
   uint64_t frame_id;
};

extern void *intel_debug_identifier(void);
extern uint32_t intel_debug_identifier_size(void);

extern uint32_t intel_debug_write_identifiers(void *output,
                                              uint32_t output_size,
                                              const char *driver_name);

extern void *intel_debug_get_identifier_block(void *buffer,
                                              uint32_t buffer_size,
                                              enum intel_debug_block_type type);

bool intel_debug_batch_in_range(uint64_t frame_id);

#ifdef __cplusplus
}
#endif
