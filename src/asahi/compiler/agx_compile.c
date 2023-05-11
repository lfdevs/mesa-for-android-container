/*
 * Copyright 2021 Alyssa Rosenzweig
 * Copyright 2020 Collabora Ltd.
 * Copyright 2016 Broadcom
 * SPDX-License-Identifier: MIT
 */

#include "agx_compile.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir_types.h"
#include "util/glheader.h"
#include "util/u_debug.h"
#include "agx_builder.h"
#include "agx_compiler.h"
#include "agx_debug.h"
#include "agx_internal_formats.h"
#include "agx_nir.h"

/* Alignment for shader programs. I'm not sure what the optimal value is. */
#define AGX_CODE_ALIGN 0x100

/* clang-format off */
static const struct debug_named_value agx_debug_options[] = {
   {"msgs",      AGX_DBG_MSGS,		"Print debug messages"},
   {"shaders",   AGX_DBG_SHADERS,	"Dump shaders in NIR and AIR"},
   {"shaderdb",  AGX_DBG_SHADERDB,	"Print statistics"},
   {"verbose",   AGX_DBG_VERBOSE,	"Disassemble verbosely"},
   {"internal",  AGX_DBG_INTERNAL,	"Dump even internal shaders"},
   {"novalidate",AGX_DBG_NOVALIDATE,"Skip IR validation in debug builds"},
   {"noopt",     AGX_DBG_NOOPT,     "Disable backend optimizations"},
   {"wait",      AGX_DBG_WAIT,      "Wait after all async instructions"},
   {"nopreamble",AGX_DBG_NOPREAMBLE,"Do not use shader preambles"},
   DEBUG_NAMED_VALUE_END
};
/* clang-format on */

DEBUG_GET_ONCE_FLAGS_OPTION(agx_compiler_debug, "AGX_MESA_DEBUG",
                            agx_debug_options, 0)

int agx_compiler_debug = 0;

uint64_t
agx_get_compiler_debug(void)
{
   return debug_get_option_agx_compiler_debug();
}

#define DBG(fmt, ...)                                                          \
   do {                                                                        \
      if (agx_compiler_debug & AGX_DBG_MSGS)                                   \
         fprintf(stderr, "%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__);    \
   } while (0)

static agx_index
agx_cached_preload(agx_context *ctx, agx_index *cache, unsigned base,
                   enum agx_size size)
{
   if (agx_is_null(*cache)) {
      agx_block *block = agx_start_block(ctx);
      agx_builder b = agx_init_builder(ctx, agx_before_block(block));
      *cache = agx_preload(&b, agx_register(base, size));
   }

   return *cache;
}

static agx_index
agx_vertex_id(agx_builder *b)
{
   return agx_cached_preload(b->shader, &b->shader->vertex_id, 10, AGX_SIZE_32);
}

static agx_index
agx_instance_id(agx_builder *b)
{
   return agx_cached_preload(b->shader, &b->shader->instance_id, 12,
                             AGX_SIZE_32);
}

static agx_index
agx_get_cf(agx_context *ctx, bool smooth, bool perspective,
           gl_varying_slot slot, unsigned offset, unsigned count)
{
   struct agx_varyings_fs *varyings = &ctx->out->varyings.fs;
   unsigned cf_base = varyings->nr_cf;

   if (slot == VARYING_SLOT_POS) {
      assert(offset == 2 || offset == 3);
      varyings->reads_z |= (offset == 2);
   }

   /* First, search for an appropriate binding. This is O(n) to the number of
    * bindings, which isn't great, but n should be small in practice.
    */
   for (unsigned b = 0; b < varyings->nr_bindings; ++b) {
      if ((varyings->bindings[b].slot == slot) &&
          (varyings->bindings[b].offset == offset) &&
          (varyings->bindings[b].count == count) &&
          (varyings->bindings[b].smooth == smooth) &&
          (varyings->bindings[b].perspective == perspective)) {

         return agx_immediate(varyings->bindings[b].cf_base);
      }
   }

   /* If we didn't find one, make one */
   unsigned b = varyings->nr_bindings++;
   varyings->bindings[b].cf_base = varyings->nr_cf;
   varyings->bindings[b].slot = slot;
   varyings->bindings[b].offset = offset;
   varyings->bindings[b].count = count;
   varyings->bindings[b].smooth = smooth;
   varyings->bindings[b].perspective = perspective;
   varyings->nr_cf += count;

   return agx_immediate(cf_base);
}

/* Builds a 64-bit hash table key for an index */
static uint64_t
agx_index_to_key(agx_index idx)
{
   STATIC_ASSERT(sizeof(idx) <= sizeof(uint64_t));

   uint64_t key = 0;
   memcpy(&key, &idx, sizeof(idx));
   return key;
}

/*
 * Extract a single channel out of a vector source. We split vectors with
 * p_split so we can use the split components directly, without emitting a
 * machine instruction. This has advantages of RA, as the split can usually be
 * optimized away.
 */
static agx_index
agx_emit_extract(agx_builder *b, agx_index vec, unsigned channel)
{
   agx_index *components = _mesa_hash_table_u64_search(b->shader->allocated_vec,
                                                       agx_index_to_key(vec));

   assert(components != NULL && "missing agx_emit_collect_to");

   return components[channel];
}

static agx_index
agx_extract_nir_src(agx_builder *b, nir_src src, unsigned channel)
{
   agx_index idx = agx_src_index(&src);

   /* We only deal with scalars, extract a single scalar if needed */
   if (nir_src_num_components(src) > 1)
      return agx_emit_extract(b, idx, channel);
   else
      return idx;
}

static void
agx_cache_collect(agx_builder *b, agx_index dst, unsigned nr_srcs,
                  agx_index *srcs)
{
   /* Lifetime of a hash table entry has to be at least as long as the table */
   agx_index *channels = ralloc_array(b->shader, agx_index, nr_srcs);

   for (unsigned i = 0; i < nr_srcs; ++i)
      channels[i] = srcs[i];

   _mesa_hash_table_u64_insert(b->shader->allocated_vec, agx_index_to_key(dst),
                               channels);
}

/*
 * Combine multiple scalars into a vector destination. This corresponds to
 * collect, lowered to moves (a shuffle in general) after register allocation.
 *
 * To optimize vector extractions, we record the individual channels
 */
static agx_instr *
agx_emit_collect_to(agx_builder *b, agx_index dst, unsigned nr_srcs,
                    agx_index *srcs)
{
   agx_cache_collect(b, dst, nr_srcs, srcs);

   if (nr_srcs == 1)
      return agx_mov_to(b, dst, srcs[0]);

   agx_instr *I = agx_collect_to(b, dst, nr_srcs);

   agx_foreach_src(I, s)
      I->src[s] = srcs[s];

   return I;
}

static agx_index
agx_emit_collect(agx_builder *b, unsigned nr_srcs, agx_index *srcs)
{
   agx_index dst = agx_temp(b->shader, srcs[0].size);
   agx_emit_collect_to(b, dst, nr_srcs, srcs);
   return dst;
}

static agx_index
agx_vec2(agx_builder *b, agx_index s0, agx_index s1)
{
   return agx_emit_collect(b, 2, (agx_index[]){s0, s1});
}

/*
 * Extract the lower or upper N-bits from a (2*N)-bit quantity. We use a split
 * without null destinations to let us CSE (and coalesce) the splits when both x
 * and y are split.
 */
static agx_instr *
agx_subdivide_to(agx_builder *b, agx_index dst, agx_index s0, unsigned comp)
{
   assert((s0.size == (dst.size + 1)) && "only 2x subdivide handled");
   assert((comp == 0 || comp == 1) && "too many components");

   agx_instr *split = agx_split(b, 2, s0);
   split->dest[comp] = dst;
   split->dest[1 - comp] = agx_temp(b->shader, dst.size);
   return split;
}

static void
agx_block_add_successor(agx_block *block, agx_block *successor)
{
   assert(block != NULL && successor != NULL);

   /* Cull impossible edges */
   if (block->unconditional_jumps)
      return;

   for (unsigned i = 0; i < ARRAY_SIZE(block->successors); ++i) {
      if (block->successors[i]) {
         if (block->successors[i] == successor)
            return;
         else
            continue;
      }

      block->successors[i] = successor;
      util_dynarray_append(&successor->predecessors, agx_block *, block);
      return;
   }

   unreachable("Too many successors");
}

/*
 * Splits an n-component vector (vec) into n scalar destinations (dests) using a
 * split pseudo-instruction.
 *
 * Pre-condition: dests is filled with agx_null().
 */
static void
agx_emit_split(agx_builder *b, agx_index *dests, agx_index vec, unsigned n)
{
   agx_instr *I = agx_split(b, n, vec);

   agx_foreach_dest(I, d) {
      dests[d] = agx_temp(b->shader, vec.size);
      I->dest[d] = dests[d];
   }
}

static void
agx_emit_cached_split(agx_builder *b, agx_index vec, unsigned n)
{
   agx_index dests[4] = {agx_null(), agx_null(), agx_null(), agx_null()};
   agx_emit_split(b, dests, vec, n);
   agx_cache_collect(b, vec, n, dests);
}

static void
agx_emit_load_const(agx_builder *b, nir_load_const_instr *instr)
{
   /* Ensure we've been scalarized and bit size lowered */
   unsigned bit_size = instr->def.bit_size;
   assert(instr->def.num_components == 1);

   /* Emit move, later passes can inline/push if useful */
   agx_mov_imm_to(b, agx_nir_ssa_index(&instr->def),
                  nir_const_value_as_uint(instr->value[0], bit_size));
}

/*
 * Implement umul_high of 32-bit sources by doing a 32x32->64-bit multiply and
 * extracting only the high word.
 */
static agx_instr *
agx_umul_high_to(agx_builder *b, agx_index dst, agx_index P, agx_index Q)
{
   assert(P.size == Q.size && "source sizes must match");
   assert(P.size == dst.size && "dest size must match");
   assert(P.size != AGX_SIZE_64 && "64x64 multiply should have been lowered");

   static_assert(AGX_SIZE_64 == (AGX_SIZE_32 + 1), "enum wrong");
   static_assert(AGX_SIZE_32 == (AGX_SIZE_16 + 1), "enum wrong");

   agx_index product = agx_temp(b->shader, P.size + 1);
   agx_imad_to(b, product, agx_abs(P), agx_abs(Q), agx_zero(), 0);

   return agx_subdivide_to(b, dst, product, 1);
}

static enum agx_format
agx_format_for_pipe(enum pipe_format format)
{
#define CASE(x)                                                                \
   if (format == (enum pipe_format)AGX_INTERNAL_FORMAT_##x)                    \
      return AGX_FORMAT_##x;

   CASE(I8);
   CASE(I16);
   CASE(I32);
   CASE(F16);
   CASE(U8NORM);
   CASE(S8NORM);
   CASE(U16NORM);
   CASE(S16NORM);
   CASE(RGB10A2);
   CASE(SRGBA8);
   CASE(RG11B10F);
   CASE(RGB9E5);

#undef CASE
   unreachable("Invalid format");
}

static void
agx_emit_load_vary_flat(agx_builder *b, agx_index dest,
                        nir_intrinsic_instr *instr)
{
   unsigned components = instr->num_components;
   assert(components >= 1 && components <= 4);

   nir_io_semantics sem = nir_intrinsic_io_semantics(instr);
   nir_src *offset = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset) && "no indirects");
   assert(nir_dest_bit_size(instr->dest) == 32 && "no 16-bit flat shading");

   /* Get all coefficient registers up front. This ensures the driver emits a
    * single vectorized binding.
    */
   agx_index cf = agx_get_cf(b->shader, false, false,
                             sem.location + nir_src_as_uint(*offset),
                             nir_intrinsic_component(instr), components);
   agx_index dests[4] = {agx_null()};

   for (unsigned i = 0; i < components; ++i) {
      /* vec3 for each vertex, unknown what first 2 channels are for */
      agx_index d[3] = {agx_null()};
      agx_index tmp = agx_temp(b->shader, AGX_SIZE_32);
      agx_ldcf_to(b, tmp, cf, 1);
      agx_emit_split(b, d, tmp, 3);
      dests[i] = d[2];

      /* Each component accesses a sequential coefficient register */
      cf.value++;
   }

   agx_emit_collect_to(b, dest, components, dests);
}

static void
agx_emit_load_vary(agx_builder *b, agx_index dest, nir_intrinsic_instr *instr)
{
   ASSERTED unsigned components = instr->num_components;
   nir_intrinsic_instr *bary = nir_src_as_intrinsic(instr->src[0]);

   assert(components >= 1 && components <= 4);

   /* TODO: Interpolation modes */
   assert(bary != NULL);
   assert(bary->intrinsic == nir_intrinsic_load_barycentric_pixel);

   bool perspective =
      nir_intrinsic_interp_mode(bary) != INTERP_MODE_NOPERSPECTIVE;

   nir_io_semantics sem = nir_intrinsic_io_semantics(instr);
   nir_src *offset = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset) && "no indirects");

   assert(nir_ssa_def_components_read(&instr->dest.ssa) ==
             nir_component_mask(components) &&
          "iter does not handle write-after-write hazards");

   /* For perspective interpolation, we need W */
   agx_index J =
      !perspective ? agx_zero()
                   : agx_get_cf(b->shader, true, false, VARYING_SLOT_POS, 3, 1);

   agx_index I = agx_get_cf(b->shader, true, perspective,
                            sem.location + nir_src_as_uint(*offset),
                            nir_intrinsic_component(instr), components);

   agx_iter_to(b, dest, I, J, components, perspective);
   agx_emit_cached_split(b, dest, components);
}

