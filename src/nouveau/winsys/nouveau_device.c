#include "nouveau_device.h"

#include "nouveau_context.h"

#include "nvidia/g_nv_name_released.h"

#include "drm-uapi/nouveau_drm.h"
#include "util/hash_table.h"
#include "util/u_debug.h"
#include "util/os_file.h"
#include "util/os_misc.h"

#include <fcntl.h>
#include "nvif/cl0080.h"
#include "nvif/class.h"
#include "nvif/ioctl.h"
#include <unistd.h>
#include <xf86drm.h>

static const char *
name_for_chip(uint32_t dev_id,
              uint16_t subsystem_id,
              uint16_t subsystem_vendor_id)
{
   const char *name = NULL;
   for (uint32_t i = 0; i < ARRAY_SIZE(sChipsReleased); i++) {
      const CHIPS_RELEASED *chip = &sChipsReleased[i];

      if (dev_id != chip->devID)
         continue;

      if (chip->subSystemID == 0 && chip->subSystemVendorID == 0) {
         /* When subSystemID and subSystemVendorID are both 0, this is the
          * default name for the given chip.  A more specific name may exist
          * elsewhere in the list.
          */
         assert(name == NULL);
         name = chip->name;
         continue;
      }

      /* If we find a specific name, return it */
      if (chip->subSystemID == subsystem_id &&
          chip->subSystemVendorID == subsystem_vendor_id)
         return chip->name;
   }

   return name;
}

static uint8_t
sm_for_chipset(uint16_t chipset)
{
   if (chipset >= 0x1b0)
      return 120;
   else if (chipset >= 0x1a0)
      return 100;
   else if (chipset >= 0x190)
      return 89;
   // GH100 is older than AD10X, but is SM90
   else if (chipset >= 0x180)
      return 90;
   else if (chipset == 0x17b)
      return 87;
   else if (chipset >= 0x172)
      return 86;
   else if (chipset >= 0x170)
      return 80;
   else if (chipset >= 0x160)
      return 75;
   else if (chipset >= 0x14b)
      return 72;
   else if (chipset >= 0x140)
      return 70;
   else if (chipset >= 0x13b)
      return 62;
   else if (chipset >= 0x132)
      return 61;
   else if (chipset >= 0x130)
      return 60;
   else if (chipset >= 0x12b)
      return 53;
   else if (chipset >= 0x120)
      return 52;
   else if (chipset >= 0x110)
      return 50;
   // TODO: 37
   else if (chipset >= 0x0f0)
      return 35;
   else if (chipset >= 0x0ea)
      return 32;
   else if (chipset >= 0x0e0)
      return 30;
   // GF110 is SM20
   else if (chipset == 0x0c8)
      return 20;
   else if (chipset >= 0x0c1)
      return 21;
   else if (chipset >= 0x0c0)
      return 20;
   else if (chipset >= 0x0a3)
      return 12;
   // GT200 is SM13
   else if (chipset >= 0x0a0)
      return 13;
   else if (chipset >= 0x080)
      return 11;
   // this has to be == because 0x63 is older than 0x50 and has no compute
   else if (chipset == 0x050)
      return 10;
   // no compute
   return 0x00;
}

static uint8_t
max_warps_per_mp_for_sm(uint8_t sm)
{
   /* These are documented in each architecture's tuning guide. Eg. see:
    * https://docs.nvidia.com/cuda/blackwell-tuning-guide/index.html#occupancy
    */
   switch (sm) {
   case 10:
   case 11:
      return 24;
   case 12:
   case 13:
   case 75:
      return 32;
   case 20:
   case 21:
   case 86:
   case 87:
   case 89:
   case 120:
      return 48;
   case 30:
   case 32:
   case 35:
   case 37:
   case 50:
   case 52:
   case 53:
   case 60:
   case 61:
   case 62:
   case 70:
   case 72:
   case 80:
   case 90:
   case 100:
   case 104:
      return 64;
   default:
      assert(!"unkown SM version");
      // return the biggest known value
      return 64;
   }
}

