/*
 * Copyright 2026 XclipseTools
 * SPDX-License-Identifier: MIT
 *
 * Ported from JimVulkan/mesa-unified-drivers (Bifrost geometry stage
 * emulation) by biblioklept. Debug scaffolding (hardpos/echo/dump) removed.
 */

/*
 * Geometry shaders on hardware that has no geometry stage.
 *
 * The Mali job manager runs vertex -> tiler -> fragment and nothing else.
 * The vertex shader WRITES VARYINGS TO MEMORY, and the tiler reads them
 * back from buffers whose addresses and strides live in descriptors we
 * build (position is a vec4 at stride 16, point size an fp16 at stride 2,
 * everything else is packed into one generic buffer). So a geometry shader
 * is a COMPUTE JOB SPLICED BETWEEN THE TWO: it reads the vertex shader's
 * varying buffers, runs the geometry shader body once per input primitive,
 * and writes a fresh set of varying buffers for the tiler.
 *
 * WORST-CASE ALLOCATION. How many vertices a geometry shader emits is not
 * known until it runs. Every input primitive gets a fixed slot of
 * max_vertices output vertices and (max_vertices - 2) output primitives, so
 * the counts are known at record time, and slots the shader did not fill are
 * made DEGENERATE so the tiler discards them.
 *
 * STRIPS BECOME LISTS VIA AN INDEX BUFFER. The pass writes each vertex once
 * and appends INDICES describing the expanded primitive list. Unused index
 * slots repeat one index, a zero-area primitive that is culled.
 *
 * The pass leaves a plain compute shader that reads its buffer addresses
 * from push constants (struct panvk_gs_push).
 */

#include "panvk_gs.h"
#include "poly/geometry.h"
#include "poly/tessellator.h"

#include "nir.h"
#include "nir_builder.h"
#include "nir_xfb_info.h"

static unsigned
gs_glsl_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

/* One 32-bit store per component: a multi-component store_global did not
 * reach memory in JimVulkan's measurements on G72/G76.
 */
static void
store_components(nir_builder *b, nir_def *base, nir_def *byte_off, nir_def *val)
{
   for (unsigned c = 0; c < val->num_components; c++) {
      nir_def *addr =
         nir_iadd(b, base, nir_u2u64(b, nir_iadd_imm(b, byte_off, c * 4)));
      nir_store_global(b, nir_channel(b, val, c), addr, .align_mul = 4,
                       .write_mask = 0x1);
   }
}

/* The mirror of store_components: component-at-a-time loads. */
static nir_def *
load_components(nir_builder *b, nir_def *base, nir_def *byte_off,
                unsigned num_components, unsigned bit_size)
{
   const unsigned sz = bit_size / 8;
   nir_def *comps[NIR_MAX_VEC_COMPONENTS];

   for (unsigned c = 0; c < num_components; c++) {
      nir_def *addr =
         nir_iadd(b, base, nir_u2u64(b, nir_iadd_imm(b, byte_off, c * sz)));
      comps[c] = nir_load_global(b, 1, bit_size, addr, .align_mul = sz);
   }

   return nir_vec(b, comps, num_components);
}

struct lower_gs_state {
   const struct panvk_gs_lower_options *opts;

   /* The params block, loaded once at the top of the shader. */
   nir_def *in_pos, *in_general;
   nir_def *out_pos, *out_general, *out_index;
   nir_def *in_offsets, *out_offsets;
   nir_def *in_general_stride, *out_general_stride;
   nir_def *vp_scale, *vp_offset;
   nir_def *in_screen_space;
   nir_def *out_clip_space;
   /* gl_ViewportIndex: the shader's variable, and the per-viewport table. */
   nir_variable *viewport_var;
   nir_def *viewports;
   /* Per-viewport index lists (jm-v7 031), see panvk_gs_push::vp_split. */
   nir_def *vp_split, *index_region, *out_viewport;
   /* gl_PointSize (jm-v7 033), see panvk_gs_push::in_psiz. */
   nir_def *in_psiz, *in_psiz_stride, *out_psiz;
   /* gl_PrimitiveIDIn (per instance) and gl_InvocationID. */
   nir_def *prim_id;
   /* What load_primitive_id returns when it is not prim_id: the patch ID
    * of a tess-fed geometry shader (jm-v7 049). */
   nir_def *api_prim_id;
   nir_def *invocation_id;
   /* Input assembly. */
   nir_def *instance;
   nir_def *prims_per_instance;
   nir_def *index_buf, *index_size, *topology, *instance_stride;
   /* Subtracted from an index to get the vertex record. */
   nir_def *index_bias;
   /* Vertices per input primitive when opts->input_verts_per_prim is 0. */
   nir_def *vpp_dyn;
   /* Tessellation: the draw's struct poly_tess_params. */
   nir_def *tess_params;

   /* Base output vertex slot for this invocation. */
   nir_def *out_vtx_base;
   /* Base output index slot. */
   nir_def *out_idx_base;
   /* Primitives this invocation has written indices for, per stream. */
   nir_variable *prims_written[4];
   /* geometryStreams: see panvk_gs_push::xfb_stream_*. */
   nir_def *stream_slots, *stream_idx, *stream_index, *xfb_invocations;

   /* This compute invocation, and transform feedback (xfb_dwords == 0: the
    * shader captures nothing).
    */
   nir_def *gid;
   unsigned xfb_dwords;
   nir_def *xfb_counts, *xfb_staging;
};

int
panvk_gs_xfb_table(const nir_xfb_info *xfb, uint32_t *table)
{
   if (!xfb)
      return 0;

   int n = 0;
   for (unsigned i = 0; i < xfb->output_count; i++) {
      const nir_xfb_output_info *o = &xfb->outputs[i];

      /* 32-bit data only. */
      if (o->data_is_16bit)
         return -1;

      unsigned dw = o->offset / 4;
      u_foreach_bit(c, o->component_mask) {
         if (n >= PANVK_XFB_MAX_RECORD_DWORDS)
            return -1;
         table[n++] = ((uint32_t)o->buffer << 16) | dw++;
      }
   }

   return n;
}

/* Component comp of output location loc as the shader holds it right now,
 * or NULL when no 32-bit output variable covers it.
 */
static nir_def *
xfb_output_component(nir_builder *b, unsigned loc, unsigned comp)
{
   nir_foreach_shader_out_variable(var, b->shader) {
      const struct glsl_type *type = var->type;
      nir_deref_instr *d = nir_build_deref_var(b, var);

      if (var->data.compact) {
         const unsigned first =
            var->data.location * 4 + var->data.location_frac;
         const unsigned want = loc * 4 + comp;
         if (want < first || want >= first + glsl_get_length(type))
            continue;
         return nir_load_deref(b, nir_build_deref_array_imm(b, d, want - first));
      }

      const unsigned slots = glsl_count_attribute_slots(type, false);
      if (loc < var->data.location || loc >= var->data.location + slots)
         continue;

      if (glsl_type_is_array_or_matrix(type)) {
         const struct glsl_type *elem = glsl_type_is_array(type)
                                           ? glsl_get_array_element(type)
                                           : glsl_get_column_type(type);
         if (glsl_count_attribute_slots(elem, false) != 1)
            continue;
         d = nir_build_deref_array_imm(b, d, loc - var->data.location);
         type = elem;
      }

      if (!glsl_type_is_vector_or_scalar(type) || glsl_get_bit_size(type) != 32)
         continue;

      const unsigned frac = var->data.location_frac;
      if (comp < frac || comp >= frac + glsl_get_vector_elements(type))
         continue;

      return nir_channel(b, nir_load_deref(b, d), comp - frac);
   }

   return NULL;
}

/* EmitVertex with transform feedback: the vertex's captured components, in
 * table order, into its staging record. gl_Position is still clip space.
 */
static void
stage_xfb_record(nir_builder *b, struct lower_gs_state *st, nir_def *slot)
{
   const nir_xfb_info *xfb = st->opts->xfb;

   nir_push_if(b, nir_ine_imm(b, st->xfb_staging, 0));
   {
      nir_def *rec = nir_iadd(
         b, st->xfb_staging,
         nir_u2u64(b, nir_imul_imm(b, slot, st->xfb_dwords * 4)));
      unsigned j = 0;
      for (unsigned i = 0; i < xfb->output_count; i++) {
         const nir_xfb_output_info *o = &xfb->outputs[i];
         u_foreach_bit(c, o->component_mask) {
            nir_def *v = xfb_output_component(b, o->location, c);
            if (!v)
               v = nir_imm_int(b, 0);
            nir_store_global(b, v, nir_iadd_imm(b, rec, j * 4), .align_mul = 4,
                             .write_mask = 0x1);
            j++;
         }
      }
   }
   nir_pop_if(b, NULL);
}