static agx_instr *
agx_emit_store_vary(agx_builder *b, nir_intrinsic_instr *instr)
{
   nir_io_semantics sem = nir_intrinsic_io_semantics(instr);
   nir_src *offset = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset) && "todo: indirects");

   unsigned imm_index = b->shader->out->varyings.vs.slots[sem.location];
   assert(imm_index < ~0);
   imm_index += (nir_src_as_uint(*offset) * 4) + nir_intrinsic_component(instr);

   /* nir_lower_io_to_scalar */
   assert(nir_intrinsic_write_mask(instr) == 0x1);

   return agx_st_vary(b, agx_immediate(imm_index),
                      agx_src_index(&instr->src[0]));
}

static void
agx_write_sample_mask_1(agx_builder *b)
{
   if (b->shader->nir->info.fs.uses_discard && !b->shader->did_sample_mask) {
      /* If the shader uses discard, the sample mask must be written by the
       * shader on all execution paths. If we've reached the end of the shader,
       * we are therefore still active and need to write a full sample mask.
       * TODO: interactions with MSAA and gl_SampleMask writes
       */
      agx_sample_mask(b, agx_immediate(1));
      agx_signal_pix(b, 1);
      b->shader->did_sample_mask = true;

      assert(!(b->shader->nir->info.outputs_written &
               (BITFIELD64_BIT(FRAG_RESULT_DEPTH) |
                BITFIELD64_BIT(FRAG_RESULT_STENCIL))) &&
             "incompatible");
   }
}

static agx_instr *
agx_emit_local_store_pixel(agx_builder *b, nir_intrinsic_instr *instr)
{
   /* TODO: Reverse-engineer interactions with MRT */
   if (b->shader->key->fs.ignore_tib_dependencies) {
      assert(b->shader->nir->info.internal && "only for clear shaders");
   } else if (b->shader->did_writeout) {
      agx_wait_pix(b, 0x0004);
   } else {
      agx_wait_pix(b, 0x000C);
   }

   agx_write_sample_mask_1(b);

   /* Compact the registers according to the mask */
   agx_index compacted[4] = {agx_null()};

   unsigned compact_count = 0;
   u_foreach_bit(i, nir_intrinsic_write_mask(instr)) {
      compacted[compact_count++] = agx_extract_nir_src(b, instr->src[0], i);
   }

   agx_index collected = agx_emit_collect(b, compact_count, compacted);

   b->shader->did_writeout = true;
   return agx_st_tile(b, collected, agx_src_index(&instr->src[1]),
                      agx_format_for_pipe(nir_intrinsic_format(instr)),
                      nir_intrinsic_write_mask(instr),
                      nir_intrinsic_base(instr));
}

static agx_instr *
agx_emit_store_zs(agx_builder *b, nir_intrinsic_instr *instr)
{
   unsigned base = nir_intrinsic_base(instr);
   bool write_z = base & 1;
   bool write_s = base & 2;

   /* TODO: Handle better */
   assert(!b->shader->key->fs.ignore_tib_dependencies && "not used");
   agx_wait_pix(b, 0x0001);

   agx_index z = agx_src_index(&instr->src[1]);
   agx_index s = agx_src_index(&instr->src[2]);

   agx_index zs = (write_z && write_s) ? agx_vec2(b, z, s) : write_z ? z : s;

   /* Not necessarily a sample mask but overlapping hw mechanism... Should
    * maybe rename this flag to something more general.
    */
   b->shader->out->writes_sample_mask = true;
   assert(!b->shader->did_sample_mask && "incompatible");

   return agx_zs_emit(b, agx_src_index(&instr->src[0]), zs, base);
}

static void
agx_emit_local_load_pixel(agx_builder *b, agx_index dest,
                          nir_intrinsic_instr *instr)
{
   /* TODO: Reverse-engineer interactions with MRT */
   assert(!b->shader->key->fs.ignore_tib_dependencies && "invalid usage");
   agx_wait_pix(b, 0x0008);
   b->shader->did_writeout = true;
   b->shader->out->reads_tib = true;

   unsigned nr_comps = nir_dest_num_components(instr->dest);
   agx_ld_tile_to(b, dest, agx_src_index(&instr->src[0]),
                  agx_format_for_pipe(nir_intrinsic_format(instr)),
                  BITFIELD_MASK(nr_comps), nir_intrinsic_base(instr));
   agx_emit_cached_split(b, dest, nr_comps);
}

static void
agx_emit_load(agx_builder *b, agx_index dest, nir_intrinsic_instr *instr)
{
   agx_index addr = agx_src_index(&instr->src[0]);
   agx_index offset = agx_src_index(&instr->src[1]);
   enum agx_format fmt = agx_format_for_pipe(nir_intrinsic_format(instr));
   unsigned shift = nir_intrinsic_base(instr);

   /* Zero-extend offset if we're not sign-extending */
   if (!nir_intrinsic_sign_extend(instr))
      offset = agx_abs(offset);

   agx_device_load_to(b, dest, addr, offset, fmt,
                      BITFIELD_MASK(nir_dest_num_components(instr->dest)),
                      shift, 0);
   agx_emit_cached_split(b, dest, nir_dest_num_components(instr->dest));
}

static void
agx_emit_store(agx_builder *b, nir_intrinsic_instr *instr)
{
   agx_index value = agx_src_index(&instr->src[0]);
   agx_index addr = agx_src_index(&instr->src[1]);
   agx_index offset = agx_src_index(&instr->src[2]);
   enum agx_format fmt = agx_format_for_pipe(nir_intrinsic_format(instr));
   unsigned shift = nir_intrinsic_base(instr);

   /* Zero-extend offset if we're not sign-extending */
   if (!nir_intrinsic_sign_extend(instr))
      offset = agx_abs(offset);

   agx_device_store(b, value, addr, offset, fmt,
                    BITFIELD_MASK(nir_src_num_components(instr->src[0])), shift,
                    0);
}

/* Preambles write directly to uniform registers, so move from uniform to GPR */
static agx_instr *
agx_emit_load_preamble(agx_builder *b, agx_index dst,
                       nir_intrinsic_instr *instr)
{
   agx_index srcs[4] = {agx_null()};
   unsigned dim = nir_dest_num_components(instr->dest);
   assert(dim <= ARRAY_SIZE(srcs) && "shouldn't see larger vectors");

   unsigned base = nir_intrinsic_base(instr);
   unsigned stride = agx_size_align_16(dst.size);

   for (unsigned i = 0; i < dim; ++i)
      srcs[i] = agx_uniform(base + i * stride, dst.size);

   return agx_emit_collect_to(b, dst, dim, srcs);
}

static agx_instr *
agx_emit_store_preamble(agx_builder *b, nir_intrinsic_instr *instr)
{
   agx_index vec = agx_src_index(&instr->src[0]);
   unsigned base = nir_intrinsic_base(instr);
   unsigned stride = agx_size_align_16(vec.size);

   for (unsigned i = 0; i < nir_src_num_components(instr->src[0]); ++i) {
      agx_uniform_store(b, agx_extract_nir_src(b, instr->src[0], i),
                        agx_immediate(base + i * stride));
   }

   return NULL;
}

static enum agx_dim
agx_tex_dim(enum glsl_sampler_dim dim, bool array)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_1D:
      return array ? AGX_DIM_1D_ARRAY : AGX_DIM_1D;

   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_EXTERNAL:
      return array ? AGX_DIM_2D_ARRAY : AGX_DIM_2D;

   case GLSL_SAMPLER_DIM_MS:
      return array ? AGX_DIM_2D_MS_ARRAY : AGX_DIM_2D_MS;

   case GLSL_SAMPLER_DIM_3D:
      assert(!array && "3D arrays unsupported");
      return AGX_DIM_3D;

   case GLSL_SAMPLER_DIM_CUBE:
      return array ? AGX_DIM_CUBE_ARRAY : AGX_DIM_CUBE;

   case GLSL_SAMPLER_DIM_BUF:
      unreachable("Buffer textures should have been lowered");

   default:
      unreachable("Invalid sampler dim\n");
   }
}