static uint8_t
mp_per_tpc_for_chipset(uint16_t chipset)
{
   // GP100 is special and has two, otherwise it's a Volta and newer thing to have two
   if (chipset == 0x130 || chipset >= 0x140)
      return 2;
   return 1;
}

static void
init_shared_mem_sizes(struct nv_device_info *info)
{
   if (info->sm >= 80) {
      const uint16_t ampere_shared_mem[10] =
         { 0, 8, 16, 32, 64, 100, 132, 164, 196, 228 };

      /* Quotes taken from current CUDA docs */
      if (info->sm >= 120) {
         /* The docs on this are a bit contradictory, but CUDA tooling reports
          * values in line with the older SM levels reporting up to 100k.
          *
          * For devices of compute capability 12.0, shared memory capacity per
          * SM is 128KB.
          * For devices of compute capability 12.0 the maximum shared memory
          * per thread block is 99 KB.
          */
         info->sm_smem_size_count = 6;
      } else if (info->sm >= 90) {
         /* Both the NVIDIA H100 GPU and the NVIDIA B200 GPU support shared
          * memory capacities of 0, 8, 16, 32, 64, 100, 132, 164, 196 and
          * 228 KB per SM.
          *
          * GB10X has the same limits.
          */
         info->sm_smem_size_count = 10;
      } else if (info->sm == 80 || info->sm == 87) {
         /* The NVIDIA A100 GPU supports shared memory capacity of 0, 8, 16,
          * 32, 64, 100, 132 or 164 KB per SM.
          *
          * Same for GA10B.
          */
         info->sm_smem_size_count = 8;
      } else if (info->sm == 89 || info->sm == 86) {
         /* GPUs with compute capability 8.6 support shared memory capacity of
          * 0, 8, 16, 32, 64 or 100 KB per SM.
          * The NVIDIA Ada GPU architecture supports shared memory capacity of
          * 0, 8, 16, 32, 64 or 100 KB per SM.
          */
         info->sm_smem_size_count = 6;
      } else {
         UNREACHABLE("Unknown shared memory support for SM.");
      }

      assert(info->sm_smem_size_count <= ARRAY_SIZE(ampere_shared_mem));
      typed_memcpy(info->sm_smem_sizes_kB, ampere_shared_mem,
                   info->sm_smem_size_count);
      STATIC_ASSERT(ARRAY_SIZE(ampere_shared_mem) <=
                    ARRAY_SIZE(info->sm_smem_sizes_kB));
   } else if (info->sm >= 75) {
      /* https://docs.nvidia.com/cuda/turing-tuning-guide/index.html#unified-shared-memory-l1-texture-cache
       *   Turing supports two carveout configurations, either with 64 KB of
       *   shared memory and 32 KB of L1, or with 32 KB of shared memory and
       *   64 KB of L1. Turing allows a single thread block to address the full
       *   64 KB of shared memory.
       */
      info->sm_smem_sizes_kB[0] = 32;
      info->sm_smem_sizes_kB[1] = 64;
      info->sm_smem_size_count = 2;
   } else if (info->sm >= 70) {
      /* https://docs.nvidia.com/cuda/archive/12.9.1/volta-tuning-guide/index.html#unified-shared-memory-l1-texture-cache
       *
       *   Volta supports shared memory capacities of 0, 8, 16, 32, 64, or
       *   96 KB per SM. A new feature, Volta enables a single thread block
       *   to address the full 96 KB of shared memory.
       */
      const uint16_t volta_shared_mem[6] = { 0, 8, 16, 32, 64, 96 };
      info->sm_smem_size_count = ARRAY_SIZE(volta_shared_mem);
      typed_memcpy(info->sm_smem_sizes_kB, volta_shared_mem,
                   info->sm_smem_size_count);
      STATIC_ASSERT(ARRAY_SIZE(volta_shared_mem) <=
                    ARRAY_SIZE(info->sm_smem_sizes_kB));
   } else if (info->sm >= 50) {
      /* https://docs.nvidia.com/cuda/archive/12.9.1/maxwell-tuning-guide/index.html#shared-memory-capacity
       *
       *   GM107 provides 64 KB shared memory per SMM, and GM204 further
       *   increases this to 96 KB shared memory per SMM.
       *
       * https://docs.nvidia.com/cuda/archive/12.9.1/pascal-tuning-guide/index.html#shared-memory-capacity
       *
       *   GP100 offers 64 KB shared memory per SM, and GP104 provides 96 KB per SM.
       *
       * Limits for Tegra (SM53, SM62) are taken from the now gone occupancy
       * calculator.
       */
      info->sm_smem_size_count = 1;
      switch (info->sm) {
      case 50:
      case 53:
      case 60:
      case 62:
         info->sm_smem_sizes_kB[0] = 64;
         break;
      case 52:
      case 61:
         info->sm_smem_sizes_kB[0] = 96;
         break;
      default:
         UNREACHABLE("unknown shared mem size for sm");
         break;
      }
   } else if (info->sm == 37) {
      /* https://docs.nvidia.com/cuda/archive/11.8.0/kepler-tuning-guide/index.html#shared-memory-and-warp-shuffle:
       *
       *   GK210 improves on this by increasing the shared memory capacity per
       *   multiprocessor for each of the configurations described above by a
       *   further 64 (i.e., the application can select 112 KB, 96 KB, or 80
       *   KB of shared memory).
       *
       *   Note: The maximum shared memory per thread block for all Kepler
       *   GPUs, including GK210, remains 48 KB.
       */
      info->sm_smem_sizes_kB[0] = 80;
      info->sm_smem_sizes_kB[1] = 96;
      info->sm_smem_sizes_kB[2] = 112;
      info->sm_smem_size_count = 3;
   } else if (info->sm >= 30) {
      /* NVA0C0_QMDV00_06_L1_CONFIGURATION */
      info->sm_smem_sizes_kB[0] = 16;
      info->sm_smem_sizes_kB[1] = 32;
      info->sm_smem_sizes_kB[2] = 48;
      info->sm_smem_size_count = 3;
   } else if (info->sm >= 20) {
      /* NV90C0_SET_L1_CONFIGURATION */
      info->sm_smem_sizes_kB[0] = 16;
      info->sm_smem_sizes_kB[1] = 48;
      info->sm_smem_size_count = 2;
   } else {
      info->sm_smem_sizes_kB[0] = 16;
      info->sm_smem_size_count = 1;
   }

   /* See above, despite having more shared memory available, Kepler up to
    * Pascal can only address up to 48kB per workgroup.
    */
   if (info->sm < 70 && info->sm >= 30) {
      info->max_smem_per_wg_kB = 48;
   } else {
      info->max_smem_per_wg_kB =
         info->sm_smem_sizes_kB[info->sm_smem_size_count - 1];
   }
}

