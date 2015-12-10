/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2014 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#ifndef ILO_BUILDER_3D_BOTTOM_H
#define ILO_BUILDER_3D_BOTTOM_H

#include "genhw/genhw.h"
#include "intel_winsys.h"

#include "ilo_core.h"
#include "ilo_dev.h"
#include "ilo_state_cc.h"
#include "ilo_state_raster.h"
#include "ilo_state_sbe.h"
#include "ilo_state_shader.h"
#include "ilo_state_viewport.h"
#include "ilo_state_zs.h"
#include "ilo_vma.h"
#include "ilo_builder.h"
#include "ilo_builder_3d_top.h"

static inline void
gen6_3DSTATE_CLIP(struct ilo_builder *builder,
                  const struct ilo_state_raster *rs)
{
   const uint8_t cmd_len = 4;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_CLIP) | (cmd_len - 2);
   /* see raster_set_gen6_3DSTATE_CLIP() */
   dw[1] = rs->clip[0];
   dw[2] = rs->clip[1];
   dw[3] = rs->clip[2];
}

static inline void
gen6_3DSTATE_SF(struct ilo_builder *builder,
                const struct ilo_state_raster *rs,
                const struct ilo_state_sbe *sbe)
{
   const uint8_t cmd_len = 20;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 6);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_SF) | (cmd_len - 2);
   /* see sbe_set_gen8_3DSTATE_SBE() */
   dw[1] = sbe->sbe[0];

   /* see raster_set_gen7_3DSTATE_SF() */
   dw[2] = rs->sf[0];
   dw[3] = rs->sf[1];
   dw[4] = rs->sf[2];
   dw[5] = rs->raster[1];
   dw[6] = rs->raster[2];
   dw[7] = rs->raster[3];

   /* see sbe_set_gen8_3DSTATE_SBE_SWIZ() */
   memcpy(&dw[8], sbe->swiz, sizeof(*dw) * 8);

   dw[16] = sbe->sbe[1];
   dw[17] = sbe->sbe[2];
   /* WrapShortest enables */
   dw[18] = 0;
   dw[19] = 0;
}

static inline void
gen7_3DSTATE_SF(struct ilo_builder *builder,
                const struct ilo_state_raster *rs)
{
   const uint8_t cmd_len = (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) ? 4 : 7;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_SF) | (cmd_len - 2);

   /* see raster_set_gen7_3DSTATE_SF() or raster_set_gen8_3DSTATE_SF() */
   dw[1] = rs->sf[0];
   dw[2] = rs->sf[1];
   dw[3] = rs->sf[2];
   if (ilo_dev_gen(builder->dev) < ILO_GEN(8)) {
      dw[4] = rs->raster[1];
      dw[5] = rs->raster[2];
      dw[6] = rs->raster[3];
   }
}

static inline void
gen7_3DSTATE_SBE(struct ilo_builder *builder,
                 const struct ilo_state_sbe *sbe)
{
   const uint8_t cmd_len = 14;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 7.5);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_SBE) | (cmd_len - 2);
   /* see sbe_set_gen8_3DSTATE_SBE() and sbe_set_gen8_3DSTATE_SBE_SWIZ() */
   dw[1] = sbe->sbe[0];
   memcpy(&dw[2], sbe->swiz, sizeof(*dw) * 8);
   dw[10] = sbe->sbe[1];
   dw[11] = sbe->sbe[2];

   /* WrapShortest enables */
   dw[12] = 0;
   dw[13] = 0;
}

static inline void
gen8_3DSTATE_SBE(struct ilo_builder *builder,
                 const struct ilo_state_sbe *sbe)
{
   const uint8_t cmd_len = 4;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   /* see sbe_set_gen8_3DSTATE_SBE() */
   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_SBE) | (cmd_len - 2);
   dw[1] = sbe->sbe[0];
   dw[2] = sbe->sbe[1];
   dw[3] = sbe->sbe[2];
}

