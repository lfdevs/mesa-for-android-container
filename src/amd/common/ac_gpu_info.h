/*
 * Copyright © 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_GPU_INFO_H
#define AC_GPU_INFO_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "amd_family.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AMD_MAX_SE         32
#define AMD_MAX_SA_PER_SE  2
#define AMD_MAX_WGP        60

/* Memory is divided among memory channels such that each 256B maps to a different memory channel
 * and the memory channel index increments with each 256B block, which wraps around to 0 after
 * the last memory channel index.
 *
 * For example, with 16 memory channels, address bits 8:11 contain the memory channel index.
 * Let's call them "channel address bits". The number of memory channels can be a non-power-of-two
 * on some chips.
 *
 * AMD GPUs usually assign 16 bits of memory bus to 1 memory channel. For example, 192-bit GDDR
 * memory bus has 12 memory channels. APUs usually have 1 memory channel per 32 bits or 64 bits
 * of memory bus. The physical memory channels don't always map 1:1 to AMD GPU memory channels.
 *
 * Memory channels are like separate cores. The advertised bandwidth and cache sizes are always
 * for all memory channels combined. That means that each channel has only 1/num_memory_channels
 * bandwidth and 1/memory_channels cache size. If all memory accesses unluckily end up in the same
 * channel for all running shaders, the available memory bandwidth is only 1/num_memory_channels
 * and the available cache size is also only 1/num_memory_channels. With 16 memory channels, that
 * would be 16x worse cache and memory performance.
 *
 * Strategies to distribute work among all memory channels evenly:
 *
 * - Ring element sizes should be set to an odd multiple of 256 to make sure each element starts on
 *   a different memory channel. This is similar to how LDS banks work, but the granularity is 256B
 *   instead of 4B here. The simplest way to do that is that if the ring element size is > 256,
 *   apply "|= 256;" to it. The scratch ring and the task shader payload ring do this.
 *
 * - For tree data structures in memory, try to randomize channel address bits, which can be done by
 *   making sure that tree nodes start on an odd multiple of 256. All possible numbers of
 *   ((address / 256) % num_memory_channels) should be represented equally in the node addresses.
 *
 * - If we have a ring buffer where we can't set the ring element size (e.g. TCS outputs where it's
 *   set to 32K), each workgroup should write at least (num_memory_channels * 256) of TCS outputs
 *   in bytes, and ideally twice that amount, to make sure each workgroup doesn't leave some memory
 *   channels (and thus bandwidth) completely unused or underutilized. We could also shift
 *   the placement of TCS outputs to a random 256*i offset within each 32K segment instead. Our TCS
 *   workgroup size calculation takes this into account.
 *
 * - radeon_surf::tile_swizzle is a random number that randomizes channel address bits to make sure
 *   some fixed image coordinates (x,y) map to a different memory channel for each image, so if
 *   a shader were to access multiple images at some fixed image coordinates (x,y) with the same
 *   bpp, each image would load from a different channel if radeon_surf::tile_swizzle is different.
 *   If multiple render targets are bound, it's recommended that they all have different tile_swizzle,
 *   so that MRT0 goes to channel A, MRT1 goes to channel B (A != B), etc. Other than that, image
 *   tiling does the optimal thing for us. The main purpose of 4K and bigger tiling is to distribute
 *   work among all memory channels evenly. Linear and 256B tiling generally don't do that.
 *
 * - Performance is also affected by how many memory channels a VMEM instruction or a clause
 *   intersects. Stores are more sensitive to this than loads because they are often globally
 *   coherent. For example, a 32-lane VMEM store can store to address range=128..640 (size=512),
 *   which stores data to 3 memory channels, while storing to address range=256..768 stores the same
 *   amount of data to only 2 memory channels. The latter case has better performance (less VMEM
 *   latency) when all memory channels are already busy because the wave only has to wait for replies
 *   from 2 channels instead of 3, and 1 channel has less work to do. Examples are:
 *   - Our clear_buffer and copy_buffer compute shaders where the store address of lane 0 is always
 *     a multiple of 256, so that each subgroup always stores to a 256B-aligned memory region of
 *     size 256*N.
 *   - Our image clear and blit compute shaders where the stored adress range of each compute
 *     subgroup is always aligned to 256B and stores 256*N. That's accomplished by making compute
 *     subgroups always clear or copy whole 256B image tiles, whose dimensions differ between tiling
 *     modes.
 *   - Vertex 0 of each TCS output starts on an address aligned to 256 to make TCS output stores
 *     from each subgroup always store 256B-aligned blocks of 256*N bytes.
 *
 * Number 256 comes from GB_ADDR_CONFIG.PIPE_INTERLEAVE_SIZE. It's always 256 on all GCN and RDNA
 * chips. "Pipe" means a memory channel in this context.
 */
