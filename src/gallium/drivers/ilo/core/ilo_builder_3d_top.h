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

#ifndef ILO_BUILDER_3D_TOP_H
#define ILO_BUILDER_3D_TOP_H

#include "genhw/genhw.h"
#include "intel_winsys.h"

#include "ilo_core.h"
#include "ilo_dev.h"
#include "ilo_state_sampler.h"
#include "ilo_state_shader.h"
#include "ilo_state_sol.h"
#include "ilo_state_surface.h"
#include "ilo_state_urb.h"
#include "ilo_state_vf.h"
#include "ilo_vma.h"
#include "ilo_builder.h"

static inline void
gen6_3DSTATE_URB(struct ilo_builder *builder,
                 const struct ilo_state_urb *urb)
{
   const uint8_t cmd_len = 3;
   uint32_t *dw;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_URB) | (cmd_len - 2);
   /* see urb_set_gen6_3DSTATE_URB() */
   dw[1] = urb->urb[0];
   dw[2] = urb->urb[1];
}

static inline void
gen7_3DSTATE_PUSH_CONSTANT_ALLOC_VS(struct ilo_builder *builder,
                                    const struct ilo_state_urb *urb)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_PUSH_CONSTANT_ALLOC_VS) |
           (cmd_len - 2);
   /* see urb_set_gen7_3dstate_push_constant_alloc() */
   dw[1] = urb->pcb[0];
}

static inline void
gen7_3DSTATE_PUSH_CONSTANT_ALLOC_HS(struct ilo_builder *builder,
                                    const struct ilo_state_urb *urb)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_PUSH_CONSTANT_ALLOC_HS) |
           (cmd_len - 2);
   /* see urb_set_gen7_3dstate_push_constant_alloc() */
   dw[1] = urb->pcb[1];
}

static inline void
gen7_3DSTATE_PUSH_CONSTANT_ALLOC_DS(struct ilo_builder *builder,
                                    const struct ilo_state_urb *urb)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_PUSH_CONSTANT_ALLOC_DS) |
           (cmd_len - 2);
   /* see urb_set_gen7_3dstate_push_constant_alloc() */
   dw[1] = urb->pcb[2];
}

static inline void
gen7_3DSTATE_PUSH_CONSTANT_ALLOC_GS(struct ilo_builder *builder,
                                    const struct ilo_state_urb *urb)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_PUSH_CONSTANT_ALLOC_GS) |
           (cmd_len - 2);
   /* see urb_set_gen7_3dstate_push_constant_alloc() */
   dw[1] = urb->pcb[3];
}

static inline void
gen7_3DSTATE_PUSH_CONSTANT_ALLOC_PS(struct ilo_builder *builder,
                                    const struct ilo_state_urb *urb)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_PUSH_CONSTANT_ALLOC_PS) |
           (cmd_len - 2);
   /* see urb_set_gen7_3dstate_push_constant_alloc() */
   dw[1] = urb->pcb[4];
}

static inline void
gen7_3DSTATE_URB_VS(struct ilo_builder *builder,
                    const struct ilo_state_urb *urb)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_URB_VS) | (cmd_len - 2);
   /* see urb_set_gen7_3dstate_push_constant_alloc() */
   dw[1] = urb->urb[0];
}

static inline void
gen7_3DSTATE_URB_HS(struct ilo_builder *builder,
                    const struct ilo_state_urb *urb)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_URB_HS) | (cmd_len - 2);
   /* see urb_set_gen7_3dstate_push_constant_alloc() */
   dw[1] = urb->urb[1];
}

static inline void
gen7_3DSTATE_URB_DS(struct ilo_builder *builder,
                    const struct ilo_state_urb *urb)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_URB_DS) | (cmd_len - 2);
   /* see urb_set_gen7_3dstate_push_constant_alloc() */
   dw[1] = urb->urb[2];
}

static inline void
gen7_3DSTATE_URB_GS(struct ilo_builder *builder,
                    const struct ilo_state_urb *urb)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_URB_GS) | (cmd_len - 2);
   /* see urb_set_gen7_3dstate_push_constant_alloc() */
   dw[1] = urb->urb[3];
}

static inline void
gen75_3DSTATE_VF(struct ilo_builder *builder,
                 const struct ilo_state_vf *vf)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7.5, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   /* see vf_params_set_gen75_3DSTATE_VF() */
   dw[0] = GEN75_RENDER_CMD(3D, 3DSTATE_VF) | (cmd_len - 2) |
           vf->cut[0];
   dw[1] = vf->cut[1];
}

static inline void
gen6_3DSTATE_VF_STATISTICS(struct ilo_builder *builder,
                           bool enable)
{
   const uint8_t cmd_len = 1;
   const uint32_t dw0 = GEN6_RENDER_CMD(SINGLE_DW, 3DSTATE_VF_STATISTICS) |
                        enable;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   ilo_builder_batch_write(builder, cmd_len, &dw0);
}

static inline void
gen8_3DSTATE_VF_TOPOLOGY(struct ilo_builder *builder,
                         enum gen_3dprim_type topology)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_VF_TOPOLOGY) | (cmd_len - 2);
   dw[1] = topology << GEN8_TOPOLOGY_DW1_TYPE__SHIFT;
}

