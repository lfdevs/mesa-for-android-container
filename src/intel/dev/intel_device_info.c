/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "intel_device_info.h"
#include "compiler/shader_enums.h"
#include "intel/common/intel_gem.h"
#include "util/bitscan.h"
#include "util/log.h"
#include "util/macros.h"

#include "drm-uapi/i915_drm.h"

static const struct {
   const char *name;
   int pci_id;
} name_map[] = {
   { "lpt", 0x27a2 },
   { "brw", 0x2a02 },
   { "g4x", 0x2a42 },
   { "ilk", 0x0042 },
   { "snb", 0x0126 },
   { "ivb", 0x016a },
   { "hsw", 0x0d2e },
   { "byt", 0x0f33 },
   { "bdw", 0x162e },
   { "chv", 0x22B3 },
   { "skl", 0x1912 },
   { "bxt", 0x5A85 },
   { "kbl", 0x5912 },
   { "aml", 0x591C },
   { "glk", 0x3185 },
   { "cfl", 0x3E9B },
   { "whl", 0x3EA1 },
   { "cml", 0x9b41 },
   { "icl", 0x8a52 },
   { "ehl", 0x4500 },
   { "jsl", 0x4E71 },
   { "tgl", 0x9a49 },
   { "rkl", 0x4c8a },
   { "dg1", 0x4905 },
   { "adl", 0x4680 },
};

/**
 * Get the PCI ID for the device name.
 *
 * Returns -1 if the device is not known.
 */
int
intel_device_name_to_pci_device_id(const char *name)
{
   for (unsigned i = 0; i < ARRAY_SIZE(name_map); i++) {
      if (!strcmp(name_map[i].name, name))
         return name_map[i].pci_id;
   }

   return -1;
}

static const struct intel_device_info intel_device_info_gfx3 = {
   .ver = 3,
   .simulator_id = -1,
   .cs_prefetch_size = 512,
};

static const struct intel_device_info intel_device_info_i965 = {
   .ver = 4,
   .has_negative_rhw_bug = true,
   .num_slices = 1,
   .num_subslices = { 1, },
   .num_eu_per_subslice = 8,
   .num_thread_per_eu = 4,
   .max_vs_threads = 16,
   .max_gs_threads = 2,
   .max_wm_threads = 8 * 4,
   .urb = {
      .size = 256,
   },
   .timestamp_frequency = 12500000,
   .simulator_id = -1,
   .cs_prefetch_size = 512,
};

static const struct intel_device_info intel_device_info_g4x = {
   .ver = 4,
   .verx10 = 45,
   .has_pln = true,
   .has_compr4 = true,
   .has_surface_tile_offset = true,
   .is_g4x = true,
   .num_slices = 1,
   .num_subslices = { 1, },
   .num_eu_per_subslice = 10,
   .num_thread_per_eu = 5,
   .max_vs_threads = 32,
   .max_gs_threads = 2,
   .max_wm_threads = 10 * 5,
   .urb = {
      .size = 384,
   },
   .timestamp_frequency = 12500000,
   .simulator_id = -1,
   .cs_prefetch_size = 512,
};

static const struct intel_device_info intel_device_info_ilk = {
   .ver = 5,
   .has_pln = true,
   .has_compr4 = true,
   .has_surface_tile_offset = true,
   .num_slices = 1,
   .num_subslices = { 1, },
   .num_eu_per_subslice = 12,
   .num_thread_per_eu = 6,
   .max_vs_threads = 72,
   .max_gs_threads = 32,
   .max_wm_threads = 12 * 6,
   .urb = {
      .size = 1024,
   },
   .timestamp_frequency = 12500000,
   .simulator_id = -1,
   .cs_prefetch_size = 512,
};

static const struct intel_device_info intel_device_info_snb_gt1 = {
   .ver = 6,
   .gt = 1,
   .has_hiz_and_separate_stencil = true,
   .has_llc = true,
   .has_pln = true,
   .has_surface_tile_offset = true,
   .needs_unlit_centroid_workaround = true,
   .num_slices = 1,
   .num_subslices = { 1, },
   .num_eu_per_subslice = 6,
   .num_thread_per_eu = 6, /* Not confirmed */
   .max_vs_threads = 24,
   .max_gs_threads = 21, /* conservative; 24 if rendering disabled. */
   .max_wm_threads = 40,
   .urb = {
      .size = 32,
      .min_entries = {
         [MESA_SHADER_VERTEX]   = 24,
      },
      .max_entries = {
         [MESA_SHADER_VERTEX]   = 256,
         [MESA_SHADER_GEOMETRY] = 256,
      },
   },
   .timestamp_frequency = 12500000,
   .simulator_id = -1,
   .cs_prefetch_size = 512,
};

static const struct intel_device_info intel_device_info_snb_gt2 = {
   .ver = 6,
   .gt = 2,
   .has_hiz_and_separate_stencil = true,
   .has_llc = true,
   .has_pln = true,
   .has_surface_tile_offset = true,
   .needs_unlit_centroid_workaround = true,
   .num_slices = 1,
   .num_subslices = { 1, },
   .num_eu_per_subslice = 12,
   .num_thread_per_eu = 6, /* Not confirmed */
   .max_vs_threads = 60,
   .max_gs_threads = 60,
   .max_wm_threads = 80,
   .urb = {
      .size = 64,
      .min_entries = {
         [MESA_SHADER_VERTEX]   = 24,
      },
      .max_entries = {
         [MESA_SHADER_VERTEX]   = 256,
         [MESA_SHADER_GEOMETRY] = 256,
      },
   },
   .timestamp_frequency = 12500000,
   .simulator_id = -1,
   .cs_prefetch_size = 512,
};

#define GFX7_FEATURES                               \
   .ver = 7,                                        \
   .has_hiz_and_separate_stencil = true,            \
   .must_use_separate_stencil = true,               \
   .has_llc = true,                                 \
   .has_pln = true,                                 \
   .has_64bit_float = true,                         \
   .has_surface_tile_offset = true,                 \
   .timestamp_frequency = 12500000,                 \
   .cs_prefetch_size = 512

