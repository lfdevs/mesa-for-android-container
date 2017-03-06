/*
 * Copyright © 2009 Corbin Simpson <MostAwesomeDude@gmail.com>
 * Copyright © 2009 Joakim Sindholt <opensource@zhasha.com>
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */
/*
 * Authors:
 *      Marek Olšák <maraeo@gmail.com>
 */

#include "amdgpu_cs.h"
#include "amdgpu_public.h"

#include "util/u_hash_table.h"
#include <amdgpu_drm.h>
#include <xf86drm.h>
#include <stdio.h>
#include <sys/stat.h>
#include "amd/common/amdgpu_id.h"

#define CIK_TILE_MODE_COLOR_2D			14

#define CIK__GB_TILE_MODE__PIPE_CONFIG(x)        (((x) >> 6) & 0x1f)
#define     CIK__PIPE_CONFIG__ADDR_SURF_P2               0
#define     CIK__PIPE_CONFIG__ADDR_SURF_P4_8x16          4
#define     CIK__PIPE_CONFIG__ADDR_SURF_P4_16x16         5
#define     CIK__PIPE_CONFIG__ADDR_SURF_P4_16x32         6
#define     CIK__PIPE_CONFIG__ADDR_SURF_P4_32x32         7
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_16x16_8x16    8
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_16x32_8x16    9
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_8x16    10
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_16x32_16x16   11
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_16x16   12
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_16x32   13
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_32x64_32x32   14
#define     CIK__PIPE_CONFIG__ADDR_SURF_P16_32X32_8X16   16
#define     CIK__PIPE_CONFIG__ADDR_SURF_P16_32X32_16X16  17

#ifndef AMDGPU_INFO_NUM_EVICTIONS
#define AMDGPU_INFO_NUM_EVICTIONS		0x18
#endif

static struct util_hash_table *dev_tab = NULL;
pipe_static_mutex(dev_tab_mutex);

static unsigned cik_get_num_tile_pipes(struct amdgpu_gpu_info *info)
{
   unsigned mode2d = info->gb_tile_mode[CIK_TILE_MODE_COLOR_2D];

   switch (CIK__GB_TILE_MODE__PIPE_CONFIG(mode2d)) {
   case CIK__PIPE_CONFIG__ADDR_SURF_P2:
       return 2;
   case CIK__PIPE_CONFIG__ADDR_SURF_P4_8x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P4_16x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P4_16x32:
   case CIK__PIPE_CONFIG__ADDR_SURF_P4_32x32:
       return 4;
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_16x16_8x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_16x32_8x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_8x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_16x32_16x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_16x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_16x32:
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_32x64_32x32:
       return 8;
   case CIK__PIPE_CONFIG__ADDR_SURF_P16_32X32_8X16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P16_32X32_16X16:
       return 16;
   default:
       fprintf(stderr, "Invalid CIK pipe configuration, assuming P2\n");
       assert(!"this should never occur");
       return 2;
   }
}

