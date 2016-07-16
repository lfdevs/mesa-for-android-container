/*
 * Copyright (c) 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "brw_context.h"
#include "brw_defines.h"
#include "brw_state.h"
#include "intel_batchbuffer.h"

/**
 * Chunk of L3 cache reserved for some specific purpose.
 */
enum brw_l3_partition {
   /** Shared local memory. */
   L3P_SLM = 0,
   /** Unified return buffer. */
   L3P_URB,
   /** Union of DC and RO. */
   L3P_ALL,
   /** Data cluster RW partition. */
   L3P_DC,
   /** Union of IS, C and T. */
   L3P_RO,
   /** Instruction and state cache. */
   L3P_IS,
   /** Constant cache. */
   L3P_C,
   /** Texture cache. */
   L3P_T,
   /** Number of supported L3 partitions. */
   NUM_L3P
};

/**
 * L3 configuration represented as the number of ways allocated for each
 * partition.  \sa get_l3_way_size().
 */
struct brw_l3_config {
   unsigned n[NUM_L3P];
};

/**
 * IVB/HSW validated L3 configurations.  The first entry will be used as
 * default by gen7_restore_default_l3_config(), otherwise the ordering is
 * unimportant.
 */
static const struct brw_l3_config ivb_l3_configs[] = {
   /* SLM URB ALL DC  RO  IS   C   T */
   {{  0, 32,  0,  0, 32,  0,  0,  0 }},
   {{  0, 32,  0, 16, 16,  0,  0,  0 }},
   {{  0, 32,  0,  4,  0,  8,  4, 16 }},
   {{  0, 28,  0,  8,  0,  8,  4, 16 }},
   {{  0, 28,  0, 16,  0,  8,  4,  8 }},
   {{  0, 28,  0,  8,  0, 16,  4,  8 }},
   {{  0, 28,  0,  0,  0, 16,  4, 16 }},
   {{  0, 32,  0,  0,  0, 16,  0, 16 }},
   {{  0, 28,  0,  4, 32,  0,  0,  0 }},
   {{ 16, 16,  0, 16, 16,  0,  0,  0 }},
   {{ 16, 16,  0,  8,  0,  8,  8,  8 }},
   {{ 16, 16,  0,  4,  0,  8,  4, 16 }},
   {{ 16, 16,  0,  4,  0, 16,  4,  8 }},
   {{ 16, 16,  0,  0, 32,  0,  0,  0 }},
   {{ 0 }}
};

/**
 * VLV validated L3 configurations.  \sa ivb_l3_configs.
 */
static const struct brw_l3_config vlv_l3_configs[] = {
   /* SLM URB ALL DC  RO  IS   C   T */
   {{  0, 64,  0,  0, 32,  0,  0,  0 }},
   {{  0, 80,  0,  0, 16,  0,  0,  0 }},
   {{  0, 80,  0,  8,  8,  0,  0,  0 }},
   {{  0, 64,  0, 16, 16,  0,  0,  0 }},
   {{  0, 60,  0,  4, 32,  0,  0,  0 }},
   {{ 32, 32,  0, 16, 16,  0,  0,  0 }},
   {{ 32, 40,  0,  8, 16,  0,  0,  0 }},
   {{ 32, 40,  0, 16,  8,  0,  0,  0 }},
   {{ 0 }}
};

/**
 * BDW validated L3 configurations.  \sa ivb_l3_configs.
 */
static const struct brw_l3_config bdw_l3_configs[] = {
   /* SLM URB ALL DC  RO  IS   C   T */
   {{  0, 48, 48,  0,  0,  0,  0,  0 }},
   {{  0, 48,  0, 16, 32,  0,  0,  0 }},
   {{  0, 32,  0, 16, 48,  0,  0,  0 }},
   {{  0, 32,  0,  0, 64,  0,  0,  0 }},
   {{  0, 32, 64,  0,  0,  0,  0,  0 }},
   {{ 24, 16, 48,  0,  0,  0,  0,  0 }},
   {{ 24, 16,  0, 16, 32,  0,  0,  0 }},
   {{ 24, 16,  0, 32, 16,  0,  0,  0 }},
   {{ 0 }}
};

