/**************************************************************************
 *
 * Copyright 2007-2010 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/*
 * Rasterization for binned triangles within a tile
 */



/**
 * Prototype for a 8 plane rasterizer function.  Will codegenerate
 * several of these.
 *
 * XXX: Varients for more/fewer planes.
 * XXX: Need ways of dropping planes as we descend.
 * XXX: SIMD
 */
static void
TAG(do_block_4)(struct lp_rasterizer_task *task,
                const struct lp_rast_triangle *tri,
                const struct lp_rast_plane *plane,
                int x, int y,
                const int64_t *c)
{
   static_assert(LP_MAX_SAMPLES <= 8, "Code below assumes max of 8 samples");
#ifndef MULTISAMPLE
   uint64_t mask[2] = { 0xffff, 0 };
#else
   uint64_t mask[2] = { UINT64_MAX, UINT64_MAX };
#endif

   for (unsigned j = 0; j < NR_PLANES; j++) {
#ifndef MULTISAMPLE
      mask[0] &= ~build_mask_linear_32(
         (int32_t)((c[j] - 1) >> FIXED_ORDER),
         -plane[j].dcdx,
         plane[j].dcdy);
#else
      for (unsigned s = 0; s < task->scene->fb_max_samples; s++) {
         int64_t new_c = c[j] +
            (IMUL64(task->scene->fixed_sample_pos[s][1], plane[j].dcdy) +
            IMUL64(task->scene->fixed_sample_pos[s][0], -plane[j].dcdx));
         uint32_t build_mask = build_mask_linear_32(
            (int32_t)((new_c - 1) >> FIXED_ORDER),
            -plane[j].dcdx,
            plane[j].dcdy);
         mask[s / 4] &= ~((uint64_t)build_mask << ((s % 4) * 16));
      }
#endif
   }

   /* Now pass to the shader:
    */
   if (mask[0] | mask[1])
      lp_rast_shade_quads_mask_sample(task, &tri->inputs, x, y, mask);
}


/**
 * Evaluate a 16x16 block of pixels to determine which 4x4 subblocks are in/out
 * of the triangle's bounds.
 */
static void
TAG(do_block_16)(struct lp_rasterizer_task *task,
                 const struct lp_rast_triangle *tri,
                 const struct lp_rast_plane *plane,
                 int x, int y,
                 const int64_t *c)
{
   unsigned outmask = 0;      /* outside one or more trivial reject planes */
   unsigned partmask = 0;     /* outside one or more trivial accept planes */

   for (unsigned j = 0; j < NR_PLANES; j++) {
      const int32_t dcdx = -plane[j].dcdx << 2;
      const int32_t dcdy = plane[j].dcdy << 2;
      const int32_t cox = plane[j].eo << 2;
      const int32_t ei = dcdy + dcdx - cox;
      const int32_t co = (int32_t)((c[j] >> FIXED_ORDER) + cox);
      const int32_t cdiff = ei - cox +
         (int32_t)(((c[j] - 1) >> FIXED_ORDER) - (c[j] >> FIXED_ORDER));
      build_masks_32(co, cdiff,
                     dcdx, dcdy,
                     &outmask,   /* sign bits from c[i][0..15] + cox */
                     &partmask); /* sign bits from c[i][0..15] + cio */
   }

   if (outmask == 0xffff)
      return;

   /* Mask of sub-blocks which are inside all trivial accept planes:
    */
   unsigned inmask = ~partmask & 0xffff;

   /* Mask of sub-blocks which are inside all trivial reject planes,
    * but outside at least one trivial accept plane:
    */
   unsigned partial_mask = partmask & ~outmask;

   assert((partial_mask & inmask) == 0);

   LP_COUNT_ADD(nr_empty_4, util_bitcount(0xffff & ~(partial_mask | inmask)));

   /* Iterate over partials:
    */
   while (partial_mask) {
      int i = ffs(partial_mask) - 1;
      int ix = (i & 3) * 4;
      int iy = (i >> 2) * 4;
      int px = x + ix;
      int py = y + iy;
      int64_t cx[NR_PLANES];

      partial_mask &= ~(1 << i);

      LP_COUNT(nr_partially_covered_4);

      for (unsigned j = 0; j < NR_PLANES; j++) {
         cx[j] = (c[j]
                  - IMUL64_FIXED(plane[j].dcdx, ix)
                  + IMUL64_FIXED(plane[j].dcdy, iy));
      }

      TAG(do_block_4)(task, tri, plane, px, py, cx);
   }

   /* Iterate over fulls:
    */
   while (inmask) {
      int i = ffs(inmask) - 1;
      int ix = (i & 3) * 4;
      int iy = (i >> 2) * 4;
      int px = x + ix;
      int py = y + iy;

      inmask &= ~(1 << i);

      LP_COUNT(nr_fully_covered_4);
      block_full_4(task, tri, px, py);
   }
}