static const struct intel_device_info intel_device_info_ivb_gt1 = {
   GFX7_FEATURES, .is_ivybridge = true, .gt = 1,
   .num_slices = 1,
   .num_subslices = { 1, },
   .num_eu_per_subslice = 6,
   .num_thread_per_eu = 6,
   .l3_banks = 2,
   .max_vs_threads = 36,
   .max_tcs_threads = 36,
   .max_tes_threads = 36,
   .max_gs_threads = 36,
   .max_wm_threads = 48,
   .max_cs_threads = 36,
   .urb = {
      .min_entries = {
         [MESA_SHADER_VERTEX]    = 32,
         [MESA_SHADER_TESS_EVAL] = 10,
      },
      .max_entries = {
         [MESA_SHADER_VERTEX]    = 512,
         [MESA_SHADER_TESS_CTRL] = 32,
         [MESA_SHADER_TESS_EVAL] = 288,
         [MESA_SHADER_GEOMETRY]  = 192,
      },
   },
   .simulator_id = 7,
};

static const struct intel_device_info intel_device_info_ivb_gt2 = {
   GFX7_FEATURES, .is_ivybridge = true, .gt = 2,
   .num_slices = 1,
   .num_subslices = { 1, },
   .num_eu_per_subslice = 12,
   .num_thread_per_eu = 8, /* Not sure why this isn't a multiple of
                            * @max_wm_threads ... */
   .l3_banks = 4,
   .max_vs_threads = 128,
   .max_tcs_threads = 128,
   .max_tes_threads = 128,
   .max_gs_threads = 128,
   .max_wm_threads = 172,
   .max_cs_threads = 64,
   .urb = {
      .min_entries = {
         [MESA_SHADER_VERTEX]    = 32,
         [MESA_SHADER_TESS_EVAL] = 10,
      },
      .max_entries = {
         [MESA_SHADER_VERTEX]    = 704,
         [MESA_SHADER_TESS_CTRL] = 64,
         [MESA_SHADER_TESS_EVAL] = 448,
         [MESA_SHADER_GEOMETRY]  = 320,
      },
   },
   .simulator_id = 7,
};

static const struct intel_device_info intel_device_info_byt = {
   GFX7_FEATURES, .is_baytrail = true, .gt = 1,
   .num_slices = 1,
   .num_subslices = { 1, },
   .num_eu_per_subslice = 4,
   .num_thread_per_eu = 8,
   .l3_banks = 1,
   .has_llc = false,
   .max_vs_threads = 36,
   .max_tcs_threads = 36,
   .max_tes_threads = 36,
   .max_gs_threads = 36,
   .max_wm_threads = 48,
   .max_cs_threads = 32,
   .urb = {
      .min_entries = {
         [MESA_SHADER_VERTEX]    = 32,
         [MESA_SHADER_TESS_EVAL] = 10,
      },
      .max_entries = {
         [MESA_SHADER_VERTEX]    = 512,
         [MESA_SHADER_TESS_CTRL] = 32,
         [MESA_SHADER_TESS_EVAL] = 288,
         [MESA_SHADER_GEOMETRY]  = 192,
      },
   },
   .simulator_id = 10,
};

#define HSW_FEATURES             \
   GFX7_FEATURES,                \
   .is_haswell = true,           \
   .verx10 = 75,                 \
   .supports_simd16_3src = true

static const struct intel_device_info intel_device_info_hsw_gt1 = {
   HSW_FEATURES, .gt = 1,
   .num_slices = 1,
   .num_subslices = { 1, },
   .num_eu_per_subslice = 10,
   .num_thread_per_eu = 7,
   .l3_banks = 2,
   .max_vs_threads = 70,
   .max_tcs_threads = 70,
   .max_tes_threads = 70,
   .max_gs_threads = 70,
   .max_wm_threads = 102,
   .max_cs_threads = 70,
   .urb = {
      .min_entries = {
         [MESA_SHADER_VERTEX]    = 32,
         [MESA_SHADER_TESS_EVAL] = 10,
      },
      .max_entries = {
         [MESA_SHADER_VERTEX]    = 640,
         [MESA_SHADER_TESS_CTRL] = 64,
         [MESA_SHADER_TESS_EVAL] = 384,
         [MESA_SHADER_GEOMETRY]  = 256,
      },
   },
   .simulator_id = 9,
};

static const struct intel_device_info intel_device_info_hsw_gt2 = {
   HSW_FEATURES, .gt = 2,
   .num_slices = 1,
   .num_subslices = { 2, },
   .num_eu_per_subslice = 10,
   .num_thread_per_eu = 7,
   .l3_banks = 4,
   .max_vs_threads = 280,
   .max_tcs_threads = 256,
   .max_tes_threads = 280,
   .max_gs_threads = 256,
   .max_wm_threads = 204,
   .max_cs_threads = 70,
   .urb = {
      .min_entries = {
         [MESA_SHADER_VERTEX]    = 64,
         [MESA_SHADER_TESS_EVAL] = 10,
      },
      .max_entries = {
         [MESA_SHADER_VERTEX]    = 1664,
         [MESA_SHADER_TESS_CTRL] = 128,
         [MESA_SHADER_TESS_EVAL] = 960,
         [MESA_SHADER_GEOMETRY]  = 640,
      },
   },
   .simulator_id = 9,
};

static const struct intel_device_info intel_device_info_hsw_gt3 = {
   HSW_FEATURES, .gt = 3,
   .num_slices = 2,
   .num_subslices = { 2, },
   .num_eu_per_subslice = 10,
   .num_thread_per_eu = 7,
   .l3_banks = 8,
   .max_vs_threads = 280,
   .max_tcs_threads = 256,
   .max_tes_threads = 280,
   .max_gs_threads = 256,
   .max_wm_threads = 408,
   .max_cs_threads = 70,
   .urb = {
      .min_entries = {
         [MESA_SHADER_VERTEX]    = 64,
         [MESA_SHADER_TESS_EVAL] = 10,
      },
      .max_entries = {
         [MESA_SHADER_VERTEX]    = 1664,
         [MESA_SHADER_TESS_CTRL] = 128,
         [MESA_SHADER_TESS_EVAL] = 960,
         [MESA_SHADER_GEOMETRY]  = 640,
      },
   },
   .simulator_id = 9,
};

/* It's unclear how well supported sampling from the hiz buffer is on GFX8,
 * so keep things conservative for now and set has_sample_with_hiz = false.
 */