static inline void
gen8_3DSTATE_SBE_SWIZ(struct ilo_builder *builder,
                      const struct ilo_state_sbe *sbe)
{
   const uint8_t cmd_len = 11;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_SBE_SWIZ) | (cmd_len - 2);
   /* see sbe_set_gen8_3DSTATE_SBE_SWIZ() */
   memcpy(&dw[1], sbe->swiz, sizeof(*dw) * 8);
   /* WrapShortest enables */
   dw[9] = 0;
   dw[10] = 0;
}

static inline void
gen8_3DSTATE_RASTER(struct ilo_builder *builder,
                    const struct ilo_state_raster *rs)
{
   const uint8_t cmd_len = 5;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_RASTER) | (cmd_len - 2);
   /* see raster_set_gen8_3DSTATE_RASTER() */
   dw[1] = rs->raster[0];
   dw[2] = rs->raster[1];
   dw[3] = rs->raster[2];
   dw[4] = rs->raster[3];
}

static inline void
gen6_3DSTATE_WM(struct ilo_builder *builder,
                const struct ilo_state_raster *rs,
                const struct ilo_state_ps *ps,
                uint32_t kernel_offset,
                struct intel_bo *scratch_bo)
{
   const uint8_t cmd_len = 9;
   uint32_t *dw;
   unsigned pos;

   ILO_DEV_ASSERT(builder->dev, 6, 6);

   pos = ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_WM) | (cmd_len - 2);
   dw[1] = kernel_offset;
   /* see raster_set_gen6_3dstate_wm() and ps_set_gen6_3dstate_wm() */
   dw[2] = ps->ps[0];
   dw[3] = ps->ps[1];
   dw[4] = rs->wm[0] | ps->ps[2];
   dw[5] = rs->wm[1] | ps->ps[3];
   dw[6] = rs->wm[2] | ps->ps[4];
   dw[7] = 0; /* kernel 1 */
   dw[8] = 0; /* kernel 2 */

   if (ilo_state_ps_get_scratch_size(ps)) {
      ilo_builder_batch_reloc(builder, pos + 2, scratch_bo,
            ps->ps[0], 0);
   }
}

static inline void
gen7_3DSTATE_WM(struct ilo_builder *builder,
                const struct ilo_state_raster *rs,
                const struct ilo_state_ps *ps)
{
   const uint8_t cmd_len = 3;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 7.5);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_WM) | (cmd_len - 2);
   /* see raster_set_gen8_3DSTATE_WM() and ps_set_gen7_3dstate_wm() */
   dw[1] = rs->wm[0] | ps->ps[0];
   dw[2] = ps->ps[1];
}

static inline void
gen8_3DSTATE_WM(struct ilo_builder *builder,
                const struct ilo_state_raster *rs)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_WM) | (cmd_len - 2);
   /* see raster_set_gen8_3DSTATE_WM() */
   dw[1] = rs->wm[0];
}

static inline void
gen8_3DSTATE_WM_DEPTH_STENCIL(struct ilo_builder *builder,
                              const struct ilo_state_cc *cc)
{
   const uint8_t cmd_len = 3;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_WM_DEPTH_STENCIL) | (cmd_len - 2);
   /* see cc_set_gen8_3DSTATE_WM_DEPTH_STENCIL() */
   dw[1] = cc->ds[0];
   dw[2] = cc->ds[1];
}

static inline void
gen8_3DSTATE_WM_HZ_OP(struct ilo_builder *builder,
                      const struct ilo_state_raster *rs,
                      uint16_t width, uint16_t height)
{
   const uint8_t cmd_len = 5;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_WM_HZ_OP) | (cmd_len - 2);
   /* see raster_set_gen8_3dstate_wm_hz_op() */
   dw[1] = rs->wm[1];
   dw[2] = 0;
   /* exclusive */
   dw[3] = height << 16 | width;
   dw[4] = rs->wm[2];
}

static inline void
gen8_disable_3DSTATE_WM_HZ_OP(struct ilo_builder *builder)
{
   const uint8_t cmd_len = 5;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_WM_HZ_OP) | (cmd_len - 2);
   dw[1] = 0;
   dw[2] = 0;
   dw[3] = 0;
   dw[4] = 0;
}

static inline void
gen8_3DSTATE_WM_CHROMAKEY(struct ilo_builder *builder)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_WM_CHROMAKEY) | (cmd_len - 2);
   dw[1] = 0;
}