static inline void
gen8_3DSTATE_VF_INSTANCING(struct ilo_builder *builder,
                           const struct ilo_state_vf *vf,
                           uint32_t attr)
{
   const uint8_t cmd_len = 3;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_VF_INSTANCING) | (cmd_len - 2);
   dw[1] = attr << GEN8_INSTANCING_DW1_VE_INDEX__SHIFT;
   dw[2] = 0;
   /* see vf_set_gen8_3DSTATE_VF_INSTANCING() */
   if (attr >= vf->internal_ve_count) {
      attr -= vf->internal_ve_count;

      dw[1] |= vf->user_instancing[attr][0];
      dw[2] |= vf->user_instancing[attr][1];
   }
}

static inline void
gen8_3DSTATE_VF_SGVS(struct ilo_builder *builder,
                     const struct ilo_state_vf *vf)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN8_RENDER_CMD(3D, 3DSTATE_VF_SGVS) | (cmd_len - 2);
   /* see vf_params_set_gen8_3DSTATE_VF_SGVS() */
   dw[1] = vf->sgvs[0];
}

static inline void
gen6_3DSTATE_VERTEX_BUFFERS(struct ilo_builder *builder,
                            const struct ilo_state_vf *vf,
                            const struct ilo_state_vertex_buffer *vb,
                            unsigned vb_count)
{
   uint8_t cmd_len;
   uint32_t *dw;
   unsigned pos, i;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 82:
    *
    *     "From 1 to 33 VBs can be specified..."
    */
   assert(vb_count <= 33);

   if (!vb_count)
      return;

   cmd_len = 1 + 4 * vb_count;
   pos = ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_VERTEX_BUFFERS) | (cmd_len - 2);
   dw++;
   pos++;

   for (i = 0; i < vb_count; i++) {
      const struct ilo_state_vertex_buffer *b = &vb[i];

      /* see vertex_buffer_set_gen8_vertex_buffer_state() */
      dw[0] = b->vb[0] |
              i << GEN6_VB_DW0_INDEX__SHIFT;

      if (ilo_dev_gen(builder->dev) >= ILO_GEN(8))
         dw[0] |= builder->mocs << GEN8_VB_DW0_MOCS__SHIFT;
      else
         dw[0] |= builder->mocs << GEN6_VB_DW0_MOCS__SHIFT;

      dw[1] = 0;
      dw[2] = 0;
      dw[3] = 0;

      if (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) {
         if (b->vma) {
            ilo_builder_batch_reloc64(builder, pos + 1, b->vma->bo,
                  b->vma->bo_offset + b->vb[1], 0);
         }

         dw[3] |= b->vb[2];
      } else {
         const int8_t elem = vf->vb_to_first_elem[i];

         /* see vf_set_gen6_vertex_buffer_state() */
         if (elem >= 0) {
            dw[0] |= vf->user_instancing[elem][0];
            dw[3] |= vf->user_instancing[elem][1];
         }

         if (b->vma) {
            ilo_builder_batch_reloc(builder, pos + 1, b->vma->bo,
                  b->vma->bo_offset + b->vb[1], 0);
            ilo_builder_batch_reloc(builder, pos + 2, b->vma->bo,
                  b->vma->bo_offset + b->vb[2], 0);
         }
      }

      dw += 4;
      pos += 4;
   }
}

/* the user vertex buffer must be uploaded with gen6_user_vertex_buffer() */
static inline void
gen6_user_3DSTATE_VERTEX_BUFFERS(struct ilo_builder *builder,
                                 uint32_t vb_begin, uint32_t vb_end,
                                 uint32_t stride)
{
   const struct ilo_builder_writer *bat =
      &builder->writers[ILO_BUILDER_WRITER_BATCH];
   const uint8_t cmd_len = 1 + 4;
   uint32_t *dw;
   unsigned pos;

   ILO_DEV_ASSERT(builder->dev, 6, 7.5);

   pos = ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_VERTEX_BUFFERS) | (cmd_len - 2);
   dw++;
   pos++;

   /* VERTEX_BUFFER_STATE */
   dw[0] = 0 << GEN6_VB_DW0_INDEX__SHIFT |
           GEN6_VB_DW0_ACCESS_VERTEXDATA |
           stride << GEN6_VB_DW0_PITCH__SHIFT;
   if (ilo_dev_gen(builder->dev) >= ILO_GEN(7))
      dw[0] |= GEN7_VB_DW0_ADDR_MODIFIED;

   dw[3] = 0;

   ilo_builder_batch_reloc(builder, pos + 1, bat->bo, vb_begin, 0);
   ilo_builder_batch_reloc(builder, pos + 2, bat->bo, vb_end, 0);
}

static inline void
gen6_3DSTATE_VERTEX_ELEMENTS(struct ilo_builder *builder,
                             const struct ilo_state_vf *vf)
{
   uint8_t cmd_len;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   cmd_len = 1 + 2 * (vf->internal_ve_count + vf->user_ve_count);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_VERTEX_ELEMENTS) | (cmd_len - 2);
   dw++;

   /*
    * see vf_params_set_gen6_internal_ve() and
    * vf_set_gen6_3DSTATE_VERTEX_ELEMENTS()
    */
   if (vf->internal_ve_count) {
      memcpy(dw, vf->internal_ve,
            sizeof(vf->internal_ve[0]) * vf->internal_ve_count);
      dw += 2 * vf->internal_ve_count;
   }

   memcpy(dw, vf->user_ve, sizeof(vf->user_ve[0]) * vf->user_ve_count);
}

