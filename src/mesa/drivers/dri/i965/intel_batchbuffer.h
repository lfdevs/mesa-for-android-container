#ifndef INTEL_BATCHBUFFER_H
#define INTEL_BATCHBUFFER_H

#include "main/mtypes.h"

#include "brw_context.h"
#include "intel_bufmgr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Number of bytes to reserve for commands necessary to complete a batch.
 *
 * This includes:
 * - MI_BATCHBUFFER_END (4 bytes)
 * - Optional MI_NOOP for ensuring the batch length is qword aligned (4 bytes)
 * - Any state emitted by vtbl->finish_batch():
 *   - Gen4-5 record ending occlusion query values (4 * 4 = 16 bytes)
 *   - Disabling OA counters on Gen6+ (3 DWords = 12 bytes)
 *   - Ending MI_REPORT_PERF_COUNT on Gen5+, plus associated PIPE_CONTROLs:
 *     - Two sets of PIPE_CONTROLs, which become 4 PIPE_CONTROLs each on SNB,
 *       which are 5 DWords each ==> 2 * 4 * 5 * 4 = 160 bytes
 *     - 3 DWords for MI_REPORT_PERF_COUNT itself on Gen6+.  ==> 12 bytes.
 *       On Ironlake, it's 6 DWords, but we have some slack due to the lack of
 *       Sandybridge PIPE_CONTROL madness.
 *   - CC_STATE workaround on HSW (17 * 4 = 68 bytes)
 *     - 10 dwords for initial mi_flush
 *     - 2 dwords for CC state setup
 *     - 5 dwords for the required pipe control at the end
 *   - Restoring L3 configuration: (24 dwords = 96 bytes)
 *     - 2*6 dwords for two PIPE_CONTROL flushes.
 *     - 7 dwords for L3 configuration set-up.
 *     - 5 dwords for L3 atomic set-up (on HSW).
 */
#define BATCH_RESERVED 308

struct intel_batchbuffer;

void intel_batchbuffer_emit_render_ring_prelude(struct brw_context *brw);
void intel_batchbuffer_init(struct intel_batchbuffer *batch, dri_bufmgr *bufmgr,
                            bool has_llc);
void intel_batchbuffer_free(struct intel_batchbuffer *batch);
void intel_batchbuffer_save_state(struct brw_context *brw);
void intel_batchbuffer_reset_to_saved(struct brw_context *brw);
void intel_batchbuffer_require_space(struct brw_context *brw, GLuint sz,
                                     enum brw_gpu_ring ring);

int _intel_batchbuffer_flush(struct brw_context *brw,
			     const char *file, int line);

#define intel_batchbuffer_flush(intel) \
	_intel_batchbuffer_flush(intel, __FILE__, __LINE__)



/* Unlike bmBufferData, this currently requires the buffer be mapped.
 * Consider it a convenience function wrapping multple
 * intel_buffer_dword() calls.
 */
void intel_batchbuffer_data(struct brw_context *brw,
                            const void *data, GLuint bytes,
                            enum brw_gpu_ring ring);

uint32_t intel_batchbuffer_reloc(struct intel_batchbuffer *batch,
                                 drm_intel_bo *buffer,
                                 uint32_t offset,
                                 uint32_t read_domains,
                                 uint32_t write_domain,
                                 uint32_t delta);
uint64_t intel_batchbuffer_reloc64(struct intel_batchbuffer *batch,
                                   drm_intel_bo *buffer,
                                   uint32_t offset,
                                   uint32_t read_domains,
                                   uint32_t write_domain,
                                   uint32_t delta);

#define USED_BATCH(batch) ((uintptr_t)((batch).map_next - (batch).map))

static inline uint32_t float_as_int(float f)
{
   union {
      float f;
      uint32_t d;
   } fi;

   fi.f = f;
   return fi.d;
}

/* Inline functions - might actually be better off with these
 * non-inlined.  Certainly better off switching all command packets to
 * be passed as structs rather than dwords, but that's a little bit of
 * work...
 */
static inline unsigned
intel_batchbuffer_space(struct intel_batchbuffer *batch)
{
   return (batch->state_batch_offset - batch->reserved_space)
      - USED_BATCH(*batch) * 4;
}


static inline void
intel_batchbuffer_emit_dword(struct intel_batchbuffer *batch, GLuint dword)
{
#ifdef DEBUG
   assert(intel_batchbuffer_space(batch) >= 4);
#endif
   *batch->map_next++ = dword;
   assert(batch->ring != UNKNOWN_RING);
}

static inline void
intel_batchbuffer_emit_float(struct intel_batchbuffer *batch, float f)
{
   intel_batchbuffer_emit_dword(batch, float_as_int(f));
}

static inline void
intel_batchbuffer_begin(struct brw_context *brw, int n, enum brw_gpu_ring ring)
{
   intel_batchbuffer_require_space(brw, n * 4, ring);

#ifdef DEBUG
   brw->batch.emit = USED_BATCH(brw->batch);
   brw->batch.total = n;
#endif
}

static inline void
intel_batchbuffer_advance(struct brw_context *brw)
{
#ifdef DEBUG
   struct intel_batchbuffer *batch = &brw->batch;
   unsigned int _n = USED_BATCH(*batch) - batch->emit;
   assert(batch->total != 0);
   if (_n != batch->total) {
      fprintf(stderr, "ADVANCE_BATCH: %d of %d dwords emitted\n",
	      _n, batch->total);
      abort();
   }
   batch->total = 0;
#else
   (void) brw;
#endif
}

#define BEGIN_BATCH(n) do {                            \
   intel_batchbuffer_begin(brw, (n), RENDER_RING);     \
   uint32_t *__map = brw->batch.map_next;              \
   brw->batch.map_next += (n)

#define BEGIN_BATCH_BLT(n) do {                        \
   intel_batchbuffer_begin(brw, (n), BLT_RING);        \
   uint32_t *__map = brw->batch.map_next;              \
   brw->batch.map_next += (n)

#define OUT_BATCH(d) *__map++ = (d)
#define OUT_BATCH_F(f) OUT_BATCH(float_as_int((f)))

#define OUT_RELOC(buf, read_domains, write_domain, delta) do {    \
   uint32_t __offset = (__map - brw->batch.map) * 4;              \
   OUT_BATCH(intel_batchbuffer_reloc(&brw->batch, (buf), __offset, \
                                     (read_domains),              \
                                     (write_domain),              \
                                     (delta)));                   \
} while (0)

/* Handle 48-bit address relocations for Gen8+ */
#define OUT_RELOC64(buf, read_domains, write_domain, delta) do {      \
   uint32_t __offset = (__map - brw->batch.map) * 4;                  \
   uint64_t reloc64 = intel_batchbuffer_reloc64(&brw->batch, (buf), __offset, \
                                                (read_domains),       \
                                                (write_domain),       \
                                                (delta));             \
   OUT_BATCH(reloc64);                                                \
   OUT_BATCH(reloc64 >> 32);                                          \
} while (0)

#define ADVANCE_BATCH()                  \
   assert(__map == brw->batch.map_next); \
   intel_batchbuffer_advance(brw);       \
} while (0)

#ifdef __cplusplus
}
#endif

#endif
