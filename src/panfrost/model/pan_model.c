/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "pan_model.h"

/* GPU revision (rXpY) */
#define GPU_REV(X, Y) (((X) & 0xf) << 12 | ((Y) & 0xff) << 4)

/* Fixed "minimum revisions" */
#define GPU_REV_NONE (~0)
#define GPU_REV_ALL  GPU_REV(0, 0)
#define GPU_REV_R0P3 GPU_REV(0, 3)
#define GPU_REV_R1P1 GPU_REV(1, 1)

#define MODEL(gpu_prod_id_, gpu_prod_id_mask_, gpu_variant_, shortname,        \
              counters, ...)                                                   \
   {                                                                           \
      .gpu_prod_id = gpu_prod_id_,                                             \
      .gpu_prod_id_mask = gpu_prod_id_mask_,                                   \
      .gpu_variant = gpu_variant_,                                             \
      .name = "Mali-" shortname,                                               \
      .performance_counters = counters,                                        \
      ##__VA_ARGS__,                                                           \
   }

#define MIDGARD_MODEL(gpu_prod_id, shortname, counters, ...)                   \
   MODEL(gpu_prod_id << 16, 0xffff0000, 0, shortname, counters, ##__VA_ARGS__)

#define BIFROST_MODEL(gpu_prod_id, shortname, counters, ...)                   \
   MODEL(gpu_prod_id << 16, ARCH_MAJOR | ARCH_MINOR | PRODUCT_MAJOR, 0,        \
         shortname, counters, ##__VA_ARGS__)

#define VALHALL_MODEL(gpu_prod_id, gpu_variant, shortname, counters, ...)      \
   MODEL(gpu_prod_id << 16, ARCH_MAJOR | ARCH_MINOR | PRODUCT_MAJOR,           \
         gpu_variant, shortname, counters, ##__VA_ARGS__)

#define AVALON_MODEL(gpu_prod_id, gpu_variant, shortname, counters, ...)       \
   MODEL(gpu_prod_id << 16, ARCH_MAJOR | ARCH_MINOR | PRODUCT_MAJOR,           \
         gpu_variant, shortname, counters, ##__VA_ARGS__)

#define MODEL_ANISO(rev) .min_rev_anisotropic = GPU_REV_##rev

#define MODEL_TB_SIZES(color_tb_size, z_tb_size)                               \
   .tilebuffer = {                                                             \
      .color_size = color_tb_size,                                             \
      .z_size = z_tb_size,                                                     \
   }

#define MODEL_RATES(pixel_rate, texel_rate, fma_rate)                          \
   .rates = {                                                                  \
      .pixel = pixel_rate,                                                     \
      .texel = texel_rate,                                                     \
      .fma = fma_rate,                                                         \
   }

#define MODEL_QUIRKS(...) .quirks = {__VA_ARGS__}

/* Table of supported Mali GPUs */
/* clang-format off */
const struct pan_model pan_model_list[] = {
   MIDGARD_MODEL(0x600,     "T600",   "T60x", MODEL_ANISO(NONE), MODEL_TB_SIZES( 4096,  4096),
                                              MODEL_QUIRKS( .max_4x_msaa = true )),
   MIDGARD_MODEL(0x620,     "T620",   "T62x", MODEL_ANISO(NONE), MODEL_TB_SIZES( 4096,  4096)),
   MIDGARD_MODEL(0x720,     "T720",   "T72x", MODEL_ANISO(NONE), MODEL_TB_SIZES( 4096,  4096),
                                              MODEL_QUIRKS( .no_hierarchical_tiling = true, .max_4x_msaa = true )),
   MIDGARD_MODEL(0x750,     "T760",   "T76x", MODEL_ANISO(NONE), MODEL_TB_SIZES( 8192,  8192)),
   MIDGARD_MODEL(0x820,     "T820",   "T82x", MODEL_ANISO(NONE), MODEL_TB_SIZES( 8192,  8192),
                                              MODEL_QUIRKS( .no_hierarchical_tiling = true, .max_4x_msaa = true )),
   MIDGARD_MODEL(0x830,     "T830",   "T83x", MODEL_ANISO(NONE), MODEL_TB_SIZES( 8192,  8192),
                                              MODEL_QUIRKS( .no_hierarchical_tiling = true, .max_4x_msaa = true )),
   MIDGARD_MODEL(0x860,     "T860",   "T86x", MODEL_ANISO(NONE), MODEL_TB_SIZES( 8192,  8192)),
   MIDGARD_MODEL(0x880,     "T880",   "T88x", MODEL_ANISO(NONE), MODEL_TB_SIZES( 8192,  8192)),

   BIFROST_MODEL(0x6000,    "G71",    "TMIx", MODEL_ANISO(NONE), MODEL_TB_SIZES( 4096,  4096)),
   BIFROST_MODEL(0x6201,    "G72",    "THEx", MODEL_ANISO(R0P3), MODEL_TB_SIZES( 8192,  4096)),
   BIFROST_MODEL(0x7000,    "G51",    "TSIx", MODEL_ANISO(R1P1), MODEL_TB_SIZES( 8192,  8192)),
   BIFROST_MODEL(0x7003,    "G31",    "TDVx", MODEL_ANISO(ALL),  MODEL_TB_SIZES( 8192,  8192)),
   BIFROST_MODEL(0x7201,    "G76",    "TNOx", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192)),
   BIFROST_MODEL(0x7202,    "G52",    "TGOx", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192)),
   BIFROST_MODEL(0x7402,    "G52 r1", "TGOx", MODEL_ANISO(ALL),  MODEL_TB_SIZES( 8192,  8192)),

   VALHALL_MODEL(0x9001, 0, "G57",    "TNAx", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES(2, 4,  32)),
   VALHALL_MODEL(0x9003, 0, "G57",    "TNAx", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES(2, 4,  32)),
   VALHALL_MODEL(0xa807, 0, "G610",   "TVIx", MODEL_ANISO(ALL),  MODEL_TB_SIZES(32768, 16384),
                                              MODEL_RATES(4, 8,  64)),
   VALHALL_MODEL(0xac04, 0, "G310",   "TVAx", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES(2, 2,  16)),
   VALHALL_MODEL(0xac04, 1, "G310",   "TVAx", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES(2, 4,  32)),
   VALHALL_MODEL(0xac04, 2, "G310",   "TVAx", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES(4, 4,  48)),
   VALHALL_MODEL(0xac04, 3, "G310",   "TVAx", MODEL_ANISO(ALL),  MODEL_TB_SIZES(32768, 16384),
                                              MODEL_RATES(4, 8,  48)),
   VALHALL_MODEL(0xac04, 4, "G310",   "TVAx", MODEL_ANISO(ALL),  MODEL_TB_SIZES(32768, 16384),
                                              MODEL_RATES(4, 8,  64)),

   AVALON_MODEL( 0xc800, 4, "G720",   "TTIx", MODEL_ANISO(ALL),  MODEL_TB_SIZES(65536, 32768),
                                              MODEL_RATES(4, 8, 128)),
   AVALON_MODEL( 0xd800, 4, "G725",   "TKRx", MODEL_ANISO(ALL),  MODEL_TB_SIZES(65536, 65536),
                                              MODEL_RATES(4, 8, 128)),
};
/* clang-format on */

#undef GPU_REV
#undef GPU_REV_NONE
#undef GPU_REV_ALL
#undef GPU_REV_R0P3
#undef GPU_REV_R1P1

#undef MIDGARD_MODEL
#undef BIFROST_MODEL
#undef VALHALL_MODEL
#undef AVALON_MODEL
#undef MODEL

#undef MODEL_ANISO
#undef MODEL_TB_SIZES
#undef MODEL_RATES
#undef MODEL_QUIRKS

/*
 * Look up a supported model by its GPU ID, or return NULL if the model is not
 * supported at this time.
 */
const struct pan_model *
pan_get_model(uint32_t gpu_id, uint32_t gpu_variant)
{
   for (unsigned i = 0; i < ARRAY_SIZE(pan_model_list); ++i) {
      uint32_t gpu_prod_id = gpu_id & pan_model_list[i].gpu_prod_id_mask;

      if (pan_model_list[i].gpu_prod_id == gpu_prod_id &&
          pan_model_list[i].gpu_variant == gpu_variant)
         return &pan_model_list[i];
   }

   return NULL;
}