#define GFX8_FEATURES                               \
   .ver = 8,                                        \
   .has_hiz_and_separate_stencil = true,            \
   .must_use_separate_stencil = true,               \
   .has_llc = true,                                 \
   .has_sample_with_hiz = false,                    \
   .has_pln = true,                                 \
   .has_integer_dword_mul = true,                   \
   .has_64bit_float = true,                         \
   .has_64bit_int = true,                           \
   .supports_simd16_3src = true,                    \
   .has_surface_tile_offset = true,                 \
   .num_thread_per_eu = 7,                          \
   .max_vs_threads = 504,                           \
   .max_tcs_threads = 504,                          \
   .max_tes_threads = 504,                          \
   .max_gs_threads = 504,                           \
   .max_wm_threads = 384,                           \
   .timestamp_frequency = 12500000,                 \
   .cs_prefetch_size = 512

static const struct intel_device_info intel_device_info_bdw_gt1 = {
   GFX8_FEATURES, .gt = 1,
   .is_broadwell = true,
   .num_slices = 1,
   .num_subslices = { 2, },
   .num_eu_per_subslice = 6,
   .l3_banks = 2,
   .max_cs_threads = 42,
   .urb = {
      .min_entries = {
         [MESA_SHADER_VERTEX]    = 64,
         [MESA_SHADER_TESS_EVAL] = 34,
      },
      .max_entries = {
         [MESA_SHADER_VERTEX]    = 2560,
         [MESA_SHADER_TESS_CTRL] = 504,
         [MESA_SHADER_TESS_EVAL] = 1536,
         /* Reduced from 960, seems to be similar to the bug on Gfx9 GT1. */
         [MESA_SHADER_GEOMETRY]  = 690,
      },
   },
   .simulator_id = 11,
};

static const struct intel_device_info intel_device_info_bdw_gt2 = {
   GFX8_FEATURES, .gt = 2,
   .is_broadwell = true,
   .num_slices = 1,
   .num_subslices = { 3, },
   .num_eu_per_subslice = 8,
   .l3_banks = 4,
   .max_cs_threads = 56,
   .urb = {
      .min_entries = {
         [MESA_SHADER_VERTEX]    = 64,
         [MESA_SHADER_TESS_EVAL] = 34,
      },
      .max_entries = {
         [MESA_SHADER_VERTEX]    = 2560,
         [MESA_SHADER_TESS_CTRL] = 504,
         [MESA_SHADER_TESS_EVAL] = 1536,
         [MESA_SHADER_GEOMETRY]  = 960,
      },
   },
   .simulator_id = 11,
};

static const struct intel_device_info intel_device_info_bdw_gt3 = {
   GFX8_FEATURES, .gt = 3,
   .is_broadwell = true,
   .num_slices = 2,
   .num_subslices = { 3, 3, },
   .num_eu_per_subslice = 8,
   .l3_banks = 8,
   .max_cs_threads = 56,
   .urb = {
      .min_entries = {
         [MESA_SHADER_VERTEX]    = 64,
         [MESA_SHADER_TESS_EVAL] = 34,
      },
      .max_entries = {
         [MESA_SHADER_VERTEX]    = 2560,
         [MESA_SHADER_TESS_CTRL] = 504,
         [MESA_SHADER_TESS_EVAL] = 1536,
         [MESA_SHADER_GEOMETRY]  = 960,
      },
   },
   .simulator_id = 11,
};

static const struct intel_device_info intel_device_info_chv = {
   GFX8_FEATURES, .is_cherryview = 1, .gt = 1,
   .has_llc = false,
   .has_integer_dword_mul = false,
   .num_slices = 1,
   .num_subslices = { 2, },
   .num_eu_per_subslice = 8,
   .l3_banks = 2,
   .max_vs_threads = 80,
   .max_tcs_threads = 80,
   .max_tes_threads = 80,
   .max_gs_threads = 80,
   .max_wm_threads = 128,
   .max_cs_threads = 6 * 7,
   .urb = {
      .min_entries = {
         [MESA_SHADER_VERTEX]    = 34,
         [MESA_SHADER_TESS_EVAL] = 34,
      },
      .max_entries = {
         [MESA_SHADER_VERTEX]    = 640,
         [MESA_SHADER_TESS_CTRL] = 80,
         [MESA_SHADER_TESS_EVAL] = 384,
         [MESA_SHADER_GEOMETRY]  = 256,
      },
   },
   .simulator_id = 13,
};

#define GFX9_HW_INFO                                \
   .ver = 9,                                        \
   .max_vs_threads = 336,                           \
   .max_gs_threads = 336,                           \
   .max_tcs_threads = 336,                          \
   .max_tes_threads = 336,                          \
   .max_cs_threads = 56,                            \
   .timestamp_frequency = 12000000,                 \
   .cs_prefetch_size = 512,                         \
   .urb = {                                         \
      .min_entries = {                              \
         [MESA_SHADER_VERTEX]    = 64,              \
         [MESA_SHADER_TESS_EVAL] = 34,              \
      },                                            \
      .max_entries = {                              \
         [MESA_SHADER_VERTEX]    = 1856,            \
         [MESA_SHADER_TESS_CTRL] = 672,             \
         [MESA_SHADER_TESS_EVAL] = 1120,            \
         [MESA_SHADER_GEOMETRY]  = 640,             \
      },                                            \
   }

#define GFX9_LP_FEATURES                           \
   GFX8_FEATURES,                                  \
   GFX9_HW_INFO,                                   \
   .has_integer_dword_mul = false,                 \
   .gt = 1,                                        \
   .has_llc = false,                               \
   .has_sample_with_hiz = true,                    \
   .num_slices = 1,                                \
   .num_thread_per_eu = 6,                         \
   .max_vs_threads = 112,                          \
   .max_tcs_threads = 112,                         \
   .max_tes_threads = 112,                         \
   .max_gs_threads = 112,                          \
   .max_cs_threads = 6 * 6,                        \
   .timestamp_frequency = 19200000,                \
   .urb = {                                        \
      .min_entries = {                             \
         [MESA_SHADER_VERTEX]    = 34,             \
         [MESA_SHADER_TESS_EVAL] = 34,             \
      },                                           \
      .max_entries = {                             \
         [MESA_SHADER_VERTEX]    = 704,            \
         [MESA_SHADER_TESS_CTRL] = 256,            \
         [MESA_SHADER_TESS_EVAL] = 416,            \
         [MESA_SHADER_GEOMETRY]  = 256,            \
      },                                           \
   }

#define GFX9_LP_FEATURES_3X6                       \
   GFX9_LP_FEATURES,                               \
   .num_subslices = { 3, },                        \
   .num_eu_per_subslice = 6