static inline void
gen7_3DSTATE_PS(struct ilo_builder *builder,
                const struct ilo_state_ps *ps,
                uint32_t kernel_offset,
                struct intel_bo *scratch_bo)
{
   const uint8_t cmd_len = 8;
   uint32_t *dw;
   unsigned pos;

   ILO_DEV_ASSERT(builder->dev, 7, 7.5);

   pos = ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_PS) | (cmd_len - 2);
   dw[1] = kernel_offset;
   /* see ps_set_gen7_3DSTATE_PS() */
   dw[2] = ps->ps[2];
   dw[3] = ps->ps[3];
   dw[4] = ps->ps[4];
   dw[5] = ps->ps[5];
   dw[6] = 0; /* kernel 1 */
   dw[7] = 0; /* kernel 2 */

   if (ilo_state_ps_get_scratch_size(ps)) {
      ilo_builder_batch_reloc(builder, pos + 3, scratch_bo,
            ps->ps[3], 0);
   }
}

static inline void
gen8_3DSTATE_PS(struct ilo_builder *builder,
                const struct ilo_state_ps *ps,
                uint32_t kernel_offset,
                struct intel_bo *scratch_bo)
{
   const uint8_t cmd_len = 12;
   uint32_t *dw;
   unsigned pos;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   pos = ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_PS) | (cmd_len - 2);
   dw[1] = kernel_offset;
   dw[2] = 0;
   /* see ps_set_gen8_3DSTATE_PS() */
   dw[3] = ps->ps[0];
   dw[4] = ps->ps[1];
   dw[5] = 0;
   dw[6] = ps->ps[2];
   dw[7] = ps->ps[3];
   dw[8] = 0; /* kernel 1 */
   dw[9] = 0;
   dw[10] = 0; /* kernel 2 */
   dw[11] = 0;

   if (ilo_state_ps_get_scratch_size(ps)) {
      ilo_builder_batch_reloc64(builder, pos + 4, scratch_bo,
            ps->ps[1], 0);
   }
}

static inline void
gen8_3DSTATE_PS_EXTRA(struct ilo_builder *builder,
                      const struct ilo_state_ps *ps)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_PS_EXTRA) | (cmd_len - 2);
   /* see ps_set_gen8_3DSTATE_PS_EXTRA() */
   dw[1] = ps->ps[4];
}

static inline void
gen8_3DSTATE_PS_BLEND(struct ilo_builder *builder,
                      const struct ilo_state_cc *cc)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_PS_BLEND) | (cmd_len - 2);
   /* see cc_set_gen8_3DSTATE_PS_BLEND() */
   dw[1] = cc->blend[0];
}

static inline void
gen6_3DSTATE_CONSTANT_PS(struct ilo_builder *builder,
                         const uint32_t *bufs, const int *sizes,
                         int num_bufs)
{
   gen6_3dstate_constant(builder, GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_PS,
         bufs, sizes, num_bufs);
}

static inline void
gen7_3DSTATE_CONSTANT_PS(struct ilo_builder *builder,
                         const uint32_t *bufs, const int *sizes,
                         int num_bufs)
{
   gen7_3dstate_constant(builder, GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_PS,
         bufs, sizes, num_bufs);
}

static inline void
gen7_3DSTATE_BINDING_TABLE_POINTERS_PS(struct ilo_builder *builder,
                                       uint32_t binding_table)
{
   ILO_DEV_ASSERT(builder->dev, 7, 8);

   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_BINDING_TABLE_POINTERS_PS,
         binding_table);
}

static inline void
gen7_3DSTATE_SAMPLER_STATE_POINTERS_PS(struct ilo_builder *builder,
                                       uint32_t sampler_state)
{
   ILO_DEV_ASSERT(builder->dev, 7, 8);

   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_SAMPLER_STATE_POINTERS_PS,
         sampler_state);
}