static nir_def *
load_param_u64(nir_builder *b, const struct panvk_gs_lower_options *opts,
               unsigned offset)
{
   return nir_load_push_constant(b, 1, 64, nir_imm_int(b, 0),
                                 .base = opts->params_base + offset, .range = 8);
}

static nir_def *
load_param_u32(nir_builder *b, const struct panvk_gs_lower_options *opts,
               unsigned offset)
{
   return nir_load_push_constant(b, 1, 32, nir_imm_int(b, 0),
                                 .base = opts->params_base + offset, .range = 4);
}

static nir_def *
load_param_vec3f(nir_builder *b, const struct panvk_gs_lower_options *opts,
                 unsigned offset)
{
   nir_def *c[3];

   for (unsigned i = 0; i < 3; i++)
      c[i] = nir_load_push_constant(b, 1, 32, nir_imm_int(b, 0),
                                    .base = opts->params_base + offset + i * 4,
                                    .range = 4);

   return nir_vec(b, c, 3);
}

/* The tiler takes POSITION in screen space with w replaced by 1/w. A vertex
 * shader feeding this emulation is compiled WITHOUT the viewport transform
 * (pan_compile_inputs::vs_keep_clip_space), so the input is clip space and
 * the transform is applied once, on the way out. A vertex shader compiled
 * for the tiler (in_screen_space) is converted back first; a zero scale
 * selects zero rather than dividing.
 */
static nir_def *
screen_to_clip(nir_builder *b, struct lower_gs_state *st, nir_def *p)
{
   nir_def *w = nir_frcp(b, nir_channel(b, p, 3));
   nir_def *zero3 = nir_replicate(b, nir_imm_float(b, 0.0f), 3);
   nir_def *rcp_scale = nir_bcsel(b, nir_feq(b, st->vp_scale, zero3), zero3,
                                  nir_frcp(b, st->vp_scale));
   nir_def *ndc =
      nir_fmul(b, nir_fsub(b, nir_trim_vector(b, p, 3), st->vp_offset),
               rcp_scale);
   nir_def *clip = nir_fmul(b, ndc, nir_replicate(b, w, 3));

   return nir_vec4(b, nir_channel(b, clip, 0), nir_channel(b, clip, 1),
                   nir_channel(b, clip, 2), w);
}

static nir_def *
clip_to_screen_xf(nir_builder *b, nir_def *p, nir_def *vp_scale,
                  nir_def *vp_offset)
{
   /* Same arithmetic and clamp as nir_lower_viewport_transform. */
   nir_def *w_recip = nir_frcp(b, nir_channel(b, p, 3));
   w_recip = nir_fclamp(b, w_recip, nir_imm_float(b, -32768.0f),
                        nir_imm_float(b, 32768.0f));

   nir_def *ndc =
      nir_fmul(b, nir_trim_vector(b, p, 3), nir_replicate(b, w_recip, 3));
   nir_def *screen = nir_fadd(b, nir_fmul(b, ndc, vp_scale), vp_offset);

   /* The vertex shader's position reaches memory through the SNAP_4 varying
    * format (1/256 px). These are plain stores, so snap here too, or shared
    * edges rasterize differently from the same draw without a GS.
    */
   nir_def *snap_x = nir_fmul_imm(
      b, nir_fround_even(b, nir_fmul_imm(b, nir_channel(b, screen, 0), 256.0)),
      1.0 / 256.0);
   nir_def *snap_y = nir_fmul_imm(
      b, nir_fround_even(b, nir_fmul_imm(b, nir_channel(b, screen, 1), 256.0)),
      1.0 / 256.0);

   return nir_vec4(b, snap_x, snap_y, nir_channel(b, screen, 2), w_recip);
}

static nir_def *
clip_to_screen(nir_builder *b, struct lower_gs_state *st, nir_def *p)
{
   return clip_to_screen_xf(b, p, st->vp_scale, st->vp_offset);
}

/* slot -> byte offset, from the table the draw path filled in. */
static nir_def *
load_slot_offset(nir_builder *b, nir_def *table, unsigned location)
{
   nir_def *addr = nir_iadd(b, table, nir_imm_int64(b, location * 4));
   return nir_load_global(b, 1, 32, addr, .align_mul = 4);
}

/* The address in an ATTRIBUTE_BUFFER descriptor: bits 6-55 of its first 64
 * bits, on 32-bit halves (no 64-bit AND with an immediate on Bifrost).
 */
static nir_def *
desc_pointer(nir_builder *b, nir_def *desc)
{
   nir_def *lo = nir_load_global(b, 1, 32, desc, .align_mul = 8);
   nir_def *hi =
      nir_load_global(b, 1, 32, nir_iadd_imm(b, desc, 4), .align_mul = 4);
   return nir_pack_64_2x32_split(b, nir_iand_imm(b, lo, 0xffffffc0),
                                 nir_iand_imm(b, hi, 0x00ffffff));
}

/* An indirect draw's input assembly, read inside the caller's
 * indirect_cmd != 0 branch. See struct panvk_gs_push.
 */
struct indirect_inputs {
   nir_def *inst, *index_buf, *bias, *stride, *pos, *gen;
};

static struct indirect_inputs
load_indirect_inputs(nir_builder *b, const struct panvk_gs_lower_options *opts,
                     nir_def *cmd, nir_def *index_buf, nir_def *index_size)
{
   struct indirect_inputs in;
   nir_def *indexed = nir_ine_imm(b, index_size, 0);
   in.inst = nir_load_global(b, 1, 32, nir_iadd_imm(b, cmd, 4), .align_mul = 4);
   nir_def *first =
      nir_load_global(b, 1, 32, nir_iadd_imm(b, cmd, 8), .align_mul = 4);

   in.index_buf = nir_bcsel(
      b, indexed,
      nir_iadd(b, index_buf, nir_u2u64(b, nir_imul(b, first, index_size))),
      nir_imm_int64(b, 0));
   /* Only an indexed draw has a minimum to read. */
   nir_def *min_addr =
      load_param_u64(b, opts, offsetof(struct panvk_gs_push, index_min));
   /* The else value must exist before the if: a phi only follows phis. */
   nir_def *zero = nir_imm_int(b, 0);
   nir_push_if(b, indexed);
   nir_def *min_val = nir_load_global(b, 1, 32, min_addr, .align_mul = 4);
   nir_pop_if(b, NULL);
   in.bias = nir_if_phi(b, min_val, zero);

   /* Instance Size, a "padded" count: (2 * odd + 1) << shift. */
   nir_def *dcd =
      load_param_u64(b, opts, offsetof(struct panvk_gs_push, vertex_dcd));
   nir_def *w0 = nir_load_global(b, 1, 32, dcd, .align_mul = 4);
   nir_def *packed = nir_iand_imm(b, nir_ushr_imm(b, w0, 16), 0xff);
   nir_def *padded = nir_ishl(
      b, nir_iadd_imm(b, nir_imul_imm(b, nir_ushr_imm(b, packed, 5), 2), 1),
      nir_iand_imm(b, packed, 0x1f));
   in.stride = nir_bcsel(b, nir_ugt_imm(b, in.inst, 1), padded, nir_imm_int(b, 0));

   nir_def *pdesc =
      load_param_u64(b, opts, offsetof(struct panvk_gs_push, in_pos_desc));
   nir_def *gdesc =
      load_param_u64(b, opts, offsetof(struct panvk_gs_push, in_general_desc));
   in.pos = desc_pointer(b, pdesc);
   in.gen = desc_pointer(b, gdesc);
   return in;
}

/* A direct indexed draw whose vertex job starts at the smallest index:
 * indices are biased by it; index_min is 0 when there is none.
 */
static nir_def *
load_direct_bias(nir_builder *b, const struct panvk_gs_lower_options *opts,
                 nir_def *index_size)
{
   nir_def *min_addr =
      load_param_u64(b, opts, offsetof(struct panvk_gs_push, index_min));
   nir_def *zero = nir_imm_int(b, 0);
   nir_push_if(b, nir_iand(b, nir_ine_imm(b, min_addr, 0),
                           nir_ine_imm(b, index_size, 0)));
   nir_def *v = nir_load_global(b, 1, 32, min_addr, .align_mul = 4);
   nir_pop_if(b, NULL);
   return nir_if_phi(b, v, zero);
}

/* The vertex shader's point sizes (jm-v7 033): an indirect draw's from the
 * patched descriptor. 0 when it writes none.
 */