/**
 * CHV/SKL validated L3 configurations.  \sa ivb_l3_configs.
 */
static const struct brw_l3_config chv_l3_configs[] = {
   /* SLM URB ALL DC  RO  IS   C   T */
   {{  0, 48, 48,  0,  0,  0,  0,  0 }},
   {{  0, 48,  0, 16, 32,  0,  0,  0 }},
   {{  0, 32,  0, 16, 48,  0,  0,  0 }},
   {{  0, 32,  0,  0, 64,  0,  0,  0 }},
   {{  0, 32, 64,  0,  0,  0,  0,  0 }},
   {{ 32, 16, 48,  0,  0,  0,  0,  0 }},
   {{ 32, 16,  0, 16, 32,  0,  0,  0 }},
   {{ 32, 16,  0, 32, 16,  0,  0,  0 }},
   {{ 0 }}
};

/**
 * Return a zero-terminated array of validated L3 configurations for the
 * specified device.
 */
static const struct brw_l3_config *
get_l3_configs(const struct brw_device_info *devinfo)
{
   switch (devinfo->gen) {
   case 7:
      return (devinfo->is_baytrail ? vlv_l3_configs : ivb_l3_configs);

   case 8:
      return (devinfo->is_cherryview ? chv_l3_configs : bdw_l3_configs);

   case 9:
      return chv_l3_configs;

   default:
      unreachable("Not implemented");
   }
}

/**
 * Return the size of an L3 way in KB.
 */
static unsigned
get_l3_way_size(const struct brw_device_info *devinfo)
{
   if (devinfo->is_baytrail)
      return 2;

   else if (devinfo->is_cherryview || devinfo->gt == 1)
      return 4;

   else
      return 8 * devinfo->num_slices;
}

/**
 * L3 configuration represented as a vector of weights giving the desired
 * relative size of each partition.  The scale is arbitrary, only the ratios
 * between weights will have an influence on the selection of the closest L3
 * configuration.
 */
struct brw_l3_weights {
   float w[NUM_L3P];
};

/**
 * L1-normalize a vector of L3 partition weights.
 */
static struct brw_l3_weights
norm_l3_weights(struct brw_l3_weights w)
{
   float sz = 0;

   for (unsigned i = 0; i < NUM_L3P; i++)
      sz += w.w[i];

   for (unsigned i = 0; i < NUM_L3P; i++)
      w.w[i] /= sz;

   return w;
}

/**
 * Get the relative partition weights of the specified L3 configuration.
 */
static struct brw_l3_weights
get_config_l3_weights(const struct brw_l3_config *cfg)
{
   if (cfg) {
      struct brw_l3_weights w;

      for (unsigned i = 0; i < NUM_L3P; i++)
         w.w[i] = cfg->n[i];

      return norm_l3_weights(w);
   } else {
      const struct brw_l3_weights w = { { 0 } };
      return w;
   }
}

/**
 * Distance between two L3 configurations represented as vectors of weights.
 * Usually just the L1 metric except when the two configurations are
 * considered incompatible in which case the distance will be infinite.  Note
 * that the compatibility condition is asymmetric -- They will be considered
 * incompatible whenever the reference configuration \p w0 requires SLM, DC,
 * or URB but \p w1 doesn't provide it.
 */
static float
diff_l3_weights(struct brw_l3_weights w0, struct brw_l3_weights w1)
{
   if ((w0.w[L3P_SLM] && !w1.w[L3P_SLM]) ||
       (w0.w[L3P_DC] && !w1.w[L3P_DC] && !w1.w[L3P_ALL]) ||
       (w0.w[L3P_URB] && !w1.w[L3P_URB])) {
      return HUGE_VALF;

   } else {
      float dw = 0;

      for (unsigned i = 0; i < NUM_L3P; i++)
         dw += fabs(w0.w[i] - w1.w[i]);

      return dw;
   }
}

/**
 * Return the closest validated L3 configuration for the specified device and
 * weight vector.
 */