static inline void
gen6_3DSTATE_INDEX_BUFFER(struct ilo_builder *builder,
                          const struct ilo_state_vf *vf,
                          const struct ilo_state_index_buffer *ib)
{
   const uint8_t cmd_len = 3;
   uint32_t dw0, *dw;
   unsigned pos;

   ILO_DEV_ASSERT(builder->dev, 6, 7.5);

   dw0 = GEN6_RENDER_CMD(3D, 3DSTATE_INDEX_BUFFER) | (cmd_len - 2) |
         builder->mocs << GEN6_IB_DW0_MOCS__SHIFT;

   /*
    * see index_buffer_set_gen8_3DSTATE_INDEX_BUFFER() and
    * vf_params_set_gen6_3dstate_index_buffer()
    */
   dw0 |= ib->ib[0];
   if (ilo_dev_gen(builder->dev) <= ILO_GEN(7))
      dw0 |= vf->cut[0];

   pos = ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = dw0;
   if (ib->vma) {
      ilo_builder_batch_reloc(builder, pos + 1, ib->vma->bo,
            ib->vma->bo_offset + ib->ib[1], 0);
      ilo_builder_batch_reloc(builder, pos + 2, ib->vma->bo,
            ib->vma->bo_offset + ib->ib[2], 0);
   } else {
      dw[1] = 0;
      dw[2] = 0;
   }
}

static inline void
gen8_3DSTATE_INDEX_BUFFER(struct ilo_builder *builder,
                          const struct ilo_state_vf *vf,
                          const struct ilo_state_index_buffer *ib)
{
   const uint8_t cmd_len = 5;
   uint32_t *dw;
   unsigned pos;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   pos = ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_INDEX_BUFFER) | (cmd_len - 2);
   /* see index_buffer_set_gen8_3DSTATE_INDEX_BUFFER() */
   dw[1] = ib->ib[0] |
           builder->mocs << GEN8_IB_DW1_MOCS__SHIFT;

   if (ib->vma) {
      ilo_builder_batch_reloc64(builder, pos + 2, ib->vma->bo,
            ib->vma->bo_offset + ib->ib[1], 0);
   } else {
      dw[2] = 0;
      dw[3] = 0;
   }

   dw[4] = ib->ib[2];
}

static inline void
gen6_3DSTATE_VS(struct ilo_builder *builder,
                const struct ilo_state_vs *vs,
                uint32_t kernel_offset)
{
   const uint8_t cmd_len = 6;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 7.5);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_VS) | (cmd_len - 2);
   dw[1] = kernel_offset;
   /* see vs_set_gen6_3DSTATE_VS() */
   dw[2] = vs->vs[0];
   dw[3] = vs->vs[1];
   dw[4] = vs->vs[2];
   dw[5] = vs->vs[3];
}

static inline void
gen8_3DSTATE_VS(struct ilo_builder *builder,
                const struct ilo_state_vs *vs,
                uint32_t kernel_offset)
{
   const uint8_t cmd_len = 9;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_VS) | (cmd_len - 2);
   dw[1] = kernel_offset;
   dw[2] = 0;
   /* see vs_set_gen6_3DSTATE_VS() */
   dw[3] = vs->vs[0];
   dw[4] = vs->vs[1];
   dw[5] = 0;
   dw[6] = vs->vs[2];
   dw[7] = vs->vs[3];
   dw[8] = vs->vs[4];
}

static inline void
gen7_3DSTATE_HS(struct ilo_builder *builder,
                const struct ilo_state_hs *hs,
                uint32_t kernel_offset)
{
   const uint8_t cmd_len = 7;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 7.5);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_HS) | (cmd_len - 2);
   /* see hs_set_gen7_3DSTATE_HS() */
   dw[1] = hs->hs[0];
   dw[2] = hs->hs[1];
   dw[3] = kernel_offset;
   dw[4] = hs->hs[2];
   dw[5] = hs->hs[3];
   dw[6] = 0;
}

static inline void
gen8_3DSTATE_HS(struct ilo_builder *builder,
                const struct ilo_state_hs *hs,
                uint32_t kernel_offset)
{
   const uint8_t cmd_len = 9;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_HS) | (cmd_len - 2);
   /* see hs_set_gen7_3DSTATE_HS() */
   dw[1] = hs->hs[0];
   dw[2] = hs->hs[1];
   dw[3] = kernel_offset;
   dw[4] = 0;
   dw[5] = hs->hs[2];
   dw[6] = 0;
   dw[7] = hs->hs[3];
   dw[8] = 0;
}

static inline void
gen7_3DSTATE_TE(struct ilo_builder *builder,
                const struct ilo_state_ds *ds)
{
   const uint8_t cmd_len = 4;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_TE) | (cmd_len - 2);
   /* see ds_set_gen7_3DSTATE_TE() */
   dw[1] = ds->te[0];
   dw[2] = ds->te[1];
   dw[3] = ds->te[2];
}