/**
 * Scan the tile in chunks and figure out which pixels to rasterize
 * for this triangle.
 */
void
TAG(lp_rast_triangle)(struct lp_rasterizer_task *task,
                      const union lp_rast_cmd_arg arg)
{
   const struct lp_rast_triangle *tri = arg.triangle.tri;
   unsigned plane_mask = arg.triangle.plane_mask;
   const struct lp_rast_plane *tri_plane = GET_PLANES(tri);
   const int x = task->x, y = task->y;
   struct lp_rast_plane plane[NR_PLANES];
   int64_t c[NR_PLANES];
   unsigned outmask, inmask, partmask, partial_mask;
   unsigned j = 0;

   if (tri->inputs.disable) {
      /* This triangle was partially binned and has been disabled */
      return;
   }

   outmask = 0;                 /* outside one or more trivial reject planes */
   partmask = 0;                /* outside one or more trivial accept planes */

   while (plane_mask) {
      int i = ffs(plane_mask) - 1;
      plane[j] = tri_plane[i];
      plane_mask &= ~(1 << i);
      c[j] = plane[j].c +
         IMUL64_FIXED(plane[j].dcdy, y) -
         IMUL64_FIXED(plane[j].dcdx, x);

      {
         const int32_t dcdx = -plane[j].dcdx << 4;
         const int32_t dcdy = plane[j].dcdy << 4;
         const int32_t cox = plane[j].eo << 4;
         const int32_t ei = dcdy + dcdx - cox;
         const int64_t co = (c[j] >> FIXED_ORDER) + cox;
         const int32_t cdiff = ei - cox +
            (int32_t)(((c[j] - 1) >> FIXED_ORDER) - (c[j] >> FIXED_ORDER));
#ifdef RASTER_64
         build_masks(co, cdiff,
                     dcdx, dcdy,
                     &outmask,   /* sign bits from c[i][0..15] + cox */
                     &partmask); /* sign bits from c[i][0..15] + cio */
#else
         build_masks_32((int32_t)co, cdiff,
                        dcdx, dcdy,
                        &outmask,   /* sign bits from c[i][0..15] + cox */
                        &partmask); /* sign bits from c[i][0..15] + cio */
#endif
      }

      j++;
   }

   if (outmask == 0xffff)
      return;

   /* Mask of sub-blocks which are inside all trivial accept planes:
    */
   inmask = ~partmask & 0xffff;

   /* Mask of sub-blocks which are inside all trivial reject planes,
    * but outside at least one trivial accept plane:
    */
   partial_mask = partmask & ~outmask;

   assert((partial_mask & inmask) == 0);

   LP_COUNT_ADD(nr_empty_16, util_bitcount(0xffff & ~(partial_mask | inmask)));

   /* Iterate over partials:
    */
   while (partial_mask) {
      int i = ffs(partial_mask) - 1;
      int ix = (i & 3) * 16;
      int iy = (i >> 2) * 16;
      int px = x + ix;
      int py = y + iy;
      int64_t cx[NR_PLANES];

      for (j = 0; j < NR_PLANES; j++)
         cx[j] = c[j] -
            IMUL64_FIXED(plane[j].dcdx, ix) +
            IMUL64_FIXED(plane[j].dcdy, iy);

      partial_mask &= ~(1 << i);

      LP_COUNT(nr_partially_covered_16);
      TAG(do_block_16)(task, tri, plane, px, py, cx);
   }

   /* Iterate over fulls:
    */
   while (inmask) {
      int i = ffs(inmask) - 1;
      int ix = (i & 3) * 16;
      int iy = (i >> 2) * 16;
      int px = x + ix;
      int py = y + iy;

      inmask &= ~(1 << i);

      LP_COUNT(nr_fully_covered_16);
      block_full_16(task, tri, px, py);
   }
}


