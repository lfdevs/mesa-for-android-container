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

#include "util/libdrm.h"

#include "intel_device_info.h"
#include "intel_hwconfig.h"
#include "intel_wa.h"
#include "i915/intel_device_info.h"
#include "xe/intel_device_info.h"

#include "common/intel_gem.h"
#include "util/u_debug.h"
#include "util/log.h"
#include "util/macros.h"

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
   { "ehl", 0x4571 },
   { "jsl", 0x4E71 },
   { "tgl", 0x9a49 },
   { "rkl", 0x4c8a },
   { "dg1", 0x4905 },
   { "adl", 0x4680 },
   { "sg1", 0x4907 },
   { "rpl", 0xa780 },
   { "dg2", 0x5690 },
   { "mtl", 0x7d60 },
   { "arl", 0x7d67 },
   { "lnl", 0x64a0 },
   { "bmg", 0xe202 },
   { "ptl", 0xb080 },
   { "nvl-u", 0xd740 },
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
   .platform = INTEL_PLATFORM_GFX3,
   .simulator_id = -1,
   .num_slices = 1,
   .num_subslices = { 1, },
   .max_eus_per_subslice = 8,
   .num_thread_per_eu = 4,
   .grf_size = 32,
   .timestamp_frequency = 12500000,
};

static const struct intel_device_info intel_device_info_i965 = {
   .ver = 4,
   .platform = INTEL_PLATFORM_I965,
   .num_slices = 1,
   .num_subslices = { 1, },
   .max_eus_per_subslice = 8,
   .num_thread_per_eu = 4,
   .grf_size = 32,
   .max_vs_threads = 16,
   .max_gs_threads = 2,
   .max_wm_threads = 8 * 4,
   .urb = {
      .size = 256,
   },
   .timestamp_frequency = 12500000,
   .simulator_id = -1,
};

static const struct intel_device_info intel_device_info_g4x = {
   .ver = 4,
   .verx10 = 45,
   .has_pln = true,
   .platform = INTEL_PLATFORM_G4X,
   .num_slices = 1,
   .num_subslices = { 1, },
   .max_eus_per_subslice = 10,
   .num_thread_per_eu = 5,
   .grf_size = 32,
   .max_vs_threads = 32,
   .max_gs_threads = 2,
   .max_wm_threads = 10 * 5,
   .urb = {
      .size = 384,
   },
   .timestamp_frequency = 12500000,
   .simulator_id = -1,
};

static const struct intel_device_info intel_device_info_ilk = {
   .ver = 5,
   .platform = INTEL_PLATFORM_ILK,
   .has_pln = true,
   .num_slices = 1,
   .num_subslices = { 1, },
   .max_eus_per_subslice = 12,
   .num_thread_per_eu = 6,
   .grf_size = 32,
   .max_vs_threads = 72,
   .max_gs_threads = 32,
   .max_wm_threads = 12 * 6,
   .urb = {
      .size = 1024,
   },
   .timestamp_frequency = 12500000,
   .simulator_id = -1,
};

static const struct intel_device_info intel_device_info_snb_gt1 = {
   .ver = 6,
   .gt = 1,
   .platform = INTEL_PLATFORM_SNB,
   .has_hiz_and_separate_stencil = true,
   .has_llc = true,
   .has_pln = true,
   .num_slices = 1,
   .num_subslices = { 1, },
   .max_eus_per_subslice = 6,
   .num_thread_per_eu = 6, /* Not confirmed */
   .grf_size = 32,
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
};

static const struct intel_device_info intel_device_info_snb_gt2 = {
   .ver = 6,
   .gt = 2,
   .platform = INTEL_PLATFORM_SNB,
   .has_hiz_and_separate_stencil = true,
   .has_llc = true,
   .has_pln = true,
   .num_slices = 1,
   .num_subslices = { 1, },
   .max_eus_per_subslice = 12,
   .num_thread_per_eu = 6, /* Not confirmed */
   .grf_size = 32,
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
};

#define GFX7_FEATURES                               \
   .ver = 7,                                        \
   .has_hiz_and_separate_stencil = true,            \
   .has_llc = true,                                 \
   .has_pln = true,                                 \
   .has_64bit_float = true,                         \
   .grf_size = 32,                                  \
   .timestamp_frequency = 12500000,                 \
   .max_constant_urb_size_kb = 16

static const struct intel_device_info intel_device_info_ivb_gt1 = {
   GFX7_FEATURES, .platform = INTEL_PLATFORM_IVB, .gt = 1,
   .num_slices = 1,
   .num_subslices = { 1, },
   .max_eus_per_subslice = 6,
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
   GFX7_FEATURES, .platform = INTEL_PLATFORM_IVB, .gt = 2,
   .num_slices = 1,
   .num_subslices = { 1, },
   .max_eus_per_subslice = 12,
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
   GFX7_FEATURES, .platform = INTEL_PLATFORM_BYT, .gt = 1,
   .num_slices = 1,
   .num_subslices = { 1, },
   .max_eus_per_subslice = 4,
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

#define HSW_FEATURES \
   GFX7_FEATURES, \
   .platform = INTEL_PLATFORM_HSW, \
   .verx10 = 75, \
   .supports_simd16_3src = true

static const struct intel_device_info intel_device_info_hsw_gt1 = {
   HSW_FEATURES, .gt = 1,
   .num_slices = 1,
   .num_subslices = { 1, },
   .max_eus_per_subslice = 10,
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
   .max_eus_per_subslice = 10,
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
   .num_subslices = { 2, 2, },
   .max_eus_per_subslice = 10,
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
   .max_constant_urb_size_kb = 32,
   .simulator_id = 9,
};

/* It's unclear how well supported sampling from the hiz buffer is on GFX8,
 * so keep things conservative for now and set has_sample_with_hiz = false.
 */
#define GFX8_FEATURES                               \
   .ver = 8,                                        \
   .has_hiz_and_separate_stencil = true,            \
   .has_llc = true,                                 \
   .has_sample_with_hiz = false,                    \
   .has_pln = true,                                 \
   .has_integer_dword_mul = true,                   \
   .has_64bit_float = true,                         \
   .has_64bit_int = true,                           \
   .supports_simd16_3src = true,                    \
   .num_thread_per_eu = 7,                          \
   .grf_size = 32,                                  \
   .timestamp_frequency = 12500000,                 \
   .max_constant_urb_size_kb = 32

#define GFX8_MAX_THREADS                            \
   .max_vs_threads = 504,                           \
   .max_tcs_threads = 504,                          \
   .max_tes_threads = 504,                          \
   .max_gs_threads = 504,                           \
   .max_wm_threads = 384,                           \
   .max_threads_per_psd = 64

/* We always use SIMD8 geometry shaders on GFX8+.  The Broadwell
 * 3DSTATE_GS docs include a note:
 *
 *   "At least 8 URB entries must be allocated in order to use
 *    SIMD8 DispatchMode."
 */
#define GFX8_URB_MIN_MAX_ENTRIES                      \
   .urb = {                                           \
      .min_entries = {                                \
         [MESA_SHADER_VERTEX]    = 64,                \
         [MESA_SHADER_TESS_EVAL] = 34,                \
         [MESA_SHADER_GEOMETRY]  = 8,                 \
      },                                              \
      .max_entries = {                                \
         [MESA_SHADER_VERTEX]    = 2560,              \
         [MESA_SHADER_TESS_CTRL] = 504,               \
         [MESA_SHADER_TESS_EVAL] = 1536,              \
         [MESA_SHADER_GEOMETRY]  = 960,               \
      },                                              \
   }

#define BDW_CONFIG                                            \
   GFX8_FEATURES, GFX8_MAX_THREADS, GFX8_URB_MIN_MAX_ENTRIES, \
   .platform = INTEL_PLATFORM_BDW,                            \
   .simulator_id = 11

static const struct intel_device_info intel_device_info_bdw_gt1 = {
   BDW_CONFIG, .gt = 1,
   .num_slices = 1,
   .num_subslices = { 2, },
   .max_eus_per_subslice = 6,
   .l3_banks = 2,
   .max_cs_threads = 42,
   /* Reduced from 960, seems to be similar to the bug on Gfx9 GT1. */
   .urb.max_entries[MESA_SHADER_GEOMETRY] = 690,
};

static const struct intel_device_info intel_device_info_bdw_gt2 = {
   BDW_CONFIG, .gt = 2,
   .num_slices = 1,
   .num_subslices = { 3, },
   .max_eus_per_subslice = 8,
   .l3_banks = 4,
   .max_cs_threads = 56,
};

static const struct intel_device_info intel_device_info_bdw_gt3 = {
   BDW_CONFIG, .gt = 3,
   .num_slices = 2,
   .num_subslices = { 3, 3, },
   .max_eus_per_subslice = 8,
   .l3_banks = 8,
   .max_cs_threads = 56,
};

static const struct intel_device_info intel_device_info_chv = {
   GFX8_FEATURES, .platform = INTEL_PLATFORM_CHV, .gt = 1,
   .has_llc = false,
   .has_integer_dword_mul = false,
   .num_slices = 1,
   .num_subslices = { 2, },
   .max_eus_per_subslice = 8,
   .l3_banks = 2,
   .max_vs_threads = 80,
   .max_tcs_threads = 80,
   .max_tes_threads = 80,
   .max_gs_threads = 80,
   .max_wm_threads = 128,
   .max_cs_threads = 6 * 7,
   .max_threads_per_psd = 64,
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

#define CMAT_PRE_XEHP_CONFIGURATIONS                                                                                            \
   .cooperative_matrix_configurations = {                                                                                       \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 8, 16, INTEL_CMAT_FLOAT16, INTEL_CMAT_FLOAT16, INTEL_CMAT_FLOAT16, INTEL_CMAT_FLOAT16 },    \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 8, 16, INTEL_CMAT_FLOAT16, INTEL_CMAT_FLOAT16, INTEL_CMAT_FLOAT32, INTEL_CMAT_FLOAT32 },    \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 8, 32, INTEL_CMAT_SINT8, INTEL_CMAT_SINT8, INTEL_CMAT_SINT32, INTEL_CMAT_SINT32 },          \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 8, 32, INTEL_CMAT_UINT8, INTEL_CMAT_UINT8, INTEL_CMAT_UINT32, INTEL_CMAT_UINT32 },          \
   }