static inline void
gen7_3DSTATE_DS(struct ilo_builder *builder,
                const struct ilo_state_ds *ds,
                uint32_t kernel_offset)
{
   const uint8_t cmd_len = 6;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 7.5);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_DS) | (cmd_len - 2);
   /* see ds_set_gen7_3DSTATE_DS() */
   dw[1] = kernel_offset;
   dw[2] = ds->ds[0];
   dw[3] = ds->ds[1];
   dw[4] = ds->ds[2];
   dw[5] = ds->ds[3];
}

static inline void
gen8_3DSTATE_DS(struct ilo_builder *builder,
                const struct ilo_state_ds *ds,
                uint32_t kernel_offset)
{
   const uint8_t cmd_len = 9;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_DS) | (cmd_len - 2);
   /* see ds_set_gen7_3DSTATE_DS() */
   dw[1] = kernel_offset;
   dw[2] = 0;
   dw[3] = ds->ds[0];
   dw[4] = ds->ds[1];
   dw[5] = 0;
   dw[6] = ds->ds[2];
   dw[7] = ds->ds[3];
   dw[8] = ds->ds[4];
}

static inline void
gen6_3DSTATE_GS(struct ilo_builder *builder,
                const struct ilo_state_gs *gs,
                uint32_t kernel_offset)
{
   const uint8_t cmd_len = 7;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 6);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_GS) | (cmd_len - 2);
   dw[1] = kernel_offset;
   /* see gs_set_gen6_3DSTATE_GS() */
   dw[2] = gs->gs[0];
   dw[3] = gs->gs[1];
   dw[4] = gs->gs[2];
   dw[5] = gs->gs[3];
   dw[6] = gs->gs[4];
}

static inline void
gen6_3DSTATE_GS_SVB_INDEX(struct ilo_builder *builder,
                          int index, unsigned svbi,
                          unsigned max_svbi,
                          bool load_vertex_count)
{
   const uint8_t cmd_len = 4;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 6);
   assert(index >= 0 && index < 4);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_GS_SVB_INDEX) | (cmd_len - 2);

   dw[1] = index << GEN6_SVBI_DW1_INDEX__SHIFT;
   if (load_vertex_count)
      dw[1] |= GEN6_SVBI_DW1_LOAD_INTERNAL_VERTEX_COUNT;

   dw[2] = svbi;
   dw[3] = max_svbi;
}

static inline void
gen7_3DSTATE_GS(struct ilo_builder *builder,
                const struct ilo_state_gs *gs,
                uint32_t kernel_offset)
{
   const uint8_t cmd_len = 7;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 7.5);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_GS) | (cmd_len - 2);
   dw[1] = kernel_offset;
   /* see gs_set_gen7_3DSTATE_GS() */
   dw[2] = gs->gs[0];
   dw[3] = gs->gs[1];
   dw[4] = gs->gs[2];
   dw[5] = gs->gs[3];
   dw[6] = 0;
}

static inline void
gen8_3DSTATE_GS(struct ilo_builder *builder,
                const struct ilo_state_gs *gs,
                uint32_t kernel_offset)
{
   const uint8_t cmd_len = 10;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_GS) | (cmd_len - 2);
   dw[1] = kernel_offset;
   dw[2] = 0;
   /* see gs_set_gen7_3DSTATE_GS() */
   dw[3] = gs->gs[0];
   dw[4] = gs->gs[1];
   dw[5] = 0;
   dw[6] = gs->gs[2];
   dw[7] = gs->gs[3];
   dw[8] = 0;
   dw[9] = gs->gs[4];
}

static inline void
gen7_3DSTATE_STREAMOUT(struct ilo_builder *builder,
                       const struct ilo_state_sol *sol)
{
   const uint8_t cmd_len = (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) ? 5 : 3;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_STREAMOUT) | (cmd_len - 2);
   /* see sol_set_gen7_3DSTATE_STREAMOUT() */
   dw[1] = sol->streamout[0];
   dw[2] = sol->streamout[1];
   if (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) {
      dw[3] = sol->strides[1] << GEN8_SO_DW3_BUFFER1_PITCH__SHIFT |
              sol->strides[0] << GEN8_SO_DW3_BUFFER0_PITCH__SHIFT;
      dw[4] = sol->strides[3] << GEN8_SO_DW4_BUFFER3_PITCH__SHIFT |
              sol->strides[2] << GEN8_SO_DW4_BUFFER2_PITCH__SHIFT;
   }
}