#define AMD_MEMCHANNEL_INTERLEAVE_BYTES 256 /* always equal to GB_ADDR_CONFIG.PIPE_INTERLEAVE_SIZE */

struct amdgpu_gpu_info;
struct drm_amdgpu_info_device;
struct drm_amdgpu_memory_info;
struct drm_amdgpu_info_hw_ip;

struct amd_ip_info {
   uint8_t ver_major;
   uint8_t ver_minor;
   uint8_t ver_rev;
   uint8_t num_queues;
   uint8_t num_instances;
   uint32_t ib_alignment;
   uint32_t ib_pad_dw_mask;
};

struct ac_compiler_info {
   enum amd_gfx_level gfx_level;
   uint32_t max_waves_per_simd;
   uint32_t num_physical_sgprs_per_simd;
   uint32_t num_physical_wave64_vgprs_per_simd;
   uint32_t num_simd_per_compute_unit;
   uint32_t min_sgpr_alloc;
   uint32_t max_sgpr_alloc;
   uint32_t sgpr_alloc_granularity;
   uint32_t min_wave64_vgpr_alloc;
   uint32_t max_vgpr_alloc;
   uint32_t wave64_vgpr_alloc_granularity;

   uint32_t lds_size_per_workgroup;

   uint32_t hs_offchip_workgroup_dw_size;

   /* Flags */
   uint32_t has_lds_bank_count_16 : 1;
   uint32_t has_sram_ecc_enabled : 1;
   /* Whether image_sample* instructions can be either a sampler or no-sampler access.*/
   uint32_t has_point_sample_accel : 1;
   uint32_t has_fast_fma32 : 1;
   /* Whether chips support fused v_fma_mix* instructions.
    * Otherwise, unfused v_mad_mix* is available on GFX9.
    */
   uint32_t has_fma_mix : 1;
   /* Whether chips support unfused multiply-add instructions. */
   uint32_t has_mad32 : 1;
   /* Whether chips support double rate packed math instructions. */
   uint32_t has_packed_math_16bit : 1;
   /* Whether chips support dot product instructions. A subset of these support a smaller
    * instruction encoding which accumulates with the destination.
    */
   uint32_t has_accelerated_dot_product : 1;
   /* Device supports hardware-accelerated raytracing using
    * image_bvh*_intersect_ray instructions
    */
   uint32_t has_image_bvh_intersect_ray : 1;
   /* Whether PRIMGEN_PASSTHRU_NO_MSG is supported. */
   uint32_t has_ngg_passthru_no_msg : 1;
   /* Whether local invocation IDs are packed in a single VGPR. */
   uint32_t local_invocation_ids_packed : 1;
   /* Whether the chip supports FMASK. */
   uint32_t has_fmask : 1;
   /* Whether 3D textures, cubemap textures, border colors, and mipmapping are supported. (CDNA) */
   uint32_t has_3d_cube_border_color_mipmap : 1;

