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

struct pixel_ff {
   uint8_t dispatch_modes;

   uint32_t kernel_offsets[3];
   uint8_t grf_starts[3];
   bool pcb_enable;
   uint8_t scratch_space;

   uint8_t sampler_count;
   uint8_t surface_count;
   bool has_uav;

   uint16_t thread_count;

   struct ilo_state_ps_dispatch_conds conds;

   bool kill_pixel;
   bool dispatch_enable;
   bool dual_source_blending;
   uint32_t sample_mask;
};

static bool
ps_kernel_validate_gen6(const struct ilo_dev *dev,
                        const struct ilo_state_shader_kernel_info *kernel)
{
   /* "Dispatch GRF Start Register for Constant/Setup Data" is U7 */
   const uint8_t max_grf_start = 128;
   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 271:
    *
    *     "(Per-Thread Scratch Space)
    *      Range  [0,11] indicating [1k bytes, 2M bytes] in powers of two"
    */
   const uint32_t max_scratch_size = 2 * 1024 * 1024;

   ILO_DEV_ASSERT(dev, 6, 8);

   /* "Kernel Start Pointer" is 64-byte aligned */
   assert(kernel->offset % 64 == 0);

   assert(kernel->grf_start < max_grf_start);
   assert(kernel->scratch_size <= max_scratch_size);

   return true;
}

static bool
ps_validate_gen6(const struct ilo_dev *dev,
                 const struct ilo_state_ps_info *info)
{
   const struct ilo_state_shader_kernel_info *kernel_8 = &info->kernel_8;
   const struct ilo_state_shader_kernel_info *kernel_16 = &info->kernel_16;
   const struct ilo_state_shader_kernel_info *kernel_32 = &info->kernel_32;
   const struct ilo_state_ps_io_info *io = &info->io;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (!ps_kernel_validate_gen6(dev, kernel_8) ||
       !ps_kernel_validate_gen6(dev, kernel_16) ||
       !ps_kernel_validate_gen6(dev, kernel_32))
      return false;

   /* unsupported on Gen6 */
   if (ilo_dev_gen(dev) == ILO_GEN(6))
      assert(!io->use_coverage_mask);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 275:
    *
    *     "If a NULL Depth Buffer is selected, the Pixel Shader Computed Depth
    *      field must be set to disabled."
    */
   if (ilo_dev_gen(dev) == ILO_GEN(6) && io->pscdepth != GEN7_PSCDEPTH_OFF)
      assert(info->cv_has_depth_buffer);

   if (!info->per_sample_dispatch) {
      /*
       * From the Sandy Bridge PRM, volume 2 part 1, page 281:
       *
       *     "MSDISPMODE_PERSAMPLE is required in order to select
       *      POSOFFSET_SAMPLE."
       */
      assert(io->posoffset != GEN6_POSOFFSET_SAMPLE);

      /*
       * From the Sandy Bridge PRM, volume 2 part 1, page 282:
       *
       *     "MSDISPMODE_PERSAMPLE is required in order to select
       *      INTERP_SAMPLE."
       *
       * From the Sandy Bridge PRM, volume 2 part 1, page 283:
       *
       *     "MSDISPMODE_PERSAMPLE is required in order to select Perspective
       *      Sample or Non-perspective Sample barycentric coordinates."
       */
      assert(!info->cv_per_sample_interp);
   }

   /*
    *
    * From the Sandy Bridge PRM, volume 2 part 1, page 314:
    *
    *     "Pixel Shader Dispatch, Alpha... must all be disabled."
    *
    * Simply disallow any valid kernel when there is early-z op.  Also, when
    * there is no valid kernel, io should be zeroed.
    */
   if (info->valid_kernels)
      assert(!info->cv_has_earlyz_op);
   else
      assert(ilo_is_zeroed(io, sizeof(*io)));

   return true;
}

