#ifndef WRAPPER_BCDEC_H
#define WRAPPER_BCDEC_H

#include "wrapper_private.h"

VkFormat
get_format_for_bcn(VkFormat bcn_format);

int
get_texel_size_for_format(VkFormat format);

int
is_emulated_bcn(struct wrapper_physical_device *pdev, VkFormat format);

void
decompress_bcn_format(void *srcBuffer,
                      void *dstBuffer,
                      int w,
                      int h,
                      VkFormat format,
                      int offset);

#endif
