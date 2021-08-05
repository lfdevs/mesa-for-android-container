/*
 * Copyright (C) 2021 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "util/macros.h"
#include "compiler/shader_enums.h"

#include "panfrost-quirks.h"
#include "pan_cs.h"
#include "pan_encoder.h"
#include "pan_pool.h"

#include "panvk_cs.h"
#include "panvk_private.h"
#include "panvk_varyings.h"

static mali_pixel_format
panvk_varying_hw_format(const struct panvk_device *dev,
                        const struct panvk_varyings_info *varyings,
                        gl_shader_stage stage, unsigned idx)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;
   gl_varying_slot loc = varyings->stage[stage].loc[idx];
   bool fs = stage == MESA_SHADER_FRAGMENT;

   switch (loc) {
   case VARYING_SLOT_PNTC:
   case VARYING_SLOT_PSIZ:
      return (MALI_R16F << 12) |
             (pdev->quirks & HAS_SWIZZLES ?
              panfrost_get_default_swizzle(1) : 0);
   case VARYING_SLOT_POS:
      return ((fs ? MALI_RGBA32F : MALI_SNAP_4) << 12) |
             (pdev->quirks & HAS_SWIZZLES ?
              panfrost_get_default_swizzle(4) : 0);
   default:
      assert(!panvk_varying_is_builtin(stage, loc));
      return pdev->formats[varyings->varying[loc].format].hw;
   }
}

static void
panvk_emit_varying(const struct panvk_device *dev,
                   const struct panvk_varyings_info *varyings,
                   gl_shader_stage stage, unsigned idx,
                   void *attrib)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;
   gl_varying_slot loc = varyings->stage[stage].loc[idx];
   bool fs = stage == MESA_SHADER_FRAGMENT;

   pan_pack(attrib, ATTRIBUTE, cfg) {
      if (!panvk_varying_is_builtin(stage, loc)) {
         cfg.buffer_index = varyings->varying[loc].buf;
         cfg.offset = varyings->varying[loc].offset;
      } else {
         cfg.buffer_index =
            panvk_varying_buf_index(varyings,
                                    panvk_varying_buf_id(fs, loc));
      }
      cfg.offset_enable = !pan_is_bifrost(pdev);
      cfg.format = panvk_varying_hw_format(dev, varyings, stage, idx);
   }
}

void
panvk_emit_varyings(const struct panvk_device *dev,
                    const struct panvk_varyings_info *varyings,
                    gl_shader_stage stage,
                    void *descs)
{
   struct mali_attribute_packed *attrib = descs;

   for (unsigned i = 0; i < varyings->stage[stage].count; i++)
      panvk_emit_varying(dev, varyings, stage, i, attrib++);
}

static void
panvk_emit_varying_buf(const struct panvk_device *dev,
                       const struct panvk_varyings_info *varyings,
                       enum panvk_varying_buf_id id, void *buf)
{
   unsigned buf_idx = panvk_varying_buf_index(varyings, id);
   enum mali_attribute_special special_id = panvk_varying_special_buf_id(id);

   pan_pack(buf, ATTRIBUTE_BUFFER, cfg) {
      if (special_id) {
         cfg.type = 0;
         cfg.special = special_id;
      } else {
         unsigned offset = varyings->buf[buf_idx].address & 63;

         cfg.stride = varyings->buf[buf_idx].stride;
         cfg.size = varyings->buf[buf_idx].size + offset;
         cfg.pointer = varyings->buf[buf_idx].address & ~63ULL;
      }
   }
}

void
panvk_emit_varying_bufs(const struct panvk_device *dev,
                        const struct panvk_varyings_info *varyings,
                        void *descs)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;
   struct mali_attribute_buffer_packed *buf = descs;

   for (unsigned i = 0; i < PANVK_VARY_BUF_MAX; i++) {
      if (varyings->buf_mask & (1 << i))
         panvk_emit_varying_buf(dev, varyings, i, buf++);
   }

   if (pan_is_bifrost(pdev))
      memset(buf, 0, sizeof(*buf));
}

static void
panvk_emit_attrib_buf(const struct panvk_device *dev,
                      const struct panvk_attribs_info *info,
                      const struct panvk_draw_info *draw,
                      const struct panvk_attrib_buf *bufs,
                      unsigned buf_count,
                      unsigned idx, void *desc)
{
   ASSERTED const struct panfrost_device *pdev = &dev->physical_device->pdev;
   const struct panvk_attrib_buf_info *buf_info = &info->buf[idx];

   if (buf_info->special) {
      assert(!pan_is_bifrost(pdev));
      switch (buf_info->special_id) {
      case PAN_VERTEX_ID:
         panfrost_vertex_id(draw->padded_vertex_count, desc,
                            draw->instance_count > 1);
         return;
      case PAN_INSTANCE_ID:
         panfrost_instance_id(draw->padded_vertex_count, desc,
                              draw->instance_count > 1);
         return;
      default:
         unreachable("Invalid attribute ID");
      }
   }

   assert(idx < buf_count);
   const struct panvk_attrib_buf *buf = &bufs[idx];
   unsigned divisor = buf_info->per_instance ?
                      draw->padded_vertex_count : 0;
   unsigned stride = divisor && draw->instance_count == 1 ?
                     0 : buf_info->stride;
   mali_ptr addr = buf->address & ~63ULL;
   unsigned size = buf->size + (buf->address & 63);

   /* TODO: support instanced arrays */
   pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
      if (draw->instance_count > 1 && divisor) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_MODULUS;
         cfg.divisor = divisor;
      }

      cfg.pointer = addr;
      cfg.stride = stride;
      cfg.size = size;
   }
}