static const struct brw_l3_config *
get_l3_config(const struct brw_device_info *devinfo, struct brw_l3_weights w0)
{
   const struct brw_l3_config *const cfgs = get_l3_configs(devinfo);
   const struct brw_l3_config *cfg_best = NULL;
   float dw_best = HUGE_VALF;

   for (const struct brw_l3_config *cfg = cfgs; cfg->n[L3P_URB]; cfg++) {
      const float dw = diff_l3_weights(w0, get_config_l3_weights(cfg));

      if (dw < dw_best) {
         cfg_best = cfg;
         dw_best = dw;
      }
   }

   return cfg_best;
}

/**
 * Return a reasonable default L3 configuration for the specified device based
 * on whether SLM and DC are required.  In the non-SLM non-DC case the result
 * is intended to approximately resemble the hardware defaults.
 */
static struct brw_l3_weights
get_default_l3_weights(const struct brw_device_info *devinfo,
                       bool needs_dc, bool needs_slm)
{
   struct brw_l3_weights w = {{ 0 }};

   w.w[L3P_SLM] = needs_slm;
   w.w[L3P_URB] = 1.0;

   if (devinfo->gen >= 8) {
      w.w[L3P_ALL] = 1.0;
   } else {
      w.w[L3P_DC] = needs_dc ? 0.1 : 0;
      w.w[L3P_RO] = devinfo->is_baytrail ? 0.5 : 1.0;
   }

   return norm_l3_weights(w);
}

/**
 * Calculate the desired L3 partitioning based on the current state of the
 * pipeline.  For now this simply returns the conservative defaults calculated
 * by get_default_l3_weights(), but we could probably do better by gathering
 * more statistics from the pipeline state (e.g. guess of expected URB usage
 * and bound surfaces), or by using feed-back from performance counters.
 */
static struct brw_l3_weights
get_pipeline_state_l3_weights(const struct brw_context *brw)
{
   const struct brw_stage_state *stage_states[] = {
      [MESA_SHADER_VERTEX] = &brw->vs.base,
      [MESA_SHADER_TESS_CTRL] = &brw->tcs.base,
      [MESA_SHADER_TESS_EVAL] = &brw->tes.base,
      [MESA_SHADER_GEOMETRY] = &brw->gs.base,
      [MESA_SHADER_FRAGMENT] = &brw->wm.base,
      [MESA_SHADER_COMPUTE] = &brw->cs.base
   };
   bool needs_dc = false, needs_slm = false;

   for (unsigned i = 0; i < ARRAY_SIZE(stage_states); i++) {
      const struct gl_shader_program *prog =
         brw->ctx._Shader->CurrentProgram[stage_states[i]->stage];
      const struct brw_stage_prog_data *prog_data = stage_states[i]->prog_data;

      needs_dc |= (prog && prog->NumAtomicBuffers) ||
         (prog_data && (prog_data->total_scratch || prog_data->nr_image_params));
      needs_slm |= prog_data && prog_data->total_shared;
   }

   return get_default_l3_weights(brw->intelScreen->devinfo,
                                 needs_dc, needs_slm);
}

/**
 * Program the hardware to use the specified L3 configuration.
 */