static inline void
gen7_3DSTATE_SO_DECL_LIST(struct ilo_builder *builder,
                          const struct ilo_state_sol *sol)
{
   /*
    * Note that "DWord Length" has 9 bits for this command and the type of
    * cmd_len cannot be uint8_t.
    */
   uint16_t cmd_len;
   int cmd_decl_count;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 8);

   if (ilo_dev_gen(builder->dev) >= ILO_GEN(7.5)) {
      cmd_decl_count = sol->decl_count;
   } else {
      /*
       * From the Ivy Bridge PRM, volume 2 part 1, page 201:
       *
       *     "Errata: All 128 decls for all four streams must be included
       *      whenever this command is issued. The "Num Entries [n]" fields
       *      still contain the actual numbers of valid decls."
       */
      cmd_decl_count = 128;
   }

   cmd_len = 3 + 2 * cmd_decl_count;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_SO_DECL_LIST) | (cmd_len - 2);
   /* see sol_set_gen7_3DSTATE_SO_DECL_LIST() */
   dw[1] = sol->so_decl[0];
   dw[2] = sol->so_decl[1];
   memcpy(&dw[3], sol->decl, sizeof(sol->decl[0]) * sol->decl_count);

   if (sol->decl_count < cmd_decl_count) {
      memset(&dw[3 + 2 * sol->decl_count], 0, sizeof(sol->decl[0]) *
            cmd_decl_count - sol->decl_count);
   }
}

static inline void
gen7_3DSTATE_SO_BUFFER(struct ilo_builder *builder,
                       const struct ilo_state_sol *sol,
                       const struct ilo_state_sol_buffer *sb,
                       uint8_t buffer)
{
   const uint8_t cmd_len = 4;
   uint32_t *dw;
   unsigned pos;

   ILO_DEV_ASSERT(builder->dev, 7, 7.5);

   assert(buffer < ILO_STATE_SOL_MAX_BUFFER_COUNT);

   pos = ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_SO_BUFFER) | (cmd_len - 2);
   /* see sol_buffer_set_gen7_3dstate_so_buffer() */
   dw[1] = buffer << GEN7_SO_BUF_DW1_INDEX__SHIFT |
           builder->mocs << GEN7_SO_BUF_DW1_MOCS__SHIFT |
           sol->strides[buffer] << GEN7_SO_BUF_DW1_PITCH__SHIFT;

   if (sb->vma) {
      ilo_builder_batch_reloc(builder, pos + 2, sb->vma->bo,
            sb->vma->bo_offset + sb->so_buf[0], INTEL_RELOC_WRITE);
      ilo_builder_batch_reloc(builder, pos + 3, sb->vma->bo,
            sb->vma->bo_offset + sb->so_buf[1], INTEL_RELOC_WRITE);
   } else {
      dw[2] = 0;
      dw[3] = 0;
   }
}

static inline void
gen8_3DSTATE_SO_BUFFER(struct ilo_builder *builder,
                       const struct ilo_state_sol *sol,
                       const struct ilo_state_sol_buffer *sb,
                       uint8_t buffer)
{
   const uint8_t cmd_len = 8;
   uint32_t *dw;
   unsigned pos;

   ILO_DEV_ASSERT(builder->dev, 8, 8);

   pos = ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(3D, 3DSTATE_SO_BUFFER) | (cmd_len - 2);
   /* see sol_buffer_set_gen8_3dstate_so_buffer() */
   dw[1] = sb->so_buf[0] |
           buffer << GEN7_SO_BUF_DW1_INDEX__SHIFT |
           builder->mocs << GEN8_SO_BUF_DW1_MOCS__SHIFT;

   if (sb->vma) {
      ilo_builder_batch_reloc64(builder, pos + 2, sb->vma->bo,
            sb->vma->bo_offset + sb->so_buf[1], INTEL_RELOC_WRITE);
   } else {
      dw[2] = 0;
      dw[3] = 0;
   }

   dw[4] = sb->so_buf[2];

   if (sb->write_offset_vma) {
      ilo_builder_batch_reloc64(builder, pos + 5, sb->write_offset_vma->bo,
            sb->write_offset_vma->bo_offset + sizeof(uint32_t) * buffer,
            INTEL_RELOC_WRITE);
   } else {
      dw[5] = 0;
      dw[6] = 0;
   }

   dw[7] = sb->so_buf[3];
}

static inline void
gen6_3DSTATE_BINDING_TABLE_POINTERS(struct ilo_builder *builder,
                                    uint32_t vs_binding_table,
                                    uint32_t gs_binding_table,
                                    uint32_t ps_binding_table)
{
   const uint8_t cmd_len = 4;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 6);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_BINDING_TABLE_POINTERS) |
           GEN6_BINDING_TABLE_PTR_DW0_VS_CHANGED |
           GEN6_BINDING_TABLE_PTR_DW0_GS_CHANGED |
           GEN6_BINDING_TABLE_PTR_DW0_PS_CHANGED |
           (cmd_len - 2);
   dw[1] = vs_binding_table;
   dw[2] = gs_binding_table;
   dw[3] = ps_binding_table;
}

static inline void
gen6_3DSTATE_SAMPLER_STATE_POINTERS(struct ilo_builder *builder,
                                    uint32_t vs_sampler_state,
                                    uint32_t gs_sampler_state,
                                    uint32_t ps_sampler_state)
{
   const uint8_t cmd_len = 4;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 6);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(3D, 3DSTATE_SAMPLER_STATE_POINTERS) |
           GEN6_SAMPLER_PTR_DW0_VS_CHANGED |
           GEN6_SAMPLER_PTR_DW0_GS_CHANGED |
           GEN6_SAMPLER_PTR_DW0_PS_CHANGED |
           (cmd_len - 2);
   dw[1] = vs_sampler_state;
   dw[2] = gs_sampler_state;
   dw[3] = ps_sampler_state;
}

