/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2012-2015 LunarG, Inc.
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

#include "ilo_debug.h"
#include "ilo_state_shader.h"

enum vertex_stage {
   STAGE_VS,
   STAGE_HS,
   STAGE_DS,
   STAGE_GS,
};

struct vertex_ff {
   uint8_t grf_start;

   uint8_t per_thread_scratch_space;
   uint32_t per_thread_scratch_size;

   uint8_t sampler_count;
   uint8_t surface_count;
   bool has_uav;

   uint8_t vue_read_offset;
   uint8_t vue_read_len;

   uint8_t user_clip_enables;
};

static bool
vertex_validate_gen6_kernel(const struct ilo_dev *dev,
                            enum vertex_stage stage,
                            const struct ilo_state_shader_kernel_info *kernel)
{
   /*
    * "Dispatch GRF Start Register for URB Data" is U4 for GS and U5 for
    * others.
    */
   const uint8_t max_grf_start = (stage == STAGE_GS) ? 16 : 32;

   ILO_DEV_ASSERT(dev, 6, 8);

   /* we do not want to save it */
   assert(!kernel->offset);

   assert(kernel->grf_start < max_grf_start);

   return true;
}

static bool
vertex_validate_gen6_urb(const struct ilo_dev *dev,
                         enum vertex_stage stage,
                         const struct ilo_state_shader_urb_info *urb)
{
   /* "Vertex/Patch URB Entry Read Offset" is U6, in pairs */
   const uint8_t max_read_base = 63 * 2;
   /*
    * "Vertex/Patch URB Entry Read Length" is limited to 64 for DS and U6 for
    * others, in pairs
    */
   const uint8_t max_read_count = ((stage == STAGE_DS) ? 64 : 63) * 2;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(urb->read_base + urb->read_count <= urb->cv_input_attr_count);

   assert(urb->read_base % 2 == 0 && urb->read_base <= max_read_base);

   /*
    * There is no need to worry about reading past entries, as URB entries are
    * aligned to 1024-bits (Gen6) or 512-bits (Gen7+).
    */
   assert(urb->read_count <= max_read_count);

   return true;
}

static bool
vertex_get_gen6_ff(const struct ilo_dev *dev,
                   enum vertex_stage stage,
                   const struct ilo_state_shader_kernel_info *kernel,
                   const struct ilo_state_shader_resource_info *resource,
                   const struct ilo_state_shader_urb_info *urb,
                   uint32_t per_thread_scratch_size,
                   struct vertex_ff *ff)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   memset(ff, 0, sizeof(*ff));

   if (!vertex_validate_gen6_kernel(dev, stage, kernel) ||
       !vertex_validate_gen6_urb(dev, stage, urb))
      return false;

   ff->grf_start = kernel->grf_start;

   if (per_thread_scratch_size) {
      /*
       * From the Sandy Bridge PRM, volume 2 part 1, page 134:
       *
       *     "(Per-Thread Scratch Space)
       *      Range    [0,11] indicating [1K Bytes, 2M Bytes]"
       */
      assert(per_thread_scratch_size <= 2 * 1024 * 1024);

      /* next power of two, starting from 1KB */
      ff->per_thread_scratch_space = (per_thread_scratch_size > 1024) ?
         (util_last_bit(per_thread_scratch_size - 1) - 10) : 0;
      ff->per_thread_scratch_size = 1 << (10 + ff->per_thread_scratch_space);
   }

   ff->sampler_count = (resource->sampler_count <= 12) ?
      (resource->sampler_count + 3) / 4 : 4;
   ff->surface_count = resource->surface_count;
   ff->has_uav = resource->has_uav;

   ff->vue_read_offset = urb->read_base / 2;
   ff->vue_read_len = (urb->read_count + 1) / 2;

   /* need to read something unless VUE handles are included */
   switch (stage) {
   case STAGE_VS:
      if (!ff->vue_read_len)
         ff->vue_read_len = 1;

      /* one GRF per attribute */
      assert(kernel->grf_start + urb->read_count * 2 <= 128);
      break;
   case STAGE_GS:
      if (ilo_dev_gen(dev) == ILO_GEN(6) && !ff->vue_read_len)
         ff->vue_read_len = 1;
      break;
   default:
      break;
   }

   ff->user_clip_enables = urb->user_clip_enables;

   return true;
}

