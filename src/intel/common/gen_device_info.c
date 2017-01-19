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

#include <stdio.h>
#include <stdlib.h>
#include "gen_device_info.h"
#include "compiler/shader_enums.h"

static const struct gen_device_info gen_device_info_i965 = {
   .gen = 4,
   .has_negative_rhw_bug = true,
   .num_slices = 1,
   .max_vs_threads = 16,
   .max_gs_threads = 2,
   .max_wm_threads = 8 * 4,
   .urb = {
      .size = 256,
   },
};

static const struct gen_device_info gen_device_info_g4x = {
   .gen = 4,
   .has_pln = true,
   .has_compr4 = true,
   .has_surface_tile_offset = true,
   .is_g4x = true,
   .num_slices = 1,
   .max_vs_threads = 32,
   .max_gs_threads = 2,
   .max_wm_threads = 10 * 5,
   .urb = {
      .size = 384,
   },
};

static const struct gen_device_info gen_device_info_ilk = {
   .gen = 5,
   .has_pln = true,
   .has_compr4 = true,
   .has_surface_tile_offset = true,
   .num_slices = 1,
   .max_vs_threads = 72,
   .max_gs_threads = 32,
   .max_wm_threads = 12 * 6,
   .urb = {
      .size = 1024,
   },
};

static const struct gen_device_info gen_device_info_snb_gt1 = {
   .gen = 6,
   .gt = 1,
   .has_hiz_and_separate_stencil = true,
   .has_llc = true,
   .has_pln = true,
   .has_surface_tile_offset = true,
   .needs_unlit_centroid_workaround = true,
   .num_slices = 1,
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
};

static const struct gen_device_info gen_device_info_snb_gt2 = {
   .gen = 6,
   .gt = 2,
   .has_hiz_and_separate_stencil = true,
   .has_llc = true,
   .has_pln = true,
   .has_surface_tile_offset = true,
   .needs_unlit_centroid_workaround = true,
   .num_slices = 1,
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
};

#define GEN7_FEATURES                               \
   .gen = 7,                                        \
   .has_hiz_and_separate_stencil = true,            \
   .must_use_separate_stencil = true,               \
   .has_llc = true,                                 \
   .has_pln = true,                                 \
   .has_surface_tile_offset = true

