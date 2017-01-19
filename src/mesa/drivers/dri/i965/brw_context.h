/*
 Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 Intel funded Tungsten Graphics to
 develop this 3D driver.

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:

 The above copyright notice and this permission notice (including the
 next paragraph) shall be included in all copies or substantial
 portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 **********************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  */


#ifndef BRWCONTEXT_INC
#define BRWCONTEXT_INC

#include <stdbool.h>
#include "main/macros.h"
#include "main/mtypes.h"
#include "brw_structs.h"
#include "brw_compiler.h"
#include "intel_aub.h"

#include "isl/isl.h"
#include "blorp/blorp.h"

#ifdef __cplusplus
extern "C" {
	/* Evil hack for using libdrm in a c++ compiler. */
        #define virtual virt
#endif

#include <intel_bufmgr.h>
#ifdef __cplusplus
	#undef virtual
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include "intel_debug.h"
#include "intel_screen.h"
#include "intel_tex_obj.h"
#include "intel_resolve_map.h"

/* Glossary:
 *
 * URB - uniform resource buffer.  A mid-sized buffer which is
 * partitioned between the fixed function units and used for passing
 * values (vertices, primitives, constants) between them.
 *
 * CURBE - constant URB entry.  An urb region (entry) used to hold
 * constant values which the fixed function units can be instructed to
 * preload into the GRF when spawning a thread.
 *
 * VUE - vertex URB entry.  An urb entry holding a vertex and usually
 * a vertex header.  The header contains control information and
 * things like primitive type, Begin/end flags and clip codes.
 *
 * PUE - primitive URB entry.  An urb entry produced by the setup (SF)
 * unit holding rasterization and interpolation parameters.
 *
 * GRF - general register file.  One of several register files
 * addressable by programmed threads.  The inputs (r0, payload, curbe,
 * urb) of the thread are preloaded to this area before the thread is
 * spawned.  The registers are individually 8 dwords wide and suitable
 * for general usage.  Registers holding thread input values are not
 * special and may be overwritten.
 *
 * MRF - message register file.  Threads communicate (and terminate)
 * by sending messages.  Message parameters are placed in contiguous
 * MRF registers.  All program output is via these messages.  URB
 * entries are populated by sending a message to the shared URB
 * function containing the new data, together with a control word,
 * often an unmodified copy of R0.
 *
 * R0 - GRF register 0.  Typically holds control information used when
 * sending messages to other threads.
 *
 * EU or GEN4 EU: The name of the programmable subsystem of the
 * i965 hardware.  Threads are executed by the EU, the registers
 * described above are part of the EU architecture.
 *
 * Fixed function units:
 *
 * CS - Command streamer.  Notional first unit, little software
 * interaction.  Holds the URB entries used for constant data, ie the
 * CURBEs.
 *
 * VF/VS - Vertex Fetch / Vertex Shader.  The fixed function part of
 * this unit is responsible for pulling vertices out of vertex buffers
 * in vram and injecting them into the processing pipe as VUEs.  If
 * enabled, it first passes them to a VS thread which is a good place
 * for the driver to implement any active vertex shader.
 *
 * HS - Hull Shader (Tessellation Control Shader)
 *
 * TE - Tessellation Engine (Tessellation Primitive Generation)
 *
 * DS - Domain Shader (Tessellation Evaluation Shader)
 *
 * GS - Geometry Shader.  This corresponds to a new DX10 concept.  If
 * enabled, incoming strips etc are passed to GS threads in individual
 * line/triangle/point units.  The GS thread may perform arbitary
 * computation and emit whatever primtives with whatever vertices it
 * chooses.  This makes GS an excellent place to implement GL's
 * unfilled polygon modes, though of course it is capable of much
 * more.  Additionally, GS is used to translate away primitives not
 * handled by latter units, including Quads and Lineloops.
 *
 * CS - Clipper.  Mesa's clipping algorithms are imported to run on
 * this unit.  The fixed function part performs cliptesting against
 * the 6 fixed clipplanes and makes descisions on whether or not the
 * incoming primitive needs to be passed to a thread for clipping.
 * User clip planes are handled via cooperation with the VS thread.
 *
 * SF - Strips Fans or Setup: Triangles are prepared for
 * rasterization.  Interpolation coefficients are calculated.
 * Flatshading and two-side lighting usually performed here.
 *
 * WM - Windower.  Interpolation of vertex attributes performed here.
 * Fragment shader implemented here.  SIMD aspects of EU taken full
 * advantage of, as pixels are processed in blocks of 16.
 *
 * CC - Color Calculator.  No EU threads associated with this unit.
 * Handles blending and (presumably) depth and stencil testing.
 */

struct brw_context;
struct brw_inst;
struct brw_vs_prog_key;
struct brw_vue_prog_key;
struct brw_wm_prog_key;
struct brw_wm_prog_data;
struct brw_cs_prog_key;
struct brw_cs_prog_data;

enum brw_pipeline {
   BRW_RENDER_PIPELINE,
   BRW_COMPUTE_PIPELINE,

   BRW_NUM_PIPELINES
};

enum brw_cache_id {
   BRW_CACHE_FS_PROG,
   BRW_CACHE_BLORP_PROG,
   BRW_CACHE_SF_PROG,
   BRW_CACHE_VS_PROG,
   BRW_CACHE_FF_GS_PROG,
   BRW_CACHE_GS_PROG,
   BRW_CACHE_TCS_PROG,
   BRW_CACHE_TES_PROG,
   BRW_CACHE_CLIP_PROG,
   BRW_CACHE_CS_PROG,

   BRW_MAX_CACHE
};

enum brw_state_id {
   /* brw_cache_ids must come first - see brw_program_cache.c */
   BRW_STATE_URB_FENCE = BRW_MAX_CACHE,
   BRW_STATE_FRAGMENT_PROGRAM,
   BRW_STATE_GEOMETRY_PROGRAM,
   BRW_STATE_TESS_PROGRAMS,
   BRW_STATE_VERTEX_PROGRAM,
   BRW_STATE_CURBE_OFFSETS,
   BRW_STATE_REDUCED_PRIMITIVE,
   BRW_STATE_PATCH_PRIMITIVE,
   BRW_STATE_PRIMITIVE,
   BRW_STATE_CONTEXT,
   BRW_STATE_PSP,
   BRW_STATE_SURFACES,
   BRW_STATE_BINDING_TABLE_POINTERS,
   BRW_STATE_INDICES,
   BRW_STATE_VERTICES,
   BRW_STATE_DEFAULT_TESS_LEVELS,
   BRW_STATE_BATCH,
   BRW_STATE_INDEX_BUFFER,
   BRW_STATE_VS_CONSTBUF,
   BRW_STATE_TCS_CONSTBUF,
   BRW_STATE_TES_CONSTBUF,
   BRW_STATE_GS_CONSTBUF,
   BRW_STATE_PROGRAM_CACHE,
   BRW_STATE_STATE_BASE_ADDRESS,
   BRW_STATE_VUE_MAP_GEOM_OUT,
   BRW_STATE_TRANSFORM_FEEDBACK,
   BRW_STATE_RASTERIZER_DISCARD,
   BRW_STATE_STATS_WM,
   BRW_STATE_UNIFORM_BUFFER,
   BRW_STATE_ATOMIC_BUFFER,
   BRW_STATE_IMAGE_UNITS,
   BRW_STATE_META_IN_PROGRESS,
   BRW_STATE_PUSH_CONSTANT_ALLOCATION,
   BRW_STATE_NUM_SAMPLES,
   BRW_STATE_TEXTURE_BUFFER,
   BRW_STATE_GEN4_UNIT_STATE,
   BRW_STATE_CC_VP,
   BRW_STATE_SF_VP,
   BRW_STATE_CLIP_VP,
   BRW_STATE_SAMPLER_STATE_TABLE,
   BRW_STATE_VS_ATTRIB_WORKAROUNDS,
   BRW_STATE_COMPUTE_PROGRAM,
   BRW_STATE_CS_WORK_GROUPS,
   BRW_STATE_URB_SIZE,
   BRW_STATE_CC_STATE,
   BRW_STATE_BLORP,
   BRW_STATE_VIEWPORT_COUNT,
   BRW_STATE_CONSERVATIVE_RASTERIZATION,
   BRW_NUM_STATE_BITS
};

/**
 * BRW_NEW_*_PROG_DATA and BRW_NEW_*_PROGRAM are similar, but distinct.
 *
 * BRW_NEW_*_PROGRAM relates to the gl_shader_program/gl_program structures.
 * When the currently bound shader program differs from the previous draw
 * call, these will be flagged.  They cover brw->{stage}_program and
 * ctx->{Stage}Program->_Current.
 *
 * BRW_NEW_*_PROG_DATA is flagged when the effective shaders change, from a
 * driver perspective.  Even if the same shader is bound at the API level,
 * we may need to switch between multiple versions of that shader to handle
 * changes in non-orthagonal state.
 *
 * Additionally, multiple shader programs may have identical vertex shaders
 * (for example), or compile down to the same code in the backend.  We combine
 * those into a single program cache entry.
 *
 * BRW_NEW_*_PROG_DATA occurs when switching program cache entries, which
 * covers the brw_*_prog_data structures, and brw->*.prog_offset.
 */
#define BRW_NEW_FS_PROG_DATA            (1ull << BRW_CACHE_FS_PROG)
/* XXX: The BRW_NEW_BLORP_BLIT_PROG_DATA dirty bit is unused (as BLORP doesn't
 * use the normal state upload paths), but the cache is still used.  To avoid
 * polluting the brw_program_cache code with special cases, we retain the
 * dirty bit for now.  It should eventually be removed.
 */
#define BRW_NEW_BLORP_BLIT_PROG_DATA    (1ull << BRW_CACHE_BLORP_PROG)
#define BRW_NEW_SF_PROG_DATA            (1ull << BRW_CACHE_SF_PROG)
#define BRW_NEW_VS_PROG_DATA            (1ull << BRW_CACHE_VS_PROG)
#define BRW_NEW_FF_GS_PROG_DATA         (1ull << BRW_CACHE_FF_GS_PROG)
#define BRW_NEW_GS_PROG_DATA            (1ull << BRW_CACHE_GS_PROG)
#define BRW_NEW_TCS_PROG_DATA           (1ull << BRW_CACHE_TCS_PROG)
#define BRW_NEW_TES_PROG_DATA           (1ull << BRW_CACHE_TES_PROG)
#define BRW_NEW_CLIP_PROG_DATA          (1ull << BRW_CACHE_CLIP_PROG)
#define BRW_NEW_CS_PROG_DATA            (1ull << BRW_CACHE_CS_PROG)
#define BRW_NEW_URB_FENCE               (1ull << BRW_STATE_URB_FENCE)
#define BRW_NEW_FRAGMENT_PROGRAM        (1ull << BRW_STATE_FRAGMENT_PROGRAM)
#define BRW_NEW_GEOMETRY_PROGRAM        (1ull << BRW_STATE_GEOMETRY_PROGRAM)
#define BRW_NEW_TESS_PROGRAMS           (1ull << BRW_STATE_TESS_PROGRAMS)
#define BRW_NEW_VERTEX_PROGRAM          (1ull << BRW_STATE_VERTEX_PROGRAM)
#define BRW_NEW_CURBE_OFFSETS           (1ull << BRW_STATE_CURBE_OFFSETS)
#define BRW_NEW_REDUCED_PRIMITIVE       (1ull << BRW_STATE_REDUCED_PRIMITIVE)
#define BRW_NEW_PATCH_PRIMITIVE         (1ull << BRW_STATE_PATCH_PRIMITIVE)
#define BRW_NEW_PRIMITIVE               (1ull << BRW_STATE_PRIMITIVE)
#define BRW_NEW_CONTEXT                 (1ull << BRW_STATE_CONTEXT)
#define BRW_NEW_PSP                     (1ull << BRW_STATE_PSP)
#define BRW_NEW_SURFACES                (1ull << BRW_STATE_SURFACES)
#define BRW_NEW_BINDING_TABLE_POINTERS  (1ull << BRW_STATE_BINDING_TABLE_POINTERS)
#define BRW_NEW_INDICES                 (1ull << BRW_STATE_INDICES)
#define BRW_NEW_VERTICES                (1ull << BRW_STATE_VERTICES)
#define BRW_NEW_DEFAULT_TESS_LEVELS     (1ull << BRW_STATE_DEFAULT_TESS_LEVELS)
/**
 * Used for any batch entry with a relocated pointer that will be used
 * by any 3D rendering.
 */
#define BRW_NEW_BATCH                   (1ull << BRW_STATE_BATCH)
/** \see brw.state.depth_region */
#define BRW_NEW_INDEX_BUFFER            (1ull << BRW_STATE_INDEX_BUFFER)
#define BRW_NEW_VS_CONSTBUF             (1ull << BRW_STATE_VS_CONSTBUF)
#define BRW_NEW_TCS_CONSTBUF            (1ull << BRW_STATE_TCS_CONSTBUF)
#define BRW_NEW_TES_CONSTBUF            (1ull << BRW_STATE_TES_CONSTBUF)
#define BRW_NEW_GS_CONSTBUF             (1ull << BRW_STATE_GS_CONSTBUF)
#define BRW_NEW_PROGRAM_CACHE           (1ull << BRW_STATE_PROGRAM_CACHE)
#define BRW_NEW_STATE_BASE_ADDRESS      (1ull << BRW_STATE_STATE_BASE_ADDRESS)
#define BRW_NEW_VUE_MAP_GEOM_OUT        (1ull << BRW_STATE_VUE_MAP_GEOM_OUT)
#define BRW_NEW_VIEWPORT_COUNT          (1ull << BRW_STATE_VIEWPORT_COUNT)
#define BRW_NEW_TRANSFORM_FEEDBACK      (1ull << BRW_STATE_TRANSFORM_FEEDBACK)
#define BRW_NEW_RASTERIZER_DISCARD      (1ull << BRW_STATE_RASTERIZER_DISCARD)
#define BRW_NEW_STATS_WM                (1ull << BRW_STATE_STATS_WM)
#define BRW_NEW_UNIFORM_BUFFER          (1ull << BRW_STATE_UNIFORM_BUFFER)
#define BRW_NEW_ATOMIC_BUFFER           (1ull << BRW_STATE_ATOMIC_BUFFER)
#define BRW_NEW_IMAGE_UNITS             (1ull << BRW_STATE_IMAGE_UNITS)
#define BRW_NEW_META_IN_PROGRESS        (1ull << BRW_STATE_META_IN_PROGRESS)
#define BRW_NEW_PUSH_CONSTANT_ALLOCATION (1ull << BRW_STATE_PUSH_CONSTANT_ALLOCATION)
#define BRW_NEW_NUM_SAMPLES             (1ull << BRW_STATE_NUM_SAMPLES)
#define BRW_NEW_TEXTURE_BUFFER          (1ull << BRW_STATE_TEXTURE_BUFFER)
#define BRW_NEW_GEN4_UNIT_STATE         (1ull << BRW_STATE_GEN4_UNIT_STATE)
#define BRW_NEW_CC_VP                   (1ull << BRW_STATE_CC_VP)
#define BRW_NEW_SF_VP                   (1ull << BRW_STATE_SF_VP)
#define BRW_NEW_CLIP_VP                 (1ull << BRW_STATE_CLIP_VP)
#define BRW_NEW_SAMPLER_STATE_TABLE     (1ull << BRW_STATE_SAMPLER_STATE_TABLE)
#define BRW_NEW_VS_ATTRIB_WORKAROUNDS   (1ull << BRW_STATE_VS_ATTRIB_WORKAROUNDS)
#define BRW_NEW_COMPUTE_PROGRAM         (1ull << BRW_STATE_COMPUTE_PROGRAM)
#define BRW_NEW_CS_WORK_GROUPS          (1ull << BRW_STATE_CS_WORK_GROUPS)
#define BRW_NEW_URB_SIZE                (1ull << BRW_STATE_URB_SIZE)
#define BRW_NEW_CC_STATE                (1ull << BRW_STATE_CC_STATE)
#define BRW_NEW_BLORP                   (1ull << BRW_STATE_BLORP)
#define BRW_NEW_CONSERVATIVE_RASTERIZATION (1ull << BRW_STATE_CONSERVATIVE_RASTERIZATION)

struct brw_state_flags {
   /** State update flags signalled by mesa internals */
   GLuint mesa;
   /**
    * State update flags signalled as the result of brw_tracked_state updates
    */
   uint64_t brw;
};


/** Subclass of Mesa program */
struct brw_program {
   struct gl_program program;
   GLuint id;

   bool compiled_once;
};


/**
 * Bitmask indicating which fragment shader inputs represent varyings (and
 * hence have to be delivered to the fragment shader by the SF/SBE stage).
 */
#define BRW_FS_VARYING_INPUT_MASK \
   (BITFIELD64_RANGE(0, VARYING_SLOT_MAX) & \
    ~VARYING_BIT_POS & ~VARYING_BIT_FACE)


struct brw_sf_prog_data {
   GLuint urb_read_length;
   GLuint total_grf;

   /* Each vertex may have upto 12 attributes, 4 components each,
    * except WPOS which requires only 2.  (11*4 + 2) == 44 ==> 11
    * rows.
    *
    * Actually we use 4 for each, so call it 12 rows.
    */
   GLuint urb_entry_size;
};


/**
 * We always program SF to start reading at an offset of 1 (2 varying slots)
 * from the start of the vertex URB entry.  This causes it to skip:
 * - VARYING_SLOT_PSIZ and BRW_VARYING_SLOT_NDC on gen4-5
 * - VARYING_SLOT_PSIZ and VARYING_SLOT_POS on gen6+
 */
#define BRW_SF_URB_ENTRY_READ_OFFSET 1


struct brw_clip_prog_data {
   GLuint curb_read_length;	/* user planes? */
   GLuint clip_mode;
   GLuint urb_read_length;
   GLuint total_grf;
};

struct brw_ff_gs_prog_data {
   GLuint urb_read_length;
   GLuint total_grf;

   /**
    * Gen6 transform feedback: Amount by which the streaming vertex buffer
    * indices should be incremented each time the GS is invoked.
    */
   unsigned svbi_postincrement_value;
};

/** Number of texture sampler units */
#define BRW_MAX_TEX_UNIT 32

/** Max number of render targets in a shader */
#define BRW_MAX_DRAW_BUFFERS 8

/** Max number of UBOs in a shader */
#define BRW_MAX_UBO 14

/** Max number of SSBOs in a shader */
#define BRW_MAX_SSBO 12

/** Max number of atomic counter buffer objects in a shader */
#define BRW_MAX_ABO 16

/** Max number of image uniforms in a shader */
#define BRW_MAX_IMAGES 32

/**
 * Max number of binding table entries used for stream output.
 *
 * From the OpenGL 3.0 spec, table 6.44 (Transform Feedback State), the
 * minimum value of MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS is 64.
 *
 * On Gen6, the size of transform feedback data is limited not by the number
 * of components but by the number of binding table entries we set aside.  We
 * use one binding table entry for a float, one entry for a vector, and one
 * entry per matrix column.  Since the only way we can communicate our
 * transform feedback capabilities to the client is via
 * MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS, we need to plan for the
 * worst case, in which all the varyings are floats, so we use up one binding
 * table entry per component.  Therefore we need to set aside at least 64
 * binding table entries for use by transform feedback.
 *
 * Note: since we don't currently pack varyings, it is currently impossible
 * for the client to actually use up all of these binding table entries--if
 * all of their varyings were floats, they would run out of varying slots and
 * fail to link.  But that's a bug, so it seems prudent to go ahead and
 * allocate the number of binding table entries we will need once the bug is
 * fixed.
 */
#define BRW_MAX_SOL_BINDINGS 64

/** Maximum number of actual buffers used for stream output */
#define BRW_MAX_SOL_BUFFERS 4

#define BRW_MAX_SURFACES   (BRW_MAX_DRAW_BUFFERS +                      \
                            BRW_MAX_TEX_UNIT * 2 + /* normal, gather */ \
                            BRW_MAX_UBO +                               \
                            BRW_MAX_SSBO +                              \
                            BRW_MAX_ABO +                               \
                            BRW_MAX_IMAGES +                            \
                            2 + /* shader time, pull constants */       \
                            1 /* cs num work groups */)

