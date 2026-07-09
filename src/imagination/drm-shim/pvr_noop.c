/*
 * Copyright © 2026 Imagination Technologies Ltd.
 * Copyright 2018 Broadcom
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common/pvr_device_info.h"
#include "drm-shim/drm_shim.h"
#include "drm-uapi/pvr_drm.h"
#include "hwdef/rogue_hw_utils.h"
#include "util/macros.h"
#include "util/u_debug.h"

#define DEFAULT_DEVICE_BVNC PVR_BVNC_PACK(36, 53, 104, 796)
static struct pvr_device_info device_info;

static bool musthave_quirks = false;
static unsigned num_quirks = 0;
static uint32_t *quirks = NULL;

static unsigned num_enhancements = 0;
static uint32_t *enhancements = NULL;

static int
pvr_ioctl_noop(UNUSED int fd, UNUSED unsigned long request, UNUSED void *arg)
{
   return 0;
}

static inline uint32_t
rogue_num_phantoms(const struct pvr_device_info *dev_info)
{
   return DIV_ROUND_UP(PVR_GET_FEATURE_VALUE(&device_info, num_clusters, 1U),
                       4U);
}

static inline uint32_t
rogue_common_store_partition_space_size(const struct pvr_device_info *dev_info)
{
   const uint32_t max_partitions =
      PVR_GET_FEATURE_VALUE(dev_info, max_partitions, 0U);
   const uint32_t tile_size_x =
      PVR_GET_FEATURE_VALUE(dev_info, tile_size_x, 0U);
   const uint32_t tile_size_y =
      PVR_GET_FEATURE_VALUE(dev_info, tile_size_y, 0U);

   if (tile_size_x == 16U && tile_size_y == 16U) {
      uint32_t usc_min_output_registers_per_pix =
         PVR_GET_FEATURE_VALUE(dev_info, usc_min_output_registers_per_pix, 0U);

      return tile_size_x * tile_size_y * max_partitions *
             usc_min_output_registers_per_pix;
   }

   return max_partitions * 1024U;
}

static inline uint32_t
rogue_common_store_alloc_region_size(const struct pvr_device_info *dev_info)
{
   uint32_t alloc_region_size;

   const uint32_t common_store_size_in_dwords =
      PVR_GET_FEATURE_VALUE(dev_info,
                            common_store_size_in_dwords,
                            512U * 4U * 4U);

   alloc_region_size = common_store_size_in_dwords - (256U * 4U) -
                       rogue_common_store_partition_space_size(dev_info);

   if (PVR_HAS_QUIRK(dev_info, 44079)) {
      uint32_t common_store_split_point = (768U * 4U * 4U);

      return MIN2(common_store_split_point - (256U * 4U), alloc_region_size);
   }

   return alloc_region_size;
}

static inline uint32_t rogue_max_coeffs(const struct pvr_device_info *dev_info)
{
   uint32_t max_coeff_additional_portion = ROGUE_MAX_VERTEX_SHARED_REGISTERS;
   uint32_t pending_allocation_shared_regs = 2U * 1024U;
   uint32_t pending_allocation_coeff_regs = 0U;
   uint32_t num_phantoms = rogue_num_phantoms(dev_info);
   uint32_t tiles_in_flight =
      PVR_GET_FEATURE_VALUE(dev_info, isp_max_tiles_in_flight, 0U);
   uint32_t max_coeff_pixel_portion =
      DIV_ROUND_UP(tiles_in_flight, num_phantoms);
   max_coeff_pixel_portion *= ROGUE_MAX_PIXEL_SHARED_REGISTERS;

   /*
    * Compute tasks on cores with BRN48492 and without compute overlap may lock
    * up without two additional lines of coeffs.
    */
   if (PVR_HAS_QUIRK(dev_info, 48492) &&
       !PVR_HAS_FEATURE(dev_info, compute_overlap))
      pending_allocation_coeff_regs = 2U * 1024U;

   if (PVR_HAS_ENHANCEMENT(dev_info, 38748))
      pending_allocation_shared_regs = 0;

   if (PVR_HAS_ENHANCEMENT(dev_info, 38020))
      max_coeff_additional_portion += ROGUE_MAX_COMPUTE_SHARED_REGISTERS;

   return rogue_common_store_alloc_region_size(dev_info) +
          pending_allocation_coeff_regs -
          (max_coeff_pixel_portion + max_coeff_additional_portion +
           pending_allocation_shared_regs);
}