void
panvk_emit_attrib_bufs(const struct panvk_device *dev,
                       const struct panvk_attribs_info *info,
                       const struct panvk_attrib_buf *bufs,
                       unsigned buf_count,
                       const struct panvk_draw_info *draw,
                       void *descs)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;
   struct mali_attribute_buffer_packed *buf = descs;

   for (unsigned i = 0; i < info->buf_count; i++)
      panvk_emit_attrib_buf(dev, info, draw, bufs, buf_count, i, buf++);

   /* A NULL entry is needed to stop prefecting on Bifrost */
   if (pan_is_bifrost(pdev))
      memset(buf, 0, sizeof(*buf));
}

static void
panvk_emit_attrib(const struct panvk_device *dev,
                  const struct panvk_attribs_info *attribs,
                  const struct panvk_attrib_buf *bufs,
                  unsigned buf_count,
                  unsigned idx, void *attrib)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;

   pan_pack(attrib, ATTRIBUTE, cfg) {
      cfg.buffer_index = attribs->attrib[idx].buf;
      cfg.offset = attribs->attrib[idx].offset +
                   (bufs[cfg.buffer_index].address & 63);
      cfg.format = pdev->formats[attribs->attrib[idx].format].hw;
   }
}

void
panvk_emit_attribs(const struct panvk_device *dev,
                   const struct panvk_attribs_info *attribs,
                   const struct panvk_attrib_buf *bufs,
                   unsigned buf_count,
                   void *descs)
{
   struct mali_attribute_packed *attrib = descs;

   for (unsigned i = 0; i < attribs->attrib_count; i++)
      panvk_emit_attrib(dev, attribs, bufs, buf_count, i, attrib++);
}

