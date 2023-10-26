/*
 * Copyright 2021 Alyssa Rosenzweig
 * Copyright 2019-2020 Collabora, Ltd.
 * Copyright 2014-2017 Broadcom
 * Copyright 2010 Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "agx_state.h"
#include <errno.h>
#include <stdio.h>
#include "asahi/compiler/agx_compile.h"
#include "asahi/layout/layout.h"
#include "asahi/lib/agx_formats.h"
#include "asahi/lib/agx_helpers.h"
#include "asahi/lib/agx_pack.h"
#include "asahi/lib/agx_ppp.h"
#include "asahi/lib/agx_usc.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_serialize.h"
#include "compiler/shader_enums.h"
#include "gallium/auxiliary/nir/tgsi_to_nir.h"
#include "gallium/auxiliary/tgsi/tgsi_from_mesa.h"
#include "gallium/auxiliary/util/u_blend.h"
#include "gallium/auxiliary/util/u_draw.h"
#include "gallium/auxiliary/util/u_framebuffer.h"
#include "gallium/auxiliary/util/u_helpers.h"
#include "gallium/auxiliary/util/u_prim_restart.h"
#include "gallium/auxiliary/util/u_viewport.h"
#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/blob.h"
#include "util/compiler.h"
#include "util/format/u_format.h"
#include "util/format_srgb.h"
#include "util/half_float.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_prim.h"
#include "util/u_resource.h"
#include "util/u_transfer.h"
#include "util/u_upload_mgr.h"
#include "agx_device.h"
#include "agx_disk_cache.h"
#include "agx_tilebuffer.h"
#include "pool.h"

void
agx_legalize_compression(struct agx_context *ctx, struct agx_resource *rsrc,
                         enum pipe_format format)
{
   /* If the resource isn't compressed, we can reinterpret */
   if (rsrc->layout.tiling != AIL_TILING_TWIDDLED_COMPRESSED)
      return;

   /* Normalize due to Gallium shenanigans */
   if (format == PIPE_FORMAT_Z24_UNORM_S8_UINT ||
       format == PIPE_FORMAT_Z24X8_UNORM)
      format = PIPE_FORMAT_Z32_FLOAT;

   /* The physical format */
   enum pipe_format storage = rsrc->layout.format;

   /* sRGB vs linear are always compatible */
   storage = util_format_linear(storage);
   format = util_format_linear(format);

   /* If no reinterpretation happens, we don't have to decompress */
   if (storage == format)
      return;

   /* Otherwise, decompress. TODO: Reverse-engineer which formats are compatible
    * and don't need decompression. There are some vague hints in the Metal
    * documentation:
    *
    * https://developer.apple.com/documentation/metal/mtltextureusage/mtltextureusagepixelformatview?language=objc
    */
   agx_decompress(ctx, rsrc, "Incompatible formats");
}

static void
agx_set_shader_images(struct pipe_context *pctx, enum pipe_shader_type shader,
                      unsigned start_slot, unsigned count,
                      unsigned unbind_num_trailing_slots,
                      const struct pipe_image_view *iviews)
{
   struct agx_context *ctx = agx_context(pctx);
   ctx->stage[shader].dirty |= AGX_STAGE_DIRTY_IMAGE;

   /* Unbind start_slot...start_slot+count */
   if (!iviews) {
      for (int i = start_slot;
           i < start_slot + count + unbind_num_trailing_slots; i++) {
         pipe_resource_reference(&ctx->stage[shader].images[i].resource, NULL);
      }

      ctx->stage[shader].image_mask &= ~(((1ull << count) - 1) << start_slot);
      return;
   }

   /* Bind start_slot...start_slot+count */
   for (int i = 0; i < count; i++) {
      const struct pipe_image_view *image = &iviews[i];

      if (image->resource)
         ctx->stage[shader].image_mask |= BITFIELD_BIT(start_slot + i);
      else
         ctx->stage[shader].image_mask &= ~BITFIELD_BIT(start_slot + i);

      if (!image->resource) {
         util_copy_image_view(&ctx->stage[shader].images[start_slot + i], NULL);
         continue;
      }

      /* Images writeable with pixel granularity are incompatible with
       * compression. Decompress if necessary.
       */
      struct agx_resource *rsrc = agx_resource(image->resource);
      if (!rsrc->layout.writeable_image &&
          (image->shader_access & PIPE_IMAGE_ACCESS_WRITE)) {

         agx_decompress(ctx, rsrc, "Shader image");
      }

      /* Readable images may be compressed but are still subject to format
       * reinterpretation rules.
       */
      agx_legalize_compression(ctx, rsrc, image->format);

      if (image->shader_access & PIPE_IMAGE_ACCESS_WRITE)
         assert(rsrc->layout.writeable_image);

      /* FIXME: Decompress here once we have texture compression */
      util_copy_image_view(&ctx->stage[shader].images[start_slot + i], image);
   }

   /* Unbind start_slot+count...start_slot+count+unbind_num_trailing_slots */
   for (int i = 0; i < unbind_num_trailing_slots; i++) {
      ctx->stage[shader].image_mask &= ~BITFIELD_BIT(start_slot + count + i);
      util_copy_image_view(&ctx->stage[shader].images[start_slot + count + i],
                           NULL);
   }
}

static void
agx_set_shader_buffers(struct pipe_context *pctx, enum pipe_shader_type shader,
                       unsigned start, unsigned count,
                       const struct pipe_shader_buffer *buffers,
                       unsigned writable_bitmask)
{
   struct agx_context *ctx = agx_context(pctx);

   util_set_shader_buffers_mask(ctx->stage[shader].ssbo,
                                &ctx->stage[shader].ssbo_mask, buffers, start,
                                count);

   ctx->stage[shader].dirty |= AGX_STAGE_DIRTY_SSBO;
}

static void
agx_set_blend_color(struct pipe_context *pctx,
                    const struct pipe_blend_color *state)
{
   struct agx_context *ctx = agx_context(pctx);

   if (state)
      memcpy(&ctx->blend_color, state, sizeof(*state));

   ctx->dirty |= AGX_DIRTY_BLEND_COLOR;
}

static void *
agx_create_blend_state(struct pipe_context *ctx,
                       const struct pipe_blend_state *state)
{
   struct agx_blend *so = CALLOC_STRUCT(agx_blend);

   so->alpha_to_coverage = state->alpha_to_coverage;
   so->alpha_to_one = state->alpha_to_one;

   if (state->logicop_enable) {
      so->logicop_enable = true;
      so->logicop_func = state->logicop_func;
   }

   for (unsigned i = 0; i < PIPE_MAX_COLOR_BUFS; ++i) {
      unsigned rti = state->independent_blend_enable ? i : 0;
      struct pipe_rt_blend_state rt = state->rt[rti];

      if (state->logicop_enable) {
         /* No blending, but we get the colour mask below */
      } else if (!rt.blend_enable) {
         static const nir_lower_blend_channel replace = {
            .func = PIPE_BLEND_ADD,
            .src_factor = PIPE_BLENDFACTOR_ONE,
            .dst_factor = PIPE_BLENDFACTOR_ZERO,
         };

         so->rt[i].rgb = replace;
         so->rt[i].alpha = replace;
      } else {
         so->rt[i].rgb.func = rt.rgb_func;
         so->rt[i].rgb.src_factor = rt.rgb_src_factor;
         so->rt[i].rgb.dst_factor = rt.rgb_dst_factor;

         so->rt[i].alpha.func = rt.alpha_func;
         so->rt[i].alpha.src_factor = rt.alpha_src_factor;
         so->rt[i].alpha.dst_factor = rt.alpha_dst_factor;

         so->blend_enable = true;
      }

      so->rt[i].colormask = rt.colormask;

      if (rt.colormask)
         so->store |= (PIPE_CLEAR_COLOR0 << i);
   }

   return so;
}

static void
agx_bind_blend_state(struct pipe_context *pctx, void *cso)
{
   struct agx_context *ctx = agx_context(pctx);
   ctx->blend = cso;
   ctx->dirty |= AGX_DIRTY_BLEND;
}

static const enum agx_stencil_op agx_stencil_ops[PIPE_STENCIL_OP_INVERT + 1] = {
   [PIPE_STENCIL_OP_KEEP] = AGX_STENCIL_OP_KEEP,
   [PIPE_STENCIL_OP_ZERO] = AGX_STENCIL_OP_ZERO,
   [PIPE_STENCIL_OP_REPLACE] = AGX_STENCIL_OP_REPLACE,
   [PIPE_STENCIL_OP_INCR] = AGX_STENCIL_OP_INCR_SAT,
   [PIPE_STENCIL_OP_DECR] = AGX_STENCIL_OP_DECR_SAT,
   [PIPE_STENCIL_OP_INCR_WRAP] = AGX_STENCIL_OP_INCR_WRAP,
   [PIPE_STENCIL_OP_DECR_WRAP] = AGX_STENCIL_OP_DECR_WRAP,
   [PIPE_STENCIL_OP_INVERT] = AGX_STENCIL_OP_INVERT,
};

static void
agx_pack_stencil(struct agx_fragment_stencil_packed *out,
                 struct pipe_stencil_state st)
{
   if (st.enabled) {
      agx_pack(out, FRAGMENT_STENCIL, cfg) {
         cfg.compare = (enum agx_zs_func)st.func;
         cfg.write_mask = st.writemask;
         cfg.read_mask = st.valuemask;

         cfg.depth_pass = agx_stencil_ops[st.zpass_op];
         cfg.depth_fail = agx_stencil_ops[st.zfail_op];
         cfg.stencil_fail = agx_stencil_ops[st.fail_op];
      }
   } else {
      agx_pack(out, FRAGMENT_STENCIL, cfg) {
         cfg.compare = AGX_ZS_FUNC_ALWAYS;
         cfg.write_mask = 0xFF;
         cfg.read_mask = 0xFF;

         cfg.depth_pass = AGX_STENCIL_OP_KEEP;
         cfg.depth_fail = AGX_STENCIL_OP_KEEP;
         cfg.stencil_fail = AGX_STENCIL_OP_KEEP;
      }
   }
}

static void *
agx_create_zsa_state(struct pipe_context *ctx,
                     const struct pipe_depth_stencil_alpha_state *state)
{
   struct agx_zsa *so = CALLOC_STRUCT(agx_zsa);
   assert(!state->depth_bounds_test && "todo");

   so->base = *state;

   /* Handle the enable flag */
   enum pipe_compare_func depth_func =
      state->depth_enabled ? state->depth_func : PIPE_FUNC_ALWAYS;

   /* Z func can otherwise be used as-is */
   STATIC_ASSERT((enum agx_zs_func)PIPE_FUNC_NEVER == AGX_ZS_FUNC_NEVER);
   STATIC_ASSERT((enum agx_zs_func)PIPE_FUNC_LESS == AGX_ZS_FUNC_LESS);
   STATIC_ASSERT((enum agx_zs_func)PIPE_FUNC_EQUAL == AGX_ZS_FUNC_EQUAL);
   STATIC_ASSERT((enum agx_zs_func)PIPE_FUNC_LEQUAL == AGX_ZS_FUNC_LEQUAL);
   STATIC_ASSERT((enum agx_zs_func)PIPE_FUNC_GREATER == AGX_ZS_FUNC_GREATER);
   STATIC_ASSERT((enum agx_zs_func)PIPE_FUNC_NOTEQUAL == AGX_ZS_FUNC_NOT_EQUAL);
   STATIC_ASSERT((enum agx_zs_func)PIPE_FUNC_GEQUAL == AGX_ZS_FUNC_GEQUAL);
   STATIC_ASSERT((enum agx_zs_func)PIPE_FUNC_ALWAYS == AGX_ZS_FUNC_ALWAYS);

   agx_pack(&so->depth, FRAGMENT_FACE, cfg) {
      cfg.depth_function = (enum agx_zs_func)depth_func;
      cfg.disable_depth_write = !state->depth_writemask;
   }

   agx_pack_stencil(&so->front_stencil, state->stencil[0]);

   if (state->stencil[1].enabled) {
      agx_pack_stencil(&so->back_stencil, state->stencil[1]);
   } else {
      /* One sided stencil */
      so->back_stencil = so->front_stencil;
   }

   if (depth_func != PIPE_FUNC_NEVER && depth_func != PIPE_FUNC_ALWAYS)
      so->load |= PIPE_CLEAR_DEPTH;

   if (state->depth_writemask) {
      so->load |= PIPE_CLEAR_DEPTH;
      so->store |= PIPE_CLEAR_DEPTH;
   }

   if (state->stencil[0].enabled) {
      so->load |= PIPE_CLEAR_STENCIL; /* TODO: Optimize */
      so->store |= PIPE_CLEAR_STENCIL;
   }

   return so;
}

static void
agx_bind_zsa_state(struct pipe_context *pctx, void *cso)
{
   struct agx_context *ctx = agx_context(pctx);
   ctx->zs = cso;
   ctx->dirty |= AGX_DIRTY_ZS;
}

static enum agx_polygon_mode
agx_translate_polygon_mode(unsigned mode)
{
   switch (mode) {
   case PIPE_POLYGON_MODE_FILL:
      return AGX_POLYGON_MODE_FILL;
   case PIPE_POLYGON_MODE_POINT:
      return AGX_POLYGON_MODE_POINT;
   case PIPE_POLYGON_MODE_LINE:
      return AGX_POLYGON_MODE_LINE;
   default:
      unreachable("Unsupported polygon mode");
   }
}

static void *
agx_create_rs_state(struct pipe_context *ctx,
                    const struct pipe_rasterizer_state *cso)
{
   struct agx_rasterizer *so = CALLOC_STRUCT(agx_rasterizer);
   so->base = *cso;

   agx_pack(so->cull, CULL, cfg) {
      cfg.cull_front = cso->cull_face & PIPE_FACE_FRONT;
      cfg.cull_back = cso->cull_face & PIPE_FACE_BACK;
      cfg.front_face_ccw = cso->front_ccw;
      cfg.depth_clip = cso->depth_clip_near;
      cfg.depth_clamp = !cso->depth_clip_near;
      cfg.flat_shading_vertex =
         cso->flatshade_first ? AGX_PPP_VERTEX_0 : AGX_PPP_VERTEX_2;
      cfg.rasterizer_discard = cso->rasterizer_discard;
   };

   /* Two-sided polygon mode doesn't seem to work on G13. Apple's OpenGL
    * implementation lowers to multiple draws with culling. Warn.
    */
   if (unlikely(cso->fill_front != cso->fill_back)) {
      agx_msg("Warning: Two-sided fill modes are unsupported, "
              "rendering may be incorrect.\n");
   }

   so->polygon_mode = agx_translate_polygon_mode(cso->fill_front);
   so->line_width = agx_pack_line_width(cso->line_width);

   return so;
}

static void
agx_bind_rasterizer_state(struct pipe_context *pctx, void *cso)
{
   struct agx_context *ctx = agx_context(pctx);
   struct agx_rasterizer *so = cso;

   bool base_cso_changed = (cso == NULL) || (ctx->rast == NULL);

   /* Check if scissor or depth bias state has changed, since scissor/depth bias
    * enable is part of the rasterizer state but everything else needed for
    * scissors and depth bias is part of the scissor/depth bias arrays */
   bool scissor_zbias_changed =
      base_cso_changed || (ctx->rast->base.scissor != so->base.scissor) ||
      (ctx->rast->base.offset_tri != so->base.offset_tri);

   ctx->dirty |= AGX_DIRTY_RS;

   if (scissor_zbias_changed)
      ctx->dirty |= AGX_DIRTY_SCISSOR_ZBIAS;

   if (base_cso_changed ||
       (ctx->rast->base.sprite_coord_mode != so->base.sprite_coord_mode))
      ctx->dirty |= AGX_DIRTY_SPRITE_COORD_MODE;

   ctx->rast = so;
}

static enum agx_wrap
agx_wrap_from_pipe(enum pipe_tex_wrap in)
{
   switch (in) {
   case PIPE_TEX_WRAP_REPEAT:
      return AGX_WRAP_REPEAT;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
      return AGX_WRAP_CLAMP_TO_EDGE;
   case PIPE_TEX_WRAP_MIRROR_REPEAT:
      return AGX_WRAP_MIRRORED_REPEAT;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
      return AGX_WRAP_CLAMP_TO_BORDER;
   case PIPE_TEX_WRAP_CLAMP:
      return AGX_WRAP_CLAMP_GL;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
      return AGX_WRAP_MIRRORED_CLAMP_TO_EDGE;
   default:
      unreachable("Invalid wrap mode");
   }
}

static enum agx_mip_filter
agx_mip_filter_from_pipe(enum pipe_tex_mipfilter in)
{
   switch (in) {
   case PIPE_TEX_MIPFILTER_NEAREST:
      return AGX_MIP_FILTER_NEAREST;
   case PIPE_TEX_MIPFILTER_LINEAR:
      return AGX_MIP_FILTER_LINEAR;
   case PIPE_TEX_MIPFILTER_NONE:
      return AGX_MIP_FILTER_NONE;
   }

   unreachable("Invalid mip filter");
}

