/*
 * Copyright © 2021 Collabora, Ltd.
 * Copyright © 2026 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <string.h>
#include <xf86drm.h>

#include "util/macros.h"
#include "util/ralloc.h"
#include "util/timespec.h"

#include "pan_perf.h"

#include <drm-uapi/panfrost_drm.h>
#include <lib/kmod/pan_kmod.h>
#include <lib/pan_props.h>

int64_t
pan_perf_counter_read(const struct pan_perf *perf,
                      const struct mali_perf_counter *counter, uint8_t blk_idx)
{
   return counter->get_value(&perf->session->mali_perf_backend, blk_idx,
                             &perf->constants, &perf->dump_info);
}

int64_t
pan_perf_counter_read_sum(const struct pan_perf *perf,
                          const struct mali_perf_counter *counter)
{
   /* If counter belongs to shader core, sum values for all cores. */
   uint8_t blk_cnt =
      mali_perf_block_count(counter->block, &perf->constants);
   int64_t ret = 0;

   for (uint8_t blk_idx = 0; blk_idx < blk_cnt; blk_idx++) {
      ret += pan_perf_counter_read(perf, counter, blk_idx);
      assert(ret >= 0 && "counter sum should not overflow");
   }

   return ret;
}

void
pan_perf_destroy(struct pan_perf *perf)
{
   if (!perf)
      return;

   if (perf->session)
      pan_kmod_perf_destroy(perf->session);

   if (perf->dev)
      pan_kmod_dev_destroy(perf->dev);

   free(perf);
}

struct pan_perf *
pan_perf_create(int fd)
{
   struct pan_perf *perf = calloc(1, sizeof(*perf));
   if (!perf) {
      mesa_loge("Could not allocate pan_perf instance");
      return NULL;
   }

   perf->dev = pan_kmod_dev_create(fd, 0, NULL);
   if (!perf->dev) {
      mesa_loge("Could not create kmod device");
      goto err_destroy_perf;
   }

   perf->session = pan_kmod_perf_create(perf->dev);
   if (!perf->session) {
      mesa_loge("Could not create kmod perf session");
      goto err_destroy_perf;
   }

   struct pan_kmod_dev_props props = perf->dev->props;

   perf->constants.ext_bus_byte_size = pan_query_bus_width(&props);
   perf->constants.l2_cache_count = pan_query_l2_slices(&props);
   perf->constants.shader_core_count = pan_query_core_count(&props);

   const struct pan_model *model =
      pan_get_model(props.gpu_id, props.gpu_variant);
   if (model == NULL) {
      mesa_loge("GPU not supported");
      goto err_destroy_perf;
   }

   perf->info = mali_perf_get_info_for_gpu(model->performance_counters);
   if (perf->info == NULL) {
      mesa_loge("Performance counters missing!");
      goto err_destroy_perf;
   }

   return perf;

err_destroy_perf:
   pan_perf_destroy(perf);
   return NULL;
}

int
pan_perf_enable(struct pan_perf *perf, uint64_t sampling_period_ns)
{
   struct pan_kmod_perf_config cfg = {
      .sampling_period_ns = sampling_period_ns,
   };

   /* For each counter, we enable the HW sources. */
   for (const struct mali_perf_counter *counter = perf->info->counters;
        counter->name; counter++) {
      for (const struct mali_perf_counter_source *source = counter->sources;
           source->block != MALI_PERF_BLOCK_NONE; source++)
         BITSET_SET(cfg.blocks[source->block].counters, source->counter);
   }

   return pan_kmod_perf_enable(perf->session, &cfg);
}

int
pan_perf_disable(struct pan_perf *perf)
{
   return pan_kmod_perf_disable(perf->session);
}

int
pan_perf_dump(struct pan_perf *perf)
{
   pan_kmod_perf_dump(perf->session, &perf->dump_info);
   return 0;
}