static agx_instr *
agx_emit_block_image_store(agx_builder *b, nir_intrinsic_instr *instr)
{
   unsigned image = nir_src_as_uint(instr->src[0]);
   agx_index offset = agx_src_index(&instr->src[1]);
   enum agx_format format = agx_format_for_pipe(nir_intrinsic_format(instr));
   enum agx_dim dim = agx_tex_dim(nir_intrinsic_image_dim(instr), false);

   // XXX: how does this possibly work
   if (format == AGX_FORMAT_F16)
      format = AGX_FORMAT_I16;

   return agx_block_image_store(b, agx_immediate(image), offset, format, dim);
}

/*
 * Emit code to generate gl_FragCoord. The xy components are calculated from
 * special registers, whereas the zw components are interpolated varyings.
 * Because interpolating varyings requires allocating coefficient registers that
 * might not be used, we only emit code for components that are actually used.
 */
static void
agx_emit_load_frag_coord(agx_builder *b, agx_index dst,
                         nir_intrinsic_instr *instr)
{
   agx_index dests[4] = {agx_null()};

   u_foreach_bit(i, nir_ssa_def_components_read(&instr->dest.ssa)) {
      agx_index fp32 = agx_temp(b->shader, AGX_SIZE_32);

      if (i < 2) {
         agx_convert_to(b, fp32, agx_immediate(AGX_CONVERT_U32_TO_F),
                        agx_get_sr(b, 32, AGX_SR_THREAD_POSITION_IN_GRID_X + i),
                        AGX_ROUND_RTE);

         dests[i] = agx_fadd(b, fp32, agx_immediate_f(0.5f));
      } else {
         agx_index cf =
            agx_get_cf(b->shader, true, false, VARYING_SLOT_POS, i, 1);

         dests[i] = fp32;
         agx_iter_to(b, fp32, cf, agx_null(), 1, false);
      }
   }

   agx_emit_collect_to(b, dst, 4, dests);
}

/*
 * Demoting a helper invocation is logically equivalent to zeroing the sample
 * mask. Metal implement discard as such.
 *
 * XXX: Actually, Metal's "discard" is a demote, and what is implemented here
 * is a demote. There might be a better way to implement this to get correct
 * helper invocation semantics. For now, I'm kicking the can down the road.
 */
static agx_instr *
agx_emit_discard(agx_builder *b)
{
   assert(!b->shader->key->fs.ignore_tib_dependencies && "invalid usage");
   agx_wait_pix(b, 0x0001);
   b->shader->did_writeout = true;

   b->shader->out->writes_sample_mask = true;
   agx_sample_mask(b, agx_immediate(0));
   return agx_signal_pix(b, 1);
}

static agx_instr *
agx_load_compute_dimension(agx_builder *b, agx_index dst,
                           nir_intrinsic_instr *instr, enum agx_sr base)
{
   unsigned dim = nir_dest_num_components(instr->dest);
   unsigned size = nir_dest_bit_size(instr->dest);
   assert(size == 16 || size == 32);

   agx_index srcs[] = {
      agx_get_sr(b, size, base + 0),
      agx_get_sr(b, size, base + 1),
      agx_get_sr(b, size, base + 2),
   };

   return agx_emit_collect_to(b, dst, dim, srcs);
}

static enum agx_atomic_opc
translate_atomic_opcode(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_global_atomic_add:
   case nir_intrinsic_shared_atomic_add:
      return AGX_ATOMIC_OPC_ADD;
   case nir_intrinsic_global_atomic_imin:
   case nir_intrinsic_shared_atomic_imin:
      return AGX_ATOMIC_OPC_IMIN;
   case nir_intrinsic_global_atomic_umin:
   case nir_intrinsic_shared_atomic_umin:
      return AGX_ATOMIC_OPC_UMIN;
   case nir_intrinsic_global_atomic_imax:
   case nir_intrinsic_shared_atomic_imax:
      return AGX_ATOMIC_OPC_IMAX;
   case nir_intrinsic_global_atomic_umax:
   case nir_intrinsic_shared_atomic_umax:
      return AGX_ATOMIC_OPC_UMAX;
   case nir_intrinsic_global_atomic_and:
   case nir_intrinsic_shared_atomic_and:
      return AGX_ATOMIC_OPC_AND;
   case nir_intrinsic_global_atomic_or:
   case nir_intrinsic_shared_atomic_or:
      return AGX_ATOMIC_OPC_OR;
   case nir_intrinsic_global_atomic_xor:
   case nir_intrinsic_shared_atomic_xor:
      return AGX_ATOMIC_OPC_XOR;
   case nir_intrinsic_global_atomic_exchange:
   case nir_intrinsic_shared_atomic_exchange:
      return AGX_ATOMIC_OPC_XCHG;
   case nir_intrinsic_global_atomic_comp_swap:
   case nir_intrinsic_shared_atomic_comp_swap:
      return AGX_ATOMIC_OPC_CMPXCHG;
   default:
      unreachable("unknown atomic intrinsic");
   }
}

/*
 * The "base" of a local load/store/atomic can be zero but no other immediates.
 * This would be a little silly to handle when inlining immediates, so we
 * instead exclude these ops from immediate inlining and just handle 0 specially
 * when translating.
 */
static agx_index
agx_local_base(nir_src src)
{
   if (nir_src_is_const(src) && nir_src_as_uint(src) == 0)
      return agx_zero();
   else
      return agx_src_index(&src);
}

static void
agx_emit_atomic(agx_builder *b, agx_index dst, nir_intrinsic_instr *instr,
                bool local)
{
   enum agx_atomic_opc op = translate_atomic_opcode(instr->intrinsic);
   agx_index base =
      local ? agx_local_base(instr->src[0]) : agx_src_index(&instr->src[0]);
   agx_index value = agx_src_index(&instr->src[1]);
   agx_index index = agx_zero(); /* TODO: optimize address arithmetic? */

   /* cmpxchg (only) takes 2 sources, passed in consecutive registers */
   if (op == AGX_ATOMIC_OPC_CMPXCHG) {
      agx_index value2 = agx_src_index(&instr->src[2]);
      value = agx_vec2(b, value2, value);
   }

   if (local) {
      assert(base.size == AGX_SIZE_16);
      agx_local_atomic_to(b, dst, value, base, index, op);
   } else {
      assert(base.size == AGX_SIZE_64);
      agx_atomic_to(b, dst, value, base, index, op, 0);
   }
}

static void
agx_emit_local_load(agx_builder *b, agx_index dst, nir_intrinsic_instr *instr)
{
   agx_index base = agx_local_base(instr->src[0]);
   agx_index index = agx_zero(); /* TODO: optimize address arithmetic */
   assert(base.size == AGX_SIZE_16);

   assert(nir_dest_bit_size(instr->dest) == 32 && "todo");
   enum agx_format format = AGX_FORMAT_I32;

   unsigned nr = nir_dest_num_components(instr->dest);
   unsigned mask = BITFIELD_MASK(nr);

   agx_local_load_to(b, dst, base, index, format, mask);
   agx_emit_cached_split(b, dst, nr);
}

static void
agx_emit_local_store(agx_builder *b, nir_intrinsic_instr *instr)
{
   agx_index value = agx_src_index(&instr->src[0]);
   agx_index base = agx_local_base(instr->src[1]);
   agx_index index = agx_zero(); /* TODO: optimize address arithmetic */
   assert(base.size == AGX_SIZE_16);

   assert(nir_src_bit_size(instr->src[0]) == 32 && "todo");
   enum agx_format format = AGX_FORMAT_I32;
   unsigned mask = BITFIELD_MASK(
      nir_src_num_components(instr->src[0])); /* XXX: there's a write mask */

   agx_local_store(b, value, base, index, format, mask);
}

static agx_instr *
agx_emit_intrinsic(agx_builder *b, nir_intrinsic_instr *instr)
{
   agx_index dst = nir_intrinsic_infos[instr->intrinsic].has_dest
                      ? agx_dest_index(&instr->dest)
                      : agx_null();
   gl_shader_stage stage = b->shader->stage;

   switch (instr->intrinsic) {
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_at_sample:
   case nir_intrinsic_load_barycentric_at_offset:
      /* handled later via load_vary */
      return NULL;
   case nir_intrinsic_load_interpolated_input:
      assert(stage == MESA_SHADER_FRAGMENT);
      agx_emit_load_vary(b, dst, instr);
      return NULL;

   case nir_intrinsic_load_input:
      assert(stage == MESA_SHADER_FRAGMENT && "vertex loads lowered");
      agx_emit_load_vary_flat(b, dst, instr);
      return NULL;

   case nir_intrinsic_load_agx:
   case nir_intrinsic_load_constant_agx:
      agx_emit_load(b, dst, instr);
      return NULL;

   case nir_intrinsic_store_output:
      assert(stage == MESA_SHADER_VERTEX);
      return agx_emit_store_vary(b, instr);

   case nir_intrinsic_store_agx:
      agx_emit_store(b, instr);
      return NULL;

   case nir_intrinsic_store_shared:
      agx_emit_local_store(b, instr);
      return NULL;

   case nir_intrinsic_load_shared:
      agx_emit_local_load(b, dst, instr);
      return NULL;

   case nir_intrinsic_global_atomic_add:
   case nir_intrinsic_global_atomic_imin:
   case nir_intrinsic_global_atomic_umin:
   case nir_intrinsic_global_atomic_imax:
   case nir_intrinsic_global_atomic_umax:
   case nir_intrinsic_global_atomic_and:
   case nir_intrinsic_global_atomic_or:
   case nir_intrinsic_global_atomic_xor:
   case nir_intrinsic_global_atomic_exchange:
   case nir_intrinsic_global_atomic_comp_swap:
      agx_emit_atomic(b, dst, instr, false);
      return NULL;

   case nir_intrinsic_shared_atomic_add:
   case nir_intrinsic_shared_atomic_imin:
   case nir_intrinsic_shared_atomic_umin:
   case nir_intrinsic_shared_atomic_imax:
   case nir_intrinsic_shared_atomic_umax:
   case nir_intrinsic_shared_atomic_and:
   case nir_intrinsic_shared_atomic_or:
   case nir_intrinsic_shared_atomic_xor:
   case nir_intrinsic_shared_atomic_exchange:
   case nir_intrinsic_shared_atomic_comp_swap:
      agx_emit_atomic(b, dst, instr, true);
      return NULL;

   case nir_intrinsic_store_zs_agx:
      assert(stage == MESA_SHADER_FRAGMENT);
      return agx_emit_store_zs(b, instr);

   case nir_intrinsic_store_local_pixel_agx:
      assert(stage == MESA_SHADER_FRAGMENT);
      return agx_emit_local_store_pixel(b, instr);

   case nir_intrinsic_load_local_pixel_agx:
      assert(stage == MESA_SHADER_FRAGMENT);
      agx_emit_local_load_pixel(b, dst, instr);
      return NULL;

   case nir_intrinsic_load_frag_coord:
      agx_emit_load_frag_coord(b, dst, instr);
      return NULL;

   case nir_intrinsic_discard:
      return agx_emit_discard(b);

   case nir_intrinsic_load_back_face_agx:
      return agx_get_sr_to(b, dst, AGX_SR_BACKFACING);

   case nir_intrinsic_load_helper_invocation:
      /* Compare special register to zero. We could lower this in NIR (letting
       * us fold in an inot) but meh?
       */
      return agx_icmpsel_to(b, dst, agx_get_sr(b, 32, AGX_SR_IS_ACTIVE_THREAD),
                            agx_zero(), agx_immediate(1), agx_zero(),
                            AGX_ICOND_UEQ);

   case nir_intrinsic_load_vertex_id:
      return agx_mov_to(b, dst, agx_abs(agx_vertex_id(b)));

   case nir_intrinsic_load_instance_id:
      return agx_mov_to(b, dst, agx_abs(agx_instance_id(b)));

   case nir_intrinsic_load_preamble:
      return agx_emit_load_preamble(b, dst, instr);

   case nir_intrinsic_store_preamble:
      return agx_emit_store_preamble(b, instr);

   case nir_intrinsic_block_image_store_agx:
      return agx_emit_block_image_store(b, instr);

   case nir_intrinsic_load_workgroup_id:
      return agx_load_compute_dimension(b, dst, instr,
                                        AGX_SR_THREADGROUP_POSITION_IN_GRID_X);

   case nir_intrinsic_load_global_invocation_id:
      return agx_load_compute_dimension(b, dst, instr,
                                        AGX_SR_THREAD_POSITION_IN_GRID_X);

   case nir_intrinsic_load_local_invocation_id:
      return agx_load_compute_dimension(
         b, dst, instr, AGX_SR_THREAD_POSITION_IN_THREADGROUP_X);

   case nir_intrinsic_scoped_barrier: {
      bool needs_threadgroup_barrier = false;

      if (nir_intrinsic_execution_scope(instr) != NIR_SCOPE_NONE) {
         assert(nir_intrinsic_execution_scope(instr) > NIR_SCOPE_SUBGROUP &&
                "todo: subgroup barriers");

         needs_threadgroup_barrier = true;
      }

      if (nir_intrinsic_memory_scope(instr) != NIR_SCOPE_NONE) {
         nir_variable_mode modes = nir_intrinsic_memory_modes(instr);

         if (modes & nir_var_mem_global)
            agx_memory_barrier(b);

         if (modes & nir_var_mem_shared)
            needs_threadgroup_barrier = true;

         if (nir_intrinsic_memory_scope(instr) >= NIR_SCOPE_WORKGROUP)
            needs_threadgroup_barrier = true;
      }

      if (needs_threadgroup_barrier)
         agx_threadgroup_barrier(b);

      return NULL;
   }

   default:
      fprintf(stderr, "Unhandled intrinsic %s\n",
              nir_intrinsic_infos[instr->intrinsic].name);
      unreachable("Unhandled intrinsic");
   }
}