static nir_def *
load_in_psiz(nir_builder *b, const struct panvk_gs_lower_options *opts,
             nir_def *indirect_cmd)
{
   nir_def *direct =
      load_param_u64(b, opts, offsetof(struct panvk_gs_push, in_psiz));
   nir_def *desc =
      load_param_u64(b, opts, offsetof(struct panvk_gs_push, in_psiz_desc));
   nir_push_if(b, nir_iand(b, nir_ine_imm(b, indirect_cmd, 0),
                           nir_ine_imm(b, desc, 0)));
   nir_def *ind = desc_pointer(b, desc);
   nir_pop_if(b, NULL);
   return nir_if_phi(b, ind, direct);
}

/* Which of the vertex shader's varying records is vertex i of this
 * invocation's input primitive: primitive assembly, then the index buffer,
 * then the instance.
 */
static nir_def *
gs_input_record(nir_builder *b, struct lower_gs_state *st, nir_def *i)
{
   const unsigned vpp = st->opts->input_verts_per_prim;
   nir_def *p = st->prim_id;

   /* LIST: p * vpp + i */
   nir_def *list = vpp ? nir_iadd(b, nir_imul_imm(b, p, vpp), i)
                       : nir_iadd(b, nir_imul(b, p, st->vpp_dyn), i);

   /* STRIP: p + i, triangle strips take p, p+2, p+1 on odd p. */
   nir_def *strip_i = i;
   if (vpp == 3) {
      nir_def *odd = nir_ine_imm(b, nir_iand_imm(b, p, 1), 0);
      nir_def *swapped =
         nir_bcsel(b, nir_ieq_imm(b, i, 1), nir_imm_int(b, 2),
                   nir_bcsel(b, nir_ieq_imm(b, i, 2), nir_imm_int(b, 1), i));
      strip_i = nir_bcsel(b, odd, swapped, i);
   }
   nir_def *strip = nir_iadd(b, p, strip_i);

   /* FAN: p + 1, p + 2, 0 */
   nir_def *fan = nir_bcsel(b, nir_ieq_imm(b, i, 2), nir_imm_int(b, 0),
                            nir_iadd(b, p, nir_iadd_imm(b, i, 1)));

   /* TRIANGLE STRIP WITH ADJACENCY, in the shader's order
    * (v0, a01, v1, a12, v2, a20):
    *    even p:  2p, first ? 1 : 2p-2,  2p+2, last ? 2p+5 : 2p+6,  2p+4, 2p+3
    *    odd p:   2p, 2p+3,  2p+4, last ? 2p+5 : 2p+6,  2p+2, 2p-2
    */
   nir_def *tsa = list;
   if (vpp == 6) {
      nir_def *p2 = nir_imul_imm(b, p, 2);
      nir_def *odd = nir_ine_imm(b, nir_iand_imm(b, p, 1), 0);
      nir_def *first = nir_ieq_imm(b, p, 0);
      nir_def *last = nir_ieq(b, p, nir_iadd_imm(b, st->prims_per_instance, -1));
      nir_def *far =
         nir_bcsel(b, last, nir_iadd_imm(b, p2, 5), nir_iadd_imm(b, p2, 6));
      nir_def *even_v[6] = {
         p2,
         nir_bcsel(b, first, nir_imm_int(b, 1), nir_iadd_imm(b, p2, -2)),
         nir_iadd_imm(b, p2, 2),
         far,
         nir_iadd_imm(b, p2, 4),
         nir_iadd_imm(b, p2, 3),
      };
      /* Vulkan order (jm-v7 023), the OpenGL one rotated so that v0 is
       * 2p like every other triangle of the strip.
       */
      nir_def *odd_v[6] = {
         p2, nir_iadd_imm(b, p2, 3), nir_iadd_imm(b, p2, 4),
         far, nir_iadd_imm(b, p2, 2), nir_iadd_imm(b, p2, -2),
      };
      nir_def *pick = nir_bcsel(b, odd, odd_v[5], even_v[5]);
      for (int k = 4; k >= 0; k--)
         pick = nir_bcsel(b, nir_ieq_imm(b, i, k),
                          nir_bcsel(b, odd, odd_v[k], even_v[k]), pick);
      tsa = pick;
   }

   /* A patch is only ever a list. */
   nir_def *seq =
      !vpp ? list
           : nir_bcsel(
                b, nir_ieq_imm(b, st->topology, PANVK_GS_TOPO_STRIP), strip,
                nir_bcsel(b, nir_ieq_imm(b, st->topology, PANVK_GS_TOPO_FAN),
                          fan,
                          nir_bcsel(b,
                                    nir_ieq_imm(b, st->topology,
                                                PANVK_GS_TOPO_TRI_STRIP_ADJ),
                                    tsa, list)));

   /* Through the index buffer, when there is one. */
   nir_def *vtx = seq;
   nir_push_if(b, nir_ine_imm(b, st->index_buf, 0));
   {
      nir_def *addr = nir_iadd(b, st->index_buf,
                               nir_u2u64(b, nir_imul(b, seq, st->index_size)));
      /* The aligned 32-bit word holding the index, then the index. */
      nir_def *word_addr = nir_iand_imm(b, addr, ~3ull);
      nir_def *word = nir_load_global(b, 1, 32, word_addr, .align_mul = 4);
      nir_def *shift =
         nir_imul_imm(b, nir_u2u32(b, nir_iand_imm(b, addr, 3)), 8);
      nir_def *shifted = nir_ushr(b, word, shift);
      nir_def *idx = nir_bcsel(
         b, nir_ieq_imm(b, st->index_size, 1), nir_iand_imm(b, shifted, 0xff),
         nir_bcsel(b, nir_ieq_imm(b, st->index_size, 2),
                   nir_iand_imm(b, shifted, 0xffff), word));
      vtx = nir_isub(b, idx, st->index_bias);
   }
   nir_pop_if(b, NULL);
   vtx = nir_if_phi(b, vtx, seq);

   return nir_iadd(b, vtx, nir_imul(b, st->instance, st->instance_stride));
}

/* gl_in[i].<slot>: load_per_vertex_input with the vertex index as src[0].
 * The vertex shader already wrote it to its varying buffers, so the load
 * becomes an address computation and a global load.
 */
static nir_def *
lower_per_vertex_input(nir_builder *b, nir_intrinsic_instr *intr,
                       struct lower_gs_state *st)
{
   const struct panvk_gs_lower_options *opts = st->opts;
   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

   nir_def *vtx = gs_input_record(b, st, intr->src[0].ssa);

   if (sem.location == VARYING_SLOT_POS) {
      nir_def *byte = nir_imul_imm(b, vtx, opts->pos_stride);
      nir_def *raw = load_components(b, st->in_pos, byte, 4, 32);
      nir_def *clip = nir_bcsel(b, nir_ine_imm(b, st->in_screen_space, 0),
                                screen_to_clip(b, st, raw), raw);

      unsigned first = nir_intrinsic_component(intr);
      nir_def *c[NIR_MAX_VEC_COMPONENTS];
      for (unsigned i = 0; i < intr->def.num_components; i++)
         c[i] = nir_channel(b, clip, MIN2(first + i, 3));

      return nir_vec(b, c, intr->def.num_components);
   }

   /* gl_in[i].gl_PointSize (jm-v7 033): the fp16 point size buffer, 1.0
    * when the vertex shader writes none.
    */
   if (sem.location == VARYING_SLOT_PSIZ && intr->def.bit_size == 32) {
      nir_def *one = nir_imm_float(b, 1.0f);
      nir_push_if(b, nir_ine_imm(b, st->in_psiz, 0));
      nir_def *addr = nir_iadd(
         b, st->in_psiz, nir_u2u64(b, nir_imul(b, vtx, st->in_psiz_stride)));
      nir_def *word = nir_load_global(b, 1, 32, nir_iand_imm(b, addr, ~3ull),
                                      .align_mul = 4);
      nir_def *shift =
         nir_imul_imm(b, nir_u2u32(b, nir_iand_imm(b, addr, 2)), 8);
      nir_def *h = nir_u2u16(b, nir_ushr(b, word, shift));
      nir_def *size = nir_f2f32(b, h);
      nir_pop_if(b, NULL);
      nir_def *v = nir_if_phi(b, size, one);
      return nir_replicate(b, v, intr->def.num_components);
   }

   /* An input array element: the io semantics name the variable's first
    * location and src[1] the slot within it. The geometry shader lowers
    * indirect input derefs first, a tess control shader may index an input
    * array dynamically.
    */
   nir_def *offset;
   if (nir_src_is_const(intr->src[1])) {
      offset = load_slot_offset(b, st->in_offsets,
                                sem.location + nir_src_as_uint(intr->src[1]));
   } else {
      nir_def *loc = nir_iadd_imm(b, intr->src[1].ssa, sem.location);
      offset = nir_load_global(
         b, 1, 32,
         nir_iadd(b, st->in_offsets, nir_u2u64(b, nir_imul_imm(b, loc, 4))),
         .align_mul = 4);
   }