static inline void
gen7_3dstate_pointer(struct ilo_builder *builder,
                     int subop, uint32_t pointer)
{
   const uint32_t cmd = GEN6_RENDER_TYPE_RENDER |
                        GEN6_RENDER_SUBTYPE_3D |
                        subop;
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 8);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = cmd | (cmd_len - 2);
   dw[1] = pointer;
}

static inline void
gen7_3DSTATE_BINDING_TABLE_POINTERS_VS(struct ilo_builder *builder,
                                       uint32_t binding_table)
{
   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_BINDING_TABLE_POINTERS_VS,
         binding_table);
}

static inline void
gen7_3DSTATE_BINDING_TABLE_POINTERS_HS(struct ilo_builder *builder,
                                       uint32_t binding_table)
{
   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_BINDING_TABLE_POINTERS_HS,
         binding_table);
}

static inline void
gen7_3DSTATE_BINDING_TABLE_POINTERS_DS(struct ilo_builder *builder,
                                       uint32_t binding_table)
{
   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_BINDING_TABLE_POINTERS_DS,
         binding_table);
}

static inline void
gen7_3DSTATE_BINDING_TABLE_POINTERS_GS(struct ilo_builder *builder,
                                       uint32_t binding_table)
{
   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_BINDING_TABLE_POINTERS_GS,
         binding_table);
}

static inline void
gen7_3DSTATE_SAMPLER_STATE_POINTERS_VS(struct ilo_builder *builder,
                                       uint32_t sampler_state)
{
   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_SAMPLER_STATE_POINTERS_VS,
         sampler_state);
}

static inline void
gen7_3DSTATE_SAMPLER_STATE_POINTERS_HS(struct ilo_builder *builder,
                                       uint32_t sampler_state)
{
   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_SAMPLER_STATE_POINTERS_HS,
         sampler_state);
}

static inline void
gen7_3DSTATE_SAMPLER_STATE_POINTERS_DS(struct ilo_builder *builder,
                                       uint32_t sampler_state)
{
   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_SAMPLER_STATE_POINTERS_DS,
         sampler_state);
}

static inline void
gen7_3DSTATE_SAMPLER_STATE_POINTERS_GS(struct ilo_builder *builder,
                                       uint32_t sampler_state)
{
   gen7_3dstate_pointer(builder,
         GEN7_RENDER_OPCODE_3DSTATE_SAMPLER_STATE_POINTERS_GS,
         sampler_state);
}

static inline void
gen6_3dstate_constant(struct ilo_builder *builder, int subop,
                      const uint32_t *bufs, const int *sizes,
                      int num_bufs)
{
   const uint32_t cmd = GEN6_RENDER_TYPE_RENDER |
                        GEN6_RENDER_SUBTYPE_3D |
                        subop;
   const uint8_t cmd_len = 5;
   unsigned buf_enabled = 0x0;
   uint32_t buf_dw[4], *dw;
   int max_read_length, total_read_length;
   int i;

   ILO_DEV_ASSERT(builder->dev, 6, 6);

   assert(num_bufs <= 4);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 138:
    *
    *     "(3DSTATE_CONSTANT_VS) The sum of all four read length fields (each
    *      incremented to represent the actual read length) must be less than
    *      or equal to 32"
    *
    * From the Sandy Bridge PRM, volume 2 part 1, page 161:
    *
    *     "(3DSTATE_CONSTANT_GS) The sum of all four read length fields (each
    *      incremented to represent the actual read length) must be less than
    *      or equal to 64"
    *
    * From the Sandy Bridge PRM, volume 2 part 1, page 287:
    *
    *     "(3DSTATE_CONSTANT_PS) The sum of all four read length fields (each
    *      incremented to represent the actual read length) must be less than
    *      or equal to 64"
    */
   switch (subop) {
   case GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_VS:
      max_read_length = 32;
      break;
   case GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_GS:
   case GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_PS:
      max_read_length = 64;
      break;
   default:
      assert(!"unknown pcb subop");
      max_read_length = 0;
      break;
   }

   total_read_length = 0;
   for (i = 0; i < 4; i++) {
      if (i < num_bufs && sizes[i]) {
         /* in 256-bit units */
         const int read_len = (sizes[i] + 31) / 32;

         assert(bufs[i] % 32 == 0);
         assert(read_len <= 32);

         buf_enabled |= 1 << i;
         buf_dw[i] = bufs[i] | (read_len - 1);

         total_read_length += read_len;
      } else {
         buf_dw[i] = 0;
      }
   }

   assert(total_read_length <= max_read_length);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = cmd | (cmd_len - 2) |
           buf_enabled << GEN6_CONSTANT_DW0_BUFFER_ENABLES__SHIFT |
           builder->mocs << GEN6_CONSTANT_DW0_MOCS__SHIFT;

   memcpy(&dw[1], buf_dw, sizeof(buf_dw));
}

static inline void
gen6_3DSTATE_CONSTANT_VS(struct ilo_builder *builder,
                         const uint32_t *bufs, const int *sizes,
                         int num_bufs)
{
   gen6_3dstate_constant(builder, GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_VS,
         bufs, sizes, num_bufs);
}