void
panvk_emit_ubos(const struct panvk_pipeline *pipeline,
                const struct panvk_descriptor_state *state,
                void *descs)
{
   struct mali_uniform_buffer_packed *ubos = descs;

   for (unsigned i = 0; i < ARRAY_SIZE(state->sets); i++) {
      const struct panvk_descriptor_set_layout *set_layout =
         pipeline->layout->sets[i].layout;
      const struct panvk_descriptor_set *set = state->sets[i].set;
      unsigned offset = pipeline->layout->sets[i].ubo_offset;

      if (!set_layout)
         continue;

      if (!set) {
         unsigned num_ubos = (set_layout->num_dynoffsets != 0) + set_layout->num_ubos;
         memset(&ubos[offset], 0, num_ubos * sizeof(*ubos));
      } else {
         memcpy(&ubos[offset], set->ubos, set_layout->num_ubos * sizeof(*ubos));
         if (set_layout->num_dynoffsets) {
            pan_pack(&ubos[offset + set_layout->num_ubos], UNIFORM_BUFFER, cfg) {
               cfg.pointer = state->sets[i].dynoffsets.gpu;
               cfg.entries = DIV_ROUND_UP(set->layout->num_dynoffsets, 16);
            }
         }
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(pipeline->sysvals); i++) {
      if (!pipeline->sysvals[i].ids.sysval_count)
         continue;

      pan_pack(&ubos[pipeline->sysvals[i].ubo_idx], UNIFORM_BUFFER, cfg) {
         cfg.pointer = pipeline->sysvals[i].ubo ? :
                       state->sysvals[i];
         cfg.entries = pipeline->sysvals[i].ids.sysval_count;
      }
   }
}

void
panvk_emit_vertex_job(const struct panvk_device *dev,
                      const struct panvk_pipeline *pipeline,
                      const struct panvk_draw_info *draw,
                      void *job)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;
   void *section = pan_section_ptr(job, COMPUTE_JOB, INVOCATION);

   memcpy(section, &draw->invocation, MALI_INVOCATION_LENGTH);

   pan_section_pack(job, COMPUTE_JOB, PARAMETERS, cfg) {
      cfg.job_task_split = 5;
   }

   pan_section_pack(job, COMPUTE_JOB, DRAW, cfg) {
      cfg.draw_descriptor_is_64b = true;
      if (!pan_is_bifrost(pdev))
         cfg.texture_descriptor_is_64b = true;
      cfg.state = pipeline->rsds[MESA_SHADER_VERTEX];
      cfg.attributes = draw->stages[MESA_SHADER_VERTEX].attributes;
      cfg.attribute_buffers = draw->attribute_bufs;
      cfg.varyings = draw->stages[MESA_SHADER_VERTEX].varyings;
      cfg.varying_buffers = draw->varying_bufs;
      cfg.thread_storage = draw->tls;
      cfg.offset_start = draw->offset_start;
      cfg.instance_size = draw->instance_count > 1 ?
                          draw->padded_vertex_count : 1;
      cfg.uniform_buffers = draw->ubos;
      cfg.push_uniforms = draw->stages[PIPE_SHADER_VERTEX].push_constants;
      cfg.textures = draw->textures;
      cfg.samplers = draw->samplers;
   }

   pan_section_pack(job, COMPUTE_JOB, DRAW_PADDING, cfg);
}

void
panvk_emit_tiler_job(const struct panvk_device *dev,
                     const struct panvk_pipeline *pipeline,
                     const struct panvk_draw_info *draw,
                     void *job)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;
   void *section = pan_is_bifrost(pdev) ?
                   pan_section_ptr(job, BIFROST_TILER_JOB, INVOCATION) :
                   pan_section_ptr(job, MIDGARD_TILER_JOB, INVOCATION);

   memcpy(section, &draw->invocation, MALI_INVOCATION_LENGTH);

   section = pan_is_bifrost(pdev) ?
             pan_section_ptr(job, BIFROST_TILER_JOB, PRIMITIVE) :
             pan_section_ptr(job, MIDGARD_TILER_JOB, PRIMITIVE);

   pan_pack(section, PRIMITIVE, cfg) {
      cfg.draw_mode = pipeline->ia.topology;
      if (pipeline->ia.writes_point_size)
         cfg.point_size_array_format = MALI_POINT_SIZE_ARRAY_FORMAT_FP16;

      cfg.first_provoking_vertex = true;
      if (pipeline->ia.primitive_restart)
         cfg.primitive_restart = MALI_PRIMITIVE_RESTART_IMPLICIT;
      cfg.job_task_split = 6;
      /* TODO: indexed draws */
      cfg.index_count = draw->vertex_count;
   }

   section = pan_is_bifrost(pdev) ?
             pan_section_ptr(job, BIFROST_TILER_JOB, PRIMITIVE_SIZE) :
             pan_section_ptr(job, MIDGARD_TILER_JOB, PRIMITIVE_SIZE);
   pan_pack(section, PRIMITIVE_SIZE, cfg) {
      if (pipeline->ia.writes_point_size) {
         cfg.size_array = draw->psiz;
      } else {
         cfg.constant = draw->line_width;
      }
   }

   section = pan_is_bifrost(pdev) ?
             pan_section_ptr(job, BIFROST_TILER_JOB, DRAW) :
             pan_section_ptr(job, MIDGARD_TILER_JOB, DRAW);

   pan_pack(section, DRAW, cfg) {
      cfg.four_components_per_vertex = true;
      cfg.draw_descriptor_is_64b = true;
      if (!pan_is_bifrost(pdev))
         cfg.texture_descriptor_is_64b = true;
      cfg.front_face_ccw = pipeline->rast.front_ccw;
      cfg.cull_front_face = pipeline->rast.cull_front_face;
      cfg.cull_back_face = pipeline->rast.cull_back_face;
      cfg.position = draw->position;
      cfg.state = draw->fs_rsd;
      cfg.attributes = draw->stages[MESA_SHADER_FRAGMENT].attributes;
      cfg.attribute_buffers = draw->attribute_bufs;
      cfg.viewport = draw->viewport;
      cfg.varyings = draw->stages[MESA_SHADER_FRAGMENT].varyings;
      cfg.varying_buffers = cfg.varyings ? draw->varying_bufs : 0;
      if (pan_is_bifrost(pdev))
         cfg.thread_storage = draw->tls;
      else
         cfg.fbd = draw->fb;

      /* For all primitives but lines DRAW.flat_shading_vertex must
       * be set to 0 and the provoking vertex is selected with the
       * PRIMITIVE.first_provoking_vertex field.
       */
      if (pipeline->ia.topology == MALI_DRAW_MODE_LINES ||
          pipeline->ia.topology == MALI_DRAW_MODE_LINE_STRIP ||
          pipeline->ia.topology == MALI_DRAW_MODE_LINE_LOOP) {
         /* The logic is inverted on bifrost. */
         cfg.flat_shading_vertex = pan_is_bifrost(pdev) ?
                                   true : false;
      }

      cfg.offset_start = draw->offset_start;
      cfg.instance_size = draw->instance_count > 1 ?
                         draw->padded_vertex_count : 1;
      cfg.uniform_buffers = draw->ubos;
      cfg.push_uniforms = draw->stages[PIPE_SHADER_FRAGMENT].push_constants;
      cfg.textures = draw->textures;
      cfg.samplers = draw->samplers;

      /* TODO: occlusion queries */
   }

   if (pan_is_bifrost(pdev)) {
      pan_section_pack(job, BIFROST_TILER_JOB, TILER, cfg) {
         cfg.address = draw->tiler_ctx->bifrost;
      }
      pan_section_pack(job, BIFROST_TILER_JOB, DRAW_PADDING, padding);
      pan_section_pack(job, BIFROST_TILER_JOB, PADDING, padding);
   }
}