static void
setup_l3_config(struct brw_context *brw, const struct brw_l3_config *cfg)
{
   const bool has_dc = cfg->n[L3P_DC] || cfg->n[L3P_ALL];
   const bool has_is = cfg->n[L3P_IS] || cfg->n[L3P_RO] || cfg->n[L3P_ALL];
   const bool has_c = cfg->n[L3P_C] || cfg->n[L3P_RO] || cfg->n[L3P_ALL];
   const bool has_t = cfg->n[L3P_T] || cfg->n[L3P_RO] || cfg->n[L3P_ALL];
   const bool has_slm = cfg->n[L3P_SLM];

   /* According to the hardware docs, the L3 partitioning can only be changed
    * while the pipeline is completely drained and the caches are flushed,
    * which involves a first PIPE_CONTROL flush which stalls the pipeline...
    */
   brw_emit_pipe_control_flush(brw,
                               PIPE_CONTROL_DATA_CACHE_FLUSH |
                               PIPE_CONTROL_NO_WRITE |
                               PIPE_CONTROL_CS_STALL);

   /* ...followed by a second pipelined PIPE_CONTROL that initiates
    * invalidation of the relevant caches.  Note that because RO invalidation
    * happens at the top of the pipeline (i.e. right away as the PIPE_CONTROL
    * command is processed by the CS) we cannot combine it with the previous
    * stalling flush as the hardware documentation suggests, because that
    * would cause the CS to stall on previous rendering *after* RO
    * invalidation and wouldn't prevent the RO caches from being polluted by
    * concurrent rendering before the stall completes.  This intentionally
    * doesn't implement the SKL+ hardware workaround suggesting to enable CS
    * stall on PIPE_CONTROLs with the texture cache invalidation bit set for
    * GPGPU workloads because the previous and subsequent PIPE_CONTROLs
    * already guarantee that there is no concurrent GPGPU kernel execution
    * (see SKL HSD 2132585).
    */
   brw_emit_pipe_control_flush(brw,
                               PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE |
                               PIPE_CONTROL_CONST_CACHE_INVALIDATE |
                               PIPE_CONTROL_INSTRUCTION_INVALIDATE |
                               PIPE_CONTROL_STATE_CACHE_INVALIDATE |
                               PIPE_CONTROL_NO_WRITE);

   /* Now send a third stalling flush to make sure that invalidation is
    * complete when the L3 configuration registers are modified.
    */
   brw_emit_pipe_control_flush(brw,
                               PIPE_CONTROL_DATA_CACHE_FLUSH |
                               PIPE_CONTROL_NO_WRITE |
                               PIPE_CONTROL_CS_STALL);

   if (brw->gen >= 8) {
      assert(!cfg->n[L3P_IS] && !cfg->n[L3P_C] && !cfg->n[L3P_T]);

      BEGIN_BATCH(3);
      OUT_BATCH(MI_LOAD_REGISTER_IMM | (3 - 2));

      /* Set up the L3 partitioning. */
      OUT_BATCH(GEN8_L3CNTLREG);
      OUT_BATCH((has_slm ? GEN8_L3CNTLREG_SLM_ENABLE : 0) |
                SET_FIELD(cfg->n[L3P_URB], GEN8_L3CNTLREG_URB_ALLOC) |
                SET_FIELD(cfg->n[L3P_RO], GEN8_L3CNTLREG_RO_ALLOC) |
                SET_FIELD(cfg->n[L3P_DC], GEN8_L3CNTLREG_DC_ALLOC) |
                SET_FIELD(cfg->n[L3P_ALL], GEN8_L3CNTLREG_ALL_ALLOC));

      ADVANCE_BATCH();

   } else {
      assert(!cfg->n[L3P_ALL]);

      /* When enabled SLM only uses a portion of the L3 on half of the banks,
       * the matching space on the remaining banks has to be allocated to a
       * client (URB for all validated configurations) set to the
       * lower-bandwidth 2-bank address hashing mode.
       */
      const bool urb_low_bw = has_slm && !brw->is_baytrail;
      assert(!urb_low_bw || cfg->n[L3P_URB] == cfg->n[L3P_SLM]);

      /* Minimum number of ways that can be allocated to the URB. */
      const unsigned n0_urb = (brw->is_baytrail ? 32 : 0);
      assert(cfg->n[L3P_URB] >= n0_urb);

      BEGIN_BATCH(7);
      OUT_BATCH(MI_LOAD_REGISTER_IMM | (7 - 2));

      /* Demote any clients with no ways assigned to LLC. */
      OUT_BATCH(GEN7_L3SQCREG1);
      OUT_BATCH((brw->is_haswell ? HSW_L3SQCREG1_SQGHPCI_DEFAULT :
                 brw->is_baytrail ? VLV_L3SQCREG1_SQGHPCI_DEFAULT :
                 IVB_L3SQCREG1_SQGHPCI_DEFAULT) |
                (has_dc ? 0 : GEN7_L3SQCREG1_CONV_DC_UC) |
                (has_is ? 0 : GEN7_L3SQCREG1_CONV_IS_UC) |
                (has_c ? 0 : GEN7_L3SQCREG1_CONV_C_UC) |
                (has_t ? 0 : GEN7_L3SQCREG1_CONV_T_UC));

      /* Set up the L3 partitioning. */
      OUT_BATCH(GEN7_L3CNTLREG2);
      OUT_BATCH((has_slm ? GEN7_L3CNTLREG2_SLM_ENABLE : 0) |
                SET_FIELD(cfg->n[L3P_URB] - n0_urb, GEN7_L3CNTLREG2_URB_ALLOC) |
                (urb_low_bw ? GEN7_L3CNTLREG2_URB_LOW_BW : 0) |
                SET_FIELD(cfg->n[L3P_ALL], GEN7_L3CNTLREG2_ALL_ALLOC) |
                SET_FIELD(cfg->n[L3P_RO], GEN7_L3CNTLREG2_RO_ALLOC) |
                SET_FIELD(cfg->n[L3P_DC], GEN7_L3CNTLREG2_DC_ALLOC));
      OUT_BATCH(GEN7_L3CNTLREG3);
      OUT_BATCH(SET_FIELD(cfg->n[L3P_IS], GEN7_L3CNTLREG3_IS_ALLOC) |
                SET_FIELD(cfg->n[L3P_C], GEN7_L3CNTLREG3_C_ALLOC) |
                SET_FIELD(cfg->n[L3P_T], GEN7_L3CNTLREG3_T_ALLOC));

      ADVANCE_BATCH();

      if (brw->is_haswell && brw->intelScreen->cmd_parser_version >= 4) {
         /* Enable L3 atomics on HSW if we have a DC partition, otherwise keep
          * them disabled to avoid crashing the system hard.
          */
         BEGIN_BATCH(5);
         OUT_BATCH(MI_LOAD_REGISTER_IMM | (5 - 2));
         OUT_BATCH(HSW_SCRATCH1);
         OUT_BATCH(has_dc ? 0 : HSW_SCRATCH1_L3_ATOMIC_DISABLE);
         OUT_BATCH(HSW_ROW_CHICKEN3);
         OUT_BATCH(REG_MASK(HSW_ROW_CHICKEN3_L3_ATOMIC_DISABLE) |
                   (has_dc ? 0 : HSW_ROW_CHICKEN3_L3_ATOMIC_DISABLE));
         ADVANCE_BATCH();
      }
   }
}