   nir_def *base = st->in_general;
   nir_def *stride = st->in_general_stride;

   nir_def *byte =
      nir_iadd(b, nir_imul(b, vtx, stride),
               nir_iadd_imm(b, offset, nir_intrinsic_component(intr) * 4));

   /* A slot the vertex shader does not write reads as zero, and must not be
    * loaded at all.
    */
   nir_def *zero =
      nir_imm_zero(b, intr->def.num_components, intr->def.bit_size);
   nir_push_if(b, nir_ine_imm(b, offset, PANVK_GS_SLOT_UNUSED));
   nir_def *val = load_components(b, base, byte, intr->def.num_components,
                                  intr->def.bit_size);
   nir_pop_if(b, NULL);
   return nir_if_phi(b, val, zero);
}

/* Output vertex slot counter of stream s, the address of index list entry
 * `entry` of stream s for this invocation, and the address of its primitive
 * count. Stream 0 is the rasterized one.
 */
static nir_def *
stream_vtx_slot(nir_builder *b, struct lower_gs_state *st, unsigned s,
                nir_def *counter)
{
   nir_def *slot = nir_iadd(b, st->out_vtx_base, counter);
   return s ? nir_iadd(b, slot, nir_imul_imm(b, st->stream_slots, s)) : slot;
}

static nir_def *
stream_index_entry(nir_builder *b, struct lower_gs_state *st, unsigned s,
                   nir_def *entry)
{
   nir_def *e = nir_iadd(b, st->out_idx_base, entry);
   if (!s)
      return nir_iadd(b, st->out_index, nir_u2u64(b, nir_imul_imm(b, e, 4)));
   e = nir_iadd(b, e, nir_imul_imm(b, st->stream_idx, s - 1));
   return nir_iadd(b, st->stream_index, nir_u2u64(b, nir_imul_imm(b, e, 4)));
}

/* Per-viewport index lists (jm-v7 031): with vp_split viewports, the
 * rasterized index list is repeated after the full one (which transform
 * feedback reads), once per viewport, and a primitive is also listed in the
 * copy of the viewport its first vertex (vp_slot) selected. Each copy is
 * drawn by its own tiler job, with that viewport's scissor and depth range.
 */
static void
store_split_index(nir_builder *b, struct lower_gs_state *st, nir_def *vp_slot,
                  nir_def *entry, nir_def *v)
{
   if (!st->viewport_var)
      return;

   nir_push_if(b, nir_ine_imm(b, st->vp_split, 0));
   {
      nir_def *vp = nir_load_global(
         b, 1, 32,
         nir_iadd(b, st->out_viewport, nir_u2u64(b, nir_imul_imm(b, vp_slot, 4))),
         .align_mul = 4);
      vp = nir_umin(b, vp, nir_iadd_imm(b, st->vp_split, -1));
      nir_def *e = nir_iadd(b, nir_iadd(b, st->out_idx_base, entry),
                            nir_imul(b, nir_iadd_imm(b, vp, 1), st->index_region));
      nir_store_global(b, v,
                       nir_iadd(b, st->out_index, nir_u2u64(b, nir_imul_imm(b, e, 4))),
                       .align_mul = 4, .write_mask = 0x1);
   }
   nir_pop_if(b, NULL);
}

static nir_def *
stream_count_addr(nir_builder *b, struct lower_gs_state *st, unsigned s)
{
   nir_def *i =
      s ? nir_iadd(b, st->gid, nir_imul_imm(b, st->xfb_invocations, s)) : st->gid;
   return nir_iadd(b, st->xfb_counts, nir_u2u64(b, nir_imul_imm(b, i, 4)));
}

/* EmitVertex(): flush a whole output vertex to memory. */
static void
lower_emit_vertex(nir_builder *b, nir_intrinsic_instr *intr,
                  struct lower_gs_state *st)
{
   const struct panvk_gs_lower_options *opts = st->opts;
   nir_def *counter = intr->src[0].ssa;
   const unsigned stream = nir_intrinsic_stream_id(intr);

   /* A vertex on another stream than 0 only matters to transform feedback. */
   if (stream != 0) {
      if (!st->xfb_dwords)
         return;
      nir_push_if(b, nir_iand(b, nir_ult_imm(b, counter, opts->max_output_verts),
                              nir_ine_imm(b, st->xfb_staging, 0)));
      {
         nir_def *slot = stream_vtx_slot(b, st, stream, counter);
         stage_xfb_record(b, st, slot);
         if (opts->output_verts_per_prim == 1) {
            nir_def *n = nir_load_var(b, st->prims_written[stream]);
            nir_store_global(b, slot, stream_index_entry(b, st, stream, n),
                             .align_mul = 4, .write_mask = 0x1);
            nir_def *written = nir_iadd_imm(b, n, 1);
            nir_store_var(b, st->prims_written[stream], written, 1);
            nir_store_global(b, written, stream_count_addr(b, st, stream),
                             .align_mul = 4, .write_mask = 0x1);
         }
      }
      nir_pop_if(b, NULL);
      return;
   }