#define GFX9_LP_FEATURES_2X6                       \
   GFX9_LP_FEATURES,                               \
   .num_subslices = { 2, },                        \
   .num_eu_per_subslice = 6,                       \
   .max_vs_threads = 56,                           \
   .max_tcs_threads = 56,                          \
   .max_tes_threads = 56,                          \
   .max_gs_threads = 56,                           \
   .max_cs_threads = 6 * 6,                        \
   .urb = {                                        \
      .min_entries = {                             \
         [MESA_SHADER_VERTEX]    = 34,             \
         [MESA_SHADER_TESS_EVAL] = 34,             \
      },                                           \
      .max_entries = {                             \
         [MESA_SHADER_VERTEX]    = 352,            \
         [MESA_SHADER_TESS_CTRL] = 128,            \
         [MESA_SHADER_TESS_EVAL] = 208,            \
         [MESA_SHADER_GEOMETRY]  = 128,            \
      },                                           \
   }

#define GFX9_FEATURES                               \
   GFX8_FEATURES,                                   \
   GFX9_HW_INFO,                                    \
   .has_sample_with_hiz = true

static const struct intel_device_info intel_device_info_skl_gt1 = {
   GFX9_FEATURES, .gt = 1,
   .is_skylake = true,
   .num_slices = 1,
   .num_subslices = { 2, },
   .num_eu_per_subslice = 6,
   .l3_banks = 2,
   /* GT1 seems to have a bug in the top of the pipe (VF/VS?) fixed functions
    * leading to some vertices to go missing if we use too much URB.
    */
   .urb.max_entries[MESA_SHADER_VERTEX] = 928,
   .simulator_id = 12,
};

static const struct intel_device_info intel_device_info_skl_gt2 = {
   GFX9_FEATURES, .gt = 2,
   .is_skylake = true,
   .num_slices = 1,
   .num_subslices = { 3, },
   .num_eu_per_subslice = 8,
   .l3_banks = 4,
   .simulator_id = 12,
};

static const struct intel_device_info intel_device_info_skl_gt3 = {
   GFX9_FEATURES, .gt = 3,
   .is_skylake = true,
   .num_slices = 2,
   .num_subslices = { 3, 3, },
   .num_eu_per_subslice = 8,
   .l3_banks = 8,
   .simulator_id = 12,
};

static const struct intel_device_info intel_device_info_skl_gt4 = {
   GFX9_FEATURES, .gt = 4,
   .is_skylake = true,
   .num_slices = 3,
   .num_subslices = { 3, 3, 3, },
   .num_eu_per_subslice = 8,
   .l3_banks = 12,
   /* From the "L3 Allocation and Programming" documentation:
    *
    * "URB is limited to 1008KB due to programming restrictions.  This is not a
    * restriction of the L3 implementation, but of the FF and other clients.
    * Therefore, in a GT4 implementation it is possible for the programmed
    * allocation of the L3 data array to provide 3*384KB=1152KB for URB, but
    * only 1008KB of this will be used."
    */
   .simulator_id = 12,
};

static const struct intel_device_info intel_device_info_bxt = {
   GFX9_LP_FEATURES_3X6,
   .is_broxton = true,
   .l3_banks = 2,
   .simulator_id = 14,
};

static const struct intel_device_info intel_device_info_bxt_2x6 = {
   GFX9_LP_FEATURES_2X6,
   .is_broxton = true,
   .l3_banks = 1,
   .simulator_id = 14,
};
/*
 * Note: for all KBL SKUs, the PRM says SKL for GS entries, not SKL+.
 * There's no KBL entry. Using the default SKL (GFX9) GS entries value.
 */

static const struct intel_device_info intel_device_info_kbl_gt1 = {
   GFX9_FEATURES,
   .is_kabylake = true,
   .gt = 1,

   .max_cs_threads = 7 * 6,
   .num_slices = 1,
   .num_subslices = { 2, },
   .num_eu_per_subslice = 6,
   .l3_banks = 2,
   /* GT1 seems to have a bug in the top of the pipe (VF/VS?) fixed functions
    * leading to some vertices to go missing if we use too much URB.
    */
   .urb.max_entries[MESA_SHADER_VERTEX] = 928,
   .urb.max_entries[MESA_SHADER_GEOMETRY] = 256,
   .simulator_id = 16,
};

static const struct intel_device_info intel_device_info_kbl_gt1_5 = {
   GFX9_FEATURES,
   .is_kabylake = true,
   .gt = 1,

   .max_cs_threads = 7 * 6,
   .num_slices = 1,
   .num_subslices = { 3, },
   .num_eu_per_subslice = 6,
   .l3_banks = 4,
   .simulator_id = 16,
};

static const struct intel_device_info intel_device_info_kbl_gt2 = {
   GFX9_FEATURES,
   .is_kabylake = true,
   .gt = 2,

   .num_slices = 1,
   .num_subslices = { 3, },
   .num_eu_per_subslice = 8,
   .l3_banks = 4,
   .simulator_id = 16,
};

static const struct intel_device_info intel_device_info_kbl_gt3 = {
   GFX9_FEATURES,
   .is_kabylake = true,
   .gt = 3,

   .num_slices = 2,
   .num_subslices = { 3, 3, },
   .num_eu_per_subslice = 8,
   .l3_banks = 8,
   .simulator_id = 16,
};

static const struct intel_device_info intel_device_info_kbl_gt4 = {
   GFX9_FEATURES,
   .is_kabylake = true,
   .gt = 4,

   /*
    * From the "L3 Allocation and Programming" documentation:
    *
    * "URB is limited to 1008KB due to programming restrictions.  This
    *  is not a restriction of the L3 implementation, but of the FF and
    *  other clients.  Therefore, in a GT4 implementation it is
    *  possible for the programmed allocation of the L3 data array to
    *  provide 3*384KB=1152KB for URB, but only 1008KB of this
    *  will be used."
    */
   .num_slices = 3,
   .num_subslices = { 3, 3, 3, },
   .num_eu_per_subslice = 8,
   .l3_banks = 12,
   .simulator_id = 16,
};

static const struct intel_device_info intel_device_info_glk = {
   GFX9_LP_FEATURES_3X6,
   .is_geminilake = true,
   .l3_banks = 2,
   .simulator_id = 17,
};

static const struct intel_device_info intel_device_info_glk_2x6 = {
   GFX9_LP_FEATURES_2X6,
   .is_geminilake = true,
   .l3_banks = 2,
   .simulator_id = 17,
};