static inline void
gen6_3DSTATE_MULTISAMPLE(struct ilo_builder *builder,
                         const struct ilo_state_raster *rs,
                         const struct ilo_state_sample_pattern *pattern,
                         uint8_t sample_count)
{
   const uint8_t cmd_len = (ilo_dev_gen(builder->dev) >= ILO_GEN(7)) ? 4 : 3;
   const uint32_t *packed = (const uint32_t *)
      ilo_state_sample_pattern_get_packed_offsets(pattern,
            builder->dev, sample_count);
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 7.5);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_MULTISAMPLE) | (cmd_len - 2);
   /* see raster_set_gen8_3DSTATE_MULTISAMPLE() */
   dw[1] = rs->sample[0];

   /* see sample_pattern_set_gen8_3DSTATE_SAMPLE_PATTERN() */
   dw[2] = (sample_count >= 4) ? packed[0] : 0;
   if (ilo_dev_gen(builder->dev) >= ILO_GEN(7))
      dw[3] = (sample_count >= 8) ? packed[1] : 0;
}

static inline void
gen8_3DSTATE_MULTISAMPLE(struct ilo_builder *builder,
                         const struct ilo_state_raster *rs)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_MULTISAMPLE) | (cmd_len - 2);
   /* see raster_set_gen8_3DSTATE_MULTISAMPLE() */
   dw[1] = rs->sample[0];
}

static inline void
gen8_3DSTATE_SAMPLE_PATTERN(struct ilo_builder *builder,
                            const struct ilo_state_sample_pattern *pattern)
{
   const uint8_t cmd_len = 9;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_SAMPLE_PATTERN) | (cmd_len - 2);
   dw[1] = 0;
   dw[2] = 0;
   dw[3] = 0;
   dw[4] = 0;
   /* see sample_pattern_set_gen8_3DSTATE_SAMPLE_PATTERN() */
   dw[5] = ((const uint32_t *) pattern->pattern_8x)[1];
   dw[6] = ((const uint32_t *) pattern->pattern_8x)[0];
   dw[7] = ((const uint32_t *) pattern->pattern_4x)[0];
   dw[8] = pattern->pattern_1x[0] << 16 |
           ((const uint16_t *) pattern->pattern_2x)[0];
}

static inline void
gen6_3DSTATE_SAMPLE_MASK(struct ilo_builder *builder,
                         const struct ilo_state_raster *rs)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_SAMPLE_MASK) | (cmd_len - 2);
   /* see raster_set_gen6_3DSTATE_SAMPLE_MASK() */
   dw[1] = rs->sample[1];
}

static inline void
gen6_3DSTATE_DRAWING_RECTANGLE(struct ilo_builder *builder,
                               unsigned x, unsigned y,
                               unsigned width, unsigned height)
{
   const uint8_t cmd_len = 4;
   unsigned xmax = x + width - 1;
   unsigned ymax = y + height - 1;
   unsigned rect_limit;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   if (ilo_dev_gen(builder->dev) >= ILO_GEN(7)) {
      rect_limit = 16383;
   }
   else {
      /*
       * From the Sandy Bridge PRM, volume 2 part 1, page 230:
       *
       *     "[DevSNB] Errata: This field (Clipped Drawing Rectangle Y Min)
       *      must be an even number"
       */
      assert(y % 2 == 0);

      rect_limit = 8191;
   }

   if (x > rect_limit) x = rect_limit;
   if (y > rect_limit) y = rect_limit;
   if (xmax > rect_limit) xmax = rect_limit;
   if (ymax > rect_limit) ymax = rect_limit;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_DRAWING_RECTANGLE) | (cmd_len - 2);
   dw[1] = y << 16 | x;
   dw[2] = ymax << 16 | xmax;
   /*
    * There is no need to set the origin.  It is intended to support front
    * buffer rendering.
    */
   dw[3] = 0;
}

static inline void
gen6_3DSTATE_POLY_STIPPLE_OFFSET(struct ilo_builder *builder,
                                 const struct ilo_state_poly_stipple *stipple)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_POLY_STIPPLE_OFFSET) | (cmd_len - 2);
   /* constant */
   dw[1] = 0;
}

static inline void
gen6_3DSTATE_POLY_STIPPLE_PATTERN(struct ilo_builder *builder,
                                  const struct ilo_state_poly_stipple *stipple)
{
   const uint8_t cmd_len = 33;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_POLY_STIPPLE_PATTERN) | (cmd_len - 2);
   /* see poly_stipple_set_gen6_3DSTATE_POLY_STIPPLE_PATTERN() */
   memcpy(&dw[1], stipple->stipple, sizeof(stipple->stipple));
}