static const enum agx_compare_func agx_compare_funcs[PIPE_FUNC_ALWAYS + 1] = {
   [PIPE_FUNC_NEVER] = AGX_COMPARE_FUNC_NEVER,
   [PIPE_FUNC_LESS] = AGX_COMPARE_FUNC_LESS,
   [PIPE_FUNC_EQUAL] = AGX_COMPARE_FUNC_EQUAL,
   [PIPE_FUNC_LEQUAL] = AGX_COMPARE_FUNC_LEQUAL,
   [PIPE_FUNC_GREATER] = AGX_COMPARE_FUNC_GREATER,
   [PIPE_FUNC_NOTEQUAL] = AGX_COMPARE_FUNC_NOT_EQUAL,
   [PIPE_FUNC_GEQUAL] = AGX_COMPARE_FUNC_GEQUAL,
   [PIPE_FUNC_ALWAYS] = AGX_COMPARE_FUNC_ALWAYS,
};

static enum pipe_format
fixup_border_zs(enum pipe_format orig, union pipe_color_union *c)
{
   switch (orig) {
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
   case PIPE_FORMAT_Z24X8_UNORM:
      /* Z24 is internally promoted to Z32F via transfer_helper. These formats
       * are normalized so should get clamped, but Z32F does not get clamped, so
       * we clamp here.
       */
      c->f[0] = SATURATE(c->f[0]);
      return PIPE_FORMAT_Z32_FLOAT;

   case PIPE_FORMAT_X24S8_UINT:
   case PIPE_FORMAT_X32_S8X24_UINT:
      /* Separate stencil is internally promoted */
      return PIPE_FORMAT_S8_UINT;

   default:
      return orig;
   }
}

static void *
agx_create_sampler_state(struct pipe_context *pctx,
                         const struct pipe_sampler_state *state)
{
   struct agx_sampler_state *so = CALLOC_STRUCT(agx_sampler_state);
   so->base = *state;

   /* We report a max texture LOD bias of 16, so clamp appropriately */
   float lod_bias = CLAMP(state->lod_bias, -16.0, 16.0);
   so->lod_bias_as_fp16 = _mesa_float_to_half(lod_bias);

   agx_pack(&so->desc, SAMPLER, cfg) {
      cfg.minimum_lod = state->min_lod;
      cfg.maximum_lod = state->max_lod;
      cfg.maximum_anisotropy =
         util_next_power_of_two(MAX2(state->max_anisotropy, 1));
      cfg.magnify_linear = (state->mag_img_filter == PIPE_TEX_FILTER_LINEAR);
      cfg.minify_linear = (state->min_img_filter == PIPE_TEX_FILTER_LINEAR);
      cfg.mip_filter = agx_mip_filter_from_pipe(state->min_mip_filter);
      cfg.wrap_s = agx_wrap_from_pipe(state->wrap_s);
      cfg.wrap_t = agx_wrap_from_pipe(state->wrap_t);
      cfg.wrap_r = agx_wrap_from_pipe(state->wrap_r);
      cfg.pixel_coordinates = state->unnormalized_coords;
      cfg.compare_func = agx_compare_funcs[state->compare_func];
      cfg.compare_enable = state->compare_mode == PIPE_TEX_COMPARE_R_TO_TEXTURE;
      cfg.seamful_cube_maps = !state->seamless_cube_map;

      if (state->border_color_format != PIPE_FORMAT_NONE) {
         /* TODO: Optimize to use compact descriptors for black/white borders */
         so->uses_custom_border = true;
         cfg.border_colour = AGX_BORDER_COLOUR_CUSTOM;
      }
   }

   if (so->uses_custom_border) {
      union pipe_color_union border = state->border_color;
      enum pipe_format format =
         fixup_border_zs(state->border_color_format, &border);

      agx_pack_border(&so->border, border.ui, format);
   }

   return so;
}

static void
agx_delete_sampler_state(struct pipe_context *ctx, void *state)
{
   struct agx_sampler_state *so = state;
   FREE(so);
}

static void
agx_bind_sampler_states(struct pipe_context *pctx, enum pipe_shader_type shader,
                        unsigned start, unsigned count, void **states)
{
   struct agx_context *ctx = agx_context(pctx);

   ctx->stage[shader].dirty |= AGX_STAGE_DIRTY_SAMPLER;

   for (unsigned i = 0; i < count; i++) {
      unsigned p = start + i;
      ctx->stage[shader].samplers[p] = states ? states[i] : NULL;
      if (ctx->stage[shader].samplers[p])
         ctx->stage[shader].valid_samplers |= BITFIELD_BIT(p);
      else
         ctx->stage[shader].valid_samplers &= ~BITFIELD_BIT(p);
   }

   ctx->stage[shader].sampler_count =
      util_last_bit(ctx->stage[shader].valid_samplers);

   /* Recalculate whether we need custom borders */
   ctx->stage[shader].custom_borders = false;

   u_foreach_bit(i, ctx->stage[shader].valid_samplers) {
      if (ctx->stage[shader].samplers[i]->uses_custom_border)
         ctx->stage[shader].custom_borders = true;
   }
}

static enum agx_texture_dimension
agx_translate_tex_dim(enum pipe_texture_target dim, unsigned samples)
{
   assert(samples >= 1);

   switch (dim) {
   case PIPE_BUFFER:
   case PIPE_TEXTURE_1D:
      /* Lowered to 2D */
      assert(samples == 1);
      return AGX_TEXTURE_DIMENSION_2D;

   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D:
      return samples > 1 ? AGX_TEXTURE_DIMENSION_2D_MULTISAMPLED
                         : AGX_TEXTURE_DIMENSION_2D;

   case PIPE_TEXTURE_1D_ARRAY:
      assert(samples == 1);
      /* Lowered to 2D */
      FALLTHROUGH;
   case PIPE_TEXTURE_2D_ARRAY:
      return samples > 1 ? AGX_TEXTURE_DIMENSION_2D_ARRAY_MULTISAMPLED
                         : AGX_TEXTURE_DIMENSION_2D_ARRAY;

   case PIPE_TEXTURE_3D:
      assert(samples == 1);
      return AGX_TEXTURE_DIMENSION_3D;

   case PIPE_TEXTURE_CUBE:
      assert(samples == 1);
      return AGX_TEXTURE_DIMENSION_CUBE;

   case PIPE_TEXTURE_CUBE_ARRAY:
      assert(samples == 1);
      return AGX_TEXTURE_DIMENSION_CUBE_ARRAY;

   default:
      unreachable("Unsupported texture dimension");
   }
}

static enum agx_sample_count
agx_translate_sample_count(unsigned samples)
{
   switch (samples) {
   case 2:
      return AGX_SAMPLE_COUNT_2;
   case 4:
      return AGX_SAMPLE_COUNT_4;
   default:
      unreachable("Invalid sample count");
   }
}

