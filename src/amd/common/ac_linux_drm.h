/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_LINUX_DRM_H
#define AC_LINUX_DRM_H

#include <stdbool.h>
#include <stdint.h>

#if !defined(_WIN32)
#include "drm-uapi/amdgpu_drm.h"
#include "amdgpu.h"
#else
#define DRM_CAP_ADDFB2_MODIFIERS 0x10
#define DRM_CAP_SYNCOBJ 0x13
#define DRM_CAP_SYNCOBJ_TIMELINE 0x14
#define AMDGPU_GEM_DOMAIN_GTT 0x2
#define AMDGPU_GEM_DOMAIN_VRAM 0x4
#define AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED (1 << 0)
#define AMDGPU_GEM_CREATE_ENCRYPTED (1 << 10)
#define AMDGPU_HW_IP_GFX 0
#define AMDGPU_HW_IP_COMPUTE 1
#define AMDGPU_HW_IP_DMA 2
#define AMDGPU_HW_IP_UVD 3
#define AMDGPU_HW_IP_VCE 4
#define AMDGPU_HW_IP_UVD_ENC 5
#define AMDGPU_HW_IP_VCN_DEC 6
#define AMDGPU_HW_IP_VCN_ENC 7
#define AMDGPU_HW_IP_VCN_JPEG 8
#define AMDGPU_HW_IP_VPE 9
#define AMDGPU_IDS_FLAGS_FUSION 0x1
#define AMDGPU_IDS_FLAGS_PREEMPTION 0x2
#define AMDGPU_IDS_FLAGS_TMZ 0x4
#define AMDGPU_IDS_FLAGS_CONFORMANT_TRUNC_COORD 0x8
#define AMDGPU_INFO_FW_VCE 0x1
#define AMDGPU_INFO_FW_UVD 0x2
#define AMDGPU_INFO_FW_GFX_ME 0x04
#define AMDGPU_INFO_FW_GFX_PFP 0x05
#define AMDGPU_INFO_FW_GFX_CE 0x06
#define AMDGPU_INFO_FW_VCN 0x0e
#define AMDGPU_INFO_DEV_INFO 0x16
#define AMDGPU_INFO_MEMORY 0x19
#define AMDGPU_INFO_VIDEO_CAPS_DECODE 0
#define AMDGPU_INFO_VIDEO_CAPS_ENCODE 1
#define AMDGPU_INFO_FW_GFX_MEC 0x08
#define AMDGPU_INFO_MAX_IBS 0x22

#define AMDGPU_VRAM_TYPE_UNKNOWN 0
#define AMDGPU_VRAM_TYPE_GDDR1 1
#define AMDGPU_VRAM_TYPE_DDR2  2
#define AMDGPU_VRAM_TYPE_GDDR3 3
#define AMDGPU_VRAM_TYPE_GDDR4 4
#define AMDGPU_VRAM_TYPE_GDDR5 5
#define AMDGPU_VRAM_TYPE_HBM   6
#define AMDGPU_VRAM_TYPE_DDR3  7
#define AMDGPU_VRAM_TYPE_DDR4  8
#define AMDGPU_VRAM_TYPE_GDDR6 9
#define AMDGPU_VRAM_TYPE_DDR5  10
#define AMDGPU_VRAM_TYPE_LPDDR4 11
#define AMDGPU_VRAM_TYPE_LPDDR5 12

#define AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG2 0
#define AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4 1
#define AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VC1 2
#define AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC 3
#define AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC 4
#define AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG 5
#define AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9 6
#define AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1 7
#define AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_COUNT 8