   /* conformant_trunc_coord is equal to TA_CNTL2.TRUNCATE_COORD_MODE, which exists since gfx11.
    *
    * If TA_CNTL2.TRUNCATE_COORD_MODE == 0, coordinate truncation is the same as gfx10 and older.
    *
    * If TA_CNTL2.TRUNCATE_COORD_MODE == 1, coordinate truncation is adjusted to be D3D9/GL/Vulkan
    * conformant if you also set TRUNC_COORD. Coordinate truncation uses D3D10+ behaviour if
    * TRUNC_COORD is unset.
    *
    * Behavior if TA_CNTL2.TRUNCATE_COORD_MODE == 1:
    *    truncate_coord_xy = TRUNC_COORD && (xy_filter == Point && !gather);
    *    truncate_coord_z = TRUNC_COORD && (z_filter == Point);
    *    truncate_coord_layer = false;
    *
    * Behavior if TA_CNTL2.TRUNCATE_COORD_MODE == 0:
    *    truncate_coord_xy = TRUNC_COORD;
    *    truncate_coord_z = TRUNC_COORD;
    *    truncate_coord_layer = TRUNC_COORD;
    *
    * AnisoPoint is treated as Point.
    */
   uint32_t conformant_trunc_coord : 1;

   uint32_t has_attr_ring : 1;

   /* GFX6-7: limit TCS workgroup to 16 patches for better performance. */
   uint32_t smaller_tcs_workgroups : 1;

   /* Some GFX6 GPUs have a bug where it only looks at the x writemask component. */
   uint32_t has_gfx6_mrt_export_bug : 1;
   /* Pre-GFX9: A bug where the alpha component of 10_10_10_2 formats is always unsigned.*/
   uint32_t has_vtx_format_alpha_adjust_bug : 1;
   /* GFX6-7: SMEM accesses memory even when it's out of bounds */
   uint32_t has_smem_oob_access_bug : 1;
   /* GFX10.3: WRITE_COMPRESS_ENABLE must be 0 for all image loads. */
   uint32_t has_image_load_dcc_bug : 1;
   /* GFX9: If there are no HS threads, SPI mistakenly loads the LS VGPRs starting at VGPR 0. */
   uint32_t has_ls_vgpr_init_bug : 1;
   /* GFX6-7: FS exports are not clamped correctly in certain situations. */
   uint32_t has_cb_lt16bit_int_clamp_bug : 1;
   /* GFX10.3: whether frag_pos.z needs adjusting when VRS is used. */
   uint32_t has_vrs_frag_pos_z_bug : 1;
   /* GFX10: hang when NGG exports zero vertices and primitives. */
   uint32_t has_ngg_fully_culled_bug : 1;
   /* GFX11-11.5: require wait between attribute stores and the final export. */
   uint32_t has_attr_ring_wait_bug : 1;
   /* GFX6: limit TCS workgroup to one patch if primitive ID is used. */
   uint32_t has_primid_instancing_bug : 1;
   /* GFX6 and certain GFX7 chips: bug with compute workgroups larger 256 invocations. */
   uint32_t has_cs_regalloc_hang_bug : 1;
   /* GFX6-GFX12, except GFX9: SMEM loads on NULL PRT page don't work. */
   uint32_t has_smem_with_null_prt_bug : 1;

   uint32_t reserved : 3;
};

struct radeon_info {
   /* Device info. */
   char marketing_name[64];
   uint32_t num_se;           /* only enabled SEs */
   uint32_t num_rb;           /* only enabled RBs */
   uint32_t num_cu;           /* only enabled CUs */
   uint32_t max_gpu_freq_mhz; /* also known as the shader clock */
   uint32_t max_gflops;
   uint32_t sqc_inst_cache_size;
   uint32_t sqc_scalar_cache_size;
   uint32_t num_sqc_per_wgp;
   uint32_t tcp_cache_size;
   uint32_t l1_cache_size;
   uint32_t l2_cache_size;
   uint32_t l3_cache_size_mb;
   uint32_t num_tcc_blocks; /* also the number of memory channels */
   uint32_t memory_freq_mhz;
   uint32_t memory_freq_mhz_effective;
   uint32_t memory_bus_width;
   uint32_t memory_bandwidth_gbps;
   uint32_t pcie_gen;
   uint32_t pcie_num_lanes;
   uint32_t pcie_bandwidth_mbps;
   uint32_t clock_crystal_freq;
   struct amd_ip_info ip[AMD_NUM_IP_TYPES];