static int
nouveau_ws_param(int fd, uint64_t param, uint64_t *value)
{
   struct drm_nouveau_getparam data = { .param = param };

   int ret = drmCommandWriteRead(fd, DRM_NOUVEAU_GETPARAM, &data, sizeof(data));
   if (ret)
      return ret;

   *value = data.value;
   return 0;
}

static int
nouveau_ws_device_alloc(int fd, struct nouveau_ws_device *dev)
{
   struct {
      struct nvif_ioctl_v0 ioctl;
      struct nvif_ioctl_new_v0 new;
      struct nv_device_v0 dev;
   } args = {
      .ioctl = {
         .object = 0,
         .owner = NVIF_IOCTL_V0_OWNER_ANY,
         .route = 0x00,
         .type = NVIF_IOCTL_V0_NEW,
         .version = 0,
      },
      .new = {
         .handle = 0,
         .object = (uintptr_t)dev,
         .oclass = NV_DEVICE,
         .route = NVIF_IOCTL_V0_ROUTE_NVIF,
         .token = (uintptr_t)dev,
         .version = 0,
      },
      .dev = {
         .device = ~0ULL,
      },
   };

   return drmCommandWrite(fd, DRM_NOUVEAU_NVIF, &args, sizeof(args));
}

static int
nouveau_ws_device_info(int fd, struct nouveau_ws_device *dev)
{
   struct {
      struct nvif_ioctl_v0 ioctl;
      struct nvif_ioctl_mthd_v0 mthd;
      struct nv_device_info_v0 info;
   } args = {
      .ioctl = {
         .object = (uintptr_t)dev,
         .owner = NVIF_IOCTL_V0_OWNER_ANY,
         .route = 0x00,
         .type = NVIF_IOCTL_V0_MTHD,
         .version = 0,
      },
      .mthd = {
         .method = NV_DEVICE_V0_INFO,
         .version = 0,
      },
      .info = {
         .version = 0,
      },
   };

   int ret = drmCommandWriteRead(fd, DRM_NOUVEAU_NVIF, &args, sizeof(args));
   if (ret)
      return ret;

   dev->info.chipset = args.info.chipset;
   dev->info.vram_size_B = args.info.ram_user;

   switch (args.info.platform) {
   case NV_DEVICE_INFO_V0_IGP:
      dev->info.type = NV_DEVICE_TYPE_IGP;
      break;
   case NV_DEVICE_INFO_V0_SOC:
      dev->info.type = NV_DEVICE_TYPE_SOC;
      break;
   case NV_DEVICE_INFO_V0_PCI:
   case NV_DEVICE_INFO_V0_AGP:
   case NV_DEVICE_INFO_V0_PCIE:
   default:
      dev->info.type = NV_DEVICE_TYPE_DIS;
      break;
   }

   STATIC_ASSERT(sizeof(dev->info.device_name) >= sizeof(args.info.name));
   memcpy(dev->info.device_name, args.info.name, sizeof(args.info.name));

   STATIC_ASSERT(sizeof(dev->info.chipset_name) >= sizeof(args.info.chip));
   memcpy(dev->info.chipset_name, args.info.chip, sizeof(args.info.chip));

   return 0;
}