void
panvk_emit_fragment_job(const struct panvk_device *dev,
                        const struct panvk_framebuffer *fb,
                        mali_ptr fbdesc,
                        void *job)
{
   pan_section_pack(job, FRAGMENT_JOB, HEADER, header) {
      header.type = MALI_JOB_TYPE_FRAGMENT;
      header.index = 1;
   }

   pan_section_pack(job, FRAGMENT_JOB, PAYLOAD, payload) {
      payload.bound_min_x = 0;
      payload.bound_min_y = 0;

      payload.bound_max_x = (fb->width - 1) >> MALI_TILE_SHIFT;
      payload.bound_max_y = (fb->height - 1) >> MALI_TILE_SHIFT;
      payload.framebuffer = fbdesc;
   }
}

void
panvk_emit_viewport(const VkViewport *viewport, const VkRect2D *scissor,
                    void *vpd)
{
   /* The spec says "width must be greater than 0.0" */
   assert(viewport->x >= 0);
   int minx = (int)viewport->x;
   int maxx = (int)(viewport->x + viewport->width);

   /* Viewport height can be negative */
   int miny = MIN2((int)viewport->y, (int)(viewport->y + viewport->height));
   int maxy = MAX2((int)viewport->y, (int)(viewport->y + viewport->height));

   assert(scissor->offset.x >= 0 && scissor->offset.y >= 0);
   miny = MAX2(scissor->offset.x, minx);
   miny = MAX2(scissor->offset.y, miny);
   maxx = MIN2(scissor->offset.x + scissor->extent.width, maxx);
   maxy = MIN2(scissor->offset.y + scissor->extent.height, maxy);

   /* Make sure we don't end up with a max < min when width/height is 0 */
   maxx = maxx > minx ? maxx - 1 : maxx;
   maxy = maxy > miny ? maxy - 1 : maxy;

   assert(viewport->minDepth >= 0.0f && viewport->minDepth <= 1.0f);
   assert(viewport->maxDepth >= 0.0f && viewport->maxDepth <= 1.0f);

   pan_pack(vpd, VIEWPORT, cfg) {
      cfg.scissor_minimum_x = minx;
      cfg.scissor_minimum_y = miny;
      cfg.scissor_maximum_x = maxx;
      cfg.scissor_maximum_y = maxy;
      cfg.minimum_z = MIN2(viewport->minDepth, viewport->maxDepth);
      cfg.maximum_z = MAX2(viewport->minDepth, viewport->maxDepth);
   }
}