   /* Never run off the end of this invocation's slot. */
   nir_push_if(b, nir_ult_imm(b, counter, opts->max_output_verts));
   {
      nir_def *slot = nir_iadd(b, st->out_vtx_base, counter);

      nir_foreach_shader_out_variable(var, b->shader) {
         const unsigned loc = var->data.location;

         if (loc == VARYING_SLOT_POS) {
            nir_def *clip = nir_load_var(b, var);
            nir_def *val;
            if (st->viewport_var) {
               /* The transform of the viewport this vertex names (clamped). */
               nir_def *idx =
                  nir_umin(b, nir_load_var(b, st->viewport_var),
                           nir_imm_int(b, PANVK_GS_MAX_VIEWPORTS - 1));
               nir_def *xf = nir_iadd(
                  b, st->viewports,
                  nir_u2u64(b, nir_imul_imm(b, idx, PANVK_GS_VIEWPORT_STRIDE)));
               nir_def *scale = load_components(b, xf, nir_imm_int(b, 0), 3, 32);
               nir_def *offset =
                  load_components(b, xf, nir_imm_int(b, 12), 3, 32);
               val = clip_to_screen_xf(b, clip, scale, offset);

               /* Per-viewport index lists: EndPrimitive needs the viewport
                * of each vertex it lists.
                */
               nir_push_if(b, nir_ine_imm(b, st->vp_split, 0));
               nir_store_global(
                  b, idx,
                  nir_iadd(b, st->out_viewport,
                           nir_u2u64(b, nir_imul_imm(b, slot, 4))),
                  .align_mul = 4, .write_mask = 0x1);
               nir_pop_if(b, NULL);

               /* multiViewport (jm-v7 017): the tiler only has one scissor
                * box (the union), so the selected viewport's scissor is a
                * clip-distance kill in the FS. d >= 0 inside; times clip w
                * so that perspective interpolation keeps the sign.
                */
               if (opts->scissor_slot) {
                  nir_def *sc = load_components(b, xf, nir_imm_int(b, 24), 4, 32);
                  nir_def *sx = nir_channel(b, val, 0);
                  nir_def *sy = nir_channel(b, val, 1);
                  nir_def *w = nir_channel(b, clip, 3);
                  nir_def *d = nir_vec4(
                     b, nir_fsub(b, sx, nir_channel(b, sc, 0)),
                     nir_fsub(b, nir_channel(b, sc, 2), sx),
                     nir_fsub(b, sy, nir_channel(b, sc, 1)),
                     nir_fsub(b, nir_channel(b, sc, 3), sy));
                  d = nir_fmul(b, d, nir_replicate(b, w, 4));
                  nir_def *soff =
                     load_slot_offset(b, st->out_offsets, opts->scissor_slot);
                  nir_push_if(b, nir_ine_imm(b, soff, PANVK_GS_SLOT_UNUSED));
                  store_components(
                     b, st->out_general,
                     nir_iadd(b, nir_imul(b, slot, st->out_general_stride),
                              soff),
                     d);
                  nir_pop_if(b, NULL);
               }
            } else {
               val = clip_to_screen(b, st, clip);
            }
            val = nir_bcsel(b, nir_ine_imm(b, st->out_clip_space, 0), clip, val);
            store_components(b, st->out_pos,
                             nir_imul_imm(b, slot, opts->pos_stride), val);
            continue;
         }

         /* gl_PointSize of an emitted point (jm-v7 033): fp32, the tiler
          * reads it as a FP32 point size array.
          */
         if (loc == VARYING_SLOT_PSIZ) {
            if (opts->output_verts_per_prim == 1 && !var->data.compact) {
               nir_def *size = nir_channel(b, nir_load_var(b, var), 0);
               if (size->bit_size == 32) {
                  nir_push_if(b, nir_ine_imm(b, st->out_psiz, 0));
                  nir_store_global(
                     b, size,
                     nir_iadd(b, st->out_psiz,
                              nir_u2u64(b, nir_imul_imm(b, slot, 4))),
                     .align_mul = 4, .write_mask = 0x1);
                  nir_pop_if(b, NULL);
               }
            }
            continue;
         }

         /* gl_ViewportIndex goes on to the fragment shader. */
         if (loc == VARYING_SLOT_VIEWPORT) {
            nir_def *offset =
               load_slot_offset(b, st->out_offsets, PANVK_GS_VIEWPORT_INDEX_SLOT);
            nir_push_if(b, nir_ine_imm(b, offset, PANVK_GS_SLOT_UNUSED));
            nir_def *byte =
               nir_iadd(b, nir_imul(b, slot, st->out_general_stride), offset);
            store_components(b, st->out_general, byte, nir_load_var(b, var));
            nir_pop_if(b, NULL);
            continue;
         }

         /* Point size lives in its own fp16 buffer, and the other built-ins
          * (layer, compact clip distances) have no path to the tiler here.
          * The vec4 clip/cull slots a lowered vertex shader writes (see
          * panvk_lower_clip_cull_vs) are generic, and the transform feedback
          * passthrough carries them on to the fragment shader's discard.
          */
         const bool dist_slot =
            loc == VARYING_SLOT_CLIP_DIST0 || loc == VARYING_SLOT_CLIP_DIST1 ||
            loc == VARYING_SLOT_CULL_DIST0 || loc == VARYING_SLOT_CULL_DIST1;
         if ((loc < VARYING_SLOT_VAR0 && !dist_slot) || var->data.compact)
            continue;

         /* One location per element of an output array, each at the
          * component the variable starts at.
          */
         const struct glsl_type *type = var->type;
         const unsigned elems =
            glsl_type_is_array(type) ? glsl_get_length(type) : 1;
         const struct glsl_type *elem_type =
            glsl_type_is_array(type) ? glsl_get_array_element(type) : type;
         if (!glsl_type_is_vector_or_scalar(elem_type))
            continue;

         for (unsigned e = 0; e < elems; e++) {
            nir_deref_instr *d = nir_build_deref_var(b, var);
            if (glsl_type_is_array(type))
               d = nir_build_deref_array_imm(b, d, e);
            nir_def *val = nir_load_deref(b, d);
            if (val->bit_size != 32)
               continue;

            nir_def *offset = load_slot_offset(b, st->out_offsets, loc + e);

            /* A slot the fragment shader does not read has no home. */
            nir_push_if(b, nir_ine_imm(b, offset, PANVK_GS_SLOT_UNUSED));
            nir_def *byte = nir_iadd(
               b, nir_imul(b, slot, st->out_general_stride),
               nir_iadd_imm(b, offset, var->data.location_frac * 4));
            store_components(b, st->out_general, byte, val);
            nir_pop_if(b, NULL);
         }
      }

      if (st->xfb_dwords)
         stage_xfb_record(b, st, slot);

      /* Points: nir_lower_gs_intrinsics removes EndPrimitive, so the point
       * is recorded here; its index is its own slot.
       */
      if (opts->output_verts_per_prim == 1) {
         nir_def *addr = nir_iadd(
            b, st->out_index,
            nir_u2u64(b, nir_imul_imm(b, nir_iadd(b, st->out_idx_base, counter),
                                      4)));
         nir_store_global(b, slot, addr, .align_mul = 4, .write_mask = 0x1);
         store_split_index(b, st, slot, counter, slot);
         nir_def *written = nir_iadd_imm(b, counter, 1);
         nir_store_var(b, st->prims_written[0], written, 1);
         if (st->xfb_dwords) {
            nir_push_if(b, nir_ine_imm(b, st->xfb_counts, 0));
            nir_store_global(
               b, written,
               nir_iadd(b, st->xfb_counts,
                        nir_u2u64(b, nir_imul_imm(b, st->gid, 4))),
               .align_mul = 4, .write_mask = 0x1);
            nir_pop_if(b, NULL);
         }
      }
   }
   nir_pop_if(b, NULL);
}