#define CMAT_XEHP_CONFIGURATIONS                                                                                                \
   .cooperative_matrix_configurations = {                                                                                       \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 8, 16, INTEL_CMAT_FLOAT16, INTEL_CMAT_FLOAT16, INTEL_CMAT_FLOAT32, INTEL_CMAT_FLOAT32 },    \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 8, 16, INTEL_CMAT_BFLOAT16, INTEL_CMAT_BFLOAT16, INTEL_CMAT_FLOAT32, INTEL_CMAT_FLOAT32 },  \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 8, 32, INTEL_CMAT_SINT8, INTEL_CMAT_SINT8, INTEL_CMAT_SINT32, INTEL_CMAT_SINT32 },          \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 8, 32, INTEL_CMAT_UINT8, INTEL_CMAT_UINT8, INTEL_CMAT_UINT32, INTEL_CMAT_UINT32 },          \
   }

#define CMAT_XE2_CONFIGURATIONS                                                                                                 \
   .cooperative_matrix_configurations = {                                                                                       \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 16, 16, INTEL_CMAT_FLOAT16, INTEL_CMAT_FLOAT16, INTEL_CMAT_FLOAT16, INTEL_CMAT_FLOAT16 },   \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 16, 16, INTEL_CMAT_FLOAT16, INTEL_CMAT_FLOAT16, INTEL_CMAT_FLOAT32, INTEL_CMAT_FLOAT32 },   \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 16, 16, INTEL_CMAT_BFLOAT16, INTEL_CMAT_BFLOAT16, INTEL_CMAT_BFLOAT16, INTEL_CMAT_BFLOAT16 }, \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 16, 16, INTEL_CMAT_BFLOAT16, INTEL_CMAT_BFLOAT16, INTEL_CMAT_FLOAT32, INTEL_CMAT_FLOAT32 }, \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 16, 32, INTEL_CMAT_SINT8, INTEL_CMAT_SINT8, INTEL_CMAT_SINT32, INTEL_CMAT_SINT32 },         \
    { INTEL_CMAT_SCOPE_SUBGROUP, 8, 16, 32, INTEL_CMAT_UINT8, INTEL_CMAT_UINT8, INTEL_CMAT_UINT32, INTEL_CMAT_UINT32 },         \
   }

#define GFX9_FEATURES                               \
   GFX8_FEATURES,                                   \
   CMAT_PRE_XEHP_CONFIGURATIONS,                    \
   .ver = 9,                                        \
   .has_sample_with_hiz = true,                     \
   .timestamp_frequency = 12000000

#define GFX9_MAX_THREADS                            \
   .max_vs_threads = 336,                           \
   .max_gs_threads = 336,                           \
   .max_tcs_threads = 336,                          \
   .max_tes_threads = 336,                          \
   .max_wm_threads = 0,                             \
   .max_threads_per_psd = 64,                       \
   .max_cs_threads = 56                             \

/* On Skylake through Xe, the 3DSTATE_GS_BODY docs include a note:
 *
 *   "The driver must send pipe control with a cs stall after a
 *    3dstate_gs state change and the Dispatch Mode is simd8 and
 *    the number of handles allocated to gs is less than 16."
 *
 * We don't implement this PIPE_CONTROL, so instead we always allocate
 * at least 16 URB handles for GS when it's enabled.
 */
#define GFX9_URB_MIN_MAX_ENTRIES                    \
   .urb = {                                         \
      .min_entries = {                              \
         [MESA_SHADER_VERTEX]    = 64,              \
         [MESA_SHADER_TESS_EVAL] = 34,              \
         [MESA_SHADER_GEOMETRY]  = 16,              \
      },                                            \
      .max_entries = {                              \
         [MESA_SHADER_VERTEX]    = 1856,            \
         [MESA_SHADER_TESS_CTRL] = 672,             \
         [MESA_SHADER_TESS_EVAL] = 1120,            \
         [MESA_SHADER_GEOMETRY]  = 640,             \
      },                                            \
   }

#define GFX9_LP_CONFIG_BASE                        \
   GFX8_FEATURES,                                  \
   .ver = 9,                                       \
   .gt = 1,                                        \
   .has_integer_dword_mul = false,                 \
   .has_llc = false,                               \
   .has_sample_with_hiz = true,                    \
   .num_slices = 1,                                \
   .num_thread_per_eu = 6,                         \
   .max_eus_per_subslice = 6,                      \
   .max_threads_per_psd = 64,                      \
   .timestamp_frequency = 19200000

#define GFX9_LP_CONFIG_3X6                         \
   GFX9_LP_CONFIG_BASE,                            \
   .num_subslices = { 3, },                        \
   .max_vs_threads = 112,                          \
   .max_tcs_threads = 112,                         \
   .max_tes_threads = 112,                         \
   .max_gs_threads = 112,                          \
   .max_cs_threads = 6 * 6,                        \
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

#define GFX9_LP_CONFIG_2X6                         \
   GFX9_LP_CONFIG_BASE,                            \
   .num_subslices = { 2, },                        \
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

#define SKL_CONFIG                                            \
   GFX9_FEATURES, GFX9_MAX_THREADS, GFX9_URB_MIN_MAX_ENTRIES, \
   .platform = INTEL_PLATFORM_SKL,                            \
   .simulator_id = 12

static const struct intel_device_info intel_device_info_skl_gt1 = {
   SKL_CONFIG, .gt = 1,
   .num_slices = 1,
   .num_subslices = { 2, },
   .max_eus_per_subslice = 6,
   .l3_banks = 2,
   /* GT1 seems to have a bug in the top of the pipe (VF/VS?) fixed functions
    * leading to some vertices to go missing if we use too much URB.
    */
   .urb.max_entries[MESA_SHADER_VERTEX] = 928,
   .simulator_id = 12,
};

static const struct intel_device_info intel_device_info_skl_gt2 = {
   SKL_CONFIG, .gt = 2,
   .num_slices = 1,
   .num_subslices = { 3, },
   .max_eus_per_subslice = 8,
   .l3_banks = 4,
};

static const struct intel_device_info intel_device_info_skl_gt3 = {
   SKL_CONFIG, .gt = 3,
   .num_slices = 2,
   .num_subslices = { 3, 3, },
   .max_eus_per_subslice = 8,
   .l3_banks = 8,
};