struct nouveau_ws_device *
nouveau_ws_device_new(drmDevicePtr drm_device)
{
   const char *path = drm_device->nodes[DRM_NODE_RENDER];
   struct nouveau_ws_device *device = CALLOC_STRUCT(nouveau_ws_device);
   uint64_t value = 0;
   drmVersionPtr ver = NULL;

   int fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0)
      goto out_open;

   ver = drmGetVersion(fd);
   if (!ver)
      goto out_err;

   if (strncmp("nouveau", ver->name, ver->name_len) != 0) {
      fprintf(stderr,
              "DRM kernel driver '%.*s' in use. NVK requires nouveau.\n",
              ver->name_len, ver->name);
      goto out_err;
   }

   uint32_t version =
      ver->version_major << 24 |
      ver->version_minor << 8  |
      ver->version_patchlevel;
   drmFreeVersion(ver);
   ver = NULL;

   if (version < 0x01000301)
      goto out_err;

   device->nouveau_version = version;

   const uint64_t KERN = NOUVEAU_WS_DEVICE_KERNEL_RESERVATION_START;
   const uint64_t TOP = 1ull << 40;
   struct drm_nouveau_vm_init vminit = { KERN, TOP-KERN };
   int ret = drmCommandWrite(fd, DRM_NOUVEAU_VM_INIT, &vminit, sizeof(vminit));
   if (ret == 0)
      device->has_vm_bind = true;

   if (nouveau_ws_device_alloc(fd, device))
      goto out_err;

   if (nouveau_ws_param(fd, NOUVEAU_GETPARAM_PCI_DEVICE, &value))
      goto out_err;

   device->info.device_id = value;

   if (nouveau_ws_device_info(fd, device))
      goto out_err;

   const char *name;
   if (drm_device->bustype == DRM_BUS_PCI) {
      assert(device->info.type != NV_DEVICE_TYPE_SOC);
      assert(device->info.device_id == drm_device->deviceinfo.pci->device_id);

      device->info.pci.domain       = drm_device->businfo.pci->domain;
      device->info.pci.bus          = drm_device->businfo.pci->bus;
      device->info.pci.dev          = drm_device->businfo.pci->dev;
      device->info.pci.func         = drm_device->businfo.pci->func;
      device->info.pci.revision_id  = drm_device->deviceinfo.pci->revision_id;

      name = name_for_chip(drm_device->deviceinfo.pci->device_id,
                           drm_device->deviceinfo.pci->subdevice_id,
                           drm_device->deviceinfo.pci->subvendor_id);
   } else {
      name = name_for_chip(device->info.device_id, 0, 0);
   }

   if (name != NULL) {
      size_t end = sizeof(device->info.device_name) - 1;
      strncpy(device->info.device_name, name, end);
      device->info.device_name[end] = 0;
   }

   device->fd = fd;

   if (nouveau_ws_param(fd, NOUVEAU_GETPARAM_EXEC_PUSH_MAX, &value))
      device->max_push = NOUVEAU_GEM_MAX_PUSH;
   else
      device->max_push = value;

   if (drm_device->bustype == DRM_BUS_PCI &&
       !nouveau_ws_param(fd, NOUVEAU_GETPARAM_VRAM_BAR_SIZE, &value))
      device->info.bar_size_B = value;

   if (nouveau_ws_param(fd, NOUVEAU_GETPARAM_GRAPH_UNITS, &value))
      goto out_err;

   device->info.gpc_count = (value >> 0) & 0x000000ff;
   device->info.tpc_count = (value >> 8) & 0x0000ffff;

   struct nouveau_ws_context *tmp_ctx;
   if (nouveau_ws_context_create(device, ~0, &tmp_ctx))
      goto out_err;

   device->info.sm = sm_for_chipset(device->info.chipset);
   device->info.cls_copy = tmp_ctx->copy.cls;
   device->info.cls_eng2d = tmp_ctx->eng2d.cls;
   device->info.cls_eng3d = tmp_ctx->eng3d.cls;
   device->info.cls_m2mf = tmp_ctx->m2mf.cls;
   device->info.cls_compute = tmp_ctx->compute.cls;

   // for now we hardcode those values, but in the future Nouveau could provide that information to
   // us instead.
   device->info.max_warps_per_mp = max_warps_per_mp_for_sm(device->info.sm);
   device->info.mp_per_tpc = mp_per_tpc_for_chipset(device->info.chipset);

   init_shared_mem_sizes(&device->info);

   nouveau_ws_context_destroy(tmp_ctx);

   simple_mtx_init(&device->bos_lock, mtx_plain);
   device->bos = _mesa_pointer_hash_table_create(NULL);

   return device;