static void
agx_pack_texture(void *out, struct agx_resource *rsrc,
                 enum pipe_format format /* override */,
                 const struct pipe_sampler_view *state, bool include_bo)
{
   const struct util_format_description *desc = util_format_description(format);

   assert(agx_is_valid_pixel_format(format));

   uint8_t format_swizzle[4] = {
      desc->swizzle[0],
      desc->swizzle[1],
      desc->swizzle[2],
      desc->swizzle[3],
   };

   if (util_format_is_depth_or_stencil(format)) {
      assert(!util_format_is_depth_and_stencil(format) &&
             "separate stencil always used");

      /* Broadcast depth and stencil */
      format_swizzle[0] = 0;
      format_swizzle[1] = 0;
      format_swizzle[2] = 0;
      format_swizzle[3] = 0;
   }

   /* We only have a single swizzle for the user swizzle and the format fixup,
    * so compose them now. */
   uint8_t out_swizzle[4];
   uint8_t view_swizzle[4] = {state->swizzle_r, state->swizzle_g,
                              state->swizzle_b, state->swizzle_a};

   util_format_compose_swizzles(format_swizzle, view_swizzle, out_swizzle);

   unsigned first_layer =
      (state->target == PIPE_BUFFER) ? 0 : state->u.tex.first_layer;

   /* Pack the descriptor into GPU memory */
   agx_pack(out, TEXTURE, cfg) {
      cfg.dimension = agx_translate_tex_dim(state->target,
                                            util_res_sample_count(&rsrc->base));
      cfg.layout = agx_translate_layout(rsrc->layout.tiling);
      cfg.channels = agx_pixel_format[format].channels;
      cfg.type = agx_pixel_format[format].type;
      cfg.swizzle_r = agx_channel_from_pipe(out_swizzle[0]);
      cfg.swizzle_g = agx_channel_from_pipe(out_swizzle[1]);
      cfg.swizzle_b = agx_channel_from_pipe(out_swizzle[2]);
      cfg.swizzle_a = agx_channel_from_pipe(out_swizzle[3]);

      if (state->target == PIPE_BUFFER) {
         unsigned size_el =
            agx_texture_buffer_size_el(format, state->u.buf.size);

         /* Use a 2D texture to increase the maximum size */
         cfg.width = 1024;
         cfg.height = DIV_ROUND_UP(size_el, cfg.width);
         cfg.first_level = cfg.last_level = 0;

         /* Stash the actual size in an unused part of the texture descriptor,
          * which we'll read later to implement txs.
          */
         cfg.acceleration_buffer = (size_el << 4);
      } else {
         cfg.width = rsrc->base.width0;
         cfg.height = rsrc->base.height0;
         cfg.first_level = state->u.tex.first_level;
         cfg.last_level = state->u.tex.last_level;
      }

      cfg.srgb = (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
      cfg.unk_mipmapped = rsrc->mipmapped;
      cfg.srgb_2_channel = cfg.srgb && util_format_colormask(desc) == 0x3;

      if (ail_is_compressed(&rsrc->layout)) {
         cfg.compressed_1 = true;
         cfg.extended = true;
      }

      if (include_bo) {
         cfg.address = agx_map_texture_gpu(rsrc, first_layer);

         if (state->target == PIPE_BUFFER)
            cfg.address += state->u.buf.offset;

         if (ail_is_compressed(&rsrc->layout)) {
            cfg.acceleration_buffer =
               agx_map_texture_gpu(rsrc, 0) + rsrc->layout.metadata_offset_B +
               (first_layer * rsrc->layout.compression_layer_stride_B);
         }
      }

      if (state->target == PIPE_TEXTURE_3D) {
         cfg.depth = rsrc->base.depth0;
      } else if (state->target == PIPE_BUFFER) {
         cfg.depth = 1;
      } else {
         unsigned layers =
            state->u.tex.last_layer - state->u.tex.first_layer + 1;

         if ((state->target == PIPE_TEXTURE_CUBE) ||
             (state->target == PIPE_TEXTURE_CUBE_ARRAY))
            layers /= 6;

         if (rsrc->layout.tiling == AIL_TILING_LINEAR &&
             (state->target == PIPE_TEXTURE_1D_ARRAY ||
              state->target == PIPE_TEXTURE_2D_ARRAY)) {

            cfg.depth_linear = layers;
            cfg.layer_stride_linear = (rsrc->layout.layer_stride_B - 0x80);
            cfg.extended = true;
         } else {
            assert((rsrc->layout.tiling != AIL_TILING_LINEAR) || (layers == 1));
            cfg.depth = layers;
         }
      }

      if (rsrc->base.nr_samples > 1)
         cfg.samples = agx_translate_sample_count(rsrc->base.nr_samples);

      if (state->target == PIPE_BUFFER) {
         cfg.stride = (cfg.width * util_format_get_blocksize(format)) - 16;
      } else if (rsrc->layout.tiling == AIL_TILING_LINEAR) {
         cfg.stride = ail_get_linear_stride_B(&rsrc->layout, 0) - 16;
      } else {
         assert(rsrc->layout.tiling == AIL_TILING_TWIDDLED ||
                rsrc->layout.tiling == AIL_TILING_TWIDDLED_COMPRESSED);

         cfg.page_aligned_layers = rsrc->layout.page_aligned_layers;
      }
   }
}

static struct pipe_sampler_view *
agx_create_sampler_view(struct pipe_context *pctx,
                        struct pipe_resource *orig_texture,
                        const struct pipe_sampler_view *state)
{
   struct agx_resource *rsrc = agx_resource(orig_texture);
   struct agx_sampler_view *so = CALLOC_STRUCT(agx_sampler_view);

   if (!so)
      return NULL;

   struct pipe_resource *texture = orig_texture;
   enum pipe_format format = state->format;

   const struct util_format_description *desc = util_format_description(format);

   /* Separate stencil always used on G13, so we need to fix up for Z32S8 */
   if (util_format_has_stencil(desc) && rsrc->separate_stencil) {
      if (util_format_has_depth(desc)) {
         /* Reinterpret as the depth-only part */
         format = util_format_get_depth_only(format);
      } else {
         /* Use the stencil-only-part */
         rsrc = rsrc->separate_stencil;
         texture = &rsrc->base;
         format = texture->format;
      }
   }

   agx_legalize_compression(agx_context(pctx), rsrc, format);

   /* Save off the resource that we actually use, with the stencil fixed up */
   so->rsrc = rsrc;
   agx_pack_texture(&so->desc, rsrc, format, state, false);

   so->base = *state;
   so->base.texture = NULL;
   pipe_resource_reference(&so->base.texture, orig_texture);
   pipe_reference_init(&so->base.reference, 1);
   so->base.context = pctx;
   return &so->base;
}

static void
agx_set_sampler_views(struct pipe_context *pctx, enum pipe_shader_type shader,
                      unsigned start, unsigned count,
                      unsigned unbind_num_trailing_slots, bool take_ownership,
                      struct pipe_sampler_view **views)
{
   struct agx_context *ctx = agx_context(pctx);
   unsigned new_nr = 0;
   unsigned i;

   assert(start == 0);

   if (!views)
      count = 0;

   for (i = 0; i < count; ++i) {
      if (views[i])
         new_nr = i + 1;

      if (take_ownership) {
         pipe_sampler_view_reference(
            (struct pipe_sampler_view **)&ctx->stage[shader].textures[i], NULL);
         ctx->stage[shader].textures[i] = (struct agx_sampler_view *)views[i];
      } else {
         pipe_sampler_view_reference(
            (struct pipe_sampler_view **)&ctx->stage[shader].textures[i],
            views[i]);
      }
   }

   for (; i < ctx->stage[shader].texture_count; i++) {
      pipe_sampler_view_reference(
         (struct pipe_sampler_view **)&ctx->stage[shader].textures[i], NULL);
   }
   ctx->stage[shader].texture_count = new_nr;
   ctx->stage[shader].dirty |= AGX_STAGE_DIRTY_IMAGE;
}

static void
agx_sampler_view_destroy(struct pipe_context *ctx,
                         struct pipe_sampler_view *pview)
{
   struct agx_sampler_view *view = (struct agx_sampler_view *)pview;
   pipe_resource_reference(&view->base.texture, NULL);
   FREE(view);
}

static struct pipe_surface *
agx_create_surface(struct pipe_context *ctx, struct pipe_resource *texture,
                   const struct pipe_surface *surf_tmpl)
{
   agx_legalize_compression(agx_context(ctx), agx_resource(texture),
                            surf_tmpl->format);

   struct pipe_surface *surface = CALLOC_STRUCT(pipe_surface);

   if (!surface)
      return NULL;

   unsigned level = surf_tmpl->u.tex.level;

   pipe_reference_init(&surface->reference, 1);
   pipe_resource_reference(&surface->texture, texture);

   assert(texture->target != PIPE_BUFFER && "buffers are not renderable");

   surface->context = ctx;
   surface->format = surf_tmpl->format;
   surface->nr_samples = surf_tmpl->nr_samples;
   surface->width = u_minify(texture->width0, level);
   surface->height = u_minify(texture->height0, level);
   surface->texture = texture;
   surface->u.tex.first_layer = surf_tmpl->u.tex.first_layer;
   surface->u.tex.last_layer = surf_tmpl->u.tex.last_layer;
   surface->u.tex.level = level;

   return surface;
}

static void
agx_set_clip_state(struct pipe_context *ctx,
                   const struct pipe_clip_state *state)
{
}

static void
agx_set_polygon_stipple(struct pipe_context *ctx,
                        const struct pipe_poly_stipple *state)
{
}

static void
agx_set_sample_mask(struct pipe_context *pipe, unsigned sample_mask)
{
   struct agx_context *ctx = agx_context(pipe);

   /* Optimization: At most MSAA 4x supported, so normalize to avoid pointless
    * dirtying switching between e.g. 0xFFFF and 0xFFFFFFFF masks.
    */
   unsigned new_mask = sample_mask & BITFIELD_MASK(4);

   if (ctx->sample_mask != new_mask) {
      ctx->sample_mask = new_mask;
      ctx->dirty |= AGX_DIRTY_SAMPLE_MASK;
   }
}

static void
agx_set_scissor_states(struct pipe_context *pctx, unsigned start_slot,
                       unsigned num_scissors,
                       const struct pipe_scissor_state *scissor)
{
   struct agx_context *ctx = agx_context(pctx);

   assert(start_slot == 0 && "no geometry shaders");
   assert(num_scissors == 1 && "no geometry shaders");

   ctx->scissor = *scissor;
   ctx->dirty |= AGX_DIRTY_SCISSOR_ZBIAS;
}

static void
agx_set_stencil_ref(struct pipe_context *pctx,
                    const struct pipe_stencil_ref state)
{
   struct agx_context *ctx = agx_context(pctx);
   ctx->stencil_ref = state;
   ctx->dirty |= AGX_DIRTY_STENCIL_REF;
}

static void
agx_set_viewport_states(struct pipe_context *pctx, unsigned start_slot,
                        unsigned num_viewports,
                        const struct pipe_viewport_state *vp)
{
   struct agx_context *ctx = agx_context(pctx);

   assert(start_slot == 0 && "no geometry shaders");
   assert(num_viewports == 1 && "no geometry shaders");

   ctx->dirty |= AGX_DIRTY_VIEWPORT;
   ctx->viewport = *vp;
}

static void
agx_get_scissor_extents(const struct pipe_viewport_state *vp,
                        const struct pipe_scissor_state *ss,
                        const struct pipe_framebuffer_state *fb, unsigned *minx,
                        unsigned *miny, unsigned *maxx, unsigned *maxy)
{
   float trans_x = vp->translate[0], trans_y = vp->translate[1];
   float abs_scale_x = fabsf(vp->scale[0]), abs_scale_y = fabsf(vp->scale[1]);

   /* Calculate the extent of the viewport. Note if a particular dimension of
    * the viewport is an odd number of pixels, both the translate and the scale
    * will have a fractional part of 0.5, so adding and subtracting them yields
    * an integer. Therefore we don't need to round explicitly */
   *minx = CLAMP((int)(trans_x - abs_scale_x), 0, fb->width);
   *miny = CLAMP((int)(trans_y - abs_scale_y), 0, fb->height);
   *maxx = CLAMP((int)(trans_x + abs_scale_x), 0, fb->width);
   *maxy = CLAMP((int)(trans_y + abs_scale_y), 0, fb->height);

   if (ss) {
      *minx = MAX2(ss->minx, *minx);
      *miny = MAX2(ss->miny, *miny);
      *maxx = MIN2(ss->maxx, *maxx);
      *maxy = MIN2(ss->maxy, *maxy);
   }
}

static void
agx_upload_viewport_scissor(struct agx_pool *pool, struct agx_batch *batch,
                            uint8_t **out, const struct pipe_viewport_state *vp,
                            const struct pipe_scissor_state *ss)
{
   unsigned minx, miny, maxx, maxy;

   agx_get_scissor_extents(vp, ss, &batch->key, &minx, &miny, &maxx, &maxy);

   assert(maxx > minx && maxy > miny);

   float minz, maxz;
   util_viewport_zmin_zmax(vp, false, &minz, &maxz);

   /* Allocate a new scissor descriptor */
   unsigned index = batch->scissor.size / AGX_SCISSOR_LENGTH;
   void *ptr = util_dynarray_grow_bytes(&batch->scissor, 1, AGX_SCISSOR_LENGTH);

   agx_pack(ptr, SCISSOR, cfg) {
      cfg.min_x = minx;
      cfg.min_y = miny;
      cfg.min_z = minz;
      cfg.max_x = maxx;
      cfg.max_y = maxy;
      cfg.max_z = maxz;
   }

   /* Upload state */
   struct agx_ppp_update ppp =
      agx_new_ppp_update(pool, (struct AGX_PPP_HEADER){
                                  .depth_bias_scissor = true,
                                  .region_clip = true,
                                  .viewport = true,
                               });

   agx_ppp_push(&ppp, DEPTH_BIAS_SCISSOR, cfg) {
      cfg.scissor = index;

      /* Use the current depth bias, we allocate linearly */
      unsigned count = batch->depth_bias.size / AGX_DEPTH_BIAS_LENGTH;
      cfg.depth_bias = count ? count - 1 : 0;
   };

   agx_ppp_push(&ppp, REGION_CLIP, cfg) {
      cfg.enable = true;
      cfg.min_x = minx / 32;
      cfg.min_y = miny / 32;
      cfg.max_x = DIV_ROUND_UP(maxx, 32);
      cfg.max_y = DIV_ROUND_UP(maxy, 32);
   }

   agx_ppp_push(&ppp, VIEWPORT, cfg) {
      cfg.translate_x = vp->translate[0];
      cfg.translate_y = vp->translate[1];
      cfg.translate_z = vp->translate[2];
      cfg.scale_x = vp->scale[0];
      cfg.scale_y = vp->scale[1];
      cfg.scale_z = vp->scale[2];
   }

   agx_ppp_fini(out, &ppp);
}

static void
agx_upload_depth_bias(struct agx_batch *batch,
                      const struct pipe_rasterizer_state *rast)
{
   void *ptr =
      util_dynarray_grow_bytes(&batch->depth_bias, 1, AGX_DEPTH_BIAS_LENGTH);

   agx_pack(ptr, DEPTH_BIAS, cfg) {
      cfg.depth_bias = rast->offset_units;
      cfg.slope_scale = rast->offset_scale;
      cfg.clamp = rast->offset_clamp;
   }
}

/* A framebuffer state can be reused across batches, so it doesn't make sense
 * to add surfaces to the BO list here. Instead we added them when flushing.
 */

static void
agx_set_framebuffer_state(struct pipe_context *pctx,
                          const struct pipe_framebuffer_state *state)
{
   struct agx_context *ctx = agx_context(pctx);

   if (!state)
      return;

   util_copy_framebuffer_state(&ctx->framebuffer, state);
   ctx->batch = NULL;
   agx_dirty_all(ctx);
}

/*
 * To write out render targets, each render target surface is bound as a
 * writable shader image, written with the end-of-tile program. This helper
 * constructs the internal pipe_image_view used.
 */
static struct pipe_image_view
image_view_for_surface(struct pipe_surface *surf)
{
   return (struct pipe_image_view){
      .resource = surf->texture,
      .format = surf->format,
      .access = PIPE_IMAGE_ACCESS_READ_WRITE,
      .shader_access = PIPE_IMAGE_ACCESS_READ_WRITE,
      .u.tex.single_layer_view =
         surf->u.tex.first_layer == surf->u.tex.last_layer,
      .u.tex.first_layer = surf->u.tex.first_layer,
      .u.tex.last_layer = surf->u.tex.last_layer,
      .u.tex.level = surf->u.tex.level,
   };
}

/* Similarly, to read render targets, surfaces are bound as textures */
static struct pipe_sampler_view
sampler_view_for_surface(struct pipe_surface *surf)
{
   bool layered = surf->u.tex.last_layer > surf->u.tex.first_layer;

   return (struct pipe_sampler_view){
      /* To reduce shader variants, we always use a 2D texture. For reloads of
       * arrays and cube maps, we map a single layer as a 2D image.
       */
      .target = layered ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D,
      .swizzle_r = PIPE_SWIZZLE_X,
      .swizzle_g = PIPE_SWIZZLE_Y,
      .swizzle_b = PIPE_SWIZZLE_Z,
      .swizzle_a = PIPE_SWIZZLE_W,
      .u.tex =
         {
            .first_layer = surf->u.tex.first_layer,
            .last_layer = surf->u.tex.last_layer,
            .first_level = surf->u.tex.level,
            .last_level = surf->u.tex.level,
         },
   };
}

static void
agx_pack_image_atomic_data(void *packed, struct pipe_image_view *view)
{
   struct agx_resource *tex = agx_resource(view->resource);

   if (tex->base.target == PIPE_BUFFER) {
      agx_pack(packed, PBE_BUFFER_SOFTWARE, cfg) {
         cfg.base = tex->bo->ptr.gpu + view->u.buf.offset;
      }
   } else if (tex->layout.writeable_image) {
      unsigned level = view->u.tex.level;
      unsigned blocksize_B = util_format_get_blocksize(tex->layout.format);

      agx_pack(packed, ATOMIC_SOFTWARE, cfg) {
         cfg.base =
            tex->bo->ptr.gpu +
            ail_get_layer_level_B(&tex->layout, view->u.tex.first_layer, level);

         cfg.sample_count = MAX2(util_res_sample_count(view->resource), 1);

         if (tex->layout.tiling == AIL_TILING_TWIDDLED) {
            struct ail_tile tile_size = tex->layout.tilesize_el[level];
            cfg.tile_width = tile_size.width_el;
            cfg.tile_height = tile_size.height_el;

            unsigned width_el = u_minify(tex->base.width0, level);
            cfg.tiles_per_row = DIV_ROUND_UP(width_el, tile_size.width_el);

            cfg.layer_stride_pixels = DIV_ROUND_UP(
               tex->layout.layer_stride_B, blocksize_B * cfg.sample_count);
         }
      }
   }
}

static bool
target_is_array(enum pipe_texture_target target)
{
   switch (target) {
   case PIPE_TEXTURE_3D:
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_1D_ARRAY:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_CUBE_ARRAY:
      return true;
   default:
      return false;
   }
}

static void
agx_batch_upload_pbe(struct agx_batch *batch, struct agx_pbe_packed *out,
                     struct pipe_image_view *view, bool block_access,
                     bool arrays_as_2d)
{
   struct agx_resource *tex = agx_resource(view->resource);
   const struct util_format_description *desc =
      util_format_description(view->format);
   enum pipe_texture_target target = tex->base.target;
   bool is_buffer = (target == PIPE_BUFFER);

   if (!is_buffer && view->u.tex.single_layer_view)
      target = PIPE_TEXTURE_2D;

   /* To reduce shader variants, spilled layered render targets are accessed as
    * 2D Arrays regardless of the actual target, so force in that case.
    */
   if (arrays_as_2d && target_is_array(target))
      target = PIPE_TEXTURE_2D_ARRAY;

   unsigned level = is_buffer ? 0 : view->u.tex.level;
   unsigned layer = is_buffer ? 0 : view->u.tex.first_layer;

   agx_pack(out, PBE, cfg) {
      cfg.dimension =
         agx_translate_tex_dim(target, util_res_sample_count(&tex->base));
      cfg.layout = agx_translate_layout(tex->layout.tiling);
      cfg.channels = agx_pixel_format[view->format].channels;
      cfg.type = agx_pixel_format[view->format].type;
      cfg.srgb = util_format_is_srgb(view->format);

      assert(desc->nr_channels >= 1 && desc->nr_channels <= 4);
      cfg.swizzle_r = agx_channel_from_pipe(desc->swizzle[0]) & 3;

      if (desc->nr_channels >= 2)
         cfg.swizzle_g = agx_channel_from_pipe(desc->swizzle[1]) & 3;

      if (desc->nr_channels >= 3)
         cfg.swizzle_b = agx_channel_from_pipe(desc->swizzle[2]) & 3;

      if (desc->nr_channels >= 4)
         cfg.swizzle_a = agx_channel_from_pipe(desc->swizzle[3]) & 3;

      cfg.buffer = agx_map_texture_gpu(tex, layer);
      cfg.unk_mipmapped = tex->mipmapped;

      if (is_buffer) {
         unsigned size_el =
            agx_texture_buffer_size_el(view->format, view->u.buf.size);

         /* Buffers uniquely have offsets (in bytes, not texels) */
         cfg.buffer += view->u.buf.offset;

         /* Use a 2D texture to increase the maximum size */
         cfg.width = 1024;
         cfg.height = DIV_ROUND_UP(size_el, cfg.width);
         cfg.level = 0;
         cfg.stride = (cfg.width * util_format_get_blocksize(view->format)) - 4;
         cfg.layers = 1;
         cfg.levels = 1;
      } else if (util_res_sample_count(&tex->base) > 1 && !block_access) {
         /* Multisampled images are bound like buffer textures, with
          * addressing arithmetic to determine the texel to write.
          *
          * Note that the end-of-tile program uses real multisample images with
          * image_write_block instructions.
          */
         unsigned blocksize_B = util_format_get_blocksize(view->format);
         unsigned size_px = tex->layout.size_B / blocksize_B;

         cfg.dimension = AGX_TEXTURE_DIMENSION_2D;
         cfg.layout = AGX_LAYOUT_LINEAR;
         cfg.width = 1024;
         cfg.height = DIV_ROUND_UP(size_px, cfg.width);
         cfg.stride = (cfg.width * blocksize_B) - 4;
         cfg.layers = 1;
         cfg.levels = 1;

         cfg.buffer += tex->layout.level_offsets_B[level];
         cfg.level = 0;
      } else {
         cfg.width = view->resource->width0;
         cfg.height = view->resource->height0;
         cfg.level = level;

         unsigned layers = view->u.tex.last_layer - layer + 1;

         if (tex->layout.tiling == AIL_TILING_LINEAR &&
             (tex->base.target == PIPE_TEXTURE_1D_ARRAY ||
              tex->base.target == PIPE_TEXTURE_2D_ARRAY)) {

            cfg.depth_linear = layers;
            cfg.layer_stride_linear = (tex->layout.layer_stride_B - 0x80);
            cfg.extended = true;
         } else {
            assert((tex->layout.tiling != AIL_TILING_LINEAR) || (layers == 1));
            cfg.layers = layers;
         }

         if (tex->layout.tiling == AIL_TILING_LINEAR) {
            cfg.stride = ail_get_linear_stride_B(&tex->layout, level) - 4;
            cfg.levels = 1;
         } else {
            cfg.page_aligned_layers = tex->layout.page_aligned_layers;
            cfg.levels = tex->base.last_level + 1;
         }

         if (tex->base.nr_samples > 1)
            cfg.samples = agx_translate_sample_count(tex->base.nr_samples);
      }

      if (ail_is_compressed(&tex->layout)) {
         cfg.compressed_1 = true;
         cfg.extended = true;

         cfg.acceleration_buffer =
            agx_map_texture_gpu(tex, 0) + tex->layout.metadata_offset_B +
            (layer * tex->layout.compression_layer_stride_B);
      }

      /* When the descriptor isn't extended architecturally, we can use the last
       * 8 bytes as a sideband. We use it to provide metadata for image atomics.
       */
      if (!cfg.extended) {
         bool compact =
            (target == PIPE_BUFFER || target == PIPE_TEXTURE_1D ||
             target == PIPE_TEXTURE_2D || target == PIPE_TEXTURE_RECT);

         if (compact) {
            struct agx_atomic_software_packed packed;
            agx_pack_image_atomic_data(&packed, view);

            STATIC_ASSERT(sizeof(cfg.software_defined) == 8);
            memcpy(&cfg.software_defined, packed.opaque, 8);
         } else {
            struct agx_ptr desc = agx_pool_alloc_aligned(
               &batch->pool, AGX_ATOMIC_SOFTWARE_LENGTH, 8);

            agx_pack_image_atomic_data(desc.cpu, view);
            cfg.software_defined = desc.gpu;
         }
      }
   };
}

/* Likewise constant buffers, textures, and samplers are handled in a common
 * per-draw path, with dirty tracking to reduce the costs involved.
 */

static void
agx_set_constant_buffer(struct pipe_context *pctx, enum pipe_shader_type shader,
                        uint index, bool take_ownership,
                        const struct pipe_constant_buffer *cb)
{
   struct agx_context *ctx = agx_context(pctx);
   struct agx_stage *s = &ctx->stage[shader];
   struct pipe_constant_buffer *constants = &s->cb[index];

   util_copy_constant_buffer(&s->cb[index], cb, take_ownership);

   /* Upload user buffer immediately */
   if (constants->user_buffer && !constants->buffer) {
      u_upload_data(ctx->base.const_uploader, 0, constants->buffer_size, 64,
                    constants->user_buffer, &constants->buffer_offset,
                    &constants->buffer);
   }

   unsigned mask = (1 << index);

   if (cb)
      s->cb_mask |= mask;
   else
      s->cb_mask &= ~mask;

   ctx->stage[shader].dirty |= AGX_STAGE_DIRTY_CONST;
}

static void
agx_surface_destroy(struct pipe_context *ctx, struct pipe_surface *surface)
{
   pipe_resource_reference(&surface->texture, NULL);
   FREE(surface);
}

static void
agx_delete_state(struct pipe_context *ctx, void *state)
{
   FREE(state);
}

/* BOs added to the batch in the uniform upload path */

static void
agx_set_vertex_buffers(struct pipe_context *pctx, unsigned count,
                       unsigned unbind_num_trailing_slots, bool take_ownership,
                       const struct pipe_vertex_buffer *buffers)
{
   struct agx_context *ctx = agx_context(pctx);

   util_set_vertex_buffers_mask(ctx->vertex_buffers, &ctx->vb_mask, buffers,
                                count, unbind_num_trailing_slots,
                                take_ownership);

   ctx->dirty |= AGX_DIRTY_VERTEX;
}

static void *
agx_create_vertex_elements(struct pipe_context *ctx, unsigned count,
                           const struct pipe_vertex_element *state)
{
   assert(count <= AGX_MAX_ATTRIBS);

   struct agx_attribute *attribs = calloc(sizeof(*attribs), AGX_MAX_ATTRIBS);
   for (unsigned i = 0; i < count; ++i) {
      const struct pipe_vertex_element ve = state[i];

      const struct util_format_description *desc =
         util_format_description(ve.src_format);
      unsigned chan_size = desc->channel[0].size / 8;
      assert((ve.src_offset & (chan_size - 1)) == 0);

      attribs[i] = (struct agx_attribute){
         .buf = ve.vertex_buffer_index,
         .src_offset = ve.src_offset,
         .stride = ve.src_stride,
         .format = ve.src_format,
         .divisor = ve.instance_divisor,
      };
   }

   return attribs;
}

static void
agx_bind_vertex_elements_state(struct pipe_context *pctx, void *cso)
{
   struct agx_context *ctx = agx_context(pctx);
   ctx->attributes = cso;
   ctx->dirty |= AGX_DIRTY_VERTEX;
}

static uint32_t
asahi_vs_shader_key_hash(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct asahi_vs_shader_key));
}