static uint16_t
vs_get_gen6_thread_count(const struct ilo_dev *dev,
                         const struct ilo_state_vs_info *info)
{
   uint16_t thread_count;

   ILO_DEV_ASSERT(dev, 6, 8);

   /* Maximum Number of Threads of 3DSTATE_VS */
   switch (ilo_dev_gen(dev)) {
   case ILO_GEN(8):
      thread_count = 504;
      break;
   case ILO_GEN(7.5):
      thread_count = (dev->gt >= 2) ? 280 : 70;
      break;
   case ILO_GEN(7):
   case ILO_GEN(6):
   default:
      thread_count = dev->thread_count;
      break;
   }

   return thread_count - 1;
}

static bool
vs_set_gen6_3DSTATE_VS(struct ilo_state_vs *vs,
                       const struct ilo_dev *dev,
                       const struct ilo_state_vs_info *info)
{
   struct vertex_ff ff;
   uint16_t thread_count;
   uint32_t dw2, dw3, dw4, dw5;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (!vertex_get_gen6_ff(dev, STAGE_VS, &info->kernel, &info->resource,
            &info->urb, info->per_thread_scratch_size, &ff))
      return false;

   thread_count = vs_get_gen6_thread_count(dev, info);

   dw2 = ff.sampler_count << GEN6_THREADDISP_SAMPLER_COUNT__SHIFT |
         ff.surface_count << GEN6_THREADDISP_BINDING_TABLE_SIZE__SHIFT;

   if (false)
      dw2 |= GEN6_THREADDISP_FP_MODE_ALT;

   if (ilo_dev_gen(dev) >= ILO_GEN(7.5) && ff.has_uav)
      dw2 |= GEN75_THREADDISP_ACCESS_UAV;

   dw3 = ff.per_thread_scratch_space <<
      GEN6_THREADSCRATCH_SPACE_PER_THREAD__SHIFT;

   dw4 = ff.grf_start << GEN6_VS_DW4_URB_GRF_START__SHIFT |
         ff.vue_read_len << GEN6_VS_DW4_URB_READ_LEN__SHIFT |
         ff.vue_read_offset << GEN6_VS_DW4_URB_READ_OFFSET__SHIFT;

   dw5 = 0;

   if (ilo_dev_gen(dev) >= ILO_GEN(7.5))
      dw5 |= thread_count << GEN75_VS_DW5_MAX_THREADS__SHIFT;
   else
      dw5 |= thread_count << GEN6_VS_DW5_MAX_THREADS__SHIFT;

   if (info->stats_enable)
      dw5 |= GEN6_VS_DW5_STATISTICS;
   if (info->dispatch_enable)
      dw5 |= GEN6_VS_DW5_VS_ENABLE;

   STATIC_ASSERT(ARRAY_SIZE(vs->vs) >= 5);
   vs->vs[0] = dw2;
   vs->vs[1] = dw3;
   vs->vs[2] = dw4;
   vs->vs[3] = dw5;

   if (ilo_dev_gen(dev) >= ILO_GEN(8))
      vs->vs[4] = ff.user_clip_enables << GEN8_VS_DW8_UCP_CLIP_ENABLES__SHIFT;

   vs->scratch_size = ff.per_thread_scratch_size * thread_count;

   return true;
}

static uint16_t
hs_get_gen7_thread_count(const struct ilo_dev *dev,
                         const struct ilo_state_hs_info *info)
{
   uint16_t thread_count;

   ILO_DEV_ASSERT(dev, 7, 8);

   /* Maximum Number of Threads of 3DSTATE_HS */
   switch (ilo_dev_gen(dev)) {
   case ILO_GEN(8):
      thread_count = 504;
      break;
   case ILO_GEN(7.5):
      thread_count = (dev->gt >= 2) ? 256 : 70;
      break;
   case ILO_GEN(7):
   default:
      thread_count = dev->thread_count;
      break;
   }

   return thread_count - 1;
}

