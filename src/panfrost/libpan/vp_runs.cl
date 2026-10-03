#ifdef PANLIB_VP_HOST
#define VP_GLOBAL
#define VP_CONST const
#else
#define VP_GLOBAL global
#define VP_CONST global const
#endif

static bool
vp_add_u32(uint32_t a, uint32_t b, uint32_t *out)
{
   if (a > UINT32_MAX - b)
      return false;
   *out = a + b;
   return true;
}

/* Provoking vertex's packed slot, clamped into the active table.
 * Missing slot is viewport 0. count is at least 1.
 */
static uint32_t
vp_prim_viewport(VP_CONST uint32_t *records, uint32_t record_stride,
                 uint32_t vp_slot, bool has_vp, uint32_t provoking,
                 uint32_t count)
{
   uint32_t vp = 0;

   if (has_vp) {
      VP_CONST uint32_t *rec =
         (VP_CONST uint32_t *)((VP_CONST char *)records +
                               (uint64_t)provoking * record_stride);
      vp = rec[vp_slot * 4];
   }
   return vp < count ? vp : count - 1;
}

/* A new run needs a free slot. A merge into the open run does not.
 * Either way the primitive's vertices must fit. Failure discards later.
 */
static bool
vp_emit(VP_GLOBAL uint32_t *out_indices, VP_GLOBAL struct panlib_vp_run *runs,
        uint32_t *nidx, uint32_t *nrun, uint32_t index_cap, uint32_t run_cap,
        uint32_t vp, uint32_t k, const uint32_t *vtx)
{
   uint32_t next;
   bool merge = *nrun && runs[*nrun - 1].viewport == vp &&
                runs[*nrun - 1].first_index + runs[*nrun - 1].index_count == *nidx;

   if (!vp_add_u32(*nidx, k, &next) || next > index_cap)
      return false;
   if (!merge && (*nrun >= run_cap || !vp_add_u32(*nrun, 1, &next)))
      return false;

   if (merge)
      runs[*nrun - 1].index_count += k;
   else {
      runs[*nrun].viewport = vp;
      runs[*nrun].first_index = *nidx;
      runs[*nrun].index_count = k;
      (*nrun)++;
   }
   for (uint32_t v = 0; v < k; v++)
      out_indices[(*nidx)++] = vtx[v];
   return true;
}

static bool
vp_prim(VP_CONST uint32_t *in_indices, VP_CONST uint32_t *records,
        uint32_t record_stride, uint32_t vp_slot, bool has_vp, uint32_t count,
        uint32_t prim, bool provoking_last, uint32_t p, uint32_t num_prims,
        uint32_t base, VP_GLOBAL uint32_t *out_indices,
        VP_GLOBAL struct panlib_vp_run *runs, uint32_t *nidx, uint32_t *nrun,
        uint32_t index_cap, uint32_t run_cap)
{
   uint32_t k = mesa_vertices_per_prim((enum mesa_prim)prim);
   uint32_t vtx[3];
   uint32_t prov = provoking_last ? k - 1 : 0;

   if (k == 0 || k > 3)
      return false;

   for (uint32_t v = 0; v < k; v++) {
      uint32_t at;
      if (!vp_add_u32(base, poly_vertex_id_for_topology((enum mesa_prim)prim,
                                                     !provoking_last, p, v,
                                                     num_prims),
                      &at))
         return false;
      vtx[v] = in_indices[at];
   }
   return vp_emit(out_indices, runs, nidx, nrun, index_cap, run_cap,
                  vp_prim_viewport(records, record_stride, vp_slot, has_vp,
                                   vtx[prov], count),
                  k, vtx);
}