static const struct intel_device_info intel_device_info_cfl_gt1 = {
   GFX9_FEATURES,
   .is_coffeelake = true,
   .gt = 1,

   .num_slices = 1,
   .num_subslices = { 2, },
   .num_eu_per_subslice = 6,
   .l3_banks = 2,
   /* GT1 seems to have a bug in the top of the pipe (VF/VS?) fixed functions
    * leading to some vertices to go missing if we use too much URB.
    */
   .urb.max_entries[MESA_SHADER_VERTEX] = 928,
   .urb.max_entries[MESA_SHADER_GEOMETRY] = 256,
   .simulator_id = 24,
};
static const struct intel_device_info intel_device_info_cfl_gt2 = {
   GFX9_FEATURES,
   .is_coffeelake = true,
   .gt = 2,

   .num_slices = 1,
   .num_subslices = { 3, },
   .num_eu_per_subslice = 8,
   .l3_banks = 4,
   .simulator_id = 24,
};

static const struct intel_device_info intel_device_info_cfl_gt3 = {
   GFX9_FEATURES,
   .is_coffeelake = true,
   .gt = 3,

   .num_slices = 2,
   .num_subslices = { 3, 3, },
   .num_eu_per_subslice = 8,
   .l3_banks = 8,
   .simulator_id = 24,
};

#define subslices(args...) { args, }

#define GFX11_HW_INFO                               \
   .ver = 11,                                       \
   .has_pln = false,                                \
   .max_vs_threads = 364,                           \
   .max_gs_threads = 224,                           \
   .max_tcs_threads = 224,                          \
   .max_tes_threads = 364,                          \
   .max_cs_threads = 56,                            \
   .cs_prefetch_size = 512

#define GFX11_FEATURES(_gt, _slices, _subslices, _l3) \
   GFX8_FEATURES,                                     \
   GFX11_HW_INFO,                                     \
   .has_64bit_float = false,                          \
   .has_64bit_int = false,                            \
   .has_integer_dword_mul = false,                    \
   .has_sample_with_hiz = false,                      \
   .gt = _gt, .num_slices = _slices, .l3_banks = _l3, \
   .num_subslices = _subslices,                       \
   .num_eu_per_subslice = 8

#define GFX11_URB_MIN_MAX_ENTRIES                     \
   .min_entries = {                                   \
      [MESA_SHADER_VERTEX]    = 64,                   \
      [MESA_SHADER_TESS_EVAL] = 34,                   \
   },                                                 \
   .max_entries = {                                   \
      [MESA_SHADER_VERTEX]    = 2384,                 \
      [MESA_SHADER_TESS_CTRL] = 1032,                 \
      [MESA_SHADER_TESS_EVAL] = 2384,                 \
      [MESA_SHADER_GEOMETRY]  = 1032,                 \
   }

static const struct intel_device_info intel_device_info_icl_gt2 = {
   GFX11_FEATURES(2, 1, subslices(8), 8),
   .urb = {
      GFX11_URB_MIN_MAX_ENTRIES,
   },
   .simulator_id = 19,
};

static const struct intel_device_info intel_device_info_icl_gt1_5 = {
   GFX11_FEATURES(1, 1, subslices(6), 6),
   .urb = {
      GFX11_URB_MIN_MAX_ENTRIES,
   },
   .simulator_id = 19,
};

static const struct intel_device_info intel_device_info_icl_gt1 = {
   GFX11_FEATURES(1, 1, subslices(4), 6),
   .urb = {
      GFX11_URB_MIN_MAX_ENTRIES,
   },
   .simulator_id = 19,
};

static const struct intel_device_info intel_device_info_icl_gt0_5 = {
   GFX11_FEATURES(1, 1, subslices(1), 6),
   .urb = {
      GFX11_URB_MIN_MAX_ENTRIES,
   },
   .simulator_id = 19,
};

#define GFX11_LP_FEATURES                           \
   .is_elkhartlake = true,                          \
   .urb = {                                         \
      GFX11_URB_MIN_MAX_ENTRIES,                    \
   },                                               \
   .disable_ccs_repack = true,                      \
   .simulator_id = 28

static const struct intel_device_info intel_device_info_ehl_4x8 = {
   GFX11_FEATURES(1, 1, subslices(4), 4),
   GFX11_LP_FEATURES,
};

static const struct intel_device_info intel_device_info_ehl_4x6 = {
   GFX11_FEATURES(1, 1, subslices(4), 4),
   GFX11_LP_FEATURES,
   .num_eu_per_subslice = 6,
};

static const struct intel_device_info intel_device_info_ehl_4x5 = {
   GFX11_FEATURES(1, 1, subslices(4), 4),
   GFX11_LP_FEATURES,
   .num_eu_per_subslice = 5,
};

static const struct intel_device_info intel_device_info_ehl_4x4 = {
   GFX11_FEATURES(1, 1, subslices(4), 4),
   GFX11_LP_FEATURES,
   .num_eu_per_subslice = 4,
};

static const struct intel_device_info intel_device_info_ehl_2x8 = {
   GFX11_FEATURES(1, 1, subslices(2), 4),
   GFX11_LP_FEATURES,
};

static const struct intel_device_info intel_device_info_ehl_2x4 = {
   GFX11_FEATURES(1, 1, subslices(2), 4),
   GFX11_LP_FEATURES,
   .num_eu_per_subslice =4,
};

#define GFX12_URB_MIN_MAX_ENTRIES                   \
   .min_entries = {                                 \
      [MESA_SHADER_VERTEX]    = 64,                 \
      [MESA_SHADER_TESS_EVAL] = 34,                 \
   },                                               \
   .max_entries = {                                 \
      [MESA_SHADER_VERTEX]    = 3576,               \
      [MESA_SHADER_TESS_CTRL] = 1548,               \
      [MESA_SHADER_TESS_EVAL] = 3576,               \
      /* Wa_14013840143 */                          \
      [MESA_SHADER_GEOMETRY]  = 1536,               \
   }

#define GFX12_HW_INFO                               \
   .ver = 12,                                       \
   .has_pln = false,                                \
   .has_sample_with_hiz = false,                    \
   .has_aux_map = true,                             \
   .max_vs_threads = 546,                           \
   .max_gs_threads = 336,                           \
   .max_tcs_threads = 336,                          \
   .max_tes_threads = 546,                          \
   .max_cs_threads = 112, /* threads per DSS */     \
   .urb = {                                         \
      GFX12_URB_MIN_MAX_ENTRIES,                    \
   }