static inline void
gen6_3DSTATE_LINE_STIPPLE(struct ilo_builder *builder,
                          const struct ilo_state_line_stipple *stipple)
{
   const uint8_t cmd_len = 3;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_LINE_STIPPLE) | (cmd_len - 2);
   /* see line_stipple_set_gen6_3DSTATE_LINE_STIPPLE() */
   dw[1] = stipple->stipple[0];
   dw[2] = stipple->stipple[1];
}

static inline void
gen6_3DSTATE_AA_LINE_PARAMETERS(struct ilo_builder *builder,
                                const struct ilo_state_raster *rs)
{
   const uint8_t cmd_len = 3;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_AA_LINE_PARAMETERS) | (cmd_len - 2);
   /* constant */
   dw[1] = 0 << GEN6_AA_LINE_DW1_BIAS__SHIFT |
           0 << GEN6_AA_LINE_DW1_SLOPE__SHIFT;
   dw[2] = 0 << GEN6_AA_LINE_DW2_CAP_BIAS__SHIFT |
           0 << GEN6_AA_LINE_DW2_CAP_SLOPE__SHIFT;
}

static inline void
gen6_3DSTATE_DEPTH_BUFFER(struct ilo_builder *builder,
                          const struct ilo_state_zs *zs)
{
   const uint32_t cmd = (ilo_dev_gen(builder->dev) >= ILO_GEN(7)) ?
      GEN7_RENDER_CMD(3D, 3DSTATE_DEPTH_BUFFER) :
      GEN6_RENDER_CMD(3D, 3DSTATE_DEPTH_BUFFER);
   const uint8_t cmd_len = (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) ? 8 : 7;
   uint32_t *dw;
   unsigned pos;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   pos = ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = cmd | (cmd_len - 2);

   /*
    * see zs_set_gen6_3DSTATE_DEPTH_BUFFER() and
    * zs_set_gen7_3DSTATE_DEPTH_BUFFER()
    */
   if (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) {
      dw[1] = zs->depth[0];
      dw[2] = 0;
      dw[3] = 0;
      dw[4] = zs->depth[2];
      dw[5] = zs->depth[3];
      dw[6] = 0;
      dw[7] = zs->depth[4];

      dw[5] |= builder->mocs << GEN8_DEPTH_DW5_MOCS__SHIFT;

      if (zs->z_vma) {
         ilo_builder_batch_reloc64(builder, pos + 2, zs->z_vma->bo,
               zs->z_vma->bo_offset + zs->depth[1],
               (zs->z_readonly) ? 0 : INTEL_RELOC_WRITE);
      }
   } else {
      dw[1] = zs->depth[0];
      dw[2] = 0;
      dw[3] = zs->depth[2];
      dw[4] = zs->depth[3];
      dw[5] = 0;
      dw[6] = zs->depth[4];

      if (ilo_dev_gen(builder->dev) >= ILO_GEN(7))
         dw[4] |= builder->mocs << GEN7_DEPTH_DW4_MOCS__SHIFT;
      else
         dw[6] |= builder->mocs << GEN6_DEPTH_DW6_MOCS__SHIFT;

      if (zs->z_vma) {
         ilo_builder_batch_reloc(builder, pos + 2, zs->z_vma->bo,
               zs->z_vma->bo_offset + zs->depth[1],
               (zs->z_readonly) ? 0 : INTEL_RELOC_WRITE);
      }
   }
}