static const struct intel_device_info intel_device_info_skl_gt4 = {
   SKL_CONFIG, .gt = 4,
   .num_slices = 3,
   .num_subslices = { 3, 3, 3, },
   .max_eus_per_subslice = 8,
   .l3_banks = 12,
   /* From the "L3 Allocation and Programming" documentation:
    *
    * "URB is limited to 1008KB due to programming restrictions.  This is not a
    * restriction of the L3 implementation, but of the FF and other clients.
    * Therefore, in a GT4 implementation it is possible for the programmed
    * allocation of the L3 data array to provide 3*384KB=1152KB for URB, but
    * only 1008KB of this will be used."
    */
};

static const struct intel_device_info intel_device_info_bxt = {
   GFX9_LP_CONFIG_3X6,
   .platform = INTEL_PLATFORM_BXT,
   .l3_banks = 2,
   .simulator_id = 14,
};

static const struct intel_device_info intel_device_info_bxt_2x6 = {
   GFX9_LP_CONFIG_2X6,
   .platform = INTEL_PLATFORM_BXT,
   .l3_banks = 1,
   .simulator_id = 14,
};

#define KBL_CONFIG                                            \
   GFX9_FEATURES, GFX9_MAX_THREADS, GFX9_URB_MIN_MAX_ENTRIES, \
   .platform = INTEL_PLATFORM_KBL,                            \
   .simulator_id = 16

static const struct intel_device_info intel_device_info_kbl_gt1 = {
   KBL_CONFIG, .gt = 1,
   .max_cs_threads = 7 * 6,
   .num_slices = 1,
   .num_subslices = { 2, },
   .max_eus_per_subslice = 6,
   .l3_banks = 2,
   /* GT1 seems to have a bug in the top of the pipe (VF/VS?) fixed functions
    * leading to some vertices to go missing if we use too much URB.
    */
   .urb.max_entries[MESA_SHADER_VERTEX] = 928,
   .urb.max_entries[MESA_SHADER_GEOMETRY] = 256,
};

static const struct intel_device_info intel_device_info_kbl_gt1_5 = {
   KBL_CONFIG, .gt = 1,
   .max_cs_threads = 7 * 6,
   .num_slices = 1,
   .num_subslices = { 3, },
   .max_eus_per_subslice = 6,
   .l3_banks = 4,
};

static const struct intel_device_info intel_device_info_kbl_gt2 = {
   KBL_CONFIG, .gt = 2,
   .num_slices = 1,
   .num_subslices = { 3, },
   .max_eus_per_subslice = 8,
   .l3_banks = 4,
};

static const struct intel_device_info intel_device_info_kbl_gt3 = {
   KBL_CONFIG, .gt = 3,
   .num_slices = 2,
   .num_subslices = { 3, 3, },
   .max_eus_per_subslice = 8,
   .l3_banks = 8,
};

static const struct intel_device_info intel_device_info_kbl_gt4 = {
   KBL_CONFIG, .gt = 4,

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
   .max_eus_per_subslice = 8,
   .l3_banks = 12,
};

static const struct intel_device_info intel_device_info_glk = {
   GFX9_LP_CONFIG_3X6,
   .platform = INTEL_PLATFORM_GLK,
   .l3_banks = 2,
   .simulator_id = 17,
};

static const struct intel_device_info intel_device_info_glk_2x6 = {
   GFX9_LP_CONFIG_2X6,
   .platform = INTEL_PLATFORM_GLK,
   .l3_banks = 2,
   .simulator_id = 17,
};

#define CFL_CONFIG                                            \
   GFX9_FEATURES, GFX9_MAX_THREADS, GFX9_URB_MIN_MAX_ENTRIES, \
   .platform = INTEL_PLATFORM_CFL,                            \
   .simulator_id = 24

static const struct intel_device_info intel_device_info_cfl_gt1 = {
   CFL_CONFIG, .gt = 1,

   .num_slices = 1,
   .num_subslices = { 2, },
   .max_eus_per_subslice = 6,
   .l3_banks = 2,
   /* GT1 seems to have a bug in the top of the pipe (VF/VS?) fixed functions
    * leading to some vertices to go missing if we use too much URB.
    */
   .urb.max_entries[MESA_SHADER_VERTEX] = 928,
   .urb.max_entries[MESA_SHADER_GEOMETRY] = 256,
};

static const struct intel_device_info intel_device_info_cfl_gt2 = {
   CFL_CONFIG, .gt = 2,

   .num_slices = 1,
   .num_subslices = { 3, },
   .max_eus_per_subslice = 8,
   .l3_banks = 4,
};

static const struct intel_device_info intel_device_info_cfl_gt3 = {
   CFL_CONFIG, .gt = 3,

   .num_slices = 2,
   .num_subslices = { 3, 3, },
   .max_eus_per_subslice = 8,
   .l3_banks = 8,
};

#define subslices(args...) { args, }

#define GFX11_FEATURES                              \
   GFX9_FEATURES,                                   \
   .ver = 11,                                       \
   .has_pln = false,                                \
   .has_64bit_float = false,                        \
   .has_64bit_int = false,                          \
   .has_integer_dword_mul = false,                  \
   .has_sample_with_hiz = false,                    \
   .timestamp_frequency = 12500000

#define GFX11_MAX_THREADS                           \
   .max_vs_threads = 364,                           \
   .max_gs_threads = 224,                           \
   .max_tcs_threads = 224,                          \
   .max_tes_threads = 364,                          \
   .max_wm_threads = 0,                             \
   .max_threads_per_psd = 64,                       \
   .max_cs_threads = 56

/* See GFX9_URB_MIN_MAX_ENTRIES comment */
#define GFX11_URB_MIN_MAX_ENTRIES                   \
   .urb = {                                         \
      .min_entries = {                              \
         [MESA_SHADER_VERTEX]    = 64,              \
         [MESA_SHADER_TESS_EVAL] = 34,              \
         [MESA_SHADER_GEOMETRY]  = 16,              \
      },                                            \
      .max_entries = {                              \
         [MESA_SHADER_VERTEX]    = 2384,            \
         [MESA_SHADER_TESS_CTRL] = 1032,            \
         [MESA_SHADER_TESS_EVAL] = 2384,            \
         [MESA_SHADER_GEOMETRY]  = 1032,            \
      },                                            \
   }

#define ICL_CONFIG(_gt, _slices, _subslices, _l3)                \
   GFX11_FEATURES, GFX11_MAX_THREADS, GFX11_URB_MIN_MAX_ENTRIES, \
   .platform = INTEL_PLATFORM_ICL,                               \
   .gt = _gt, .num_slices = _slices, .l3_banks = _l3,            \
   .num_subslices = _subslices,                                  \
   .max_eus_per_subslice = 8,                                    \
   .simulator_id = 19

static const struct intel_device_info intel_device_info_icl_gt2 = {
   ICL_CONFIG(2, 1, subslices(8), 8),
};

static const struct intel_device_info intel_device_info_icl_gt1_5 = {
   ICL_CONFIG(1, 1, subslices(6), 6),
};

static const struct intel_device_info intel_device_info_icl_gt1 = {
   ICL_CONFIG(1, 1, subslices(4), 6),
};

static const struct intel_device_info intel_device_info_icl_gt0_5 = {
   ICL_CONFIG(1, 1, subslices(1), 6),
};

#define EHL_CONFIG(nr_subslices, max_eus_per_sub)   \
   ICL_CONFIG(1, 1, subslices(nr_subslices), 4),    \
   .platform = INTEL_PLATFORM_EHL,                  \
   .max_eus_per_subslice = max_eus_per_sub,         \
   .disable_ccs_repack = true,                      \
   .simulator_id = 28

static const struct intel_device_info intel_device_info_ehl_4x8 = {
   EHL_CONFIG(4, 8),
};

static const struct intel_device_info intel_device_info_ehl_4x6 = {
   EHL_CONFIG(4, 6),
};

static const struct intel_device_info intel_device_info_ehl_4x5 = {
   EHL_CONFIG(4, 5),
};

static const struct intel_device_info intel_device_info_ehl_4x4 = {
   EHL_CONFIG(4, 4),
};

static const struct intel_device_info intel_device_info_ehl_2x8 = {
   EHL_CONFIG(2, 8),
};

static const struct intel_device_info intel_device_info_ehl_2x4 = {
   EHL_CONFIG(2, 4),
};

#define GFX12_FEATURES                              \
   GFX11_FEATURES,                                  \
   .ver = 12,                                       \
   .has_aux_map = true