#define SURF_INDEX_GEN6_SOL_BINDING(t) (t)

/**
 * Stride in bytes between shader_time entries.
 *
 * We separate entries by a cacheline to reduce traffic between EUs writing to
 * different entries.
 */
#define SHADER_TIME_STRIDE 64

struct brw_cache {
   struct brw_context *brw;

   struct brw_cache_item **items;
   drm_intel_bo *bo;
   GLuint size, n_items;

   uint32_t next_offset;
   bool bo_used_by_gpu;
};


/* Considered adding a member to this struct to document which flags
 * an update might raise so that ordering of the state atoms can be
 * checked or derived at runtime.  Dropped the idea in favor of having
 * a debug mode where the state is monitored for flags which are
 * raised that have already been tested against.
 */
struct brw_tracked_state {
   struct brw_state_flags dirty;
   void (*emit)( struct brw_context *brw );
};

enum shader_time_shader_type {
   ST_NONE,
   ST_VS,
   ST_TCS,
   ST_TES,
   ST_GS,
   ST_FS8,
   ST_FS16,
   ST_CS,
};

struct brw_vertex_buffer {
   /** Buffer object containing the uploaded vertex data */
   drm_intel_bo *bo;
   uint32_t offset;
   uint32_t size;
   /** Byte stride between elements in the uploaded array */
   GLuint stride;
   GLuint step_rate;
};
struct brw_vertex_element {
   const struct gl_vertex_array *glarray;