static uint8_t
ps_get_gen6_dispatch_modes(const struct ilo_dev *dev,
                           const struct ilo_state_ps_info *info)
{
   const struct ilo_state_ps_io_info *io = &info->io;
   uint8_t dispatch_modes = info->valid_kernels;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (!dispatch_modes)
      return 0;

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 334:
    *
    *     "Not valid on [DevSNB] if 4x PERPIXEL mode with pixel shader
    *      computed depth."
    *
    *     "Valid on all products, except when in non-1x PERSAMPLE mode
    *      (applies to [DevSNB+] only)"
    *
    * From the Sandy Bridge PRM, volume 4 part 1, page 239:
    *
    *     "[DevSNB]: When Pixel Shader outputs oDepth and PS invocation mode
    *      is PERPIXEL, Message Type for Render Target Write must be SIMD8.
    *
    *      Errata: [DevSNB+]: When Pixel Shader outputs oMask, this message
    *      type is not supported: SIMD8 (including SIMD8_DUALSRC_xx)."
    *
    * It is really hard to follow what combinations are valid on what
    * platforms.  Judging from the restrictions on RT write messages on Gen6,
    * oDepth and oMask related issues should be Gen6-specific.  PERSAMPLE
    * issue should be universal, and disallows multiple dispatch modes.
    */
   if (ilo_dev_gen(dev) == ILO_GEN(6)) {
      if (io->pscdepth != GEN7_PSCDEPTH_OFF && !info->per_sample_dispatch)
         dispatch_modes &= GEN6_PS_DISPATCH_8;
      if (io->write_omask)
         dispatch_modes &= ~GEN6_PS_DISPATCH_8;
   }
   if (info->per_sample_dispatch && !info->sample_count_one) {
      /* prefer 32 over 16 over 8 */
      if (dispatch_modes & GEN6_PS_DISPATCH_32)
         dispatch_modes &= GEN6_PS_DISPATCH_32;
      else if (dispatch_modes & GEN6_PS_DISPATCH_16)
         dispatch_modes &= GEN6_PS_DISPATCH_16;
      else
         dispatch_modes &= GEN6_PS_DISPATCH_8;
   }

   /*
    * From the Broadwell PRM, volume 2b, page 149:
    *
    *     "When Render Target Fast Clear Enable is ENABLED or Render Target
    *      Resolve Type = RESOLVE_PARTIAL or RESOLVE_FULL, this bit (8 Pixel
    *      Dispatch or Dual-8 Pixel Dispatch Enable) must be DISABLED."
    */
   if (info->rt_clear_enable || info->rt_resolve_enable)
      dispatch_modes &= ~GEN6_PS_DISPATCH_8;

   assert(dispatch_modes);

   return dispatch_modes;
}

static uint16_t
ps_get_gen6_thread_count(const struct ilo_dev *dev,
                         const struct ilo_state_ps_info *info)
{
   uint16_t thread_count;

   ILO_DEV_ASSERT(dev, 6, 8);

   /* Maximum Number of Threads of 3DSTATE_PS */
   switch (ilo_dev_gen(dev)) {
   case ILO_GEN(8):
      /* scaled automatically */
      thread_count = 64 - 1;
      break;
   case ILO_GEN(7.5):
      thread_count = (dev->gt == 3) ? 408 :
                     (dev->gt == 2) ? 204 : 102;
      break;
   case ILO_GEN(7):
      thread_count = (dev->gt == 2) ? 172 : 48;
      break;
   case ILO_GEN(6):
   default:
      /* from the classic driver instead of the PRM */
      thread_count = (dev->gt == 2) ? 80 : 40;
      break;
   }

   return thread_count - 1;
}

static bool
ps_params_get_gen6_kill_pixel(const struct ilo_dev *dev,
                              const struct ilo_state_ps_params_info *params,
                              const struct ilo_state_ps_dispatch_conds *conds)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 275:
    *
    *     "This bit (Pixel Shader Kill Pixel), if ENABLED, indicates that the
    *      PS kernel or color calculator has the ability to kill (discard)
    *      pixels or samples, other than due to depth or stencil testing.
    *      This bit is required to be ENABLED in the following situations:
    *
    *      The API pixel shader program contains "killpix" or "discard"
    *      instructions, or other code in the pixel shader kernel that can
    *      cause the final pixel mask to differ from the pixel mask received
    *      on dispatch.
    *
    *      A sampler with chroma key enabled with kill pixel mode is used by
    *      the pixel shader.
    *
    *      Any render target has Alpha Test Enable or AlphaToCoverage Enable
    *      enabled.
    *
    *      The pixel shader kernel generates and outputs oMask.
    *
    *      Note: As ClipDistance clipping is fully supported in hardware and
    *      therefore not via PS instructions, there should be no need to
    *      ENABLE this bit due to ClipDistance clipping."
    */
   return (conds->ps_may_kill || params->alpha_may_kill);
}