#define GFX12_MAX_THREADS                           \
   .max_vs_threads = 546,                           \
   .max_gs_threads = 336,                           \
   .max_tcs_threads = 336,                          \
   .max_tes_threads = 546,                          \
   .max_wm_threads = 0,                             \
   .max_threads_per_psd = 64,                       \
   .max_cs_threads = 112 /* threads per DSS */

/* See GFX9_URB_MIN_MAX_ENTRIES comment */
#define GFX12_URB_MIN_MAX_ENTRIES                   \
   .urb = {                                         \
      .size = 512, /* For intel_stub_gpu */         \
      .min_entries = {                              \
         [MESA_SHADER_VERTEX]    = 64,              \
         [MESA_SHADER_TESS_EVAL] = 34,              \
         [MESA_SHADER_GEOMETRY]  = 16,              \
      },                                            \
      .max_entries = {                              \
         [MESA_SHADER_VERTEX]    = 3576,            \
         [MESA_SHADER_TESS_CTRL] = 1548,            \
         [MESA_SHADER_TESS_EVAL] = 3576,            \
         [MESA_SHADER_GEOMETRY]  = 1548,            \
      },                                            \
   }

#define GFX12_PAT_ENTRIES                                       \
   /* BSpec 45101 (r51017) */                                   \
   .pat = {                                                     \
         /* CPU: WB, GPU: PAT 0 => WB, 2WAY */                  \
         .cached_coherent = PAT_ENTRY(0, WB),                   \
         /* CPU: WC, GPU: PAT 1 => WC */                        \
         .scanout = PAT_ENTRY(1, WC),                           \
         /* CPU: WB, GPU: PAT 0 => WB, 2WAY */                  \
         .writeback_incoherent = PAT_ENTRY(0, WB),              \
         /* CPU: WC, GPU: PAT 1 => WC */                        \
         .writecombining = PAT_ENTRY(1, WC),                    \
   }

#define GFX12_CONFIG(_gt, _slices, _l3)                         \
   GFX12_FEATURES, GFX12_MAX_THREADS,                           \
   GFX12_URB_MIN_MAX_ENTRIES, GFX12_PAT_ENTRIES,                \
   .gt = _gt, .num_slices = _slices, .l3_banks = _l3,           \
   .max_eus_per_subslice = 16,                                  \
   .simulator_id = 22

#define dual_subslices(args...) { args, }

#define GFX12_GT05_CONFIG                                     \
   GFX12_CONFIG(1, 1, 4),                                     \
   .num_subslices = dual_subslices(1)

#define GFX12_GT_CONFIG(_gt)                                  \
   GFX12_CONFIG(_gt, 1, _gt == 1 ? 4 : 8),                    \
   .num_subslices = dual_subslices(_gt == 1 ? 2 : 6)

static const struct intel_device_info intel_device_info_tgl_gt1 = {
   GFX12_GT_CONFIG(1),
   .platform = INTEL_PLATFORM_TGL,
};

static const struct intel_device_info intel_device_info_tgl_gt2 = {
   GFX12_GT_CONFIG(2),
   .platform = INTEL_PLATFORM_TGL,
};

static const struct intel_device_info intel_device_info_rkl_gt05 = {
   GFX12_GT05_CONFIG,
   .platform = INTEL_PLATFORM_RKL,
};

static const struct intel_device_info intel_device_info_rkl_gt1 = {
   GFX12_GT_CONFIG(1),
   .platform = INTEL_PLATFORM_RKL,
};

static const struct intel_device_info intel_device_info_adl_gt05 = {
   GFX12_GT05_CONFIG,
   .platform = INTEL_PLATFORM_ADL,
};

static const struct intel_device_info intel_device_info_adl_gt1 = {
   GFX12_GT_CONFIG(1),
   .platform = INTEL_PLATFORM_ADL,
};

static const struct intel_device_info intel_device_info_adl_gt2 = {
   GFX12_GT_CONFIG(2),
   .platform = INTEL_PLATFORM_ADL,
};

static const struct intel_device_info intel_device_info_rpl = {
   GFX12_CONFIG(1, 1, 4),
   .num_subslices = dual_subslices(2),
   .platform = INTEL_PLATFORM_RPL,
};

static const struct intel_device_info intel_device_info_rpl_p = {
   GFX12_GT_CONFIG(2),
   .platform = INTEL_PLATFORM_RPL,
};

#define DG1_SG1_CONFIG                                   \
   GFX12_GT_CONFIG(2),                                   \
   .platform = INTEL_PLATFORM_DG1,                       \
   .has_llc = false,                                     \
   .has_local_mem = true,                                \
   .urb.size = 768,                                      \
   .simulator_id = 30

static const struct intel_device_info intel_device_info_dg1 = {
   DG1_SG1_CONFIG,
};

static const struct intel_device_info intel_device_info_sg1 = {
   DG1_SG1_CONFIG,
};

#define XEHP_URB_MIN_MAX_ENTRIES                           \
   .urb = {                                                \
      .size = 768, /* For intel_stub_gpu */                \
      .min_entries = {                                     \
         [MESA_SHADER_VERTEX]    = 64,                     \
         [MESA_SHADER_TESS_EVAL] = 34,                     \
         [MESA_SHADER_GEOMETRY]  = 2,                      \
      },                                                   \
      .max_entries = {                                     \
         [MESA_SHADER_VERTEX]    = 3832, /* BSpec 47138 */ \
         [MESA_SHADER_TESS_CTRL] = 1548, /* BSpec 47137 */ \
         [MESA_SHADER_TESS_EVAL] = 3576, /* BSpec 47135 */ \
         [MESA_SHADER_GEOMETRY]  = 1548, /* BSpec 47136 */ \
      }                                                    \
   }

#define XEHP_FEATURES                                           \
   GFX12_FEATURES,                                              \
   CMAT_XEHP_CONFIGURATIONS,                                    \
   .verx10 = 125,                                               \
   .has_lsc = true,                                             \
   .has_llc = false,                                            \
   .has_ray_tracing = true,                                     \
   .has_mesh_shading = true,                                    \
   .has_bfloat16 = true,                                        \
   .has_systolic = true,                                        \
   .has_coarse_pixel_primitive_and_cb = true,                   \
   .needs_null_push_constant_tbimr_workaround = true,           \
   .simulator_id = 29

/* (Sub)slice info, thread counts, and URB come from hwconfig tables */
#define XEHP_PLACEHOLDER_THREADS_AND_URB                        \
   .gt = 0, .num_slices = 1, .l3_banks = 0,                     \
   .num_subslices = dual_subslices(1),                          \
   .max_vs_threads = 546,  /* BSpec 46312 */                    \
   .max_gs_threads = 336,  /* BSpec 46299 */                    \
   .max_tcs_threads = 336, /* BSpec 46300 */                    \
   .max_tes_threads = 546, /* BSpec 46298 */                    \
   .max_wm_threads = 0,                                         \
   .max_threads_per_psd = 64,                                   \
   .max_cs_threads = 112, /* threads per DSS */                 \
   .num_thread_per_eu = 8 /* BSpec 44472 */,                    \
   .max_eus_per_subslice = 16,                                  \
   XEHP_URB_MIN_MAX_ENTRIES

#define DG2_CONFIG(platform_suffix)                             \
   XEHP_FEATURES, XEHP_PLACEHOLDER_THREADS_AND_URB,             \
   .platform = INTEL_PLATFORM_ ## platform_suffix,              \
   .revision = 4, /* For offline compiler */                    \
   .has_flat_ccs = true,                                        \
   .has_aux_map = false,                                        \
   .has_local_mem = true,                                       \
   /* There is no PAT table for DG2, using TGL ones */          \
   /* BSpec 45101 (r51017) */                                   \
   .pat = {                                                     \
         /* CPU: WB, GPU: PAT 0 => WB, 2WAY */                  \
         .cached_coherent = PAT_ENTRY(0, WB),                   \
         /* CPU: WC, GPU: PAT 1 => WC */                        \
         .scanout = PAT_ENTRY(1, WC),                           \
         /* CPU: WB, GPU: PAT 0 => WB, 2WAY */                  \
         .writeback_incoherent = PAT_ENTRY(0, WB),              \
         /* CPU: WC, GPU: PAT 1 => WC */                        \
         .writecombining = PAT_ENTRY(1, WC),                    \
   }