static inline void
gen6_3DSTATE_CONSTANT_GS(struct ilo_builder *builder,
                         const uint32_t *bufs, const int *sizes,
                         int num_bufs)
{
   gen6_3dstate_constant(builder, GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_GS,
         bufs, sizes, num_bufs);
}

static inline void
gen7_3dstate_constant(struct ilo_builder *builder,
                      int subop,
                      const uint32_t *bufs, const int *sizes,
                      int num_bufs)
{
   const uint32_t cmd = GEN6_RENDER_TYPE_RENDER |
                        GEN6_RENDER_SUBTYPE_3D |
                        subop;
   const uint8_t cmd_len = (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) ? 11 : 7;
   uint32_t payload[6], *dw;
   int total_read_length, i;

   ILO_DEV_ASSERT(builder->dev, 7, 8);

   /* VS, HS, DS, GS, and PS variants */
   assert(subop >= GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_VS &&
          subop <= GEN7_RENDER_OPCODE_3DSTATE_CONSTANT_DS &&
          subop != GEN6_RENDER_OPCODE_3DSTATE_SAMPLE_MASK);

   assert(num_bufs <= 4);

   payload[0] = 0;
   payload[1] = 0;

   total_read_length = 0;
   for (i = 0; i < 4; i++) {
      int read_len;

      /*
       * From the Ivy Bridge PRM, volume 2 part 1, page 112:
       *
       *     "Constant buffers must be enabled in order from Constant Buffer 0
       *      to Constant Buffer 3 within this command.  For example, it is
       *      not allowed to enable Constant Buffer 1 by programming a
       *      non-zero value in the VS Constant Buffer 1 Read Length without a
       *      non-zero value in VS Constant Buffer 0 Read Length."
       */
      if (i >= num_bufs || !sizes[i]) {
         for (; i < 4; i++) {
            assert(i >= num_bufs || !sizes[i]);
            payload[2 + i] = 0;
         }
         break;
      }

      /* read lengths are in 256-bit units */
      read_len = (sizes[i] + 31) / 32;
      /* the lower 5 bits are used for memory object control state */
      assert(bufs[i] % 32 == 0);

      payload[i / 2] |= read_len << ((i % 2) ? 16 : 0);
      payload[2 + i] = bufs[i];

      total_read_length += read_len;
   }

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 113:
    *
    *     "The sum of all four read length fields must be less than or equal
    *      to the size of 64"
    */
   assert(total_read_length <= 64);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = cmd | (cmd_len - 2);
   if (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) {
      dw[1] = payload[0];
      dw[2] = payload[1];
      dw[3] = payload[2];
      dw[4] = 0;
      dw[5] = payload[3];
      dw[6] = 0;
      dw[7] = payload[4];
      dw[8] = 0;
      dw[9] = payload[5];
      dw[10] = 0;
   } else {
      payload[2] |= builder->mocs << GEN7_CONSTANT_DW_ADDR_MOCS__SHIFT;

      memcpy(&dw[1], payload, sizeof(payload));
   }
}

static inline void
gen7_3DSTATE_CONSTANT_VS(struct ilo_builder *builder,
                         const uint32_t *bufs, const int *sizes,
                         int num_bufs)
{
   gen7_3dstate_constant(builder, GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_VS,
         bufs, sizes, num_bufs);
}

static inline void
gen7_3DSTATE_CONSTANT_HS(struct ilo_builder *builder,
                         const uint32_t *bufs, const int *sizes,
                         int num_bufs)
{
   gen7_3dstate_constant(builder, GEN7_RENDER_OPCODE_3DSTATE_CONSTANT_HS,
         bufs, sizes, num_bufs);
}

static inline void
gen7_3DSTATE_CONSTANT_DS(struct ilo_builder *builder,
                         const uint32_t *bufs, const int *sizes,
                         int num_bufs)
{
   gen7_3dstate_constant(builder, GEN7_RENDER_OPCODE_3DSTATE_CONSTANT_DS,
         bufs, sizes, num_bufs);
}

static inline void
gen7_3DSTATE_CONSTANT_GS(struct ilo_builder *builder,
                         const uint32_t *bufs, const int *sizes,
                         int num_bufs)
{
   gen7_3dstate_constant(builder, GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_GS,
         bufs, sizes, num_bufs);
}

static inline uint32_t
gen6_BINDING_TABLE_STATE(struct ilo_builder *builder,
                         const uint32_t *surface_states,
                         int num_surface_states)
{
   const int state_align = 32;
   const int state_len = num_surface_states;
   uint32_t state_offset, *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 69:
    *
    *     "It is stored as an array of up to 256 elements..."
    */
   assert(num_surface_states <= 256);

   if (!num_surface_states)
      return 0;

   state_offset = ilo_builder_surface_pointer(builder,
         ILO_BUILDER_ITEM_BINDING_TABLE, state_align, state_len, &dw);
   memcpy(dw, surface_states, state_len << 2);

   return state_offset;
}

