/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "perf/i915/intel_perf.h"

#include <sys/stat.h>

#include "common/intel_gem.h"
#include "common/i915/intel_gem.h"
#include "dev/intel_debug.h"
#include "dev/intel_device_info.h"
#include "intel_perf_common.h"
#include "perf/intel_perf.h"
#include "perf/intel_perf_private.h"
#include "util/compiler.h"

#include "drm-uapi/i915_drm.h"

#include "perf/intel_perf_private.h"

#define FILE_DEBUG_FLAG DEBUG_PERFMON

uint64_t i915_perf_get_oa_format(struct intel_perf_config *perf)
{
   if (perf->devinfo->verx10 <= 75)
      return I915_OA_FORMAT_A45_B8_C8;
   else if (perf->devinfo->verx10 <= 120)
      return I915_OA_FORMAT_A32u40_A4u32_B8_C8;
   else
      return I915_OA_FORMAT_A24u40_A14u32_B8_C8;
}

int
i915_perf_stream_open(struct intel_perf_config *perf_config, int drm_fd,
                      uint32_t ctx_id, uint64_t metrics_set_id,
                      uint64_t report_format, uint64_t period_exponent,
                      bool hold_preemption, bool enable)
{
   uint64_t properties[DRM_I915_PERF_PROP_MAX * 2];
   uint32_t p = 0;

   /* Single context sampling if valid context id. */
   if (ctx_id != INTEL_PERF_INVALID_CTX_ID) {
      properties[p++] = DRM_I915_PERF_PROP_CTX_HANDLE;
      properties[p++] = ctx_id;
   }

   /* Include OA reports in samples */
   properties[p++] = DRM_I915_PERF_PROP_SAMPLE_OA;
   properties[p++] = true;

   /* OA unit configuration */
   properties[p++] = DRM_I915_PERF_PROP_OA_METRICS_SET;
   properties[p++] = metrics_set_id;

   properties[p++] = DRM_I915_PERF_PROP_OA_FORMAT;
   properties[p++] = report_format;

   properties[p++] = DRM_I915_PERF_PROP_OA_EXPONENT;
   properties[p++] = period_exponent;

   if (hold_preemption) {
      properties[p++] = DRM_I915_PERF_PROP_HOLD_PREEMPTION;
      properties[p++] = true;
   }

   /* If global SSEU is available, pin it to the default. This will ensure on
    * Gfx11 for instance we use the full EU array. Initially when perf was
    * enabled we would use only half on Gfx11 because of functional
    * requirements.
    *
    * Not supported on Gfx12.5+.
    */
   if (intel_perf_has_global_sseu(perf_config) &&
       perf_config->devinfo->verx10 < 125) {
      properties[p++] = DRM_I915_PERF_PROP_GLOBAL_SSEU;
      properties[p++] = (uintptr_t) perf_config->sseu;
   }

   assert(p <= ARRAY_SIZE(properties));

   struct drm_i915_perf_open_param param = {
      .flags = I915_PERF_FLAG_FD_CLOEXEC |
               I915_PERF_FLAG_FD_NONBLOCK |
               (enable ? 0 : I915_PERF_FLAG_DISABLED),
      .num_properties = p / 2,
      .properties_ptr = (uintptr_t) properties,
   };
   int fd = intel_ioctl(drm_fd, DRM_IOCTL_I915_PERF_OPEN, &param);
   return fd > -1 ? fd : 0;
}

static bool
i915_query_perf_config_supported(struct intel_perf_config *perf, int fd)
{
   int32_t length = 0;
   return !intel_i915_query_flags(fd, DRM_I915_QUERY_PERF_CONFIG,
                                  DRM_I915_QUERY_PERF_CONFIG_LIST,
                                  NULL, &length);
}

static bool
i915_query_perf_config_data(struct intel_perf_config *perf,
                            int fd, const char *guid,
                            struct drm_i915_perf_oa_config *config)
{
   char data[sizeof(struct drm_i915_query_perf_config) +
             sizeof(struct drm_i915_perf_oa_config)] = {};
   struct drm_i915_query_perf_config *i915_query = (void *)data;
   struct drm_i915_perf_oa_config *i915_config = (void *)data + sizeof(*i915_query);

   memcpy(i915_query->uuid, guid, sizeof(i915_query->uuid));
   memcpy(i915_config, config, sizeof(*config));

   int32_t item_length = sizeof(data);
   if (intel_i915_query_flags(fd, DRM_I915_QUERY_PERF_CONFIG,
                              DRM_I915_QUERY_PERF_CONFIG_DATA_FOR_UUID,
                              i915_query, &item_length))
      return false;

   memcpy(config, i915_config, sizeof(*config));

   return true;
}

struct intel_perf_registers *
i915_perf_load_configurations(struct intel_perf_config *perf_cfg, int fd, const char *guid)
{
   struct drm_i915_perf_oa_config i915_config = { 0, };
   if (!i915_query_perf_config_data(perf_cfg, fd, guid, &i915_config))
      return NULL;

   struct intel_perf_registers *config = rzalloc(NULL, struct intel_perf_registers);
   config->n_flex_regs = i915_config.n_flex_regs;
   config->flex_regs = rzalloc_array(config, struct intel_perf_query_register_prog, config->n_flex_regs);
   config->n_mux_regs = i915_config.n_mux_regs;
   config->mux_regs = rzalloc_array(config, struct intel_perf_query_register_prog, config->n_mux_regs);
   config->n_b_counter_regs = i915_config.n_boolean_regs;
   config->b_counter_regs = rzalloc_array(config, struct intel_perf_query_register_prog, config->n_b_counter_regs);

   /*
    * struct intel_perf_query_register_prog maps exactly to the tuple of
    * (register offset, register value) returned by the i915.
    */
   i915_config.flex_regs_ptr = to_const_user_pointer(config->flex_regs);
   i915_config.mux_regs_ptr = to_const_user_pointer(config->mux_regs);
   i915_config.boolean_regs_ptr = to_const_user_pointer(config->b_counter_regs);
   if (!i915_query_perf_config_data(perf_cfg, fd, guid, &i915_config)) {
      ralloc_free(config);
      return NULL;
   }

   return config;
}