   int buffer;
   bool is_dual_slot;
   /** Offset of the first element within the buffer object */
   unsigned int offset;
};

struct brw_query_object {
   struct gl_query_object Base;

   /** Last query BO associated with this query. */
   drm_intel_bo *bo;

   /** Last index in bo with query data for this object. */
   int last_index;

   /** True if we know the batch has been flushed since we ended the query. */
   bool flushed;
};

enum brw_gpu_ring {
   UNKNOWN_RING,
   RENDER_RING,
   BLT_RING,
};

struct intel_batchbuffer {
   /** Current batchbuffer being queued up. */
   drm_intel_bo *bo;
   /** Last BO submitted to the hardware.  Used for glFinish(). */
   drm_intel_bo *last_bo;

#ifdef DEBUG
   uint16_t emit, total;
#endif
   uint16_t reserved_space;
   uint32_t *map_next;
   uint32_t *map;
   uint32_t *cpu_map;
#define BATCH_SZ (8192*sizeof(uint32_t))

   uint32_t state_batch_offset;
   enum brw_gpu_ring ring;
   bool needs_sol_reset;
   bool state_base_address_emitted;

   struct {
      uint32_t *map_next;
      int reloc_count;
   } saved;
};

#define MAX_GS_INPUT_VERTICES 6