static bool
asahi_vs_shader_key_equal(const void *a, const void *b)
{
   return memcmp(a, b, sizeof(struct asahi_vs_shader_key)) == 0;
}

static uint32_t
asahi_fs_shader_key_hash(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct asahi_fs_shader_key));
}

static bool
asahi_fs_shader_key_equal(const void *a, const void *b)
{
   return memcmp(a, b, sizeof(struct asahi_fs_shader_key)) == 0;
}

/* No compute variants */
static uint32_t
asahi_cs_shader_key_hash(const void *key)
{
   return 0;
}

static bool
asahi_cs_shader_key_equal(const void *a, const void *b)
{
   return true;
}

static unsigned
agx_find_linked_slot(struct agx_varyings_vs *vs, struct agx_varyings_fs *fs,
                     gl_varying_slot slot, unsigned offset)
{
   assert(offset < 4);
   assert(slot != VARYING_SLOT_PNTC && "point coords aren't linked");

   if (slot == VARYING_SLOT_POS) {
      if (offset == 3) {
         return 0; /* W */
      } else if (offset == 2) {
         assert(fs->reads_z);
         return 1; /* Z */
      } else {
         unreachable("gl_Position.xy are not varyings");
      }
   }

   unsigned vs_index = vs->slots[slot];

   /* If the layer is read but not written, its value will be ignored by the
    * agx_nir_predicate_layer_id lowering, so read garbage.
    */
   if (vs_index >= vs->nr_index && slot == VARYING_SLOT_LAYER)
      return 0;

   assert(vs_index >= 4 && "gl_Position should have been the first 4 slots");
   assert(vs_index < vs->nr_index &&
          "varyings not written by vertex shader are undefined");
   assert((vs_index < vs->base_index_fp16) ==
             ((vs_index + offset) < vs->base_index_fp16) &&
          "a given varying must have a consistent type");

   unsigned vs_user_index = (vs_index + offset) - 4;

   if (fs->reads_z)
      return vs_user_index + 2;
   else
      return vs_user_index + 1;
}

static unsigned
agx_num_general_outputs(struct agx_varyings_vs *vs)
{
   unsigned nr_vs = vs->nr_index;
   bool writes_psiz = vs->slots[VARYING_SLOT_PSIZ] < nr_vs;

   assert(nr_vs >= 4 && "gl_Position must be written");
   if (writes_psiz)
      assert(nr_vs >= 5 && "gl_PointSize is written");

   return nr_vs - (writes_psiz ? 5 : 4);
}

static uint32_t
agx_link_varyings_vs_fs(struct agx_pool *pool, struct agx_varyings_vs *vs,
                        struct agx_varyings_fs *fs, bool first_provoking_vertex)
{
   /* If there are no bindings, there's nothing to emit */
   if (fs->nr_bindings == 0)
      return 0;

   size_t linkage_size =
      AGX_CF_BINDING_HEADER_LENGTH + (fs->nr_bindings * AGX_CF_BINDING_LENGTH);

   void *tmp = alloca(linkage_size);
   struct agx_cf_binding_header_packed *header = tmp;
   struct agx_cf_binding_packed *bindings = (void *)(header + 1);

   unsigned nr_slots = agx_num_general_outputs(vs) + 1 + (fs->reads_z ? 1 : 0);

   agx_pack(header, CF_BINDING_HEADER, cfg) {
      cfg.number_of_32_bit_slots = nr_slots;
      cfg.number_of_coefficient_registers = fs->nr_cf;
   }

   for (unsigned i = 0; i < fs->nr_bindings; ++i) {
      agx_pack(bindings + i, CF_BINDING, cfg) {
         cfg.base_coefficient_register = fs->bindings[i].cf_base;
         cfg.components = fs->bindings[i].count;
         cfg.perspective = fs->bindings[i].perspective;

         cfg.shade_model = fs->bindings[i].smooth ? AGX_SHADE_MODEL_GOURAUD
                           : first_provoking_vertex
                              ? AGX_SHADE_MODEL_FLAT_VERTEX_0
                              : AGX_SHADE_MODEL_FLAT_VERTEX_2;

         if (fs->bindings[i].slot == VARYING_SLOT_PNTC) {
            assert(fs->bindings[i].offset == 0);
            cfg.point_sprite = true;
         } else {
            cfg.base_slot = agx_find_linked_slot(vs, fs, fs->bindings[i].slot,
                                                 fs->bindings[i].offset);

            assert(cfg.base_slot + cfg.components <= nr_slots &&
                   "overflow slots");
         }

         if (fs->bindings[i].slot == VARYING_SLOT_POS) {
            if (fs->bindings[i].offset == 2)
               cfg.fragcoord_z = true;
            else
               assert(!cfg.perspective && "W must not be perspective divided");
         }

         assert(cfg.base_coefficient_register + cfg.components <= fs->nr_cf &&
                "overflowed coefficient registers");
      }
   }

   struct agx_ptr ptr = agx_pool_alloc_aligned(pool, (3 * linkage_size), 256);
   assert(ptr.gpu < (1ull << 32) && "varyings must be in low memory");

   /* I don't understand why the data structures are repeated thrice */
   for (unsigned i = 0; i < 3; ++i) {
      memcpy(((uint8_t *)ptr.cpu) + (i * linkage_size), (uint8_t *)tmp,
             linkage_size);
   }

   return ptr.gpu;
}

/* Does not take ownership of key. Clones if necessary. */
static struct agx_compiled_shader *
agx_compile_variant(struct agx_device *dev, struct agx_uncompiled_shader *so,
                    struct util_debug_callback *debug,
                    union asahi_shader_key *key_)
{
   struct agx_compiled_shader *compiled = CALLOC_STRUCT(agx_compiled_shader);
   struct util_dynarray binary;
   util_dynarray_init(&binary, NULL);

   struct blob_reader reader;
   blob_reader_init(&reader, so->serialized_nir.data, so->serialized_nir.size);
   nir_shader *nir = nir_deserialize(NULL, &agx_nir_options, &reader);

   /* This can happen at inopportune times and cause jank, log it */
   perf_debug(dev, "Compiling shader variant #%u",
              _mesa_hash_table_num_entries(so->variants));

   bool force_translucent = false;

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      struct asahi_vs_shader_key *key = &key_->vs;

      NIR_PASS_V(nir, agx_nir_lower_vbo, &key->vbuf);

      if (key->xfb.active && nir->xfb_info != NULL)
         NIR_PASS_V(nir, agx_nir_lower_xfb, &key->xfb);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      struct asahi_fs_shader_key *key = &key_->fs;

      struct agx_tilebuffer_layout tib = agx_build_tilebuffer_layout(
         key->rt_formats, key->nr_cbufs, key->nr_samples, key->layered);

      if (dev->debug & AGX_DBG_SMALLTILE)
         tib.tile_size = (struct agx_tile_size){16, 16};

      nir_lower_blend_options opts = {
         .scalar_blend_const = true,
         .logicop_enable = key->blend.logicop_enable,
         .logicop_func = key->blend.logicop_func,
      };

      static_assert(ARRAY_SIZE(opts.format) == PIPE_MAX_COLOR_BUFS,
                    "max RTs out of sync");

      for (unsigned i = 0; i < PIPE_MAX_COLOR_BUFS; ++i)
         opts.format[i] = key->rt_formats[i];

      memcpy(opts.rt, key->blend.rt, sizeof(opts.rt));

      /* It's more efficient to use masked stores (with
       * agx_nir_lower_tilebuffer) than to emulate colour masking with
       * nir_lower_blend.
       */
      uint8_t colormasks[PIPE_MAX_COLOR_BUFS] = {0};

      for (unsigned i = 0; i < PIPE_MAX_COLOR_BUFS; ++i) {
         /* TODO: Flakes some dEQPs, seems to invoke UB. Revisit later.
          * dEQP-GLES2.functional.fragment_ops.interaction.basic_shader.77
          * dEQP-GLES2.functional.fragment_ops.interaction.basic_shader.98
          */
         if (0 /* agx_tilebuffer_supports_mask(&tib, i) */) {
            colormasks[i] = key->blend.rt[i].colormask;
            opts.rt[i].colormask = BITFIELD_MASK(4);
         } else {
            colormasks[i] = BITFIELD_MASK(4);
         }

         /* If not all bound RTs are fully written to, we need to force
          * translucent pass type. agx_nir_lower_tilebuffer will take
          * care of this for its own colormasks input.
          */
         unsigned comps = util_format_get_nr_components(key->rt_formats[i]);
         if (i < key->nr_cbufs &&
             (opts.rt[i].colormask & BITFIELD_MASK(comps)) !=
                BITFIELD_MASK(comps))
            force_translucent = true;
      }

      /* Clip plane lowering creates discard instructions, so run that before
       * lowering discards. Note: this introduces extra loads from the clip
       * plane outputs, but they use smooth interpolation so it does not affect
       * the flat/linear masks that get propagated back to the VS.
       */
      if (key->clip_plane_enable) {
         NIR_PASS_V(nir, nir_lower_clip_fs, key->clip_plane_enable, false);
      }

      /* Discards must be lowering before lowering MSAA to handle discards */
      NIR_PASS_V(nir, agx_nir_lower_discard_zs_emit);

      /* Alpha-to-coverage must be lowered before alpha-to-one */
      if (key->blend.alpha_to_coverage)
         NIR_PASS_V(nir, agx_nir_lower_alpha_to_coverage, tib.nr_samples);

      /* Alpha-to-one must be lowered before blending */
      if (key->blend.alpha_to_one)
         NIR_PASS_V(nir, agx_nir_lower_alpha_to_one);

      NIR_PASS_V(nir, nir_lower_blend, &opts);

      /* XXX: don't replicate this all over the driver */
      unsigned rt_spill_base = BITSET_LAST_BIT(nir->info.textures_used) +
                               (2 * BITSET_LAST_BIT(nir->info.images_used));
      unsigned rt_spill = rt_spill_base;
      NIR_PASS_V(nir, agx_nir_lower_tilebuffer, &tib, colormasks, &rt_spill,
                 &force_translucent, false);

      /* If anything spilled, we have bindless texture */
      so->internal_bindless |= (rt_spill != rt_spill_base);

      NIR_PASS_V(nir, agx_nir_lower_sample_intrinsics);
      NIR_PASS_V(nir, agx_nir_lower_monolithic_msaa,
                 &(struct agx_msaa_state){
                    .nr_samples = tib.nr_samples,
                    .api_sample_mask = key->api_sample_mask,
                 });

      if (key->sprite_coord_enable) {
         NIR_PASS_V(nir, nir_lower_texcoord_replace_late,
                    key->sprite_coord_enable,
                    false /* point coord is sysval */);
      }

      NIR_PASS_V(nir, agx_nir_predicate_layer_id);
   }

   struct agx_shader_key base_key = {
      .needs_g13x_coherency = (dev->params.gpu_generation == 13 &&
                               dev->params.num_clusters_total > 1) ||
                              dev->params.num_dies > 1,
   };

   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      base_key.fs.nr_samples = key_->fs.nr_samples;

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      base_key.vs.outputs_flat_shaded = key_->vs.outputs_flat_shaded;
      base_key.vs.outputs_linear_shaded = key_->vs.outputs_linear_shaded;
   }

   NIR_PASS_V(nir, agx_nir_lower_sysvals);
   NIR_PASS_V(nir, agx_nir_layout_uniforms, so->internal_bindless, compiled,
              &base_key.reserved_preamble);

   agx_compile_shader_nir(nir, &base_key, debug, &binary, &compiled->info);

   /* reads_tib => Translucent pass type */
   compiled->info.reads_tib |= force_translucent;

   /* Could be optimized to use non-translucent pass types with the appropriate
    * HSR configuration, but that mechanism is not yet understood. Warn that
    * we're leaving perf on the table when used.
    */
   if (force_translucent)
      perf_debug(dev, "Translucency forced due to colour masking");

   if (binary.size) {
      compiled->bo = agx_bo_create(dev, binary.size,
                                   AGX_BO_EXEC | AGX_BO_LOW_VA, "Executable");

      memcpy(compiled->bo->ptr.cpu, binary.data, binary.size);
   }

   ralloc_free(nir);
   util_dynarray_fini(&binary);

   return compiled;
}

static struct agx_compiled_shader *
agx_get_shader_variant(struct agx_screen *screen,
                       struct agx_uncompiled_shader *so,
                       struct util_debug_callback *debug,
                       union asahi_shader_key *key)
{
   struct agx_compiled_shader *compiled =
      agx_disk_cache_retrieve(screen, so, key);

   if (!compiled) {
      compiled = agx_compile_variant(&screen->dev, so, debug, key);
      agx_disk_cache_store(screen->disk_cache, so, key, compiled);
   }

   /* key may be destroyed after we return, so clone it before using it as a
    * hash table key. The clone is logically owned by the hash table.
    */
   union asahi_shader_key *cloned_key =
      rzalloc(so->variants, union asahi_shader_key);

   if (so->type == PIPE_SHADER_FRAGMENT) {
      memcpy(cloned_key, key, sizeof(struct asahi_fs_shader_key));
   } else if (so->type == PIPE_SHADER_VERTEX) {
      memcpy(cloned_key, key, sizeof(struct asahi_vs_shader_key));
   } else {
      assert(gl_shader_stage_is_compute(so->type));
      /* No key */
   }

   _mesa_hash_table_insert(so->variants, cloned_key, compiled);

   return compiled;
}

static void
agx_shader_initialize(struct agx_device *dev, struct agx_uncompiled_shader *so,
                      nir_shader *nir, bool support_lod_bias)
{
   if (nir->info.stage == MESA_SHADER_KERNEL)
      nir->info.stage = MESA_SHADER_COMPUTE;

   so->type = pipe_shader_type_from_mesa(nir->info.stage);

   nir_lower_robust_access_options robustness = {
      /* Images accessed through the texture or PBE hardware are robust, so we
       * don't set lower_image. However, buffer images and image atomics are
       * lowered so require robustness lowering.
       */
      .lower_buffer_image = true,
      .lower_image_atomic = true,
   };

   /* We need to lower robustness before bindings, since robustness lowering
    * affects the bindings used.
    */
   NIR_PASS_V(nir, nir_lower_robust_access, &robustness);

   /* Similarly, we need to do early texture lowering before bindings */
   NIR_PASS_V(nir, agx_nir_lower_texture_early);

   /* We need to lower binding tables before calling agx_preprocess_nir, since
    * that does texture lowering that needs to know the binding model.
    */
   NIR_PASS_V(nir, agx_nir_lower_bindings, &so->internal_bindless);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* Lower to maximum colour buffers, the excess stores will get cleaned up
       * by tilebuffer lowering so they won't become real shader code. However,
       * that depends on the shader key which we don't have at this point.
       */
      NIR_PASS_V(nir, nir_lower_fragcolor, 8);
   }

   bool allow_mediump = !(dev->debug & AGX_DBG_NO16);
   agx_preprocess_nir(nir, support_lod_bias, allow_mediump, &so->info);

   blob_init(&so->serialized_nir);
   nir_serialize(&so->serialized_nir, nir, true);
   _mesa_sha1_compute(so->serialized_nir.data, so->serialized_nir.size,
                      so->nir_sha1);

   so->has_xfb_info = (nir->xfb_info != NULL);
}