void
panvk_sysval_upload_viewport_scale(const VkViewport *viewport,
                                   union panvk_sysval_data *data)
{
   data->f32[0] = 0.5f * viewport->width;
   data->f32[1] = 0.5f * viewport->height;
   data->f32[2] = 0.5f * (viewport->maxDepth - viewport->minDepth);
}

void
panvk_sysval_upload_viewport_offset(const VkViewport *viewport,
                                    union panvk_sysval_data *data)
{
   data->f32[0] = (0.5f * viewport->width) + viewport->x;
   data->f32[1] = (0.5f * viewport->height) + viewport->y;
   data->f32[2] = (0.5f * (viewport->maxDepth - viewport->minDepth)) + viewport->minDepth;
}

static enum mali_bifrost_register_file_format
bifrost_blend_type_from_nir(nir_alu_type nir_type)
{
   switch(nir_type) {
   case 0: /* Render target not in use */
      return 0;
   case nir_type_float16:
      return MALI_BIFROST_REGISTER_FILE_FORMAT_F16;
   case nir_type_float32:
      return MALI_BIFROST_REGISTER_FILE_FORMAT_F32;
   case nir_type_int32:
      return MALI_BIFROST_REGISTER_FILE_FORMAT_I32;
   case nir_type_uint32:
      return MALI_BIFROST_REGISTER_FILE_FORMAT_U32;
   case nir_type_int16:
      return MALI_BIFROST_REGISTER_FILE_FORMAT_I16;
   case nir_type_uint16:
      return MALI_BIFROST_REGISTER_FILE_FORMAT_U16;
   default:
      unreachable("Unsupported blend shader type for NIR alu type");
   }
}

static void
panvk_emit_bifrost_blend(const struct panvk_device *dev,
                         const struct panvk_pipeline *pipeline,
                         unsigned rt, void *bd)
{
   const struct pan_blend_state *blend = &pipeline->blend.state;
   const struct panfrost_device *pdev = &dev->physical_device->pdev;
   const struct pan_blend_rt_state *rts = &blend->rts[rt];

   pan_pack(bd, BLEND, cfg) {
      if (!blend->rt_count || !rts->equation.color_mask) {
         cfg.enable = false;
         cfg.bifrost.internal.mode = MALI_BIFROST_BLEND_MODE_OFF;
         continue;
      }

      cfg.srgb = util_format_is_srgb(rts->format);
      cfg.load_destination = pan_blend_reads_dest(blend->rts[rt].equation);
      cfg.round_to_fb_precision = true;

      const struct util_format_description *format_desc =
         util_format_description(rts->format);
      unsigned chan_size = 0;
      for (unsigned i = 0; i < format_desc->nr_channels; i++)
         chan_size = MAX2(format_desc->channel[0].size, chan_size);

      pan_blend_to_fixed_function_equation(blend->rts[rt].equation,
                                           &cfg.bifrost.equation);

      /* Fixed point constant */
      float fconst =
         pan_blend_get_constant(pan_blend_constant_mask(blend->rts[rt].equation),
                                blend->constants);
      u16 constant = fconst * ((1 << chan_size) - 1);
      constant <<= 16 - chan_size;
      cfg.bifrost.constant = constant;

      if (pan_blend_is_opaque(blend->rts[rt].equation))
         cfg.bifrost.internal.mode = MALI_BIFROST_BLEND_MODE_OPAQUE;
      else
         cfg.bifrost.internal.mode = MALI_BIFROST_BLEND_MODE_FIXED_FUNCTION;

      /* If we want the conversion to work properly,
       * num_comps must be set to 4
       */
      cfg.bifrost.internal.fixed_function.num_comps = 4;
      cfg.bifrost.internal.fixed_function.conversion.memory_format =
         panfrost_format_to_bifrost_blend(pdev, rts->format);
      cfg.bifrost.internal.fixed_function.conversion.register_format =
         bifrost_blend_type_from_nir(pipeline->fs.info.bifrost.blend[rt].type);
      cfg.bifrost.internal.fixed_function.rt = rt;
   }
}