struct drm_amdgpu_heap_info {
   uint64_t total_heap_size;
};
struct drm_amdgpu_memory_info {
   struct drm_amdgpu_heap_info vram;
   struct drm_amdgpu_heap_info cpu_accessible_vram;
   struct drm_amdgpu_heap_info gtt;
};
struct drm_amdgpu_info_device {
   /** PCI Device ID */
   uint32_t device_id;
   /** Internal chip revision: A0, A1, etc.) */
   uint32_t chip_rev;
   uint32_t external_rev;
   /** Revision id in PCI Config space */
   uint32_t pci_rev;
   uint32_t family;
   uint32_t num_shader_engines;
   uint32_t num_shader_arrays_per_engine;
   /* in KHz */
   uint32_t gpu_counter_freq;
   uint64_t max_engine_clock;
   uint64_t max_memory_clock;
   /* cu information */
   uint32_t cu_active_number;
   /* NOTE: cu_ao_mask is INVALID, DON'T use it */
   uint32_t cu_ao_mask;
   uint32_t cu_bitmap[4][4];
   /** Render backend pipe mask. One render backend is CB+DB. */
   uint32_t enabled_rb_pipes_mask;
   uint32_t num_rb_pipes;
   uint32_t num_hw_gfx_contexts;
   /* PCIe version (the smaller of the GPU and the CPU/motherboard) */
   uint32_t pcie_gen;
   uint64_t ids_flags;
   /** Starting virtual address for UMDs. */
   uint64_t virtual_address_offset;
   /** The maximum virtual address */
   uint64_t virtual_address_max;
   /** Required alignment of virtual addresses. */
   uint32_t virtual_address_alignment;
   /** Page table entry - fragment size */
   uint32_t pte_fragment_size;
   uint32_t gart_page_size;
   /** constant engine ram size*/
   uint32_t ce_ram_size;
   /** video memory type info*/
   uint32_t vram_type;
   /** video memory bit width*/
   uint32_t vram_bit_width;
   /* vce harvesting instance */
   uint32_t vce_harvest_config;
   /* gfx double offchip LDS buffers */
   uint32_t gc_double_offchip_lds_buf;
   /* NGG Primitive Buffer */
   uint64_t prim_buf_gpu_addr;
   /* NGG Position Buffer */
   uint64_t pos_buf_gpu_addr;
   /* NGG Control Sideband */
   uint64_t cntl_sb_buf_gpu_addr;
   /* NGG Parameter Cache */
   uint64_t param_buf_gpu_addr;
   uint32_t prim_buf_size;
   uint32_t pos_buf_size;
   uint32_t cntl_sb_buf_size;
   uint32_t param_buf_size;
   /* wavefront size*/
   uint32_t wave_front_size;
   /* shader visible vgprs*/
   uint32_t num_shader_visible_vgprs;
   /* CU per shader array*/
   uint32_t num_cu_per_sh;
   /* number of tcc blocks*/
   uint32_t num_tcc_blocks;
   /* gs vgt table depth*/
   uint32_t gs_vgt_table_depth;
   /* gs primitive buffer depth*/
   uint32_t gs_prim_buffer_depth;
   /* max gs wavefront per vgt*/
   uint32_t max_gs_waves_per_vgt;
   /* PCIe number of lanes (the smaller of the GPU and the CPU/motherboard) */
   uint32_t pcie_num_lanes;
   /* always on cu bitmap */
   uint32_t cu_ao_bitmap[4][4];
   /** Starting high virtual address for UMDs. */
   uint64_t high_va_offset;
   /** The maximum high virtual address */
   uint64_t high_va_max;
   /* gfx10 pa_sc_tile_steering_override */
   uint32_t pa_sc_tile_steering_override;
   /* disabled TCCs */
   uint64_t tcc_disabled_mask;
   uint64_t min_engine_clock;
   uint64_t min_memory_clock;
   /* The following fields are only set on gfx11+, older chips set 0. */
   uint32_t tcp_cache_size;       /* AKA GL0, VMEM cache */
   uint32_t num_sqc_per_wgp;
   uint32_t sqc_data_cache_size;  /* AKA SMEM cache */
   uint32_t sqc_inst_cache_size;
   uint32_t gl1c_cache_size;
   uint32_t gl2c_cache_size;
   uint64_t mall_size;            /* AKA infinity cache */
   /* high 32 bits of the rb pipes mask */
   uint32_t enabled_rb_pipes_mask_hi;
   /* shadow area size for gfx11 */
   uint32_t shadow_size;
   /* shadow area base virtual alignment for gfx11 */
   uint32_t shadow_alignment;
   /* context save area size for gfx11 */
   uint32_t csa_size;
   /* context save area base virtual alignment for gfx11 */
   uint32_t csa_alignment;
   /* Userq IP mask (1 << AMDGPU_HW_IP_*) */
   uint32_t userq_ip_mask;
   uint32_t pad;
};
struct drm_amdgpu_info_hw_ip {
   uint32_t hw_ip_version_major;
   uint32_t hw_ip_version_minor;
   uint32_t ib_start_alignment;
   uint32_t ib_size_alignment;
   uint32_t available_rings;
   uint32_t ip_discovery_version;
};