static void *
agx_create_shader_state(struct pipe_context *pctx,
                        const struct pipe_shader_state *cso)
{
   struct agx_context *ctx = agx_context(pctx);
   struct agx_uncompiled_shader *so =
      rzalloc(NULL, struct agx_uncompiled_shader);
   struct agx_device *dev = agx_device(pctx->screen);

   if (!so)
      return NULL;

   so->base = *cso;

   nir_shader *nir = cso->type == PIPE_SHADER_IR_NIR
                        ? cso->ir.nir
                        : tgsi_to_nir(cso->tokens, pctx->screen, false);

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      so->variants = _mesa_hash_table_create(so, asahi_vs_shader_key_hash,
                                             asahi_vs_shader_key_equal);
   } else {
      so->variants = _mesa_hash_table_create(so, asahi_fs_shader_key_hash,
                                             asahi_fs_shader_key_equal);
   }

   agx_shader_initialize(dev, so, nir, ctx->support_lod_bias);

   /* We're done with the NIR, throw it away */
   ralloc_free(nir);
   nir = NULL;

   /* For shader-db, precompile a shader with a default key. This could be
    * improved but hopefully this is acceptable for now.
    */
   if (dev->debug & AGX_DBG_PRECOMPILE) {
      union asahi_shader_key key = {0};

      switch (so->type) {
      case PIPE_SHADER_VERTEX: {
         key.vs.vbuf.count = AGX_MAX_VBUFS;
         for (unsigned i = 0; i < AGX_MAX_VBUFS; ++i) {
            key.vs.vbuf.attributes[i] = (struct agx_attribute){
               .buf = i,
               .stride = 16,
               .format = PIPE_FORMAT_R32G32B32A32_FLOAT,
            };
         }

         break;
      }
      case PIPE_SHADER_FRAGMENT:
         key.fs.nr_cbufs = 1;
         key.fs.nr_samples = 1;
         for (unsigned i = 0; i < key.fs.nr_cbufs; ++i) {
            key.fs.rt_formats[i] = PIPE_FORMAT_R8G8B8A8_UNORM;
            key.fs.blend.rt[i].colormask = 0xF;

            const nir_lower_blend_channel replace = {
               .func = PIPE_BLEND_ADD,
               .src_factor = PIPE_BLENDFACTOR_ONE,
               .dst_factor = PIPE_BLENDFACTOR_ZERO,
            };

            key.fs.blend.rt[i].rgb = replace;
            key.fs.blend.rt[i].alpha = replace;
         }
         break;
      default:
         unreachable("Unknown shader stage in shader-db precompile");
      }

      agx_compile_variant(dev, so, &pctx->debug, &key);
   }

   return so;
}

static void *
agx_create_compute_state(struct pipe_context *pctx,
                         const struct pipe_compute_state *cso)
{
   struct agx_context *ctx = agx_context(pctx);
   struct agx_device *dev = agx_device(pctx->screen);
   struct agx_uncompiled_shader *so =
      rzalloc(NULL, struct agx_uncompiled_shader);

   if (!so)
      return NULL;

   so->variants = _mesa_hash_table_create(so, asahi_cs_shader_key_hash,
                                          asahi_cs_shader_key_equal);

   union asahi_shader_key key = {0};

   assert(cso->ir_type == PIPE_SHADER_IR_NIR && "TGSI kernels unsupported");
   nir_shader *nir = (void *)cso->prog;

   agx_shader_initialize(dev, so, nir, ctx->support_lod_bias);
   agx_get_shader_variant(agx_screen(pctx->screen), so, &pctx->debug, &key);

   /* We're done with the NIR, throw it away */
   ralloc_free(nir);
   return so;
}

static void
agx_get_compute_state_info(struct pipe_context *pctx, void *cso,
                           struct pipe_compute_state_object_info *info)
{
   union asahi_shader_key key = {0};
   struct agx_compiled_shader *so =
      agx_get_shader_variant(agx_screen(pctx->screen), cso, &pctx->debug, &key);

   info->max_threads =
      agx_occupancy_for_register_count(so->info.nr_gprs).max_threads;
   info->private_memory = 0;
   info->preferred_simd_size = 32;
   info->simd_sizes = 32;

   /* HACK: Clamp max_threads to what we advertise. When we fix the CAP
    * situation around block sizes, we can drop this.
    */
   info->max_threads = MIN2(info->max_threads, 256);
}

/* Does not take ownership of key. Clones if necessary. */
static bool
agx_update_shader(struct agx_context *ctx, struct agx_compiled_shader **out,
                  enum pipe_shader_type stage, union asahi_shader_key *key)
{
   struct agx_uncompiled_shader *so = ctx->stage[stage].shader;
   assert(so != NULL);

   struct hash_entry *he = _mesa_hash_table_search(so->variants, key);

   if (he) {
      if ((*out) == he->data)
         return false;

      *out = he->data;
      return true;
   }

   struct agx_screen *screen = agx_screen(ctx->base.screen);
   *out = agx_get_shader_variant(screen, so, &ctx->base.debug, key);
   return true;
}

static bool
agx_update_vs(struct agx_context *ctx)
{
   /* Only proceed if the shader or anything the key depends on changes
    *
    * vb_mask, attributes, vertex_buffers: VERTEX
    * streamout.active: XFB
    * outputs_{flat,linear}_shaded: FS_PROG
    */
   if (!(ctx->dirty & (AGX_DIRTY_VS_PROG | AGX_DIRTY_VERTEX | AGX_DIRTY_XFB |
                       AGX_DIRTY_FS_PROG)))
      return false;

   struct asahi_vs_shader_key key = {
      .vbuf.count = util_last_bit(ctx->vb_mask),
      .xfb = ctx->streamout.key,
      .outputs_flat_shaded =
         ctx->stage[PIPE_SHADER_FRAGMENT].shader->info.inputs_flat_shaded,
      .outputs_linear_shaded =
         ctx->stage[PIPE_SHADER_FRAGMENT].shader->info.inputs_linear_shaded,
   };

   memcpy(key.vbuf.attributes, ctx->attributes,
          sizeof(key.vbuf.attributes[0]) * AGX_MAX_ATTRIBS);

   return agx_update_shader(ctx, &ctx->vs, PIPE_SHADER_VERTEX,
                            (union asahi_shader_key *)&key);
}

static bool
agx_update_fs(struct agx_batch *batch)
{
   struct agx_context *ctx = batch->ctx;

   /* Only proceed if the shader or anything the key depends on changes
    *
    * batch->key: implicitly dirties everything, no explicit check
    * rast: RS
    * blend: BLEND
    * sample_mask: SAMPLE_MASK
    */
   if (!(ctx->dirty & (AGX_DIRTY_FS_PROG | AGX_DIRTY_RS | AGX_DIRTY_BLEND |
                       AGX_DIRTY_SAMPLE_MASK)))
      return false;

   unsigned nr_samples = util_framebuffer_get_num_samples(&batch->key);
   bool msaa = ctx->rast->base.multisample;

   struct asahi_fs_shader_key key = {
      .nr_cbufs = batch->key.nr_cbufs,
      .clip_plane_enable = ctx->rast->base.clip_plane_enable,
      .nr_samples = nr_samples,
      .layered = util_framebuffer_get_num_layers(&batch->key) > 1,
      .multisample = msaa,

      /* Only lower sample mask if at least one sample is masked out */
      .api_sample_mask =
         msaa && (~ctx->sample_mask & BITFIELD_MASK(nr_samples)),
   };

   if (batch->reduced_prim == MESA_PRIM_POINTS)
      key.sprite_coord_enable = ctx->rast->base.sprite_coord_enable;

   for (unsigned i = 0; i < key.nr_cbufs; ++i) {
      struct pipe_surface *surf = batch->key.cbufs[i];

      key.rt_formats[i] = surf ? surf->format : PIPE_FORMAT_NONE;
   }

   memcpy(&key.blend, ctx->blend, sizeof(key.blend));

   /* Normalize key */
   if (!key.multisample)
      key.blend.alpha_to_coverage = false;

   return agx_update_shader(ctx, &ctx->fs, PIPE_SHADER_FRAGMENT,
                            (union asahi_shader_key *)&key);
}

static void
agx_bind_shader_state(struct pipe_context *pctx, void *cso)
{
   if (!cso)
      return;

   struct agx_context *ctx = agx_context(pctx);
   struct agx_uncompiled_shader *so = cso;

   if (so->type == PIPE_SHADER_VERTEX)
      ctx->dirty |= AGX_DIRTY_VS_PROG;
   else if (so->type == PIPE_SHADER_FRAGMENT)
      ctx->dirty |= AGX_DIRTY_FS_PROG;

   ctx->stage[so->type].shader = so;
}

static void
agx_delete_compiled_shader(struct hash_entry *ent)
{
   struct agx_compiled_shader *so = ent->data;
   agx_bo_unreference(so->bo);
   FREE(so);
}

static void
agx_delete_shader_state(struct pipe_context *ctx, void *cso)
{
   struct agx_uncompiled_shader *so = cso;
   _mesa_hash_table_destroy(so->variants, agx_delete_compiled_shader);
   blob_finish(&so->serialized_nir);
   ralloc_free(so);
}

static unsigned
sampler_count(struct agx_context *ctx, struct agx_compiled_shader *cs,
              enum pipe_shader_type stage)
{
   unsigned sampler_count = ctx->stage[stage].sampler_count;

   if (cs->info.uses_txf)
      sampler_count = MAX2(sampler_count, cs->info.txf_sampler + 1);

   return sampler_count;
}

static inline enum agx_sampler_states
translate_sampler_state_count(struct agx_context *ctx,
                              struct agx_compiled_shader *cs,
                              enum pipe_shader_type stage)
{
   return agx_translate_sampler_state_count(sampler_count(ctx, cs, stage),
                                            ctx->stage[stage].custom_borders);
}

/*
 * Despite having both a layout *and* a flag that I only see Metal use with null
 * textures, AGX doesn't seem to have "real" null textures. Instead we need to
 * bind an arbitrary address and throw away the results to read all 0's.
 * Accordingly, the caller must pass some address that lives at least as long as
 * the texture descriptor itself.
 */
static void
agx_set_null_texture(struct agx_texture_packed *tex, uint64_t valid_address)
{
   agx_pack(tex, TEXTURE, cfg) {
      cfg.layout = AGX_LAYOUT_NULL;
      cfg.channels = AGX_CHANNELS_R8;
      cfg.type = AGX_TEXTURE_TYPE_UNORM /* don't care */;
      cfg.swizzle_r = AGX_CHANNEL_0;
      cfg.swizzle_g = AGX_CHANNEL_0;
      cfg.swizzle_b = AGX_CHANNEL_0;
      cfg.swizzle_a = AGX_CHANNEL_0;
      cfg.address = valid_address;
      cfg.null = true;
   }
}

static uint32_t
agx_nr_tex_descriptors_without_spilled_rts(const struct agx_compiled_shader *cs)
{
   /* 2 descriptors per image, 1 descriptor per texture */
   return cs->info.nr_bindful_textures + (2 * cs->info.nr_bindful_images);
}

static uint32_t
agx_nr_tex_descriptors(struct agx_batch *batch, enum pipe_shader_type stage,
                       const struct agx_compiled_shader *cs)
{
   unsigned n = agx_nr_tex_descriptors_without_spilled_rts(cs);

   /* We add on texture/PBE descriptors for spilled render targets */
   bool spilled_rt = stage == PIPE_SHADER_FRAGMENT &&
                     agx_tilebuffer_spills(&batch->tilebuffer_layout);
   if (spilled_rt)
      n += (batch->key.nr_cbufs * 2);

   return n;
}

/*
 * For spilled render targets, upload a texture/PBE pair for each surface to
 * allow loading/storing to the render target from the shader.
 */
static void
agx_upload_spilled_rt_descriptors(struct agx_texture_packed *out,
                                  struct agx_batch *batch)
{
   for (unsigned rt = 0; rt < batch->key.nr_cbufs; ++rt) {
      struct agx_texture_packed *texture = out + (2 * rt);
      struct agx_pbe_packed *pbe = (struct agx_pbe_packed *)(texture + 1);

      struct pipe_surface *surf = batch->key.cbufs[rt];
      if (!surf)
         continue;

      struct agx_resource *rsrc = agx_resource(surf->texture);
      struct pipe_image_view view = image_view_for_surface(surf);
      struct pipe_sampler_view sampler_view = sampler_view_for_surface(surf);

      agx_pack_texture(texture, rsrc, surf->format, &sampler_view, true);
      agx_batch_upload_pbe(batch, pbe, &view, false, true);
   }
}

static void
agx_upload_textures(struct agx_batch *batch, struct agx_compiled_shader *cs,
                    enum pipe_shader_type stage)
{
   struct agx_context *ctx = batch->ctx;
   unsigned nr_textures = cs->info.nr_bindful_textures;
   unsigned nr_active_textures = ctx->stage[stage].texture_count;
   unsigned nr_tex_descriptors = agx_nr_tex_descriptors(batch, stage, cs);
   unsigned nr_images = cs->info.nr_bindful_images;

   struct agx_ptr T_tex = agx_pool_alloc_aligned(
      &batch->pool, AGX_TEXTURE_LENGTH * nr_tex_descriptors, 64);

   struct agx_texture_packed *textures = T_tex.cpu;

   for (unsigned i = 0; i < MIN2(nr_textures, nr_active_textures); ++i) {
      struct agx_sampler_view *tex = ctx->stage[stage].textures[i];

      if (tex == NULL) {
         agx_set_null_texture(&textures[i], T_tex.gpu);
         continue;
      }

      struct agx_resource *rsrc = tex->rsrc;
      agx_batch_reads(batch, tex->rsrc);

      unsigned first_layer =
         (tex->base.target == PIPE_BUFFER) ? 0 : tex->base.u.tex.first_layer;

      /* Without the address */
      struct agx_texture_packed texture = tex->desc;

      /* Just the address */
      struct agx_texture_packed texture2;
      agx_pack(&texture2, TEXTURE, cfg) {
         cfg.address = agx_map_texture_gpu(rsrc, first_layer);

         if (rsrc->base.target == PIPE_BUFFER)
            cfg.address += tex->base.u.buf.offset;

         if (ail_is_compressed(&rsrc->layout)) {
            cfg.acceleration_buffer =
               agx_map_texture_gpu(rsrc, 0) + rsrc->layout.metadata_offset_B +
               (first_layer * rsrc->layout.compression_layer_stride_B);
         }
      }

      agx_merge(texture, texture2, TEXTURE);
      textures[i] = texture;
   }

   for (unsigned i = nr_active_textures; i < nr_textures; ++i)
      agx_set_null_texture(&textures[i], T_tex.gpu);

   for (unsigned i = 0; i < nr_images; ++i) {
      if (!(ctx->stage[stage].image_mask & BITFIELD_BIT(i))) {
         /* TODO: Null images */
         continue;
      }

      struct pipe_image_view *view = &ctx->stage[stage].images[i];
      agx_batch_track_image(batch, view);

      /* Image descriptors come in pairs after the textures */
      struct agx_texture_packed *texture =
         ((struct agx_texture_packed *)T_tex.cpu) +
         cs->info.nr_bindful_textures + (2 * i);

      struct agx_pbe_packed *pbe = (struct agx_pbe_packed *)(texture + 1);

      struct pipe_sampler_view sampler_view = util_image_to_sampler_view(view);
      agx_pack_texture(texture, agx_resource(view->resource), view->format,
                       &sampler_view, true);
      agx_batch_upload_pbe(batch, pbe, view, false, false);
   }

   if (stage == PIPE_SHADER_FRAGMENT &&
       agx_tilebuffer_spills(&batch->tilebuffer_layout)) {

      struct agx_texture_packed *out =
         ((struct agx_texture_packed *)T_tex.cpu) +
         agx_nr_tex_descriptors_without_spilled_rts(cs);

      agx_upload_spilled_rt_descriptors(out, batch);
   }

   batch->texture_count[stage] = nr_tex_descriptors;
   batch->textures[stage] = T_tex.gpu;
}

static void
agx_upload_samplers(struct agx_batch *batch, struct agx_compiled_shader *cs,
                    enum pipe_shader_type stage)
{
   struct agx_context *ctx = batch->ctx;
   unsigned nr_samplers = sampler_count(ctx, cs, stage);
   bool custom_borders = ctx->stage[stage].custom_borders;

   size_t sampler_length =
      AGX_SAMPLER_LENGTH + (custom_borders ? AGX_BORDER_LENGTH : 0);

   struct agx_ptr T =
      agx_pool_alloc_aligned(&batch->pool, sampler_length * nr_samplers, 64);

   uint8_t *out_sampler = T.cpu;
   for (unsigned i = 0; i < nr_samplers; ++i) {
      struct agx_sampler_state *sampler = ctx->stage[stage].samplers[i];
      struct agx_sampler_packed *out = (struct agx_sampler_packed *)out_sampler;

      if (cs->info.uses_txf && i == cs->info.txf_sampler) {
         agx_pack(out, SAMPLER, cfg) {
            /* Allow mipmapping. This is respected by txf, weirdly. */
            cfg.mip_filter = AGX_MIP_FILTER_NEAREST;

            /* Out-of-bounds reads must return 0 */
            cfg.wrap_s = AGX_WRAP_CLAMP_TO_BORDER;
            cfg.wrap_t = AGX_WRAP_CLAMP_TO_BORDER;
            cfg.wrap_r = AGX_WRAP_CLAMP_TO_BORDER;
            cfg.border_colour = AGX_BORDER_COLOUR_TRANSPARENT_BLACK;
         }
      } else if (sampler) {
         *out = sampler->desc;

         if (custom_borders) {
            memcpy(out_sampler + AGX_SAMPLER_LENGTH, &sampler->border,
                   AGX_BORDER_LENGTH);
         } else {
            assert(!sampler->uses_custom_border && "invalid combination");
         }
      } else {
         memset(out, 0, sampler_length);
      }

      out_sampler += sampler_length;
   }

   batch->sampler_count[stage] = nr_samplers;
   batch->samplers[stage] = T.gpu;
}

static void
agx_update_descriptors(struct agx_batch *batch, struct agx_compiled_shader *cs,
                       enum pipe_shader_type stage)
{
   struct agx_context *ctx = batch->ctx;