/* Helper function to do the ioctls needed for setup and init. */
static bool do_winsys_init(struct amdgpu_winsys *ws, int fd)
{
   struct amdgpu_buffer_size_alignments alignment_info = {};
   struct amdgpu_heap_info vram, gtt;
   struct drm_amdgpu_info_hw_ip dma = {}, uvd = {}, vce = {};
   uint32_t vce_version = 0, vce_feature = 0, uvd_version = 0, uvd_feature = 0;
   uint32_t unused_feature;
   int r, i, j;
   drmDevicePtr devinfo;

   /* Get PCI info. */
   r = drmGetDevice(fd, &devinfo);
   if (r) {
      fprintf(stderr, "amdgpu: drmGetDevice failed.\n");
      goto fail;
   }
   ws->info.pci_domain = devinfo->businfo.pci->domain;
   ws->info.pci_bus = devinfo->businfo.pci->bus;
   ws->info.pci_dev = devinfo->businfo.pci->dev;
   ws->info.pci_func = devinfo->businfo.pci->func;
   drmFreeDevice(&devinfo);

   /* Query hardware and driver information. */
   r = amdgpu_query_gpu_info(ws->dev, &ws->amdinfo);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_gpu_info failed.\n");
      goto fail;
   }

   r = amdgpu_query_buffer_size_alignment(ws->dev, &alignment_info);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_buffer_size_alignment failed.\n");
      goto fail;
   }

   r = amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_VRAM, 0, &vram);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_heap_info(vram) failed.\n");
      goto fail;
   }

   r = amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_GTT, 0, &gtt);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_heap_info(gtt) failed.\n");
      goto fail;
   }

   r = amdgpu_query_hw_ip_info(ws->dev, AMDGPU_HW_IP_DMA, 0, &dma);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(dma) failed.\n");
      goto fail;
   }

   r = amdgpu_query_hw_ip_info(ws->dev, AMDGPU_HW_IP_UVD, 0, &uvd);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(uvd) failed.\n");
      goto fail;
   }

   r = amdgpu_query_firmware_version(ws->dev, AMDGPU_INFO_FW_GFX_ME, 0, 0,
				     &ws->info.me_fw_version, &unused_feature);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(me) failed.\n");
      goto fail;
   }

   r = amdgpu_query_firmware_version(ws->dev, AMDGPU_INFO_FW_GFX_PFP, 0, 0,
				     &ws->info.pfp_fw_version, &unused_feature);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(pfp) failed.\n");
      goto fail;
   }

   r = amdgpu_query_firmware_version(ws->dev, AMDGPU_INFO_FW_GFX_CE, 0, 0,
				     &ws->info.ce_fw_version, &unused_feature);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(ce) failed.\n");
      goto fail;
   }

   r = amdgpu_query_firmware_version(ws->dev, AMDGPU_INFO_FW_UVD, 0, 0,
				     &uvd_version, &uvd_feature);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(uvd) failed.\n");
      goto fail;
   }

   r = amdgpu_query_hw_ip_info(ws->dev, AMDGPU_HW_IP_VCE, 0, &vce);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(vce) failed.\n");
      goto fail;
   }

   r = amdgpu_query_firmware_version(ws->dev, AMDGPU_INFO_FW_VCE, 0, 0,
				     &vce_version, &vce_feature);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(vce) failed.\n");
      goto fail;
   }

   /* Set chip identification. */
   ws->info.pci_id = ws->amdinfo.asic_id; /* TODO: is this correct? */
   ws->info.vce_harvest_config = ws->amdinfo.vce_harvest_config;

   switch (ws->info.pci_id) {
#define CHIPSET(pci_id, name, cfamily) case pci_id: ws->info.family = CHIP_##cfamily; break;
#include "pci_ids/radeonsi_pci_ids.h"
#undef CHIPSET

   default:
      fprintf(stderr, "amdgpu: Invalid PCI ID.\n");
      goto fail;
   }

   if (ws->info.family >= CHIP_TONGA)
      ws->info.chip_class = VI;
   else if (ws->info.family >= CHIP_BONAIRE)
      ws->info.chip_class = CIK;
   else if (ws->info.family >= CHIP_TAHITI)
      ws->info.chip_class = SI;
   else {
      fprintf(stderr, "amdgpu: Unknown family.\n");
      goto fail;
   }

   /* LLVM 3.6.1 is required for VI. */
   if (ws->info.chip_class >= VI &&
       HAVE_LLVM == 0x0306 && MESA_LLVM_VERSION_PATCH < 1) {
      fprintf(stderr, "amdgpu: LLVM 3.6.1 is required, got LLVM %i.%i.%i\n",
              HAVE_LLVM >> 8, HAVE_LLVM & 255, MESA_LLVM_VERSION_PATCH);
      goto fail;
   }

   /* family and rev_id are for addrlib */
   switch (ws->info.family) {
   case CHIP_TAHITI:
      ws->family = FAMILY_SI;
      ws->rev_id = SI_TAHITI_P_A0;
      break;
   case CHIP_PITCAIRN:
      ws->family = FAMILY_SI;
      ws->rev_id = SI_PITCAIRN_PM_A0;
      break;
   case CHIP_VERDE:
      ws->family = FAMILY_SI;
      ws->rev_id = SI_CAPEVERDE_M_A0;
      break;
   case CHIP_OLAND:
      ws->family = FAMILY_SI;
      ws->rev_id = SI_OLAND_M_A0;
      break;
   case CHIP_HAINAN:
      ws->family = FAMILY_SI;
      ws->rev_id = SI_HAINAN_V_A0;
      break;
   case CHIP_BONAIRE:
      ws->family = FAMILY_CI;
      ws->rev_id = CI_BONAIRE_M_A0;
      break;
   case CHIP_KAVERI:
      ws->family = FAMILY_KV;
      ws->rev_id = KV_SPECTRE_A0;
      break;
   case CHIP_KABINI:
      ws->family = FAMILY_KV;
      ws->rev_id = KB_KALINDI_A0;
      break;
   case CHIP_HAWAII:
      ws->family = FAMILY_CI;
      ws->rev_id = CI_HAWAII_P_A0;
      break;
   case CHIP_MULLINS:
      ws->family = FAMILY_KV;
      ws->rev_id = ML_GODAVARI_A0;
      break;
   case CHIP_TONGA:
      ws->family = FAMILY_VI;
      ws->rev_id = VI_TONGA_P_A0;
      break;
   case CHIP_ICELAND:
      ws->family = FAMILY_VI;
      ws->rev_id = VI_ICELAND_M_A0;
      break;
   case CHIP_CARRIZO:
      ws->family = FAMILY_CZ;
      ws->rev_id = CARRIZO_A0;
      break;
   case CHIP_STONEY:
      ws->family = FAMILY_CZ;
      ws->rev_id = STONEY_A0;
      break;
   case CHIP_FIJI:
      ws->family = FAMILY_VI;
      ws->rev_id = VI_FIJI_P_A0;
      break;
   case CHIP_POLARIS10:
      ws->family = FAMILY_VI;
      ws->rev_id = VI_POLARIS10_P_A0;
      break;
   case CHIP_POLARIS11:
      ws->family = FAMILY_VI;
      ws->rev_id = VI_POLARIS11_M_A0;
      break;
   case CHIP_POLARIS12:
      ws->family = FAMILY_VI;
      ws->rev_id = VI_POLARIS12_V_A0;
      break;
   default:
      fprintf(stderr, "amdgpu: Unknown family.\n");
      goto fail;
   }

   ws->addrlib = amdgpu_addr_create(ws);
   if (!ws->addrlib) {
      fprintf(stderr, "amdgpu: Cannot create addrlib.\n");
      goto fail;
   }

   /* Set which chips have dedicated VRAM. */
   ws->info.has_dedicated_vram =
      !(ws->amdinfo.ids_flags & AMDGPU_IDS_FLAGS_FUSION);

   /* Set hardware information. */
   ws->info.gart_size = gtt.heap_size;
   ws->info.vram_size = vram.heap_size;
   /* The kernel can split large buffers in VRAM but not in GTT, so large
    * allocations can fail or cause buffer movement failures in the kernel.
    */
   ws->info.max_alloc_size = MIN2(ws->info.vram_size * 0.9, ws->info.gart_size * 0.7);
   /* convert the shader clock from KHz to MHz */
   ws->info.max_shader_clock = ws->amdinfo.max_engine_clk / 1000;
   ws->info.max_se = ws->amdinfo.num_shader_engines;
   ws->info.max_sh_per_se = ws->amdinfo.num_shader_arrays_per_engine;
   ws->info.has_uvd = uvd.available_rings != 0;
   ws->info.uvd_fw_version =
         uvd.available_rings ? uvd_version : 0;
   ws->info.vce_fw_version =
         vce.available_rings ? vce_version : 0;
   ws->info.has_userptr = true;
   ws->info.num_render_backends = ws->amdinfo.rb_pipes;
   ws->info.clock_crystal_freq = ws->amdinfo.gpu_counter_freq;
   ws->info.num_tile_pipes = cik_get_num_tile_pipes(&ws->amdinfo);
   ws->info.pipe_interleave_bytes = 256 << ((ws->amdinfo.gb_addr_cfg >> 4) & 0x7);
   ws->info.has_virtual_memory = true;
   ws->info.has_sdma = dma.available_rings != 0;

   /* Get the number of good compute units. */
   ws->info.num_good_compute_units = 0;
   for (i = 0; i < ws->info.max_se; i++)
      for (j = 0; j < ws->info.max_sh_per_se; j++)
         ws->info.num_good_compute_units +=
            util_bitcount(ws->amdinfo.cu_bitmap[i][j]);

   memcpy(ws->info.si_tile_mode_array, ws->amdinfo.gb_tile_mode,
          sizeof(ws->amdinfo.gb_tile_mode));
   ws->info.enabled_rb_mask = ws->amdinfo.enabled_rb_pipes_mask;

   memcpy(ws->info.cik_macrotile_mode_array, ws->amdinfo.gb_macro_tile_mode,
          sizeof(ws->amdinfo.gb_macro_tile_mode));

   ws->info.gart_page_size = alignment_info.size_remote;

   if (ws->info.chip_class == SI)
      ws->info.gfx_ib_pad_with_type2 = TRUE;

   ws->check_vm = strstr(debug_get_option("R600_DEBUG", ""), "check_vm") != NULL;

   return true;