#define BRW_MAX_XFB_STREAMS 4

struct brw_transform_feedback_object {
   struct gl_transform_feedback_object base;

   /** A buffer to hold SO_WRITE_OFFSET(n) values while paused. */
   drm_intel_bo *offset_bo;

   /** If true, SO_WRITE_OFFSET(n) should be reset to zero at next use. */
   bool zero_offsets;

   /** The most recent primitive mode (GL_TRIANGLES/GL_POINTS/GL_LINES). */
   GLenum primitive_mode;

   /**
    * Count of primitives generated during this transform feedback operation.
    *  @{
    */
   uint64_t prims_generated[BRW_MAX_XFB_STREAMS];
   drm_intel_bo *prim_count_bo;
   unsigned prim_count_buffer_index; /**< in number of uint64_t units */
   /** @} */

   /**
    * Number of vertices written between last Begin/EndTransformFeedback().
    *
    * Used to implement DrawTransformFeedback().
    */
   uint64_t vertices_written[BRW_MAX_XFB_STREAMS];
   bool vertices_written_valid;
};

/**
 * Data shared between each programmable stage in the pipeline (vs, gs, and
 * wm).
 */
struct brw_stage_state
{
   gl_shader_stage stage;
   struct brw_stage_prog_data *prog_data;

   /**
    * Optional scratch buffer used to store spilled register values and
    * variably-indexed GRF arrays.
    *
    * The contents of this buffer are short-lived so the same memory can be
    * re-used at will for multiple shader programs (executed by the same fixed
    * function).  However reusing a scratch BO for which shader invocations
    * are still in flight with a per-thread scratch slot size other than the
    * original can cause threads with different scratch slot size and FFTID
    * (which may be executed in parallel depending on the shader stage and
    * hardware generation) to map to an overlapping region of the scratch
    * space, which can potentially lead to mutual scratch space corruption.
    * For that reason if you borrow this scratch buffer you should only be
    * using the slot size given by the \c per_thread_scratch member below,
    * unless you're taking additional measures to synchronize thread execution
    * across slot size changes.
    */
   drm_intel_bo *scratch_bo;

   /**
    * Scratch slot size allocated for each thread in the buffer object given
    * by \c scratch_bo.
    */
   uint32_t per_thread_scratch;

   /** Offset in the program cache to the program */
   uint32_t prog_offset;

   /** Offset in the batchbuffer to Gen4-5 pipelined state (VS/WM/GS_STATE). */
   uint32_t state_offset;

   uint32_t push_const_offset; /* Offset in the batchbuffer */
   int push_const_size; /* in 256-bit register increments */

   /* Binding table: pointers to SURFACE_STATE entries. */
   uint32_t bind_bo_offset;
   uint32_t surf_offset[BRW_MAX_SURFACES];

   /** SAMPLER_STATE count and table offset */
   uint32_t sampler_count;
   uint32_t sampler_offset;
};

enum brw_predicate_state {
   /* The first two states are used if we can determine whether to draw
    * without having to look at the values in the query object buffer. This
    * will happen if there is no conditional render in progress, if the query
    * object is already completed or if something else has already added
    * samples to the preliminary result such as via a BLT command.
    */
   BRW_PREDICATE_STATE_RENDER,
   BRW_PREDICATE_STATE_DONT_RENDER,
   /* In this case whether to draw or not depends on the result of an
    * MI_PREDICATE command so the predicate enable bit needs to be checked.
    */
   BRW_PREDICATE_STATE_USE_BIT
};

struct shader_times;

struct gen_l3_config;

/**
 * brw_context is derived from gl_context.
 */
struct brw_context
{
   struct gl_context ctx; /**< base class, must be first field */

   struct
   {
      uint32_t (*update_renderbuffer_surface)(struct brw_context *brw,
                                              struct gl_renderbuffer *rb,
                                              uint32_t flags, unsigned unit,
                                              uint32_t surf_index);
      void (*emit_null_surface_state)(struct brw_context *brw,
                                      unsigned width,
                                      unsigned height,
                                      unsigned samples,
                                      uint32_t *out_offset);

      /**
       * Send the appropriate state packets to configure depth, stencil, and
       * HiZ buffers (i965+ only)
       */
      void (*emit_depth_stencil_hiz)(struct brw_context *brw,
                                     struct intel_mipmap_tree *depth_mt,
                                     uint32_t depth_offset,
                                     uint32_t depthbuffer_format,
                                     uint32_t depth_surface_type,
                                     struct intel_mipmap_tree *stencil_mt,
                                     bool hiz, bool separate_stencil,
                                     uint32_t width, uint32_t height,
                                     uint32_t tile_x, uint32_t tile_y);

   } vtbl;

   dri_bufmgr *bufmgr;

   drm_intel_context *hw_ctx;

   /** BO for post-sync nonzero writes for gen6 workaround. */
   drm_intel_bo *workaround_bo;
   uint8_t pipe_controls_since_last_cs_stall;

   /**
    * Set of drm_intel_bo * that have been rendered to within this batchbuffer
    * and would need flushing before being used from another cache domain that
    * isn't coherent with it (i.e. the sampler).
    */
   struct set *render_cache;

   /**
    * Number of resets observed in the system at context creation.
    *
    * This is tracked in the context so that we can determine that another
    * reset has occurred.
    */
   uint32_t reset_count;

   struct intel_batchbuffer batch;
   bool no_batch_wrap;

   struct {
      drm_intel_bo *bo;
      uint32_t next_offset;
   } upload;

   /**
    * Set if rendering has occurred to the drawable's front buffer.
    *
    * This is used in the DRI2 case to detect that glFlush should also copy
    * the contents of the fake front buffer to the real front buffer.
    */
   bool front_buffer_dirty;

   /** Framerate throttling: @{ */
   drm_intel_bo *throttle_batch[2];

   /* Limit the number of outstanding SwapBuffers by waiting for an earlier
    * frame of rendering to complete. This gives a very precise cap to the
    * latency between input and output such that rendering never gets more
    * than a frame behind the user. (With the caveat that we technically are
    * not using the SwapBuffers itself as a barrier but the first batch
    * submitted afterwards, which may be immediately prior to the next
    * SwapBuffers.)
    */
   bool need_swap_throttle;

   /** General throttling, not caught by throttling between SwapBuffers */
   bool need_flush_throttle;
   /** @} */

   GLuint stats_wm;

   /**
    * drirc options:
    * @{
    */
   bool no_rast;
   bool always_flush_batch;
   bool always_flush_cache;
   bool disable_throttling;
   bool precompile;
   bool dual_color_blend_by_location;

   driOptionCache optionCache;
   /** @} */

   GLuint primitive; /**< Hardware primitive, such as _3DPRIM_TRILIST. */

   GLenum reduced_primitive;

   /**
    * Set if we're either a debug context or the INTEL_DEBUG=perf environment
    * variable is set, this is the flag indicating to do expensive work that
    * might lead to a perf_debug() call.
    */
   bool perf_debug;

   uint64_t max_gtt_map_object_size;

   int gen;
   int gt;

   bool is_g4x;
   bool is_baytrail;
   bool is_haswell;
   bool is_cherryview;
   bool is_broxton;

   bool has_hiz;
   bool has_separate_stencil;
   bool must_use_separate_stencil;
   bool has_llc;
   bool has_swizzling;
   bool has_surface_tile_offset;
   bool has_compr4;
   bool has_negative_rhw_bug;
   bool has_pln;
   bool no_simd8;
   bool use_rep_send;
   bool use_resource_streamer;