static inline uint32_t
rogue_cdm_max_local_mem_size_regs(const struct pvr_device_info *dev_info)
{
   uint32_t available_coeffs_in_dwords = rogue_max_coeffs(dev_info);

   if (PVR_HAS_QUIRK(dev_info, 48492) && PVR_HAS_FEATURE(dev_info, roguexe) &&
       !PVR_HAS_FEATURE(dev_info, compute_overlap)) {
      /* Driver must not use the 2 reserved lines. */
      available_coeffs_in_dwords -= ROGUE_CSRM_LINE_SIZE_IN_DWORDS * 2U;
   }

   /*
    * The maximum amount of local memory available to a kernel is the minimum
    * of the total number of coefficient registers available and the max common
    * store allocation size which can be made by the CDM.
    *
    * If any coeff lines are reserved for tessellation or pixel then we need to
    * subtract those too.
    */
   return MIN2(available_coeffs_in_dwords,
               ROGUE_MAX_PER_KERNEL_LOCAL_MEM_SIZE_REGS);
}

static int pvr_dev_query_gpu_info_get(struct drm_pvr_ioctl_dev_query_args *args)
{
   struct drm_pvr_dev_query_gpu_info *gpu_info =
      (struct drm_pvr_dev_query_gpu_info *)args->pointer;

   if (!gpu_info)
      goto dev_query_gpu_info_set_size;

   gpu_info->gpu_id = pvr_get_packed_bvnc(&device_info);
   gpu_info->num_phantoms = rogue_num_phantoms(&device_info);

dev_query_gpu_info_set_size:
   args->size = sizeof(*gpu_info);
   return 0;
}

static int
pvr_dev_query_runtime_info_get(struct drm_pvr_ioctl_dev_query_args *args)
{
   struct drm_pvr_dev_query_runtime_info *runtime_info =
      (struct drm_pvr_dev_query_runtime_info *)args->pointer;

   if (!runtime_info)
      goto dev_query_runtime_info_set_size;

   if (PVR_HAS_FEATURE(&device_info, roguexe)) {
      if (PVR_HAS_QUIRK(&device_info, 66011))
         runtime_info->free_list_min_pages = 40;
      else
         runtime_info->free_list_min_pages = 25;
   } else {
      runtime_info->free_list_min_pages = 50;
   }

   runtime_info->free_list_max_pages = 0x80000;

   runtime_info->common_store_alloc_region_size =
      rogue_common_store_alloc_region_size(&device_info);

   runtime_info->common_store_partition_space_size =
      rogue_common_store_partition_space_size(&device_info);

   runtime_info->max_coeffs = rogue_max_coeffs(&device_info);

   runtime_info->cdm_max_local_mem_size_regs =
      rogue_cdm_max_local_mem_size_regs(&device_info);

dev_query_runtime_info_set_size:
   args->size = sizeof(*runtime_info);
   return 0;
}

static int pvr_dev_query_quirks_get(struct drm_pvr_ioctl_dev_query_args *args)
{
   struct drm_pvr_dev_query_quirks *query_quirks =
      (struct drm_pvr_dev_query_quirks *)args->pointer;

   if (!query_quirks)
      goto dev_query_quirks_set_size;

   if (!query_quirks->quirks) {
      query_quirks->count = num_quirks;
      query_quirks->musthave_count = musthave_quirks ? num_quirks : 0;
      goto dev_query_quirks_set_size;
   }

   if (query_quirks->count > num_quirks)
      query_quirks->count = num_quirks;

   memcpy((void *)query_quirks->quirks,
          quirks,
          sizeof(*quirks) * query_quirks->count);

dev_query_quirks_set_size:
   args->size = sizeof(*query_quirks);
   return 0;
}