static bool
ps_params_get_gen6_dispatch_enable(const struct ilo_dev *dev,
                                   const struct ilo_state_ps_params_info *params,
                                   const struct ilo_state_ps_dispatch_conds *conds)
{
   /*
    * We want to skip dispatching when EarlyZ suffices.  The conditions that
    * require dispatching are
    *
    *  - PS writes RTs and RTs are writeable
    *  - PS changes depth value and depth test/write is enabled
    *  - PS changes stencil value and stencil test is enabled
    *  - PS writes UAVs
    *  - PS or CC kills pixels
    *  - EDSC is PSEXEC, and depth test/write or stencil test is enabled
    */
   bool dispatch_required =
      ((conds->has_rt_write && params->has_writeable_rt) ||
       conds->write_odepth ||
       conds->write_ostencil ||
       conds->has_uav_write ||
       ps_params_get_gen6_kill_pixel(dev, params, conds) ||
       params->earlyz_control_psexec);

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 280:
    *
    *     "If EDSC_PSEXEC mode is selected, Thread Dispatch Enable must be
    *      set."
    */
   if (ilo_dev_gen(dev) < ILO_GEN(8) && params->earlyz_control_psexec)
      dispatch_required = true;

   /* assert it is valid to dispatch */
   if (dispatch_required)
      assert(conds->ps_valid);

   return dispatch_required;
}

static bool
ps_get_gen6_ff_kernels(const struct ilo_dev *dev,
                       const struct ilo_state_ps_info *info,
                       struct pixel_ff *ff)
{
   const struct ilo_state_shader_kernel_info *kernel_8 = &info->kernel_8;
   const struct ilo_state_shader_kernel_info *kernel_16 = &info->kernel_16;
   const struct ilo_state_shader_kernel_info *kernel_32 = &info->kernel_32;
   uint32_t scratch_size;

   ILO_DEV_ASSERT(dev, 6, 8);

   ff->dispatch_modes = ps_get_gen6_dispatch_modes(dev, info);

   /* initialize kernel offsets and GRF starts */
   if (util_is_power_of_two(ff->dispatch_modes)) {
      if (ff->dispatch_modes & GEN6_PS_DISPATCH_8) {
         ff->kernel_offsets[0] = kernel_8->offset;
         ff->grf_starts[0] = kernel_8->grf_start;
      } else if (ff->dispatch_modes & GEN6_PS_DISPATCH_16) {
         ff->kernel_offsets[0] = kernel_16->offset;
         ff->grf_starts[0] = kernel_16->grf_start;
      } else if (ff->dispatch_modes & GEN6_PS_DISPATCH_32) {
         ff->kernel_offsets[0] = kernel_32->offset;
         ff->grf_starts[0] = kernel_32->grf_start;
      }
   } else {
      ff->kernel_offsets[0] = kernel_8->offset;
      ff->kernel_offsets[1] = kernel_32->offset;
      ff->kernel_offsets[2] = kernel_16->offset;

      ff->grf_starts[0] = kernel_8->grf_start;
      ff->grf_starts[1] = kernel_32->grf_start;
      ff->grf_starts[2] = kernel_16->grf_start;
   }

   /* we do not want to save it */
   assert(ff->kernel_offsets[0] == 0);

   ff->pcb_enable = (((ff->dispatch_modes & GEN6_PS_DISPATCH_8) &&
                      kernel_8->pcb_attr_count) ||
                     ((ff->dispatch_modes & GEN6_PS_DISPATCH_16) &&
                      kernel_16->pcb_attr_count) ||
                     ((ff->dispatch_modes & GEN6_PS_DISPATCH_32) &&
                      kernel_32->pcb_attr_count));

   scratch_size = 0;
   if ((ff->dispatch_modes & GEN6_PS_DISPATCH_8) &&
       scratch_size < kernel_8->scratch_size)
      scratch_size = kernel_8->scratch_size;
   if ((ff->dispatch_modes & GEN6_PS_DISPATCH_16) &&
       scratch_size < kernel_16->scratch_size)
      scratch_size = kernel_16->scratch_size;
   if ((ff->dispatch_modes & GEN6_PS_DISPATCH_32) &&
       scratch_size < kernel_32->scratch_size)
      scratch_size = kernel_32->scratch_size;

