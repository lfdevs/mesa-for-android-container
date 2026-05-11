/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_tests.h"
#include "si_tests_private.h"

#include "si_pipe.h"

static const struct debug_named_value test_options[] = {
   /* Tests: */
   {"clearbuffer", DBG(TEST_CLEAR_BUFFER), "Test correctness of the clear_buffer compute shader"},
   {"copybuffer", DBG(TEST_COPY_BUFFER), "Test correctness of the copy_buffer compute shader"},
   {"imagecopy", DBG(TEST_IMAGE_COPY), "Invoke resource_copy_region tests with images and exit."},
   {"computeblit", DBG(TEST_COMPUTE_BLIT), "Invoke blits tests and exit."},
   {"testvmfaultcp", DBG(TEST_VMFAULT_CP), "Invoke a CP VM fault test and exit."},
   {"testvmfaultshader", DBG(TEST_VMFAULT_SHADER), "Invoke a shader VM fault test and exit."},
   {"dmaperf", DBG(TEST_DMA_PERF), "Test DMA performance"},
   {"testmemperf", DBG(TEST_MEM_PERF), "Test map + memcpy perf using the winsys."},

   DEBUG_NAMED_VALUE_END /* must be last */
};

void si_run_tests(struct si_screen *sscreen)
{
   uint64_t test_flags = debug_get_flags_option("AMD_TEST", test_options, 0);

   if (test_flags & DBG(TEST_CLEAR_BUFFER))
      si_test_clear_buffer(sscreen);

   if (test_flags & DBG(TEST_COPY_BUFFER))
      si_test_copy_buffer(sscreen);

   if (test_flags & DBG(TEST_IMAGE_COPY))
      si_test_image_copy_region(sscreen);

   if (test_flags & DBG(TEST_COMPUTE_BLIT))
      si_test_blit(sscreen, test_flags);

   if (test_flags & DBG(TEST_DMA_PERF))
      si_test_dma_perf(sscreen);

   if (test_flags & DBG(TEST_MEM_PERF))
      si_test_mem_perf(sscreen);

   if (test_flags & (DBG(TEST_VMFAULT_CP) | DBG(TEST_VMFAULT_SHADER)))
      si_test_vmfault(sscreen, test_flags);

   if (test_flags)
      exit(0);
}
