/*
 * Host emulation of panlib_vp_runs. Same .cl body.
 * Topology and the packed-slot rank are the production helpers.
 * libcl headers shadow libc, so they are not on the include path.
 * OpenCL address-space words are empty here.
 *
 *   gcc -std=c11 -Wall -Werror -o /tmp/vp-runs \
 *     src/panfrost/libpan/tests/vp_runs.c \
 *     -Isrc/panfrost/libpan -Iinclude -Isrc -Isrc/panfrost \
 *     && /tmp/vp-runs
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define global
#define constant
#define kernel
#define __global
#define __constant
#define __kernel
#define __attribute__(x)
typedef unsigned int uint;

#define UTIL_ARCH_LITTLE_ENDIAN 1
#define UTIL_ARCH_BIG_ENDIAN 0
#include "util/bitscan.h"
#include "compiler/shader_enums.h"
#include "poly/prim.h"

#define PANLIB_VP_HOST 1
#include "draw_helper_vp.h"
#include "vp_runs.cl"

#undef kernel
#undef global

#define CHECK(c)                                                               \
   do {                                                                        \
      if (!(c)) {                                                              \
         fprintf(stderr, "vp_runs: %s\n", #c);                                 \
         return 1;                                                             \
      }                                                                        \
   } while (0)

#define STRIDE 512u

static uint64_t
outputs_with_viewport(void)
{
   return ((1ull << VARYING_SLOT_VIEWPORT) - 1) | VARYING_BIT_VIEWPORT;
}

static uint32_t
vp_slot(void)
{
   return util_bitcount64(outputs_with_viewport() &
                          BITFIELD64_MASK(VARYING_SLOT_VIEWPORT));
}

static void
put_vp(uint32_t *rec, uint32_t id, uint32_t vp)
{
   rec[id * (STRIDE / 4) + vp_slot() * 4] = vp;
   rec[id * (STRIDE / 4)] = 7;
}

static void
call(const uint32_t *idx, uint32_t nidx, const uint32_t *rec, uint64_t outputs,
     uint32_t vp_count, uint32_t prim, uint32_t prov_last, uint32_t icap,
     uint32_t rcap, uint32_t *out, struct panlib_vp_run_header *hdr)
{
   memset(hdr, 0xa5, sizeof(*hdr) + 8 * sizeof(struct panlib_vp_run));
   memset(out, 0xa5, 64 * sizeof(uint32_t));
   panlib_vp_runs(idx, nidx, rec, STRIDE, outputs, vp_count, prim, prov_last,
                  icap, rcap, out, hdr);
}

static int
one_mode(uint32_t *rec, uint32_t prim, uint32_t prov_last, uint32_t nprim,
         const uint32_t *expect_idx, const uint32_t *expect_vp)
{
   uint32_t idx[16];
   uint32_t out[64];
   struct {
      struct panlib_vp_run_header h;
      struct panlib_vp_run r[8];
   } buf;
   uint32_t nidx = prim == MESA_PRIM_POINTS ? nprim :
                   prim == MESA_PRIM_LINES  ? nprim * 2 :
                                              2 + nprim;

   for (uint32_t i = 0; i < nidx; i++)
      idx[i] = 10 + i;

   call(idx, nidx, rec, outputs_with_viewport(), 4, prim, prov_last, 64, 8,
        out, &buf.h);
   CHECK(!buf.h.overflow);
   CHECK(buf.h.index_count == nidx);
   CHECK(buf.h.run_count >= 1);
   for (uint32_t i = 0; i < nidx; i++)
      CHECK(out[i] == expect_idx[i]);

   uint32_t covered = 0;
   for (uint32_t r = 0; r < buf.h.run_count; r++) {
      CHECK(buf.r[r].first_index == covered);
      CHECK(buf.r[r].viewport == expect_vp[r]);
      covered += buf.r[r].index_count;
   }
   CHECK(covered == nidx);
   return 0;
}

int
main(void)
{
   CHECK(sizeof(struct panlib_vp_run_header) == PANLIB_VP_RUN_HEADER_BYTES);
   CHECK(sizeof(struct panlib_vp_run) == PANLIB_VP_RUN_BYTES);
   CHECK(util_bitcount64(outputs_with_viewport() &
                         BITFIELD64_MASK(VARYING_SLOT_VIEWPORT)) ==
         VARYING_SLOT_VIEWPORT);

   uint64_t bytes = 0;
   CHECK(panlib_vp_run_bytes(0, &bytes) && bytes == PANLIB_VP_RUN_HEADER_BYTES);
   CHECK(panlib_vp_run_bytes(PANLIB_VP_MAX_RUNS, &bytes));
   CHECK(!panlib_vp_run_bytes(PANLIB_VP_MAX_RUNS + 1, &bytes));
   CHECK(bytes == PANLIB_VP_RUN_HEADER_BYTES + (uint64_t)PANLIB_VP_MAX_RUNS * 12);

   uint32_t *rec = calloc(40, STRIDE);
   CHECK(rec);
   memset(rec, 0, 32 * STRIDE);
   /* records 10..: viewports 0,0,1,1,1 then 2,9 */
   put_vp(rec, 10, 0);
   put_vp(rec, 11, 0);
   put_vp(rec, 12, 1);
   put_vp(rec, 13, 1);
   put_vp(rec, 14, 1);
   put_vp(rec, 15, 2);
   put_vp(rec, 16, 9);

   uint32_t out[64];
   struct {
      struct panlib_vp_run_header h;
      struct panlib_vp_run r[8];
   } buf;
   uint64_t outs = outputs_with_viewport();

   /* points: each point is its own run, 9 clamps to last viewport */
   {
      uint32_t idx[] = {10, 12, 16};
      call(idx, 3, rec, outs, 4, MESA_PRIM_POINTS, 0, 64, 8, out, &buf.h);
      CHECK(!buf.h.overflow && buf.h.run_count == 3 && buf.h.index_count == 3);
      CHECK(buf.r[0].viewport == 0 && buf.r[1].viewport == 1 &&
            buf.r[2].viewport == 3);
      CHECK(out[0] == 10 && out[1] == 12 && out[2] == 16);
   }

   /* lines, list: (10,11) vp0, (12,13) vp1. Provoking last. */
   {
      uint32_t ei[] = {10, 11, 12, 13};
      uint32_t ev[] = {0, 1};
      CHECK(one_mode(rec, MESA_PRIM_LINES, 1, 2, ei, ev) == 0);
   }

   /* line strip, provoking last: (10,11) vp0, (11,12) vp1 */
   {
      uint32_t idx[] = {10, 11, 12};
      call(idx, 3, rec, outs, 4, MESA_PRIM_LINE_STRIP, 1, 64, 8, out, &buf.h);
      CHECK(!buf.h.overflow && buf.h.run_count == 2);
      CHECK(buf.r[0].viewport == 0 && buf.r[0].index_count == 2);
      CHECK(buf.r[1].viewport == 1 && buf.r[1].first_index == 2);
      CHECK(out[2] == 11 && out[3] == 12);
   }

   /* triangles, list, one prim, provoking last is record 12 */
   {
      uint32_t ei[] = {10, 11, 12};
      uint32_t ev[] = {1};
      CHECK(one_mode(rec, MESA_PRIM_TRIANGLES, 1, 1, ei, ev) == 0);
   }

   /* triangle strip, provoking last, odd primitive swaps */
   {
      uint32_t idx[] = {10, 11, 12, 13, 14};
      call(idx, 5, rec, outs, 4, MESA_PRIM_TRIANGLE_STRIP, 1, 64, 8, out,
           &buf.h);
      CHECK(!buf.h.overflow && buf.h.index_count == 9 && buf.h.run_count == 1);
      CHECK(buf.r[0].viewport == 1 && buf.r[0].index_count == 9);
      CHECK(out[0] == 10 && out[1] == 11 && out[2] == 12);
      CHECK(out[3] == 12 && out[4] == 11 && out[5] == 13);
      CHECK(out[6] == 12 && out[7] == 13 && out[8] == 14);
   }

   /* provoking first splits the strip: vp0 then vp1 */
   {
      uint32_t idx[] = {10, 11, 12, 13, 14};
      call(idx, 5, rec, outs, 4, MESA_PRIM_TRIANGLE_STRIP, 0, 64, 8, out,
           &buf.h);
      CHECK(buf.h.run_count == 2);
      CHECK(buf.r[0].viewport == 0 && buf.r[0].index_count == 6);
      CHECK(buf.r[1].viewport == 1 && buf.r[1].first_index == 6 &&
            buf.r[1].index_count == 3);
      CHECK(out[3] == 11 && out[4] == 13 && out[5] == 12);
   }

   /* restart drops the tail of a strip and starts another */
   {
      uint32_t idx[] = {10, 11, 12, PANLIB_VP_RESTART, 15, 16};
      call(idx, 6, rec, outs, 4, MESA_PRIM_LINE_STRIP, 1, 64, 8, out, &buf.h);
      CHECK(!buf.h.overflow && buf.h.index_count == 6 && buf.h.run_count == 3);
      CHECK(buf.r[0].viewport == 0 && out[0] == 10 && out[1] == 11);
      CHECK(buf.r[1].viewport == 1 && out[2] == 11 && out[3] == 12);
      CHECK(buf.r[2].viewport == 3 && out[4] == 15 && out[5] == 16);
   }

   /* no viewport output: every provoking vertex is viewport 0, one run */
   {
      uint32_t idx[] = {10, 12, 16};
      call(idx, 3, rec, 0, 4, MESA_PRIM_POINTS, 0, 64, 8, out, &buf.h);
      CHECK(buf.h.run_count == 1 && buf.r[0].viewport == 0 &&
            buf.r[0].index_count == 3);
   }

   /* exact index cap merges into the open run; one past fails closed */
   {
      uint32_t idx[] = {10, 11, 12, 13};
      call(idx, 4, rec, outs, 4, MESA_PRIM_TRIANGLE_STRIP, 1, 6, 1, out,
           &buf.h);
      CHECK(!buf.h.overflow && buf.h.index_count == 6 && buf.h.run_count == 1);
      call(idx, 4, rec, outs, 4, MESA_PRIM_TRIANGLE_STRIP, 1, 5, 8, out,
           &buf.h);
      CHECK(buf.h.overflow == 1 && buf.h.index_count == 0 &&
            buf.h.run_count == 0);
   }

   /* exact run cap, then one past: different viewports cannot merge */
   {
      uint32_t idx[] = {10, 12};
      call(idx, 2, rec, outs, 4, MESA_PRIM_POINTS, 0, 64, 2, out, &buf.h);
      CHECK(!buf.h.overflow && buf.h.run_count == 2);
      call(idx, 2, rec, outs, 4, MESA_PRIM_POINTS, 0, 64, 1, out, &buf.h);
      CHECK(buf.h.overflow == 1 && buf.h.index_count == 0 &&
            buf.h.run_count == 0);
   }

   /* empty input and a restart-only buffer are valid empty draws */
   {
      call(NULL, 0, rec, outs, 4, MESA_PRIM_POINTS, 0, 64, 8, out, &buf.h);
      CHECK(!buf.h.overflow && buf.h.index_count == 0 && buf.h.run_count == 0);
      uint32_t holes[] = {PANLIB_VP_RESTART, PANLIB_VP_RESTART};
      call(holes, 2, rec, outs, 4, MESA_PRIM_TRIANGLE_STRIP, 1, 64, 8, out,
           &buf.h);
      CHECK(!buf.h.overflow && buf.h.index_count == 0 && buf.h.run_count == 0);
   }

   /* zero capacities: nothing emitted is fine, one primitive is overflow */
   {
      uint32_t idx[] = {10};
      call(idx, 0, rec, outs, 4, MESA_PRIM_POINTS, 0, 0, 0, out, &buf.h);
      CHECK(!buf.h.overflow && buf.h.index_count == 0);
      call(idx, 1, rec, outs, 4, MESA_PRIM_POINTS, 0, 0, 0, out, &buf.h);
      CHECK(buf.h.overflow == 1 && buf.h.index_count == 0 &&
            buf.h.run_count == 0);
   }

   /* absent viewport, record_stride 0: one run, viewport 0, no record read */
   {
      uint32_t idx[] = {10, 11, 12};
      memset(&buf, 0, sizeof(buf));
      panlib_vp_runs(idx, 3, NULL, 0, 0, 4, MESA_PRIM_POINTS, 0, 64, 8, out,
                     &buf.h);
      CHECK(!buf.h.overflow && buf.h.run_count == 1 && buf.r[0].viewport == 0 &&
            buf.r[0].index_count == 3);
   }

   /* sparse mask: only viewport bit set, so the slot rank is 0 */
   {
      uint32_t idx[] = {0};
      rec[0] = 2;
      call(idx, 1, rec, VARYING_BIT_VIEWPORT, 4, MESA_PRIM_POINTS, 0, 64, 8,
           out, &buf.h);
      CHECK(!buf.h.overflow && buf.h.run_count == 1 && buf.r[0].viewport == 2);
      rec[0] = 7;
   }

   /* quads are not a GS output mode */
   {
      uint32_t idx[] = {10, 11, 12, 13};
      call(idx, 4, rec, outs, 4, MESA_PRIM_QUADS, 1, 64, 8, out, &buf.h);
      CHECK(buf.h.overflow == 1 && buf.h.index_count == 0 &&
            buf.h.run_count == 0);
   }

   /* restart inside a triangle strip: odd primitive after the cut */
   {
      uint32_t idx[] = {10, 11, 12, 13, PANLIB_VP_RESTART, 12, 13, 14};
      call(idx, 8, rec, outs, 4, MESA_PRIM_TRIANGLE_STRIP, 1, 64, 8, out,
           &buf.h);
      /* First segment: even (10,11,12), odd swapped (12,11,13).
       * Restart resets parity, so the next segment starts even again.
       */
      CHECK(!buf.h.overflow && buf.h.index_count == 9 && buf.h.run_count == 1);
      CHECK(out[0] == 10 && out[1] == 11 && out[2] == 12);
      CHECK(out[3] == 12 && out[4] == 11 && out[5] == 13);
      CHECK(out[6] == 12 && out[7] == 13 && out[8] == 14);
      CHECK(buf.r[0].viewport == 1);
   }

   /* canaries past the written indices and past the one run stay put */
   {
      uint32_t idx[] = {10};
      memset(out, 0xa5, 64 * sizeof(uint32_t));
      memset(&buf, 0xa5, sizeof(buf));
      panlib_vp_runs(idx, 1, rec, STRIDE, outs, 4, MESA_PRIM_POINTS, 0, 64, 8,
                     out, &buf.h);
      CHECK(!buf.h.overflow && buf.h.index_count == 1 && buf.h.run_count == 1);
      CHECK(out[0] == 10 && out[1] == 0xa5a5a5a5u);
      CHECK(buf.r[1].viewport == 0xa5a5a5a5u);
   }

   /* limits: an output capacity past the documented bound discards the draw */
   {
      uint32_t idx[] = {10};
      call(idx, 1, rec, outs, 4, MESA_PRIM_POINTS, 0,
           PANLIB_VP_MAX_OUT_INDICES + 1, 1, out, &buf.h);
      CHECK(buf.h.overflow == 1 && buf.h.index_count == 0 &&
            buf.h.run_count == 0);
      call(idx, PANLIB_VP_MAX_INPUT_INDICES + 1, rec, outs, 4, MESA_PRIM_POINTS,
           0, 64, 1, out, &buf.h);
      CHECK(buf.h.overflow == 1 && buf.h.run_count == 0);
   }

   /* A count past the output capacity discards the draw. */
   {
      uint32_t idx[] = {10, 11, 12};
      call(idx, 3, rec, outs, 4, MESA_PRIM_POINTS, 0, 2, 8, out, &buf.h);
      CHECK(buf.h.overflow == 1 && buf.h.index_count == 0 &&
            buf.h.run_count == 0);
   }

   /* VS draw: two instances of two points, instance k reads records at
    * 2 * k. Ids 0..3 pick viewports 0, 1, 1, 0.
    */
   {
      uint32_t idx[] = {0, 1};
      const uint32_t vps[] = {0, 1, 1, 0};

      for (uint32_t i = 0; i < 4; i++)
         put_vp(rec, 20 + i, vps[i]);
      memset(out, 0xa5, 64 * sizeof(uint32_t));
      memset(&buf, 0xa5, sizeof(buf));
      for (uint32_t i = 0; i < 2; i++)
         idx[i] += 20;
      panlib_vp_runs_n(idx, 2, rec, STRIDE, outs, 4, MESA_PRIM_POINTS, 0, 64,
                       8, out, &buf.h, 2, 2, 0, 0);
      CHECK(!buf.h.overflow && buf.h.index_count == 4 && buf.h.run_count == 3);
      CHECK(out[0] == 20 && out[1] == 21 && out[2] == 22 && out[3] == 23);
      CHECK(buf.r[0].viewport == 0 && buf.r[0].index_count == 1);
      CHECK(buf.r[1].viewport == 1 && buf.r[1].first_index == 1 &&
            buf.r[1].index_count == 2);
      CHECK(buf.r[2].viewport == 0 && buf.r[2].first_index == 3);
      /* the instance offset must not wrap */
      panlib_vp_runs_n(idx, 2, rec, STRIDE, outs, 4, MESA_PRIM_POINTS, 0, 64,
                       8, out, &buf.h, 2, 0xffffffffu, 0, 0);
      CHECK(buf.h.overflow == 1 && buf.h.run_count == 0);

      /* pack_prim_id: the primitive ID rides in bits 16+, per instance. */
      panlib_vp_runs_n(idx, 2, rec, STRIDE, outs, 4, MESA_PRIM_POINTS, 0, 64,
                       8, out, &buf.h, 2, 2, 1, 0);
      CHECK(!buf.h.overflow && buf.h.index_count == 4 && buf.h.run_count == 3);
      CHECK(out[0] == 20 && out[1] == (21 | 1u << 16) && out[2] == 22 &&
            out[3] == (23 | 1u << 16));
   }

   /* pack_prim_id counts across a restart, and a record past 16 bits
    * discards the draw.
    */
   {
      uint32_t idx[] = {20, PANLIB_VP_RESTART, 21};
      panlib_vp_runs_n(idx, 3, rec, STRIDE, outs, 4, MESA_PRIM_POINTS, 0, 64,
                       8, out, &buf.h, 1, 0, 1, 0);
      CHECK(!buf.h.overflow && buf.h.index_count == 2);
      CHECK(out[0] == 20 && out[1] == (21 | 1u << 16));

      uint32_t big[] = {0x10000};
      panlib_vp_runs_n(big, 1, NULL, 0, 0, 4, MESA_PRIM_POINTS, 0, 64, 8, out,
                       &buf.h, 1, 0, 1, 0);
      CHECK(buf.h.overflow == 1);

      /* A CPU-split chunk starts at its primitive base, IDs wrap at 16 bits. */
      uint32_t two[] = {20, 21};
      panlib_vp_runs_n(two, 2, rec, STRIDE, outs, 4, MESA_PRIM_POINTS, 0, 64,
                       8, out, &buf.h, 1, 0, 1, 0xffff);
      CHECK(!buf.h.overflow && out[0] == (20 | 0xffffu << 16) && out[1] == 21);
   }

   free(rec);
   printf("vp_runs ok\n");
   return 0;
}