   /**
    * Some versions of Gen hardware don't do centroid interpolation correctly
    * on unlit pixels, causing incorrect values for derivatives near triangle
    * edges.  Enabling this flag causes the fragment shader to use
    * non-centroid interpolation for unlit pixels, at the expense of two extra
    * fragment shader instructions.
    */
   bool needs_unlit_centroid_workaround;

   struct isl_device isl_dev;

   struct blorp_context blorp;

   GLuint NewGLState;
   struct {
      struct brw_state_flags pipelines[BRW_NUM_PIPELINES];
   } state;

   enum brw_pipeline last_pipeline;

   struct brw_cache cache;

   /** IDs for meta stencil blit shader programs. */
   struct gl_shader_program *meta_stencil_blit_programs[2];

   /* Whether a meta-operation is in progress. */
   bool meta_in_progress;

   /* Whether the last depth/stencil packets were both NULL. */
   bool no_depth_or_stencil;

   /* The last PMA stall bits programmed. */
   uint32_t pma_stall_bits;

   struct {
      struct {
         /** The value of gl_BaseVertex for the current _mesa_prim. */
         int gl_basevertex;

         /** The value of gl_BaseInstance for the current _mesa_prim. */
         int gl_baseinstance;
      } params;

      /**
       * Buffer and offset used for GL_ARB_shader_draw_parameters
       * (for now, only gl_BaseVertex).
       */
      drm_intel_bo *draw_params_bo;
      uint32_t draw_params_offset;

      /**
       * The value of gl_DrawID for the current _mesa_prim. This always comes
       * in from it's own vertex buffer since it's not part of the indirect
       * draw parameters.
       */
      int gl_drawid;
      drm_intel_bo *draw_id_bo;
      uint32_t draw_id_offset;
   } draw;

   struct {
      /**
       * For gl_NumWorkGroups: If num_work_groups_bo is non NULL, then it is
       * an indirect call, and num_work_groups_offset is valid. Otherwise,
       * num_work_groups is set based on glDispatchCompute.
       */
      drm_intel_bo *num_work_groups_bo;
      GLintptr num_work_groups_offset;
      const GLuint *num_work_groups;
   } compute;

   struct {
      struct brw_vertex_element inputs[VERT_ATTRIB_MAX];
      struct brw_vertex_buffer buffers[VERT_ATTRIB_MAX];

      struct brw_vertex_element *enabled[VERT_ATTRIB_MAX];
      GLuint nr_enabled;
      GLuint nr_buffers;

      /* Summary of size and varying of active arrays, so we can check
       * for changes to this state:
       */
      bool index_bounds_valid;
      unsigned int min_index, max_index;

      /* Offset from start of vertex buffer so we can avoid redefining
       * the same VB packed over and over again.
       */
      unsigned int start_vertex_bias;

      /**
       * Certain vertex attribute formats aren't natively handled by the
       * hardware and require special VS code to fix up their values.
       *
       * These bitfields indicate which workarounds are needed.
       */
      uint8_t attrib_wa_flags[VERT_ATTRIB_MAX];
   } vb;

   struct {
      /**
       * Index buffer for this draw_prims call.
       *
       * Updates are signaled by BRW_NEW_INDICES.
       */
      const struct _mesa_index_buffer *ib;

      /* Updates are signaled by BRW_NEW_INDEX_BUFFER. */
      drm_intel_bo *bo;
      uint32_t size;
      GLuint type;

      /* Offset to index buffer index to use in CMD_3D_PRIM so that we can
       * avoid re-uploading the IB packet over and over if we're actually
       * referencing the same index buffer.
       */
      unsigned int start_vertex_offset;
   } ib;

   /* Active vertex program:
    */
   const struct gl_program *vertex_program;
   const struct gl_program *geometry_program;
   const struct gl_program *tess_ctrl_program;
   const struct gl_program *tess_eval_program;
   const struct gl_program *fragment_program;
   const struct gl_program *compute_program;

   /**
    * Number of samples in ctx->DrawBuffer, updated by BRW_NEW_NUM_SAMPLES so
    * that we don't have to reemit that state every time we change FBOs.
    */
   int num_samples;

   /* BRW_NEW_URB_ALLOCATIONS:
    */
   struct {
      GLuint vsize;		/* vertex size plus header in urb registers */
      GLuint gsize;	        /* GS output size in urb registers */
      GLuint hsize;             /* Tessellation control output size in urb registers */
      GLuint dsize;             /* Tessellation evaluation output size in urb registers */
      GLuint csize;		/* constant buffer size in urb registers */
      GLuint sfsize;		/* setup data size in urb registers */

      bool constrained;

      GLuint nr_vs_entries;
      GLuint nr_hs_entries;
      GLuint nr_ds_entries;
      GLuint nr_gs_entries;
      GLuint nr_clip_entries;
      GLuint nr_sf_entries;
      GLuint nr_cs_entries;

      GLuint vs_start;
      GLuint hs_start;
      GLuint ds_start;
      GLuint gs_start;
      GLuint clip_start;
      GLuint sf_start;
      GLuint cs_start;
      /**
       * URB size in the current configuration.  The units this is expressed
       * in are somewhat inconsistent, see gen_device_info::urb::size.
       *
       * FINISHME: Represent the URB size consistently in KB on all platforms.
       */
      GLuint size;

      /* True if the most recently sent _3DSTATE_URB message allocated
       * URB space for the GS.
       */
      bool gs_present;

      /* True if the most recently sent _3DSTATE_URB message allocated
       * URB space for the HS and DS.
       */
      bool tess_present;
   } urb;


   /* BRW_NEW_CURBE_OFFSETS:
    */
   struct {
      GLuint wm_start;  /**< pos of first wm const in CURBE buffer */
      GLuint wm_size;   /**< number of float[4] consts, multiple of 16 */
      GLuint clip_start;
      GLuint clip_size;
      GLuint vs_start;
      GLuint vs_size;
      GLuint total_size;

      /**
       * Pointer to the (intel_upload.c-generated) BO containing the uniforms
       * for upload to the CURBE.
       */
      drm_intel_bo *curbe_bo;
      /** Offset within curbe_bo of space for current curbe entry */
      GLuint curbe_offset;
   } curbe;

   /**
    * Layout of vertex data exiting the geometry portion of the pipleine.
    * This comes from the last enabled shader stage (GS, DS, or VS).
    *
    * BRW_NEW_VUE_MAP_GEOM_OUT is flagged when the VUE map changes.
    */
   struct brw_vue_map vue_map_geom_out;

   struct {
      struct brw_stage_state base;
   } vs;

   struct {
      struct brw_stage_state base;

      /**
       * True if the 3DSTATE_HS command most recently emitted to the 3D
       * pipeline enabled the HS; false otherwise.
       */
      bool enabled;
   } tcs;

   struct {
      struct brw_stage_state base;

      /**
       * True if the 3DSTATE_DS command most recently emitted to the 3D
       * pipeline enabled the DS; false otherwise.
       */
      bool enabled;
   } tes;

   struct {
      struct brw_stage_state base;

      /**
       * True if the 3DSTATE_GS command most recently emitted to the 3D
       * pipeline enabled the GS; false otherwise.
       */
      bool enabled;
   } gs;

   struct {
      struct brw_ff_gs_prog_data *prog_data;

      bool prog_active;
      /** Offset in the program cache to the CLIP program pre-gen6 */
      uint32_t prog_offset;
      uint32_t state_offset;

      uint32_t bind_bo_offset;
      /**
       * Surface offsets for the binding table. We only need surfaces to
       * implement transform feedback so BRW_MAX_SOL_BINDINGS is all that we
       * need in this case.
       */
      uint32_t surf_offset[BRW_MAX_SOL_BINDINGS];
   } ff_gs;

   struct {
      struct brw_clip_prog_data *prog_data;

