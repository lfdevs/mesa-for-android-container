/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 */

#ifndef DISASM_H_
#define DISASM_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "compiler/shader_enums.h"

/* bitmask of debug flags */
enum debug_t {
   PRINT_RAW = 0x1, /* dump raw hexdump */
   PRINT_VERBOSE = 0x2,
   PRINT_STATS = 0x4,
   EXPAND_REPEAT = 0x8,
};

/* bitmask of sampler/memobj descriptor usage per bindless/bindfull
 * bases.  If the sampler/texture is not immediate (such as .s2en)
 * then the corresponding bitmask is set to ~0 (ie. all used).  For
 * simplicity, #tN immediate > 64 marks all used (~0)
 */
struct descriptor_stats {
   uint64_t img;             /* img/img_bindless descriptor usage */
   uint64_t tex;             /* non img/img_bindless cat5/cat6 descriptor usage */
   uint64_t samp;            /* cat5 sampler usage */
   uint64_t ubo;             /* ubo descriptor usage */
};

/* Keep in sync with domain "ir3_shader_stats" in xml */
struct shader_stats {
   /* desc[0] is bindfull, desc[N+1] is .baseN: */
   struct descriptor_stats desc[9];
   uint32_t has_img;
   uint32_t has_tex;
   uint32_t has_samp;
   uint32_t has_ubo;
   /* instructions counts rpnN, and instlen does not */
   int instructions, instlen;
   int nops;
   int ss, sy;
   int constlen;
   int halfreg;
   int fullreg;
   uint16_t sstall;
   uint16_t mov_count;
   uint16_t cov_count;
   uint16_t last_baryf;
   uint16_t instrs_per_cat[8];
};

int disasm_a2xx(const uint32_t *dwords, int sizedwords, int level,
                mesa_shader_stage type);
int disasm_a3xx(const uint32_t *dwords, int sizedwords, int level, FILE *out,
                unsigned gpu_id);
int disasm_a3xx_stat(const uint32_t *dwords, int sizedwords, int level,
                     FILE *out, unsigned gpu_id, struct shader_stats *stats);
int try_disasm_a3xx(const uint32_t *dwords, int sizedwords, int level,
                    FILE *out, unsigned gpu_id);
int try_disasm_a3xx_stat(const uint32_t *dwords, int sizedwords, int level,
                         FILE *out, unsigned gpu_id,
                         struct shader_stats *stats);

void disasm_a2xx_set_debug(enum debug_t debug);
void disasm_a3xx_set_debug(enum debug_t debug);

#endif /* DISASM_H_ */