static inline void
gen6_3DSTATE_STENCIL_BUFFER(struct ilo_builder *builder,
                            const struct ilo_state_zs *zs)
{
   const uint32_t cmd = (ilo_dev_gen(builder->dev) >= ILO_GEN(7)) ?
      GEN7_RENDER_CMD(3D, 3DSTATE_STENCIL_BUFFER) :
      GEN6_RENDER_CMD(3D, 3DSTATE_STENCIL_BUFFER);
   const uint8_t cmd_len = (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) ? 5 : 3;
   uint32_t *dw;
   unsigned pos;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   pos = ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = cmd | (cmd_len - 2);

   /* see zs_set_gen6_3DSTATE_STENCIL_BUFFER() */
   if (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) {
      dw[1] = zs->stencil[0];
      dw[2] = 0;
      dw[3] = 0;
      dw[4] = zs->stencil[2];

      dw[1] |= builder->mocs << GEN8_STENCIL_DW1_MOCS__SHIFT;

      if (zs->s_vma) {
         ilo_builder_batch_reloc64(builder, pos + 2, zs->s_vma->bo,
               zs->s_vma->bo_offset + zs->stencil[1],
               (zs->s_readonly) ? 0 : INTEL_RELOC_WRITE);
      }
   } else {
      dw[1] = zs->stencil[0];
      dw[2] = 0;

      dw[1] |= builder->mocs << GEN6_STENCIL_DW1_MOCS__SHIFT;

      if (zs->s_vma) {
         ilo_builder_batch_reloc(builder, pos + 2, zs->s_vma->bo,
               zs->s_vma->bo_offset + zs->stencil[1],
               (zs->s_readonly) ? 0 : INTEL_RELOC_WRITE);
      }
   }
}

static inline void
gen6_3DSTATE_HIER_DEPTH_BUFFER(struct ilo_builder *builder,
                               const struct ilo_state_zs *zs)
{
   const uint32_t cmd = (ilo_dev_gen(builder->dev) >= ILO_GEN(7)) ?
      GEN7_RENDER_CMD(3D, 3DSTATE_HIER_DEPTH_BUFFER) :
      GEN6_RENDER_CMD(3D, 3DSTATE_HIER_DEPTH_BUFFER);
   const uint8_t cmd_len = (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) ? 5 : 3;
   uint32_t *dw;
   unsigned pos;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   pos = ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = cmd | (cmd_len - 2);

   /* see zs_set_gen6_3DSTATE_HIER_DEPTH_BUFFER() */
   if (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) {
      dw[1] = zs->hiz[0];
      dw[2] = 0;
      dw[3] = 0;
      dw[4] = zs->hiz[2];

      dw[1] |= builder->mocs << GEN8_HIZ_DW1_MOCS__SHIFT;

      if (zs->hiz_vma) {
         ilo_builder_batch_reloc64(builder, pos + 2, zs->hiz_vma->bo,
               zs->hiz_vma->bo_offset + zs->hiz[1],
               (zs->z_readonly) ? 0 : INTEL_RELOC_WRITE);
      }
   } else {
      dw[1] = zs->hiz[0];
      dw[2] = 0;

      dw[1] |= builder->mocs << GEN6_HIZ_DW1_MOCS__SHIFT;

      if (zs->hiz_vma) {
         ilo_builder_batch_reloc(builder, pos + 2, zs->hiz_vma->bo,
               zs->hiz_vma->bo_offset + zs->hiz[1],
               (zs->z_readonly) ? 0 : INTEL_RELOC_WRITE);
      }
   }
}

static inline void
gen6_3DSTATE_CLEAR_PARAMS(struct ilo_builder *builder,
                          uint32_t clear_val)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 6);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_CLEAR_PARAMS) |
           GEN6_CLEAR_PARAMS_DW0_VALID |
           (cmd_len - 2);
   dw[1] = clear_val;
}

static inline void
gen7_3DSTATE_CLEAR_PARAMS(struct ilo_builder *builder,
                          uint32_t clear_val)
{
   const uint8_t cmd_len = 3;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_CLEAR_PARAMS) | (cmd_len - 2);
   dw[1] = clear_val;
   dw[2] = GEN7_CLEAR_PARAMS_DW2_VALID;
}

static inline void
gen6_3DSTATE_VIEWPORT_STATE_POINTERS(struct ilo_builder *builder,
                                     uint32_t clip_viewport,
                                     uint32_t sf_viewport,
                                     uint32_t cc_viewport)
{
   const uint8_t cmd_len = 4;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 6);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_VIEWPORT_STATE_POINTERS) |
           GEN6_VP_PTR_DW0_CLIP_CHANGED |
           GEN6_VP_PTR_DW0_SF_CHANGED |
           GEN6_VP_PTR_DW0_CC_CHANGED |
           (cmd_len - 2);
   dw[1] = clip_viewport;
   dw[2] = sf_viewport;
   dw[3] = cc_viewport;
}