struct drm_amdgpu_info_uq_metadata_gfx {
   /* shadow area size for gfx11 */
   uint32_t shadow_size;
   /* shadow area base virtual alignment for gfx11 */
   uint32_t shadow_alignment;
   /* context save area size for gfx11 */
   uint32_t csa_size;
   /* context save area base virtual alignment for gfx11 */
   uint32_t csa_alignment;
};

struct drm_amdgpu_info_uq_metadata_compute {
   /* EOP size for gfx11 */
   uint32_t eop_size;
   /* EOP base virtual alignment for gfx11 */
   uint32_t eop_alignment;
};

struct drm_amdgpu_info_uq_metadata_sdma {
   /* context save area size for sdma6 */
   uint32_t csa_size;
   /* context save area base virtual alignment for sdma6 */
   uint32_t csa_alignment;
};

struct drm_amdgpu_info_uq_metadata {
   union {
      struct drm_amdgpu_info_uq_metadata_gfx gfx;
      struct drm_amdgpu_info_uq_metadata_compute compute;
      struct drm_amdgpu_info_uq_metadata_sdma sdma;
   };
};

typedef struct _drmPciBusInfo {
   uint16_t domain;
   uint8_t bus;
   uint8_t dev;
   uint8_t func;
} drmPciBusInfo, *drmPciBusInfoPtr;
typedef struct _drmDevice {
   union {
      drmPciBusInfoPtr pci;
   } businfo;
} drmDevice, *drmDevicePtr;

/**
 * Enum describing possible handle types
 *
 * \sa amdgpu_bo_import, amdgpu_bo_export
 *
*/
enum amdgpu_bo_handle_type {
   /** GEM flink name (needs DRM authentication, used by DRI2) */
   amdgpu_bo_handle_type_gem_flink_name = 0,

   /** KMS handle which is used by all driver ioctls */
   amdgpu_bo_handle_type_kms = 1,

   /** DMA-buf fd handle */
   amdgpu_bo_handle_type_dma_buf_fd = 2,

   /** Deprecated in favour of and same behaviour as
   * amdgpu_bo_handle_type_kms, use that instead of this
   */
   amdgpu_bo_handle_type_kms_noimport = 3,
};

/** Define known types of GPU VM VA ranges */
enum amdgpu_gpu_va_range
{
   /** Allocate from "normal"/general range */
   amdgpu_gpu_va_range_general = 0
};

enum amdgpu_sw_info {
   amdgpu_sw_info_address32_hi = 0,
};

struct amdgpu_bo_alloc_request {
   uint64_t alloc_size;
   uint64_t phys_alignment;
   uint32_t preferred_heap;
   uint64_t flags;
};

struct amdgpu_gpu_info {
   uint32_t asic_id;
   uint32_t chip_external_rev;
   uint32_t family_id;
   uint64_t ids_flags;
   uint64_t max_engine_clk;
   uint64_t max_memory_clk;
   uint32_t num_shader_engines;
   uint32_t num_shader_arrays_per_engine;
   uint32_t rb_pipes;
   uint32_t enabled_rb_pipes_mask;
   uint32_t gpu_counter_freq;
   uint32_t mc_arb_ramcfg;
   uint32_t gb_addr_cfg;
   uint32_t gb_tile_mode[32];
   uint32_t gb_macro_tile_mode[16];
   uint32_t cu_bitmap[4][4];
   uint32_t vram_type;
   uint32_t vram_bit_width;
   uint32_t ce_ram_size;
   uint32_t vce_harvest_config;
   uint32_t pci_rev_id;
};