static const struct intel_device_info intel_device_info_dg2_g10 = {
   DG2_CONFIG(DG2_G10),
};

static const struct intel_device_info intel_device_info_dg2_g11 = {
   DG2_CONFIG(DG2_G11),
};

static const struct intel_device_info intel_device_info_dg2_g12 = {
   DG2_CONFIG(DG2_G12),
};

static const struct intel_device_info intel_device_info_atsm_g10 = {
   DG2_CONFIG(ATSM_G10),
};

static const struct intel_device_info intel_device_info_atsm_g11 = {
   DG2_CONFIG(ATSM_G11),
};

#define MTL_CONFIG(platform_suffix)                             \
   XEHP_FEATURES, XEHP_PLACEHOLDER_THREADS_AND_URB,             \
   CMAT_PRE_XEHP_CONFIGURATIONS,                                \
   .platform = INTEL_PLATFORM_ ## platform_suffix,              \
   .has_64bit_float = true,                                     \
   .has_64bit_float_via_math_pipe = true,                       \
   .has_bfloat16 = false,                                       \
   /* BSpec 55414 (r53716). */                                  \
   .has_systolic = false,                                       \
   /* BSpec 45101 (r51017) */                                   \
   .pat = {                                                     \
         /* CPU: WB, GPU: PAT 3 => WB, 1WAY */                  \
         .cached_coherent = PAT_ENTRY(3, WB),                   \
         /* CPU: WC, GPU: PAT 1 => WC */                        \
         .scanout = PAT_ENTRY(1, WC),                           \
         /* CPU: WB, GPU: PAT 0 => WB, 0WAY */                  \
         .writeback_incoherent = PAT_ENTRY(0, WB),              \
         /* CPU: WC, GPU: PAT 1 => WC */                        \
         .writecombining = PAT_ENTRY(1, WC),                    \
   }

static const struct intel_device_info intel_device_info_mtl_u = {
   MTL_CONFIG(MTL_U),
};

static const struct intel_device_info intel_device_info_mtl_h = {
   MTL_CONFIG(MTL_H),
};

static const struct intel_device_info intel_device_info_arl_u = {
   MTL_CONFIG(ARL_U),
};

static const struct intel_device_info intel_device_info_arl_h = {
   MTL_CONFIG(ARL_H),
   .has_bfloat16 = true,
   /* BSpec 55414 (r53716). */
   .has_systolic = true,
   CMAT_XEHP_CONFIGURATIONS,
};

#define XE2_FEATURES                                            \
   XEHP_FEATURES,                                               \
   CMAT_XE2_CONFIGURATIONS,                                     \
   .ver = 20,                                                   \
   .verx10 = 200,                                               \
   .grf_size = 64,                                              \
   .needs_null_push_constant_tbimr_workaround = false,          \
   .has_64bit_float = true,                                     \
   .has_64bit_int = true,                                       \
   .has_indirect_unroll = true,                                 \
   .has_aux_map = false,                                        \
   .has_flat_ccs = true

/* Note, do not enable PAT 10 or 12 on BMG, according to
 * Wa_18038669374 we should not not use any MOCS/PAT settings
 * that has "Compressible UC policy"
 *
 * (both 10 and 12 map to different compressed L3UC entries)
 */
#define XE2_PAT_ENTRIES                                         \
   /* BSpec 71582 (r59285) */                                   \
   .pat = {                                                     \
      /* CPU: WB, GPU: PAT 1 => WB, 1WAY */                     \
      .cached_coherent = PAT_ENTRY(1, WB),                      \
      /* CPU: WC, GPU: PAT 6 => XD */                           \
      .scanout = PAT_ENTRY(6, WC),                              \
      /* CPU: WC, GPU: PAT 0 => WB */                           \
      .writecombining = PAT_ENTRY(0, WC),                       \
      /* CPU: WC, GPU: PAT 11 => XD, compressed */              \
      .compressed_scanout = PAT_ENTRY(11, WC),                  \
      /* CPU: WC, GPU: PAT 9 => WB, compressed */               \
      .compressed = PAT_ENTRY(9, WC)                            \
   }

#define XE2_CONFIG(platform_suffix)                             \
   XE2_FEATURES, XE2_PAT_ENTRIES,                               \
   XEHP_PLACEHOLDER_THREADS_AND_URB,                            \
   .platform = INTEL_PLATFORM_ ## platform_suffix

static const struct intel_device_info intel_device_info_bmg = {
   XE2_CONFIG(BMG),
   .has_local_mem = true,
};

static const struct intel_device_info intel_device_info_lnl = {
   XE2_CONFIG(LNL),
   .has_local_mem = false,
};

#define XE3_FEATURES                                            \
   XE2_FEATURES,                                                \
   .ver = 30,                                                   \
   .verx10 = 300

#define XE3_CONFIG(platform_suffix)                             \
   XE3_FEATURES, XE2_PAT_ENTRIES,                               \
   XEHP_PLACEHOLDER_THREADS_AND_URB,                            \
   .platform = INTEL_PLATFORM_ ## platform_suffix


static const struct intel_device_info intel_device_info_ptl = {
   XE3_CONFIG(PTL),
   .has_local_mem = false,
};

static const struct intel_device_info intel_device_info_wcl = {
   XE3_CONFIG(WCL),
   .has_local_mem = false,
   .has_ray_tracing = false,
};

static const struct intel_device_info intel_device_info_nvl_s_hx_ul = {
   XE3_CONFIG(NVL_U),
   .has_local_mem = false,
   .has_ray_tracing = false,
};

static const struct intel_device_info intel_device_info_nvl_u_h = {
   XE3_CONFIG(NVL_U),
   .has_local_mem = false,
};

void
intel_device_info_topology_reset_masks(struct intel_device_info *devinfo)
{
   devinfo->subslice_slice_stride = 0;
   devinfo->eu_subslice_stride = 0;
   devinfo->eu_slice_stride = 0;

   devinfo->num_slices = 0;
   memset(devinfo->num_subslices, 0, sizeof(devinfo->num_subslices));

   memset(&devinfo->slice_masks, 0, sizeof(devinfo->slice_masks));
   memset(devinfo->subslice_masks, 0, sizeof(devinfo->subslice_masks));
   memset(devinfo->eu_masks, 0, sizeof(devinfo->eu_masks));
   memset(devinfo->ppipe_subslices, 0, sizeof(devinfo->ppipe_subslices));
}

void
intel_device_info_topology_update_counts(struct intel_device_info *devinfo)
{
   devinfo->num_slices = __builtin_popcount(devinfo->slice_masks);
   devinfo->subslice_total = 0;
   for (int s = 0; s < devinfo->max_slices; s++) {
      if (!intel_device_info_slice_available(devinfo, s))
         continue;

      for (int b = 0; b < devinfo->subslice_slice_stride; b++) {
         devinfo->num_subslices[s] +=
            __builtin_popcount(devinfo->subslice_masks[s * devinfo->subslice_slice_stride + b]);
      }
      devinfo->subslice_total += devinfo->num_subslices[s];
   }
   assert(devinfo->num_slices > 0);
   assert(devinfo->subslice_total > 0);
}

void
intel_device_info_update_pixel_pipes(struct intel_device_info *devinfo, uint8_t *subslice_masks)
{
   if (devinfo->ver < 11)
      return;

   /* The kernel only reports one slice on all existing ICL+ platforms, even
    * if multiple slices are present. The slice mask is allowed to have the
    * accurate value greater than 1 on gfx12.5+ platforms though, in order to
    * be tolerant with the behavior of our simulation environment.
    */
   assert(devinfo->slice_masks == 1 || devinfo->verx10 >= 125);

   /* Count the number of subslices on each pixel pipe. Assume that every
    * contiguous group of 4 subslices in the mask belong to the same pixel
    * pipe. However note that on TGL+ the kernel returns a mask of enabled
    * *dual* subslices instead of actual subslices somewhat confusingly, so
    * each pixel pipe only takes 2 bits in the mask even though it's still 4
    * subslices.
    */
   const unsigned ppipe_bits = devinfo->ver >= 12 ? 2 : 4;
   for (unsigned p = 0; p < INTEL_DEVICE_MAX_PIXEL_PIPES; p++) {
      const unsigned offset = p * ppipe_bits;
      const unsigned subslice_idx = offset /
         devinfo->max_subslices_per_slice * devinfo->subslice_slice_stride;
      const unsigned ppipe_mask =
         BITFIELD_RANGE(offset % devinfo->max_subslices_per_slice, ppipe_bits);

      if (subslice_idx < ARRAY_SIZE(devinfo->subslice_masks))
         devinfo->ppipe_subslices[p] =
            __builtin_popcount(subslice_masks[subslice_idx] & ppipe_mask);
      else
         devinfo->ppipe_subslices[p] = 0;
   }
}