static inline void
gen6_3DSTATE_SCISSOR_STATE_POINTERS(struct ilo_builder *builder,
                                    uint32_t scissor_rect)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_SCISSOR_STATE_POINTERS) |
           (cmd_len - 2);
   dw[1] = scissor_rect;
}

static inline void
gen6_3DSTATE_CC_STATE_POINTERS(struct ilo_builder *builder,
                               uint32_t blend_state,
                               uint32_t depth_stencil_state,
                               uint32_t color_calc_state)
{
   const uint8_t cmd_len = 4;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 6);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_CC_STATE_POINTERS) | (cmd_len - 2);
   dw[1] = blend_state | GEN6_CC_PTR_DW1_BLEND_CHANGED;
   dw[2] = depth_stencil_state | GEN6_CC_PTR_DW2_ZS_CHANGED;
   dw[3] = color_calc_state | GEN6_CC_PTR_DW3_CC_CHANGED;
}

static inline void
gen7_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP(struct ilo_builder *builder,
                                             uint32_t sf_clip_viewport)
{
   ILO_DEV_ASSERT(builder->dev, 7, 8);

   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP,
         sf_clip_viewport);
}

static inline void
gen7_3DSTATE_VIEWPORT_STATE_POINTERS_CC(struct ilo_builder *builder,
                                        uint32_t cc_viewport)
{
   ILO_DEV_ASSERT(builder->dev, 7, 8);

   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_VIEWPORT_STATE_POINTERS_CC,
         cc_viewport);
}

static inline void
gen7_3DSTATE_CC_STATE_POINTERS(struct ilo_builder *builder,
                               uint32_t color_calc_state)
{
   ILO_DEV_ASSERT(builder->dev, 7, 8);

   if (ilo_dev_gen(builder->dev) >= ILO_GEN(8))
      color_calc_state |= 1;

   gen7_3dstate_pointer(builder,
         GEN6_RENDER_OPCODE_3DSTATE_CC_STATE_POINTERS, color_calc_state);
}

static inline void
gen7_3DSTATE_DEPTH_STENCIL_STATE_POINTERS(struct ilo_builder *builder,
                                          uint32_t depth_stencil_state)
{
   ILO_DEV_ASSERT(builder->dev, 7, 8);

   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_DEPTH_STENCIL_STATE_POINTERS,
         depth_stencil_state);
}

static inline void
gen7_3DSTATE_BLEND_STATE_POINTERS(struct ilo_builder *builder,
                                  uint32_t blend_state)
{
   ILO_DEV_ASSERT(builder->dev, 7, 8);

   if (ilo_dev_gen(builder->dev) >= ILO_GEN(8))
      blend_state |= 1;

   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_BLEND_STATE_POINTERS,
         blend_state);
}

static inline uint32_t
gen6_CLIP_VIEWPORT(struct ilo_builder *builder,
                   const struct ilo_state_viewport *vp)
{
   const int state_align = 32;
   const int state_len = 4 * vp->count;
   uint32_t state_offset, *dw;
   int i;

   ILO_DEV_ASSERT(builder->dev, 6, 6);

   state_offset = ilo_builder_dynamic_pointer(builder,
         ILO_BUILDER_ITEM_CLIP_VIEWPORT, state_align, state_len, &dw);

   for (i = 0; i < vp->count; i++) {
      /* see viewport_matrix_set_gen7_SF_CLIP_VIEWPORT() */
      dw[0] = vp->sf_clip[i][8];
      dw[1] = vp->sf_clip[i][9];
      dw[2] = vp->sf_clip[i][10];
      dw[3] = vp->sf_clip[i][11];

      dw += 4;
   }

   return state_offset;
}