fail:
   if (ws->addrlib)
      AddrDestroy(ws->addrlib);
   amdgpu_device_deinitialize(ws->dev);
   ws->dev = NULL;
   return false;
}

static void do_winsys_deinit(struct amdgpu_winsys *ws)
{
   AddrDestroy(ws->addrlib);
   amdgpu_device_deinitialize(ws->dev);
}

static void amdgpu_winsys_destroy(struct radeon_winsys *rws)
{
   struct amdgpu_winsys *ws = (struct amdgpu_winsys*)rws;

   if (util_queue_is_initialized(&ws->cs_queue))
      util_queue_destroy(&ws->cs_queue);

   pipe_mutex_destroy(ws->bo_fence_lock);
   pb_slabs_deinit(&ws->bo_slabs);
   pb_cache_deinit(&ws->bo_cache);
   pipe_mutex_destroy(ws->global_bo_list_lock);
   do_winsys_deinit(ws);
   FREE(rws);
}

static void amdgpu_winsys_query_info(struct radeon_winsys *rws,
                                     struct radeon_info *info)
{
   *info = ((struct amdgpu_winsys *)rws)->info;
}

static bool amdgpu_cs_request_feature(struct radeon_winsys_cs *rcs,
                                      enum radeon_feature_id fid,
                                      bool enable)
{
   return false;
}