void
intel_device_info_update_l3_banks(struct intel_device_info *devinfo)
{
   if (devinfo->ver != 12)
      return;

   if (devinfo->verx10 >= 125) {
      if (devinfo->subslice_total > 16) {
         assert(devinfo->subslice_total <= 32);
         devinfo->l3_banks = 32;
      } else if (devinfo->subslice_total > 8) {
         devinfo->l3_banks = 16;
      } else {
         devinfo->l3_banks = 8;
      }
   } else {
      assert(devinfo->num_slices == 1);
      if (devinfo->subslice_total >= 6) {
         assert(devinfo->subslice_total == 6);
         devinfo->l3_banks = 8;
      } else if (devinfo->subslice_total > 2) {
         devinfo->l3_banks = 6;
      } else {
         devinfo->l3_banks = 4;
      }
   }
}

/* Returns the number of EUs of the first subslice enabled */
uint32_t
intel_device_info_get_eu_count_first_subslice(const struct intel_device_info *devinfo)
{
   uint32_t first_subslice, first_slice, offset, i;
   uint32_t eu_count = 0;

   first_slice = ffs(devinfo->slice_masks);
   first_slice--;
   offset = first_slice * devinfo->subslice_slice_stride;

   for (i = 0; i < DIV_ROUND_UP(devinfo->max_subslices_per_slice, 8); i++) {
      first_subslice = ffs(devinfo->subslice_masks[offset + i]);

      if (first_subslice == 0)
         continue;

      break;
   }

   assert(first_subslice > 0);
   first_subslice--;
   offset = first_slice * devinfo->eu_slice_stride +
            first_subslice * devinfo->eu_subslice_stride;
   for (i = 0; i < DIV_ROUND_UP(devinfo->max_eus_per_subslice, 8); i++)
      eu_count += __builtin_popcount(devinfo->eu_masks[offset + i]);

   assert(eu_count > 0);
   return eu_count;
}

/* Generate mask from the device data. */
static void
fill_masks(struct intel_device_info *devinfo)
{
   /* All of our internal device descriptions assign the same number of
    * subslices for each slice. Just verify that this is true.
    */
   for (int s = 1; s < devinfo->num_slices; s++)
      assert(devinfo->num_subslices[0] == devinfo->num_subslices[s]);

   intel_device_info_i915_update_from_masks(devinfo,
                          (1U << devinfo->num_slices) - 1,
                          (1U << devinfo->num_subslices[0]) - 1,
                          devinfo->num_slices * devinfo->num_subslices[0] *
                          devinfo->max_eus_per_subslice);
}

void
intel_device_info_update_cs_workgroup_threads(struct intel_device_info *devinfo)
{
   /* GPGPU_WALKER::ThreadWidthCounterMaximum is U6-1 so the most threads we
    * can program is 64 without going up to a rectangular group. This only
    * impacts Haswell and TGL which have higher thread counts.
    *
    * INTERFACE_DESCRIPTOR_DATA::NumberofThreadsinGPGPUThreadGroup on Xe-HP+
    * is 10 bits so we have no such restrictions.
    */
   devinfo->max_cs_workgroup_threads =
      devinfo->verx10 >= 125 ? devinfo->max_cs_threads :
                               MIN2(devinfo->max_cs_threads, 64);
}

static bool
parse_force_probe_entry(int pci_id, const char *entry, bool *force_on,
                        bool *force_off)
{
   const char *cp = entry;

   bool negated = *cp == '!';
   if (negated)
      cp++;

   if (*cp == '\0')
      return false;

   bool wildcard = *cp == '*';
   long val = 0;

   if (wildcard) {
      cp++;
   } else {
      char *end;
      val = strtol(cp, &end, 16);
      if (end == cp)
         return false;
      cp = end;
   }

   if (*cp != '\0')
      return false;

   bool matched = wildcard || (long)pci_id == val;
   if (matched) {
      *force_on = !negated;
      *force_off = negated;
   }

   return matched;
}

static void
scan_for_force_probe(int pci_id, bool *force_on, bool *force_off)
{
   *force_on = false;
   *force_off = false;

   const char *env = os_get_option("INTEL_FORCE_PROBE");
   if (env == NULL)
      return;

   size_t len = strlen(env);
   if (len == 0)
      return;

   char *dup = strndup(env, len);
   if (dup == NULL)
      return;

   for (char *entry = strtok(dup, ","); entry; entry = strtok(NULL, ","))
      parse_force_probe_entry(pci_id, entry, force_on, force_off);

   free(dup);
   assert(!*force_on || !*force_off);
}

struct device_init_config {
   bool require_force_probe;
};

/* Example PCI ID entry using FORCE_PROBE:
 *
 * CHIPSET(0x1234, foo, "FOO", "Intel(R) Graphics", FORCE_PROBE)
 */
#define FORCE_PROBE .require_force_probe = true

static bool
intel_device_info_init_common(int pci_id, bool building,
                              struct intel_device_info *devinfo)
{
   struct device_init_config device_config = { 0 };
   switch (pci_id) {
#undef CHIPSET
#define CHIPSET(id, family, fam_str, name, ...)                         \
      case id:                                                          \
         *devinfo = intel_device_info_##family;                         \
         device_config = *&(struct device_init_config) { __VA_ARGS__ }; \
         break;
#include "pci_ids/crocus_pci_ids.h"
#include "pci_ids/iris_pci_ids.h"

#undef CHIPSET
#define CHIPSET(id, fam_str, name) \
      case id: *devinfo = intel_device_info_gfx3; break;
#include "pci_ids/i915_pci_ids.h"

   default:
      mesa_logw("Driver does not support the 0x%x PCI ID.", pci_id);
      return false;
   }

   switch (pci_id) {
#undef CHIPSET
#define CHIPSET(_id, _family, _fam_str, _name, ...) \
   case _id: \
      /* sizeof(str_literal) includes the null */ \
      STATIC_ASSERT(sizeof(_name) + sizeof(_fam_str) + 2 <= \
                    sizeof(devinfo->name)); \
      strncpy(devinfo->name, _name " (" _fam_str ")", sizeof(devinfo->name)); \
      break;
#include "pci_ids/crocus_pci_ids.h"
#include "pci_ids/iris_pci_ids.h"
   default:
      strncpy(devinfo->name, "Intel Unknown", sizeof(devinfo->name));
   }

   bool force_on = false;
   bool force_off = false;
   if (building)
      force_on = true;
   else
      scan_for_force_probe(pci_id, &force_on, &force_off);
   devinfo->probe_forced = force_on;
   if (force_off) {
      mesa_logw("%s (0x%x) disabled with INTEL_FORCE_PROBE", devinfo->name,
                pci_id);
      return false;
   } else if (device_config.require_force_probe) {
      if (force_on) {
         if (!building)
            mesa_logw("Forcing probe of unsupported: %s (0x%x)", devinfo->name,
                      pci_id);
      } else {
         mesa_loge("%s (0x%x) requires INTEL_FORCE_PROBE", devinfo->name,
                   pci_id);
         return false;
      }
   }

   devinfo->pci_device_id = pci_id;

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
   case 20:
   case 30:
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

   uint16_t major = devinfo->ver;
   uint16_t minor = (devinfo->verx10 - (devinfo->ver * 10)) * 10;
   /* When supported gfx_ip_ver will be overwritten by values read from KMD.
    * This is a approximation for platforms that do not support GMD ID or
    * when running offline tools.
    * verx10 125 becomes GFX_IP_VER(12, 50) for example.
    */
   devinfo->gfx_ip_ver = GFX_IP_VER(major, minor);

   if (devinfo->has_mesh_shading) {
      /* Half of push constant space matches the size used in the simplest
       * primitive pipeline (VS + FS). Tweaking this affects performance.
       */
      devinfo->mesh_max_constant_urb_size_kb =
            devinfo->max_constant_urb_size_kb / 2;
   }