static agx_index
agx_alu_src_index(agx_builder *b, nir_alu_src src)
{
   /* Check well-formedness of the input NIR */
   ASSERTED unsigned bitsize = nir_src_bit_size(src.src);
   unsigned comps = nir_src_num_components(src.src);
   unsigned channel = src.swizzle[0];

   assert(bitsize == 1 || bitsize == 8 || bitsize == 16 || bitsize == 32 ||
          bitsize == 64);
   assert(!(src.negate || src.abs));
   assert(channel < comps);

   return agx_extract_nir_src(b, src.src, channel);
}

static agx_instr *
agx_emit_alu(agx_builder *b, nir_alu_instr *instr)
{
   unsigned srcs = nir_op_infos[instr->op].num_inputs;
   unsigned sz = nir_dest_bit_size(instr->dest.dest);
   unsigned src_sz = srcs ? nir_src_bit_size(instr->src[0].src) : 0;
   ASSERTED unsigned comps = nir_dest_num_components(instr->dest.dest);

   assert(comps == 1 || nir_op_is_vec(instr->op));
   assert(sz == 1 || sz == 16 || sz == 32 || sz == 64);

   agx_index dst = agx_dest_index(&instr->dest.dest);
   agx_index s0 = srcs > 0 ? agx_alu_src_index(b, instr->src[0]) : agx_null();
   agx_index s1 = srcs > 1 ? agx_alu_src_index(b, instr->src[1]) : agx_null();
   agx_index s2 = srcs > 2 ? agx_alu_src_index(b, instr->src[2]) : agx_null();
   agx_index s3 = srcs > 3 ? agx_alu_src_index(b, instr->src[3]) : agx_null();

   agx_index i0 = agx_immediate(0);
   agx_index i1 = agx_immediate(1);

#define UNOP(nop, aop)                                                         \
   case nir_op_##nop:                                                          \
      return agx_##aop##_to(b, dst, s0);
#define BINOP(nop, aop)                                                        \
   case nir_op_##nop:                                                          \
      return agx_##aop##_to(b, dst, s0, s1);
#define TRIOP(nop, aop)                                                        \
   case nir_op_##nop:                                                          \
      return agx_##aop##_to(b, dst, s0, s1, s2);

   switch (instr->op) {
      BINOP(fadd, fadd);
      BINOP(fmul, fmul);
      TRIOP(ffma, fma);

      UNOP(f2f16, fmov);
      UNOP(f2f32, fmov);
      UNOP(fround_even, roundeven);
      UNOP(ftrunc, trunc);
      UNOP(ffloor, floor);
      UNOP(fceil, ceil);
      UNOP(frcp, rcp);
      UNOP(frsq, rsqrt);
      UNOP(flog2, log2);
      UNOP(fexp2, exp2);

      UNOP(fddx, dfdx);
      UNOP(fddx_coarse, dfdx);
      UNOP(fddx_fine, dfdx);

      UNOP(fddy, dfdy);
      UNOP(fddy_coarse, dfdy);
      UNOP(fddy_fine, dfdy);

      UNOP(mov, mov);
      UNOP(u2u32, mov);
      UNOP(bitfield_reverse, bitrev);
      UNOP(bit_count, popcount);
      UNOP(ufind_msb, ffs);
      BINOP(iand, and);
      BINOP(ior, or);
      BINOP(ixor, xor);

   case nir_op_feq:
      return agx_fcmpsel_to(b, dst, s0, s1, i1, i0, AGX_FCOND_EQ);
   case nir_op_flt:
      return agx_fcmpsel_to(b, dst, s0, s1, i1, i0, AGX_FCOND_LT);
   case nir_op_fge:
      return agx_fcmpsel_to(b, dst, s0, s1, i1, i0, AGX_FCOND_GE);
   case nir_op_fneu:
      return agx_fcmpsel_to(b, dst, s0, s1, i0, i1, AGX_FCOND_EQ);

   case nir_op_ieq:
      return agx_icmpsel_to(b, dst, s0, s1, i1, i0, AGX_ICOND_UEQ);
   case nir_op_ine:
      return agx_icmpsel_to(b, dst, s0, s1, i0, i1, AGX_ICOND_UEQ);
   case nir_op_ilt:
      return agx_icmpsel_to(b, dst, s0, s1, i1, i0, AGX_ICOND_SLT);
   case nir_op_ige:
      return agx_icmpsel_to(b, dst, s0, s1, i0, i1, AGX_ICOND_SLT);
   case nir_op_ult:
      return agx_icmpsel_to(b, dst, s0, s1, i1, i0, AGX_ICOND_ULT);
   case nir_op_uge:
      return agx_icmpsel_to(b, dst, s0, s1, i0, i1, AGX_ICOND_ULT);

   case nir_op_inot:
      if (sz == 1)
         return agx_xor_to(b, dst, s0, i1);
      else
         return agx_not_to(b, dst, s0);

   case nir_op_b2b1:
      return agx_icmpsel_to(b, dst, s0, i0, i0, i1, AGX_ICOND_UEQ);

   case nir_op_fsqrt:
      return agx_fmul_to(b, dst, s0, agx_srsqrt(b, s0));
   case nir_op_fsub:
      return agx_fadd_to(b, dst, s0, agx_neg(s1));
   case nir_op_fabs:
      return agx_fmov_to(b, dst, agx_abs(s0));
   case nir_op_fneg:
      return agx_fmov_to(b, dst, agx_neg(s0));

   case nir_op_fmin:
      return agx_fcmpsel_to(b, dst, s0, s1, s0, s1, AGX_FCOND_LTN);
   case nir_op_fmax:
      return agx_fcmpsel_to(b, dst, s0, s1, s0, s1, AGX_FCOND_GTN);
   case nir_op_imin:
      return agx_icmpsel_to(b, dst, s0, s1, s0, s1, AGX_ICOND_SLT);
   case nir_op_imax:
      return agx_icmpsel_to(b, dst, s0, s1, s0, s1, AGX_ICOND_SGT);
   case nir_op_umin:
      return agx_icmpsel_to(b, dst, s0, s1, s0, s1, AGX_ICOND_ULT);
   case nir_op_umax:
      return agx_icmpsel_to(b, dst, s0, s1, s0, s1, AGX_ICOND_UGT);

   case nir_op_iadd:
      return agx_iadd_to(b, dst, s0, s1, 0);
   case nir_op_isub:
      return agx_iadd_to(b, dst, s0, agx_neg(s1), 0);
   case nir_op_ineg:
      return agx_iadd_to(b, dst, i0, agx_neg(s0), 0);
   case nir_op_imul:
      return agx_imad_to(b, dst, s0, s1, i0, 0);
   case nir_op_umul_2x32_64:
      return agx_imad_to(b, dst, agx_abs(s0), agx_abs(s1), i0, 0);
   case nir_op_imul_2x32_64:
      return agx_imad_to(b, dst, s0, s1, i0, 0);
   case nir_op_umul_high:
      return agx_umul_high_to(b, dst, s0, s1);

   case nir_op_ishl:
      return agx_bfi_to(b, dst, i0, s0, s1, 0);
   case nir_op_ushr:
      return agx_ushr_to(b, dst, s0, s1);
   case nir_op_ishr:
      return agx_asr_to(b, dst, s0, s1);

   case nir_op_extr_agx:
      return agx_extr_to(b, dst, s0, s1, s2,
                         nir_src_as_uint(instr->src[3].src));

   case nir_op_bcsel:
      return agx_icmpsel_to(b, dst, s0, i0, s2, s1, AGX_ICOND_UEQ);

   case nir_op_b2i32:
   case nir_op_b2i16:
      return agx_icmpsel_to(b, dst, s0, i0, i0, i1, AGX_ICOND_UEQ);

   case nir_op_b2b32:
      return agx_icmpsel_to(b, dst, s0, i0, i0, agx_mov_imm(b, 32, ~0),
                            AGX_ICOND_UEQ);

   case nir_op_b2f16:
   case nir_op_b2f32: {
      /* At this point, boolean is just zero/nonzero, so compare with zero */
      agx_index f1 = (sz == 16) ? agx_mov_imm(b, 16, _mesa_float_to_half(1.0))
                                : agx_mov_imm(b, 32, fui(1.0));

      return agx_fcmpsel_to(b, dst, s0, i0, i0, f1, AGX_FCOND_EQ);
   }

   case nir_op_i2i32: {
      if (src_sz == 8) {
         /* Sign extend in software, NIR likes 8-bit conversions */
         agx_index ishl16 = agx_bfi(b, i0, s0, agx_immediate(8), 0);
         return agx_asr_to(b, dst, ishl16, agx_immediate(8));
      } else {
         assert(s0.size == AGX_SIZE_16 && "other conversions lowered");
         return agx_iadd_to(b, dst, s0, i0, 0);
      }
   }

   case nir_op_i2i16: {
      if (src_sz == 8) {
         /* Sign extend in software, NIR likes 8-bit conversions */
         agx_index ishl16 = agx_bfi(b, i0, s0, agx_immediate(8), 0);
         return agx_asr_to(b, dst, ishl16, agx_immediate(8));
      } else {
         assert(s0.size == AGX_SIZE_32 && "other conversions lowered");
         return agx_subdivide_to(b, dst, s0, 0);
      }
   }

   case nir_op_u2u16: {
      if (s0.size == AGX_SIZE_32)
         return agx_subdivide_to(b, dst, s0, 0);
      else
         return agx_mov_to(b, dst, s0);
   }

   case nir_op_iadd_sat: {
      agx_instr *I = agx_iadd_to(b, dst, s0, s1, 0);
      I->saturate = true;
      return I;
   }

   case nir_op_isub_sat: {
      agx_instr *I = agx_iadd_to(b, dst, s0, agx_neg(s1), 0);
      I->saturate = true;
      return I;
   }

   case nir_op_uadd_sat: {
      agx_instr *I = agx_iadd_to(b, dst, agx_abs(s0), agx_abs(s1), 0);
      I->saturate = true;
      return I;
   }

   case nir_op_usub_sat: {
      agx_instr *I = agx_iadd_to(b, dst, agx_abs(s0), agx_neg(agx_abs(s1)), 0);
      I->saturate = true;
      return I;
   }

   case nir_op_fsat: {
      agx_instr *I = agx_fadd_to(b, dst, s0, agx_negzero());
      I->saturate = true;
      return I;
   }

   case nir_op_fsin_agx: {
      agx_index fixup = agx_sin_pt_1(b, s0);
      agx_index sinc = agx_sin_pt_2(b, fixup);
      return agx_fmul_to(b, dst, sinc, fixup);
   }

   case nir_op_f2i16:
      return agx_convert_to(b, dst, agx_immediate(AGX_CONVERT_F_TO_S16), s0,
                            AGX_ROUND_RTZ);

   case nir_op_f2i32:
      return agx_convert_to(b, dst, agx_immediate(AGX_CONVERT_F_TO_S32), s0,
                            AGX_ROUND_RTZ);

   case nir_op_f2u16:
      return agx_convert_to(b, dst, agx_immediate(AGX_CONVERT_F_TO_U16), s0,
                            AGX_ROUND_RTZ);

   case nir_op_f2u32:
      return agx_convert_to(b, dst, agx_immediate(AGX_CONVERT_F_TO_U32), s0,
                            AGX_ROUND_RTZ);

   case nir_op_u2f16:
   case nir_op_u2f32: {
      if (src_sz == 64)
         unreachable("64-bit conversions unimplemented");

      enum agx_convert mode = (src_sz == 32)   ? AGX_CONVERT_U32_TO_F
                              : (src_sz == 16) ? AGX_CONVERT_U16_TO_F
                                               : AGX_CONVERT_U8_TO_F;

      return agx_convert_to(b, dst, agx_immediate(mode), s0, AGX_ROUND_RTE);
   }

   case nir_op_i2f16:
   case nir_op_i2f32: {
      if (src_sz == 64)
         unreachable("64-bit conversions unimplemented");

      enum agx_convert mode = (src_sz == 32)   ? AGX_CONVERT_S32_TO_F
                              : (src_sz == 16) ? AGX_CONVERT_S16_TO_F
                                               : AGX_CONVERT_S8_TO_F;

      return agx_convert_to(b, dst, agx_immediate(mode), s0, AGX_ROUND_RTE);
   }

   case nir_op_pack_32_2x16_split:
   case nir_op_pack_64_2x32_split: {
      agx_index idx[] = {s0, s1};
      return agx_emit_collect_to(b, dst, 2, idx);
   }

   case nir_op_unpack_64_2x32_split_x:
   case nir_op_unpack_32_2x16_split_x:
      return agx_subdivide_to(b, dst, s0, 0);

   case nir_op_unpack_64_2x32_split_y:
   case nir_op_unpack_32_2x16_split_y:
      return agx_subdivide_to(b, dst, s0, 1);

   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4: {
      agx_index idx[] = {s0, s1, s2, s3};
      return agx_emit_collect_to(b, dst, srcs, idx);
   }

   case nir_op_vec8:
   case nir_op_vec16:
      unreachable("should've been lowered");

   default:
      fprintf(stderr, "Unhandled ALU op %s\n", nir_op_infos[instr->op].name);
      unreachable("Unhandled ALU instruction");
   }
}