static uint64_t amdgpu_query_value(struct radeon_winsys *rws,
                                   enum radeon_value_id value)
{
   struct amdgpu_winsys *ws = (struct amdgpu_winsys*)rws;
   struct amdgpu_heap_info heap;
   uint64_t retval = 0;

   switch (value) {
   case RADEON_REQUESTED_VRAM_MEMORY:
      return ws->allocated_vram;
   case RADEON_REQUESTED_GTT_MEMORY:
      return ws->allocated_gtt;
   case RADEON_MAPPED_VRAM:
      return ws->mapped_vram;
   case RADEON_MAPPED_GTT:
      return ws->mapped_gtt;
   case RADEON_BUFFER_WAIT_TIME_NS:
      return ws->buffer_wait_time;
   case RADEON_TIMESTAMP:
      amdgpu_query_info(ws->dev, AMDGPU_INFO_TIMESTAMP, 8, &retval);
      return retval;
   case RADEON_NUM_GFX_IBS:
      return ws->num_gfx_IBs;
   case RADEON_NUM_SDMA_IBS:
      return ws->num_sdma_IBs;
   case RADEON_NUM_BYTES_MOVED:
      amdgpu_query_info(ws->dev, AMDGPU_INFO_NUM_BYTES_MOVED, 8, &retval);
      return retval;
   case RADEON_NUM_EVICTIONS:
      amdgpu_query_info(ws->dev, AMDGPU_INFO_NUM_EVICTIONS, 8, &retval);
      return retval;
   case RADEON_VRAM_USAGE:
      amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_VRAM, 0, &heap);
      return heap.heap_usage;
   case RADEON_GTT_USAGE:
      amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_GTT, 0, &heap);
      return heap.heap_usage;
   case RADEON_GPU_TEMPERATURE:
   case RADEON_CURRENT_SCLK:
   case RADEON_CURRENT_MCLK:
      return 0;
   case RADEON_GPU_RESET_COUNTER:
      assert(0);
      return 0;
   }
   return 0;
}

static bool amdgpu_read_registers(struct radeon_winsys *rws,
                                  unsigned reg_offset,
                                  unsigned num_registers, uint32_t *out)
{
   struct amdgpu_winsys *ws = (struct amdgpu_winsys*)rws;

   return amdgpu_read_mm_registers(ws->dev, reg_offset / 4, num_registers,
                                   0xffffffff, 0, out) == 0;
}

static unsigned hash_dev(void *key)
{
#if defined(PIPE_ARCH_X86_64)
   return pointer_to_intptr(key) ^ (pointer_to_intptr(key) >> 32);
#else
   return pointer_to_intptr(key);
#endif
}

static int compare_dev(void *key1, void *key2)
{
   return key1 != key2;
}

static bool amdgpu_winsys_unref(struct radeon_winsys *rws)
{
   struct amdgpu_winsys *ws = (struct amdgpu_winsys*)rws;
   bool destroy;

   /* When the reference counter drops to zero, remove the device pointer
    * from the table.
    * This must happen while the mutex is locked, so that
    * amdgpu_winsys_create in another thread doesn't get the winsys
    * from the table when the counter drops to 0. */
   pipe_mutex_lock(dev_tab_mutex);

   destroy = pipe_reference(&ws->reference, NULL);
   if (destroy && dev_tab)
      util_hash_table_remove(dev_tab, ws->dev);

   pipe_mutex_unlock(dev_tab_mutex);
   return destroy;
}

