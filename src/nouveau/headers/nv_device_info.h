#ifndef NV_DEVINFO_H
#define NV_DEVINFO_H

#include <stdbool.h>
#include <string.h>

#include "util/macros.h"

#define NVIDIA_VENDOR_ID 0x10de

enum ENUM_PACKED nv_device_type {
   NV_DEVICE_TYPE_IGP,
   NV_DEVICE_TYPE_DIS,
   NV_DEVICE_TYPE_SOC,
};

/* Matches drm_nouveau_get_zcull_info */
struct nv_zcull_device_info {
   uint32_t width_align_pixels;
   uint32_t height_align_pixels;
   uint32_t pixel_squares_by_aliquots;
   uint32_t aliquot_total;
   uint32_t zcull_region_byte_multiplier;
   uint32_t zcull_region_header_size;
   uint32_t zcull_subregion_header_size;
   uint32_t subregion_count;
   uint32_t subregion_width_align_pixels;
   uint32_t subregion_height_align_pixels;

   uint32_t ctxsw_size;
   uint32_t ctxsw_align;
};

struct nv_device_info {
   enum nv_device_type type;

   uint16_t device_id;
   uint16_t chipset;

   char device_name[64];
   char chipset_name[16];

   /* Populated if type != NV_DEVICE_TYPE_SOC */
   struct {
      uint16_t domain;
      uint8_t bus;
      uint8_t dev;
      uint8_t func;
      uint8_t revision_id;
   } pci;

   uint8_t sm; /**< Shader model */

   uint8_t gpc_count;
   uint16_t tpc_count;
   uint8_t mp_per_tpc;
   uint8_t max_warps_per_mp;
   uint8_t max_blocks_per_mp;

   bool has_transfer_queue;
   bool has_video;

   /** Non-coherent memory map atom size */
   uint16_t nc_atom_size_B;

   uint16_t cls_copy;
   uint16_t cls_eng2d;
   uint16_t cls_eng3d;
   uint16_t cls_m2mf;
   uint16_t cls_compute;
   uint16_t cls_gpfifo;
   uint16_t cls_vdec;

   uint64_t vram_size_B;
   uint64_t bar_size_B;

   /* Max shared memory per workgroup */
   uint16_t max_smem_per_wg_kB;

   /* Max shared memory configurations per MP. Those values matter for
    * occupancy calculations and describe legal shared memory splits we can
    * configure the hardware with.
    */
   uint16_t sm_smem_sizes_kB[10];
   uint8_t sm_smem_size_count;

   struct nv_zcull_device_info zcull_info;
   bool has_zcull_info;
};

static inline void
nv_device_uuid(const struct nv_device_info *info, uint8_t *uuid, size_t len, bool vm_bind)
{
   uint16_t vendor_id = NVIDIA_VENDOR_ID;

   assert(len >= 16);

   memset(uuid, 0, len);
   memcpy(&uuid[0], &info->chipset, 2);
   memcpy(&uuid[2], &vendor_id, 2);
   memcpy(&uuid[4], &info->device_id, 2);

   if (info->type != NV_DEVICE_TYPE_SOC) {
      memcpy(&uuid[6], &info->pci.domain, 2);
      uuid[8]  = info->pci.bus;
      uuid[9]  = info->pci.dev;
      uuid[10] = info->pci.func;
   }
   uuid[11] = vm_bind;
}

#endif /* NV_DEVINFO_H */