static enum agx_lod_mode
agx_lod_mode_for_nir(nir_texop op)
{
   switch (op) {
   case nir_texop_tex:
   case nir_texop_tg4:
      return AGX_LOD_MODE_AUTO_LOD;
   case nir_texop_txb:
      return AGX_LOD_MODE_AUTO_LOD_BIAS;
   case nir_texop_txd:
      return AGX_LOD_MODE_LOD_GRAD;
   case nir_texop_txl:
      return AGX_LOD_MODE_LOD_MIN;
   case nir_texop_txf:
      return AGX_LOD_MODE_LOD_MIN;
   case nir_texop_txf_ms:
      return AGX_LOD_MODE_AUTO_LOD; /* no mipmapping */
   default:
      unreachable("Unhandled texture op");
   }
}

static enum agx_gather
agx_gather_for_nir(nir_tex_instr *tex)
{
   if (tex->op == nir_texop_tg4) {
      enum agx_gather components[] = {
         AGX_GATHER_R,
         AGX_GATHER_G,
         AGX_GATHER_B,
         AGX_GATHER_A,
      };

      assert(tex->component < ARRAY_SIZE(components));
      return components[tex->component];
   } else {
      return AGX_GATHER_NONE;
   }
}

static void
agx_emit_tex(agx_builder *b, nir_tex_instr *instr)
{
   agx_index coords = agx_null(), texture = agx_immediate(instr->texture_index),
             sampler = agx_immediate(instr->sampler_index),
             lod = agx_immediate(0), compare = agx_null(),
             packed_offset = agx_null();

   bool txf = (instr->op == nir_texop_txf || instr->op == nir_texop_txf_ms);

   /* txf loads a texture without an associated sampler, but in the hardware
    * there is an associated load of a sampler. This requires that the driver
    * upload a dummy sampler.
    */
   b->shader->out->needs_dummy_sampler |= txf;

   for (unsigned i = 0; i < instr->num_srcs; ++i) {
      agx_index index = agx_src_index(&instr->src[i].src);

      switch (instr->src[i].src_type) {
      case nir_tex_src_backend1:
         coords = index;
         break;

      case nir_tex_src_backend2:
         packed_offset = index;
         break;

      case nir_tex_src_lod:
      case nir_tex_src_bias:
         lod = index;
         break;

      case nir_tex_src_comparator:
         assert(index.size == AGX_SIZE_32);
         compare = index;
         break;

      case nir_tex_src_texture_offset:
         texture = index;
         break;
      case nir_tex_src_sampler_offset:
         sampler = index;
         break;

      case nir_tex_src_ddx: {
         int y_idx = nir_tex_instr_src_index(instr, nir_tex_src_ddy);
         assert(y_idx >= 0 && "we only handle gradients");

         unsigned n = nir_tex_instr_src_size(instr, y_idx);
         assert((n == 2 || n == 3) && "other sizes not supported");

         agx_index index2 = agx_src_index(&instr->src[y_idx].src);

         /* We explicitly don't cache about the split cache for this */
         lod = agx_temp(b->shader, AGX_SIZE_32);
         agx_instr *I = agx_collect_to(b, lod, 2 * n);

         for (unsigned i = 0; i < n; ++i) {
            I->src[(2 * i) + 0] = agx_emit_extract(b, index, i);
            I->src[(2 * i) + 1] = agx_emit_extract(b, index2, i);
         }

         break;
      }

      case nir_tex_src_ddy:
         /* handled above */
         break;

      default:
         unreachable("Unexpected texture source");
      }
   }

   agx_index dst = agx_dest_index(&instr->dest);

   /* Pack shadow reference value (compare) and packed offset together */
   agx_index compare_offset = agx_null();

   if (!agx_is_null(compare) && !agx_is_null(packed_offset))
      compare_offset = agx_vec2(b, compare, packed_offset);
   else if (!agx_is_null(packed_offset))
      compare_offset = packed_offset;
   else if (!agx_is_null(compare))
      compare_offset = compare;

   unsigned nr_channels = nir_dest_num_components(instr->dest);
   nir_component_mask_t mask = nir_ssa_def_components_read(&instr->dest.ssa);

   /* Destination masking doesn't seem to work properly for gathers (because
    * it's mostly pointless), but it does show up in the lowering of
    * textureGatherOffsets. Don't try to mask the destination for gathers.
    */
   if (instr->op == nir_texop_tg4)
      mask = BITFIELD_MASK(nr_channels);

   agx_index tmp = agx_temp(b->shader, dst.size);

   agx_instr *I = agx_texture_sample_to(
      b, tmp, coords, lod, texture, sampler, compare_offset,
      agx_tex_dim(instr->sampler_dim, instr->is_array),
      agx_lod_mode_for_nir(instr->op), mask, 0, !agx_is_null(packed_offset),
      !agx_is_null(compare), agx_gather_for_nir(instr));

   if (txf)
      I->op = AGX_OPCODE_TEXTURE_LOAD;

   agx_index packed_channels[4] = {agx_null()};
   agx_index unpacked_channels[4] = {agx_null()};

   /* Hardware writes the masked components contiguously, expand out for NIR */
   agx_emit_split(b, packed_channels, tmp, 4 /* XXX: why not nr_channels */);

   for (unsigned i = 0; i < nr_channels; ++i) {
      unpacked_channels[i] =
         (mask & BITFIELD_BIT(i))
            ? packed_channels[util_bitcount(mask & BITFIELD_MASK(i))]
            : agx_undef(tmp.size);
   }

   agx_emit_collect_to(b, dst, nr_channels, unpacked_channels);
}