static int
pvr_dev_query_enhancements_get(struct drm_pvr_ioctl_dev_query_args *args)
{
   struct drm_pvr_dev_query_enhancements *query_enhancements =
      (struct drm_pvr_dev_query_enhancements *)args->pointer;

   if (!query_enhancements)
      goto dev_query_enhancements_set_size;

   if (!query_enhancements->enhancements) {
      query_enhancements->count = num_enhancements;
      goto dev_query_enhancements_set_size;
   }

   if (query_enhancements->count > num_enhancements)
      query_enhancements->count = num_enhancements;

   memcpy((void *)query_enhancements->enhancements,
          enhancements,
          sizeof(*enhancements) * query_enhancements->count);

dev_query_enhancements_set_size:
   args->size = sizeof(*query_enhancements);
   return 0;
}

static const struct drm_pvr_heap pvr_heaps[DRM_PVR_HEAP_COUNT] = {
   [DRM_PVR_HEAP_GENERAL] = {
      .base = UINT64_C(0x8000000000),
      .size = UINT64_C(0x2000000000),
      .flags = 0,
      .page_size_log2 = 12,
   },
   [DRM_PVR_HEAP_PDS_CODE_DATA] = {
      .base = UINT64_C(0xda00000000),
      .size = UINT64_C(0x100000000),
      .flags = 0,
      .page_size_log2 = 12,
   },
   [DRM_PVR_HEAP_USC_CODE] = {
      .base = UINT64_C(0xe000000000),
      .size = UINT64_C(0x100000000),
      .flags = 0,
      .page_size_log2 = 12,
   },
   [DRM_PVR_HEAP_RGNHDR] = {
      .base = UINT64_C(0xdbf0000000),
      .size = 0x10000000,
      .flags = 0,
      .page_size_log2 = 12,
   },
   [DRM_PVR_HEAP_VIS_TEST] = {
      .base = UINT64_C(0xf200000000),
      .size = 0x00200000,
      .flags = 0,
      .page_size_log2 = 12,
   },
   [DRM_PVR_HEAP_TRANSFER_FRAG] = {
      .base = UINT64_C(0xe400000000),
      .size = UINT64_C(0x400000000),
      .flags = 0,
      .page_size_log2 = 12,
   },
};

static int
pvr_dev_query_heap_info_get(struct drm_pvr_ioctl_dev_query_args *args)
{
   struct drm_pvr_dev_query_heap_info *heap_info =
      (struct drm_pvr_dev_query_heap_info *)args->pointer;

   if (!heap_info)
      goto dev_query_heap_info_set_size;

   if (!heap_info->heaps.array) {
      heap_info->heaps.count = ARRAY_SIZE(pvr_heaps);
      heap_info->heaps.stride = sizeof(struct drm_pvr_heap);
      goto dev_query_heap_info_set_size;
   }

   if (heap_info->heaps.count > ARRAY_SIZE(pvr_heaps))
      heap_info->heaps.count = ARRAY_SIZE(pvr_heaps);

   memcpy((void *)heap_info->heaps.array,
          pvr_heaps,
          heap_info->heaps.stride * heap_info->heaps.count);

dev_query_heap_info_set_size:
   args->size = sizeof(*heap_info);
   return 0;
}

static const struct drm_pvr_static_data_area pvr_static_data_areas[] = {
   {
      .area_usage = DRM_PVR_STATIC_DATA_AREA_FENCE,
      .location_heap_id = DRM_PVR_HEAP_GENERAL,
      .offset = 0,
      .size = 128,
   },
   {
      .area_usage = DRM_PVR_STATIC_DATA_AREA_YUV_CSC,
      .location_heap_id = DRM_PVR_HEAP_GENERAL,
      .offset = 128,
      .size = 1024,
   },
   {
      .area_usage = DRM_PVR_STATIC_DATA_AREA_VDM_SYNC,
      .location_heap_id = DRM_PVR_HEAP_PDS_CODE_DATA,
      .offset = 0,
      .size = 128,
   },
   {
      .area_usage = DRM_PVR_STATIC_DATA_AREA_EOT,
      .location_heap_id = DRM_PVR_HEAP_PDS_CODE_DATA,
      .offset = 128,
      .size = 128,
   },
   {
      .area_usage = DRM_PVR_STATIC_DATA_AREA_VDM_SYNC,
      .location_heap_id = DRM_PVR_HEAP_USC_CODE,
      .offset = 0,
      .size = 128,
   },
};