out_err:
   if (ver)
      drmFreeVersion(ver);
out_open:
   FREE(device);
   close(fd);
   return NULL;
}

void
nouveau_ws_device_destroy(struct nouveau_ws_device *device)
{
   if (!device)
      return;

   _mesa_hash_table_destroy(device->bos, NULL);
   simple_mtx_destroy(&device->bos_lock);

   close(device->fd);
   FREE(device);
}

uint64_t
nouveau_ws_device_vram_used(struct nouveau_ws_device *device)
{
   /* On Tegra, we don't have VRAM */
   if (device->info.type == NV_DEVICE_TYPE_SOC)
      return 0;

   uint64_t used = 0;
   if (nouveau_ws_param(device->fd, NOUVEAU_GETPARAM_VRAM_USED, &used))
      return 0;

   /* Zero memory used would be very strange given that it includes kernel
    * internal allocations.
    */
   assert(used > 0);

   return used;
}

uint64_t
nouveau_ws_device_timestamp(struct nouveau_ws_device *device)
{
   uint64_t timestamp = 0;
   if (nouveau_ws_param(device->fd, NOUVEAU_GETPARAM_PTIMER_TIME, &timestamp))
      return 0;

   return timestamp;
}

bool
nouveau_ws_device_has_tiled_bo(struct nouveau_ws_device *device)
{
   uint64_t has = 0;
   if (nouveau_ws_param(device->fd, NOUVEAU_GETPARAM_HAS_VMA_TILEMODE, &has))
      return false;

   return has != 0;
}