      /** Offset in the program cache to the CLIP program pre-gen6 */
      uint32_t prog_offset;

      /* Offset in the batch to the CLIP state on pre-gen6. */
      uint32_t state_offset;

      /* As of gen6, this is the offset in the batch to the CLIP VP,
       * instead of vp_bo.
       */
      uint32_t vp_offset;

      /**
       * The number of viewports to use.  If gl_ViewportIndex is written,
       * we can have up to ctx->Const.MaxViewports viewports.  If not,
       * the viewport index is always 0, so we can only emit one.
       */
      uint8_t viewport_count;
   } clip;


   struct {
      struct brw_sf_prog_data *prog_data;

      /** Offset in the program cache to the CLIP program pre-gen6 */
      uint32_t prog_offset;
      uint32_t state_offset;
      uint32_t vp_offset;
      bool viewport_transform_enable;
   } sf;

   struct {
      struct brw_stage_state base;

      GLuint render_surf;

      /**
       * Buffer object used in place of multisampled null render targets on
       * Gen6.  See brw_emit_null_surface_state().
       */
      drm_intel_bo *multisampled_null_render_target_bo;
      uint32_t fast_clear_op;

      float offset_clamp;
   } wm;

   struct {
      struct brw_stage_state base;
   } cs;

   /* RS hardware binding table */
   struct {
      drm_intel_bo *bo;
      uint32_t next_offset;
   } hw_bt_pool;

   struct {
      uint32_t state_offset;
      uint32_t blend_state_offset;
      uint32_t depth_stencil_state_offset;
      uint32_t vp_offset;
   } cc;

   struct {
      struct brw_query_object *obj;
      bool begin_emitted;
   } query;

   struct {
      enum brw_predicate_state state;
      bool supported;
   } predicate;

   int num_atoms[BRW_NUM_PIPELINES];
   const struct brw_tracked_state render_atoms[76];
   const struct brw_tracked_state compute_atoms[11];

   /* If (INTEL_DEBUG & DEBUG_BATCH) */
   struct {
      uint32_t offset;
      uint32_t size;
      enum aub_state_struct_type type;
      int index;
   } *state_batch_list;
   int state_batch_count;

   uint32_t render_target_format[MESA_FORMAT_COUNT];
   bool format_supported_as_render_target[MESA_FORMAT_COUNT];

   /* PrimitiveRestart */
   struct {
      bool in_progress;
      bool enable_cut_index;
   } prim_restart;

   /** Computed depth/stencil/hiz state from the current attached
    * renderbuffers, valid only during the drawing state upload loop after
    * brw_workaround_depthstencil_alignment().
    */
   struct {
      struct intel_mipmap_tree *depth_mt;
      struct intel_mipmap_tree *stencil_mt;

      /* Inter-tile (page-aligned) byte offsets. */
      uint32_t depth_offset, hiz_offset, stencil_offset;
      /* Intra-tile x,y offsets for drawing to depth/stencil/hiz */
      uint32_t tile_x, tile_y;
   } depthstencil;

   uint32_t num_instances;
   int basevertex;
   int baseinstance;

   struct {
      const struct gen_l3_config *config;
   } l3;

   struct {
      drm_intel_bo *bo;
      const char **names;
      int *ids;
      enum shader_time_shader_type *types;
      struct shader_times *cumulative;
      int num_entries;
      int max_entries;
      double report_time;
   } shader_time;

   struct brw_fast_clear_state *fast_clear_state;

   /* Array of flags telling if auxiliary buffer is disabled for corresponding
    * renderbuffer. If draw_aux_buffer_disabled[i] is set then use of
    * auxiliary buffer for gl_framebuffer::_ColorDrawBuffers[i] is
    * disabled.
    * This is needed in case the same underlying buffer is also configured
    * to be sampled but with a format that the sampling engine can't treat
    * compressed or fast cleared.
    */
   bool draw_aux_buffer_disabled[MAX_DRAW_BUFFERS];

   __DRIcontext *driContext;
   struct intel_screen *screen;
};

/* brw_clear.c */
extern void intelInitClearFuncs(struct dd_function_table *functions);

/*======================================================================
 * brw_context.c
 */
extern const char *const brw_vendor_string;

extern const char *
brw_get_renderer_string(const struct intel_screen *screen);

enum {
   DRI_CONF_BO_REUSE_DISABLED,
   DRI_CONF_BO_REUSE_ALL
};

void intel_update_renderbuffers(__DRIcontext *context,
                                __DRIdrawable *drawable);
void intel_prepare_render(struct brw_context *brw);

void intel_resolve_for_dri2_flush(struct brw_context *brw,
                                  __DRIdrawable *drawable);

GLboolean brwCreateContext(gl_api api,
		      const struct gl_config *mesaVis,
		      __DRIcontext *driContextPriv,
                      unsigned major_version,
                      unsigned minor_version,
                      uint32_t flags,
                      bool notify_reset,
                      unsigned *error,
		      void *sharedContextPrivate);

/*======================================================================
 * brw_misc_state.c
 */
void
brw_meta_resolve_color(struct brw_context *brw,
                       struct intel_mipmap_tree *mt);

/*======================================================================
 * brw_misc_state.c
 */
void brw_get_depthstencil_tile_masks(struct intel_mipmap_tree *depth_mt,
                                     uint32_t depth_level,
                                     uint32_t depth_layer,
                                     struct intel_mipmap_tree *stencil_mt,
                                     uint32_t *out_tile_mask_x,
                                     uint32_t *out_tile_mask_y);
void brw_workaround_depthstencil_alignment(struct brw_context *brw,
                                           GLbitfield clear_mask);

/* brw_object_purgeable.c */
void brw_init_object_purgeable_functions(struct dd_function_table *functions);

/*======================================================================
 * brw_queryobj.c
 */
void brw_init_common_queryobj_functions(struct dd_function_table *functions);
void gen4_init_queryobj_functions(struct dd_function_table *functions);
void brw_emit_query_begin(struct brw_context *brw);
void brw_emit_query_end(struct brw_context *brw);
void brw_query_counter(struct gl_context *ctx, struct gl_query_object *q);
bool brw_is_query_pipelined(struct brw_query_object *query);

/** gen6_queryobj.c */
void gen6_init_queryobj_functions(struct dd_function_table *functions);
void brw_write_timestamp(struct brw_context *brw, drm_intel_bo *bo, int idx);
void brw_write_depth_count(struct brw_context *brw, drm_intel_bo *bo, int idx);

/** hsw_queryobj.c */
void hsw_init_queryobj_functions(struct dd_function_table *functions);

/** brw_conditional_render.c */
void brw_init_conditional_render_functions(struct dd_function_table *functions);
bool brw_check_conditional_render(struct brw_context *brw);

/** intel_batchbuffer.c */
void brw_load_register_mem(struct brw_context *brw,
                           uint32_t reg,
                           drm_intel_bo *bo,
                           uint32_t read_domains, uint32_t write_domain,
                           uint32_t offset);
void brw_load_register_mem64(struct brw_context *brw,
                             uint32_t reg,
                             drm_intel_bo *bo,
                             uint32_t read_domains, uint32_t write_domain,
                             uint32_t offset);
void brw_store_register_mem32(struct brw_context *brw,
                              drm_intel_bo *bo, uint32_t reg, uint32_t offset);
void brw_store_register_mem64(struct brw_context *brw,
                              drm_intel_bo *bo, uint32_t reg, uint32_t offset);
void brw_load_register_imm32(struct brw_context *brw,
                             uint32_t reg, uint32_t imm);
void brw_load_register_imm64(struct brw_context *brw,
                             uint32_t reg, uint64_t imm);
void brw_load_register_reg(struct brw_context *brw, uint32_t src,
                           uint32_t dest);