#define GFX12_FEATURES(_gt, _slices, _l3)                       \
   GFX8_FEATURES,                                               \
   GFX12_HW_INFO,                                               \
   .has_64bit_float = false,                                    \
   .has_64bit_int = false,                                      \
   .has_integer_dword_mul = false,                              \
   .gt = _gt, .num_slices = _slices, .l3_banks = _l3,           \
   .simulator_id = 22,                                          \
   .num_eu_per_subslice = 16,                                   \
   .cs_prefetch_size = 512

#define dual_subslices(args...) { args, }

#define GFX12_GT05_FEATURES                                     \
   GFX12_FEATURES(1, 1, 4),                                     \
   .num_subslices = dual_subslices(1)

#define GFX12_GT_FEATURES(_gt)                                  \
   GFX12_FEATURES(_gt, 1, _gt == 1 ? 4 : 8),                    \
   .num_subslices = dual_subslices(_gt == 1 ? 2 : 6)

static const struct intel_device_info intel_device_info_tgl_gt1 = {
   GFX12_GT_FEATURES(1),
   .is_tigerlake = true,
};

static const struct intel_device_info intel_device_info_tgl_gt2 = {
   GFX12_GT_FEATURES(2),
   .is_tigerlake = true,
};

static const struct intel_device_info intel_device_info_rkl_gt05 = {
   GFX12_GT05_FEATURES,
   .is_rocketlake = true,
};

static const struct intel_device_info intel_device_info_rkl_gt1 = {
   GFX12_GT_FEATURES(1),
   .is_rocketlake = true,
};

static const struct intel_device_info intel_device_info_adl_gt05 = {
   GFX12_GT05_FEATURES,
   .is_alderlake = true,
};

static const struct intel_device_info intel_device_info_adl_gt1 = {
   GFX12_GT_FEATURES(1),
   .is_alderlake = true,
};

static const struct intel_device_info intel_device_info_adl_gt2 = {
   GFX12_GT_FEATURES(2),
   .is_alderlake = true,
};

#define GFX12_DG1_FEATURES                      \
   GFX12_GT_FEATURES(2),                        \
   .is_dg1 = true,                              \
   .has_llc = false,                            \
   .has_local_mem = true,                       \
   .urb.size = 768,                             \
   .simulator_id = 30

UNUSED static const struct intel_device_info intel_device_info_dg1 = {
   GFX12_DG1_FEATURES,
};

static void
intel_device_info_set_eu_mask(struct intel_device_info *devinfo,
                            unsigned slice,
                            unsigned subslice,
                            unsigned eu_mask)
{
   unsigned subslice_offset = slice * devinfo->eu_slice_stride +
      subslice * devinfo->eu_subslice_stride;

   for (unsigned b_eu = 0; b_eu < devinfo->eu_subslice_stride; b_eu++) {
      devinfo->eu_masks[subslice_offset + b_eu] =
         (((1U << devinfo->num_eu_per_subslice) - 1) >> (b_eu * 8)) & 0xff;
   }
}

/* Generate slice/subslice/eu masks from number of
 * slices/subslices/eu_per_subslices in the per generation/gt intel_device_info
 * structure.
 *
 * These can be overridden with values reported by the kernel either from
 * getparam SLICE_MASK/SUBSLICE_MASK values or from the kernel version 4.17+
 * through the i915 query uapi.
 */
static void
fill_masks(struct intel_device_info *devinfo)
{
   devinfo->slice_masks = (1U << devinfo->num_slices) - 1;

   /* Subslice masks */
   unsigned max_subslices = 0;
   for (int s = 0; s < devinfo->num_slices; s++)
      max_subslices = MAX2(devinfo->num_subslices[s], max_subslices);
   devinfo->subslice_slice_stride = DIV_ROUND_UP(max_subslices, 8);

   for (int s = 0; s < devinfo->num_slices; s++) {
      devinfo->subslice_masks[s * devinfo->subslice_slice_stride] =
         (1U << devinfo->num_subslices[s]) - 1;
   }

   /* EU masks */
   devinfo->eu_subslice_stride = DIV_ROUND_UP(devinfo->num_eu_per_subslice, 8);
   devinfo->eu_slice_stride = max_subslices * devinfo->eu_subslice_stride;

   for (int s = 0; s < devinfo->num_slices; s++) {
      for (int ss = 0; ss < devinfo->num_subslices[s]; ss++) {
         intel_device_info_set_eu_mask(devinfo, s, ss,
                                     (1U << devinfo->num_eu_per_subslice) - 1);
      }
   }
}

static void
reset_masks(struct intel_device_info *devinfo)
{
   devinfo->subslice_slice_stride = 0;
   devinfo->eu_subslice_stride = 0;
   devinfo->eu_slice_stride = 0;

   devinfo->num_slices = 0;
   devinfo->num_eu_per_subslice = 0;
   memset(devinfo->num_subslices, 0, sizeof(devinfo->num_subslices));

   memset(&devinfo->slice_masks, 0, sizeof(devinfo->slice_masks));
   memset(devinfo->subslice_masks, 0, sizeof(devinfo->subslice_masks));
   memset(devinfo->eu_masks, 0, sizeof(devinfo->eu_masks));
   memset(devinfo->ppipe_subslices, 0, sizeof(devinfo->ppipe_subslices));
}