PUBLIC struct radeon_winsys *
amdgpu_winsys_create(int fd, radeon_screen_create_t screen_create)
{
   struct amdgpu_winsys *ws;
   drmVersionPtr version = drmGetVersion(fd);
   amdgpu_device_handle dev;
   uint32_t drm_major, drm_minor, r;

   /* The DRM driver version of amdgpu is 3.x.x. */
   if (version->version_major != 3) {
      drmFreeVersion(version);
      return NULL;
   }
   drmFreeVersion(version);

   /* Look up the winsys from the dev table. */
   pipe_mutex_lock(dev_tab_mutex);
   if (!dev_tab)
      dev_tab = util_hash_table_create(hash_dev, compare_dev);

   /* Initialize the amdgpu device. This should always return the same pointer
    * for the same fd. */
   r = amdgpu_device_initialize(fd, &drm_major, &drm_minor, &dev);
   if (r) {
      pipe_mutex_unlock(dev_tab_mutex);
      fprintf(stderr, "amdgpu: amdgpu_device_initialize failed.\n");
      return NULL;
   }

   /* Lookup a winsys if we have already created one for this device. */
   ws = util_hash_table_get(dev_tab, dev);
   if (ws) {
      pipe_reference(NULL, &ws->reference);
      pipe_mutex_unlock(dev_tab_mutex);
      return &ws->base;
   }

   /* Create a new winsys. */
   ws = CALLOC_STRUCT(amdgpu_winsys);
   if (!ws)
      goto fail;

   ws->dev = dev;
   ws->info.drm_major = drm_major;
   ws->info.drm_minor = drm_minor;

   if (!do_winsys_init(ws, fd))
      goto fail_alloc;

   /* Create managers. */
   pb_cache_init(&ws->bo_cache, 500000, ws->check_vm ? 1.0f : 2.0f, 0,
                 (ws->info.vram_size + ws->info.gart_size) / 8,
                 amdgpu_bo_destroy, amdgpu_bo_can_reclaim);

   if (!pb_slabs_init(&ws->bo_slabs,
                      AMDGPU_SLAB_MIN_SIZE_LOG2, AMDGPU_SLAB_MAX_SIZE_LOG2,
                      12, /* number of heaps (domain/flags combinations) */
                      ws,
                      amdgpu_bo_can_reclaim_slab,
                      amdgpu_bo_slab_alloc,
                      amdgpu_bo_slab_free))
      goto fail_cache;

   ws->info.min_alloc_size = 1 << AMDGPU_SLAB_MIN_SIZE_LOG2;

   /* init reference */
   pipe_reference_init(&ws->reference, 1);

   /* Set functions. */
   ws->base.unref = amdgpu_winsys_unref;
   ws->base.destroy = amdgpu_winsys_destroy;
   ws->base.query_info = amdgpu_winsys_query_info;
   ws->base.cs_request_feature = amdgpu_cs_request_feature;
   ws->base.query_value = amdgpu_query_value;
   ws->base.read_registers = amdgpu_read_registers;

   amdgpu_bo_init_functions(ws);
   amdgpu_cs_init_functions(ws);
   amdgpu_surface_init_functions(ws);

   LIST_INITHEAD(&ws->global_bo_list);
   pipe_mutex_init(ws->global_bo_list_lock);
   pipe_mutex_init(ws->bo_fence_lock);

   if (!util_queue_init(&ws->cs_queue, "amdgpu_cs", 8, 1)) {
      amdgpu_winsys_destroy(&ws->base);
      pipe_mutex_unlock(dev_tab_mutex);
      return NULL;
   }

   /* Create the screen at the end. The winsys must be initialized
    * completely.
    *
    * Alternatively, we could create the screen based on "ws->gen"
    * and link all drivers into one binary blob. */
   ws->base.screen = screen_create(&ws->base);
   if (!ws->base.screen) {
      amdgpu_winsys_destroy(&ws->base);
      pipe_mutex_unlock(dev_tab_mutex);
      return NULL;
   }

   util_hash_table_set(dev_tab, dev, ws);

   /* We must unlock the mutex once the winsys is fully initialized, so that
    * other threads attempting to create the winsys from the same fd will
    * get a fully initialized winsys and not just half-way initialized. */
   pipe_mutex_unlock(dev_tab_mutex);

   return &ws->base;

fail_cache:
   pb_cache_deinit(&ws->bo_cache);
   do_winsys_deinit(ws);
fail_alloc:
   FREE(ws);
fail:
   pipe_mutex_unlock(dev_tab_mutex);
   return NULL;
}