struct amdgpu_bo_metadata;
struct amdgpu_bo_info;
struct drm_amdgpu_cs_chunk;
struct drm_amdgpu_cs_chunk_data;
struct amdgpu_heap_info;
struct drm_amdgpu_userq_signal;
struct drm_amdgpu_userq_wait;
struct amdgpu_va;
typedef struct amdgpu_va *amdgpu_va_handle;

#endif /* !defined(_WIN32) */

#ifdef __cplusplus
extern "C" {
#endif

/* All functions are static inline stubs on Windows. */
#ifdef _WIN32
#define PROC static inline
#define TAIL                                                                                       \
   {                                                                                               \
      return -1;                                                                                   \
   }
#define TAILV                                                                                      \
   {                                                                                               \
   }
#define TAILPTR                                                                                    \
   {                                                                                               \
      return NULL;                                                                                 \
   }
#else
#define PROC
#define TAIL
#define TAILV
#define TAILPTR
#endif

struct ac_drm_device;
typedef struct ac_drm_device ac_drm_device;
struct util_sync_provider;
struct radeon_info;

typedef union ac_drm_bo {
#ifdef _WIN32
   void *abo;
#else
   amdgpu_bo_handle abo;
#endif
#ifdef HAVE_AMDGPU_VIRTIO
   struct amdvgpu_bo *vbo;
#endif
} ac_drm_bo;

struct ac_drm_bo_import_result {
   ac_drm_bo bo;
   uint64_t alloc_size;
};


PROC int ac_drm_device_initialize(int fd, bool is_virtio,
                                  uint32_t *major_version, uint32_t *minor_version,
                                  ac_drm_device **device_handle) TAIL;
PROC struct util_sync_provider *ac_drm_device_get_sync_provider(ac_drm_device *dev) TAILPTR;
PROC uintptr_t ac_drm_device_get_cookie(ac_drm_device *dev) TAIL;
PROC void ac_drm_device_deinitialize(ac_drm_device *dev) TAILV;
PROC int ac_drm_device_get_fd(ac_drm_device *dev) TAIL;
PROC int ac_drm_bo_set_metadata(ac_drm_device *dev, uint32_t bo_handle,
                                struct amdgpu_bo_metadata *info) TAIL;
PROC int ac_drm_bo_query_info(ac_drm_device *dev, uint32_t bo_handle, struct amdgpu_bo_info *info) TAIL;
PROC int ac_drm_bo_wait_for_idle(ac_drm_device *dev, ac_drm_bo bo, uint64_t timeout_ns,
                                 bool *busy) TAIL;
PROC int ac_drm_bo_va_op(ac_drm_device *dev, uint32_t bo_handle, uint64_t offset, uint64_t size,
                         uint64_t addr, uint64_t flags, uint32_t ops) TAIL;
PROC int ac_drm_bo_va_op_raw(ac_drm_device *dev, uint32_t bo_handle, uint64_t offset, uint64_t size,
                             uint64_t addr, uint64_t flags, uint32_t ops) TAIL;
PROC int ac_drm_bo_va_op_raw2(ac_drm_device *dev, uint32_t bo_handle, uint64_t offset, uint64_t size,
                              uint64_t addr, uint64_t flags, uint32_t ops,
                              uint32_t vm_timeline_syncobj_out, uint64_t vm_timeline_point,
                              uint64_t input_fence_syncobj_handles,
                              uint32_t num_syncobj_handles) TAIL;
PROC int ac_drm_cs_ctx_create2(ac_drm_device *dev, uint32_t priority, uint32_t *ctx_id) TAIL;
PROC int ac_drm_cs_ctx_free(ac_drm_device *dev, uint32_t ctx_id) TAIL;
PROC int ac_drm_cs_ctx_stable_pstate(ac_drm_device *dev, uint32_t ctx_id, uint32_t op,
                                     uint32_t flags, uint32_t *out_flags) TAIL;
PROC int ac_drm_cs_query_reset_state2(ac_drm_device *dev, uint32_t ctx_id, uint64_t *flags) TAIL;
PROC int ac_drm_cs_query_fence_status(ac_drm_device *dev, uint32_t ctx_id, uint32_t ip_type,
                                      uint32_t ip_instance, uint32_t ring, uint64_t fence_seq_no,
                                      uint64_t timeout_ns, uint64_t flags, uint32_t *expired) TAIL;
PROC int ac_drm_cs_create_syncobj2(ac_drm_device *dev, uint32_t flags, uint32_t *handle) TAIL;
PROC int ac_drm_cs_destroy_syncobj(ac_drm_device *dev, uint32_t handle) TAIL;
PROC int ac_drm_cs_syncobj_wait(ac_drm_device *dev, uint32_t *handles, unsigned num_handles,
                                int64_t timeout_nsec, unsigned flags,
                                uint32_t *first_signaled) TAIL;
PROC int ac_drm_cs_syncobj_query2(ac_drm_device *dev, uint32_t *handles, uint64_t *points,
                                  unsigned num_handles, uint32_t flags) TAIL;
PROC int ac_drm_cs_import_syncobj(ac_drm_device *dev, int shared_fd, uint32_t *handle) TAIL;
PROC int ac_drm_cs_syncobj_export_sync_file(ac_drm_device *dev, uint32_t syncobj,
                                            int *sync_file_fd) TAIL;
PROC int ac_drm_cs_syncobj_import_sync_file(ac_drm_device *dev, uint32_t syncobj, int sync_file_fd) TAIL;
PROC int ac_drm_cs_syncobj_export_sync_file2(ac_drm_device *dev, uint32_t syncobj, uint64_t point,
                                             uint32_t flags, int *sync_file_fd) TAIL;
PROC int ac_drm_cs_syncobj_transfer(ac_drm_device *dev, uint32_t dst_handle, uint64_t dst_point,
                                    uint32_t src_handle, uint64_t src_point, uint32_t flags) TAIL;
PROC int ac_drm_cs_submit_raw2(ac_drm_device *dev, uint32_t ctx_id, uint32_t bo_list_handle,
                               int num_chunks, struct drm_amdgpu_cs_chunk *chunks,
                               uint64_t *seq_no) TAIL;
PROC void ac_drm_cs_chunk_fence_info_to_data(uint32_t bo_handle, uint64_t offset,
                                             struct drm_amdgpu_cs_chunk_data *data) TAILV;
PROC int ac_drm_cs_syncobj_timeline_wait(ac_drm_device *dev, uint32_t *handles, uint64_t *points,
                                         unsigned num_handles, int64_t timeout_nsec, unsigned flags,
                                         uint32_t *first_signaled) TAIL;
PROC int ac_drm_query_info(ac_drm_device *dev, unsigned info_id, unsigned size, void *value) TAIL;
PROC int ac_drm_read_mm_registers(ac_drm_device *dev, unsigned dword_offset, unsigned count,
                                  uint32_t instance, uint32_t flags, uint32_t *values) TAIL;
PROC int ac_drm_query_hw_ip_count(ac_drm_device *dev, unsigned type, uint32_t *count) TAIL;
PROC int ac_drm_query_hw_ip_info(ac_drm_device *dev, unsigned type, unsigned ip_instance,
                                 struct drm_amdgpu_info_hw_ip *info) TAIL;
PROC int ac_drm_query_firmware_version(ac_drm_device *dev, unsigned fw_type, unsigned ip_instance,
                                       unsigned index, uint32_t *version, uint32_t *feature) TAIL;
PROC int ac_drm_query_uq_fw_area_info(ac_drm_device *dev, unsigned type, unsigned ip_instance,
                                      struct drm_amdgpu_info_uq_metadata *info) TAIL;
PROC int ac_drm_query_gpu_info(ac_drm_device *dev, struct amdgpu_gpu_info *info) TAIL;
PROC int ac_drm_query_heap_info(ac_drm_device *dev, uint32_t heap, uint32_t flags,
                                struct amdgpu_heap_info *info) TAIL;
PROC int ac_drm_query_sensor_info(ac_drm_device *dev, unsigned sensor_type, unsigned size,
                                  void *value) TAIL;
PROC int ac_drm_query_video_caps_info(ac_drm_device *dev, unsigned cap_type, unsigned size,
                                      void *value) TAIL;
PROC int ac_drm_query_gpuvm_fault_info(ac_drm_device *dev, unsigned size, void *value) TAIL;
PROC int ac_drm_vm_reserve_vmid(ac_drm_device *dev, uint32_t flags) TAIL;
PROC int ac_drm_vm_unreserve_vmid(ac_drm_device *dev, uint32_t flags) TAIL;
PROC const char *ac_drm_get_marketing_name(ac_drm_device *device) TAILPTR;
PROC int ac_drm_query_sw_info(ac_drm_device *dev,
                              enum amdgpu_sw_info info, void *value) TAIL;
PROC int ac_drm_bo_alloc(ac_drm_device *dev, struct amdgpu_bo_alloc_request *alloc_buffer,
                         ac_drm_bo *bo) TAIL;
PROC int ac_drm_bo_export(ac_drm_device *dev, ac_drm_bo bo,
                          enum amdgpu_bo_handle_type type, uint32_t *shared_handle) TAIL;
PROC int ac_drm_bo_import(ac_drm_device *dev, enum amdgpu_bo_handle_type type,
                          uint32_t shared_handle, struct ac_drm_bo_import_result *output) TAIL;
PROC int ac_drm_create_bo_from_user_mem(ac_drm_device *dev, void *cpu,
                                        uint64_t size, ac_drm_bo *bo) TAIL;
PROC int ac_drm_bo_free(ac_drm_device *dev, ac_drm_bo bo) TAIL;
PROC int ac_drm_bo_cpu_map(ac_drm_device *dev, ac_drm_bo bo, void **cpu) TAIL;
PROC int ac_drm_bo_cpu_unmap(ac_drm_device *dev, ac_drm_bo bo) TAIL;
PROC int ac_drm_va_range_alloc(ac_drm_device *dev, enum amdgpu_gpu_va_range va_range_type,
                               uint64_t size, uint64_t va_base_alignment, uint64_t va_base_required,
                               uint64_t *va_base_allocated, amdgpu_va_handle *va_range_handle,
                               uint64_t flags) TAIL;
PROC int ac_drm_va_range_free(amdgpu_va_handle va_range_handle) TAIL;
PROC int ac_drm_va_range_query(ac_drm_device *dev, enum amdgpu_gpu_va_range type, uint64_t *start,
                               uint64_t *end) TAIL;
PROC int ac_drm_create_userqueue(ac_drm_device *dev, uint32_t ip_type, uint32_t doorbell_handle,
                                 uint32_t doorbell_offset, uint64_t queue_va, uint64_t queue_size,
                                 uint64_t wptr_va, uint64_t rptr_va, void *mqd_in, uint32_t flags,
                                 uint32_t *queue_id) TAIL;
PROC int ac_drm_free_userqueue(ac_drm_device *dev, uint32_t queue_id) TAIL;
PROC int ac_drm_userq_signal(ac_drm_device *dev, struct drm_amdgpu_userq_signal *signal_data) TAIL;
PROC int ac_drm_userq_wait(ac_drm_device *dev, struct drm_amdgpu_userq_wait *wait_data) TAIL;

PROC int ac_drm_query_pci_bus_info(ac_drm_device *dev, struct radeon_info *info) TAIL;
PROC void ac_drm_query_has_vm_always_valid(ac_drm_device *dev, struct radeon_info *info) TAILV;

#ifdef __cplusplus
}
#endif

#endif