static void
panvk_emit_midgard_blend(const struct panvk_device *dev,
                         const struct panvk_pipeline *pipeline,
                         unsigned rt, void *bd)
{
   const struct pan_blend_state *blend = &pipeline->blend.state;
   const struct pan_blend_rt_state *rts = &blend->rts[rt];

   pan_pack(bd, BLEND, cfg) {
      if (!blend->rt_count || !rts->equation.color_mask) {
         cfg.enable = false;
         continue;
      }

      cfg.srgb = util_format_is_srgb(rts->format);
      cfg.load_destination = pan_blend_reads_dest(blend->rts[rt].equation);
      cfg.round_to_fb_precision = true;
      cfg.midgard.blend_shader = false;
      pan_blend_to_fixed_function_equation(blend->rts[rt].equation,
                                           &cfg.midgard.equation);
      cfg.midgard.constant =
         pan_blend_get_constant(pan_blend_constant_mask(blend->rts[rt].equation),
                                blend->constants);
   }
}

void
panvk_emit_blend(const struct panvk_device *dev,
                 const struct panvk_pipeline *pipeline,
                 unsigned rt, void *bd)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;

   if (pan_is_bifrost(pdev))
      panvk_emit_bifrost_blend(dev, pipeline, rt, bd);
   else
      panvk_emit_midgard_blend(dev, pipeline, rt, bd);
}

void
panvk_emit_blend_constant(const struct panvk_device *dev,
                          const struct panvk_pipeline *pipeline,
                          unsigned rt, const float *constants, void *bd)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;
   float constant = constants[pipeline->blend.constant[rt].index];

   pan_pack(bd, BLEND, cfg) {
      cfg.enable = false;
      if (pan_is_bifrost(pdev)) {
         cfg.bifrost.constant = constant * pipeline->blend.constant[rt].bifrost_factor;
      } else {
         cfg.midgard.constant = constant;
      }
   }
}

void
panvk_emit_dyn_fs_rsd(const struct panvk_device *dev,
                      const struct panvk_pipeline *pipeline,
                      const struct panvk_cmd_state *state,
                      void *rsd)
{
   pan_pack(rsd, RENDERER_STATE, cfg) {
      if (pipeline->dynamic_state_mask & (1 << VK_DYNAMIC_STATE_DEPTH_BIAS)) {
         cfg.depth_units = state->rast.depth_bias.constant_factor * 2.0f;
         cfg.depth_factor = state->rast.depth_bias.slope_factor;
         cfg.depth_bias_clamp = state->rast.depth_bias.clamp;
      }

      if (pipeline->dynamic_state_mask & (1 << VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK)) {
         cfg.stencil_front.mask = state->zs.s_front.compare_mask;
         cfg.stencil_back.mask = state->zs.s_back.compare_mask;
      }

      if (pipeline->dynamic_state_mask & (1 << VK_DYNAMIC_STATE_STENCIL_WRITE_MASK)) {
         cfg.stencil_mask_misc.stencil_mask_front = state->zs.s_front.write_mask;
         cfg.stencil_mask_misc.stencil_mask_back = state->zs.s_back.write_mask;
      }

      if (pipeline->dynamic_state_mask & (1 << VK_DYNAMIC_STATE_STENCIL_REFERENCE)) {
         cfg.stencil_front.reference_value = state->zs.s_front.ref;
         cfg.stencil_back.reference_value = state->zs.s_back.ref;
      }
   }
}

