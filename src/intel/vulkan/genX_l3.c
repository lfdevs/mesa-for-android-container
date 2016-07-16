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

#include "anv_private.h"

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"

/**
 * Chunk of L3 cache reserved for some specific purpose.
 */
enum anv_l3_partition {
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
struct anv_l3_config {
   unsigned n[NUM_L3P];
};

#if GEN_GEN == 7

/**
 * IVB/HSW validated L3 configurations.  The first entry will be used as
 * default by gen7_restore_default_l3_config(), otherwise the ordering is
 * unimportant.
 */
static const struct anv_l3_config ivb_l3_configs[] = {
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

#endif

#if GEN_GEN == 7 && !GEN_IS_HASWELL

/**
 * VLV validated L3 configurations.  \sa ivb_l3_configs.
 */
static const struct anv_l3_config vlv_l3_configs[] = {
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

#endif

#if GEN_GEN == 8

/**
 * BDW validated L3 configurations.  \sa ivb_l3_configs.
 */
static const struct anv_l3_config bdw_l3_configs[] = {
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

#endif

#if GEN_GEN == 8 || GEN_GEN == 9

/**
 * CHV/SKL validated L3 configurations.  \sa ivb_l3_configs.
 */
static const struct anv_l3_config chv_l3_configs[] = {
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

#endif

/**
 * Return a zero-terminated array of validated L3 configurations for the
 * specified device.
 */
static inline const struct anv_l3_config *
get_l3_configs(const struct brw_device_info *devinfo)
{
   assert(devinfo->gen == GEN_GEN);
#if GEN_IS_HASWELL
   return ivb_l3_configs;
#elif GEN_GEN == 7
   return (devinfo->is_baytrail ? vlv_l3_configs : ivb_l3_configs);
#elif GEN_GEN == 8
   return (devinfo->is_cherryview ? chv_l3_configs : bdw_l3_configs);
#elif GEN_GEN == 9
   return chv_l3_configs;
#else
#error GEN not supported
#endif
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
struct anv_l3_weights {
   float w[NUM_L3P];
};

/**
 * L1-normalize a vector of L3 partition weights.
 */
static struct anv_l3_weights
norm_l3_weights(struct anv_l3_weights w)
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
static struct anv_l3_weights
get_config_l3_weights(const struct anv_l3_config *cfg)
{
   if (cfg) {
      struct anv_l3_weights w;

      for (unsigned i = 0; i < NUM_L3P; i++)
         w.w[i] = cfg->n[i];

      return norm_l3_weights(w);
   } else {
      const struct anv_l3_weights w = { { 0 } };
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
diff_l3_weights(struct anv_l3_weights w0, struct anv_l3_weights w1)
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
static const struct anv_l3_config *
get_l3_config(const struct brw_device_info *devinfo, struct anv_l3_weights w0)
{
   const struct anv_l3_config *const cfgs = get_l3_configs(devinfo);
   const struct anv_l3_config *cfg_best = NULL;
   float dw_best = HUGE_VALF;

   for (const struct anv_l3_config *cfg = cfgs; cfg->n[L3P_URB]; cfg++) {
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
static struct anv_l3_weights
get_default_l3_weights(const struct brw_device_info *devinfo,
                       bool needs_dc, bool needs_slm)
{
   struct anv_l3_weights w = {{ 0 }};

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
static struct anv_l3_weights
get_pipeline_state_l3_weights(const struct anv_pipeline *pipeline)
{
   bool needs_dc = false, needs_slm = false;

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      const struct brw_stage_prog_data *prog_data = pipeline->prog_data[i];

      needs_dc |= pipeline->needs_data_cache;
      needs_slm |= prog_data && prog_data->total_shared;
   }

   return get_default_l3_weights(&pipeline->device->info,
                                 needs_dc, needs_slm);
}

#define emit_lri(batch, reg, imm)                               \
   anv_batch_emit(batch, GENX(MI_LOAD_REGISTER_IMM), lri) {     \
      lri.RegisterOffset = __anv_reg_num(reg);                  \
      lri.DataDWord = imm;                                      \
   }

#define IVB_L3SQCREG1_SQGHPCI_DEFAULT     0x00730000
#define VLV_L3SQCREG1_SQGHPCI_DEFAULT     0x00d30000
#define HSW_L3SQCREG1_SQGHPCI_DEFAULT     0x00610000

/**
 * Program the hardware to use the specified L3 configuration.
 */
static void
setup_l3_config(struct anv_cmd_buffer *cmd_buffer/*, struct brw_context *brw*/,
                const struct anv_l3_config *cfg)
{
   const bool has_slm = cfg->n[L3P_SLM];

   /* According to the hardware docs, the L3 partitioning can only be changed
    * while the pipeline is completely drained and the caches are flushed,
    * which involves a first PIPE_CONTROL flush which stalls the pipeline...
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DCFlushEnable = true;
      pc.PostSyncOperation = NoWrite;
      pc.CommandStreamerStallEnable = true;
   }

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
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.TextureCacheInvalidationEnable = true;
      pc.ConstantCacheInvalidationEnable = true;
      pc.InstructionCacheInvalidateEnable = true;
      pc.StateCacheInvalidationEnable = true;
      pc.PostSyncOperation = NoWrite;
   }

   /* Now send a third stalling flush to make sure that invalidation is
    * complete when the L3 configuration registers are modified.
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DCFlushEnable = true;
      pc.PostSyncOperation = NoWrite;
      pc.CommandStreamerStallEnable = true;
   }

#if GEN_GEN >= 8

   assert(!cfg->n[L3P_IS] && !cfg->n[L3P_C] && !cfg->n[L3P_T]);

   uint32_t l3cr;
   anv_pack_struct(&l3cr, GENX(L3CNTLREG),
                   .SLMEnable = has_slm,
                   .URBAllocation = cfg->n[L3P_URB],
                   .ROAllocation = cfg->n[L3P_RO],
                   .DCAllocation = cfg->n[L3P_DC],
                   .AllAllocation = cfg->n[L3P_ALL]);

   /* Set up the L3 partitioning. */
   emit_lri(&cmd_buffer->batch, GENX(L3CNTLREG), l3cr);

#else

   const bool has_dc = cfg->n[L3P_DC] || cfg->n[L3P_ALL];
   const bool has_is = cfg->n[L3P_IS] || cfg->n[L3P_RO] || cfg->n[L3P_ALL];
   const bool has_c = cfg->n[L3P_C] || cfg->n[L3P_RO] || cfg->n[L3P_ALL];
   const bool has_t = cfg->n[L3P_T] || cfg->n[L3P_RO] || cfg->n[L3P_ALL];

   assert(!cfg->n[L3P_ALL]);

   /* When enabled SLM only uses a portion of the L3 on half of the banks,
    * the matching space on the remaining banks has to be allocated to a
    * client (URB for all validated configurations) set to the
    * lower-bandwidth 2-bank address hashing mode.
    */
   const struct brw_device_info *devinfo = &cmd_buffer->device->info;
   const bool urb_low_bw = has_slm && !devinfo->is_baytrail;
   assert(!urb_low_bw || cfg->n[L3P_URB] == cfg->n[L3P_SLM]);

   /* Minimum number of ways that can be allocated to the URB. */
   const unsigned n0_urb = (devinfo->is_baytrail ? 32 : 0);
   assert(cfg->n[L3P_URB] >= n0_urb);

   uint32_t l3sqcr1, l3cr2, l3cr3;
   anv_pack_struct(&l3sqcr1, GENX(L3SQCREG1),
                   .ConvertDC_UC = !has_dc,
                   .ConvertIS_UC = !has_is,
                   .ConvertC_UC = !has_c,
                   .ConvertT_UC = !has_t);
   l3sqcr1 |=
      GEN_IS_HASWELL ? HSW_L3SQCREG1_SQGHPCI_DEFAULT :
      devinfo->is_baytrail ? VLV_L3SQCREG1_SQGHPCI_DEFAULT :
      IVB_L3SQCREG1_SQGHPCI_DEFAULT;

   anv_pack_struct(&l3cr2, GENX(L3CNTLREG2),
                   .SLMEnable = has_slm,
                   .URBLowBandwidth = urb_low_bw,
                   .URBAllocation = cfg->n[L3P_URB],
#if !GEN_IS_HASWELL
                   .ALLAllocation = cfg->n[L3P_ALL],
#endif
                   .ROAllocation = cfg->n[L3P_RO],
                   .DCAllocation = cfg->n[L3P_DC]);

   anv_pack_struct(&l3cr3, GENX(L3CNTLREG3),
                   .ISAllocation = cfg->n[L3P_IS],
                   .ISLowBandwidth = 0,
                   .CAllocation = cfg->n[L3P_C],
                   .CLowBandwidth = 0,
                   .TAllocation = cfg->n[L3P_T],
                   .TLowBandwidth = 0);

   /* Set up the L3 partitioning. */
   emit_lri(&cmd_buffer->batch, GENX(L3SQCREG1), l3sqcr1);
   emit_lri(&cmd_buffer->batch, GENX(L3CNTLREG2), l3cr2);
   emit_lri(&cmd_buffer->batch, GENX(L3CNTLREG3), l3cr3);

#if GEN_IS_HASWELL
   if (cmd_buffer->device->instance->physicalDevice.cmd_parser_version >= 4) {
      /* Enable L3 atomics on HSW if we have a DC partition, otherwise keep
       * them disabled to avoid crashing the system hard.
       */
      uint32_t scratch1, chicken3;
      anv_pack_struct(&scratch1, GENX(SCRATCH1),
                      .L3AtomicDisable = !has_dc);
      anv_pack_struct(&chicken3, GENX(CHICKEN3),
                      .L3AtomicDisable = !has_dc);
      emit_lri(&cmd_buffer->batch, GENX(SCRATCH1), scratch1);
      emit_lri(&cmd_buffer->batch, GENX(CHICKEN3), chicken3);
   }
#endif

#endif

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

void
genX(setup_pipeline_l3_config)(struct anv_pipeline *pipeline)
{
   const struct anv_l3_weights w = get_pipeline_state_l3_weights(pipeline);
   const struct brw_device_info *devinfo = &pipeline->device->info;
   const struct anv_l3_config *const cfg = get_l3_config(devinfo, w);
   pipeline->urb.l3_config = cfg;

   unsigned sz = cfg->n[L3P_URB] * get_l3_way_size(devinfo);

#if GEN_GEN == 9
   /* From the SKL "L3 Allocation and Programming" documentation:
    *
    * "URB is limited to 1008KB due to programming restrictions.  This is not
    * a restriction of the L3 implementation, but of the FF and other clients.
    * Therefore, in a GT4 implementation it is possible for the programmed
    * allocation of the L3 data array to provide 3*384KB=1152KB for URB, but
    * only 1008KB of this will be used."
    */
   sz = MIN2(1008, sz);
#endif

   pipeline->urb.total_size = sz / get_urb_size_scale(devinfo);
}

/**
 * Print out the specified L3 configuration.
 */
static void
dump_l3_config(const struct anv_l3_config *cfg)
{
   fprintf(stderr, "SLM=%d URB=%d ALL=%d DC=%d RO=%d IS=%d C=%d T=%d\n",
           cfg->n[L3P_SLM], cfg->n[L3P_URB], cfg->n[L3P_ALL],
           cfg->n[L3P_DC], cfg->n[L3P_RO],
           cfg->n[L3P_IS], cfg->n[L3P_C], cfg->n[L3P_T]);
}

void
genX(cmd_buffer_config_l3)(struct anv_cmd_buffer *cmd_buffer,
                           const struct anv_pipeline *pipeline)
{
   struct anv_cmd_state *state = &cmd_buffer->state;
   const struct anv_l3_config *const cfg = pipeline->urb.l3_config;
   assert(cfg);
   if (cfg != state->current_l3_config) {
      setup_l3_config(cmd_buffer, cfg);
      state->current_l3_config = cfg;

      if (unlikely(INTEL_DEBUG & DEBUG_L3)) {
         fprintf(stderr, "L3 config transition: ");
         dump_l3_config(cfg);
      }
   }
}