/**
 * Return the unit brw_context::urb::size is expressed in, in KB.  \sa
 * brw_device_info::urb::size.
 */
static unsigned
get_urb_size_scale(const struct brw_device_info *devinfo)
{
   return (devinfo->gen >= 8 ? devinfo->num_slices : 1);
}

/**
 * Update the URB size in the context state for the specified L3
 * configuration.
 */
static void
update_urb_size(struct brw_context *brw, const struct brw_l3_config *cfg)
{
   const struct brw_device_info *devinfo = brw->intelScreen->devinfo;
   /* From the SKL "L3 Allocation and Programming" documentation:
    *
    * "URB is limited to 1008KB due to programming restrictions.  This is not
    * a restriction of the L3 implementation, but of the FF and other clients.
    * Therefore, in a GT4 implementation it is possible for the programmed
    * allocation of the L3 data array to provide 3*384KB=1152KB for URB, but
    * only 1008KB of this will be used."
    */
   const unsigned max = (devinfo->gen == 9 ? 1008 : ~0);
   const unsigned sz =
      MIN2(max, cfg->n[L3P_URB] * get_l3_way_size(devinfo)) /
      get_urb_size_scale(devinfo);

   if (brw->urb.size != sz) {
      brw->urb.size = sz;
      brw->ctx.NewDriverState |= BRW_NEW_URB_SIZE;
   }
}

/**
 * Print out the specified L3 configuration.
 */
static void
dump_l3_config(const struct brw_l3_config *cfg)
{
   fprintf(stderr, "SLM=%d URB=%d ALL=%d DC=%d RO=%d IS=%d C=%d T=%d\n",
           cfg->n[L3P_SLM], cfg->n[L3P_URB], cfg->n[L3P_ALL],
           cfg->n[L3P_DC], cfg->n[L3P_RO],
           cfg->n[L3P_IS], cfg->n[L3P_C], cfg->n[L3P_T]);
}