/*
 * Mark the logical end of the current block by emitting a p_logical_end marker.
 * Note if an unconditional jump is emitted (for instance, to break out of a
 * loop from inside an if), the block has already reached its logical end so we
 * don't re-emit p_logical_end. The validator checks this, and correct register
 * allocation depends on it.
 */
static void
agx_emit_logical_end(agx_builder *b)
{
   if (!b->shader->current_block->unconditional_jumps)
      agx_logical_end(b);
}

/*
 * NIR loops are treated as a pair of AGX loops:
 *
 *    do {
 *       do {
 *          ...
 *       } while (0);
 *    } while (cond);
 *
 * By manipulating the nesting counter, we may break out of nested loops, so
 * under the model, both break and continue may be implemented as breaks, where
 * break breaks out of the outer loop (2 layers) and continue breaks out of the
 * inner loop (1 layer).
 *
 * After manipulating the nesting counter directly, pop_exec #0 must be used to
 * flush the update to the execution mask.
 */
static void
agx_emit_jump(agx_builder *b, nir_jump_instr *instr)
{
   agx_context *ctx = b->shader;
   assert(instr->type == nir_jump_break || instr->type == nir_jump_continue);

   /* Break out of either one or two loops */
   unsigned nestings = b->shader->loop_nesting;

   if (instr->type == nir_jump_continue) {
      nestings += 1;
      agx_block_add_successor(ctx->current_block, ctx->continue_block);
   } else if (instr->type == nir_jump_break) {
      nestings += 2;
      agx_block_add_successor(ctx->current_block, ctx->break_block);
   }

   /* Update the counter and flush */
   agx_nest(b, agx_immediate(nestings));

   /* Jumps must come at the end of a block */
   agx_emit_logical_end(b);
   agx_pop_exec(b, 0);

   ctx->current_block->unconditional_jumps = true;
}

static void
agx_emit_phi(agx_builder *b, nir_phi_instr *instr)
{
   agx_instr *I = agx_phi_to(b, agx_dest_index(&instr->dest),
                             exec_list_length(&instr->srcs));

   /* Deferred */
   I->phi = instr;
}

/* Look up the AGX block corresponding to a given NIR block. Used when
 * translating phi nodes after emitting all blocks.
 */
static agx_block *
agx_from_nir_block(agx_context *ctx, nir_block *block)
{
   return ctx->indexed_nir_blocks[block->index];
}

static void
agx_emit_phi_deferred(agx_context *ctx, agx_block *block, agx_instr *I)
{
   nir_phi_instr *phi = I->phi;

   /* Guaranteed by lower_phis_to_scalar */
   assert(phi->dest.ssa.num_components == 1);

   nir_foreach_phi_src(src, phi) {
      agx_block *pred = agx_from_nir_block(ctx, src->pred);
      unsigned i = agx_predecessor_index(block, pred);
      assert(i < I->nr_srcs);

      I->src[i] = agx_src_index(&src->src);
   }
}

static void
agx_emit_phis_deferred(agx_context *ctx)
{
   agx_foreach_block(ctx, block) {
      agx_foreach_phi_in_block(block, I)
         agx_emit_phi_deferred(ctx, block, I);
   }
}

static void
agx_emit_undef(agx_builder *b, nir_ssa_undef_instr *instr)
{
   /* For now, just lower undefs to zero. This doesn't matter too much, since
    * the lowering happens in NIR and this just allows for late lowering passes
    * to result in undefs.
    */
   agx_mov_imm_to(b, agx_nir_ssa_index(&instr->def), 0);
}

static void
agx_emit_instr(agx_builder *b, struct nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_load_const:
      agx_emit_load_const(b, nir_instr_as_load_const(instr));
      break;

   case nir_instr_type_intrinsic:
      agx_emit_intrinsic(b, nir_instr_as_intrinsic(instr));
      break;

   case nir_instr_type_alu:
      agx_emit_alu(b, nir_instr_as_alu(instr));
      break;

   case nir_instr_type_tex:
      agx_emit_tex(b, nir_instr_as_tex(instr));
      break;

   case nir_instr_type_jump:
      agx_emit_jump(b, nir_instr_as_jump(instr));
      break;

   case nir_instr_type_phi:
      agx_emit_phi(b, nir_instr_as_phi(instr));
      break;

   case nir_instr_type_ssa_undef:
      agx_emit_undef(b, nir_instr_as_ssa_undef(instr));
      break;

   default:
      unreachable("should've been lowered");
   }
}

static agx_block *
agx_create_block(agx_context *ctx)
{
   agx_block *blk = rzalloc(ctx, agx_block);

   util_dynarray_init(&blk->predecessors, blk);

   return blk;
}

static agx_block *
emit_block(agx_context *ctx, nir_block *block)
{
   if (ctx->after_block) {
      ctx->current_block = ctx->after_block;
      ctx->after_block = NULL;
   } else {
      ctx->current_block = agx_create_block(ctx);
   }

   agx_block *blk = ctx->current_block;
   list_addtail(&blk->link, &ctx->blocks);
   list_inithead(&blk->instructions);

   ctx->indexed_nir_blocks[block->index] = blk;

   agx_builder _b = agx_init_builder(ctx, agx_after_block(blk));

   nir_foreach_instr(instr, block) {
      agx_emit_instr(&_b, instr);
   }

   return blk;
}

static agx_block *emit_cf_list(agx_context *ctx, struct exec_list *list);

/* Emit if-else as
 *
 *    if_icmp cond != 0
 *       ...
 *    else_icmp cond == 0
 *       ...
 *    pop_exec
 *
 * If the else is empty, we can omit the else_icmp. This happens elsewhere, as
 * an empty else block can become nonempty after RA due to phi lowering. This is
 * not usually optimal, but it's a start.
 */

static void
emit_if(agx_context *ctx, nir_if *nif)
{
   agx_block *first_block = ctx->current_block;
   agx_builder _b = agx_init_builder(ctx, agx_after_block(first_block));
   agx_index cond = agx_src_index(&nif->condition);

   agx_emit_logical_end(&_b);
   agx_if_icmp(&_b, cond, agx_zero(), 1, AGX_ICOND_UEQ, true);
   ctx->loop_nesting++;

   /* Emit the two subblocks. */
   agx_block *if_block = emit_cf_list(ctx, &nif->then_list);
   agx_block *end_then = ctx->current_block;

   _b.cursor = agx_after_block(ctx->current_block);
   agx_emit_logical_end(&_b);
   agx_else_icmp(&_b, cond, agx_zero(), 1, AGX_ICOND_UEQ, false);

   agx_block *else_block = emit_cf_list(ctx, &nif->else_list);
   agx_block *end_else = ctx->current_block;

   ctx->after_block = agx_create_block(ctx);

   agx_block_add_successor(first_block, if_block);
   agx_block_add_successor(first_block, else_block);
   agx_block_add_successor(end_then, ctx->after_block);
   agx_block_add_successor(end_else, ctx->after_block);

   _b.cursor = agx_after_block(ctx->current_block);
   agx_emit_logical_end(&_b);
   agx_pop_exec(&_b, 1);
   ctx->loop_nesting--;
}

static void
emit_loop(agx_context *ctx, nir_loop *nloop)
{
   assert(!nir_loop_has_continue_construct(nloop));
   /* We only track nesting within the innermost loop, so push and reset */
   unsigned pushed_nesting = ctx->loop_nesting;
   ctx->loop_nesting = 0;

   agx_block *popped_break = ctx->break_block;
   agx_block *popped_continue = ctx->continue_block;

   ctx->break_block = agx_create_block(ctx);
   ctx->continue_block = agx_create_block(ctx);

   /* Make room for break/continue nesting (TODO: skip if no divergent CF) */
   agx_builder _b = agx_init_builder(ctx, agx_after_block(ctx->current_block));
   agx_emit_logical_end(&_b);
   agx_push_exec(&_b, 2);

   /* Fallthrough to body */
   agx_block_add_successor(ctx->current_block, ctx->continue_block);

   /* Emit the body */
   ctx->after_block = ctx->continue_block;
   agx_block *start_block = emit_cf_list(ctx, &nloop->body);

   /* Fix up the nesting counter via an always true while_icmp, and branch back
    * to start of loop if any lanes are active */
   _b.cursor = agx_after_block(ctx->current_block);
   agx_emit_logical_end(&_b);
   agx_while_icmp(&_b, agx_zero(), agx_zero(), 2, AGX_ICOND_UEQ, false);
   agx_jmp_exec_any(&_b, start_block);
   agx_pop_exec(&_b, 2);
   agx_block_add_successor(ctx->current_block, ctx->continue_block);

   /* Pop off */
   ctx->after_block = ctx->break_block;
   ctx->break_block = popped_break;
   ctx->continue_block = popped_continue;

   /* Update shader-db stats */
   ++ctx->loop_count;

   /* All nested control flow must have finished */
   assert(ctx->loop_nesting == 0);

   /* Restore loop nesting (we might be inside an if inside an outer loop) */
   ctx->loop_nesting = pushed_nesting;
}

/* Before the first control flow structure, the nesting counter needs to be
 * zeroed for correct operation. This only happens at most once, since by
 * definition this occurs at the end of the first block, which dominates the
 * rest of the program. */

static void
emit_first_cf(agx_context *ctx)
{
   if (ctx->any_cf)
      return;

   agx_builder _b = agx_init_builder(ctx, agx_after_block(ctx->current_block));
   agx_nest(&_b, agx_immediate(0));
   ctx->any_cf = true;
}

static agx_block *
emit_cf_list(agx_context *ctx, struct exec_list *list)
{
   agx_block *start_block = NULL;

   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block: {
         agx_block *block = emit_block(ctx, nir_cf_node_as_block(node));

         if (!start_block)
            start_block = block;

         break;
      }

      case nir_cf_node_if:
         emit_first_cf(ctx);
         emit_if(ctx, nir_cf_node_as_if(node));
         break;

      case nir_cf_node_loop:
         emit_first_cf(ctx);
         emit_loop(ctx, nir_cf_node_as_loop(node));
         break;

      default:
         unreachable("Unknown control flow");
      }
   }

   return start_block;
}

static void
agx_set_st_vary_final(agx_context *ctx)
{
   agx_foreach_instr_global_rev(ctx, I) {
      if (I->op == AGX_OPCODE_ST_VARY) {
         I->last = true;
         return;
      }
   }

   /* If we got here, there was no varying written. We need to mark that. */
   agx_block *last_block = list_last_entry(&ctx->blocks, agx_block, link);
   agx_builder _b = agx_init_builder(ctx, agx_after_block_logical(last_block));
   agx_no_varyings(&_b);
}

