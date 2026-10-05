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
        uint32_t base, uint32_t rec_base, VP_GLOBAL uint32_t *out_indices,
        VP_GLOBAL struct panlib_vp_run *runs, uint32_t *nidx, uint32_t *nrun,
        uint32_t index_cap, uint32_t run_cap, bool pack_prim_id, uint32_t pid)
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
      /* rec_base is the record index of instance k's vertex 0 for a VS draw
       * where every instance has its own records. 0 otherwise.
       */
      if (!vp_add_u32(in_indices[at], rec_base, &vtx[v]))
         return false;
   }
   uint32_t vp = vp_prim_viewport(records, record_stride, vp_slot, has_vp,
                                  vtx[prov], count);

   /* The FS primitive ID restarts with each run draw: carry it in the index
    * bits 16+ instead (PANLIB_VP_PRIM_ID_SHIFT), the passthrough VS outputs
    * it. ponytail: 16 bits, IDs wrap past 65535; widen with a side buffer
    * indexed by the record if an app cares.
    */
   if (pack_prim_id) {
      for (uint32_t v = 0; v < k; v++) {
         if (vtx[v] > PANLIB_VP_PACKED_RECORD_MASK)
            return false;
         vtx[v] |= pid << PANLIB_VP_PRIM_ID_SHIFT;
      }
   }
   return vp_emit(out_indices, runs, nidx, nrun, index_cap, run_cap, vp, k,
                  vtx);
}

/* instances > 1 repeats the index list once per instance, instance k reading
 * records at k * instance_stride. Only a VS draw has per-instance records, a
 * GS draw passes instances = 1.
 */
static void
panlib_vp_runs_n(VP_CONST uint32_t *in_indices, uint32_t index_count,
                 VP_CONST uint32_t *records, uint32_t record_stride,
                 uint64_t outputs_written, uint32_t viewport_count,
                 uint32_t prim, uint32_t provoking_last,
                 uint32_t index_capacity, uint32_t run_capacity,
                 VP_GLOBAL uint32_t *out_indices,
                 VP_GLOBAL struct panlib_vp_run_header *header,
                 uint32_t instances, uint32_t instance_stride,
                 uint32_t pack_prim_id, uint32_t prim_base)
{
   bool has_vp = (outputs_written & VARYING_BIT_VIEWPORT) != 0;
   uint32_t vp_slot = util_bitcount64(outputs_written &
                                      BITFIELD64_MASK(VARYING_SLOT_VIEWPORT));
   uint32_t count = viewport_count ? viewport_count : 1;
   uint32_t slot_bytes;
   bool overflow = false;
   bool accepted = prim == MESA_PRIM_POINTS || prim == MESA_PRIM_LINES ||
                   prim == MESA_PRIM_LINE_STRIP || prim == MESA_PRIM_TRIANGLES ||
                   prim == MESA_PRIM_TRIANGLE_STRIP ||
                   prim == MESA_PRIM_TRIANGLE_FAN;

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

   for (uint32_t inst = 0; inst < instances && !overflow; inst++) {
   uint64_t rec64 = (uint64_t)inst * instance_stride;
   uint32_t rec_base = (uint32_t)rec64;
   uint32_t needle = 0;
   /* Primitive ID: counts across restarts, restarts with each instance.
    * prim_base is the first primitive of a CPU-split chunk (one instance).
    */
   uint32_t pid = prim_base;

   if (rec64 > UINT32_MAX - 1)
      overflow = true;
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

      for (uint32_t p = 0; p < prims && !overflow; p++, pid++) {
         overflow = !vp_prim(in_indices, records, record_stride, vp_slot,
                             has_vp, count, prim, prov_last, p, prims,
                             needle, rec_base, out_indices, runs, &nidx,
                             &nrun, index_capacity, run_capacity,
                             pack_prim_id != 0, pid & 0xffffu);
      }
      if (!vp_add_u32(next, 1, &needle))
         overflow = true;
   }
   }

   /* Fail closed: a partial draw is not a draw. */
   header->index_count = overflow ? 0 : nidx;
   header->run_count = overflow ? 0 : nrun;
   header->overflow = overflow ? 1 : 0;
}

__attribute__((unused)) static void
panlib_vp_runs(VP_CONST uint32_t *in_indices, uint32_t index_count,
               VP_CONST uint32_t *records, uint32_t record_stride,
               uint64_t outputs_written, uint32_t viewport_count,
               uint32_t prim, uint32_t provoking_last, uint32_t index_capacity,
               uint32_t run_capacity, VP_GLOBAL uint32_t *out_indices,
               VP_GLOBAL struct panlib_vp_run_header *header)
{
   panlib_vp_runs_n(in_indices, index_count, records, record_stride,
                    outputs_written, viewport_count, prim, provoking_last,
                    index_capacity, run_capacity, out_indices, header, 1, 0,
                    0, 0);
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
                     uint32_t run_capacity, uint32_t per_instance,
                     uint32_t pack_prim_id, uint32_t prim_base)
{
   uint32_t n = *((global const uint32_t *)counts);
   /* A VS draw: counts is {vertex count, instance count}, and instance k
    * owns the n records after instance k - 1.
    */
   uint32_t instances = per_instance ? ((global const uint32_t *)counts)[1] : 1;

   /* A count past the filled region means the draw word is not the one
    * this allocation was sized for. Force the walker's overflow path.
    */
   if (n > index_bound ||
       (per_instance && (uint64_t)n * instances > index_bound))
      n = PANLIB_VP_MAX_INPUT_INDICES + 1;
   panlib_vp_runs_n((global const uint32_t *)in_indices, n,
                    (global const uint32_t *)records, record_stride,
                    outputs_written, viewport_count, prim, provoking_last,
                    index_capacity, run_capacity,
                    (global uint32_t *)out_indices,
                    (global struct panlib_vp_run_header *)header, instances,
                    per_instance ? n : 0, pack_prim_id, prim_base);
}
#endif
