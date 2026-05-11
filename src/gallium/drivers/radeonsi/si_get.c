/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include "compiler/nir/nir.h"
#include "ac_nir.h"
#include "ac_shader_util.h"
#include "si_pipe.h"
#include "util/u_cpu_detect.h"
#include "util/u_screen.h"
#include <sys/utsname.h>
#include "drm-uapi/drm.h"
#include "aco_interface.h"

static const char *si_get_vendor(struct pipe_screen *pscreen)
{
   return "AMD";
}

static const char *si_get_device_vendor(struct pipe_screen *pscreen)
{
   return "AMD";
}

static void si_get_driver_uuid(struct pipe_screen *pscreen, char *uuid)
{
   ac_compute_driver_uuid(uuid, PIPE_UUID_SIZE);
}

static void si_get_device_uuid(struct pipe_screen *pscreen, char *uuid)
{
   struct si_screen *sscreen = (struct si_screen *)pscreen;

   ac_compute_device_uuid(&sscreen->info, uuid, PIPE_UUID_SIZE);
}

static const char *si_get_name(struct pipe_screen *pscreen)
{
   struct si_screen *sscreen = (struct si_screen *)pscreen;

   return sscreen->renderer_string;
}

static uint64_t si_get_timestamp(struct pipe_screen *screen)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   return 1000000 * sscreen->ws->query_value(sscreen->ws, RADEON_TIMESTAMP) /
          sscreen->info.clock_crystal_freq;
}

static void si_query_memory_info(struct pipe_screen *screen, struct pipe_memory_info *info)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   struct radeon_winsys *ws = sscreen->ws;
   unsigned vram_usage, gtt_usage;

   info->total_device_memory = sscreen->info.vram_size_kb;
   info->total_staging_memory = sscreen->info.gart_size_kb;

   /* The real TTM memory usage is somewhat random, because:
    *
    * 1) TTM delays freeing memory, because it can only free it after
    *    fences expire.
    *
    * 2) The memory usage can be really low if big VRAM evictions are
    *    taking place, but the real usage is well above the size of VRAM.
    *
    * Instead, return statistics of this process.
    */
   vram_usage = ws->query_value(ws, RADEON_VRAM_USAGE) / 1024;
   gtt_usage = ws->query_value(ws, RADEON_GTT_USAGE) / 1024;

   info->avail_device_memory =
      vram_usage <= info->total_device_memory ? info->total_device_memory - vram_usage : 0;
   info->avail_staging_memory =
      gtt_usage <= info->total_staging_memory ? info->total_staging_memory - gtt_usage : 0;

   info->device_memory_evicted = ws->query_value(ws, RADEON_NUM_BYTES_MOVED) / 1024;

   if (sscreen->info.is_amdgpu)
      info->nr_device_memory_evictions = ws->query_value(ws, RADEON_NUM_EVICTIONS);
   else
      /* Just return the number of evicted 64KB pages. */
      info->nr_device_memory_evictions = info->device_memory_evicted / 64;
}

void si_init_renderer_string(struct si_screen *sscreen)
{
   char first_name[256], second_name[32] = {}, kernel_version[128] = {};
   struct utsname uname_data;
   const char *name = ac_get_family_name(sscreen->info.family);

   snprintf(first_name, sizeof(first_name), "%s", sscreen->info.marketing_name);
   memset(second_name, 0, sizeof(second_name));
   for (unsigned i = 0; name[i] && i < ARRAY_SIZE(second_name) - 1; i++)
      second_name[i] = tolower(name[i]);

   if (uname(&uname_data) == 0)
      snprintf(kernel_version, sizeof(kernel_version), ", %s", uname_data.release);

   const char *compiler_name =
#if AMD_LLVM_AVAILABLE
      !sscreen->use_aco ? "LLVM " MESA_LLVM_VERSION_STRING :
#endif
      "ACO";

   snprintf(sscreen->renderer_string, sizeof(sscreen->renderer_string),
            "%s (radeonsi, %s%s%s, DRM %i.%i%s)", first_name, second_name,
            sscreen->has_gfx_compute ? ", " : "",
            sscreen->has_gfx_compute ? compiler_name : "",
            sscreen->info.drm_major, sscreen->info.drm_minor, kernel_version);
}

static int si_get_screen_fd(struct pipe_screen *screen)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   struct radeon_winsys *ws = sscreen->ws;

   return ws->get_fd(ws);
}

void si_init_screen_get_functions(struct si_screen *sscreen)
{
   sscreen->b.get_name = si_get_name;
   sscreen->b.get_vendor = si_get_vendor;
   sscreen->b.get_device_vendor = si_get_device_vendor;
   sscreen->b.get_screen_fd = si_get_screen_fd;
   sscreen->b.get_timestamp = si_get_timestamp;
   sscreen->b.get_device_uuid = si_get_device_uuid;
   sscreen->b.get_driver_uuid = si_get_driver_uuid;
   sscreen->b.query_memory_info = si_query_memory_info;
}

void si_init_screen_caps(struct si_screen *sscreen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&sscreen->b.caps;

   u_init_pipe_screen_caps(&sscreen->b, 1);

   /* Fixup dmabuf caps for the virtio + vpipe case (when fd=-1, u_init_pipe_screen_caps
    * fails to set this capability). */
   if (sscreen->info.is_virtio)
         caps->dmabuf |= DRM_PRIME_CAP_EXPORT | DRM_PRIME_CAP_IMPORT;

   caps->graphics = caps->mesh_shader = caps->compute = false;

   caps->resource_from_user_memory = !UTIL_ARCH_BIG_ENDIAN && sscreen->info.has_userptr;

   caps->device_protected_surface = sscreen->info.has_tmz_support;
#if defined(__ANDROID__) || defined(ANDROID)
   caps->device_protected_context = sscreen->info.has_tmz_support;
#endif

   caps->min_map_buffer_alignment = SI_MAP_BUFFER_ALIGNMENT;

   caps->uma = !sscreen->info.has_dedicated_vram;

   caps->context_priority_mask = sscreen->info.is_amdgpu ?
      PIPE_CONTEXT_PRIORITY_LOW | PIPE_CONTEXT_PRIORITY_MEDIUM | PIPE_CONTEXT_PRIORITY_HIGH : 0;

   caps->fence_signal = sscreen->info.has_syncobj;

   caps->native_fence_fd = sscreen->info.has_fence_to_handle;

   caps->endianness = PIPE_ENDIAN_LITTLE;
   caps->vendor_id = ATI_VENDOR_ID;
   caps->device_id = sscreen->info.pci_id;
   caps->video_memory = sscreen->info.vram_size_kb >> 10;
   caps->pci_group = sscreen->info.pci.domain;
   caps->pci_bus = sscreen->info.pci.bus;
   caps->pci_device = sscreen->info.pci.dev;
   caps->pci_function = sscreen->info.pci.func;

   /* Conversion to nanos from cycles per millisecond */
   caps->timer_resolution = DIV_ROUND_UP(1000000, sscreen->info.clock_crystal_freq);

   if (sscreen->ws->va_range)
      sscreen->ws->va_range(sscreen->ws, &caps->min_vma, &caps->max_vma);

   if (sscreen->info.has_timeline_syncobj &&
       !(sscreen->info.userq_ip_mask & BITFIELD_BIT(AMD_IP_GFX)))
      caps->max_timeline_semaphore_difference = UINT64_MAX;
}