   /* Identification. */
   /* PCI info: domain:bus:dev:func */
   struct {
      uint32_t domain;
      uint32_t bus;
      uint32_t dev;
      uint32_t func;
      bool valid;
   } pci;

   uint32_t pci_id;
   uint32_t pci_rev_id;
   enum radeon_family family;
   enum amd_gfx_level gfx_level;
   uint32_t family_id;
   uint32_t chip_external_rev;
   uint32_t chip_rev; /* 0 = A0, 1 = A1, etc. */

   /* Flags. */
   bool has_graphics; /* false if the chip is compute-only */
   bool has_clear_state;
   bool has_distributed_tess;
   bool has_dcc_constant_encode;
   bool has_tc_compatible_htile;
   bool has_etc_support;
   bool has_rbplus;     /* if RB+ registers exist */
   bool rbplus_allowed; /* if RB+ is allowed */
   bool has_load_ctx_reg_pkt;
   bool has_out_of_order_rast;
   bool cpdma_prefetch_writes_memory;
   bool has_gfx9_scissor_bug;
   bool has_htile_stencil_mipmap_bug;
   bool has_htile_tc_z_clear_bug_without_stencil;
   bool has_htile_tc_z_clear_bug_with_stencil;
   bool has_small_prim_filter_sample_loc_bug;
   bool has_pops_missed_overlap_bug;
   bool has_zero_index_buffer_bug;
   bool has_db_force_stencil_valid_bug;
   bool has_two_planes_iterate256_bug;
   bool has_vgt_flush_ngg_legacy_bug;
   bool has_prim_restart_sync_bug;
   bool has_async_compute_threadgroup_bug;
   bool has_async_compute_align32_bug;
   bool has_32bit_predication;
   bool has_image_opcodes;
   bool never_stop_sq_perf_counters;
   bool has_sqtt_rb_harvest_bug;
   bool has_sqtt_auto_flush_mode_bug;
   bool never_send_perfcounter_stop;
   bool discardable_allows_big_page;
   bool has_export_conflict_bug;
   bool cp_dma_supports_sparse;
   bool has_vrs_ds_export_bug;
   bool has_vrs_export_bug;
   bool has_taskmesh_indirect0_bug;
   bool sdma_supports_sparse;      /* Whether SDMA can safely access sparse resources. */
   bool sdma_supports_compression; /* Whether SDMA supports DCC and HTILE. */
   bool has_set_context_pairs;
   bool has_set_context_pairs_packed;
   bool has_set_sh_pairs;
   bool has_set_sh_pairs_packed;
   bool has_set_uconfig_pairs;
   bool needs_llvm_wait_wa; /* True if the chip needs to workarounds based on s_waitcnt_deptr but
                             * the LLVM version doesn't work with multiparts shaders.
                             */

   /* Display features. */
   /* There are 2 display DCC codepaths, because display expects unaligned DCC. */
   /* Disable RB and pipe alignment to skip the retile blit. (1 RB chips only) */
   bool use_display_dcc_unaligned;
   /* Allocate both aligned and unaligned DCC and use the retile blit. */
   bool use_display_dcc_with_retile_blit;
   bool gfx12_supports_display_dcc;
   bool gfx12_supports_dcc_write_compress_disable;

   /* Memory info. */
   uint32_t pte_fragment_size;
   uint32_t gart_page_size;
   uint32_t gart_size_kb;
   uint32_t vram_size_kb;
   uint64_t vram_vis_size_kb;
   uint32_t vram_type;
   uint32_t max_heap_size_kb;
   uint32_t min_alloc_size;
   uint32_t address32_hi;
   bool has_dedicated_vram;
   bool all_vram_visible;
   uint64_t high_va_offset;
   uint64_t high_va_max;
   uint64_t virtual_address_max;
   uint64_t virtual_address_alignment;
   bool has_l2_uncached;
   bool r600_has_virtual_memory;
   uint32_t max_tcc_blocks;
   uint32_t tcc_cache_line_size;
   bool tcc_rb_non_coherent; /* whether L2 inv is needed for render->texture transitions */
   bool cp_sdma_ge_use_system_memory_scope;
   bool cp_dma_use_L2;
   unsigned pc_lines;