static void
panlib_vp_runs(VP_CONST uint32_t *in_indices, uint32_t index_count,
               VP_CONST uint32_t *records, uint32_t record_stride,
               uint64_t outputs_written, uint32_t viewport_count,
               uint32_t prim, uint32_t provoking_last, uint32_t index_capacity,
               uint32_t run_capacity, VP_GLOBAL uint32_t *out_indices,
               VP_GLOBAL struct panlib_vp_run_header *header)
{
   bool has_vp = (outputs_written & VARYING_BIT_VIEWPORT) != 0;
   uint32_t vp_slot = util_bitcount64(outputs_written &
                                      BITFIELD64_MASK(VARYING_SLOT_VIEWPORT));
   uint32_t count = viewport_count ? viewport_count : 1;
   uint32_t slot_bytes;
   bool overflow = false;
   bool accepted = prim == MESA_PRIM_POINTS || prim == MESA_PRIM_LINES ||
                   prim == MESA_PRIM_LINE_STRIP || prim == MESA_PRIM_TRIANGLES ||
                   prim == MESA_PRIM_TRIANGLE_STRIP;

   if (count > PANLIB_VP_MAX_VIEWPORTS)
      count = PANLIB_VP_MAX_VIEWPORTS;
   /* Empty input is a valid empty draw. Stride is checked only when a
    * viewport slot is actually read. record_stride 0 is fine if aligned
    * and the slot is absent.
    */
   if (!accepted || index_count > PANLIB_VP_MAX_INPUT_INDICES ||
       index_count > index_capacity ||
       index_capacity > PANLIB_VP_MAX_OUT_INDICES ||
       run_capacity > PANLIB_VP_MAX_RUNS || (record_stride & 3) ||
       (has_vp && (!vp_add_u32(vp_slot * 16, 4, &slot_bytes) ||
                   slot_bytes > record_stride)))
      overflow = true;

   VP_GLOBAL struct panlib_vp_run *runs =
      (VP_GLOBAL struct panlib_vp_run *)(header + 1);
   uint32_t nidx = 0, nrun = 0;
   bool prov_last = provoking_last != 0;
   uint32_t needle = 0;

   while (needle < index_count && !overflow) {
      uint32_t next = index_count;

      for (uint32_t i = needle; i < index_count; i++) {
         if (in_indices[i] == PANLIB_VP_RESTART) {
            next = i;
            break;
         }
      }

      uint32_t len = next - needle;
      uint32_t prims =
         u_decomposed_prims_for_vertices((enum mesa_prim)prim, (int)len);

      for (uint32_t p = 0; p < prims && !overflow; p++) {
         overflow = !vp_prim(in_indices, records, record_stride, vp_slot,
                             has_vp, count, prim, prov_last, p, prims,
                             needle, out_indices, runs, &nidx, &nrun,
                             index_capacity, run_capacity);
      }
      if (!vp_add_u32(next, 1, &needle))
         overflow = true;
   }

   /* Fail closed: a partial draw is not a draw. */
   header->index_count = overflow ? 0 : nidx;
   header->run_count = overflow ? 0 : nrun;
   header->overflow = overflow ? 1 : 0;
}

#ifndef PANLIB_VP_HOST
/* counts points at the GS draw word: the number of indices the GS kernel
 * restart-filled. Slots past it are untouched and must not be read.
 */
KERNEL(1)
panlib_vp_runs_launch(uint64_t in_indices, uint64_t records,
                     uint64_t out_indices, uint64_t header,
                     uint64_t outputs_written, uint64_t counts,
                     uint32_t record_stride, uint32_t viewport_count,
                     uint32_t prim, uint32_t provoking_last,
                     uint32_t index_bound, uint32_t index_capacity,
                     uint32_t run_capacity)
{
   uint32_t n = *((global const uint32_t *)counts);

   /* A count past the filled region means the draw word is not the one
    * this allocation was sized for. Force the walker's overflow path.
    */
   if (n > index_bound)
      n = PANLIB_VP_MAX_INPUT_INDICES + 1;
   panlib_vp_runs((global const uint32_t *)in_indices, n,
                  (global const uint32_t *)records, record_stride,
                  outputs_written, viewport_count, prim, provoking_last,
                  index_capacity, run_capacity,
                  (global uint32_t *)out_indices,
                  (global struct panlib_vp_run_header *)header);
}
#endif