static bool
hs_set_gen7_3DSTATE_HS(struct ilo_state_hs *hs,
                       const struct ilo_dev *dev,
                       const struct ilo_state_hs_info *info)
{
   struct vertex_ff ff;
   uint16_t thread_count;
   uint32_t dw1, dw2, dw4, dw5;

   ILO_DEV_ASSERT(dev, 7, 8);

   if (!vertex_get_gen6_ff(dev, STAGE_HS, &info->kernel, &info->resource,
            &info->urb, info->per_thread_scratch_size, &ff))
      return false;

   thread_count = hs_get_gen7_thread_count(dev, info);

   dw1 = ff.sampler_count << GEN6_THREADDISP_SAMPLER_COUNT__SHIFT |
         ff.surface_count << GEN6_THREADDISP_BINDING_TABLE_SIZE__SHIFT;

   dw2 = 0 << GEN7_HS_DW2_INSTANCE_COUNT__SHIFT;

   if (ilo_dev_gen(dev) >= ILO_GEN(8))
      dw2 |= thread_count << GEN8_HS_DW2_MAX_THREADS__SHIFT;
   else if (ilo_dev_gen(dev) >= ILO_GEN(7.5))
      dw1 |= thread_count << GEN75_HS_DW1_DISPATCH_MAX_THREADS__SHIFT;
   else
      dw1 |= thread_count << GEN7_HS_DW1_DISPATCH_MAX_THREADS__SHIFT;

   if (info->dispatch_enable)
      dw2 |= GEN7_HS_DW2_HS_ENABLE;
   if (info->stats_enable)
      dw2 |= GEN7_HS_DW2_STATISTICS;

   dw4 = ff.per_thread_scratch_space <<
      GEN6_THREADSCRATCH_SPACE_PER_THREAD__SHIFT;

   dw5 = GEN7_HS_DW5_INCLUDE_VERTEX_HANDLES |
         ff.grf_start << GEN7_HS_DW5_URB_GRF_START__SHIFT |
         ff.vue_read_len << GEN7_HS_DW5_URB_READ_LEN__SHIFT |
         ff.vue_read_offset << GEN7_HS_DW5_URB_READ_OFFSET__SHIFT;

   if (ilo_dev_gen(dev) >= ILO_GEN(7.5) && ff.has_uav)
      dw5 |= GEN75_HS_DW5_ACCESS_UAV;

   STATIC_ASSERT(ARRAY_SIZE(hs->hs) >= 4);
   hs->hs[0] = dw1;
   hs->hs[1] = dw2;
   hs->hs[2] = dw4;
   hs->hs[3] = dw5;

   hs->scratch_size = ff.per_thread_scratch_size * thread_count;

   return true;
}

static bool
ds_set_gen7_3DSTATE_TE(struct ilo_state_ds *ds,
                       const struct ilo_dev *dev,
                       const struct ilo_state_ds_info *info)
{
   uint32_t dw1;

   ILO_DEV_ASSERT(dev, 7, 8);

   dw1 = 0;

   if (info->dispatch_enable) {
      dw1 |= GEN7_TE_DW1_MODE_HW |
             GEN7_TE_DW1_TE_ENABLE;
   }

   STATIC_ASSERT(ARRAY_SIZE(ds->te) >= 3);
   ds->te[0] = dw1;
   ds->te[1] = fui(63.0f);
   ds->te[2] = fui(64.0f);

   return true;
}

static uint16_t
ds_get_gen7_thread_count(const struct ilo_dev *dev,
                         const struct ilo_state_ds_info *info)
{
   uint16_t thread_count;

   ILO_DEV_ASSERT(dev, 7, 8);

   /* Maximum Number of Threads of 3DSTATE_DS */
   switch (ilo_dev_gen(dev)) {
   case ILO_GEN(8):
      thread_count = 504;
      break;
   case ILO_GEN(7.5):
      thread_count = (dev->gt >= 2) ? 280 : 70;
      break;
   case ILO_GEN(7):
   default:
      thread_count = dev->thread_count;
      break;
   }

   return thread_count - 1;
}

