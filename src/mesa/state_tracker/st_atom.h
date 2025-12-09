/**************************************************************************
 * 
 * Copyright 2003 VMware, Inc.
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
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  */
    

#ifndef ST_ATOM_H
#define ST_ATOM_H

#include <stdint.h>
#include "util/bitset.h"

#ifdef __cplusplus
extern "C" {
#endif

struct st_context;
struct gl_vertex_program;
struct st_common_variant;
struct pipe_vertex_buffer;
struct pipe_vertex_element;
struct cso_velems_state;
struct gl_context;
struct gl_vertex_array_object;
struct gl_buffer_object;

void
st_setup_arrays(struct st_context *st,
                const struct gl_vertex_program *vp,
                const struct st_common_variant *vp_variant,
                struct cso_velems_state *velements,
                struct pipe_vertex_buffer *vbuffer, unsigned *num_vbuffers);

void
st_setup_current_user(struct st_context *st,
                      const struct gl_vertex_program *vp,
                      const struct st_common_variant *vp_variant,
                      struct cso_velems_state *velements,
                      struct pipe_vertex_buffer *vbuffer, unsigned *num_vbuffers);

void
st_init_update_array(struct st_context *st);

struct pipe_vertex_state *
st_create_gallium_vertex_state(struct gl_context *ctx,
                               const struct gl_vertex_array_object *vao,
                               struct gl_buffer_object *indexbuf,
                               uint32_t enabled_attribs);

/* Define ST_NEW_xxx */
enum {
#define ST_STATE(FLAG, st_update) FLAG,
#include "st_atom_list.h"
#undef ST_STATE
   ST_NUM_ATOMS,
};

/* Declare function prototypes. */
#define ST_STATE(FLAG, st_update) void st_update(struct st_context *st);
#include "st_atom_list.h"
#undef ST_STATE

typedef BITSET_DECLARE(st_state_bitset, ST_NUM_ATOMS);

#define ST_SET_STATE(bitset, state) BITSET_SET(bitset, state)
#define ST_SET_STATE2(bitset, state1, state2) \
   do { \
      ST_SET_STATE(bitset, state1); \
      ST_SET_STATE(bitset, state2); \
   } while (0)
#define ST_SET_STATE3(bitset, state1, state2, state3) \
   do { \
      ST_SET_STATE2(bitset, state1, state2); \
      ST_SET_STATE(bitset, state3); \
   } while (0)
#define ST_SET_STATE4(bitset, state1, state2, state3, state4) \
   do { \
      ST_SET_STATE3(bitset, state1, state2, state3); \
      ST_SET_STATE(bitset, state4); \
   } while (0)

#define ST_SET_STATES(bitset1, bitset2) BITSET_OR(bitset1, bitset1, bitset2)

#define ST_SET_SHADER_STATES(bitset, state) \
   do { \
      ST_SET_STATE3(bitset, ST_NEW_VS_##state, ST_NEW_TCS_##state, ST_NEW_TES_##state); \
      ST_SET_STATE3(bitset, ST_NEW_GS_##state, ST_NEW_FS_##state, ST_NEW_CS_##state); \
      ST_SET_STATE2(bitset, ST_NEW_TS_##state, ST_NEW_MS_##state); \
   } while (0)

#define ST_SET_FRAMEBUFFER_STATES(bitset) \
   ST_SET_STATE3(bitset, ST_NEW_FB_STATE, ST_NEW_SAMPLE_STATE, ST_NEW_SAMPLE_SHADING)

#define ST_SET_VERTEX_PROGRAM_STATES(bitset, ctx, p) \
   do { \
      BITSET_OR(bitset, bitset, (p)->affected_states); \
      if (st_user_clip_planes_enabled(ctx)) \
         ST_SET_STATE(bitset, ST_NEW_CLIP_STATE); \
   } while (0)

#define ST_SET_ALL_STATES(bitset) \
   BITSET_SET_RANGE(bitset, 0, ST_NUM_ATOMS - 1)

#define ST_SHADER_STATE_MASK(bitset, shader) \
   ST_SET_STATE4(bitset, ST_NEW_##shader##_STATE, ST_NEW_##shader##_SAMPLER_VIEWS, \
                 ST_NEW_##shader##_SAMPLERS, ST_NEW_##shader##_CONSTANTS); \
   ST_SET_STATE4(bitset, ST_NEW_##shader##_UBOS, ST_NEW_##shader##_ATOMICS, \
                 ST_NEW_##shader##_SSBOS, ST_NEW_##shader##_IMAGES);

/* This is in fact states for vertex pipeline (as opposite to mesh pipeline).
 * But we still include mesh shader states to ensure mesh shader is not set
 * when using vertex pipeline (so does the mesh pipeline) to simplify driver
 * implementation.
 *
 * TODO: remove mesh shader states from this macro once driver is ready.
 */
#define ST_PIPELINE_RENDER_STATE_MASK(bitset) \
   st_state_bitset bitset = {0}; \
   ST_SHADER_STATE_MASK(bitset, CS); \
   BITSET_NOT(bitset);

#define ST_PIPELINE_RENDER_STATE_MASK_NO_VARRAYS(bitset) \
   ST_PIPELINE_RENDER_STATE_MASK(bitset); \
   BITSET_CLEAR(bitset, ST_NEW_VERTEX_ARRAYS);

#define ST_PIPELINE_CLEAR_STATE_MASK(bitset) \
   st_state_bitset bitset = {0}; \
   ST_SET_STATE3(bitset, ST_NEW_FB_STATE, ST_NEW_SCISSOR, ST_NEW_WINDOW_RECTANGLES);

#define ST_PIPELINE_META_STATE_MASK(bitset) \
   ST_PIPELINE_RENDER_STATE_MASK_NO_VARRAYS(bitset)

/* For ReadPixels, ReadBuffer, GetSamplePosition: */
#define ST_PIPELINE_UPDATE_FB_STATE_MASK(bitset) \
   st_state_bitset bitset = {0}; \
   ST_SET_STATE(bitset, ST_NEW_FB_STATE);

/* We add the ST_NEW_FB_STATE bit here as well, because glBindFramebuffer
 * acts as a barrier that breaks feedback loops between the framebuffer
 * and textures bound to the framebuffer, even when those textures are
 * accessed by compute shaders; so we must inform the driver of new
 * framebuffer state.
 */
#define ST_PIPELINE_COMPUTE_STATE_MASK(bitset) \
   st_state_bitset bitset = {0}; \
   ST_SHADER_STATE_MASK(bitset, CS); \
   ST_SET_STATE(bitset, ST_NEW_FB_STATE);

#define ST_PIPELINE_MESH_STATE_MASK(bitset) \
   ST_PIPELINE_RENDER_STATE_MASK(bitset); \
   BITSET_CLEAR(bitset, ST_NEW_VERTEX_ARRAYS); \
   BITSET_CLEAR(bitset, ST_NEW_TESS_STATE); \
   BITSET_CLEAR(bitset, ST_NEW_PIXEL_TRANSFER);

#ifdef __cplusplus
}
#endif

#endif