static int
agx_dump_stats(agx_context *ctx, unsigned size, char **out)
{
   unsigned nr_ins = 0;

   /* Count instructions */
   agx_foreach_instr_global(ctx, I)
      nr_ins++;

   unsigned nr_threads =
      agx_occupancy_for_register_count(ctx->max_reg).max_threads;

   return asprintf(out,
                   "%s shader: %u inst, %u bytes, %u halfregs, %u threads, "
                   "%u loops, %u:%u spills:fills",
                   gl_shader_stage_name(ctx->stage), nr_ins, size, ctx->max_reg,
                   nr_threads, ctx->loop_count, ctx->spills, ctx->fills);
}

static int
glsl_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static bool
agx_lower_sincos_filter(const nir_instr *instr, UNUSED const void *_)
{
   if (instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu = nir_instr_as_alu(instr);
   return alu->op == nir_op_fsin || alu->op == nir_op_fcos;
}

/* Sine and cosine are implemented via the sin_pt_1 and sin_pt_2 opcodes for
 * heavy lifting. sin_pt_2 implements sinc in the first quadrant, expressed in
 * turns (sin (tau x) / x), while sin_pt_1 implements a piecewise sign/offset
 * fixup to transform a quadrant angle [0, 4] to [-1, 1]. The NIR opcode
 * fsin_agx models the fixup, sinc, and multiply to obtain sine, so we just
 * need to change units from radians to quadrants modulo turns. Cosine is
 * implemented by shifting by one quadrant: cos(x) = sin(x + tau/4).
 */

static nir_ssa_def *
agx_lower_sincos_impl(struct nir_builder *b, nir_instr *instr, UNUSED void *_)
{
   nir_alu_instr *alu = nir_instr_as_alu(instr);
   nir_ssa_def *x = nir_mov_alu(b, alu->src[0], 1);
   nir_ssa_def *turns = nir_fmul_imm(b, x, M_1_PI * 0.5f);

   if (alu->op == nir_op_fcos)
      turns = nir_fadd_imm(b, turns, 0.25f);

   nir_ssa_def *quadrants = nir_fmul_imm(b, nir_ffract(b, turns), 4.0);
   return nir_fsin_agx(b, quadrants);
}

static bool
agx_lower_sincos(nir_shader *shader)
{
   return nir_shader_lower_instructions(shader, agx_lower_sincos_filter,
                                        agx_lower_sincos_impl, NULL);
}

static bool
agx_lower_front_face(struct nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_front_face)
      return false;

   assert(intr->dest.is_ssa);
   nir_ssa_def *def = &intr->dest.ssa;
   assert(def->bit_size == 1);

   b->cursor = nir_before_instr(&intr->instr);
   nir_ssa_def_rewrite_uses(def, nir_inot(b, nir_load_back_face_agx(b, 1)));
   return true;
}

/*
 * Standard NIR optimization loop. This is run in agx_preprocess_nir, then once
 * again at shader variant compile time. Unless there was a complex shader key,
 * the latter run should be almost a no-op.
 */
static void
agx_optimize_loop_nir(nir_shader *nir)
{
   bool progress;

   do {
      progress = false;

      NIR_PASS(progress, nir, nir_lower_var_copies);
      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_lower_phis_to_scalar, true);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_peephole_select, 64, false, true);
      NIR_PASS(progress, nir, nir_opt_phi_precision);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_lower_undef_to_zero);

      NIR_PASS(progress, nir, nir_opt_loop_unroll);
   } while (progress);
}

static bool
combine_all_barriers(nir_intrinsic_instr *a, nir_intrinsic_instr *b, void *_)
{
   nir_intrinsic_set_memory_modes(
      a, nir_intrinsic_memory_modes(a) | nir_intrinsic_memory_modes(b));
   nir_intrinsic_set_memory_semantics(
      a, nir_intrinsic_memory_semantics(a) | nir_intrinsic_memory_semantics(b));
   nir_intrinsic_set_memory_scope(
      a, MAX2(nir_intrinsic_memory_scope(a), nir_intrinsic_memory_scope(b)));
   return true;
}

static void
agx_optimize_nir(nir_shader *nir, unsigned *preamble_size)
{
   agx_optimize_loop_nir(nir);

   bool progress = false;
   NIR_PASS(progress, nir, agx_nir_lower_address);

   /* If address lowering made progress, clean up before forming preambles.
    * Otherwise the optimized preambles might just be constants! Do it before
    * lowering int64 too, to avoid lowering constant int64 arithmetic.
    */
   if (progress) {
      NIR_PASS_V(nir, nir_opt_constant_folding);
      NIR_PASS_V(nir, nir_opt_dce);
   }

   /* Only lower int64 after optimizing address arithmetic, so that u2u64/i2i64
    * conversions remain.
    */
   progress = false;
   NIR_PASS(progress, nir, nir_lower_int64);

   /* If we lowered actual int64 arithmetic (not folded into the address
    * calculations), then clean up after the lowering.
    */
   if (progress) {
      do {
         progress = false;

         NIR_PASS(progress, nir, nir_opt_algebraic);
         NIR_PASS(progress, nir, nir_opt_dce);
      } while (progress);
   }

   if (likely(!(agx_compiler_debug & AGX_DBG_NOPREAMBLE)))
      NIR_PASS_V(nir, agx_nir_opt_preamble, preamble_size);

   /* Forming preambles may dramatically reduce the instruction count
    * in certain blocks, causing some if-else statements to become
    * trivial. We want to peephole select those, given that control flow
    * prediction instructions are costly.
    */
   NIR_PASS_V(nir, nir_opt_peephole_select, 64, false, true);

   NIR_PASS_V(nir, nir_opt_algebraic_late);
   NIR_PASS_V(nir, agx_nir_lower_algebraic_late);
   NIR_PASS_V(nir, nir_opt_constant_folding);
   NIR_PASS_V(nir, nir_opt_combine_barriers, combine_all_barriers, NULL);

   /* Must run after uses are fixed but before a last round of copyprop + DCE */
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS_V(nir, agx_nir_lower_load_mask);

   NIR_PASS_V(nir, nir_copy_prop);
   NIR_PASS_V(nir, nir_opt_dce);
   NIR_PASS_V(nir, nir_opt_cse);
   NIR_PASS_V(nir, nir_lower_alu_to_scalar, NULL, NULL);
   NIR_PASS_V(nir, nir_lower_load_const_to_scalar);

   /* Cleanup optimizations */
   nir_move_options move_all = nir_move_const_undef | nir_move_load_ubo |
                               nir_move_load_input | nir_move_comparisons |
                               nir_move_copies | nir_move_load_ssbo;

   NIR_PASS_V(nir, nir_opt_sink, move_all);
   NIR_PASS_V(nir, nir_opt_move, move_all);
   NIR_PASS_V(nir, nir_lower_phis_to_scalar, true);
}

/* ABI: position first, then user, then psiz */
static void
agx_remap_varyings_vs(nir_shader *nir, struct agx_varyings_vs *varyings)
{
   unsigned base = 0;

   /* Initialize to "nothing is written" */
   for (unsigned i = 0; i < ARRAY_SIZE(varyings->slots); ++i)
      varyings->slots[i] = ~0;

   /* gl_Position is implicitly written, although it may validly be absent in
    * vertex programs run only for transform feedback. Those ignore their
    * varyings so it doesn't matter what we do here as long as we don't fail.
    */
   varyings->slots[VARYING_SLOT_POS] = base;
   base += 4;

   u_foreach_bit64(loc, nir->info.outputs_written)
   {
      if (loc == VARYING_SLOT_POS || loc == VARYING_SLOT_PSIZ)
         continue;

      varyings->slots[loc] = base;
      base += 4;
   }

   /* TODO: Link FP16 varyings */
   varyings->base_index_fp16 = base;

   if (nir->info.outputs_written & VARYING_BIT_PSIZ) {
      varyings->slots[VARYING_SLOT_PSIZ] = base;
      base += 1;
   }

   /* All varyings linked now */
   varyings->nr_index = base;
}

/*
 * Varyings that are used as texture coordinates should be kept at fp32, because
 * fp16 does not have enough precision for large textures. It's technically
 * conformant not to, but every app gets this wrong.
 */
static bool
agx_gather_texcoords(nir_builder *b, nir_instr *instr, void *data)
{
   uint64_t *mask = data;

   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);

   int coord_idx = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   if (coord_idx < 0)
      return false;

   nir_src src = tex->src[coord_idx].src;
   nir_ssa_scalar x = nir_ssa_scalar_resolved(src.ssa, 0);
   nir_ssa_scalar y = nir_ssa_scalar_resolved(src.ssa, 1);

   if (x.def != y.def)
      return false;

   nir_instr *parent = x.def->parent_instr;

   if (parent->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(parent);

   if (intr->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   *mask |= BITFIELD64_BIT(sem.location);
   return false;
}

static bool
agx_gather_flat(nir_builder *b, nir_instr *instr, void *data)
{
   uint64_t *mask = data;
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_input)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   *mask |= BITFIELD64_BIT(sem.location);
   return false;
}

/*
 * Build a bit mask of varyings (by location) that are flatshaded or used as
 * texture coordinates. This information is needed by lower_mediump_io.
 */
static uint64_t
agx_fp32_varying_mask(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   uint64_t mask = 0;
   nir_shader_instructions_pass(nir, agx_gather_flat, nir_metadata_all, &mask);
   nir_shader_instructions_pass(nir, agx_gather_texcoords, nir_metadata_all,
                                &mask);
   return mask;
}

static nir_mem_access_size_align
mem_access_size_align_cb(nir_intrinsic_op intrin, uint8_t bytes, uint32_t align,
                         uint32_t align_offset, bool offset_is_const,
                         const void *cb_data)
{
   align = nir_combined_align(align, align_offset);

   assert(util_is_power_of_two_nonzero(align));
   unsigned bit_size = (bytes & 1) ? 8 : (bytes & 2) ? 16 : 32;
   if (align == 2)
      bit_size = MIN2(bit_size, 16);
   else if (align == 1)
      bit_size = 8;

   return (nir_mem_access_size_align){
      .num_components = bytes / (bit_size / 8),
      .bit_size = bit_size,
      .align = bit_size / 8,
   };
}

static bool
agx_should_dump(nir_shader *nir, unsigned agx_dbg_bit)
{
   return (agx_compiler_debug & agx_dbg_bit) &&
          !(nir->info.internal && !(agx_compiler_debug & AGX_DBG_INTERNAL));
}

static unsigned
agx_compile_function_nir(nir_shader *nir, nir_function_impl *impl,
                         struct agx_shader_key *key,
                         struct util_debug_callback *debug,
                         struct util_dynarray *binary,
                         struct agx_shader_info *out)
{
   nir_index_blocks(impl);

   agx_context *ctx = rzalloc(NULL, agx_context);
   ctx->nir = nir;
   ctx->out = out;
   ctx->key = key;
   ctx->stage = nir->info.stage;
   ctx->allocated_vec = _mesa_hash_table_u64_create(ctx);
   ctx->indexed_nir_blocks = rzalloc_array(ctx, agx_block *, impl->num_blocks);
   list_inithead(&ctx->blocks);