static bool
ds_set_gen7_3DSTATE_DS(struct ilo_state_ds *ds,
                       const struct ilo_dev *dev,
                       const struct ilo_state_ds_info *info)
{
   struct vertex_ff ff;
   uint16_t thread_count;
   uint32_t dw2, dw3, dw4, dw5;

   ILO_DEV_ASSERT(dev, 7, 8);

   if (!vertex_get_gen6_ff(dev, STAGE_DS, &info->kernel, &info->resource,
            &info->urb, info->per_thread_scratch_size, &ff))
      return false;

   thread_count = ds_get_gen7_thread_count(dev, info);

   dw2 = ff.sampler_count << GEN6_THREADDISP_SAMPLER_COUNT__SHIFT |
         ff.surface_count << GEN6_THREADDISP_BINDING_TABLE_SIZE__SHIFT;

   if (ilo_dev_gen(dev) >= ILO_GEN(7.5) && ff.has_uav)
      dw2 |= GEN75_THREADDISP_ACCESS_UAV;

   dw3 = ff.per_thread_scratch_space <<
      GEN6_THREADSCRATCH_SPACE_PER_THREAD__SHIFT;

   dw4 = ff.grf_start << GEN7_DS_DW4_URB_GRF_START__SHIFT |
         ff.vue_read_len << GEN7_DS_DW4_URB_READ_LEN__SHIFT |
         ff.vue_read_offset << GEN7_DS_DW4_URB_READ_OFFSET__SHIFT;

   dw5 = 0;

   if (ilo_dev_gen(dev) >= ILO_GEN(7.5))
      dw5 |= thread_count << GEN75_DS_DW5_MAX_THREADS__SHIFT;
   else
      dw5 |= thread_count << GEN7_DS_DW5_MAX_THREADS__SHIFT;

   if (info->stats_enable)
      dw5 |= GEN7_DS_DW5_STATISTICS;
   if (info->dispatch_enable)
      dw5 |= GEN7_DS_DW5_DS_ENABLE;

   STATIC_ASSERT(ARRAY_SIZE(ds->ds) >= 5);
   ds->ds[0] = dw2;
   ds->ds[1] = dw3;
   ds->ds[2] = dw4;
   ds->ds[3] = dw5;

   if (ilo_dev_gen(dev) >= ILO_GEN(8))
      ds->ds[4] = ff.user_clip_enables << GEN8_DS_DW8_UCP_CLIP_ENABLES__SHIFT;

   ds->scratch_size = ff.per_thread_scratch_size * thread_count;

   return true;
}

static bool
gs_get_gen6_ff(const struct ilo_dev *dev,
               const struct ilo_state_gs_info *info,
               struct vertex_ff *ff)
{
   const struct ilo_state_shader_urb_info *urb = &info->urb;
   const struct ilo_state_gs_sol_info *sol = &info->sol;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (!vertex_get_gen6_ff(dev, STAGE_GS, &info->kernel, &info->resource,
            &info->urb, info->per_thread_scratch_size, ff))
      return false;

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 168-169:
    *
    *     "[0,62] indicating [1,63] 16B units"
    *
    *     "Programming Restrictions: The vertex size must be programmed as a
    *      multiple of 32B units with the following exception: Rendering is
    *      disabled (as per SOL stage state) and the vertex size output by the
    *      GS thread is 16B.
    *
    *      If rendering is enabled (as per SOL state) the vertex size must be
    *      programmed as a multiple of 32B units. In other words, the only
    *      time software can program a vertex size with an odd number of 16B
    *      units is when rendering is disabled."
    */
   assert(urb->output_attr_count <= 63);
   if (!sol->render_disable)
      assert(urb->output_attr_count % 2 == 0);

   return true;
}

static uint16_t
gs_get_gen6_thread_count(const struct ilo_dev *dev,
                         const struct ilo_state_gs_info *info)
{
   const struct ilo_state_gs_sol_info *sol = &info->sol;
   uint16_t thread_count;

   ILO_DEV_ASSERT(dev, 6, 8);

   /* Maximum Number of Threads of 3DSTATE_GS */
   switch (ilo_dev_gen(dev)) {
   case ILO_GEN(8):
      thread_count = 504;
      break;
   case ILO_GEN(7.5):
      thread_count = (dev->gt >= 2) ? 256 : 70;
      break;
   case ILO_GEN(7):
   case ILO_GEN(6):
   default:
      thread_count = dev->thread_count;

      /*
       * From the Sandy Bridge PRM, volume 2 part 1, page 154:
       *
       *     "Maximum Number of Threads valid range is [0,27] when Rendering
       *      Enabled bit is set."
       *
       * According to the classic driver, [0, 20] for GT1.
       */
      if (!sol->render_disable)
         thread_count = (dev->gt == 2) ? 27 : 20;
      break;
   }

   return thread_count - 1;
}