   /*
    * Gfx 12.5 moved scratch to a surface and SURFTYPE_SCRATCH has this pitch
    * restriction:
    *
    * BSpec 43862 (r52666)
    * RENDER_SURFACE_STATE::Surface Pitch
    *    For surfaces of type SURFTYPE_SCRATCH, valid range of pitch is:
    *    [63,262143] -> [64B, 256KB]
    *
    * The pitch of the surface is the scratch size per thread and the surface
    * should be large enough to accommodate every physical thread.
    */
   devinfo->max_scratch_size_per_thread = devinfo->verx10 >= 125 ?
                                          (256 * 1024) : (2 * 1024 * 1024);
   intel_device_info_update_cs_workgroup_threads(devinfo);

   return true;
}

static void
intel_device_info_apply_workarounds(struct intel_device_info *devinfo)
{
   if (intel_needs_workaround(devinfo, 18012660806))
      devinfo->urb.max_entries[MESA_SHADER_GEOMETRY] = 1536;

   if (intel_needs_workaround(devinfo, 18040209780))
      devinfo->max_gs_threads = 312;

   /* Fixes issues with:
    * dEQP-GLES31.functional.geometry_shading.layered.render_with_default_layer_cubemap
    * when running on GFX12 platforms with small EU count.
    */
   const uint32_t eu_total = intel_device_info_eu_total(devinfo);
   if (devinfo->verx10 == 120 && eu_total <= 32)
      devinfo->urb.max_entries[MESA_SHADER_GEOMETRY] = 1024;
}

static bool
intel_get_device_info_from_pci_id_common(int pci_id, bool building,
                                         struct intel_device_info *devinfo)
{
   intel_device_info_init_common(pci_id, building, devinfo);

   /* This is a placeholder until a proper value is set. */
   devinfo->kmd_type = INTEL_KMD_TYPE_I915;

   intel_device_info_init_was(devinfo);
   intel_device_info_apply_workarounds(devinfo);

   return true;
}

bool
intel_get_device_info_from_pci_id(int pci_id,
                                  struct intel_device_info *devinfo)
{
   return intel_get_device_info_from_pci_id_common(pci_id, false, devinfo);
}

bool
intel_get_device_info_for_build(int pci_id,
                                struct intel_device_info *devinfo)
{
   return intel_get_device_info_from_pci_id_common(pci_id, true, devinfo);
}

bool
intel_device_info_compute_system_memory(struct intel_device_info *devinfo, bool update)
{
   if (!update) {
      if (!os_get_total_physical_memory(&devinfo->mem.sram.mappable.size))
         return false;
   }

   os_get_available_system_memory(&devinfo->mem.sram.mappable.free);

   return true;
}

static void
intel_device_info_adjust_memory(struct intel_device_info *devinfo)
{
   uint64_t available;

   /* Applications running without elevated privileges don't report valid
    * numbers for free sram
    */
   if (os_get_available_system_memory(&available)) {
      devinfo->mem.sram.mappable.free = MIN3(devinfo->mem.sram.mappable.free,
                                             devinfo->mem.sram.mappable.size,
                                             available);
   }
}

static void
init_max_scratch_ids(struct intel_device_info *devinfo)
{
   /* Determine the max subslice that potentially might be used in
    * scratch space ids.
    */
   unsigned subslices = intel_device_info_dual_subslice_id_bound(devinfo);
   assert(subslices >= devinfo->subslice_total);

   unsigned scratch_ids_per_subslice;
   if (devinfo->ver >= 12) {
      /* Same as ICL below, but with 16 EUs. */
      scratch_ids_per_subslice = 16 * 8;
   } else if (devinfo->ver >= 11) {
      /* The MEDIA_VFE_STATE docs say:
       *
       *    "Starting with this configuration, the Maximum Number of
       *     Threads must be set to (#EU * 8) for GPGPU dispatches.
       *
       *     Although there are only 7 threads per EU in the configuration,
       *     the FFTID is calculated as if there are 8 threads per EU,
       *     which in turn requires a larger amount of Scratch Space to be
       *     allocated by the driver."
       */
      scratch_ids_per_subslice = 8 * 8;
   } else if (devinfo->platform == INTEL_PLATFORM_HSW) {
      /* WaCSScratchSize:hsw
       *
       * Haswell's scratch space address calculation appears to be sparse
       * rather than tightly packed. The Thread ID has bits indicating
       * which subslice, EU within a subslice, and thread within an EU it
       * is. There's a maximum of two slices and two subslices, so these
       * can be stored with a single bit. Even though there are only 10 EUs
       * per subslice, this is stored in 4 bits, so there's an effective
       * maximum value of 16 EUs. Similarly, although there are only 7
       * threads per EU, this is stored in a 3 bit number, giving an
       * effective maximum value of 8 threads per EU.
       *
       * This means that we need to use 16 * 8 instead of 10 * 7 for the
       * number of threads per subslice.
       */
      scratch_ids_per_subslice = 16 * 8;
   } else if (devinfo->platform == INTEL_PLATFORM_CHV) {
      /* Cherryview devices have either 6 or 8 EUs per subslice, and each
       * EU has 7 threads. The 6 EU devices appear to calculate thread IDs
       * as if it had 8 EUs.
       */
      scratch_ids_per_subslice = 8 * 7;
   } else {
      scratch_ids_per_subslice = devinfo->max_cs_threads;
   }

   unsigned max_thread_ids = scratch_ids_per_subslice * subslices;

   if (devinfo->verx10 >= 125) {
      /* On GFX version 12.5, scratch access changed to a surface-based model.
       * Instead of each shader type having its own layout based on IDs passed
       * from the relevant fixed-function unit, all scratch access is based on
       * thread IDs like it always has been for compute.
       */
      for (int i = MESA_SHADER_VERTEX; i < MESA_SHADER_STAGES; i++)
         devinfo->max_scratch_ids[i] = max_thread_ids;
   } else {
      unsigned max_scratch_ids[] = {
         [MESA_SHADER_VERTEX]    = devinfo->max_vs_threads,
         [MESA_SHADER_TESS_CTRL] = devinfo->max_tcs_threads,
         [MESA_SHADER_TESS_EVAL] = devinfo->max_tes_threads,
         [MESA_SHADER_GEOMETRY]  = devinfo->max_gs_threads,
         [MESA_SHADER_FRAGMENT]  = devinfo->max_wm_threads,
         [MESA_SHADER_COMPUTE]   = max_thread_ids,
      };
      STATIC_ASSERT(sizeof(devinfo->max_scratch_ids) == sizeof(max_scratch_ids));
      memcpy(devinfo->max_scratch_ids, max_scratch_ids,
             sizeof(devinfo->max_scratch_ids));
   }
}

static unsigned
intel_device_info_calc_engine_prefetch(const struct intel_device_info *devinfo,
                                       enum intel_engine_class engine_class)
{
   if (devinfo->verx10 >= 200) {
      switch (engine_class) {
      case INTEL_ENGINE_CLASS_RENDER:
         return 4096;
      case INTEL_ENGINE_CLASS_COMPUTE:
         return 1024;
      default:
         return 512;
      }
   }

   if (intel_device_info_is_mtl_or_arl(devinfo)) {
      switch (engine_class) {
      case INTEL_ENGINE_CLASS_RENDER:
         return 2048;
      case INTEL_ENGINE_CLASS_COMPUTE:
         return 1024;
      default:
         return 512;
      }
   }

   /* DG2 */
   if (devinfo->verx10 == 125)
      return 1024;

   /* Older than DG2/MTL */
   return 512;
}

static void
intel_device_info_update_after_hwconfig(struct intel_device_info *devinfo)
{
   /* After applying hwconfig values, some items need to be recalculated. */
   devinfo->max_cs_threads =
      devinfo->max_eus_per_subslice * devinfo->num_thread_per_eu;

   intel_device_info_update_cs_workgroup_threads(devinfo);

   /* On Skylake through Xe, the 3DSTATE_GS_BODY docs include a note:
    *
    *   "The driver must send pipe control with a cs stall after a
    *    3dstate_gs state change and the Dispatch Mode is simd8 and
    *    the number of handles allocated to gs is less than 16."
    *
    * We don't implement such a PIPE_CONTROL, so instead we set the minimum
    * number of GS URB entries to 16.  The Xe2+ docs don't have such a note,
    * but considering they use SIMD16 GS threads, retaining 16 as the minimum
    * likely makes sense there as well.
    *
    * Assuming we did implement that flush, the 3DSTATE_GS docs also note:
    *
    *   "At least 8 URB entries must be allocated in order to use SIMD8
    *    DispatchMode."
    *
    * The hardware config tables instead have the limit set as 2, which is
    * likely an older limit from the SIMD4x2 dual-object/instance era which
    * is no longer accurate.
    */
   devinfo->urb.min_entries[MESA_SHADER_GEOMETRY] =
      MAX2(16, devinfo->urb.min_entries[MESA_SHADER_GEOMETRY]);
}