static void
update_from_topology(struct intel_device_info *devinfo,
                     const struct drm_i915_query_topology_info *topology)
{
   reset_masks(devinfo);

   devinfo->subslice_slice_stride = topology->subslice_stride;

   devinfo->eu_subslice_stride = DIV_ROUND_UP(topology->max_eus_per_subslice, 8);
   devinfo->eu_slice_stride = topology->max_subslices * devinfo->eu_subslice_stride;

   assert(sizeof(devinfo->slice_masks) >= DIV_ROUND_UP(topology->max_slices, 8));
   memcpy(&devinfo->slice_masks, topology->data, DIV_ROUND_UP(topology->max_slices, 8));
   devinfo->num_slices = __builtin_popcount(devinfo->slice_masks);

   uint32_t subslice_mask_len =
      topology->max_slices * topology->subslice_stride;
   assert(sizeof(devinfo->subslice_masks) >= subslice_mask_len);
   memcpy(devinfo->subslice_masks, &topology->data[topology->subslice_offset],
          subslice_mask_len);

   uint32_t n_subslices = 0;
   for (int s = 0; s < topology->max_slices; s++) {
      if ((devinfo->slice_masks & (1 << s)) == 0)
         continue;

      for (int b = 0; b < devinfo->subslice_slice_stride; b++) {
         devinfo->num_subslices[s] +=
            __builtin_popcount(devinfo->subslice_masks[s * devinfo->subslice_slice_stride + b]);
      }
      n_subslices += devinfo->num_subslices[s];
   }
   assert(n_subslices > 0);

   if (devinfo->ver >= 11) {
      /* On current ICL+ hardware we only have one slice. */
      assert(devinfo->slice_masks == 1);

      /* Count the number of subslices on each pixel pipe. Assume that every
       * contiguous group of 4 subslices in the mask belong to the same pixel
       * pipe.  However note that on TGL the kernel returns a mask of enabled
       * *dual* subslices instead of actual subslices somewhat confusingly, so
       * each pixel pipe only takes 2 bits in the mask even though it's still
       * 4 subslices.
       */
      const unsigned ppipe_bits = devinfo->ver >= 12 ? 2 : 4;
      for (unsigned p = 0; p < INTEL_DEVICE_MAX_PIXEL_PIPES; p++) {
         const unsigned ppipe_mask = BITFIELD_RANGE(p * ppipe_bits, ppipe_bits);
         devinfo->ppipe_subslices[p] =
            __builtin_popcount(devinfo->subslice_masks[0] & ppipe_mask);
      }
   }

   if (devinfo->ver == 12 && devinfo->num_slices == 1) {
      if (n_subslices >= 6) {
         assert(n_subslices == 6);
         devinfo->l3_banks = 8;
      } else if (n_subslices > 2) {
         devinfo->l3_banks = 6;
      } else {
         devinfo->l3_banks = 4;
      }
   }

   uint32_t eu_mask_len =
      topology->eu_stride * topology->max_subslices * topology->max_slices;
   assert(sizeof(devinfo->eu_masks) >= eu_mask_len);
   memcpy(devinfo->eu_masks, &topology->data[topology->eu_offset], eu_mask_len);

   uint32_t n_eus = 0;
   for (int b = 0; b < eu_mask_len; b++)
      n_eus += __builtin_popcount(devinfo->eu_masks[b]);

   devinfo->num_eu_per_subslice = DIV_ROUND_UP(n_eus, n_subslices);
}

static bool
update_from_masks(struct intel_device_info *devinfo, uint32_t slice_mask,
                  uint32_t subslice_mask, uint32_t n_eus)
{
   struct drm_i915_query_topology_info *topology;

   assert((slice_mask & 0xff) == slice_mask);

   size_t data_length = 100;

   topology = calloc(1, sizeof(*topology) + data_length);
   if (!topology)
      return false;

   topology->max_slices = util_last_bit(slice_mask);
   topology->max_subslices = util_last_bit(subslice_mask);

   topology->subslice_offset = DIV_ROUND_UP(topology->max_slices, 8);
   topology->subslice_stride = DIV_ROUND_UP(topology->max_subslices, 8);

   uint32_t n_subslices = __builtin_popcount(slice_mask) *
      __builtin_popcount(subslice_mask);
   uint32_t num_eu_per_subslice = DIV_ROUND_UP(n_eus, n_subslices);
   uint32_t eu_mask = (1U << num_eu_per_subslice) - 1;

   topology->eu_offset = topology->subslice_offset +
      DIV_ROUND_UP(topology->max_subslices, 8);
   topology->eu_stride = DIV_ROUND_UP(num_eu_per_subslice, 8);

   /* Set slice mask in topology */
   for (int b = 0; b < topology->subslice_offset; b++)
      topology->data[b] = (slice_mask >> (b * 8)) & 0xff;

   for (int s = 0; s < topology->max_slices; s++) {

      /* Set subslice mask in topology */
      for (int b = 0; b < topology->subslice_stride; b++) {
         int subslice_offset = topology->subslice_offset +
            s * topology->subslice_stride + b;

         topology->data[subslice_offset] = (subslice_mask >> (b * 8)) & 0xff;
      }

      /* Set eu mask in topology */
      for (int ss = 0; ss < topology->max_subslices; ss++) {
         for (int b = 0; b < topology->eu_stride; b++) {
            int eu_offset = topology->eu_offset +
               (s * topology->max_subslices + ss) * topology->eu_stride + b;

            topology->data[eu_offset] = (eu_mask >> (b * 8)) & 0xff;
         }
      }
   }

   update_from_topology(devinfo, topology);
   free(topology);

   return true;
}

static bool
getparam(int fd, uint32_t param, int *value)
{
   int tmp;

   struct drm_i915_getparam gp = {
      .param = param,
      .value = &tmp,
   };

   int ret = intel_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
   if (ret != 0)
      return false;

   *value = tmp;
   return true;
}

bool
intel_get_device_info_from_pci_id(int pci_id,
                                  struct intel_device_info *devinfo)
{
   switch (pci_id) {
#undef CHIPSET
#define CHIPSET(id, family, fam_str, name) \
      case id: *devinfo = intel_device_info_##family; break;
#include "pci_ids/i965_pci_ids.h"
#include "pci_ids/iris_pci_ids.h"

#undef CHIPSET
#define CHIPSET(id, fam_str, name) \
      case id: *devinfo = intel_device_info_gfx3; break;
#include "pci_ids/i915_pci_ids.h"

   default:
      mesa_logw("Driver does not support the 0x%x PCI ID.", pci_id);
      return false;
   }

   fill_masks(devinfo);

   /* From the Skylake PRM, 3DSTATE_PS::Scratch Space Base Pointer:
    *
    * "Scratch Space per slice is computed based on 4 sub-slices.  SW must
    *  allocate scratch space enough so that each slice has 4 slices allowed."
    *
    * The equivalent internal documentation says that this programming note
    * applies to all Gfx9+ platforms.
    *
    * The hardware typically calculates the scratch space pointer by taking
    * the base address, and adding per-thread-scratch-space * thread ID.
    * Extra padding can be necessary depending how the thread IDs are
    * calculated for a particular shader stage.
    */