static void
emit_l3_state(struct brw_context *brw)
{
   const struct brw_l3_weights w = get_pipeline_state_l3_weights(brw);
   const float dw = diff_l3_weights(w, get_config_l3_weights(brw->l3.config));
   /* The distance between any two compatible weight vectors cannot exceed two
    * due to the triangle inequality.
    */
   const float large_dw_threshold = 2.0;
   /* Somewhat arbitrary, simply makes sure that there will be no repeated
    * transitions to the same L3 configuration, could probably do better here.
    */
   const float small_dw_threshold = 0.5;
   /* If we're emitting a new batch the caches should already be clean and the
    * transition should be relatively cheap, so it shouldn't hurt much to use
    * the smaller threshold.  Otherwise use the larger threshold so that we
    * only reprogram the L3 mid-batch if the most recently programmed
    * configuration is incompatible with the current pipeline state.
    */
   const float dw_threshold = (brw->ctx.NewDriverState & BRW_NEW_BATCH ?
                               small_dw_threshold : large_dw_threshold);

   if (dw > dw_threshold && brw->can_do_pipelined_register_writes) {
      const struct brw_l3_config *const cfg =
         get_l3_config(brw->intelScreen->devinfo, w);

      setup_l3_config(brw, cfg);
      update_urb_size(brw, cfg);
      brw->l3.config = cfg;

      if (unlikely(INTEL_DEBUG & DEBUG_L3)) {
         fprintf(stderr, "L3 config transition (%f > %f): ", dw, dw_threshold);
         dump_l3_config(cfg);
      }
   }
}

const struct brw_tracked_state gen7_l3_state = {
   .dirty = {
      .mesa = 0,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_CS_PROG_DATA |
             BRW_NEW_FS_PROG_DATA |
             BRW_NEW_GS_PROG_DATA |
             BRW_NEW_VS_PROG_DATA,
   },
   .emit = emit_l3_state
};

/**
 * Hack to restore the default L3 configuration.
 *
 * This will be called at the end of every batch in order to reset the L3
 * configuration to the default values for the time being until the kernel is
 * fixed.  Until kernel commit 6702cf16e0ba8b0129f5aa1b6609d4e9c70bc13b
 * (included in v4.1) we would set the MI_RESTORE_INHIBIT bit when submitting
 * batch buffers for the default context used by the DDX, which meant that any
 * context state changed by the GL would leak into the DDX, the assumption
 * being that the DDX would initialize any state it cares about manually.  The
 * DDX is however not careful enough to program an L3 configuration
 * explicitly, and it makes assumptions about it (URB size) which won't hold
 * and cause it to misrender if we let our L3 set-up to leak into the DDX.
 *
 * Since v4.1 of the Linux kernel the default context is saved and restored
 * normally, so it's far less likely for our L3 programming to interfere with
 * other contexts -- In fact restoring the default L3 configuration at the end
 * of the batch will be redundant most of the time.  A kind of state leak is
 * still possible though if the context making assumptions about L3 state is
 * created immediately after our context was active (e.g. without the DDX
 * default context being scheduled in between) because at present the DRM
 * doesn't fully initialize the contents of newly created contexts and instead
 * sets the MI_RESTORE_INHIBIT flag causing it to inherit the state from the
 * last active context.
 *
 * It's possible to realize such a scenario if, say, an X server (or a GL
 * application using an outdated non-L3-aware Mesa version) is started while
 * another GL application is running and happens to have modified the L3
 * configuration, or if no X server is running at all and a GL application
 * using a non-L3-aware Mesa version is started after another GL application
 * ran and modified the L3 configuration -- The latter situation can actually
 * be reproduced easily on IVB in our CI system.
 */
void
gen7_restore_default_l3_config(struct brw_context *brw)
{
   const struct brw_device_info *devinfo = brw->intelScreen->devinfo;
   /* For efficiency assume that the first entry of the array matches the
    * default configuration.
    */
   const struct brw_l3_config *const cfg = get_l3_configs(devinfo);
   assert(cfg == get_l3_config(devinfo,
                               get_default_l3_weights(devinfo, false, false)));

   if (cfg != brw->l3.config && brw->can_do_pipelined_register_writes) {
      setup_l3_config(brw, cfg);
      update_urb_size(brw, cfg);
      brw->l3.config = cfg;
   }
}