   ctx->alloc = impl->ssa_alloc;
   emit_cf_list(ctx, &impl->body);
   agx_emit_phis_deferred(ctx);

   /* Stop the main shader or preamble shader after the exit block. For real
    * functions, we would return here.
    */
   agx_block *last_block = list_last_entry(&ctx->blocks, agx_block, link);
   agx_builder _b = agx_init_builder(ctx, agx_after_block(last_block));

   if (ctx->stage == MESA_SHADER_FRAGMENT && !impl->function->is_preamble)
      agx_write_sample_mask_1(&_b);

   agx_logical_end(&_b);
   agx_stop(&_b);

   /* Index blocks now that we're done emitting so the order is consistent */
   agx_foreach_block(ctx, block)
      block->index = ctx->num_blocks++;

   agx_validate(ctx, "IR translation");

   if (likely(!(agx_compiler_debug & AGX_DBG_NOOPT))) {
      /* Eliminate dead instructions before CSE to avoid silly scheduling */
      agx_dce(ctx, false);

      /* CSE before eliminating dead destinations so that subdivision is
       * optimized properly.
       */
      agx_opt_cse(ctx);

      /* After DCE, use counts are right so we can run the optimizer. */
      agx_optimizer(ctx);
   }

   /* For correctness, lower uniform sources after copyprop (for correctness,
    * as copyprop creates uniform sources). To keep register pressure in
    * check, lower after CSE, since moves are cheaper than registers.
    */
   agx_lower_uniform_sources(ctx);

   /* RA correctness depends on DCE */
   agx_dce(ctx, true);
   agx_validate(ctx, "Pre-RA passes");

   if (agx_should_dump(nir, AGX_DBG_SHADERS))
      agx_print_shader(ctx, stdout);

   agx_ra(ctx);
   agx_lower_64bit_postra(ctx);

   if (ctx->stage == MESA_SHADER_VERTEX && !impl->function->is_preamble)
      agx_set_st_vary_final(ctx);

   agx_lower_pseudo(ctx);
   agx_insert_waits(ctx);

   if (agx_should_dump(nir, AGX_DBG_SHADERS))
      agx_print_shader(ctx, stdout);

   /* Pad binary */
   if (binary->size % AGX_CODE_ALIGN) {
      unsigned ngrow = AGX_CODE_ALIGN - (binary->size % AGX_CODE_ALIGN);
      memset(util_dynarray_grow_bytes(binary, ngrow, 1), 0, ngrow);
   }

   unsigned offset = binary->size;
   assert((offset % AGX_CODE_ALIGN) == 0);

   agx_pack_binary(ctx, binary);

   unsigned nr_gprs = ctx->max_reg + 1;

   if (impl->function->is_preamble)
      out->nr_preamble_gprs = nr_gprs;
   else
      out->nr_gprs = nr_gprs;

   /* Don't dump statistics for preambles, since they're not worth optimizing */
   if (!impl->function->is_preamble) {
      char *stats;
      int ret = agx_dump_stats(ctx, binary->size, &stats);

      if (ret >= 0) {
         if (agx_should_dump(nir, AGX_DBG_SHADERDB)) {
            fprintf(stderr, "SHADER-DB: %s - %s\n", nir->info.label ?: "",
                    stats);
         }

         if (debug)
            util_debug_message(debug, SHADER_INFO, "%s", stats);

         free(stats);
      }
   }

   ralloc_free(ctx);

   return offset;
}

/*
 * Preprocess NIR. In particular, this lowers I/O. Drivers should call this
 * as soon as they don't need unlowered I/O.
 *
 * This also lowers as much as possible. After preprocessing NIR, the following
 * NIR passes are called by the GL driver:
 *
 *    - nir_lower_blend
 *    - nir_lower_texcoord_replace_late
 *    - agx_nir_lower_vbo
 *    - agx_nir_lower_tilebuffer
 *
 * Unless an instruction is constructed by one of the above passes, it should be
 * lowered here to avoid duplicate work with shader variants.
 */
void
agx_preprocess_nir(nir_shader *nir, bool support_lod_bias)
{
   NIR_PASS_V(nir, nir_lower_vars_to_ssa);

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      NIR_PASS_V(nir, nir_lower_point_size, 1.0, 0.0);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* Lower to maximum colour buffers, the excess stores will get cleaned up
       * by tilebuffer lowering so they won't become real shader code. However,
       * that depends on the shader key which we don't have at this point.
       */
      NIR_PASS_V(nir, nir_lower_fragcolor, 8);
   }

   /* Lower large arrays to scratch and small arrays to csel */
   NIR_PASS_V(nir, nir_lower_vars_to_scratch, nir_var_function_temp, 16,
              glsl_get_natural_size_align_bytes);
   NIR_PASS_V(nir, nir_lower_indirect_derefs, nir_var_function_temp, ~0);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_lower_vars_to_ssa);
   NIR_PASS_V(nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
              glsl_type_size, 0);
   NIR_PASS_V(nir, nir_lower_ssbo);
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(nir, agx_nir_lower_frag_sidefx);

      /* Interpolate varyings at fp16 and write to the tilebuffer at fp16. As an
       * exception, interpolate flat shaded at fp32. This works around a
       * hardware limitation. The resulting code (with an extra f2f16 at the end
       * if needed) matches what Metal produces.
       */
      NIR_PASS_V(nir, nir_lower_mediump_io,
                 nir_var_shader_in | nir_var_shader_out,
                 ~agx_fp32_varying_mask(nir), false);
   }

   /* Varying output is scalar, other I/O is vector */
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      NIR_PASS_V(nir, nir_lower_io_to_scalar, nir_var_shader_out);
   }

   /* Clean up deref gunk after lowering I/O */
   NIR_PASS_V(nir, nir_opt_dce);
   NIR_PASS_V(nir, agx_nir_lower_texture, support_lod_bias);

   nir_lower_idiv_options idiv_options = {
      .allow_fp16 = true,
   };

   NIR_PASS_V(nir, nir_lower_regs_to_ssa);
   NIR_PASS_V(nir, nir_lower_idiv, &idiv_options);
   NIR_PASS_V(nir, nir_lower_frexp);
   NIR_PASS_V(nir, nir_lower_alu_to_scalar, NULL, NULL);
   NIR_PASS_V(nir, nir_lower_load_const_to_scalar);
   NIR_PASS_V(nir, nir_lower_flrp, 16 | 32 | 64, false);
   NIR_PASS_V(nir, agx_lower_sincos);
   NIR_PASS_V(nir, nir_shader_instructions_pass, agx_lower_front_face,
              nir_metadata_block_index | nir_metadata_dominance, NULL);

   /* After lowering, run through the standard suite of NIR optimizations. We
    * will run through the loop later, once we have the shader key, but if we
    * run now, that run will ideally be almost a no-op.
    */
   agx_optimize_loop_nir(nir);

   /* We're lowered away all variables. Remove them all for smaller shaders. */
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_all, NULL);
   nir->info.io_lowered = true;

   /* Move before lowering */
   nir_move_options move_all = nir_move_const_undef | nir_move_load_ubo |
                               nir_move_load_input | nir_move_comparisons |
                               nir_move_copies | nir_move_load_ssbo;

   NIR_PASS_V(nir, nir_opt_sink, move_all);
   NIR_PASS_V(nir, nir_opt_move, move_all);
   NIR_PASS_V(nir, agx_nir_lower_ubo);
   NIR_PASS_V(nir, agx_nir_lower_shared_bitsize);
}

void
agx_compile_shader_nir(nir_shader *nir, struct agx_shader_key *key,
                       struct util_debug_callback *debug,
                       struct util_dynarray *binary,
                       struct agx_shader_info *out)
{
   agx_compiler_debug = agx_get_compiler_debug();

   memset(out, 0, sizeof *out);

   assert(nir->info.io_lowered &&
          "agx_preprocess_nir is called first, then the shader is specalized,"
          "then the specialized shader is compiled");

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      out->writes_psiz =
         nir->info.outputs_written & BITFIELD_BIT(VARYING_SLOT_PSIZ);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      out->no_colour_output = !(nir->info.outputs_written >> FRAG_RESULT_DATA0);
      out->disable_tri_merging = nir->info.fs.needs_all_helper_invocations ||
                                 nir->info.fs.needs_quad_helper_invocations ||
                                 nir->info.writes_memory;

      /* Report a canonical depth layout */
      enum gl_frag_depth_layout layout = nir->info.fs.depth_layout;

      if (!(nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH)))
         out->depth_layout = FRAG_DEPTH_LAYOUT_UNCHANGED;
      else if (layout == FRAG_DEPTH_LAYOUT_NONE)
         out->depth_layout = FRAG_DEPTH_LAYOUT_ANY;
      else
         out->depth_layout = layout;
   }

   out->nr_bindful_textures = nir->info.num_textures;

   /* Late clip plane lowering created discards */
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(nir, agx_nir_lower_zs_emit);
   }

   /* Late sysval lowering creates large loads. Load lowering creates unpacks */
   NIR_PASS_V(nir, nir_lower_mem_access_bit_sizes,
              nir_var_mem_ssbo | nir_var_mem_constant |
                 nir_var_mem_task_payload | nir_var_shader_temp |
                 nir_var_function_temp | nir_var_mem_global |
                 nir_var_mem_shared,
              mem_access_size_align_cb, NULL);
   NIR_PASS_V(nir, nir_lower_pack);

   /* Late blend lowering creates vectors */
   NIR_PASS_V(nir, nir_lower_alu_to_scalar, NULL, NULL);
   NIR_PASS_V(nir, nir_lower_load_const_to_scalar);

   /* Late VBO lowering creates constant udiv instructions */
   NIR_PASS_V(nir, nir_opt_idiv_const, 16);

   out->push_count = key->reserved_preamble;
   agx_optimize_nir(nir, &out->push_count);

   /* Implement conditional discard with real control flow like Metal */
   NIR_PASS_V(nir, nir_lower_discard_if,
              (nir_lower_discard_if_to_cf | nir_lower_demote_if_to_cf |
               nir_lower_terminate_if_to_cf));

   /* Must be last since NIR passes can remap driver_location freely */
   if (nir->info.stage == MESA_SHADER_VERTEX)
      agx_remap_varyings_vs(nir, &out->varyings.vs);

   if (agx_should_dump(nir, AGX_DBG_SHADERS))
      nir_print_shader(nir, stdout);

   nir_foreach_function(func, nir) {
      if (!func->impl)
         continue;

      unsigned offset =
         agx_compile_function_nir(nir, func->impl, key, debug, binary, out);

      if (func->is_preamble) {
         out->preamble_offset = offset;
         out->has_preamble = true;
      } else if (func->is_entrypoint) {
         out->main_offset = offset;
      } else {
         unreachable("General functions not yet supported");
      }
   }
}