static inline uint32_t
gen6_SURFACE_STATE(struct ilo_builder *builder,
                   const struct ilo_state_surface *surf)
{
   int state_align, state_len;
   uint32_t state_offset, *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   if (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) {
      state_align = 64;
      state_len = 13;

      state_offset = ilo_builder_surface_pointer(builder,
            ILO_BUILDER_ITEM_SURFACE, state_align, state_len, &dw);
      memcpy(dw, surf->surface, state_len << 2);

      if (surf->vma) {
         const uint32_t mocs = (surf->scanout) ?
            (GEN8_MOCS_MT_PTE | GEN8_MOCS_CT_L3) : builder->mocs;

         dw[1] |= mocs << GEN8_SURFACE_DW1_MOCS__SHIFT;

         ilo_builder_surface_reloc64(builder, state_offset, 8, surf->vma->bo,
               surf->vma->bo_offset + surf->surface[8],
               (surf->readonly) ? 0 : INTEL_RELOC_WRITE);
      }
   } else {
      state_align = 32;
      state_len = (ilo_dev_gen(builder->dev) >= ILO_GEN(7)) ? 8 : 6;

      state_offset = ilo_builder_surface_pointer(builder,
            ILO_BUILDER_ITEM_SURFACE, state_align, state_len, &dw);
      memcpy(dw, surf->surface, state_len << 2);

      if (surf->vma) {
         /*
          * For scanouts, we should not enable caching in LLC.  Since we only
          * enable that on Gen8+, we are fine here.
          */
         dw[5] |= builder->mocs << GEN6_SURFACE_DW5_MOCS__SHIFT;

         ilo_builder_surface_reloc(builder, state_offset, 1, surf->vma->bo,
               surf->vma->bo_offset + surf->surface[1],
               (surf->readonly) ? 0 : INTEL_RELOC_WRITE);
      }
   }

   return state_offset;
}

static inline uint32_t
gen6_SAMPLER_STATE(struct ilo_builder *builder,
                   const struct ilo_state_sampler *samplers,
                   const uint32_t *sampler_border_colors,
                   int sampler_count)
{
   const int state_align = 32;
   const int state_len = 4 * sampler_count;
   uint32_t state_offset, *dw;
   int i;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 101:
    *
    *     "The sampler state is stored as an array of up to 16 elements..."
    */
   assert(sampler_count <= 16);

   if (!sampler_count)
      return 0;

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 132:
    *
    *     "(Sampler Count of 3DSTATE_VS) Specifies how many samplers (in
    *      multiples of 4) the vertex shader 0 kernel uses. Used only for
    *      prefetching the associated sampler state entries.
    *
    * It also applies to other shader stages.
    */
   ilo_builder_dynamic_pad_top(builder, 4 * (4 - (sampler_count % 4)));

   state_offset = ilo_builder_dynamic_pointer(builder,
         ILO_BUILDER_ITEM_SAMPLER, state_align, state_len, &dw);

   for (i = 0; i < sampler_count; i++) {
      /* see sampler_set_gen6_SAMPLER_STATE() */
      dw[0] = samplers[i].sampler[0];
      dw[1] = samplers[i].sampler[1];
      dw[3] = samplers[i].sampler[2];

      assert(!(sampler_border_colors[i] & 0x1f));
      dw[2] = sampler_border_colors[i];

      dw += 4;
   }

   return state_offset;
}

static inline uint32_t
gen6_SAMPLER_BORDER_COLOR_STATE(struct ilo_builder *builder,
                                const struct ilo_state_sampler_border *border)
{
   const int state_align =
      (ilo_dev_gen(builder->dev) >= ILO_GEN(8)) ? 64 : 32;
   const int state_len = (ilo_dev_gen(builder->dev) >= ILO_GEN(7)) ? 4 : 12;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   /*
    * see border_set_gen6_SAMPLER_BORDER_COLOR_STATE() and
    * border_set_gen7_SAMPLER_BORDER_COLOR_STATE()
    */
   return ilo_builder_dynamic_write(builder, ILO_BUILDER_ITEM_BLOB,
         state_align, state_len, border->color);
}

static inline uint32_t
gen6_push_constant_buffer(struct ilo_builder *builder,
                          int size, void **pcb)
{
   /*
    * For all VS, GS, FS, and CS push constant buffers, they must be aligned
    * to 32 bytes, and their sizes are specified in 256-bit units.
    */
   const int state_align = 32;
   const int state_len = align(size, 32) / 4;
   uint32_t state_offset;
   char *buf;

   ILO_DEV_ASSERT(builder->dev, 6, 8);

   state_offset = ilo_builder_dynamic_pointer(builder,
         ILO_BUILDER_ITEM_BLOB, state_align, state_len, (uint32_t **) &buf);

   /* zero out the unused range */
   if (size < state_len * 4)
      memset(&buf[size], 0, state_len * 4 - size);

   if (pcb)
      *pcb = buf;

   return state_offset;
}

static inline uint32_t
gen6_user_vertex_buffer(struct ilo_builder *builder,
                        int size, const void *vertices)
{
   const int state_align = 8;
   const int state_len = size / 4;

   ILO_DEV_ASSERT(builder->dev, 6, 7.5);

   assert(size % 4 == 0);

   return ilo_builder_dynamic_write(builder, ILO_BUILDER_ITEM_BLOB,
         state_align, state_len, vertices);
}

#endif /* ILO_BUILDER_3D_TOP_H */