void brw_load_register_reg64(struct brw_context *brw, uint32_t src,
                             uint32_t dest);
void brw_store_data_imm32(struct brw_context *brw, drm_intel_bo *bo,
                          uint32_t offset, uint32_t imm);
void brw_store_data_imm64(struct brw_context *brw, drm_intel_bo *bo,
                          uint32_t offset, uint64_t imm);

/*======================================================================
 * brw_state_dump.c
 */
void brw_debug_batch(struct brw_context *brw);
void brw_annotate_aub(struct brw_context *brw);

/*======================================================================
 * intel_tex_validate.c
 */
void brw_validate_textures( struct brw_context *brw );


/*======================================================================
 * brw_program.c
 */
static inline bool
key_debug(struct brw_context *brw, const char *name, int a, int b)
{
   if (a != b) {
      perf_debug("  %s %d->%d\n", name, a, b);
      return true;
   }
   return false;
}

void brwInitFragProgFuncs( struct dd_function_table *functions );

/* Per-thread scratch space is a power-of-two multiple of 1KB. */
static inline int
brw_get_scratch_size(int size)
{
   return MAX2(1024, util_next_power_of_two(size));
}
void brw_get_scratch_bo(struct brw_context *brw,
			drm_intel_bo **scratch_bo, int size);
void brw_alloc_stage_scratch(struct brw_context *brw,
                             struct brw_stage_state *stage_state,
                             unsigned per_thread_size,
                             unsigned thread_count);
void brw_init_shader_time(struct brw_context *brw);
int brw_get_shader_time_index(struct brw_context *brw,
                              struct gl_program *prog,
                              enum shader_time_shader_type type,
                              bool is_glsl_sh);
void brw_collect_and_report_shader_time(struct brw_context *brw);
void brw_destroy_shader_time(struct brw_context *brw);

/* brw_urb.c
 */
void brw_upload_urb_fence(struct brw_context *brw);

/* brw_curbe.c
 */
void brw_upload_cs_urb_state(struct brw_context *brw);

/* brw_fs_reg_allocate.cpp
 */
void brw_fs_alloc_reg_sets(struct brw_compiler *compiler);

/* brw_vec4_reg_allocate.cpp */
void brw_vec4_alloc_reg_set(struct brw_compiler *compiler);

/* brw_disasm.c */
int brw_disassemble_inst(FILE *file, const struct gen_device_info *devinfo,
                         struct brw_inst *inst, bool is_compacted);

/* brw_vs.c */
gl_clip_plane *brw_select_clip_planes(struct gl_context *ctx);

/* brw_draw_upload.c */
unsigned brw_get_vertex_surface_type(struct brw_context *brw,
                                     const struct gl_vertex_array *glarray);

static inline unsigned
brw_get_index_type(GLenum type)
{
   assert((type == GL_UNSIGNED_BYTE)
          || (type == GL_UNSIGNED_SHORT)
          || (type == GL_UNSIGNED_INT));

   /* The possible values for type are GL_UNSIGNED_BYTE (0x1401),
    * GL_UNSIGNED_SHORT (0x1403), and GL_UNSIGNED_INT (0x1405) which we want
    * to map to scale factors of 0, 1, and 2, respectively.  These scale
    * factors are then left-shfited by 8 to be in the correct position in the
    * CMD_INDEX_BUFFER packet.
    *
    * Subtracting 0x1401 gives 0, 2, and 4.  Shifting left by 7 afterwards
    * gives 0x00000000, 0x00000100, and 0x00000200.  These just happen to be
    * the values the need to be written in the CMD_INDEX_BUFFER packet.
    */
   return (type - 0x1401) << 7;
}

void brw_prepare_vertices(struct brw_context *brw);

/* brw_wm_surface_state.c */
void brw_init_surface_formats(struct brw_context *brw);
void brw_create_constant_surface(struct brw_context *brw,
                                 drm_intel_bo *bo,
                                 uint32_t offset,
                                 uint32_t size,
                                 uint32_t *out_offset);
void brw_create_buffer_surface(struct brw_context *brw,
                               drm_intel_bo *bo,
                               uint32_t offset,
                               uint32_t size,
                               uint32_t *out_offset);
void brw_update_buffer_texture_surface(struct gl_context *ctx,
                                       unsigned unit,
                                       uint32_t *surf_offset);
void
brw_update_sol_surface(struct brw_context *brw,
                       struct gl_buffer_object *buffer_obj,
                       uint32_t *out_offset, unsigned num_vector_components,
                       unsigned stride_dwords, unsigned offset_dwords);
void brw_upload_ubo_surfaces(struct brw_context *brw, struct gl_program *prog,
                             struct brw_stage_state *stage_state,
                             struct brw_stage_prog_data *prog_data);
void brw_upload_abo_surfaces(struct brw_context *brw,
                             const struct gl_program *prog,
                             struct brw_stage_state *stage_state,
                             struct brw_stage_prog_data *prog_data);
void brw_upload_image_surfaces(struct brw_context *brw,
                               const struct gl_program *prog,
                               struct brw_stage_state *stage_state,
                               struct brw_stage_prog_data *prog_data);

/* brw_surface_formats.c */
bool brw_render_target_supported(struct brw_context *brw,
                                 struct gl_renderbuffer *rb);
uint32_t brw_depth_format(struct brw_context *brw, mesa_format format);

/* intel_buffer_objects.c */
int brw_bo_map(struct brw_context *brw, drm_intel_bo *bo, int write_enable,
               const char *bo_name);
int brw_bo_map_gtt(struct brw_context *brw, drm_intel_bo *bo,
                   const char *bo_name);

/* intel_extensions.c */
extern void intelInitExtensions(struct gl_context *ctx);

/* intel_state.c */
extern int intel_translate_shadow_compare_func(GLenum func);
extern int intel_translate_compare_func(GLenum func);
extern int intel_translate_stencil_op(GLenum op);
extern int intel_translate_logic_op(GLenum opcode);

/* brw_sync.c */
void brw_init_syncobj_functions(struct dd_function_table *functions);

/* gen6_sol.c */
struct gl_transform_feedback_object *
brw_new_transform_feedback(struct gl_context *ctx, GLuint name);
void
brw_delete_transform_feedback(struct gl_context *ctx,
                              struct gl_transform_feedback_object *obj);
void
brw_begin_transform_feedback(struct gl_context *ctx, GLenum mode,
			     struct gl_transform_feedback_object *obj);
void
brw_end_transform_feedback(struct gl_context *ctx,
                           struct gl_transform_feedback_object *obj);
GLsizei
brw_get_transform_feedback_vertex_count(struct gl_context *ctx,
                                        struct gl_transform_feedback_object *obj,
                                        GLuint stream);

/* gen7_sol_state.c */
void
gen7_begin_transform_feedback(struct gl_context *ctx, GLenum mode,
                              struct gl_transform_feedback_object *obj);
void
gen7_end_transform_feedback(struct gl_context *ctx,
			    struct gl_transform_feedback_object *obj);
void
gen7_pause_transform_feedback(struct gl_context *ctx,
                              struct gl_transform_feedback_object *obj);
void
gen7_resume_transform_feedback(struct gl_context *ctx,
                               struct gl_transform_feedback_object *obj);

/* hsw_sol.c */
void
hsw_begin_transform_feedback(struct gl_context *ctx, GLenum mode,
                             struct gl_transform_feedback_object *obj);
void
hsw_end_transform_feedback(struct gl_context *ctx,
                           struct gl_transform_feedback_object *obj);
void
hsw_pause_transform_feedback(struct gl_context *ctx,
                             struct gl_transform_feedback_object *obj);
void
hsw_resume_transform_feedback(struct gl_context *ctx,
                              struct gl_transform_feedback_object *obj);