   /* next power of two, starting from 1KB */
   ff->scratch_space = (scratch_size > 1024) ?
      (util_last_bit(scratch_size - 1) - 10): 0;

   /* GPU hangs on Haswell if none of the dispatch mode bits is set */
   if (ilo_dev_gen(dev) == ILO_GEN(7.5) && !ff->dispatch_modes)
      ff->dispatch_modes |= GEN6_PS_DISPATCH_8;

   return true;
}

static bool
ps_get_gen6_ff(const struct ilo_dev *dev,
               const struct ilo_state_ps_info *info,
               struct pixel_ff *ff)
{
   const struct ilo_state_shader_resource_info *resource = &info->resource;
   const struct ilo_state_ps_io_info *io = &info->io;
   const struct ilo_state_ps_params_info *params = &info->params;

   ILO_DEV_ASSERT(dev, 6, 8);

   memset(ff, 0, sizeof(*ff));

   if (!ps_validate_gen6(dev, info) || !ps_get_gen6_ff_kernels(dev, info, ff))
      return false;

   ff->sampler_count = (resource->sampler_count <= 12) ?
      (resource->sampler_count + 3) / 4 : 4;
   ff->surface_count = resource->surface_count;
   ff->has_uav = resource->has_uav;

   ff->thread_count = ps_get_gen6_thread_count(dev, info);

   ff->conds.ps_valid = (info->valid_kernels != 0x0);
   ff->conds.has_rt_write = io->has_rt_write;
   ff->conds.write_odepth = (io->pscdepth != GEN7_PSCDEPTH_OFF);
   ff->conds.write_ostencil = false;
   ff->conds.has_uav_write = resource->has_uav;
   ff->conds.ps_may_kill = (io->write_pixel_mask || io->write_omask);

   ff->kill_pixel = ps_params_get_gen6_kill_pixel(dev, params, &ff->conds);
   ff->dispatch_enable =
      ps_params_get_gen6_dispatch_enable(dev, params, &ff->conds);
   ff->dual_source_blending = params->dual_source_blending;
   ff->sample_mask = params->sample_mask;

   return true;
}

static bool
ps_set_gen6_3dstate_wm(struct ilo_state_ps *ps,
                       const struct ilo_dev *dev,
                       const struct ilo_state_ps_info *info,
                       const struct pixel_ff *ff)
{
   const struct ilo_state_ps_io_info *io = &info->io;
   uint32_t dw2, dw3, dw4, dw5, dw6;

   ILO_DEV_ASSERT(dev, 6, 6);

   dw2 = ff->sampler_count << GEN6_THREADDISP_SAMPLER_COUNT__SHIFT |
         ff->surface_count << GEN6_THREADDISP_BINDING_TABLE_SIZE__SHIFT;

   if (false)
      dw2 |= GEN6_THREADDISP_FP_MODE_ALT;

   dw3 = ff->scratch_space << GEN6_THREADSCRATCH_SPACE_PER_THREAD__SHIFT;

   dw4 = ff->grf_starts[0] << GEN6_WM_DW4_URB_GRF_START0__SHIFT |
         ff->grf_starts[1] << GEN6_WM_DW4_URB_GRF_START1__SHIFT |
         ff->grf_starts[2] << GEN6_WM_DW4_URB_GRF_START2__SHIFT;

   dw5 = ff->thread_count << GEN6_WM_DW5_MAX_THREADS__SHIFT |
         ff->dispatch_modes << GEN6_WM_DW5_PS_DISPATCH_MODE__SHIFT;

   if (ff->kill_pixel)
      dw5 |= GEN6_WM_DW5_PS_KILL_PIXEL;

   if (io->pscdepth != GEN7_PSCDEPTH_OFF)
      dw5 |= GEN6_WM_DW5_PS_COMPUTE_DEPTH;
   if (io->use_z)
      dw5 |= GEN6_WM_DW5_PS_USE_DEPTH;

   if (ff->dispatch_enable)
      dw5 |= GEN6_WM_DW5_PS_DISPATCH_ENABLE;

   if (io->write_omask)
      dw5 |= GEN6_WM_DW5_PS_COMPUTE_OMASK;
   if (io->use_w)
      dw5 |= GEN6_WM_DW5_PS_USE_W;

   if (ff->dual_source_blending)
      dw5 |= GEN6_WM_DW5_PS_DUAL_SOURCE_BLEND;

   dw6 = io->attr_count << GEN6_WM_DW6_SF_ATTR_COUNT__SHIFT |
         io->posoffset << GEN6_WM_DW6_PS_POSOFFSET__SHIFT;

   dw6 |= (info->per_sample_dispatch) ?
      GEN6_WM_DW6_MSDISPMODE_PERSAMPLE : GEN6_WM_DW6_MSDISPMODE_PERPIXEL;

   STATIC_ASSERT(ARRAY_SIZE(ps->ps) >= 7);
   ps->ps[0] = dw2;
   ps->ps[1] = dw3;
   ps->ps[2] = dw4;
   ps->ps[3] = dw5;
   ps->ps[4] = dw6;
   ps->ps[5] = ff->kernel_offsets[1];
   ps->ps[6] = ff->kernel_offsets[2];

   return true;
}