   /* CP info. */
   bool gfx_ib_pad_with_type2;
   bool can_chain_ib2;
   bool has_cp_dma;
   uint32_t me_fw_version;
   uint32_t me_fw_feature;
   uint32_t mec_fw_version;
   uint32_t mec_fw_feature;
   uint32_t pfp_fw_version;
   uint32_t pfp_fw_feature;

   /* Multimedia info. */
   uint32_t uvd_fw_version;
   uint32_t vce_fw_version;
   uint32_t vce_harvest_config;
   uint32_t vcn_dec_version;
   uint32_t vcn_enc_major_version;
   uint32_t vcn_enc_minor_version;
   uint32_t vcn_fw_revision;
   struct video_caps_info {
      struct video_codec_cap {
         uint32_t valid;
         uint32_t max_width;
         uint32_t max_height;
         uint32_t max_pixels_per_frame;
         uint32_t max_level;
         uint32_t pad;
      } codec_info[8]; /* the number of available codecs */
   } dec_caps, enc_caps;

   enum vcn_version vcn_ip_version;
   enum sdma_version sdma_ip_version;
   enum rt_version rt_ip_version;
   enum vpe_version vpe_ip_version;

   /* Kernel & winsys capabilities. */
   uint32_t drm_major; /* version */
   uint32_t drm_minor;
   uint32_t drm_patchlevel;
   uint32_t max_submitted_ibs[AMD_NUM_IP_TYPES];
   bool is_amdgpu;
   bool is_virtio;
   bool has_userptr;
   bool has_syncobj;
   bool has_timeline_syncobj;
   bool has_fence_to_handle;
   bool has_vm_always_valid;
   bool has_eqaa_surface_allocator;
   /* Sparse bindings and basic sparse features (2D image, etc.) */
   bool has_sparse;
   /* 3D sparse images */
   bool has_sparse_image_3d;
   /* 3D sparse images with standard block shape */
   bool has_sparse_image_standard_3d;
   /* Mip levels do not need to be aligned to the sparse block size */
   bool has_sparse_unaligned_mip_size;
   bool has_gpuvm_fault_query;
   /* Whether SR-IOV is enabled or amdgpu.mcbp=1 was set on the kernel command line. */
   bool has_kernelq_reg_shadowing;
   bool has_default_zerovram_support;
   bool has_tmz_support;
   bool has_trap_handler_support;
   bool kernel_has_modifiers;
   uint32_t userq_ip_mask; /* AMD_IP_* bits */
   uint8_t address_prt_wa_control_bit;

   /* If the kernel driver uses CU reservation for high priority compute on gfx10+, it programs
    * a global CU mask in the hw that is AND'ed with CU_EN register fields set by userspace.
    * The packet that does the AND'ing is SET_SH_REG_INDEX(index = 3). If you don't use
    * SET_SH_REG_INDEX, the global CU mask will not be applied.
    *
    * If uses_kernel_cu_mask is true, use SET_SH_REG_INDEX.
    *
    * If uses_kernel_cu_mask is false, SET_SH_REG_INDEX shouldn't be used because it only
    * increases CP overhead and doesn't have any other effect.
    *
    * The alternative to this is to set the AMD_CU_MASK environment variable that has the same
    * effect on radeonsi and RADV and doesn't need SET_SH_REG_INDEX.
    */
   bool uses_kernel_cu_mask;

   struct ac_compiler_info compiler_info;

   /* Shader cores. */
   uint16_t cu_mask[AMD_MAX_SE][AMD_MAX_SA_PER_SE];
   uint32_t r600_max_quad_pipes; /* wave size / 16 */
   uint32_t max_good_cu_per_sa;
   uint32_t min_good_cu_per_sa; /* min != max if SAs have different # of CUs */
   uint32_t max_se;             /* number of shader engines incl. disabled ones */
   uint32_t max_sa_per_se;      /* shader arrays per shader engine */
   uint32_t num_cu_per_sh;
   uint32_t scratch_wavesize_granularity_shift;
   uint32_t scratch_wavesize_granularity;
   uint32_t max_scratch_waves;
   bool has_scratch_base_registers;