   if (ctx->stage[stage].dirty & AGX_STAGE_DIRTY_IMAGE)
      agx_upload_textures(batch, cs, stage);

   if (ctx->stage[stage].dirty & AGX_STAGE_DIRTY_SAMPLER)
      agx_upload_samplers(batch, cs, stage);

   if (ctx->stage[stage].dirty) {
      batch->uniforms.tables[AGX_SYSVAL_STAGE(stage)] =
         agx_upload_stage_uniforms(batch, batch->textures[stage], stage);
   }
}

static uint32_t
agx_build_pipeline(struct agx_batch *batch, struct agx_compiled_shader *cs,
                   enum pipe_shader_type stage, unsigned variable_shared_mem)
{
   struct agx_context *ctx = batch->ctx;
   struct agx_usc_builder b =
      agx_alloc_usc_control(&batch->pipeline_pool, cs->push_range_count + 2);

   if (batch->texture_count[stage]) {
      agx_usc_pack(&b, TEXTURE, cfg) {
         cfg.start = 0;
         cfg.count =
            MIN2(batch->texture_count[stage], AGX_NUM_TEXTURE_STATE_REGS);
         cfg.buffer = batch->textures[stage];
      }
   }

   if (batch->sampler_count[stage]) {
      agx_usc_pack(&b, SAMPLER, cfg) {
         cfg.start = 0;
         cfg.count = batch->sampler_count[stage];
         cfg.buffer = batch->samplers[stage];
      }
   }

   for (unsigned i = 0; i < cs->push_range_count; ++i) {
      agx_usc_uniform(
         &b, cs->push[i].uniform, cs->push[i].length,
         batch->uniforms.tables[cs->push[i].table] + cs->push[i].offset);
   }

   if (stage == PIPE_SHADER_FRAGMENT) {
      agx_usc_tilebuffer(&b, &batch->tilebuffer_layout);
   } else if (stage == PIPE_SHADER_COMPUTE) {
      unsigned size = cs->info.local_size + variable_shared_mem;

      agx_usc_pack(&b, SHARED, cfg) {
         cfg.layout = AGX_SHARED_LAYOUT_VERTEX_COMPUTE;
         cfg.bytes_per_threadgroup = size > 0 ? size : 65536;
         cfg.uses_shared_memory = size > 0;
      }
   } else {
      agx_usc_shared_none(&b);
   }

   agx_usc_pack(&b, SHADER, cfg) {
      if (stage == PIPE_SHADER_FRAGMENT)
         cfg.loads_varyings = cs->info.varyings.fs.nr_bindings > 0;

      cfg.code = cs->bo->ptr.gpu + cs->info.main_offset;
      cfg.unk_2 = (stage == PIPE_SHADER_FRAGMENT) ? 2 : 3;
   }

   agx_usc_pack(&b, REGISTERS, cfg) {
      cfg.register_count = cs->info.nr_gprs;
      cfg.unk_1 = (stage == PIPE_SHADER_FRAGMENT);
   }

   if (stage == PIPE_SHADER_FRAGMENT) {
      agx_usc_pack(&b, FRAGMENT_PROPERTIES, cfg) {
         bool writes_sample_mask = ctx->fs->info.writes_sample_mask;
         cfg.early_z_testing = !writes_sample_mask;
         cfg.unk_4 = 0x2;
         cfg.unk_5 = 0x0;
      }
   }

   if (cs->info.has_preamble) {
      agx_usc_pack(&b, PRESHADER, cfg) {
         cfg.code = cs->bo->ptr.gpu + cs->info.preamble_offset;
      }
   } else {
      agx_usc_pack(&b, NO_PRESHADER, cfg)
         ;
   }

   return agx_usc_fini(&b);
}

uint64_t
agx_build_meta(struct agx_batch *batch, bool store, bool partial_render)
{
   struct agx_context *ctx = batch->ctx;

   /* Construct the key */
   struct agx_meta_key key = {.tib = batch->tilebuffer_layout};

   for (unsigned rt = 0; rt < PIPE_MAX_COLOR_BUFS; ++rt) {
      struct pipe_surface *surf = batch->key.cbufs[rt];

      if (surf == NULL)
         continue;

      if (store) {
         /* TODO: Suppress stores to discarded render targets */
         key.op[rt] = AGX_META_OP_STORE;
      } else if (batch->tilebuffer_layout.spilled[rt] && partial_render) {
         /* Partial render programs exist only to store/load the tilebuffer to
          * main memory. When render targets are already spilled to main memory,
          * there's nothing to do.
          */
         key.op[rt] = AGX_META_OP_NONE;
      } else {
         struct agx_resource *rsrc = agx_resource(surf->texture);
         bool valid = agx_resource_valid(rsrc, surf->u.tex.level);
         bool clear = (batch->clear & (PIPE_CLEAR_COLOR0 << rt));
         bool load = valid && !clear;

         /* Don't read back spilled render targets, they're already in memory */
         load &= !batch->tilebuffer_layout.spilled[rt];

         /* The background program used for partial renders must always load
          * whatever was stored in the mid-frame end-of-tile program.
          */
         load |= partial_render;

         key.op[rt] = load    ? AGX_META_OP_LOAD
                      : clear ? AGX_META_OP_CLEAR
                              : AGX_META_OP_NONE;
      }
   }

   /* Begin building the pipeline */
   struct agx_usc_builder b =
      agx_alloc_usc_control(&batch->pipeline_pool, 1 + PIPE_MAX_COLOR_BUFS);

   bool needs_sampler = false;
   unsigned uniforms = 0;

   for (unsigned rt = 0; rt < PIPE_MAX_COLOR_BUFS; ++rt) {
      if (key.op[rt] == AGX_META_OP_LOAD) {
         /* Each reloaded render target is textured */
         struct agx_ptr texture =
            agx_pool_alloc_aligned(&batch->pool, AGX_TEXTURE_LENGTH, 64);
         struct pipe_surface *surf = batch->key.cbufs[rt];
         assert(surf != NULL && "cannot load nonexistent attachment");

         struct agx_resource *rsrc = agx_resource(surf->texture);
         struct pipe_sampler_view sampler_view = sampler_view_for_surface(surf);

         agx_pack_texture(texture.cpu, rsrc, surf->format, &sampler_view, true);

         agx_usc_pack(&b, TEXTURE, cfg) {
            cfg.start = rt;
            cfg.count = 1;
            cfg.buffer = texture.gpu;
         }

         needs_sampler = true;
      } else if (key.op[rt] == AGX_META_OP_CLEAR) {
         assert(batch->uploaded_clear_color[rt] && "set when cleared");
         agx_usc_uniform(&b, 4 + (8 * rt), 8, batch->uploaded_clear_color[rt]);
         uniforms = MAX2(uniforms, 4 + (8 * rt) + 8);
      } else if (key.op[rt] == AGX_META_OP_STORE) {
         struct pipe_image_view view =
            image_view_for_surface(batch->key.cbufs[rt]);
         struct agx_ptr pbe =
            agx_pool_alloc_aligned(&batch->pool, AGX_PBE_LENGTH, 256);

         /* The tilebuffer is already in sRGB space if needed. Do not convert */
         view.format = util_format_linear(view.format);

         agx_batch_upload_pbe(batch, pbe.cpu, &view, true, true);

         agx_usc_pack(&b, TEXTURE, cfg) {
            cfg.start = rt;
            cfg.count = 1;
            cfg.buffer = pbe.gpu;
         }
      }
   }

   if (agx_tilebuffer_spills(&batch->tilebuffer_layout) && !partial_render &&
       !store) {
      /* Upload texture/PBE descriptors for each render target so we can clear
       * spilled render targets.
       */
      struct agx_ptr descs = agx_pool_alloc_aligned(
         &batch->pool, AGX_TEXTURE_LENGTH * 2 * batch->key.nr_cbufs, 64);
      agx_upload_spilled_rt_descriptors(descs.cpu, batch);

      agx_usc_pack(&b, TEXTURE, cfg) {
         cfg.start = 0;
         cfg.count = 2 * batch->key.nr_cbufs;
         cfg.buffer = descs.gpu;
      }

      /* Bind the base as u0_u1 for bindless access */
      agx_usc_uniform(&b, 0, 4,
                      agx_pool_upload_aligned(&batch->pool, &descs.gpu, 8, 8));
      uniforms = MAX2(uniforms, 4);
   }

   /* All render targets share a sampler */
   if (needs_sampler) {
      struct agx_ptr sampler =
         agx_pool_alloc_aligned(&batch->pool, AGX_SAMPLER_LENGTH, 64);

      agx_pack(sampler.cpu, SAMPLER, cfg) {
         cfg.magnify_linear = true;
         cfg.minify_linear = false;
         cfg.mip_filter = AGX_MIP_FILTER_NONE;
         cfg.wrap_s = AGX_WRAP_CLAMP_TO_EDGE;
         cfg.wrap_t = AGX_WRAP_CLAMP_TO_EDGE;
         cfg.wrap_r = AGX_WRAP_CLAMP_TO_EDGE;
         cfg.pixel_coordinates = true;
         cfg.compare_func = AGX_COMPARE_FUNC_ALWAYS;
      }

      agx_usc_pack(&b, SAMPLER, cfg) {
         cfg.start = 0;
         cfg.count = 1;
         cfg.buffer = sampler.gpu;
      }
   }

   agx_usc_tilebuffer(&b, &batch->tilebuffer_layout);

   /* Get the shader */
   key.reserved_preamble = uniforms;
   struct agx_meta_shader *shader = agx_get_meta_shader(&ctx->meta, &key);
   agx_batch_add_bo(batch, shader->bo);

   agx_usc_pack(&b, SHADER, cfg) {
      cfg.code = shader->ptr;
      cfg.unk_2 = 0;
   }

   agx_usc_pack(&b, REGISTERS, cfg)
      cfg.register_count = shader->info.nr_gprs;

   if (shader->info.has_preamble) {
      agx_usc_pack(&b, PRESHADER, cfg) {
         cfg.code = shader->ptr + shader->info.preamble_offset;
      }
   } else {
      agx_usc_pack(&b, NO_PRESHADER, cfg)
         ;
   }

   return agx_usc_fini(&b);
}

/*
 * Return the standard sample positions, packed into a 32-bit word with fixed
 * point nibbles for each x/y component of the (at most 4) samples. This is
 * suitable for programming the PPP_MULTISAMPLECTL control register.
 */
static uint32_t
agx_default_sample_positions(unsigned nr_samples)
{
   switch (nr_samples) {
   case 1:
      return 0x88;
   case 2:
      return 0x44cc;
   case 4:
      return 0xeaa26e26;
   default:
      unreachable("Invalid sample count");
   }
}

void
agx_batch_init_state(struct agx_batch *batch)
{
   if (batch->initialized)
      return;

   if (batch->key.width == AGX_COMPUTE_BATCH_WIDTH) {
      batch->initialized = true;
      return;
   }

   /* Emit state on the batch that we don't change and so don't dirty track */
   uint8_t *out = batch->encoder_current;
   struct agx_ppp_update ppp =
      agx_new_ppp_update(&batch->pool, (struct AGX_PPP_HEADER){
                                          .w_clamp = true,
                                          .cull_2 = true,
                                          .occlusion_query_2 = true,
                                          .output_unknown = true,
                                          .varying_word_2 = true,
                                       });

   /* clang-format off */
   agx_ppp_push(&ppp, W_CLAMP, cfg) cfg.w_clamp = 1e-10;
   agx_ppp_push(&ppp, CULL_2, cfg);
   agx_ppp_push(&ppp, FRAGMENT_OCCLUSION_QUERY_2, cfg);
   agx_ppp_push(&ppp, OUTPUT_UNKNOWN, cfg);
   agx_ppp_push(&ppp, VARYING_2, cfg);
   /* clang-format on */

   agx_ppp_fini(&out, &ppp);
   batch->encoder_current = out;

   /* Mark it as initialized now, since agx_batch_writes() will check this. */
   batch->initialized = true;

   /* Choose a tilebuffer layout given the framebuffer key */
   enum pipe_format formats[PIPE_MAX_COLOR_BUFS] = {0};
   for (unsigned i = 0; i < batch->key.nr_cbufs; ++i) {
      struct pipe_surface *surf = batch->key.cbufs[i];
      if (surf)
         formats[i] = surf->format;
   }

   batch->tilebuffer_layout = agx_build_tilebuffer_layout(
      formats, batch->key.nr_cbufs,
      util_framebuffer_get_num_samples(&batch->key),
      util_framebuffer_get_num_layers(&batch->key) > 1);

   if (agx_device(batch->ctx->base.screen)->debug & AGX_DBG_SMALLTILE)
      batch->tilebuffer_layout.tile_size = (struct agx_tile_size){16, 16};

   /* If the layout spilled render targets, we need to decompress those render
    * targets to ensure we can write to them.
    */
   if (agx_tilebuffer_spills(&batch->tilebuffer_layout)) {
      for (unsigned i = 0; i < batch->key.nr_cbufs; ++i) {
         if (!batch->tilebuffer_layout.spilled[i])
            continue;

         struct pipe_surface *surf = batch->key.cbufs[i];
         if (!surf)
            continue;

         struct agx_resource *rsrc = agx_resource(surf->texture);
         if (rsrc->layout.writeable_image)
            continue;

         /* Decompress if we can and shadow if we can't. */
         if (rsrc->base.bind & PIPE_BIND_SHARED)
            unreachable("TODO");
         else
            agx_decompress(batch->ctx, rsrc, "Render target spilled");
      }
   }

   if (batch->key.zsbuf) {
      struct agx_resource *rsrc = agx_resource(batch->key.zsbuf->texture);
      agx_batch_writes(batch, rsrc);

      if (rsrc->separate_stencil)
         agx_batch_writes(batch, rsrc->separate_stencil);
   }

   for (unsigned i = 0; i < batch->key.nr_cbufs; ++i) {
      if (batch->key.cbufs[i])
         agx_batch_writes(batch, agx_resource(batch->key.cbufs[i]->texture));
   }

   /* Set up standard sample positions */
   batch->uniforms.ppp_multisamplectl =
      agx_default_sample_positions(batch->tilebuffer_layout.nr_samples);
}

static enum agx_object_type
agx_point_object_type(struct agx_rasterizer *rast)
{
   return (rast->base.sprite_coord_mode == PIPE_SPRITE_COORD_UPPER_LEFT)
             ? AGX_OBJECT_TYPE_POINT_SPRITE_UV01
             : AGX_OBJECT_TYPE_POINT_SPRITE_UV10;
}

#define MAX_PPP_UPDATES 2