static bool
ps_set_gen7_3dstate_wm(struct ilo_state_ps *ps,
                       const struct ilo_dev *dev,
                       const struct ilo_state_ps_info *info,
                       const struct pixel_ff *ff)
{
   const struct ilo_state_ps_io_info *io = &info->io;
   uint32_t dw1, dw2;

   ILO_DEV_ASSERT(dev, 7, 7.5);

   dw1 = io->pscdepth << GEN7_WM_DW1_PSCDEPTH__SHIFT;

   if (ff->dispatch_enable)
      dw1 |= GEN7_WM_DW1_PS_DISPATCH_ENABLE;
   if (ff->kill_pixel)
      dw1 |= GEN7_WM_DW1_PS_KILL_PIXEL;

   if (io->use_z)
      dw1 |= GEN7_WM_DW1_PS_USE_DEPTH;
   if (io->use_w)
      dw1 |= GEN7_WM_DW1_PS_USE_W;
   if (io->use_coverage_mask)
      dw1 |= GEN7_WM_DW1_PS_USE_COVERAGE_MASK;

   dw2 = (info->per_sample_dispatch) ?
      GEN7_WM_DW2_MSDISPMODE_PERSAMPLE : GEN7_WM_DW2_MSDISPMODE_PERPIXEL;

   STATIC_ASSERT(ARRAY_SIZE(ps->ps) >= 2);
   ps->ps[0] = dw1;
   ps->ps[1] = dw2;

   return true;
}

static bool
ps_set_gen7_3DSTATE_PS(struct ilo_state_ps *ps,
                       const struct ilo_dev *dev,
                       const struct ilo_state_ps_info *info,
                       const struct pixel_ff *ff)
{
   const struct ilo_state_ps_io_info *io = &info->io;
   uint32_t dw2, dw3, dw4, dw5;

   ILO_DEV_ASSERT(dev, 7, 7.5);

   dw2 = ff->sampler_count << GEN6_THREADDISP_SAMPLER_COUNT__SHIFT |
         ff->surface_count << GEN6_THREADDISP_BINDING_TABLE_SIZE__SHIFT;

   if (false)
      dw2 |= GEN6_THREADDISP_FP_MODE_ALT;

   dw3 = ff->scratch_space << GEN6_THREADSCRATCH_SPACE_PER_THREAD__SHIFT;

   dw4 = io->posoffset << GEN7_PS_DW4_POSOFFSET__SHIFT |
         ff->dispatch_modes << GEN7_PS_DW4_DISPATCH_MODE__SHIFT;

   if (ilo_dev_gen(dev) == ILO_GEN(7.5)) {
      dw4 |= ff->thread_count << GEN75_PS_DW4_MAX_THREADS__SHIFT |
             (ff->sample_mask & 0xff) << GEN75_PS_DW4_SAMPLE_MASK__SHIFT;
   } else {
      dw4 |= ff->thread_count << GEN7_PS_DW4_MAX_THREADS__SHIFT;
   }

   if (ff->pcb_enable)
      dw4 |= GEN7_PS_DW4_PUSH_CONSTANT_ENABLE;
   if (io->attr_count)
      dw4 |= GEN7_PS_DW4_ATTR_ENABLE;
   if (io->write_omask)
      dw4 |= GEN7_PS_DW4_COMPUTE_OMASK;
   if (info->rt_clear_enable)
      dw4 |= GEN7_PS_DW4_RT_FAST_CLEAR;
   if (ff->dual_source_blending)
      dw4 |= GEN7_PS_DW4_DUAL_SOURCE_BLEND;
   if (info->rt_resolve_enable)
      dw4 |= GEN7_PS_DW4_RT_RESOLVE;
   if (ilo_dev_gen(dev) >= ILO_GEN(7.5) && ff->has_uav)
      dw4 |= GEN75_PS_DW4_ACCESS_UAV;

   dw5 = ff->grf_starts[0] << GEN7_PS_DW5_URB_GRF_START0__SHIFT |
         ff->grf_starts[1] << GEN7_PS_DW5_URB_GRF_START1__SHIFT |
         ff->grf_starts[2] << GEN7_PS_DW5_URB_GRF_START2__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(ps->ps) >= 8);
   ps->ps[2] = dw2;
   ps->ps[3] = dw3;
   ps->ps[4] = dw4;
   ps->ps[5] = dw5;
   ps->ps[6] = ff->kernel_offsets[1];
   ps->ps[7] = ff->kernel_offsets[2];

   return true;
}