bool
intel_get_device_info_from_fd(int fd, struct intel_device_info *devinfo, int min_ver, int max_ver)
{
   if (NULL != os_get_option("INTEL_STUB_GPU_JSON")) {
      /* This call will succeed when shim-drm has been initialized with a
       * serialized intel_device_info structure.
       */
      struct drm_intel_stub_devinfo arg = {
         .addr = (uintptr_t)devinfo,
         .size = sizeof(*devinfo),
      };
      if (0 == intel_ioctl(fd, DRM_IOCTL_INTEL_STUB_DEVINFO, &arg)) {
         intel_device_info_init_was(devinfo);
         intel_device_info_apply_workarounds(devinfo);
         return true;
      }
   }

   /* Get PCI info.
    *
    * Some callers may already have a valid drm device which holds values of
    * PCI fields queried here prior to calling this function. But making this
    * query optional leads to a more cumbersome implementation. These callers
    * still need to initialize the fields somewhere out of this function and
    * rely on an ioctl to get PCI device id for the next step when skipping
    * this drm query.
    */
   drmDevicePtr drmdev = NULL;
   if (drmGetDevice2(fd, DRM_DEVICE_GET_PCI_REVISION, &drmdev)) {
      mesa_loge("Failed to query drm device.");
      return false;
   }
   if (!intel_device_info_init_common(drmdev->deviceinfo.pci->device_id,
                                      false, devinfo)) {
      drmFreeDevice(&drmdev);
      return false;
   }

   if ((min_ver > 0 && devinfo->ver < min_ver) || (max_ver > 0 && devinfo->ver > max_ver)) {
      drmFreeDevice(&drmdev);
      return false;
   }

   devinfo->pci_domain = drmdev->businfo.pci->domain;
   devinfo->pci_bus = drmdev->businfo.pci->bus;
   devinfo->pci_dev = drmdev->businfo.pci->dev;
   devinfo->pci_func = drmdev->businfo.pci->func;
   devinfo->pci_device_id = drmdev->deviceinfo.pci->device_id;
   devinfo->pci_revision_id = drmdev->deviceinfo.pci->revision_id;
   drmFreeDevice(&drmdev);
   devinfo->no_hw = debug_get_bool_option("INTEL_NO_HW", false);

   devinfo->kmd_type = intel_get_kmd_type(fd);
   if (devinfo->kmd_type == INTEL_KMD_TYPE_INVALID) {
      mesa_loge("Unknown kernel mode driver");
      return false;
   }

   /* remaining initialization queries the kernel for device info */
   if (devinfo->no_hw) {
      /* Provide some sensible values for NO_HW. */
      devinfo->gtt_size =
         devinfo->ver >= 8 ? (1ull << 48) : 2ull * 1024 * 1024 * 1024;
      intel_device_info_compute_system_memory(devinfo, false);
      return true;
   }

   bool ret;
   switch (devinfo->kmd_type) {
   case INTEL_KMD_TYPE_I915:
      ret = intel_device_info_i915_get_info_from_fd(fd, devinfo);
      break;
   case INTEL_KMD_TYPE_XE:
      ret = intel_device_info_xe_get_info_from_fd(fd, devinfo);
      if (devinfo->verx10 < 200) {
         if (!debug_get_bool_option("INTEL_XE_IGNORE_EXPERIMENTAL_WARNING", false))
            mesa_logw("Support for this platform is experimental with Xe KMD, bug reports may be ignored.");
      }
      break;
   default:
      ret = false;
      UNREACHABLE("Missing");
   }
   if (!ret) {
      mesa_logw("Could not get intel_device_info.");
      return false;
   }

   /* region info is required for lmem support */
   if (devinfo->has_local_mem && !devinfo->mem.use_class_instance) {
      mesa_logw("Could not query local memory size.");
      return false;
   }

   if (intel_hwconfig_is_required(devinfo))
      intel_device_info_update_after_hwconfig(devinfo);
   intel_device_info_adjust_memory(devinfo);

   /* Gfx7 and older do not support EU/Subslice info */
   assert(devinfo->subslice_total >= 1 || devinfo->ver <= 7);
   devinfo->subslice_total = MAX2(devinfo->subslice_total, 1);

   init_max_scratch_ids(devinfo);

   for (enum intel_engine_class engine = INTEL_ENGINE_CLASS_RENDER;
        engine < ARRAY_SIZE(devinfo->engine_class_prefetch); engine++)
      devinfo->engine_class_prefetch[engine] =
            intel_device_info_calc_engine_prefetch(devinfo, engine);

   intel_device_info_init_was(devinfo);
   intel_device_info_apply_workarounds(devinfo);

   return true;
}

bool intel_device_info_update_memory_info(struct intel_device_info *devinfo, int fd)
{
   bool ret;

   switch (devinfo->kmd_type) {
   case INTEL_KMD_TYPE_I915:
      ret = intel_device_info_i915_query_regions(devinfo, fd, true);
      break;
   case INTEL_KMD_TYPE_XE:
      ret = intel_device_info_xe_query_regions(fd, devinfo, true);
      break;
   default:
      ret = false;
   }

   if (ret)
      intel_device_info_adjust_memory(devinfo);
   return ret;
}

enum intel_wa_steppings
intel_device_info_wa_stepping(struct intel_device_info *devinfo)
{
   /* When adding platforms to this function, check to see if
    * stepping-specific workarounds impact the compiler.
    *
    * If a stepping specific compiler workaround is required on a released
    * platform, intel_device_info->revision must be added as a
    * 'compiler_field' in intel_device_info.py
    */

   if (devinfo->platform == INTEL_PLATFORM_PTL) {
      switch (devinfo->revision) {
      case 0:
         return INTEL_STEPPING_A0;
      case 4:
         return INTEL_STEPPING_B0;
      default:
         return INTEL_STEPPING_RELEASE;
      }
   } else if (devinfo->platform == INTEL_PLATFORM_BMG) {
      switch (devinfo->revision) {
      case 0:
         return INTEL_STEPPING_A0;
      case 1:
         return INTEL_STEPPING_A1;
      case 4:
         return INTEL_STEPPING_B0;
      default:
         return INTEL_STEPPING_RELEASE;
      }
   } else if (devinfo->platform == INTEL_PLATFORM_LNL) {
      switch (devinfo->revision) {
      case 0:
         return INTEL_STEPPING_A0;
      case 1:
         return INTEL_STEPPING_A1;
      case 4:
         return INTEL_STEPPING_B0;
      default:
         return INTEL_STEPPING_RELEASE;
      }
   } else if (devinfo->platform == INTEL_PLATFORM_TGL) {
      /* TGL production steppings: B0 and C0 */
      switch (devinfo->revision) {
      case 1:
         return INTEL_STEPPING_B0;
      case 3:
         return INTEL_STEPPING_C0;
      default:
         return INTEL_STEPPING_RELEASE;
      }
   }

   /* all other platforms support only released steppings */
   return INTEL_STEPPING_RELEASE;
}

uint32_t
intel_device_info_get_max_slm_size(const struct intel_device_info *devinfo)
{
   uint32_t bytes = 0;

   if (devinfo->verx10 >= 300) {
      bytes = 128 * 1024;
   } else if (devinfo->verx10 >= 200) {
      bytes = intel_device_info_get_max_preferred_slm_size(devinfo);
   } else {
      bytes = 64 * 1024;
   }

   return bytes;
}

uint32_t
intel_device_info_get_max_preferred_slm_size(const struct intel_device_info *devinfo)
{
   uint32_t k_bytes = 0;

   if (devinfo->verx10 >= 300) {
      k_bytes = 192;
   } else if (devinfo->verx10 >= 200) {
      if (intel_needs_workaround(devinfo, 16018610683))
         k_bytes = 128;
      else
         k_bytes = 160;
   } else {
      k_bytes = 128;
   }

   return k_bytes * 1024;
}