static int
pvr_dev_query_static_data_areas_get(struct drm_pvr_ioctl_dev_query_args *args)
{
   struct drm_pvr_dev_query_static_data_areas *static_data_areas =
      (struct drm_pvr_dev_query_static_data_areas *)args->pointer;

   if (!static_data_areas)
      goto dev_query_static_data_areas_set_size;

   if (!static_data_areas->static_data_areas.array) {
      static_data_areas->static_data_areas.count =
         ARRAY_SIZE(pvr_static_data_areas);
      static_data_areas->static_data_areas.stride =
         sizeof(struct drm_pvr_static_data_area);
      goto dev_query_static_data_areas_set_size;
   }

   if (static_data_areas->static_data_areas.count >
       ARRAY_SIZE(pvr_static_data_areas)) {
      static_data_areas->static_data_areas.count =
         ARRAY_SIZE(pvr_static_data_areas);
   }

   memcpy((void *)static_data_areas->static_data_areas.array,
          pvr_static_data_areas,
          static_data_areas->static_data_areas.stride *
             static_data_areas->static_data_areas.count);

dev_query_static_data_areas_set_size:
   args->size = sizeof(*static_data_areas);
   return 0;
}

static int
pvr_ioctl_dev_query(UNUSED int fd, UNUSED unsigned long request, void *arg)
{
   struct drm_pvr_ioctl_dev_query_args *args = arg;

   switch ((enum drm_pvr_dev_query)args->type) {
   case DRM_PVR_DEV_QUERY_GPU_INFO_GET:
      return pvr_dev_query_gpu_info_get(args);

   case DRM_PVR_DEV_QUERY_RUNTIME_INFO_GET:
      return pvr_dev_query_runtime_info_get(args);

   case DRM_PVR_DEV_QUERY_QUIRKS_GET:
      return pvr_dev_query_quirks_get(args);

   case DRM_PVR_DEV_QUERY_ENHANCEMENTS_GET:
      return pvr_dev_query_enhancements_get(args);

   case DRM_PVR_DEV_QUERY_HEAP_INFO_GET:
      return pvr_dev_query_heap_info_get(args);

   case DRM_PVR_DEV_QUERY_STATIC_DATA_AREAS_GET:
      return pvr_dev_query_static_data_areas_get(args);

   default:
      break;
   }

   return -EINVAL;
}

static int pvr_ioctl_create_bo(int fd, UNUSED unsigned long request, void *arg)
{
   struct drm_pvr_ioctl_create_bo_args *args = arg;

   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
   struct shim_bo *bo = calloc(1, sizeof(*bo));

   drm_shim_bo_init(bo, args->size);

   args->handle = drm_shim_bo_get_handle(shim_fd, bo);

   drm_shim_bo_put(bo);

   return 0;
}

static int
pvr_ioctl_get_bo_mmap_offset(int fd, UNUSED unsigned long request, void *arg)
{
   struct drm_pvr_ioctl_get_bo_mmap_offset_args *args = arg;

   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
   struct shim_bo *bo = drm_shim_bo_lookup(shim_fd, args->handle);

   args->offset = drm_shim_bo_get_mmap_offset(shim_fd, bo);

   return 0;
}

#define PVR_IOC_NR(_name) (_IOC_NR(DRM_IOCTL_PVR_##_name) - DRM_COMMAND_BASE)

