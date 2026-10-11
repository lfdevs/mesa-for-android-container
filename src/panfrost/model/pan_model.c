/*
 * Copyright (C) 2019 Collabora, Ltd.
 * Copyright (C) 2026 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_model.h"

/* Fixed "minimum revisions" */
#define GPU_REV_NONE (~0)
#define GPU_REV_ALL  PAN_REV(0, 0)
#define GPU_REV_R0P3 PAN_REV(0, 3)
#define GPU_REV_R1P1 PAN_REV(1, 1)

#define GPU_FEAT_RAY_INTERSECTION (1u << 2)

#define MODEL(gpu_prod_id_, gpu_variant_, shortname, counters, ...)            \
   {                                                                           \
      .gpu_prod_id = gpu_prod_id_,                                             \
      .gpu_variant = gpu_variant_,                                             \
      .name = "Mali-" shortname,                                               \
      .performance_counters = counters,                                        \
      ##__VA_ARGS__,                                                           \
   }

#define MIDGARD_MODEL(gpu_prod_id, shortname, counters, ...)                   \
   MODEL(gpu_prod_id, 0, shortname, counters, ##__VA_ARGS__)

#define BIFROST_MODEL(gpu_prod_id, shortname, counters, ...)                   \
   MODEL(gpu_prod_id, 0, shortname, counters, ##__VA_ARGS__)

#define VALHALL_MODEL(gpu_prod_id, gpu_variant, shortname, counters, ...)      \
   MODEL(gpu_prod_id, gpu_variant, shortname, counters, ##__VA_ARGS__)

#define FIFTHGEN_MODEL(gpu_prod_id, gpu_variant, shortname, counters, ...)     \
   MODEL(gpu_prod_id, gpu_variant, shortname, counters, ##__VA_ARGS__)

#define MODEL_ANISO(rev) .min_rev_anisotropic = GPU_REV_##rev

#define MODEL_TB_SIZES(color_tb_size, z_tb_size)                               \
   .tilebuffer = {                                                             \
      .color_size = color_tb_size,                                             \
      .z_size = z_tb_size,                                                     \
   }

#define MODEL_RATES_X(pixel_rate, texel_rate, var_rate, cvt_rate,              \
                     fma_rate, sfu_rate)                                       \
   .rates = {                                                                  \
      .pixel = pixel_rate,                                                     \
      .texel = texel_rate,                                                     \
      .varying = var_rate,                                                     \
      .cvt = cvt_rate,                                                         \
      .fma = fma_rate,                                                         \
      .sfu = sfu_rate,                                                         \
   }

#define MODEL_RATES(pixel_rate, texel_rate, fma_rate)                          \
   MODEL_RATES_X(pixel_rate, texel_rate, 0, 0, fma_rate, 0)

#define MODEL_QUIRKS(...) .quirks = {__VA_ARGS__}

/* Table of supported Mali GPUs */
/* clang-format off */
const struct pan_model pan_model_list[] = {
   MIDGARD_MODEL(0x600,     "T600",   "T60x", MODEL_ANISO(NONE), MODEL_TB_SIZES( 4096,  4096),
                                              MODEL_QUIRKS( .max_4x_msaa = true )),
   MIDGARD_MODEL(0x620,     "T620",   "T62x", MODEL_ANISO(NONE), MODEL_TB_SIZES( 4096,  4096)),
   MIDGARD_MODEL(0x720,     "T720",   "T720", MODEL_ANISO(NONE), MODEL_TB_SIZES( 4096,  4096),
                                              MODEL_QUIRKS( .no_hierarchical_tiling = true, .max_4x_msaa = true )),
   MIDGARD_MODEL(0x750,     "T760",   "T760", MODEL_ANISO(NONE), MODEL_TB_SIZES( 8192,  8192)),
   MIDGARD_MODEL(0x820,     "T820",   "T830", MODEL_ANISO(NONE), MODEL_TB_SIZES( 8192,  8192),
                                              MODEL_QUIRKS( .no_hierarchical_tiling = true, .max_4x_msaa = true )),
   MIDGARD_MODEL(0x830,     "T830",   "T830", MODEL_ANISO(NONE), MODEL_TB_SIZES( 8192,  8192),
                                              MODEL_QUIRKS( .no_hierarchical_tiling = true, .max_4x_msaa = true )),
   MIDGARD_MODEL(0x860,     "T860",   "T880", MODEL_ANISO(NONE), MODEL_TB_SIZES( 8192,  8192)),
   MIDGARD_MODEL(0x880,     "T880",   "T880", MODEL_ANISO(NONE), MODEL_TB_SIZES( 8192,  8192)),

   /* Bifrost pixel, texel and FMA rates are from ARM's GPU datasheet, while
    * varying rates aren't confirmed yet.
    */
   BIFROST_MODEL(PAN_PROD_ID(6, 0, 0),    "G71",    "G71", MODEL_ANISO(NONE), MODEL_TB_SIZES( 4096,  4096), MODEL_RATES(1, 1, 12)),
   BIFROST_MODEL(PAN_PROD_ID(6, 2, 1),    "G72",    "G72", MODEL_ANISO(R0P3), MODEL_TB_SIZES( 8192,  4096), MODEL_RATES(1, 1, 12)),
   BIFROST_MODEL(PAN_PROD_ID(7, 0, 0),    "G51",    "G51", MODEL_ANISO(R1P1), MODEL_TB_SIZES( 8192,  8192), MODEL_RATES(2, 2, 12)),
   BIFROST_MODEL(PAN_PROD_ID(7, 0, 3),    "G31",    "G31", MODEL_ANISO(ALL),  MODEL_TB_SIZES( 8192,  8192), MODEL_RATES(2, 2,  8)),
   BIFROST_MODEL(PAN_PROD_ID(7, 2, 1),    "G76",    "G76", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192), MODEL_RATES(2, 2, 24)),
   BIFROST_MODEL(PAN_PROD_ID(7, 2, 2),    "G52",    "G52", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192), MODEL_RATES(2, 2, 24)),
   BIFROST_MODEL(PAN_PROD_ID(7, 4, 2),    "G52 r1", "G52", MODEL_ANISO(ALL),  MODEL_TB_SIZES( 8192,  8192), MODEL_RATES(2, 2, 24)),

   /* G77 (kbase TTRX, 0x9080): same Valhall v9 core as G57, 7-16 cores. */
   VALHALL_MODEL(PAN_PROD_ID(9, 0, 0), 0, "G77",    "G77", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES_X(2, 4, 8,  32,  32, 8)),
   VALHALL_MODEL(PAN_PROD_ID(9, 0, 1), 0, "G57",    "G77", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES_X(2, 4, 8,  32,  32, 8)),
   VALHALL_MODEL(PAN_PROD_ID(9, 0, 3), 0, "G57",    "G77", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES_X(2, 4, 8,  32,  32, 8)),
   VALHALL_MODEL(PAN_PROD_ID(9, 2, 4), 0, "G68",    "G78", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES_X(2, 4, 8,  32,  32, 8)),
   /* G78 (kbase TBEX, product major 2; Tensor gs101 reports 0x9202) and
    * G78AE (product major 5): same v9 core and 16 KiB colour tile buffer as
    * the G57/G68. Transaction elimination produces stale tiles on G78
    * (gs101); panvk only enables CRC on v10+, so it stays off here. */
   VALHALL_MODEL(PAN_PROD_ID(9, 2, 2), 0, "G78",    "G78", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES_X(2, 4, 8,  32,  32, 8)),
   VALHALL_MODEL(PAN_PROD_ID(9, 0, 5), 0, "G78AE",  "G78", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES_X(2, 4, 8,  32,  32, 8)),
   /* G710 (kbase TODX, 0xa862): same Valhall v10 core as G610, 7-16 cores. */
   VALHALL_MODEL(PAN_PROD_ID(10, 8, 2), 0, "G710",   "G710", MODEL_ANISO(ALL),  MODEL_TB_SIZES(32768, 16384),
                                              MODEL_RATES_X(4, 8, 16,  64,  64, 16)),
   VALHALL_MODEL(PAN_PROD_ID(10, 8, 7), 0, "G610",   "G710", MODEL_ANISO(ALL),  MODEL_TB_SIZES(32768, 16384),
                                              MODEL_RATES_X(4, 8, 16,  64,  64, 16)),
   /* var/cvt/sfu rates might not be correct (we haven't found any detailed documentation) */
   VALHALL_MODEL(PAN_PROD_ID(10, 12, 4), 0, "G310v1",   "G710", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES_X(2, 2,  8,  16,  16, 4)),
   VALHALL_MODEL(PAN_PROD_ID(10, 12, 4), 1, "G310v2",   "G710", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES_X(2, 4,  8,  32,  32, 8)),
   VALHALL_MODEL(PAN_PROD_ID(10, 12, 4), 2, "G310v3",   "G710", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES_X(4, 4,  8,  48,  48, 12)),
   VALHALL_MODEL(PAN_PROD_ID(10, 12, 4), 3, "G310v4",   "G710", MODEL_ANISO(ALL),  MODEL_TB_SIZES(32768, 16384),
                                              MODEL_RATES_X(4, 8,  8,  48,  48, 12)),
   VALHALL_MODEL(PAN_PROD_ID(10, 12, 4), 4, "G310v5",   "G710", MODEL_ANISO(ALL),  MODEL_TB_SIZES(32768, 16384),
                                              MODEL_RATES_X(4, 8,  8,  64,  64, 16)),

   VALHALL_MODEL(PAN_PROD_ID(11, 8, 2), 4, "G715-Immortalis", "G715", MODEL_ANISO(ALL), MODEL_TB_SIZES(32768, 16384),
                                              MODEL_RATES_X(4, 8,  8,  64,  64, 16),
                                              .min_core_count = 10,
                                              .required_gpu_features = GPU_FEAT_RAY_INTERSECTION),
   VALHALL_MODEL(PAN_PROD_ID(11, 8, 2), 4,  "G715",   "G715", MODEL_ANISO(ALL),  MODEL_TB_SIZES(32768, 16384),
                                              MODEL_RATES_X(4, 8, 32,  64, 128, 16)),

   VALHALL_MODEL(PAN_PROD_ID(11, 8, 3), 4,  "G615",   "G715", MODEL_ANISO(ALL),  MODEL_TB_SIZES(32768, 16384),
                                              MODEL_RATES_X(4, 8, 32,  64, 128, 16)),

   FIFTHGEN_MODEL(PAN_PROD_ID(12, 8, 0), 4, "G720-Immortalis", "G720", MODEL_ANISO(ALL), MODEL_TB_SIZES(65536, 32768),
                                              MODEL_RATES_X(4, 8, 32,  64, 128, 16),
                                              .min_core_count = 10,
                                              .required_gpu_features = GPU_FEAT_RAY_INTERSECTION),
   FIFTHGEN_MODEL(PAN_PROD_ID(12, 8, 0), 4, "G720",  "G720", MODEL_ANISO(ALL),  MODEL_TB_SIZES(65536, 32768),
                                              MODEL_RATES_X(4, 8, 32,  64, 128, 16)),

   /** G925 is a G725 with ray tracing and sufficiently many cores. Keep this before G725 to prefer it when both match. */
   FIFTHGEN_MODEL(PAN_PROD_ID(13, 8, 0), 4, "G925-Immortalis", "G725", MODEL_ANISO(ALL),  MODEL_TB_SIZES(65536, 65536),
                                              MODEL_RATES_X(4, 8, 32, 128, 128, 16),
                                              .min_core_count = 10,
                                              .required_gpu_features = GPU_FEAT_RAY_INTERSECTION),
   FIFTHGEN_MODEL(PAN_PROD_ID(13, 8, 0), 4, "G725",  "G725", MODEL_ANISO(ALL),  MODEL_TB_SIZES(65536, 65536),
                                              MODEL_RATES_X(4, 8, 32, 128, 128, 16)),

   FIFTHGEN_MODEL(PAN_PROD_ID(14, 8, 0), 4, "G1-Ultra",  "G1", MODEL_ANISO(ALL),  MODEL_TB_SIZES(65536, 65536),
                                              MODEL_RATES(4, 8, 128)),
   FIFTHGEN_MODEL(PAN_PROD_ID(14, 8, 1), 4, "G1-Premium",  "G1", MODEL_ANISO(ALL),  MODEL_TB_SIZES(65536, 65536),
                                              MODEL_RATES(4, 8, 128)),
   FIFTHGEN_MODEL(PAN_PROD_ID(14, 8, 3), 1, "G1-Pro",  "G1", MODEL_ANISO(ALL),  MODEL_TB_SIZES(65536, 65536),
                                              MODEL_RATES(4, 4, 64)),
   FIFTHGEN_MODEL(PAN_PROD_ID(14, 8, 3), 4, "G1-Pro",  "G1", MODEL_ANISO(ALL),  MODEL_TB_SIZES(65536, 65536),
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
#undef FIFTHGEN_MODEL
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
pan_get_model(uint64_t gpu_id, uint32_t gpu_variant,
              unsigned core_count, uint64_t gpu_features)
{
   uint32_t gpu_prod_id = pan_prod_id(gpu_id);
   for (unsigned i = 0; i < ARRAY_SIZE(pan_model_list); ++i) {
      if (pan_model_list[i].gpu_prod_id == gpu_prod_id &&
          pan_model_list[i].gpu_variant == gpu_variant &&
          pan_model_list[i].min_core_count <= core_count &&
          (pan_model_list[i].required_gpu_features & gpu_features) ==
          pan_model_list[i].required_gpu_features)
         return &pan_model_list[i];
   }

   /* kbase names v9 products by arch major and product major only
    * (GPU_ID2_PRODUCT_MODEL, mask 0xf00f): arch minor is not part of the
    * product, and parts report different values (gs101's G78: 0x9202). */
   if ((gpu_prod_id >> 16) == 9) {
      for (unsigned i = 0; i < ARRAY_SIZE(pan_model_list); ++i) {
         if ((pan_model_list[i].gpu_prod_id & ~0xff00) ==
                (gpu_prod_id & ~0xff00) &&
             pan_model_list[i].gpu_variant == gpu_variant)
            return &pan_model_list[i];
      }
   }

   return NULL;
}