static const struct gen_device_info gen_device_info_ivb_gt1 = {
   GEN7_FEATURES, .is_ivybridge = true, .gt = 1,
   .num_slices = 1,
   .max_vs_threads = 36,
   .max_tcs_threads = 36,
   .max_tes_threads = 36,
   .max_gs_threads = 36,
   .max_wm_threads = 48,
   .max_cs_threads = 36,
   .urb = {
      .size = 128,
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
};

static const struct gen_device_info gen_device_info_ivb_gt2 = {
   GEN7_FEATURES, .is_ivybridge = true, .gt = 2,
   .num_slices = 1,
   .max_vs_threads = 128,
   .max_tcs_threads = 128,
   .max_tes_threads = 128,
   .max_gs_threads = 128,
   .max_wm_threads = 172,
   .max_cs_threads = 64,
   .urb = {
      .size = 256,
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
};

static const struct gen_device_info gen_device_info_byt = {
   GEN7_FEATURES, .is_baytrail = true, .gt = 1,
   .num_slices = 1,
   .has_llc = false,
   .max_vs_threads = 36,
   .max_tcs_threads = 36,
   .max_tes_threads = 36,
   .max_gs_threads = 36,
   .max_wm_threads = 48,
   .max_cs_threads = 32,
   .urb = {
      .size = 128,
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
};

#define HSW_FEATURES             \
   GEN7_FEATURES,                \
   .is_haswell = true,           \
   .supports_simd16_3src = true, \
   .has_resource_streamer = true

static const struct gen_device_info gen_device_info_hsw_gt1 = {
   HSW_FEATURES, .gt = 1,
   .num_slices = 1,
   .max_vs_threads = 70,
   .max_tcs_threads = 70,
   .max_tes_threads = 70,
   .max_gs_threads = 70,
   .max_wm_threads = 102,
   .max_cs_threads = 70,
   .urb = {
      .size = 128,
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
};

static const struct gen_device_info gen_device_info_hsw_gt2 = {
   HSW_FEATURES, .gt = 2,
   .num_slices = 1,
   .max_vs_threads = 280,
   .max_tcs_threads = 256,
   .max_tes_threads = 280,
   .max_gs_threads = 256,
   .max_wm_threads = 204,
   .max_cs_threads = 70,
   .urb = {
      .size = 256,
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
};

static const struct gen_device_info gen_device_info_hsw_gt3 = {
   HSW_FEATURES, .gt = 3,
   .num_slices = 2,
   .max_vs_threads = 280,
   .max_tcs_threads = 256,
   .max_tes_threads = 280,
   .max_gs_threads = 256,
   .max_wm_threads = 408,
   .max_cs_threads = 70,
   .urb = {
      .size = 512,
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
};

#define GEN8_FEATURES                               \
   .gen = 8,                                        \
   .has_hiz_and_separate_stencil = true,            \
   .has_resource_streamer = true,                   \
   .must_use_separate_stencil = true,               \
   .has_llc = true,                                 \
   .has_pln = true,                                 \
   .supports_simd16_3src = true,                    \
   .has_surface_tile_offset = true,                 \
   .max_vs_threads = 504,                           \
   .max_tcs_threads = 504,                          \
   .max_tes_threads = 504,                          \
   .max_gs_threads = 504,                           \
   .max_wm_threads = 384

static const struct gen_device_info gen_device_info_bdw_gt1 = {
   GEN8_FEATURES, .gt = 1,
   .num_slices = 1,
   .max_cs_threads = 42,
   .urb = {
      .size = 192,
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
   }
};

static const struct gen_device_info gen_device_info_bdw_gt2 = {
   GEN8_FEATURES, .gt = 2,
   .num_slices = 1,
   .max_cs_threads = 56,
   .urb = {
      .size = 384,
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
   }
};

static const struct gen_device_info gen_device_info_bdw_gt3 = {
   GEN8_FEATURES, .gt = 3,
   .num_slices = 2,
   .max_cs_threads = 56,
   .urb = {
      .size = 384,
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
   }
};

static const struct gen_device_info gen_device_info_chv = {
   GEN8_FEATURES, .is_cherryview = 1, .gt = 1,
   .has_llc = false,
   .num_slices = 1,
   .max_vs_threads = 80,
   .max_tcs_threads = 80,
   .max_tes_threads = 80,
   .max_gs_threads = 80,
   .max_wm_threads = 128,
   .max_cs_threads = 6 * 7,
   .urb = {
      .size = 192,
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
   }
};

#define GEN9_FEATURES                               \
   .gen = 9,                                        \
   .has_hiz_and_separate_stencil = true,            \
   .has_resource_streamer = true,                   \
   .must_use_separate_stencil = true,               \
   .has_llc = true,                                 \
   .has_pln = true,                                 \
   .supports_simd16_3src = true,                    \
   .has_surface_tile_offset = true,                 \
   .max_vs_threads = 336,                           \
   .max_gs_threads = 336,                           \
   .max_tcs_threads = 336,                          \
   .max_tes_threads = 336,                          \
   .max_cs_threads = 56,                            \
   .urb = {                                         \
      .size = 384,                                  \
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

#define GEN9_LP_FEATURES                           \
   GEN9_FEATURES,                                  \
   .is_broxton = 1,                                \
   .gt = 1,                                        \
   .has_llc = false,                               \
   .num_slices = 1,                                \
   .max_vs_threads = 112,                          \
   .max_tcs_threads = 112,                         \
   .max_tes_threads = 112,                         \
   .max_gs_threads = 112,                          \
   .max_cs_threads = 6 * 6,                        \
   .urb = {                                        \
      .size = 192,                                 \
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

#define GEN9_LP_FEATURES_2X6                       \
   GEN9_LP_FEATURES,                               \
   .max_vs_threads = 56,                           \
   .max_tcs_threads = 56,                          \
   .max_tes_threads = 56,                          \
   .max_gs_threads = 56,                           \
   .max_cs_threads = 6 * 6,                        \
   .urb = {                                        \
      .size = 128,                                 \
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

static const struct gen_device_info gen_device_info_skl_gt1 = {
   GEN9_FEATURES, .gt = 1,
   .num_slices = 1,
   .urb.size = 192,
};

static const struct gen_device_info gen_device_info_skl_gt2 = {
   GEN9_FEATURES, .gt = 2,
   .num_slices = 1,
};

static const struct gen_device_info gen_device_info_skl_gt3 = {
   GEN9_FEATURES, .gt = 3,
   .num_slices = 2,
};

static const struct gen_device_info gen_device_info_skl_gt4 = {
   GEN9_FEATURES, .gt = 4,
   .num_slices = 3,
   /* From the "L3 Allocation and Programming" documentation:
    *
    * "URB is limited to 1008KB due to programming restrictions.  This is not a
    * restriction of the L3 implementation, but of the FF and other clients.
    * Therefore, in a GT4 implementation it is possible for the programmed
    * allocation of the L3 data array to provide 3*384KB=1152KB for URB, but
    * only 1008KB of this will be used."
    */
   .urb.size = 1008 / 3,
};

static const struct gen_device_info gen_device_info_bxt = {
   GEN9_LP_FEATURES
};

static const struct gen_device_info gen_device_info_bxt_2x6 = {
   GEN9_LP_FEATURES_2X6
};
/*
 * Note: for all KBL SKUs, the PRM says SKL for GS entries, not SKL+.
 * There's no KBL entry. Using the default SKL (GEN9) GS entries value.
 */

static const struct gen_device_info gen_device_info_kbl_gt1 = {
   GEN9_FEATURES,
   .is_kabylake = true,
   .gt = 1,

   .max_cs_threads = 7 * 6,
   .urb.size = 192,
   .num_slices = 1,
};

static const struct gen_device_info gen_device_info_kbl_gt1_5 = {
   GEN9_FEATURES,
   .is_kabylake = true,
   .gt = 1,

   .max_cs_threads = 7 * 6,
   .num_slices = 1,
};

static const struct gen_device_info gen_device_info_kbl_gt2 = {
   GEN9_FEATURES,
   .is_kabylake = true,
   .gt = 2,

   .num_slices = 1,
};

static const struct gen_device_info gen_device_info_kbl_gt3 = {
   GEN9_FEATURES,
   .is_kabylake = true,
   .gt = 3,

   .num_slices = 2,
};

static const struct gen_device_info gen_device_info_kbl_gt4 = {
   GEN9_FEATURES,
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
   .urb.size = 1008 / 3,
   .num_slices = 3,
};

static const struct gen_device_info gen_device_info_glk = {
   GEN9_LP_FEATURES
};

static const struct gen_device_info gen_device_info_glk_2x6 = {
   GEN9_LP_FEATURES_2X6
};

bool
gen_get_device_info(int devid, struct gen_device_info *devinfo)
{
   switch (devid) {
#undef CHIPSET
#define CHIPSET(id, family, name) \
      case id: *devinfo = gen_device_info_##family; break;
#include "pci_ids/i965_pci_ids.h"
   default:
      fprintf(stderr, "i965_dri.so does not support the 0x%x PCI ID.\n", devid);
      return false;
   }

   /* From the Skylake PRM, 3DSTATE_PS::Scratch Space Base Pointer:
    *
    * "Scratch Space per slice is computed based on 4 sub-slices.  SW must
    *  allocate scratch space enough so that each slice has 4 slices allowed."
    *
    * The equivalent internal documentation says that this programming note
    * applies to all Gen9+ platforms.
    *
    * The hardware typically calculates the scratch space pointer by taking
    * the base address, and adding per-thread-scratch-space * thread ID.
    * Extra padding can be necessary depending how the thread IDs are
    * calculated for a particular shader stage.
    */
   if (devinfo->gen >= 9) {
      devinfo->max_wm_threads = 64 /* threads-per-PSD */
                              * devinfo->num_slices
                              * 4; /* effective subslices per slice */
   }

   return true;
}

const char *
gen_get_device_name(int devid)
{
   switch (devid) {
#undef CHIPSET
#define CHIPSET(id, family, name) case id: return name;
#include "pci_ids/i965_pci_ids.h"
   default:
      return NULL;
   }
}