static uint8_t *
agx_encode_state(struct agx_batch *batch, uint8_t *out, bool is_lines,
                 bool is_points)
{
   struct agx_context *ctx = batch->ctx;
   struct agx_rasterizer *rast = ctx->rast;
   unsigned ppp_updates = 0;

#define IS_DIRTY(ST) !!(ctx->dirty & AGX_DIRTY_##ST)

   agx_update_descriptors(batch, ctx->vs, PIPE_SHADER_VERTEX);
   agx_update_descriptors(batch, ctx->fs, PIPE_SHADER_FRAGMENT);

   if (IS_DIRTY(VERTEX)) {
      agx_upload_vbos(batch);
   }

   if (IS_DIRTY(BLEND_COLOR)) {
      memcpy(batch->uniforms.blend_constant, &ctx->blend_color,
             sizeof(ctx->blend_color));
   }

   if (IS_DIRTY(VS) || IS_DIRTY(FS) || IS_DIRTY(VERTEX) ||
       IS_DIRTY(BLEND_COLOR)) {

      agx_upload_uniforms(batch);
   }

   if (IS_DIRTY(VS)) {
      agx_pack(out, VDM_STATE, cfg) {
         cfg.vertex_shader_word_0_present = true;
         cfg.vertex_shader_word_1_present = true;
         cfg.vertex_outputs_present = true;
         cfg.vertex_unknown_present = true;
      }
      out += AGX_VDM_STATE_LENGTH;

      agx_pack(out, VDM_STATE_VERTEX_SHADER_WORD_0, cfg) {
         cfg.uniform_register_count = ctx->vs->info.push_count;
         cfg.preshader_register_count = ctx->vs->info.nr_preamble_gprs;
         cfg.texture_state_register_count =
            agx_nr_tex_descriptors(batch, PIPE_SHADER_VERTEX, ctx->vs);
         cfg.sampler_state_register_count =
            translate_sampler_state_count(ctx, ctx->vs, PIPE_SHADER_VERTEX);
      }
      out += AGX_VDM_STATE_VERTEX_SHADER_WORD_0_LENGTH;

      agx_pack(out, VDM_STATE_VERTEX_SHADER_WORD_1, cfg) {
         cfg.pipeline =
            agx_build_pipeline(batch, ctx->vs, PIPE_SHADER_VERTEX, 0);
      }
      out += AGX_VDM_STATE_VERTEX_SHADER_WORD_1_LENGTH;

      agx_pack(out, VDM_STATE_VERTEX_OUTPUTS, cfg) {
         cfg.output_count_1 = ctx->vs->info.varyings.vs.nr_index;
         cfg.output_count_2 = cfg.output_count_1;
      }
      out += AGX_VDM_STATE_VERTEX_OUTPUTS_LENGTH;

      agx_pack(out, VDM_STATE_VERTEX_UNKNOWN, cfg) {
         cfg.flat_shading_control = ctx->rast->base.flatshade_first
                                       ? AGX_VDM_VERTEX_0
                                       : AGX_VDM_VERTEX_2;
         cfg.unknown_4 = cfg.unknown_5 = ctx->rast->base.rasterizer_discard;
      }
      out += AGX_VDM_STATE_VERTEX_UNKNOWN_LENGTH;

      /* Pad up to a multiple of 8 bytes */
      memset(out, 0, 4);
      out += 4;
   }

   struct agx_pool *pool = &batch->pool;
   struct agx_compiled_shader *vs = ctx->vs, *fs = ctx->fs;

   if ((ctx->dirty & AGX_DIRTY_RS) && ctx->rast->base.offset_tri) {
      agx_upload_depth_bias(batch, &ctx->rast->base);
      ctx->dirty |= AGX_DIRTY_SCISSOR_ZBIAS;
   }

   if (ctx->dirty & (AGX_DIRTY_VIEWPORT | AGX_DIRTY_SCISSOR_ZBIAS)) {
      agx_upload_viewport_scissor(
         pool, batch, &out, &ctx->viewport,
         ctx->rast->base.scissor ? &ctx->scissor : NULL);
   }

   bool varyings_dirty = false;

   if (IS_DIRTY(VS_PROG) || IS_DIRTY(FS_PROG) || IS_DIRTY(RS)) {
      batch->varyings = agx_link_varyings_vs_fs(
         &batch->pipeline_pool, &ctx->vs->info.varyings.vs,
         &ctx->fs->info.varyings.fs, ctx->rast->base.flatshade_first);

      varyings_dirty = true;
      ppp_updates++;
   }

   bool object_type_dirty =
      IS_DIRTY(PRIM) || (is_points && IS_DIRTY(SPRITE_COORD_MODE));

   bool fragment_face_dirty =
      IS_DIRTY(ZS) || IS_DIRTY(STENCIL_REF) || IS_DIRTY(RS);

   enum agx_object_type object_type = is_points  ? agx_point_object_type(rast)
                                      : is_lines ? AGX_OBJECT_TYPE_LINE
                                                 : AGX_OBJECT_TYPE_TRIANGLE;

   struct AGX_PPP_HEADER dirty = {
      .fragment_control =
         IS_DIRTY(ZS) || IS_DIRTY(RS) || IS_DIRTY(PRIM) || IS_DIRTY(QUERY),
      .fragment_control_2 = IS_DIRTY(PRIM) || IS_DIRTY(FS_PROG) || IS_DIRTY(RS),
      .fragment_front_face = fragment_face_dirty,
      .fragment_front_face_2 = object_type_dirty || IS_DIRTY(FS_PROG),
      .fragment_front_stencil = IS_DIRTY(ZS),
      .fragment_back_face = fragment_face_dirty,
      .fragment_back_face_2 = object_type_dirty || IS_DIRTY(FS_PROG),
      .fragment_back_stencil = IS_DIRTY(ZS),
      .output_select = IS_DIRTY(VS_PROG) || IS_DIRTY(FS_PROG),
      .varying_counts_32 = IS_DIRTY(VS_PROG),
      .varying_counts_16 = IS_DIRTY(VS_PROG),
      .cull = IS_DIRTY(RS),
      .fragment_shader =
         IS_DIRTY(FS) || varyings_dirty || IS_DIRTY(SAMPLE_MASK),
      .occlusion_query = IS_DIRTY(QUERY),
      .output_size = IS_DIRTY(VS_PROG),
   };

   struct agx_ppp_update ppp = agx_new_ppp_update(pool, dirty);

   if (dirty.fragment_control) {
      agx_ppp_push(&ppp, FRAGMENT_CONTROL, cfg) {
         if (ctx->active_queries && ctx->occlusion_query) {
            if (ctx->occlusion_query->type == PIPE_QUERY_OCCLUSION_COUNTER)
               cfg.visibility_mode = AGX_VISIBILITY_MODE_COUNTING;
            else
               cfg.visibility_mode = AGX_VISIBILITY_MODE_BOOLEAN;
         }

         cfg.stencil_test_enable = ctx->zs->base.stencil[0].enabled;
         cfg.two_sided_stencil = ctx->zs->base.stencil[1].enabled;
         cfg.depth_bias_enable = rast->base.offset_tri;

         /* Always enable scissoring so we may scissor to the viewport (TODO:
          * optimize this out if the viewport is the default and the app does
          * not use the scissor test)
          */
         cfg.scissor_enable = true;
      }
   }

   if (dirty.fragment_control_2) {
      agx_ppp_push(&ppp, FRAGMENT_CONTROL, cfg) {
         /* This avoids broken derivatives along primitive edges */
         cfg.disable_tri_merging =
            (is_lines || is_points || ctx->fs->info.disable_tri_merging);
         cfg.tag_write_disable = ctx->fs->info.tag_write_disable ||
                                 ctx->rast->base.rasterizer_discard;
         cfg.pass_type = agx_pass_type_for_shader(&ctx->fs->info);
      }
   }

   if (dirty.fragment_front_face) {
      struct agx_fragment_face_packed front_face;
      agx_pack(&front_face, FRAGMENT_FACE, cfg) {
         cfg.stencil_reference = ctx->stencil_ref.ref_value[0];
         cfg.line_width = rast->line_width;
         cfg.polygon_mode = rast->polygon_mode;
      };

      front_face.opaque[0] |= ctx->zs->depth.opaque[0];

      agx_ppp_push_packed(&ppp, &front_face, FRAGMENT_FACE);
   }

   if (dirty.fragment_front_face_2)
      agx_ppp_fragment_face_2(&ppp, object_type, &ctx->fs->info);

   if (dirty.fragment_front_stencil) {
      agx_ppp_push_packed(&ppp, ctx->zs->front_stencil.opaque,
                          FRAGMENT_STENCIL);
   }

   if (dirty.fragment_back_face) {
      struct agx_fragment_face_packed back_face;

      agx_pack(&back_face, FRAGMENT_FACE, cfg) {
         bool twosided = ctx->zs->base.stencil[1].enabled;
         cfg.stencil_reference = ctx->stencil_ref.ref_value[twosided ? 1 : 0];
         cfg.line_width = rast->line_width;
         cfg.polygon_mode = rast->polygon_mode;
      };

      back_face.opaque[0] |= ctx->zs->depth.opaque[0];
      agx_ppp_push_packed(&ppp, &back_face, FRAGMENT_FACE);
   }

   if (dirty.fragment_back_face_2)
      agx_ppp_fragment_face_2(&ppp, object_type, &ctx->fs->info);

   if (dirty.fragment_back_stencil)
      agx_ppp_push_packed(&ppp, ctx->zs->back_stencil.opaque, FRAGMENT_STENCIL);

   if (dirty.output_select) {
      agx_ppp_push(&ppp, OUTPUT_SELECT, cfg) {
         cfg.varyings = !!fs->info.varyings.fs.nr_bindings;
         cfg.point_size = vs->info.writes_psiz;
         cfg.viewport_target = vs->info.writes_layer_viewport;
         cfg.render_target = vs->info.writes_layer_viewport;
         cfg.frag_coord_z = fs->info.varyings.fs.reads_z;
      }
   }

   assert(dirty.varying_counts_32 == dirty.varying_counts_16);

   if (dirty.varying_counts_32) {
      agx_ppp_push(&ppp, VARYING_COUNTS, cfg) {
         cfg.smooth = vs->info.varyings.vs.num_32_smooth;
         cfg.flat = vs->info.varyings.vs.num_32_flat;
         cfg.linear = vs->info.varyings.vs.num_32_linear;
      }

      agx_ppp_push(&ppp, VARYING_COUNTS, cfg) {
         cfg.smooth = vs->info.varyings.vs.num_16_smooth;
         cfg.flat = vs->info.varyings.vs.num_16_flat;
         cfg.linear = vs->info.varyings.vs.num_16_linear;
      }
   }

   if (dirty.cull)
      agx_ppp_push_packed(&ppp, ctx->rast->cull, CULL);

   if (dirty.fragment_shader) {
      unsigned frag_tex_count = ctx->stage[PIPE_SHADER_FRAGMENT].texture_count;

      agx_ppp_push(&ppp, FRAGMENT_SHADER, cfg) {
         cfg.pipeline =
            agx_build_pipeline(batch, ctx->fs, PIPE_SHADER_FRAGMENT, 0),
         cfg.uniform_register_count = ctx->fs->info.push_count;
         cfg.preshader_register_count = ctx->fs->info.nr_preamble_gprs;
         cfg.texture_state_register_count =
            agx_nr_tex_descriptors(batch, PIPE_SHADER_FRAGMENT, ctx->fs);
         cfg.sampler_state_register_count =
            translate_sampler_state_count(ctx, ctx->fs, PIPE_SHADER_FRAGMENT);
         cfg.cf_binding_count = ctx->fs->info.varyings.fs.nr_bindings;
         cfg.cf_bindings = batch->varyings;

         /* XXX: This is probably wrong */
         cfg.unknown_30 = frag_tex_count >= 4;
      }
   }

   if (dirty.occlusion_query) {
      agx_ppp_push(&ppp, FRAGMENT_OCCLUSION_QUERY, cfg) {
         if (ctx->active_queries && ctx->occlusion_query) {
            cfg.index = agx_get_oq_index(batch, ctx->occlusion_query);
         } else {
            cfg.index = 0;
         }
      }
   }

   if (dirty.output_size) {
      agx_ppp_push(&ppp, OUTPUT_SIZE, cfg)
         cfg.count = vs->info.varyings.vs.nr_index;
   }

   agx_ppp_fini(&out, &ppp);
   ppp_updates++;

#undef IS_DIRTY

   assert(ppp_updates <= MAX_PPP_UPDATES);
   return out;
}

static enum agx_primitive
agx_primitive_for_pipe(enum mesa_prim mode)
{
   switch (mode) {
   case MESA_PRIM_POINTS:
      return AGX_PRIMITIVE_POINTS;
   case MESA_PRIM_LINES:
      return AGX_PRIMITIVE_LINES;
   case MESA_PRIM_LINE_STRIP:
      return AGX_PRIMITIVE_LINE_STRIP;
   case MESA_PRIM_LINE_LOOP:
      return AGX_PRIMITIVE_LINE_LOOP;
   case MESA_PRIM_TRIANGLES:
      return AGX_PRIMITIVE_TRIANGLES;
   case MESA_PRIM_TRIANGLE_STRIP:
      return AGX_PRIMITIVE_TRIANGLE_STRIP;
   case MESA_PRIM_TRIANGLE_FAN:
      return AGX_PRIMITIVE_TRIANGLE_FAN;
   case MESA_PRIM_QUADS:
      return AGX_PRIMITIVE_QUADS;
   case MESA_PRIM_QUAD_STRIP:
      return AGX_PRIMITIVE_QUAD_STRIP;
   default:
      unreachable("todo: other primitive types");
   }
}

static uint64_t
agx_index_buffer_rsrc_ptr(struct agx_batch *batch,
                          const struct pipe_draw_info *info, size_t *extent)
{
   assert(!info->has_user_indices && "cannot use user pointers with indirect");

   struct agx_resource *rsrc = agx_resource(info->index.resource);
   agx_batch_reads(batch, rsrc);

   *extent = ALIGN_POT(util_resource_size(&rsrc->base), 4);
   return rsrc->bo->ptr.gpu;
}

static uint64_t
agx_index_buffer_direct_ptr(struct agx_batch *batch,
                            const struct pipe_draw_start_count_bias *draw,
                            const struct pipe_draw_info *info, size_t *extent)
{
   off_t offset = draw->start * info->index_size;
   uint32_t max_extent = draw->count * info->index_size;

   if (!info->has_user_indices) {
      uint64_t base = agx_index_buffer_rsrc_ptr(batch, info, extent);

      *extent = ALIGN_POT(MIN2(*extent - offset, max_extent), 4);
      return base + offset;
   } else {
      *extent = ALIGN_POT(max_extent, 4);

      return agx_pool_upload_aligned(&batch->pool,
                                     ((uint8_t *)info->index.user) + offset,
                                     draw->count * info->index_size, 64);
   }
}

static bool
agx_scissor_culls_everything(struct agx_context *ctx)
{
   unsigned minx, miny, maxx, maxy;
   agx_get_scissor_extents(&ctx->viewport,
                           ctx->rast->base.scissor ? &ctx->scissor : NULL,
                           &ctx->framebuffer, &minx, &miny, &maxx, &maxy);

   return (minx >= maxx) || (miny >= maxy);
}

static void
agx_ensure_cmdbuf_has_space(struct agx_batch *batch, size_t space)
{
   /* Assert that we have space for a link tag */
   assert((batch->encoder_current + AGX_VDM_STREAM_LINK_LENGTH) <=
             batch->encoder_end &&
          "Encoder overflowed");

   /* Always leave room for a link tag, in case we run out of space later,
    * plus padding because VDM apparently overreads?
    *
    * 0x200 is not enough. 0x400 seems to work. 0x800 for safety.
    */
   space += AGX_VDM_STREAM_LINK_LENGTH + 0x800;

   /* If there is room in the command buffer, we're done */
   if (likely((batch->encoder_end - batch->encoder_current) >= space))
      return;

   /* Otherwise, we need to allocate a new command buffer. We use memory owned
    * by the batch to simplify lifetime management for the BO.
    */
   size_t size = 65536;
   struct agx_ptr T = agx_pool_alloc_aligned(&batch->pool, size, 256);

   /* Jump from the old command buffer to the new command buffer */
   agx_pack(batch->encoder_current, VDM_STREAM_LINK, cfg) {
      cfg.target_lo = T.gpu & BITFIELD_MASK(32);
      cfg.target_hi = T.gpu >> 32;
   }

   /* Swap out the command buffer */
   batch->encoder_current = T.cpu;
   batch->encoder_end = batch->encoder_current + size;
}