static int
i915_perf_version(int drm_fd)
{
   int tmp = 0;
   intel_gem_get_param(drm_fd, I915_PARAM_PERF_REVISION, &tmp);
   return tmp;
}

static void
i915_get_sseu(int drm_fd, struct drm_i915_gem_context_param_sseu *sseu)
{
   struct drm_i915_gem_context_param arg = {
      .param = I915_CONTEXT_PARAM_SSEU,
      .size = sizeof(*sseu),
      .value = (uintptr_t) sseu
   };

   intel_ioctl(drm_fd, DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM, &arg);
}

bool
i915_oa_metrics_available(struct intel_perf_config *perf, int fd, bool use_register_snapshots)
{
   int version = i915_perf_version(fd);
   bool i915_perf_oa_available = false;
   struct stat sb;

   if (i915_query_perf_config_supported(perf, fd))
      perf->features_supported |= INTEL_PERF_FEATURE_QUERY_PERF;

   if (version >= 4)
      perf->features_supported |= INTEL_PERF_FEATURE_GLOBAL_SSEU;
   if (version >= 3)
      perf->features_supported |= INTEL_PERF_FEATURE_HOLD_PREEMPTION;

   /* Record the default SSEU configuration. */
   perf->sseu = rzalloc(perf, struct drm_i915_gem_context_param_sseu);
   if (!perf->sseu)
      return false;

   i915_get_sseu(fd, perf->sseu);

   /* The existence of this sysctl parameter implies the kernel supports
    * the i915 perf interface.
    */
   if (stat("/proc/sys/dev/i915/perf_stream_paranoid", &sb) == 0) {

      /* If _paranoid == 1 then on Gfx8+ we won't be able to access OA
       * metrics unless running as root.
       */
      if (perf->devinfo->platform == INTEL_PLATFORM_HSW)
         i915_perf_oa_available = true;
      else {
         uint64_t paranoid = 1;

         read_file_uint64("/proc/sys/dev/i915/perf_stream_paranoid", &paranoid);

         if (paranoid == 0 || geteuid() == 0)
            i915_perf_oa_available = true;
      }
   }

   return i915_perf_oa_available;
}

int
i915_perf_stream_read_samples(struct intel_perf_config *perf_config,
                              int perf_stream_fd, uint8_t *buffer,
                              size_t buffer_len)
{
   const size_t sample_header_size = perf_config->oa_sample_size +
                                     sizeof(struct intel_perf_record_header);
   int len;

   if (buffer_len < sample_header_size)
      return -ENOSPC;

   do {
      len = read(perf_stream_fd, buffer, buffer_len);
   } while (len < 0 && errno == EINTR);

   if (len <= 0)
      return len < 0 ? -errno : 0;

   /* works as long drm_i915_perf_record_header and intel_perf_record_header
    * definition matches
    */
   return len;
}

uint64_t
i915_add_config(struct intel_perf_config *perf, int fd,
                const struct intel_perf_registers *config,
                const char *guid)
{
   struct drm_i915_perf_oa_config i915_config = { 0, };

   memcpy(i915_config.uuid, guid, sizeof(i915_config.uuid));

   i915_config.n_mux_regs = config->n_mux_regs;
   i915_config.mux_regs_ptr = to_const_user_pointer(config->mux_regs);

   i915_config.n_boolean_regs = config->n_b_counter_regs;
   i915_config.boolean_regs_ptr = to_const_user_pointer(config->b_counter_regs);

   i915_config.n_flex_regs = config->n_flex_regs;
   i915_config.flex_regs_ptr = to_const_user_pointer(config->flex_regs);

   int ret = intel_ioctl(fd, DRM_IOCTL_I915_PERF_ADD_CONFIG, &i915_config);
   return ret > 0 ? ret : 0;
}

int
i915_remove_config(struct intel_perf_config *perf, int fd, uint64_t config_id)
{
   return intel_ioctl(fd, DRM_IOCTL_I915_PERF_REMOVE_CONFIG, &config_id);
}

bool
i915_has_dynamic_config_support(struct intel_perf_config *perf, int fd)
{
   return i915_remove_config(perf, fd, UINT64_MAX) < 0 && errno == ENOENT;
}

int
i915_perf_stream_set_state(int perf_stream_fd, bool enable)
{
   unsigned long uapi = enable ? I915_PERF_IOCTL_ENABLE : I915_PERF_IOCTL_DISABLE;

   return intel_ioctl(perf_stream_fd, uapi, 0);
}

int
i915_perf_stream_set_metrics_id(int perf_stream_fd, uint64_t metrics_set_id)
{
   return intel_ioctl(perf_stream_fd, I915_PERF_IOCTL_CONFIG,
                      (void *)(uintptr_t)metrics_set_id);
}