static bool
gs_set_gen6_3DSTATE_GS(struct ilo_state_gs *gs,
                       const struct ilo_dev *dev,
                       const struct ilo_state_gs_info *info)
{
   const struct ilo_state_gs_sol_info *sol = &info->sol;
   struct vertex_ff ff;
   uint16_t thread_count;
   uint32_t dw2, dw3, dw4, dw5, dw6;

   ILO_DEV_ASSERT(dev, 6, 6);

   if (!gs_get_gen6_ff(dev, info, &ff))
      return false;

   thread_count = gs_get_gen6_thread_count(dev, info);

   dw2 = GEN6_THREADDISP_SPF |
         ff.sampler_count << GEN6_THREADDISP_SAMPLER_COUNT__SHIFT |
         ff.surface_count << GEN6_THREADDISP_BINDING_TABLE_SIZE__SHIFT;

   dw3 = ff.per_thread_scratch_space <<
      GEN6_THREADSCRATCH_SPACE_PER_THREAD__SHIFT;

   dw4 = ff.vue_read_len << GEN6_GS_DW4_URB_READ_LEN__SHIFT |
         ff.vue_read_offset << GEN6_GS_DW4_URB_READ_OFFSET__SHIFT |
         ff.grf_start << GEN6_GS_DW4_URB_GRF_START__SHIFT;

   dw5 = thread_count << GEN6_GS_DW5_MAX_THREADS__SHIFT;

   if (info->stats_enable)
      dw5 |= GEN6_GS_DW5_STATISTICS;
   if (sol->stats_enable)
      dw5 |= GEN6_GS_DW5_SO_STATISTICS;
   if (!sol->render_disable)
      dw5 |= GEN6_GS_DW5_RENDER_ENABLE;

   dw6 = 0;

   /* GEN7_REORDER_TRAILING is handled by the kernel */
   if (sol->tristrip_reorder == GEN7_REORDER_LEADING)
      dw6 |= GEN6_GS_DW6_REORDER_LEADING_ENABLE;

   if (sol->sol_enable) {
      dw6 |= GEN6_GS_DW6_SVBI_PAYLOAD_ENABLE;

      if (sol->svbi_post_inc) {
         dw6 |= GEN6_GS_DW6_SVBI_POST_INC_ENABLE |
                sol->svbi_post_inc << GEN6_GS_DW6_SVBI_POST_INC_VAL__SHIFT;
      }
   }

   if (info->dispatch_enable)
      dw6 |= GEN6_GS_DW6_GS_ENABLE;

   STATIC_ASSERT(ARRAY_SIZE(gs->gs) >= 5);
   gs->gs[0] = dw2;
   gs->gs[1] = dw3;
   gs->gs[2] = dw4;
   gs->gs[3] = dw5;
   gs->gs[4] = dw6;

   gs->scratch_size = ff.per_thread_scratch_size * thread_count;

   return true;
}

static uint8_t
gs_get_gen7_vertex_size(const struct ilo_dev *dev,
                        const struct ilo_state_gs_info *info)
{
   const struct ilo_state_shader_urb_info *urb = &info->urb;

   ILO_DEV_ASSERT(dev, 7, 8);

   return (urb->output_attr_count) ? urb->output_attr_count - 1 : 0;
}

static bool
gs_set_gen7_3DSTATE_GS(struct ilo_state_gs *gs,
                       const struct ilo_dev *dev,
                       const struct ilo_state_gs_info *info)
{
   struct vertex_ff ff;
   uint16_t thread_count;
   uint8_t vertex_size;
   uint32_t dw2, dw3, dw4, dw5;

   ILO_DEV_ASSERT(dev, 7, 8);

   if (!gs_get_gen6_ff(dev, info, &ff))
      return false;

   thread_count = gs_get_gen6_thread_count(dev, info);
   vertex_size = gs_get_gen7_vertex_size(dev, info);

   dw2 = ff.sampler_count << GEN6_THREADDISP_SAMPLER_COUNT__SHIFT |
         ff.surface_count << GEN6_THREADDISP_BINDING_TABLE_SIZE__SHIFT;

   if (ilo_dev_gen(dev) >= ILO_GEN(7.5) && ff.has_uav)
      dw2 |= GEN75_THREADDISP_ACCESS_UAV;

   dw3 = ff.per_thread_scratch_space <<
      GEN6_THREADSCRATCH_SPACE_PER_THREAD__SHIFT;

   dw4 = vertex_size << GEN7_GS_DW4_OUTPUT_SIZE__SHIFT |
         0 << GEN7_GS_DW4_OUTPUT_TOPO__SHIFT |
         ff.vue_read_len << GEN7_GS_DW4_URB_READ_LEN__SHIFT |
         GEN7_GS_DW4_INCLUDE_VERTEX_HANDLES |
         ff.vue_read_offset << GEN7_GS_DW4_URB_READ_OFFSET__SHIFT |
         ff.grf_start << GEN7_GS_DW4_URB_GRF_START__SHIFT;

   dw5 = 0;

   if (ilo_dev_gen(dev) >= ILO_GEN(7.5))
      dw5 = thread_count << GEN75_GS_DW5_MAX_THREADS__SHIFT;
   else
      dw5 = thread_count << GEN7_GS_DW5_MAX_THREADS__SHIFT;

   if (info->stats_enable)
      dw5 |= GEN7_GS_DW5_STATISTICS;
   if (info->dispatch_enable)
      dw5 |= GEN7_GS_DW5_GS_ENABLE;

   STATIC_ASSERT(ARRAY_SIZE(gs->gs) >= 5);
   gs->gs[0] = dw2;
   gs->gs[1] = dw3;
   gs->gs[2] = dw4;
   gs->gs[3] = dw5;

   if (ilo_dev_gen(dev) >= ILO_GEN(8))
      gs->gs[4] = ff.user_clip_enables << GEN8_GS_DW9_UCP_CLIP_ENABLES__SHIFT;

   gs->scratch_size = ff.per_thread_scratch_size * thread_count;

   return true;
}