static bool
ps_set_gen8_3DSTATE_PS(struct ilo_state_ps *ps,
                       const struct ilo_dev *dev,
                       const struct ilo_state_ps_info *info,
                       const struct pixel_ff *ff)
{
   const struct ilo_state_ps_io_info *io = &info->io;
   uint32_t dw3, dw4, dw6, dw7;

   ILO_DEV_ASSERT(dev, 8, 8);

   dw3 = ff->sampler_count << GEN6_THREADDISP_SAMPLER_COUNT__SHIFT |
         ff->surface_count << GEN6_THREADDISP_BINDING_TABLE_SIZE__SHIFT;

   if (false)
      dw3 |= GEN6_THREADDISP_FP_MODE_ALT;

   dw4 = ff->scratch_space << GEN6_THREADSCRATCH_SPACE_PER_THREAD__SHIFT;

   dw6 = ff->thread_count << GEN8_PS_DW6_MAX_THREADS__SHIFT |
         io->posoffset << GEN8_PS_DW6_POSOFFSET__SHIFT |
         ff->dispatch_modes << GEN8_PS_DW6_DISPATCH_MODE__SHIFT;

   if (ff->pcb_enable)
      dw6 |= GEN8_PS_DW6_PUSH_CONSTANT_ENABLE;

   if (info->rt_clear_enable)
      dw6 |= GEN8_PS_DW6_RT_FAST_CLEAR;
   if (info->rt_resolve_enable)
      dw6 |= GEN8_PS_DW6_RT_RESOLVE;

   dw7 = ff->grf_starts[0] << GEN8_PS_DW7_URB_GRF_START0__SHIFT |
         ff->grf_starts[1] << GEN8_PS_DW7_URB_GRF_START1__SHIFT |
         ff->grf_starts[2] << GEN8_PS_DW7_URB_GRF_START2__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(ps->ps) >= 6);
   ps->ps[0] = dw3;
   ps->ps[1] = dw4;
   ps->ps[2] = dw6;
   ps->ps[3] = dw7;
   ps->ps[4] = ff->kernel_offsets[1];
   ps->ps[5] = ff->kernel_offsets[2];

   return true;
}

static bool
ps_set_gen8_3DSTATE_PS_EXTRA(struct ilo_state_ps *ps,
                             const struct ilo_dev *dev,
                             const struct ilo_state_ps_info *info,
                             const struct pixel_ff *ff)
{
   const struct ilo_state_ps_io_info *io = &info->io;
   uint32_t dw1;

   ILO_DEV_ASSERT(dev, 8, 8);

   dw1 = io->pscdepth << GEN8_PSX_DW1_PSCDEPTH__SHIFT;

   if (info->valid_kernels)
      dw1 |= GEN8_PSX_DW1_VALID;
   if (!io->has_rt_write)
      dw1 |= GEN8_PSX_DW1_UAV_ONLY;
   if (io->write_omask)
      dw1 |= GEN8_PSX_DW1_COMPUTE_OMASK;
   if (io->write_pixel_mask)
      dw1 |= GEN8_PSX_DW1_KILL_PIXEL;

   if (io->use_z)
      dw1 |= GEN8_PSX_DW1_USE_DEPTH;
   if (io->use_w)
      dw1 |= GEN8_PSX_DW1_USE_W;
   if (io->attr_count)
      dw1 |= GEN8_PSX_DW1_ATTR_ENABLE;

   if (info->per_sample_dispatch)
      dw1 |= GEN8_PSX_DW1_PER_SAMPLE;
   if (ff->has_uav)
      dw1 |= GEN8_PSX_DW1_ACCESS_UAV;
   if (io->use_coverage_mask)
      dw1 |= GEN8_PSX_DW1_USE_COVERAGE_MASK;

   /*
    * From the Broadwell PRM, volume 2b, page 151:
    *
    *     "When this bit (Pixel Shader Valid) clear the rest of this command
    *      should also be clear.
    */
   if (!info->valid_kernels)
      dw1 = 0;

   STATIC_ASSERT(ARRAY_SIZE(ps->ps) >= 5);
   ps->ps[4] = dw1;

   return true;
}