void
panvk_emit_base_fs_rsd(const struct panvk_device *dev,
                       const struct panvk_pipeline *pipeline,
                       void *rsd)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;
   const struct pan_shader_info *info = &pipeline->fs.info;

   pan_pack(rsd, RENDERER_STATE, cfg) {
      if (pipeline->fs.required) {
         pan_shader_prepare_rsd(pdev, info, pipeline->fs.address, &cfg);
         if (pan_is_bifrost(pdev)) {
            cfg.properties.bifrost.allow_forward_pixel_to_kill = info->fs.can_fpk;
         } else {
            /* If either depth or stencil is enabled, discard matters */
            bool zs_enabled =
               (pipeline->zs.z_test && pipeline->zs.z_compare_func != MALI_FUNC_ALWAYS) ||
               pipeline->zs.s_test;

            cfg.properties.midgard.work_register_count = info->work_reg_count;
            cfg.properties.midgard.force_early_z =
               info->fs.can_early_z && !pipeline->ms.alpha_to_coverage &&
               pipeline->zs.z_compare_func == MALI_FUNC_ALWAYS;


            /* Workaround a hardware errata where early-z cannot be enabled
             * when discarding even when the depth buffer is read-only, by
             * lying to the hardware about the discard and setting the
             * reads tilebuffer? flag to compensate */
            cfg.properties.midgard.shader_reads_tilebuffer =
               info->fs.outputs_read ||
               (!zs_enabled && info->fs.can_discard);
            cfg.properties.midgard.shader_contains_discard =
               zs_enabled && info->fs.can_discard;
         }
      } else {
         if (pan_is_bifrost(pdev)) {
            cfg.properties.bifrost.shader_modifies_coverage = true;
            cfg.properties.bifrost.allow_forward_pixel_to_kill = true;
            cfg.properties.bifrost.allow_forward_pixel_to_be_killed = true;
            cfg.properties.bifrost.zs_update_operation = MALI_PIXEL_KILL_STRONG_EARLY;
         } else {
            cfg.shader.shader = 0x1;
            cfg.properties.midgard.work_register_count = 1;
            cfg.properties.depth_source = MALI_DEPTH_SOURCE_FIXED_FUNCTION;
            cfg.properties.midgard.force_early_z = true;
         }
      }

      bool msaa = pipeline->ms.rast_samples > 1;
      cfg.multisample_misc.multisample_enable = msaa;
      cfg.multisample_misc.sample_mask =
         msaa ? pipeline->ms.sample_mask : UINT16_MAX;

      cfg.multisample_misc.depth_function =
         pipeline->zs.z_test ? pipeline->zs.z_compare_func : MALI_FUNC_ALWAYS;

      cfg.multisample_misc.depth_write_mask = pipeline->zs.z_write;
      cfg.multisample_misc.fixed_function_near_discard = !pipeline->rast.clamp_depth;
      cfg.multisample_misc.fixed_function_far_discard = !pipeline->rast.clamp_depth;
      cfg.multisample_misc.shader_depth_range_fixed = true;

      cfg.stencil_mask_misc.stencil_enable = pipeline->zs.s_test;
      cfg.stencil_mask_misc.alpha_to_coverage = pipeline->ms.alpha_to_coverage;
      cfg.stencil_mask_misc.alpha_test_compare_function = MALI_FUNC_ALWAYS;
      cfg.stencil_mask_misc.depth_range_1 = pipeline->rast.depth_bias.enable;
      cfg.stencil_mask_misc.depth_range_2 = pipeline->rast.depth_bias.enable;
      cfg.stencil_mask_misc.single_sampled_lines = pipeline->ms.rast_samples <= 1;

      if (!(pipeline->dynamic_state_mask & (1 << VK_DYNAMIC_STATE_DEPTH_BIAS))) {
         cfg.depth_units = pipeline->rast.depth_bias.constant_factor * 2.0f;
         cfg.depth_factor = pipeline->rast.depth_bias.slope_factor;
         cfg.depth_bias_clamp = pipeline->rast.depth_bias.clamp;
      }

      if (!(pipeline->dynamic_state_mask & (1 << VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK))) {
         cfg.stencil_front.mask = pipeline->zs.s_front.compare_mask;
         cfg.stencil_back.mask = pipeline->zs.s_back.compare_mask;
      }

      if (!(pipeline->dynamic_state_mask & (1 << VK_DYNAMIC_STATE_STENCIL_WRITE_MASK))) {
         cfg.stencil_mask_misc.stencil_mask_front = pipeline->zs.s_front.write_mask;
         cfg.stencil_mask_misc.stencil_mask_back = pipeline->zs.s_back.write_mask;
      }

      if (!(pipeline->dynamic_state_mask & (1 << VK_DYNAMIC_STATE_STENCIL_REFERENCE))) {
         cfg.stencil_front.reference_value = pipeline->zs.s_front.ref;
         cfg.stencil_back.reference_value = pipeline->zs.s_back.ref;
      }

      cfg.stencil_front.compare_function = pipeline->zs.s_front.compare_func;
      cfg.stencil_front.stencil_fail = pipeline->zs.s_front.fail_op;
      cfg.stencil_front.depth_fail = pipeline->zs.s_front.z_fail_op;
      cfg.stencil_front.depth_pass = pipeline->zs.s_front.pass_op;
      cfg.stencil_back.compare_function = pipeline->zs.s_back.compare_func;
      cfg.stencil_back.stencil_fail = pipeline->zs.s_back.fail_op;
      cfg.stencil_back.depth_fail = pipeline->zs.s_back.z_fail_op;
      cfg.stencil_back.depth_pass = pipeline->zs.s_back.pass_op;
   }
}