   /* Pos, prim, and attribute rings. */
   uint32_t attribute_ring_size_per_se;   /* GFX11+ */
   uint32_t pos_ring_size_per_se;         /* GFX12+ */
   uint32_t prim_ring_size_per_se;        /* GFX12+ */
   uint32_t pos_ring_offset;              /* GFX12+ */
   uint32_t prim_ring_offset;             /* GFX12+ */
   uint32_t total_attribute_pos_prim_ring_size; /* GFX11+ */

   /* Tessellation rings. */
   uint32_t hs_offchip_param;
   uint32_t hs_offchip_workgroup_dw_size;
   uint32_t tess_factor_ring_size;
   uint32_t tess_offchip_ring_size;
   uint32_t total_tess_ring_size;

   /* Render backends (color + depth blocks). */
   uint32_t r300_num_gb_pipes;
   uint32_t r300_num_z_pipes;
   uint32_t r600_gb_backend_map; /* R600 harvest config */
   bool r600_gb_backend_map_valid;
   uint32_t r600_num_banks;
   uint32_t r600_pipe_interleave_bytes;
   uint32_t mc_arb_ramcfg;
   uint32_t gb_addr_config;
   uint32_t pa_sc_tile_steering_override; /* CLEAR_STATE also sets this */
   uint32_t max_render_backends;  /* number of render backends incl. disabled ones */
   uint32_t num_tile_pipes; /* pipe count from PIPE_CONFIG */
   uint64_t enabled_rb_mask; /* bitmask of enabled physical RBs, up to max_render_backends bits */
   uint64_t max_alignment;   /* from addrlib */
   uint32_t pbb_max_alloc_count;

   /* Tile modes. */
   uint32_t si_tile_mode_array[32];
   uint32_t cik_macrotile_mode_array[16];

   /* AMD_CU_MASK environment variable or ~0. */
   bool spi_cu_en_has_effect;
   uint32_t spi_cu_en;

   /* Raster config. */
   uint32_t pa_sc_raster_config;
   uint32_t pa_sc_raster_config_1;
   uint32_t se_tile_repeat;

   struct {
      uint32_t shadow_size;
      uint32_t shadow_alignment;
      uint32_t csa_size;
      uint32_t csa_alignment;
      uint32_t eop_size;
      uint32_t eop_alignment;
      uint32_t sdma_csa_size;
      uint32_t sdma_csa_alignment;
   } fw_based_mcbp;
};

enum ac_query_gpu_info_result {
   AC_QUERY_GPU_INFO_SUCCESS,
   AC_QUERY_GPU_INFO_FAIL,
   AC_QUERY_GPU_INFO_UNIMPLEMENTED_HW,
};

/* If compiler_compat_mode is true, then ac_compiler_info must be identical between:
 * - CHIP_VANGOGH and CHIP_REMBRANDT
 * - CHIP_NAVI33, CHIP_PHOENIX and CHIP_PHOENIX2
 * This is done by disabling features and enabling workarounds.
 *
 * conformant_trunc_coord is an exception, and might differ.
 */
enum ac_query_gpu_info_result ac_query_gpu_info(int fd, void *dev_p, struct radeon_info *info,
                                                bool require_pci_bus_info,
                                                bool compiler_compat_mode);
void ac_fill_compiler_info(struct radeon_info *info,
                           const struct drm_amdgpu_info_device *device_info, bool compat_mode);
void ac_fill_tiling_info(struct radeon_info *info, const struct amdgpu_gpu_info *amdinfo);
void ac_fill_memory_info(struct radeon_info *info, const struct drm_amdgpu_info_device *device_info,
                         const struct drm_amdgpu_memory_info *meminfo);
bool
ac_fill_hw_ip_info(struct radeon_info *info, const struct drm_amdgpu_info_device *device_info,
                   unsigned ip_type, const struct drm_amdgpu_info_hw_ip *ip_info);
