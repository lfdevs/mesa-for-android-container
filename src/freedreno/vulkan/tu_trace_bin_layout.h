/* Copyright © 2020 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef TU_BIN_LAYOUT_H_
#define TU_BIN_LAYOUT_H_

#include <stdint.h>
#include <vulkan/vulkan_core.h>

#include "util/macros.h"
#include "util/perf/u_trace.h"

#include "tu_common.h"
#include "tu_tile_config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tu_bin_layout_data {
   /* uint64_t instead of bool for alignment reasons. */
   uint64_t valid;

   VkExtent2D fb_size;
   VkExtent2D bin_size;

   VkRect2D viewports[MAX_VIEWPORTS];
   uint32_t viewport_count;

   VkRect2D scissors[MAX_SCISSORS];
   uint32_t scissor_count;

   uint32_t view_count;
   VkOffset2D fdm_offsets[MAX_VIEWS];
   bool has_fdm_offsets;

   VkExtent2D tile_size;
   uint32_t tile_count;
   struct tu_tile_config tiles[];
};

#ifdef HAVE_PERFETTO
struct tu_bin_layout_data *
tu_bin_layout_data_create(const struct tu_cmd_buffer *cmd,
                          uint32_t view_count,
                          const VkOffset2D *fdm_offsets,
                          uint32_t max_tiles);
#endif

static inline size_t
tu_bin_layout_data_size(const struct tu_bin_layout_data *data)
{
   if (!data || !data->valid) {
      return sizeof(data->valid);
   }

   return sizeof(*data) + data->tile_count * sizeof(data->tiles[0]);
}

static inline void
tu_bin_layout_data_copy(void *_dst,
                        const struct tu_bin_layout_data *src,
                        size_t size)

{
   struct tu_bin_layout_data *dst = (struct tu_bin_layout_data *)_dst;

   if (!src || !src->valid) {
      dst->valid = false;
      return;
   }

   assert(size == tu_bin_layout_data_size(src));

   memcpy(dst, src, size);
}

static bool
_tu_bin_layout_data_json_serialize_base(enum u_trace_backend_type backend_type,
                                        const struct tu_bin_layout_data *data,
                                        char **str_out)
{
    if (!data->valid) {
       *str_out = ralloc_strdup(NULL, "binInfo");
       return true;
    }

    if (backend_type == U_TRACE_BACKEND_PRINT ||
        backend_type == U_TRACE_BACKEND_JSON) {
       *str_out = ralloc_strdup(NULL, "Not shown");
       return true;
    }

    return false;
}

#ifdef HAVE_PERFETTO
char *
tu_bin_layout_data_json_serialize(enum u_trace_backend_type backend_type,
                                  const struct tu_bin_layout_data *data);
#else
static char *
tu_bin_layout_data_json_serialize(enum u_trace_backend_type backend_type,
                                  const struct tu_bin_layout_data *data)
{
   char *str;
   const bool ret =
      _tu_bin_layout_data_json_serialize_base(backend_type, data, &str);
   if (!ret) {
      UNREACHABLE("No backend other than Perfetto but no HAVE_PERFETTO");
   }

   return str;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* TU_BIN_LAYOUT_H_ */