bool
ilo_state_vs_init(struct ilo_state_vs *vs,
                  const struct ilo_dev *dev,
                  const struct ilo_state_vs_info *info)
{
   bool ret = true;

   assert(ilo_is_zeroed(vs, sizeof(*vs)));

   ret &= vs_set_gen6_3DSTATE_VS(vs, dev, info);

   assert(ret);

   return ret;
}

bool
ilo_state_vs_init_disabled(struct ilo_state_vs *vs,
                           const struct ilo_dev *dev)
{
   struct ilo_state_vs_info info;

   memset(&info, 0, sizeof(info));

   return ilo_state_vs_init(vs, dev, &info);
}

bool
ilo_state_hs_init(struct ilo_state_hs *hs,
                  const struct ilo_dev *dev,
                  const struct ilo_state_hs_info *info)
{
   bool ret = true;

   assert(ilo_is_zeroed(hs, sizeof(*hs)));

   if (ilo_dev_gen(dev) >= ILO_GEN(7))
      ret &= hs_set_gen7_3DSTATE_HS(hs, dev, info);

   assert(ret);

   return ret;
}

bool
ilo_state_hs_init_disabled(struct ilo_state_hs *hs,
                           const struct ilo_dev *dev)
{
   struct ilo_state_hs_info info;

   memset(&info, 0, sizeof(info));

   return ilo_state_hs_init(hs, dev, &info);
}

bool
ilo_state_ds_init(struct ilo_state_ds *ds,
                  const struct ilo_dev *dev,
                  const struct ilo_state_ds_info *info)
{
   bool ret = true;

   assert(ilo_is_zeroed(ds, sizeof(*ds)));

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      ret &= ds_set_gen7_3DSTATE_TE(ds, dev, info);
      ret &= ds_set_gen7_3DSTATE_DS(ds, dev, info);
   }

   assert(ret);

   return ret;
}

bool
ilo_state_ds_init_disabled(struct ilo_state_ds *ds,
                           const struct ilo_dev *dev)
{
   struct ilo_state_ds_info info;

   memset(&info, 0, sizeof(info));

   return ilo_state_ds_init(ds, dev, &info);
}

bool
ilo_state_gs_init(struct ilo_state_gs *gs,
                  const struct ilo_dev *dev,
                  const struct ilo_state_gs_info *info)
{
   bool ret = true;

   assert(ilo_is_zeroed(gs, sizeof(*gs)));

   if (ilo_dev_gen(dev) >= ILO_GEN(7))
      ret &= gs_set_gen7_3DSTATE_GS(gs, dev, info);
   else
      ret &= gs_set_gen6_3DSTATE_GS(gs, dev, info);

   assert(ret);

   return ret;
}

bool
ilo_state_gs_init_disabled(struct ilo_state_gs *gs,
                           const struct ilo_dev *dev)
{
   struct ilo_state_gs_info info;

   memset(&info, 0, sizeof(info));

   return ilo_state_gs_init(gs, dev, &info);
}