bool
ac_identify_chip(struct radeon_info *info, const struct drm_amdgpu_info_device *device_info);
void ac_fill_bug_info(struct radeon_info *info);
void ac_fill_feature_info(struct radeon_info *info, const struct drm_amdgpu_info_device *device_info);
void ac_fill_hw_info(struct radeon_info *info, const struct drm_amdgpu_info_device *device_info);
void ac_fill_tess_info(struct radeon_info *info);

void ac_compute_driver_uuid(char *uuid, size_t size);

void ac_compute_device_uuid(const struct radeon_info *info, char *uuid, size_t size);
void ac_print_gpu_info(FILE *f, const struct radeon_info *info, int fd);
int ac_get_gs_table_depth(enum amd_gfx_level gfx_level, enum radeon_family family);
void ac_get_raster_config(const struct radeon_info *info, uint32_t *raster_config_p,
                          uint32_t *raster_config_1_p, uint32_t *se_tile_repeat_p);
void ac_get_harvested_configs(const struct radeon_info *info, unsigned raster_config,
                              unsigned *cik_raster_config_1_p, unsigned *raster_config_se);
unsigned ac_get_compute_resource_limits(const struct radeon_info *info,
                                        unsigned waves_per_threadgroup, unsigned max_waves_per_sh,
                                        unsigned threadgroups_per_cu);

/* Task rings BO layout information.
 * This BO is shared between GFX and ACE queues so that the ACE and GFX
 * firmware can cooperate on task->mesh dispatches and is also used to
 * store the task payload which is passed to mesh shaders.
 *
 * The driver only needs to create this BO once,
 * and it will always be able to accommodate the maximum needed
 * task payload size.
 *
 * The following memory layout is used:
 * 1. Control buffer: 9 DWORDs, 256 byte aligned
 *    Used by the firmware to maintain the current state.
 * (padding)
 * 2. Draw ring: 4 DWORDs per entry, 256 byte aligned
 *    Task shaders store the mesh dispatch size here.
 * (padding)
 * 3. Payload ring: 16K bytes per entry, 256 byte aligned.
 *    This is where task payload is stored by task shaders and
 *    read by mesh shaders.
 *
 */
struct ac_task_info {
   uint32_t draw_ring_offset;
   uint32_t payload_ring_offset;
   uint32_t bo_size_bytes;
   uint16_t num_entries;
   uint32_t payload_entry_size;
};

/* Size of each draw entry in the task draw ring.
 * 4 DWORDs per entry.
 */
#define AC_TASK_DRAW_ENTRY_BYTES 16

/* Size of the task control buffer. 9 DWORDs. */
#define AC_TASK_CTRLBUF_BYTES 36

void ac_get_task_info(const struct radeon_info *info,
                      struct ac_task_info *task_info);

uint32_t ac_memory_ops_per_clock(uint32_t vram_type);

uint32_t ac_gfx103_get_cu_mask_ps(const struct radeon_info *info);

/* Number of entries in the mesh shader scratch ring.
 * This depends on VGT_GS_MAX_WAVE_ID which is set by the kernel
 * and is impossible to query. We leave it on its maximum value
 * because real applications are unlikely to use it.
 *
 * The maximum ID on GFX10.3 is 2047 (0x7ff), so we need 2048 entries.
 */
#define AC_MESH_SCRATCH_NUM_ENTRIES 2048

/* Size of each entry in the mesh shader scratch ring.
 * We must ensure that the absolute maximum mesh shader output fits here.
 *
 * Mesh shaders can create up to 256 vertices/primitives per workgroup,
 * and up to the following amount of outputs:
 * - 32 parameters
 * - 4 positions (clip/cull distance, etc.)
 * - 4 per-primitive built-in outputs (layer, view index, prim id, VRS rate)
 * - primitive indices which are always kept in LDS
 * That is a total of 32+4+4=40 output slots x 16 bytes per slot x 256 = 160K bytes.
 */
#define AC_MESH_SCRATCH_ENTRY_BYTES (160 * 1024)

#ifdef __cplusplus
}
#endif

#endif /* AC_GPU_INFO_H */