/* brw_blorp_blit.cpp */
GLbitfield
brw_blorp_framebuffer(struct brw_context *brw,
                      struct gl_framebuffer *readFb,
                      struct gl_framebuffer *drawFb,
                      GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                      GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                      GLbitfield mask, GLenum filter);

bool
brw_blorp_copytexsubimage(struct brw_context *brw,
                          struct gl_renderbuffer *src_rb,
                          struct gl_texture_image *dst_image,
                          int slice,
                          int srcX0, int srcY0,
                          int dstX0, int dstY0,
                          int width, int height);

/* gen6_multisample_state.c */
unsigned
gen6_determine_sample_mask(struct brw_context *brw);

void
gen6_emit_3dstate_multisample(struct brw_context *brw,
                              unsigned num_samples);
void
gen6_emit_3dstate_sample_mask(struct brw_context *brw, unsigned mask);
void
gen6_get_sample_position(struct gl_context *ctx,
                         struct gl_framebuffer *fb,
                         GLuint index,
                         GLfloat *result);
void
gen6_set_sample_maps(struct gl_context *ctx);

/* gen8_multisample_state.c */
void gen8_emit_3dstate_multisample(struct brw_context *brw, unsigned num_samp);
void gen8_emit_3dstate_sample_pattern(struct brw_context *brw);

/* gen7_urb.c */
void
gen7_emit_push_constant_state(struct brw_context *brw, unsigned vs_size,
                              unsigned hs_size, unsigned ds_size,
                              unsigned gs_size, unsigned fs_size);

void
gen6_upload_urb(struct brw_context *brw, unsigned vs_size,
                bool gs_present, unsigned gs_size);
void
gen7_upload_urb(struct brw_context *brw, unsigned vs_size,
                bool gs_present, bool tess_present);

/* brw_reset.c */
extern GLenum
brw_get_graphics_reset_status(struct gl_context *ctx);
void
brw_check_for_reset(struct brw_context *brw);

/* brw_compute.c */
extern void
brw_init_compute_functions(struct dd_function_table *functions);

/*======================================================================
 * Inline conversion functions.  These are better-typed than the
 * macros used previously:
 */
static inline struct brw_context *
brw_context( struct gl_context *ctx )
{
   return (struct brw_context *)ctx;
}

static inline struct brw_program *
brw_program(struct gl_program *p)
{
   return (struct brw_program *) p;
}

static inline const struct brw_program *
brw_program_const(const struct gl_program *p)
{
   return (const struct brw_program *) p;
}

/**
 * Pre-gen6, the register file of the EUs was shared between threads,
 * and each thread used some subset allocated on a 16-register block
 * granularity.  The unit states wanted these block counts.
 */
static inline int
brw_register_blocks(int reg_count)
{
   return ALIGN(reg_count, 16) / 16 - 1;
}

static inline uint32_t
brw_program_reloc(struct brw_context *brw, uint32_t state_offset,
		  uint32_t prog_offset)
{
   if (brw->gen >= 5) {
      /* Using state base address. */
      return prog_offset;
   }

   drm_intel_bo_emit_reloc(brw->batch.bo,
			   state_offset,
			   brw->cache.bo,
			   prog_offset,
			   I915_GEM_DOMAIN_INSTRUCTION, 0);

   return brw->cache.bo->offset64 + prog_offset;
}

bool brw_do_cubemap_normalize(struct exec_list *instructions);

extern const char * const conditional_modifier[16];
extern const char *const pred_ctrl_align16[16];

static inline bool
brw_depth_writes_enabled(const struct brw_context *brw)
{
   const struct gl_context *ctx = &brw->ctx;

   /* We consider depth writes disabled if the depth function is GL_EQUAL,
    * because it would just overwrite the existing depth value with itself.
    *
    * These bonus depth writes not only use bandwidth, but they also can
    * prevent early depth processing.  For example, if the pixel shader
    * discards, the hardware must invoke the to determine whether or not
    * to do the depth write.  If writes are disabled, we may still be able
    * to do the depth test before the shader, and skip the shader execution.
    *
    * The Broadwell 3DSTATE_WM_DEPTH_STENCIL documentation also contains
    * a programming note saying to disable depth writes for EQUAL.
    */
   return ctx->Depth.Test && ctx->Depth.Mask && ctx->Depth.Func != GL_EQUAL;
}

void
brw_emit_depthbuffer(struct brw_context *brw);

void
brw_emit_depth_stencil_hiz(struct brw_context *brw,
                           struct intel_mipmap_tree *depth_mt,
                           uint32_t depth_offset, uint32_t depthbuffer_format,
                           uint32_t depth_surface_type,
                           struct intel_mipmap_tree *stencil_mt,
                           bool hiz, bool separate_stencil,
                           uint32_t width, uint32_t height,
                           uint32_t tile_x, uint32_t tile_y);

void
gen6_emit_depth_stencil_hiz(struct brw_context *brw,
                            struct intel_mipmap_tree *depth_mt,
                            uint32_t depth_offset, uint32_t depthbuffer_format,
                            uint32_t depth_surface_type,
                            struct intel_mipmap_tree *stencil_mt,
                            bool hiz, bool separate_stencil,
                            uint32_t width, uint32_t height,
                            uint32_t tile_x, uint32_t tile_y);

void
gen7_emit_depth_stencil_hiz(struct brw_context *brw,
                            struct intel_mipmap_tree *depth_mt,
                            uint32_t depth_offset, uint32_t depthbuffer_format,
                            uint32_t depth_surface_type,
                            struct intel_mipmap_tree *stencil_mt,
                            bool hiz, bool separate_stencil,
                            uint32_t width, uint32_t height,
                            uint32_t tile_x, uint32_t tile_y);
void
gen8_emit_depth_stencil_hiz(struct brw_context *brw,
                            struct intel_mipmap_tree *depth_mt,
                            uint32_t depth_offset, uint32_t depthbuffer_format,
                            uint32_t depth_surface_type,
                            struct intel_mipmap_tree *stencil_mt,
                            bool hiz, bool separate_stencil,
                            uint32_t width, uint32_t height,
                            uint32_t tile_x, uint32_t tile_y);

void gen8_hiz_exec(struct brw_context *brw, struct intel_mipmap_tree *mt,
                   unsigned int level, unsigned int layer, enum blorp_hiz_op op);

uint32_t get_hw_prim_for_gl_prim(int mode);

void
gen6_upload_push_constants(struct brw_context *brw,
                           const struct gl_program *prog,
                           const struct brw_stage_prog_data *prog_data,
                           struct brw_stage_state *stage_state,
                           enum aub_state_struct_type type);

bool
gen9_use_linear_1d_layout(const struct brw_context *brw,
                          const struct intel_mipmap_tree *mt);

/* brw_pipe_control.c */
int brw_init_pipe_control(struct brw_context *brw,
			  const struct gen_device_info *info);
void brw_fini_pipe_control(struct brw_context *brw);

void brw_emit_pipe_control_flush(struct brw_context *brw, uint32_t flags);
void brw_emit_pipe_control_write(struct brw_context *brw, uint32_t flags,
                                 drm_intel_bo *bo, uint32_t offset,
                                 uint32_t imm_lower, uint32_t imm_upper);
void brw_emit_mi_flush(struct brw_context *brw);
void brw_emit_post_sync_nonzero_flush(struct brw_context *brw);
void brw_emit_depth_stall_flushes(struct brw_context *brw);
void gen7_emit_vs_workaround_flush(struct brw_context *brw);
void gen7_emit_cs_stall_flush(struct brw_context *brw);

/* brw_queryformat.c */
void brw_query_internal_format(struct gl_context *ctx, GLenum target,
                               GLenum internalFormat, GLenum pname,
                               GLint *params);

#ifdef __cplusplus
}
#endif

#endif