   switch(devinfo->ver) {
   case 9:
      devinfo->max_wm_threads = 64 /* threads-per-PSD */
                              * devinfo->num_slices
                              * 4; /* effective subslices per slice */
      break;
   case 11:
   case 12:
      devinfo->max_wm_threads = 128 /* threads-per-PSD */
                              * devinfo->num_slices
                              * 8; /* subslices per slice */
      break;
   default:
      assert(devinfo->ver < 9);
      break;
   }

   assert(devinfo->num_slices <= ARRAY_SIZE(devinfo->num_subslices));

   if (devinfo->verx10 == 0)
      devinfo->verx10 = devinfo->ver * 10;

   devinfo->chipset_id = pci_id;
   return true;
}

const char *
intel_get_device_name(int devid)
{
   switch (devid) {
#undef CHIPSET
#define CHIPSET(id, family, fam_str, name) case id: return name " (" fam_str ")"; break;
#include "pci_ids/i965_pci_ids.h"
#include "pci_ids/iris_pci_ids.h"
   default:
      return NULL;
   }
}

/**
 * for gfx8/gfx9, SLICE_MASK/SUBSLICE_MASK can be used to compute the topology
 * (kernel 4.13+)
 */
static bool
getparam_topology(struct intel_device_info *devinfo, int fd)
{
   int slice_mask = 0;
   if (!getparam(fd, I915_PARAM_SLICE_MASK, &slice_mask))
      goto maybe_warn;

   int n_eus;
   if (!getparam(fd, I915_PARAM_EU_TOTAL, &n_eus))
      goto maybe_warn;

   int subslice_mask = 0;
   if (!getparam(fd, I915_PARAM_SUBSLICE_MASK, &subslice_mask))
      goto maybe_warn;

   return update_from_masks(devinfo, slice_mask, subslice_mask, n_eus);

 maybe_warn:
   /* Only with Gfx8+ are we starting to see devices with fusing that can only
    * be detected at runtime.
    */
   if (devinfo->ver >= 8)
      mesa_logw("Kernel 4.1 required to properly query GPU properties.");

   return false;
}

/**
 * preferred API for updating the topology in devinfo (kernel 4.17+)
 */
static bool
query_topology(struct intel_device_info *devinfo, int fd)
{
   struct drm_i915_query_item item = {
      .query_id = DRM_I915_QUERY_TOPOLOGY_INFO,
   };
   struct drm_i915_query query = {
      .num_items = 1,
      .items_ptr = (uintptr_t) &item,
   };

   if (intel_ioctl(fd, DRM_IOCTL_I915_QUERY, &query))
      return false;

   if (item.length < 0)
      return false;

   struct drm_i915_query_topology_info *topo_info =
      (struct drm_i915_query_topology_info *) calloc(1, item.length);
   item.data_ptr = (uintptr_t) topo_info;

   if (intel_ioctl(fd, DRM_IOCTL_I915_QUERY, &query) ||
       item.length <= 0)
      return false;

   update_from_topology(devinfo, topo_info);

   free(topo_info);

   return true;

}

int
intel_get_aperture_size(int fd, uint64_t *size)
{
   struct drm_i915_gem_get_aperture aperture = { 0 };

   int ret = intel_ioctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);
   if (ret == 0 && size)
      *size = aperture.aper_size;

   return ret;
}

static bool
has_get_tiling(int fd)
{
   int ret;

   struct drm_i915_gem_create gem_create = {
      .size = 4096,
   };

   if (intel_ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &gem_create)) {
      unreachable("Failed to create GEM BO");
      return false;
   }

   struct drm_i915_gem_get_tiling get_tiling = {
      .handle = gem_create.handle,
   };
   ret = intel_ioctl(fd, DRM_IOCTL_I915_GEM_SET_TILING, &get_tiling);

   struct drm_gem_close close = {
      .handle = gem_create.handle,
   };
   intel_ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close);

   return ret == 0;
}

bool
intel_get_device_info_from_fd(int fd, struct intel_device_info *devinfo)
{
   int devid = 0;

   const char *devid_override = getenv("INTEL_DEVID_OVERRIDE");
   if (devid_override && strlen(devid_override) > 0) {
      if (geteuid() == getuid()) {
         devid = intel_device_name_to_pci_device_id(devid_override);
         /* Fallback to PCI ID. */
         if (devid <= 0)
            devid = strtol(devid_override, NULL, 0);
         if (devid <= 0) {
            mesa_loge("Invalid INTEL_DEVID_OVERRIDE=\"%s\". "
                    "Use a valid numeric PCI ID or one of the supported "
                    "platform names:", devid_override);
            for (unsigned i = 0; i < ARRAY_SIZE(name_map); i++)
               mesa_loge("   %s", name_map[i].name);
            return false;
         }
      } else {
         mesa_logi("Ignoring INTEL_DEVID_OVERRIDE=\"%s\" because "
                   "real and effective user ID don't match.", devid_override);
      }
   }

   if (devid > 0) {
      if (!intel_get_device_info_from_pci_id(devid, devinfo))
         return false;
      devinfo->no_hw = true;
   } else {
      /* query the device id */
      if (!getparam(fd, I915_PARAM_CHIPSET_ID, &devid))
         return false;
      if (!intel_get_device_info_from_pci_id(devid, devinfo))
         return false;
      devinfo->no_hw = false;
   }

   if (devinfo->ver == 10) {
      mesa_loge("Gfx10 support is redacted.");
      return false;
   }

   /* remaining initializion queries the kernel for device info */
   if (devinfo->no_hw)
      return true;

   int timestamp_frequency;
   if (getparam(fd, I915_PARAM_CS_TIMESTAMP_FREQUENCY,
                &timestamp_frequency))
      devinfo->timestamp_frequency = timestamp_frequency;
   else if (devinfo->ver >= 10) {
      mesa_loge("Kernel 4.15 required to read the CS timestamp frequency.");
      return false;
   }

   if (!getparam(fd, I915_PARAM_REVISION, &devinfo->revision))
      devinfo->revision = 0;

   if (!query_topology(devinfo, fd)) {
      if (devinfo->ver >= 10) {
         /* topology uAPI required for CNL+ (kernel 4.17+) */
         return false;
      }

      /* else use the kernel 4.13+ api for gfx8+.  For older kernels, topology
       * will be wrong, affecting GPU metrics. In this case, fail silently.
       */
      getparam_topology(devinfo, fd);
   }

   intel_get_aperture_size(fd, &devinfo->aperture_bytes);
   devinfo->has_tiling_uapi = has_get_tiling(fd);

   return true;
}