void
panvk_emit_non_fs_rsd(const struct panvk_device *dev,
                      const struct pan_shader_info *shader_info,
                      mali_ptr shader_ptr,
                      void *rsd)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;

   assert(shader_info->stage != MESA_SHADER_FRAGMENT);

   pan_pack(rsd, RENDERER_STATE, cfg) {
      pan_shader_prepare_rsd(pdev, shader_info, shader_ptr, &cfg);
   }
}

void
panvk_emit_bifrost_tiler_context(const struct panvk_device *dev,
                                 unsigned width, unsigned height,
                                 const struct panfrost_ptr *descs)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;

   pan_pack(descs->cpu + MALI_BIFROST_TILER_LENGTH, BIFROST_TILER_HEAP, cfg) {
      cfg.size = pdev->tiler_heap->size;
      cfg.base = pdev->tiler_heap->ptr.gpu;
      cfg.bottom = pdev->tiler_heap->ptr.gpu;
      cfg.top = pdev->tiler_heap->ptr.gpu + pdev->tiler_heap->size;
   }

   pan_pack(descs->cpu, BIFROST_TILER, cfg) {
      cfg.hierarchy_mask = 0x28;
      cfg.fb_width = width;
      cfg.fb_height = height;
      cfg.heap = descs->gpu + MALI_BIFROST_TILER_LENGTH;
   }
}

unsigned
panvk_emit_fb(const struct panvk_device *dev,
              const struct panvk_batch *batch,
              const struct panvk_subpass *subpass,
              const struct panvk_pipeline *pipeline,
              const struct panvk_framebuffer *fb,
              const struct panvk_clear_value *clears,
              const struct pan_tls_info *tlsinfo,
              const struct pan_tiler_context *tilerctx,
              void *desc)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;
   struct panvk_image_view *view;
   bool crc_valid[8] = { false };
   struct pan_fb_info fbinfo = {
      .width = fb->width,
      .height = fb->height,
      .extent.maxx = fb->width - 1,
      .extent.maxy = fb->height - 1,
      .nr_samples = 1,
   };

   for (unsigned cb = 0; cb < subpass->color_count; cb++) {
      int idx = subpass->color_attachments[cb].idx;
      view = idx != VK_ATTACHMENT_UNUSED ?
             fb->attachments[idx].iview : NULL;
      if (!view)
         continue;
      fbinfo.rts[cb].view = &view->pview;
      fbinfo.rts[cb].clear = subpass->color_attachments[idx].clear;
      fbinfo.rts[cb].crc_valid = &crc_valid[cb];

      memcpy(fbinfo.rts[cb].clear_value, clears[idx].color,
             sizeof(fbinfo.rts[cb].clear_value));
      fbinfo.nr_samples =
         MAX2(fbinfo.nr_samples, view->pview.image->layout.nr_samples);
   }

   if (subpass->zs_attachment.idx != VK_ATTACHMENT_UNUSED) {
      view = fb->attachments[subpass->zs_attachment.idx].iview;
      const struct util_format_description *fdesc =
         util_format_description(view->pview.format);

      fbinfo.nr_samples =
         MAX2(fbinfo.nr_samples, view->pview.image->layout.nr_samples);

      if (util_format_has_depth(fdesc)) {
         fbinfo.zs.clear.z = subpass->zs_attachment.clear;
         fbinfo.zs.clear_value.depth = clears[subpass->zs_attachment.idx].depth;
         fbinfo.zs.view.zs = &view->pview;
      }

      if (util_format_has_depth(fdesc)) {
         fbinfo.zs.clear.s = subpass->zs_attachment.clear;
         fbinfo.zs.clear_value.stencil = clears[subpass->zs_attachment.idx].depth;
         if (!fbinfo.zs.view.zs)
            fbinfo.zs.view.s = &view->pview;
      }
   }

   return pan_emit_fbd(pdev, &fbinfo, tlsinfo, tilerctx, desc);
}