/* *table[k] += val, a 64-bit counter from 32-bit atomics (jm-v7 016). */
static void
gs_pstats_add(nir_builder *b, nir_def *table, unsigned k, nir_def *val)
{
   nir_def *addr = nir_pack_64_2x32(
      b, nir_load_global(b, 2, 32, nir_iadd_imm(b, table, k * 8),
                         .align_mul = 8));
   nir_push_if(b, nir_iand(b, nir_ine_imm(b, addr, 0), nir_ine_imm(b, val, 0)));
   {
      nir_def *old = nir_global_atomic(b, 32, addr, val,
                                       .atomic_op = nir_atomic_op_iadd);
      nir_push_if(b, nir_ult(b, nir_iadd(b, old, val), old));
      nir_global_atomic(b, 32, nir_iadd_imm(b, addr, 4), nir_imm_int(b, 1),
                        .atomic_op = nir_atomic_op_iadd);
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

/* EndPrimitive(), and the implicit one at the end of the shader: write the
 * index list that turns the strip into independent primitives.
 */
static void
lower_end_primitive(nir_builder *b, nir_intrinsic_instr *intr,
                    struct lower_gs_state *st)
{
   const struct panvk_gs_lower_options *opts = st->opts;
   const unsigned vpp = opts->output_verts_per_prim;
   const unsigned stream = nir_intrinsic_stream_id(intr);

   if (stream != 0 && !st->xfb_dwords)
      return;

   /* end_primitive_with_counter(count, count_per_primitive): src[0] is the
    * running total of vertices, src[1] the vertex count of THIS primitive.
    */
   nir_def *vtx_total = intr->src[0].ssa;
   nir_def *vtx_in_prim = intr->src[1].ssa;

   nir_def *vtx_start = nir_isub(b, vtx_total, vtx_in_prim);

   /* A strip of N vertices expands to N - (vpp - 1) primitives. */
   nir_def *nprims = nir_imax(b, nir_iadd_imm(b, vtx_in_prim, -(int)(vpp - 1)),
                              nir_imm_int(b, 0));

   nir_def *prim_base = nir_load_var(b, st->prims_written[stream]);

   nir_variable *i =
      nir_local_variable_create(b->impl, glsl_uint_type(), "gs_strip_i");
   nir_store_var(b, i, nir_imm_int(b, 0), 1);

   nir_push_loop(b);
   {
      nir_def *iv = nir_load_var(b, i);
      nir_push_if(b, nir_uge(b, iv, nprims));
      nir_jump(b, nir_jump_break);
      nir_pop_if(b, NULL);

      nir_def *idx_entry = nir_imul_imm(b, nir_iadd(b, prim_base, iv), vpp);

      for (unsigned k = 0; k < vpp; k++) {
         /* Every other triangle of a strip is reversed; bake the swap into
          * the indices since the tiler draws an independent list.
          */
         nir_def *vbase =
            stream_vtx_slot(b, st, stream, nir_iadd(b, vtx_start, iv));
         nir_def *v = nir_iadd_imm(b, vbase, k);
         if (vpp == 3 && k != 0) {
            nir_def *odd = nir_iand_imm(b, iv, 1);
            nir_def *swapped = nir_iadd_imm(b, vbase, k == 1 ? 2 : 1);
            v = nir_bcsel(b, nir_ine_imm(b, odd, 0), swapped, v);
         }

         nir_def *addr =
            stream_index_entry(b, st, stream, nir_iadd_imm(b, idx_entry, k));
         nir_store_global(b, v, addr, .align_mul = 4, .write_mask = 0x1);
         if (stream == 0)
            store_split_index(b, st, vbase, nir_iadd_imm(b, idx_entry, k), v);
      }

      nir_store_var(b, i, nir_iadd_imm(b, iv, 1), 1);
   }
   nir_pop_loop(b, NULL);

   nir_store_var(b, st->prims_written[stream], nir_iadd(b, prim_base, nprims),
                 1);

   /* Keep the transform feedback count current at every EndPrimitive. */
   if (st->xfb_dwords) {
      nir_push_if(b, nir_ine_imm(b, st->xfb_counts, 0));
      nir_store_global(b, nir_iadd(b, prim_base, nprims),
                       stream_count_addr(b, st, stream), .align_mul = 4,
                       .write_mask = 0x1);
      nir_pop_if(b, NULL);
   }
}

/* Every index slot this invocation owns starts as a degenerate primitive,
 * so the ones the shader never reaches are discarded by the tiler.
 */
static void
emit_degenerate_fill(nir_builder *b, struct lower_gs_state *st)
{
   const struct panvk_gs_lower_options *opts = st->opts;
   const unsigned total = opts->max_output_prims * opts->output_verts_per_prim;

   /* jm-v7 082: the degenerate entries all name this invocation's first
    * vertex slot. Zero area does not cull a point (or, on Mali, reliably a
    * zero-length line): an invocation that emits nothing would draw that
    * slot's stale position. Since jm-v7 078 the output buffers of direct
    * draws come from the reused GPU-only pool instead of fresh (zeroed)
    * desc BOs, so the slot holds a vertex of an earlier draw (fill_mode
    * POINT/LINE cases with culled triangles: 2 stray pixels on the A06).
    * Give it a zero position (what the fresh BOs held) and size before the
    * shader body; a real EmitVertex overwrites it.
    */
   store_components(b, st->out_pos,
                    nir_imul_imm(b, st->out_vtx_base, opts->pos_stride),
                    nir_imm_vec4(b, 0.0f, 0.0f, 0.0f, 0.0f));
   if (opts->output_verts_per_prim == 1) {
      nir_push_if(b, nir_ine_imm(b, st->out_psiz, 0));
      nir_store_global(b, nir_imm_float(b, 0.0f),
                       nir_iadd(b, st->out_psiz,
                                nir_u2u64(b, nir_imul_imm(b, st->out_vtx_base, 4))),
                       .align_mul = 4, .write_mask = 0x1);
      nir_pop_if(b, NULL);
      /* Points have no EndPrimitive: an invocation that emits none would
       * leave its xfb count unwritten (stream 0).
       */
      if (st->xfb_dwords) {
         nir_push_if(b, nir_ine_imm(b, st->xfb_counts, 0));
         nir_store_global(b, nir_imm_int(b, 0),
                          nir_iadd(b, st->xfb_counts,
                                   nir_u2u64(b, nir_imul_imm(b, st->gid, 4))),
                          .align_mul = 4, .write_mask = 0x1);
         nir_pop_if(b, NULL);
      }
   }

   nir_variable *i =
      nir_local_variable_create(b->impl, glsl_uint_type(), "gs_fill_i");
   nir_store_var(b, i, nir_imm_int(b, 0), 1);

   /* The per-viewport copies too (store_split_index()). */
   nir_def *limit = nir_imm_int(b, total);
   if (st->viewport_var)
      limit = nir_imul_imm(b, nir_iadd_imm(b, st->vp_split, 1), total);

   nir_push_loop(b);
   {
      nir_def *iv = nir_load_var(b, i);
      nir_push_if(b, nir_uge(b, iv, limit));
      nir_jump(b, nir_jump_break);
      nir_pop_if(b, NULL);

      nir_def *entry = iv;
      if (st->viewport_var && total) {
         nir_def *region = nir_udiv_imm(b, iv, total);
         entry = nir_iadd(b, nir_isub(b, iv, nir_imul_imm(b, region, total)),
                          nir_imul(b, region, st->index_region));
      }

      /* All indices equal => zero area => culled. */
      nir_def *addr = nir_iadd(
         b, st->out_index,
         nir_u2u64(b, nir_imul_imm(b, nir_iadd(b, st->out_idx_base, entry), 4)));
      nir_store_global(b, st->out_vtx_base, addr, .align_mul = 4,
                       .write_mask = 0x1);

      nir_store_var(b, i, nir_iadd_imm(b, iv, 1), 1);
   }
   nir_pop_loop(b, NULL);

   nir_barrier(b, .memory_scope = SCOPE_WORKGROUP,
               .memory_semantics = NIR_MEMORY_ACQ_REL,
               .memory_modes = nir_var_mem_global);
}

static bool
lower_gs_instr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   struct lower_gs_state *st = data;

   b->cursor = nir_before_instr(&intr->instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_per_vertex_input: {
      nir_def *val = lower_per_vertex_input(b, intr, st);
      nir_def_replace(&intr->def, val);
      return true;
   }
   case nir_intrinsic_emit_vertex_with_counter:
      lower_emit_vertex(b, intr, st);
      nir_instr_remove(&intr->instr);
      return true;
   case nir_intrinsic_end_primitive_with_counter:
      lower_end_primitive(b, intr, st);
      nir_instr_remove(&intr->instr);
      return true;
   case nir_intrinsic_set_vertex_and_primitive_count:
      /* The counts are fixed by the worst-case allocation. */
      nir_instr_remove(&intr->instr);
      return true;
   case nir_intrinsic_load_primitive_id:
      nir_def_replace(&intr->def,
                      st->api_prim_id ? st->api_prim_id : st->prim_id);
      return true;
   case nir_intrinsic_load_invocation_id:
      nir_def_replace(&intr->def, st->invocation_id);
      return true;
   case nir_intrinsic_load_tess_param_buffer_poly:
      nir_def_replace(&intr->def, st->tess_params);
      return true;
   default:
      return false;
   }
}

static bool
gs_reads_primitive_id(nir_function_impl *impl)
{
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_intrinsic &&
             nir_instr_as_intrinsic(instr)->intrinsic ==
                nir_intrinsic_load_primitive_id)
            return true;
      }
   }
   return false;
}

bool
panvk_nir_lower_gs(nir_shader *nir, const struct panvk_gs_lower_options *opts)
{
   assert(nir->info.stage == MESA_SHADER_GEOMETRY);

   /* A shader that returns ends its last primitive implicitly: add the
    * EndPrimitive so a strip the shader never closed still writes indices.
    */
   {
      nir_function_impl *impl = nir_shader_get_entrypoint(nir);
      nir_builder b = nir_builder_create(impl);
      nir_foreach_pred(pred, impl->end_block) {
         b.cursor = nir_after_block_before_jump(pred);
         u_foreach_bit(s, opts->stream_mask | 1)
            nir_end_primitive(&b, .stream_id = s);
      }
      nir_progress(true, impl, nir_metadata_control_flow);
   }

   /* Give every EmitVertex/EndPrimitive a running counter, and make an
    * incomplete trailing primitive discard itself.
    */
   nir->info.gs.active_stream_mask = opts->stream_mask | 1;
   NIR_PASS(_, nir, nir_lower_gs_intrinsics,
            nir_lower_gs_intrinsics_count_primitives |
               nir_lower_gs_intrinsics_count_vertices_per_primitive |
               nir_lower_gs_intrinsics_overwrite_incomplete |
               ((opts->stream_mask & ~1u) ? nir_lower_gs_intrinsics_per_stream
                                          : 0));

   /* Inputs become load_per_vertex_input with a constant vertex index.
    * Outputs stay variables: EmitVertex reads back what they hold NOW.
    */
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_lower_indirect_derefs_to_if_else_trees,
            nir_var_shader_in, UINT32_MAX);
   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in, gs_glsl_type_size, 0);
   NIR_PASS(_, nir, nir_opt_constant_folding);

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_builder b = nir_builder_at(nir_before_impl(impl));

   struct lower_gs_state st = {.opts = opts};

   for (unsigned s = 0; s < 4; s++) {
      st.prims_written[s] =
         nir_local_variable_create(impl, glsl_uint_type(), "gs_prims_written");
      nir_store_var(&b, st.prims_written[s], nir_imm_int(&b, 0), 1);
   }