static void
agx_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info,
             unsigned drawid_offset,
             const struct pipe_draw_indirect_info *indirect,
             const struct pipe_draw_start_count_bias *draws, unsigned num_draws)
{
   struct agx_context *ctx = agx_context(pctx);

   if (unlikely(!agx_render_condition_check(ctx)))
      return;

   if (num_draws > 1) {
      util_draw_multi(pctx, info, drawid_offset, indirect, draws, num_draws);
      return;
   }

   if (indirect && indirect->count_from_stream_output) {
      agx_draw_vbo_from_xfb(pctx, info, drawid_offset, indirect);
      return;
   }

   bool has_xfb_info = ctx->stage[PIPE_SHADER_VERTEX].shader->has_xfb_info;
   bool uses_xfb = has_xfb_info && ctx->streamout.num_targets;
   bool uses_prims_generated = ctx->active_queries && ctx->prims_generated;

   if (indirect && (uses_prims_generated || uses_xfb)) {
      perf_debug_ctx(ctx, "Emulating indirect draw due to XFB");
      util_draw_indirect(pctx, info, indirect);
      return;
   }

   if (uses_xfb && info->primitive_restart) {
      perf_debug_ctx(ctx, "Emulating primitive restart due to XFB");
      util_draw_vbo_without_prim_restart(pctx, info, drawid_offset, indirect,
                                         draws);
      return;
   }

   if (!ctx->streamout.key.active && uses_prims_generated) {
      agx_primitives_update_direct(ctx, info, draws);
   }

   struct agx_batch *batch = agx_get_batch(ctx);
   unsigned idx_size = info->index_size;
   uint64_t ib = 0;
   size_t ib_extent = 0;

   if (idx_size) {
      if (indirect != NULL)
         ib = agx_index_buffer_rsrc_ptr(batch, info, &ib_extent);
      else
         ib = agx_index_buffer_direct_ptr(batch, draws, info, &ib_extent);
   }

   if (uses_xfb)
      agx_launch_so(pctx, info, draws, ib);

#ifndef NDEBUG
   if (unlikely(agx_device(pctx->screen)->debug & AGX_DBG_DIRTY))
      agx_dirty_all(ctx);
#endif

   if (agx_scissor_culls_everything(ctx))
      return;

   /* We don't support side effects in vertex stages (only used internally for
    * transform feedback lowering), so this is trivial.
    */
   if (ctx->rast->base.rasterizer_discard && !ctx->streamout.key.active)
      return;

   agx_batch_init_state(batch);

   /* Dirty track the reduced prim: lines vs points vs triangles */
   enum mesa_prim reduced_prim = u_reduced_prim(info->mode);
   if (reduced_prim != batch->reduced_prim)
      ctx->dirty |= AGX_DIRTY_PRIM;
   batch->reduced_prim = reduced_prim;

   /* Update batch masks based on current state */
   if (ctx->dirty & AGX_DIRTY_BLEND) {
      /* TODO: Any point to tracking load? */
      batch->draw |= ctx->blend->store;
      batch->resolve |= ctx->blend->store;
   }

   if (ctx->dirty & AGX_DIRTY_ZS) {
      batch->load |= ctx->zs->load;
      batch->draw |= ctx->zs->store;
      batch->resolve |= ctx->zs->store;
   }

   if (agx_update_vs(ctx)) {
      ctx->dirty |= AGX_DIRTY_VS | AGX_DIRTY_VS_PROG;
      ctx->stage[PIPE_SHADER_VERTEX].dirty = ~0;
   } else if (ctx->stage[PIPE_SHADER_VERTEX].dirty ||
              (ctx->dirty & AGX_DIRTY_VERTEX)) {
      ctx->dirty |= AGX_DIRTY_VS;
   }

   struct agx_compiled_shader *vs = ctx->vs;
   batch->uniforms.layer_id_written = vs->info.writes_layer_viewport ? ~0 : 0;

   if (agx_update_fs(batch)) {
      ctx->dirty |= AGX_DIRTY_FS | AGX_DIRTY_FS_PROG;
      ctx->stage[PIPE_SHADER_FRAGMENT].dirty = ~0;
   } else if (ctx->stage[PIPE_SHADER_FRAGMENT].dirty ||
              (ctx->dirty & (AGX_DIRTY_BLEND_COLOR | AGX_DIRTY_SAMPLE_MASK))) {
      ctx->dirty |= AGX_DIRTY_FS;
   }

   agx_batch_add_bo(batch, ctx->vs->bo);
   agx_batch_add_bo(batch, ctx->fs->bo);

   /* When we approach the end of a command buffer, cycle it out for a new one.
    * We only need to do this once per draw as long as we conservatively
    * estimate the maximum bytes of VDM commands that this draw will emit.
    */
   agx_ensure_cmdbuf_has_space(
      batch,
      (AGX_VDM_STATE_LENGTH * 2) + (AGX_PPP_STATE_LENGTH * MAX_PPP_UPDATES) +
         AGX_VDM_STATE_RESTART_INDEX_LENGTH +
         AGX_VDM_STATE_VERTEX_SHADER_WORD_0_LENGTH +
         AGX_VDM_STATE_VERTEX_SHADER_WORD_1_LENGTH +
         AGX_VDM_STATE_VERTEX_OUTPUTS_LENGTH +
         AGX_VDM_STATE_VERTEX_UNKNOWN_LENGTH + 4 /* padding */ +
         ((!batch->any_draws) ? AGX_VDM_BARRIER_LENGTH : 0) +
         AGX_INDEX_LIST_LENGTH + AGX_INDEX_LIST_BUFFER_LO_LENGTH +
         AGX_INDEX_LIST_COUNT_LENGTH + AGX_INDEX_LIST_INSTANCES_LENGTH +
         AGX_INDEX_LIST_START_LENGTH + AGX_INDEX_LIST_BUFFER_SIZE_LENGTH);

   uint8_t *out = agx_encode_state(batch, batch->encoder_current,
                                   reduced_prim == MESA_PRIM_LINES,
                                   reduced_prim == MESA_PRIM_POINTS);

   enum agx_primitive prim = agx_primitive_for_pipe(info->mode);
   if (idx_size) {
      agx_pack(out, VDM_STATE, cfg)
         cfg.restart_index_present = true;
      out += AGX_VDM_STATE_LENGTH;

      agx_pack(out, VDM_STATE_RESTART_INDEX, cfg) {
         cfg.value = info->restart_index;
      }
      out += AGX_VDM_STATE_RESTART_INDEX_LENGTH;
   }

   if (!batch->any_draws) {
      agx_pack(out, VDM_BARRIER, cfg) {
         cfg.usc_cache_inval = true;
      }
      out += AGX_VDM_BARRIER_LENGTH;
   }

   batch->any_draws = true;

   agx_pack(out, INDEX_LIST, cfg) {
      cfg.primitive = prim;
      cfg.instance_count_present = true;

      if (indirect != NULL) {
         cfg.indirect_buffer_present = true;
      } else {
         cfg.index_count_present = true;
         cfg.start_present = true;
      }

      if (idx_size) {
         cfg.restart_enable = info->primitive_restart;
         cfg.index_buffer_hi = (ib >> 32);
         cfg.index_size = agx_translate_index_size(idx_size);
         cfg.index_buffer_present = true;
         cfg.index_buffer_size_present = true;
      }
   }
   out += AGX_INDEX_LIST_LENGTH;

   if (idx_size) {
      agx_pack(out, INDEX_LIST_BUFFER_LO, cfg) {
         cfg.buffer_lo = ib & BITFIELD_MASK(32);
      }
      out += AGX_INDEX_LIST_BUFFER_LO_LENGTH;
   }

   if (!indirect) {
      agx_pack(out, INDEX_LIST_COUNT, cfg)
         cfg.count = draws->count;
      out += AGX_INDEX_LIST_COUNT_LENGTH;
   }

   agx_pack(out, INDEX_LIST_INSTANCES, cfg)
      cfg.count = info->instance_count;
   out += AGX_INDEX_LIST_INSTANCES_LENGTH;

   if (indirect) {
      struct agx_resource *indirect_rsrc = agx_resource(indirect->buffer);
      uint64_t address = indirect_rsrc->bo->ptr.gpu + indirect->offset;

      agx_pack(out, INDEX_LIST_INDIRECT_BUFFER, cfg) {
         cfg.address_hi = address >> 32;
         cfg.address_lo = address & BITFIELD_MASK(32);
      }
      out += AGX_INDEX_LIST_INDIRECT_BUFFER_LENGTH;
   } else {
      agx_pack(out, INDEX_LIST_START, cfg) {
         cfg.start = idx_size ? draws->index_bias : draws->start;
      }
      out += AGX_INDEX_LIST_START_LENGTH;
   }

   if (idx_size) {
      agx_pack(out, INDEX_LIST_BUFFER_SIZE, cfg) {
         cfg.size = ib_extent;
      }
      out += AGX_INDEX_LIST_BUFFER_SIZE_LENGTH;
   }

   /* Insert a memory barrier after transform feedback so the result may be
    * consumed by a subsequent vertex shader.
    */
   if (ctx->streamout.key.active) {
      struct agx_device *dev = agx_device(pctx->screen);
      agx_pack(out, VDM_BARRIER, cfg) {
         cfg.unk_5 = true;
         cfg.unk_6 = true;
         cfg.unk_8 = true;
         cfg.unk_11 = true;
         cfg.unk_20 = true;
         if (dev->params.num_clusters_total > 1) {
            cfg.unk_24 = true;
            if (dev->params.gpu_generation == 13) {
               cfg.unk_4 = true;
               cfg.unk_26 = true;
            }
         }
      }

      out += AGX_VDM_BARRIER_LENGTH;
   }

   batch->encoder_current = out;
   assert((batch->encoder_current + AGX_VDM_STREAM_LINK_LENGTH) <=
             batch->encoder_end &&
          "Failed to reserve sufficient space in encoder");
   agx_dirty_reset_graphics(ctx);

   assert(batch == agx_get_batch(ctx) && "batch should not change under us");

   batch->draws++;

   /* The scissor/zbias arrays are indexed with 16-bit integers, imposigin a
    * maximum of UINT16_MAX descriptors. Flush if the next draw would overflow
    */
   if (unlikely((batch->scissor.size / AGX_SCISSOR_LENGTH) >= UINT16_MAX) ||
       (batch->depth_bias.size / AGX_DEPTH_BIAS_LENGTH) >= UINT16_MAX) {
      agx_flush_batch_for_reason(ctx, batch, "Scissor/depth bias overflow");
   } else if (unlikely(batch->draws > 100000)) {
      /* Mostly so drawoverhead doesn't OOM */
      agx_flush_batch_for_reason(ctx, batch, "Absurd number of draws");
   }
}

static void
agx_texture_barrier(struct pipe_context *pipe, unsigned flags)
{
   struct agx_context *ctx = agx_context(pipe);

   /* Framebuffer fetch is coherent, so barriers are a no-op. */
   if (flags == PIPE_TEXTURE_BARRIER_FRAMEBUFFER)
      return;

   agx_flush_all(ctx, "Texture barrier");
}

static void
agx_launch_grid(struct pipe_context *pipe, const struct pipe_grid_info *info)
{
   struct agx_context *ctx = agx_context(pipe);
   struct agx_batch *batch = agx_get_compute_batch(ctx);
   struct agx_device *dev = agx_device(pipe->screen);

   agx_batch_init_state(batch);

   /* Consider compute launches as "draws" for the purposes of sanity
    * checking batch state.
    */
   batch->any_draws = true;

   /* To implement load_num_workgroups, the number of workgroups needs to be
    * available in GPU memory. This is either the indirect buffer, or just a
    * buffer we upload ourselves if not indirect.
    */
   if (info->indirect) {
      struct agx_resource *indirect = agx_resource(info->indirect);
      agx_batch_reads(batch, indirect);

      batch->uniforms.tables[AGX_SYSVAL_TABLE_GRID] =
         indirect->bo->ptr.gpu + info->indirect_offset;
   } else {
      static_assert(sizeof(info->grid) == 12,
                    "matches indirect dispatch buffer");

      batch->uniforms.tables[AGX_SYSVAL_TABLE_GRID] = agx_pool_upload_aligned(
         &batch->pool, info->grid, sizeof(info->grid), 4);
   }

   util_dynarray_foreach(&ctx->global_buffers, struct pipe_resource *, res) {
      if (!*res)
         continue;

      struct agx_resource *buffer = agx_resource(*res);
      agx_batch_writes(batch, buffer);
   }

   struct agx_uncompiled_shader *uncompiled =
      ctx->stage[PIPE_SHADER_COMPUTE].shader;

   /* There is exactly one variant, get it */
   struct agx_compiled_shader *cs =
      _mesa_hash_table_next_entry(uncompiled->variants, NULL)->data;

   agx_batch_add_bo(batch, cs->bo);

   agx_update_descriptors(batch, cs, PIPE_SHADER_COMPUTE);
   agx_upload_uniforms(batch);

   /* TODO: Ensure space if we allow multiple kernels in a batch */
   uint8_t *out = batch->encoder_current;

   agx_pack(out, CDM_HEADER, cfg) {
      if (info->indirect)
         cfg.mode = AGX_CDM_MODE_INDIRECT_GLOBAL;
      else
         cfg.mode = AGX_CDM_MODE_DIRECT;

      cfg.uniform_register_count = cs->info.push_count;
      cfg.preshader_register_count = cs->info.nr_preamble_gprs;
      cfg.texture_state_register_count =
         agx_nr_tex_descriptors(batch, PIPE_SHADER_COMPUTE, cs);
      cfg.sampler_state_register_count =
         translate_sampler_state_count(ctx, cs, PIPE_SHADER_COMPUTE);
      cfg.pipeline = agx_build_pipeline(batch, cs, PIPE_SHADER_COMPUTE,
                                        info->variable_shared_mem);
   }
   out += AGX_CDM_HEADER_LENGTH;

   /* Added in G14X */
   if (dev->params.gpu_generation >= 14 && dev->params.num_clusters_total > 1) {
      agx_pack(out, CDM_UNK_G14X, cfg)
         ;
      out += AGX_CDM_UNK_G14X_LENGTH;
   }

   if (info->indirect) {
      agx_pack(out, CDM_INDIRECT, cfg) {
         cfg.address_hi = batch->uniforms.tables[AGX_SYSVAL_TABLE_GRID] >> 32;
         cfg.address_lo =
            batch->uniforms.tables[AGX_SYSVAL_TABLE_GRID] & BITFIELD64_MASK(32);
      }
      out += AGX_CDM_INDIRECT_LENGTH;
   } else {
      agx_pack(out, CDM_GLOBAL_SIZE, cfg) {
         cfg.x = info->grid[0] * info->block[0];
         cfg.y = info->grid[1] * info->block[1];
         cfg.z = info->grid[2] * info->block[2];
      }
      out += AGX_CDM_GLOBAL_SIZE_LENGTH;
   }

   agx_pack(out, CDM_LOCAL_SIZE, cfg) {
      cfg.x = info->block[0];
      cfg.y = info->block[1];
      cfg.z = info->block[2];
   }
   out += AGX_CDM_LOCAL_SIZE_LENGTH;

   agx_pack(out, CDM_LAUNCH, cfg)
      ;
   out += AGX_CDM_LAUNCH_LENGTH;

   batch->encoder_current = out;
   assert(batch->encoder_current <= batch->encoder_end &&
          "Failed to reserve sufficient space in encoder");
   /* TODO: Dirty tracking? */

   /* TODO: Allow multiple kernels in a batch? */
   agx_flush_batch_for_reason(ctx, batch, "Compute kernel serialization");
   batch->uniforms.tables[AGX_SYSVAL_TABLE_GRID] = 0;
}

static void
agx_set_global_binding(struct pipe_context *pipe, unsigned first,
                       unsigned count, struct pipe_resource **resources,
                       uint32_t **handles)
{
   struct agx_context *ctx = agx_context(pipe);
   unsigned old_size =
      util_dynarray_num_elements(&ctx->global_buffers, *resources);

   if (old_size < first + count) {
      /* we are screwed no matter what */
      if (!util_dynarray_grow(&ctx->global_buffers, *resources,
                              (first + count) - old_size))
         unreachable("out of memory");

      for (unsigned i = old_size; i < first + count; i++)
         *util_dynarray_element(&ctx->global_buffers, struct pipe_resource *,
                                i) = NULL;
   }

   for (unsigned i = 0; i < count; ++i) {
      struct pipe_resource **res = util_dynarray_element(
         &ctx->global_buffers, struct pipe_resource *, first + i);
      if (resources && resources[i]) {
         pipe_resource_reference(res, resources[i]);

         /* The handle points to uint32_t, but space is allocated for 64
          * bits. We need to respect the offset passed in. This interface
          * is so bad.
          */
         uint64_t addr = 0;
         struct agx_resource *rsrc = agx_resource(resources[i]);

         memcpy(&addr, handles[i], sizeof(addr));
         addr += rsrc->bo->ptr.gpu;
         memcpy(handles[i], &addr, sizeof(addr));
      } else {
         pipe_resource_reference(res, NULL);
      }
   }
}

void agx_init_state_functions(struct pipe_context *ctx);

void
agx_init_state_functions(struct pipe_context *ctx)
{
   ctx->create_blend_state = agx_create_blend_state;
   ctx->create_depth_stencil_alpha_state = agx_create_zsa_state;
   ctx->create_fs_state = agx_create_shader_state;
   ctx->create_rasterizer_state = agx_create_rs_state;
   ctx->create_sampler_state = agx_create_sampler_state;
   ctx->create_sampler_view = agx_create_sampler_view;
   ctx->create_surface = agx_create_surface;
   ctx->create_vertex_elements_state = agx_create_vertex_elements;
   ctx->create_vs_state = agx_create_shader_state;
   ctx->create_compute_state = agx_create_compute_state;
   ctx->bind_blend_state = agx_bind_blend_state;
   ctx->bind_depth_stencil_alpha_state = agx_bind_zsa_state;
   ctx->bind_sampler_states = agx_bind_sampler_states;
   ctx->bind_fs_state = agx_bind_shader_state;
   ctx->bind_rasterizer_state = agx_bind_rasterizer_state;
   ctx->bind_vertex_elements_state = agx_bind_vertex_elements_state;
   ctx->bind_vs_state = agx_bind_shader_state;
   ctx->bind_compute_state = agx_bind_shader_state;
   ctx->delete_blend_state = agx_delete_state;
   ctx->delete_depth_stencil_alpha_state = agx_delete_state;
   ctx->delete_fs_state = agx_delete_shader_state;
   ctx->delete_compute_state = agx_delete_shader_state;
   ctx->delete_rasterizer_state = agx_delete_state;
   ctx->delete_sampler_state = agx_delete_sampler_state;
   ctx->delete_vertex_elements_state = agx_delete_state;
   ctx->delete_vs_state = agx_delete_shader_state;
   ctx->set_blend_color = agx_set_blend_color;
   ctx->set_clip_state = agx_set_clip_state;
   ctx->set_constant_buffer = agx_set_constant_buffer;
   ctx->set_shader_buffers = agx_set_shader_buffers;
   ctx->set_shader_images = agx_set_shader_images;
   ctx->set_sampler_views = agx_set_sampler_views;
   ctx->set_framebuffer_state = agx_set_framebuffer_state;
   ctx->set_polygon_stipple = agx_set_polygon_stipple;
   ctx->set_sample_mask = agx_set_sample_mask;
   ctx->set_scissor_states = agx_set_scissor_states;
   ctx->set_stencil_ref = agx_set_stencil_ref;
   ctx->set_vertex_buffers = agx_set_vertex_buffers;
   ctx->set_viewport_states = agx_set_viewport_states;
   ctx->sampler_view_destroy = agx_sampler_view_destroy;
   ctx->surface_destroy = agx_surface_destroy;
   ctx->draw_vbo = agx_draw_vbo;
   ctx->launch_grid = agx_launch_grid;
   ctx->set_global_binding = agx_set_global_binding;
   ctx->texture_barrier = agx_texture_barrier;
   ctx->get_compute_state_info = agx_get_compute_state_info;
}