static inline uint32_t
gen6_SF_VIEWPORT(struct ilo_builder *builder,
                 const struct ilo_state_viewport *vp)
{
   const int state_align = 32;
   const int state_len = 8 * vp->count;
   uint32_t state_offset, *dw;
   int i;

   ILO_DEV_ASSERT(builder->dev, 6, 6);

   state_offset = ilo_builder_dynamic_pointer(builder,
         ILO_BUILDER_ITEM_SF_VIEWPORT, state_align, state_len, &dw);

   for (i = 0; i < vp->count; i++) {
      /* see viewport_matrix_set_gen7_SF_CLIP_VIEWPORT() */
      memcpy(dw, vp->sf_clip[i], sizeof(*dw) * 8);

      dw += 8;
   }

   return state_offset;
}

static inline uint32_t
gen7_SF_CLIP_VIEWPORT(struct ilo_builder *builder,
                      const struct ilo_state_viewport *vp)
{
   const int state_align = 64;
   const int state_len = 16 * vp->count;

   ILO_DEV_ASSERT(builder->dev, 7, 8);

   /* see viewport_matrix_set_gen7_SF_CLIP_VIEWPORT() */
   return ilo_builder_dynamic_write(builder, ILO_BUILDER_ITEM_SF_VIEWPORT,
         state_align, state_len, (const uint32_t *) vp->sf_clip);
}

static inline uint32_t
gen6_CC_VIEWPORT(struct ilo_builder *builder,
                 const struct ilo_state_viewport *vp)
{
   const int state_align = 32;
   const int state_len = 2 * vp->count;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   /* see viewport_matrix_set_gen6_CC_VIEWPORT() */
   return ilo_builder_dynamic_write(builder, ILO_BUILDER_ITEM_CC_VIEWPORT,
         state_align, state_len, (const uint32_t *) vp->cc);
}

static inline uint32_t
gen6_SCISSOR_RECT(struct ilo_builder *builder,
                  const struct ilo_state_viewport *vp)
{
   const int state_align = 32;
   const int state_len = 2 * vp->count;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   /* see viewport_scissor_set_gen6_SCISSOR_RECT() */
   return ilo_builder_dynamic_write(builder, ILO_BUILDER_ITEM_SCISSOR_RECT,
         state_align, state_len, (const uint32_t *) vp->scissor);
}

static inline uint32_t
gen6_COLOR_CALC_STATE(struct ilo_builder *builder,
                      const struct ilo_state_cc *cc)
{
   const int state_align = 64;
   const int state_len = 6;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   /* see cc_params_set_gen6_COLOR_CALC_STATE() */
   return ilo_builder_dynamic_write(builder, ILO_BUILDER_ITEM_COLOR_CALC,
         state_align, state_len, cc->cc);
}

static inline uint32_t
gen6_DEPTH_STENCIL_STATE(struct ilo_builder *builder,
                         const struct ilo_state_cc *cc)
{
   const int state_align = 64;
   const int state_len = 3;

   ILO_DEV_ASSERT(builder->dev, 6, 7.5);

   /* see cc_set_gen6_DEPTH_STENCIL_STATE() */
   return ilo_builder_dynamic_write(builder, ILO_BUILDER_ITEM_DEPTH_STENCIL,
         state_align, state_len, cc->ds);
}

static inline uint32_t
gen6_BLEND_STATE(struct ilo_builder *builder,
                 const struct ilo_state_cc *cc)
{
   const int state_align = 64;
   const int state_len = 2 * cc->blend_state_count;

   ILO_DEV_ASSERT(builder->dev, 6, 7.5);

   if (!state_len)
      return 0;

   /* see cc_set_gen6_BLEND_STATE() */
   return ilo_builder_dynamic_write(builder, ILO_BUILDER_ITEM_BLEND,
         state_align, state_len, cc->blend);
}

static inline uint32_t
gen8_BLEND_STATE(struct ilo_builder *builder,
                 const struct ilo_state_cc *cc)
{
   const int state_align = 64;
   const int state_len = 1 + 2 * cc->blend_state_count;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   /* see cc_set_gen8_BLEND_STATE() */
   return ilo_builder_dynamic_write(builder, ILO_BUILDER_ITEM_BLEND,
         state_align, state_len, &cc->blend[1]);
}

#endif /* ILO_BUILDER_3D_BOTTOM_H */