#define P64(f) load_param_u64(&b, opts, offsetof(struct panvk_gs_push, f))
#define P32(f) load_param_u32(&b, opts, offsetof(struct panvk_gs_push, f))
   nir_def *in_pos_direct = P64(in_pos);
   nir_def *in_general_direct = P64(in_general);
   st.out_pos = P64(out_pos);
   st.out_general = P64(out_general);
   st.out_index = P64(out_index);
   st.in_offsets = P64(in_offsets);
   st.out_offsets = P64(out_offsets);
   st.vp_scale =
      load_param_vec3f(&b, opts, offsetof(struct panvk_gs_push, viewport_scale));
   st.vp_offset =
      load_param_vec3f(&b, opts, offsetof(struct panvk_gs_push, viewport_offset));
   st.in_screen_space = P32(in_screen_space);
   st.out_clip_space = P32(out_clip_space);
   st.viewport_var = nir_find_variable_with_location(nir, nir_var_shader_out,
                                                     VARYING_SLOT_VIEWPORT);
   if (st.viewport_var) {
      st.viewports = P64(viewports);
      st.vp_split = P32(vp_split);
      st.index_region = P32(index_region);
      st.out_viewport = P64(out_viewport);
   }
   st.in_general_stride = P32(in_general_stride);
   st.out_general_stride = P32(out_general_stride);
   st.tess_params = P64(tess_params);

   nir_def *index_buf_direct = P64(index_buf);
   st.index_size = P32(index_size);
   st.topology = P32(topology);
   nir_def *instance_stride_direct = P32(instance_stride);
   nir_def *ppi_direct = P32(prims_per_instance);
   nir_def *indirect_cmd = P64(indirect_cmd);

   /* One invocation per (input primitive, gl_InvocationID) pair. */
   nir_def *gid = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
   const unsigned invocations = MAX2(opts->invocations, 1);
   nir_def *prim_global = gid;
   st.invocation_id = nir_imm_int(&b, 0);
   if (invocations > 1) {
      prim_global = nir_udiv_imm(&b, gid, invocations);
      st.invocation_id = nir_umod_imm(&b, gid, invocations);
   }

   nir_def *num_prims = P32(num_input_prims);

   st.out_vtx_base = nir_imul_imm(&b, gid, opts->max_output_verts);
   st.out_idx_base =
      nir_imul_imm(&b, gid, opts->max_output_prims * opts->output_verts_per_prim);

   /* Trailing invocations of the last workgroup own no output slots. */
   nir_push_if(&b, nir_uge(&b, prim_global, num_prims));
   nir_jump(&b, nir_jump_return);
   nir_pop_if(&b, NULL);

   /* Transform feedback: every dispatched invocation owns a count, zeroed
    * first in case it is past an indirect draw's real primitive count.
    */
   st.gid = gid;
   uint32_t xfb_table[PANVK_XFB_MAX_RECORD_DWORDS];
   const int xfb_dwords = panvk_gs_xfb_table(opts->xfb, xfb_table);
   if (xfb_dwords < 0)
      return false;
   st.xfb_dwords = xfb_dwords;
   if (st.xfb_dwords) {
      st.xfb_counts = P64(xfb_counts);
      st.xfb_staging = P64(xfb_staging);
      st.stream_slots = P32(xfb_stream_slots);
      st.stream_idx = P32(xfb_stream_idx);
      st.stream_index = P64(xfb_stream_index);
      st.xfb_invocations = P32(xfb_invocations);
      nir_push_if(&b, nir_ine_imm(&b, st.xfb_counts, 0));
      u_foreach_bit(s, opts->stream_mask | 1)
         nir_store_global(&b, nir_imm_int(&b, 0), stream_count_addr(&b, &st, s),
                          .align_mul = 4, .write_mask = 0x1);
      nir_pop_if(&b, NULL);
   }

   /* An indirect draw: the real parameters are in the argument buffer, and
    * where the vertex shader wrote is in the patched descriptors.
    */
   nir_def *prims_per_instance, *actual_prims;
   nir_def *bias_direct = load_direct_bias(&b, opts, st.index_size);
   nir_push_if(&b, nir_ine_imm(&b, indirect_cmd, 0));
   nir_def *ppi_ind, *total_ind, *index_buf_ind, *stride_ind, *bias_ind,
      *pos_ind, *gen_ind;
   {
      const unsigned vpp = MAX2(opts->input_verts_per_prim, 1);
      nir_def *n = nir_load_global(&b, 1, 32, indirect_cmd, .align_mul = 4);
      const struct indirect_inputs in =
         load_indirect_inputs(&b, opts, indirect_cmd, index_buf_direct,
                              st.index_size);

      nir_def *list = nir_udiv_imm(&b, n, vpp);
      nir_def *strip =
         nir_bcsel(&b, nir_uge_imm(&b, n, vpp),
                   nir_iadd_imm(&b, n, -(int)(vpp - 1)), nir_imm_int(&b, 0));
      nir_def *fan = nir_bcsel(&b, nir_uge_imm(&b, n, 3), nir_iadd_imm(&b, n, -2),
                               nir_imm_int(&b, 0));
      nir_def *tsa =
         nir_bcsel(&b, nir_uge_imm(&b, n, 6),
                   nir_udiv_imm(&b, nir_iadd_imm(&b, n, -4), 2), nir_imm_int(&b, 0));
      ppi_ind = nir_bcsel(
         &b, nir_ieq_imm(&b, st.topology, PANVK_GS_TOPO_STRIP), strip,
         nir_bcsel(&b, nir_ieq_imm(&b, st.topology, PANVK_GS_TOPO_FAN), fan,
                   nir_bcsel(&b,
                             nir_ieq_imm(&b, st.topology,
                                         PANVK_GS_TOPO_TRI_STRIP_ADJ),
                             tsa, list)));
      total_ind = nir_umin(&b, nir_imul(&b, ppi_ind, in.inst), num_prims);
      index_buf_ind = in.index_buf;
      bias_ind = in.bias;
      stride_ind = in.stride;
      pos_ind = in.pos;
      gen_ind = in.gen;
   }
   nir_pop_if(&b, NULL);
   prims_per_instance = nir_if_phi(&b, ppi_ind, ppi_direct);
   actual_prims = nir_if_phi(&b, total_ind, num_prims);
   st.index_buf = nir_if_phi(&b, index_buf_ind, index_buf_direct);
   st.instance_stride = nir_if_phi(&b, stride_ind, instance_stride_direct);
   st.index_bias = nir_if_phi(&b, bias_ind, bias_direct);
   st.in_pos = nir_if_phi(&b, pos_ind, in_pos_direct);
   st.in_general = nir_if_phi(&b, gen_ind, in_general_direct);
   st.in_psiz = load_in_psiz(&b, opts, indirect_cmd);
   st.in_psiz_stride = P32(in_psiz_stride);
   st.out_psiz = P64(out_psiz);

   /* A count only the GPU knows (the tessellator's index count). After the
    * phis above: nir_if_phi merges the if just closed.
    */
   nir_def *count_addr = P64(num_prims_addr);
   nir_push_if(&b, nir_ine_imm(&b, count_addr, 0));
   nir_def *counted = nir_umin(
      &b, nir_load_global(&b, 1, 32, count_addr, .align_mul = 4), num_prims);
   nir_pop_if(&b, NULL);
   actual_prims = nir_if_phi(&b, counted, actual_prims);

   /* gl_PrimitiveIDIn restarts with every instance. */
   nir_def *ppi = nir_umax(&b, prims_per_instance, nir_imm_int(&b, 1));
   st.prims_per_instance = ppi;
   st.instance = nir_udiv(&b, prim_global, ppi);
   st.prim_id = nir_isub(&b, prim_global, nir_imul(&b, st.instance, ppi));

   /* Fed by a tess eval shader (tess_params set), gl_PrimitiveIDIn is the
    * patch ID, as the tess eval shader computes it: the TES draw is a
    * non-indexed list whose vertex i is tessellator index i, the raw id
    * (patch * POLY_TES_PATCH_ID_STRIDE + point) is index_buffer[i]
    * (jm-v7 049). Nothing gathered info on this NIR (system_values_read
    * is empty), so look for the intrinsic itself (jm-v7 036).
    */
   if (gs_reads_primitive_id(impl)) {
      nir_def *ordinal = st.prim_id;
      nir_push_if(&b, nir_iand(&b, nir_ine_imm(&b, st.tess_params, 0),
                               nir_ult(&b, prim_global, actual_prims)));
      nir_def *v0 = gs_input_record(&b, &st, nir_imm_int(&b, 0));
      nir_def *ib = nir_load_global(
         &b, 1, 64,
         nir_iadd_imm(&b, st.tess_params,
                      offsetof(struct poly_tess_params, index_buffer)),
         .align_mul = 8);
      nir_def *raw = nir_load_global(
         &b, 1, 32, nir_iadd(&b, ib, nir_u2u64(&b, nir_imul_imm(&b, v0, 4))),
         .align_mul = 4);
      nir_def *ppi_tess = nir_umax(
         &b,
         nir_load_global(&b, 1, 32,
                         nir_iadd_imm(&b, st.tess_params,
                                      offsetof(struct poly_tess_params,
                                               patches_per_instance)),
                         .align_mul = 4),
         nir_imm_int(&b, 1));
      nir_def *patch = nir_umod(
         &b, nir_udiv_imm(&b, raw, POLY_TES_PATCH_ID_STRIDE), ppi_tess);
      nir_pop_if(&b, NULL);
      st.api_prim_id = nir_if_phi(&b, patch, ordinal);
   }

   emit_degenerate_fill(&b, &st);

   /* Invocation 0 tells the tiler how many indices there really are (stored
    * minus one). No primitive at all still leaves one degenerate one.
    */
   nir_def *tiler_count_addr = P64(tiler_index_count);
   nir_push_if(&b, nir_iand(&b, nir_ieq_imm(&b, gid, 0),
                            nir_ine_imm(&b, tiler_count_addr, 0)));
   {
      const unsigned per_invocation =
         opts->max_output_prims * opts->output_verts_per_prim;
      nir_def *count =
         nir_imul_imm(&b, actual_prims, invocations * per_invocation);
      count = nir_umax(&b, count,
                       nir_imm_int(&b, MAX2(opts->output_verts_per_prim, 1)));
      nir_store_global(&b, nir_iadd_imm(&b, count, -1), tiler_count_addr,
                       .align_mul = 4, .write_mask = 0x1);

      /* Per-viewport tiler jobs (jm-v7 031): an array, tiler_job_stride
       * bytes apart, the first one at tiler_index_count.
       */
      if (st.viewport_var) {
         nir_def *n = st.vp_split;
         nir_def *stride = P32(tiler_job_stride);
         nir_variable *k =
            nir_local_variable_create(b.impl, glsl_uint_type(), "gs_tiler_k");
         nir_store_var(&b, k, nir_imm_int(&b, 1), 1);
         nir_push_loop(&b);
         {
            nir_def *kv = nir_load_var(&b, k);
            nir_push_if(&b, nir_uge(&b, kv, n));
            nir_jump(&b, nir_jump_break);
            nir_pop_if(&b, NULL);
            nir_store_global(
               &b, nir_iadd_imm(&b, count, -1),
               nir_iadd(&b, tiler_count_addr,
                        nir_u2u64(&b, nir_imul(&b, kv, stride))),
               .align_mul = 4, .write_mask = 0x1);
            nir_store_var(&b, k, nir_iadd_imm(&b, kv, 1), 1);
         }
         nir_pop_loop(&b, NULL);
      }
   }
   nir_pop_if(&b, NULL);