bool
ilo_state_ps_init(struct ilo_state_ps *ps,
                  const struct ilo_dev *dev,
                  const struct ilo_state_ps_info *info)
{
   struct pixel_ff ff;
   bool ret = true;

   assert(ilo_is_zeroed(ps, sizeof(*ps)));

   ret &= ps_get_gen6_ff(dev, info, &ff);

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      ret &= ps_set_gen8_3DSTATE_PS(ps, dev, info, &ff);
      ret &= ps_set_gen8_3DSTATE_PS_EXTRA(ps, dev, info, &ff);
   } else if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      ret &= ps_set_gen7_3dstate_wm(ps, dev, info, &ff);
      ret &= ps_set_gen7_3DSTATE_PS(ps, dev, info, &ff);
   } else {
      ret &= ps_set_gen6_3dstate_wm(ps, dev, info, &ff);
   }

   /* save conditions */
   ps->conds = ff.conds;

   assert(ret);

   return ret;
}

bool
ilo_state_ps_init_disabled(struct ilo_state_ps *ps,
                           const struct ilo_dev *dev)
{
   struct ilo_state_ps_info info;

   memset(&info, 0, sizeof(info));

   return ilo_state_ps_init(ps, dev, &info);
}

bool
ilo_state_ps_set_params(struct ilo_state_ps *ps,
                        const struct ilo_dev *dev,
                        const struct ilo_state_ps_params_info *params)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /* modify sample mask */
   if (ilo_dev_gen(dev) == ILO_GEN(7.5)) {
      ps->ps[4] = (ps->ps[4] & ~GEN75_PS_DW4_SAMPLE_MASK__MASK) |
         (params->sample_mask & 0xff) << GEN75_PS_DW4_SAMPLE_MASK__SHIFT;
   }

   /* modify dispatch enable, pixel kill, and dual source blending */
   if (ilo_dev_gen(dev) < ILO_GEN(8)) {
      if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
         if (ps_params_get_gen6_dispatch_enable(dev, params, &ps->conds))
            ps->ps[0] |= GEN7_WM_DW1_PS_DISPATCH_ENABLE;
         else
            ps->ps[0] &= ~GEN7_WM_DW1_PS_DISPATCH_ENABLE;

         if (ps_params_get_gen6_kill_pixel(dev, params, &ps->conds))
            ps->ps[0] |= GEN7_WM_DW1_PS_KILL_PIXEL;
         else
            ps->ps[0] &= ~GEN7_WM_DW1_PS_KILL_PIXEL;

         if (params->dual_source_blending)
            ps->ps[4] |= GEN7_PS_DW4_DUAL_SOURCE_BLEND;
         else
            ps->ps[4] &= ~GEN7_PS_DW4_DUAL_SOURCE_BLEND;
      } else {
         if (ps_params_get_gen6_dispatch_enable(dev, params, &ps->conds))
            ps->ps[3] |= GEN6_WM_DW5_PS_DISPATCH_ENABLE;
         else
            ps->ps[3] &= ~GEN6_WM_DW5_PS_DISPATCH_ENABLE;

         if (ps_params_get_gen6_kill_pixel(dev, params, &ps->conds))
            ps->ps[3] |= GEN6_WM_DW5_PS_KILL_PIXEL;
         else
            ps->ps[3] &= ~GEN6_WM_DW5_PS_KILL_PIXEL;

         if (params->dual_source_blending)
            ps->ps[3] |= GEN6_WM_DW5_PS_DUAL_SOURCE_BLEND;
         else
            ps->ps[3] &= ~GEN6_WM_DW5_PS_DUAL_SOURCE_BLEND;
      }
   }

   return true;
}