static ioctl_fn_t driver_ioctls[] = {
   [PVR_IOC_NR(DEV_QUERY)] = pvr_ioctl_dev_query,
   [PVR_IOC_NR(CREATE_BO)] = pvr_ioctl_create_bo,
   [PVR_IOC_NR(GET_BO_MMAP_OFFSET)] = pvr_ioctl_get_bo_mmap_offset,
   [PVR_IOC_NR(CREATE_VM_CONTEXT)] = pvr_ioctl_noop,
   [PVR_IOC_NR(DESTROY_VM_CONTEXT)] = pvr_ioctl_noop,
   [PVR_IOC_NR(VM_MAP)] = pvr_ioctl_noop,
   [PVR_IOC_NR(VM_UNMAP)] = pvr_ioctl_noop,
   [PVR_IOC_NR(CREATE_CONTEXT)] = pvr_ioctl_noop,
   [PVR_IOC_NR(DESTROY_CONTEXT)] = pvr_ioctl_noop,
   [PVR_IOC_NR(CREATE_FREE_LIST)] = pvr_ioctl_noop,
   [PVR_IOC_NR(DESTROY_FREE_LIST)] = pvr_ioctl_noop,
   [PVR_IOC_NR(CREATE_HWRT_DATASET)] = pvr_ioctl_noop,
   [PVR_IOC_NR(DESTROY_HWRT_DATASET)] = pvr_ioctl_noop,
   [PVR_IOC_NR(SUBMIT_JOBS)] = pvr_ioctl_noop,
};

static unsigned count_elems(const char *str, const char *delims)
{
   if (!str || !strlen(str))
      return 0;

   unsigned count = 1;
   for (unsigned u = 0; u < strlen(str); ++u) {
      for (unsigned d = 0; d < strlen(delims); ++d) {
         if (str[u] == delims[d]) {
            ++count;
            break;
         }
      }
   }

   return count;
}

static void u32_str_list_to_array(const char *str_list,
                                  const char *delims,
                                  unsigned num_elems,
                                  uint32_t *array)
{
   for (unsigned u = 0; u < num_elems; ++u) {
      array[u] = strtol(str_list, NULL, 10);
      str_list += strcspn(str_list, delims) + 1;
   }
}

static void init_overrides(void)
{
   const char *device_bvnc = debug_get_option("PVR_SHIM_DEVICE_BVNC", NULL);
   static const char *delims = ",;";
   unsigned b, v, n, c;
   uint64_t bvnc = DEFAULT_DEVICE_BVNC;
   int ret;

   if (device_bvnc) {
      ret = sscanf(device_bvnc, "%u.%u.%u.%u", &b, &v, &n, &c);
      if (ret != 4) {
         fprintf(stderr,
                 "pvr-drm-shim: Warning: Invalid device BVNC format \"%s\"; "
                 "reverting to default device BVNC.\n",
                 device_bvnc);

         bvnc = DEFAULT_DEVICE_BVNC;
      } else {
         bvnc = PVR_BVNC_PACK(b, v, n, c);
      }
   }

   ret = pvr_device_info_init(&device_info, bvnc);
   if (ret) {
      fprintf(stderr,
              "pvr-drm-shim: Warning: Device BVNC provided \"%s\" not found; "
              "reverting to default device BVNC.\n",
              device_bvnc);

      ret = pvr_device_info_init(&device_info, DEFAULT_DEVICE_BVNC);
      assert(!ret);
   }

   musthave_quirks = debug_get_bool_option("PVR_SHIM_MUSTHAVE_QUIRKS", false);
   const char *quirk_list = debug_get_option("PVR_SHIM_QUIRKS", NULL);
   if (quirk_list) {
      num_quirks = count_elems(quirk_list, delims);
      quirks = malloc(num_quirks * sizeof(*quirks));
      u32_str_list_to_array(quirk_list, delims, num_quirks, quirks);
   }

   const char *enhancement_list =
      debug_get_option("PVR_SHIM_ENHANCEMENTS", NULL);
   if (enhancement_list) {
      num_enhancements = count_elems(enhancement_list, delims);
      enhancements = malloc(num_enhancements * sizeof(*enhancements));
      u32_str_list_to_array(enhancement_list,
                            delims,
                            num_enhancements,
                            enhancements);
   }
}

static void drm_shim_driver_fini(void)
{
   free(enhancements);
   free(quirks);
}

void drm_shim_driver_init(void)
{
   init_overrides();

   shim_device.driver_ioctls = driver_ioctls;
   shim_device.driver_ioctl_count = ARRAY_SIZE(driver_ioctls);

   shim_device.version_major = 1;
   shim_device.version_minor = 0;
   shim_device.version_patchlevel = 0;

   drm_shim_platform_device_setup("powervr", "/soc/pvr", "img,img-rogue");

   atexit(drm_shim_driver_fini);
}