#undef P64
#undef P32

   /* Past the indirect draw's real primitive count: slots stay degenerate. */
   nir_push_if(&b, nir_uge(&b, prim_global, actual_prims));
   nir_jump(&b, nir_jump_return);
   nir_pop_if(&b, NULL);

   bool progress =
      nir_shader_intrinsics_pass(nir, lower_gs_instr, nir_metadata_none, &st);

   /* Pipeline statistics: at the end of every invocation that ran. */
   {
      nir_function_impl *end_impl = nir_shader_get_entrypoint(nir);
      nir_builder eb = nir_builder_at(nir_after_impl(end_impl));
      nir_def *table = load_param_u64(&eb, opts,
                                      offsetof(struct panvk_gs_push, pstats));
      nir_push_if(&eb, nir_ine_imm(&eb, table, 0));
      {
         nir_def *all = nir_imm_int(&eb, 0);
         u_foreach_bit(s, opts->stream_mask | 1)
            all = nir_iadd(&eb, all, nir_load_var(&eb, st.prims_written[s]));
         nir_def *rast = nir_load_var(&eb, st.prims_written[0]);
         nir_def *vals[4] = {nir_imm_int(&eb, 1), all, rast, rast};
         for (unsigned k = 0; k < 4; k++)
            gs_pstats_add(&eb, table, k, vals[k]);
      }
      nir_pop_if(&eb, NULL);
      progress = true;
   }

   /* The output variables must stop being outputs: every EmitVertex is now
    * explicit global stores, and leftover store_deref on shader_out does not
    * compile on Bifrost. Retype to temporaries and lower to SSA.
    */
   nir_foreach_variable_with_modes_safe(var, nir, nir_var_shader_out)
      var->data.mode = nir_var_shader_temp;
   nir->info.outputs_written = 0;
   nir_fixup_deref_modes(nir);
   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_opt_dce);

   /* It is a compute shader from here on. The geometry info shares a union
    * with the compute info, and the capture is explicit stores now.
    */
   nir->info.stage = MESA_SHADER_COMPUTE;
   memset(&nir->info.cs, 0, sizeof(nir->info.cs));
   nir->xfb_info = NULL;
   nir->info.has_transform_feedback_varyings = false;
   nir->info.workgroup_size[0] = PANVK_GS_WORKGROUP_SIZE;
   nir->info.workgroup_size[1] = 1;
   nir->info.workgroup_size[2] = 1;
   nir->info.workgroup_size_variable = false;

   NIR_PASS(_, nir, nir_lower_returns);
   NIR_PASS(_, nir, nir_opt_dce);

   return progress;
}

/* -- TESSELLATION, see panvk_gs.h. */

static bool
lower_tcs_instr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   struct lower_gs_state *st = data;

   b->cursor = nir_before_instr(&intr->instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_per_vertex_input:
      nir_def_replace(&intr->def, lower_per_vertex_input(b, intr, st));
      return true;
   case nir_intrinsic_barrier: {
      /* The outputs become global memory (poly_nir_lower_tcs), and a patch
       * does not fit in a Bifrost subgroup: keep workgroup scope and cover
       * global memory.
       */
      nir_variable_mode modes = nir_intrinsic_memory_modes(intr);
      if (!(modes & nir_var_shader_out))
         return false;
      nir_intrinsic_set_memory_modes(
         intr, (modes & ~nir_var_shader_out) | nir_var_mem_global);
      if (nir_intrinsic_execution_scope(intr) < SCOPE_WORKGROUP)
         nir_intrinsic_set_execution_scope(intr, SCOPE_WORKGROUP);
      if (nir_intrinsic_memory_scope(intr) < SCOPE_WORKGROUP)
         nir_intrinsic_set_memory_scope(intr, SCOPE_WORKGROUP);
      return true;
   }
   default:
      return false;
   }
}

static bool
lower_tess_params_instr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_tess_param_buffer_poly)
      return false;
   nir_def_replace(&intr->def, (nir_def *)data);
   return true;
}

bool
panvk_nir_lower_tess_params(nir_shader *nir,
                            const struct panvk_gs_lower_options *opts)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_builder b = nir_builder_at(nir_before_impl(impl));
   nir_def *p =
      load_param_u64(&b, opts, offsetof(struct panvk_gs_push, tess_params));
   return nir_shader_intrinsics_pass(nir, lower_tess_params_instr,
                                     nir_metadata_control_flow, p);
}

bool
panvk_nir_lower_tcs_inputs(nir_shader *nir,
                           const struct panvk_gs_lower_options *opts)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_builder b = nir_builder_at(nir_before_impl(impl));

   struct lower_gs_state st = {.opts = opts};

#define P64(f) load_param_u64(&b, opts, offsetof(struct panvk_gs_push, f))
#define P32(f) load_param_u32(&b, opts, offsetof(struct panvk_gs_push, f))
   nir_def *in_pos = P64(in_pos);
   nir_def *in_general = P64(in_general);
   st.in_offsets = P64(in_offsets);
   st.in_general_stride = P32(in_general_stride);
   st.vp_scale =
      load_param_vec3f(&b, opts, offsetof(struct panvk_gs_push, viewport_scale));
   st.vp_offset =
      load_param_vec3f(&b, opts, offsetof(struct panvk_gs_push, viewport_offset));
   st.in_screen_space = P32(in_screen_space);
   nir_def *index_buf = P64(index_buf);
   st.index_size = P32(index_size);
   st.topology = nir_imm_int(&b, PANVK_GS_TOPO_LIST);
   nir_def *instance_stride = P32(instance_stride);
   st.prims_per_instance = P32(prims_per_instance);
   st.vpp_dyn = P32(patch_size);

   /* An indirect draw, read as panvk_nir_lower_gs reads one. */
   nir_def *indirect_cmd = P64(indirect_cmd);
   nir_def *bias_direct = load_direct_bias(&b, opts, st.index_size);
   nir_push_if(&b, nir_ine_imm(&b, indirect_cmd, 0));
   const struct indirect_inputs in =
      load_indirect_inputs(&b, opts, indirect_cmd, index_buf, st.index_size);
   nir_pop_if(&b, NULL);
   st.index_buf = nir_if_phi(&b, in.index_buf, index_buf);
   st.index_bias = nir_if_phi(&b, in.bias, bias_direct);
   st.instance_stride = nir_if_phi(&b, in.stride, instance_stride);
   st.in_pos = nir_if_phi(&b, in.pos, in_pos);
   st.in_general = nir_if_phi(&b, in.gen, in_general);
   st.in_psiz = load_in_psiz(&b, opts, indirect_cmd);
   st.in_psiz_stride = P32(in_psiz_stride);
   st.tess_params = P64(tess_params);
#undef P64
#undef P32

   /* One workgroup per patch: x the patch within its instance, y the
    * instance.
    */
   nir_def *wg = nir_load_workgroup_id(&b);
   st.prim_id = nir_channel(&b, wg, 0);
   st.instance = nir_channel(&b, wg, 1);

   return nir_shader_intrinsics_pass(nir, lower_tcs_instr, nir_metadata_none,
                                     &st);
}