#if DETECT_ARCH_SSE && defined(TRI_16)
/* XXX: special case this when intersection is not required.
 *      - tile completely within bbox,
 *      - bbox completely within tile.
 */
void
TRI_16(struct lp_rasterizer_task *task,
       const union lp_rast_cmd_arg arg)
{
   const struct lp_rast_triangle *tri = arg.triangle.tri;
   const struct lp_rast_plane *plane = GET_PLANES(tri);
   unsigned mask = arg.triangle.plane_mask;
   __m128i cstep4[NR_PLANES][4];
   int x = (mask & 0xff);
   int y = (mask >> 8);
   unsigned outmask = 0;    /* outside one or more trivial reject planes */

   if (x + 12 >= 64) {
      int i = ((x + 12) - 64) / 4;
      outmask |= right_mask_tab[i];
   }

   if (y + 12 >= 64) {
      int i = ((y + 12) - 64) / 4;
      outmask |= bottom_mask_tab[i];
   }

   x += task->x;
   y += task->y;

   for (unsigned j = 0; j < NR_PLANES; j++) {
      const int64_t c = plane[j].c +
         IMUL64_FIXED(plane[j].dcdy, y) -
         IMUL64_FIXED(plane[j].dcdx, x);

      const int dcdx = -plane[j].dcdx << 2;
      const int dcdy = plane[j].dcdy << 2;
      const int cox = plane[j].eo << 2;
      const int co = (int)(c >> FIXED_ORDER) + cox;

      __m128i xdcdy = _mm_set1_epi32(dcdy);
      cstep4[j][0] = _mm_setr_epi32(0, dcdx, dcdx*2, dcdx*3);
      cstep4[j][1] = _mm_add_epi32(cstep4[j][0], xdcdy);
      cstep4[j][2] = _mm_add_epi32(cstep4[j][1], xdcdy);
      cstep4[j][3] = _mm_add_epi32(cstep4[j][2], xdcdy);

      outmask |= sign_bits4(cstep4[j], co);
   }

   if (outmask == 0xffff)
      return;


   /* Mask of sub-blocks which are inside all trivial reject planes,
    * but outside at least one trivial accept plane:
    */
   unsigned partial_mask = 0xffff & ~outmask;

   /* Iterate over partials:
    */
   while (partial_mask) {
      int i = ffs(partial_mask) - 1;
      int ix = (i & 3) * 4;
      int iy = (i >> 2) * 4;
      int px = x + ix;
      int py = y + iy;
      unsigned mask = 0xffff;

      partial_mask &= ~(1 << i);

      for (unsigned j = 0; j < NR_PLANES; j++) {
         const int64_t cx = (plane[j].c - 1
                         - IMUL64_FIXED(plane[j].dcdx, px)
                         + IMUL64_FIXED(plane[j].dcdy, py)) << 2;

         mask &= ~sign_bits4(cstep4[j], (int)(cx >> FIXED_ORDER));
      }

      if (mask)
         lp_rast_shade_quads_mask(task, &tri->inputs, px, py, mask);
   }
}
#endif


#undef TAG
#undef TRI_16
#undef NR_PLANES
