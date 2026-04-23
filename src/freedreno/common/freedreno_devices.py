#
# Copyright © 2021 Google, Inc.
#
# SPDX-License-Identifier: MIT

from freedreno_dev_info import *

# a2xx is really two sub-generations, a20x and a22x, but we don't currently
# capture that in the device-info tables
add_gpus([
        GPUId(200),
        GPUId(201),
        GPUId(205),
        GPUId(220),
    ], GPUInfo(
        CHIP.A2XX,
        gmem_align_w = 32,  gmem_align_h = 32,
        tile_align_w = 32,  tile_align_h = 32,
        tile_max_w   = 512,
        tile_max_h   = ~0, # TODO
        num_vsc_pipes = 8,
        cs_shared_mem_size = 0,
        num_sp_cores = 0, # TODO
        wave_granularity = 2,
        fibers_per_sp = 0, # TODO
        threadsize_base = 8, # TODO: Confirm this
    ))

add_gpus([
        GPUId(305),
        GPUId(307),
        GPUId(320),
        GPUId(330),
        GPUId(chip_id=0x03000512, name="FD305B"),
        GPUId(chip_id=0x03000620, name="FD306A"),
    ], GPUInfo(
        CHIP.A3XX,
        gmem_align_w = 32,  gmem_align_h = 32,
        tile_align_w = 32,  tile_align_h = 32,
        tile_max_w   = 992, # max_bitfield_val(4, 0, 5)
        tile_max_h   = max_bitfield_val(9, 5, 5),
        num_vsc_pipes = 8,
        cs_shared_mem_size = 32 * 1024,
        num_sp_cores = 0, # TODO
        wave_granularity = 2,
        fibers_per_sp = 0, # TODO
        threadsize_base = 8,
    ))

add_gpus([
        GPUId(405),
        GPUId(420),
        GPUId(430),
    ], GPUInfo(
        CHIP.A4XX,
        gmem_align_w = 32,  gmem_align_h = 32,
        tile_align_w = 32,  tile_align_h = 32,
        tile_max_w   = 1024, # max_bitfield_val(4, 0, 5)
        tile_max_h   = max_bitfield_val(9, 5, 5),
        num_vsc_pipes = 8,
        cs_shared_mem_size = 32 * 1024,
        num_sp_cores = 0, # TODO
        wave_granularity = 2,
        fibers_per_sp = 0, # TODO
        threadsize_base = 32, # TODO: Confirm this
    ))

add_gpus([
        GPUId(505),
        GPUId(506),
        GPUId(508),
        GPUId(509),
    ], GPUInfo(
        CHIP.A5XX,
        gmem_align_w = 64,  gmem_align_h = 32,
        tile_align_w = 64,  tile_align_h = 32,
        tile_max_w   = 1024, # max_bitfield_val(7, 0, 5)
        tile_max_h   = max_bitfield_val(16, 9, 5),
        num_vsc_pipes = 16,
        cs_shared_mem_size = 32 * 1024,
        num_sp_cores = 1,
        wave_granularity = 2,
        fibers_per_sp = 64 * 16, # Lowest number that didn't fault on spillall fs-varying-array-mat4-col-row-rd.
        highest_bank_bit = 14,
        threadsize_base = 32,
    ))

add_gpus([
        GPUId(510),
        GPUId(512),
    ], GPUInfo(
        CHIP.A5XX,
        gmem_align_w = 64,  gmem_align_h = 32,
        tile_align_w = 64,  tile_align_h = 32,
        tile_max_w   = 1024, # max_bitfield_val(7, 0, 5)
        tile_max_h   = max_bitfield_val(16, 9, 5),
        num_vsc_pipes = 16,
        cs_shared_mem_size = 32 * 1024,
        num_sp_cores = 2,
        wave_granularity = 2,
        fibers_per_sp = 64 * 16, # Lowest number that didn't fault on spillall fs-varying-array-mat4-col-row-rd.
        highest_bank_bit = 14,
        threadsize_base = 32,
    ))

add_gpus([
        GPUId(530),
        GPUId(540),
    ], GPUInfo(
        CHIP.A5XX,
        gmem_align_w = 64,  gmem_align_h = 32,
        tile_align_w = 64,  tile_align_h = 32,
        tile_max_w   = 1024, # max_bitfield_val(7, 0, 5)
        tile_max_h   = max_bitfield_val(16, 9, 5),
        num_vsc_pipes = 16,
        cs_shared_mem_size = 32 * 1024,
        num_sp_cores = 4,
        wave_granularity = 2,
        fibers_per_sp = 64 * 16, # Lowest number that didn't fault on spillall fs-varying-array-mat4-col-row-rd.
        highest_bank_bit = 15,
        threadsize_base = 32,
    ))

# Props could be modified with env var:
#  FD_DEV_FEATURES=%feature_name%=%value%:%feature_name%=%value%:...
# e.g.
#  FD_DEV_FEATURES=has_fs_tex_prefetch=0:max_sets=4

a6xx_base = GPUProps(
        has_cp_reg_write = True,
        has_8bpp_ubwc = True,
        has_gmem_fast_clear = True,
        has_hw_multiview = True,
        has_fs_tex_prefetch = True,
        has_sampler_minmax = True,
        has_astc_hdr = True,

        supports_double_threadsize = True,

        sysmem_per_ccu_depth_cache_size = 64 * 1024,
        sysmem_per_ccu_color_cache_size = 64 * 1024,
        gmem_ccu_color_cache_fraction = CCUColorCacheFraction.QUARTER.value,

        prim_alloc_threshold = 0x7,
        vs_max_inputs_count = 32,
        max_sets = 5,
        line_width_min = 1.0,
        line_width_max = 1.0,
        mov_half_shared_quirk = True,
        max_draw_states = 32,
    )


# a6xx and a7xx can be divided into distinct sub-generations, where certain
# device-info parameters are keyed to the sub-generation.  These templates
# reduce the copypaste

a6xx_gen1_low = GPUProps(
        reg_size_vec4 = 48,
        instr_cache_size = 64,
        indirect_draw_wfm_quirk = True,
        depth_bounds_require_depth_test_quirk = True,

        has_gmem_fast_clear = False,
        has_hw_multiview = False,
        has_sampler_minmax = False,
        has_astc_hdr = False,
        has_fs_tex_prefetch = False,
        sysmem_per_ccu_color_cache_size = 8 * 1024,
        sysmem_per_ccu_depth_cache_size = 8 * 1024,
        gmem_ccu_color_cache_fraction = CCUColorCacheFraction.HALF.value,
        vs_max_inputs_count = 16,
        supports_double_threadsize = False,
    )

a6xx_gen1 = GPUProps(
        reg_size_vec4 = 96,
        instr_cache_size = 64,
        indirect_draw_wfm_quirk = True,
        depth_bounds_require_depth_test_quirk = True,
    )

a6xx_gen2 = GPUProps(
        reg_size_vec4 = 96,
        instr_cache_size = 64, # TODO
        supports_multiview_mask = True,
        has_z24uint_s8uint = True,
        indirect_draw_wfm_quirk = True,
        depth_bounds_require_depth_test_quirk = True, # TODO: check if true
        has_dp2acc = False, # TODO: check if true
        has_8bpp_ubwc = False,
    )

a6xx_gen3 = GPUProps(
        reg_size_vec4 = 64,
        # Blob limits it to 128 but we hang with 128
        instr_cache_size = 127,
        supports_multiview_mask = True,
        has_z24uint_s8uint = True,
        tess_use_shared = True,
        storage_16bit = True,
        has_tex_filter_cubic = True,
        has_separate_chroma_filter = True,
        has_sample_locations = True,
        has_8bpp_ubwc = False,
        has_dp2acc = True,
        has_lrz_dir_tracking = True,
        enable_lrz_fast_clear = True,
        lrz_track_quirk = True,
        has_lrz_feedback = True,
        has_per_view_viewport = True,
        has_scalar_alu = True,
        has_early_preamble = True,
        prede_nop_quirk = True,
        has_pred_bit = True,
        has_pc_dgen_so_cntl = True,
        # HW seem to support this, but prop driver doesn't enable it,
        # Be safe and don't enable it either.
        # supports_linear_mipmap_threshold_in_blocks = True,
    )

a6xx_gen4 = GPUProps(
        reg_size_vec4 = 64,
        # Blob limits it to 128 but we hang with 128
        instr_cache_size = 127,
        supports_multiview_mask = True,
        has_z24uint_s8uint = True,
        tess_use_shared = True,
        storage_16bit = True,
        has_tex_filter_cubic = True,
        has_separate_chroma_filter = True,
        has_sample_locations = True,
        has_cp_reg_write = False,
        has_8bpp_ubwc = False,
        has_lpac = True,
        has_legacy_pipeline_shading_rate = True,
        has_getfiberid = True,
        has_movs = True,
        has_dp2acc = True,
        has_dp4acc = True,
        enable_lrz_fast_clear = True,
        has_lrz_dir_tracking = True,
        has_lrz_feedback = True,
        has_per_view_viewport = True,
        has_scalar_alu = True,
        has_isam_v = True,
        has_ssbo_imm_offsets = True,
        has_ubwc_linear_mipmap_fallback = True,
        # TODO: there seems to be a quirk where at least rcp can't be in an
        # early preamble. a660 at least is affected.
        #has_early_preamble = True,
        prede_nop_quirk = True,
        predtf_nop_quirk = True,
        has_sad = True,
        has_sel_b_fneg = True,
        has_pred_bit = True,
        has_pc_dgen_so_cntl = True,
        # HW seem to support this, but prop driver doesn't enable it,
        # Be safe and don't enable it either.
        # supports_linear_mipmap_threshold_in_blocks = True,
    )

add_gpus([
        GPUId(605), # TODO: Test it, based only on libwrapfake dumps
        GPUId(608), # TODO: Test it, based only on libwrapfake dumps
        GPUId(610),
        GPUId(612), # TODO: Test it, based only on libwrapfake dumps
    ], A6xxGPUInfo(
        CHIP.A6XX,
        [a6xx_base, a6xx_gen1_low],
        num_ccu = 1,
        tile_align_w = 32,
        tile_align_h = 16,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 16,
        cs_shared_mem_size = 16 * 1024,
        wave_granularity = 1,
        fibers_per_sp = 128 * 16,
        highest_bank_bit = 13,
        ubwc_swizzle = 0x7,
        macrotile_mode = 0,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            RB_RBP_CNTL = 0x00000001,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0xf],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 0],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 0],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF, 0x00000004],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x10000000],
        ],
    ))

add_gpus([
        GPUId(615),
        GPUId(616),
        GPUId(618),
        GPUId(619),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        [a6xx_base, a6xx_gen1, GPUProps(blit_wfi_quirk = True)],
        num_ccu = 1,
        tile_align_w = 32,
        tile_align_h = 32,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 16,
        highest_bank_bit = 14,
        macrotile_mode = 0,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            RB_RBP_CNTL = 0x00000001,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x1f],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 0],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 0],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x00108000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0x00000880],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00000430],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF, 0x00000004],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0x00080000],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x1],
        ],
    ))

add_gpus([
        GPUId(620),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        [a6xx_base, a6xx_gen1],
        num_ccu = 1,
        tile_align_w = 32,
        tile_align_h = 16,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 16,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            RB_RBP_CNTL = 0x0,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x1f],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 0],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 0],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x01008000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00000400],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0x01000000],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF, 0x00000004],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0x02000000],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x1],
        ],
    ))

add_gpus([
        GPUId(chip_id=0xffff06020100, name="FD621"),
        GPUId(chip_id=0xffff06020300, name="Adreno623"),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        [a6xx_base, a6xx_gen3, GPUProps(lrz_track_quirk = False)],
        num_ccu = 2,
        tile_align_w = 96,
        tile_align_h = 16,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            RB_RBP_CNTL = 0x0,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x1f],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 0],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 0],
            # this seems to be a chicken bit that fixes cubic filtering:
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x01008000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00001400],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0x03000000],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0x02000000],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x1],
        ],
    ))

add_gpus([
        GPUId(630),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        [a6xx_base, a6xx_gen1],
        num_ccu = 2,
        tile_align_w = 32,
        tile_align_h = 16,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 16,
        highest_bank_bit = 15,
        macrotile_mode = 0,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x05100000,
            RB_RBP_CNTL = 0x00000001,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x1f],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 1],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 1],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x00108000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0x00000880],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00001430],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF, 0x00000004],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0x00080000],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x10000001],
        ],
    ))

add_gpus([
        GPUId(640),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        [a6xx_base, a6xx_gen2],
        num_ccu = 2,
        tile_align_w = 32,
        tile_align_h = 16,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 4 * 16,
        highest_bank_bit = 15,
        macrotile_mode = 0,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            RB_RBP_CNTL = 0x00000001,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x1f],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 1],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 1],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x00008000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00000420],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF, 0x00000004],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0x02000000],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x1],
        ],
    ))

add_gpus([
        GPUId(680),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        [a6xx_base, a6xx_gen2],
        num_ccu = 4,
        tile_align_w = 64,
        tile_align_h = 32,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 4 * 16,
        highest_bank_bit = 15,
        macrotile_mode = 0,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            RB_RBP_CNTL = 0x00000001,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x1f],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 3],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 3],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x00108000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00001430],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF, 0x00000004],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0x02000000],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x1],
        ],
    ))

add_gpus([
        GPUId(650),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        [a6xx_base, a6xx_gen3],
        num_ccu = 3,
        tile_align_w = 96,
        tile_align_h = 16,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        highest_bank_bit = 16,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            RB_RBP_CNTL = 0x0,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x1f],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 2],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 2],
            # this seems to be a chicken bit that fixes cubic filtering:
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x00108000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00001400],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0x01000000],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF, 0x00000004],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0x02000000],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x1],
        ],
    ))

add_gpus([
        # These are all speedbins/variants of A635
        GPUId(chip_id=0x00be06030500, name="Adreno 8c Gen 3"),
        GPUId(chip_id=0x007506030500, name="Adreno 7c+ Gen 3"),
        GPUId(chip_id=0x006006030500, name="Adreno 7c+ Gen 3 Lite"),
        GPUId(chip_id=0x00ac06030500, name="FD643"), # e.g. QCM6490, Fairphone 5
        # fallback wildcard entry should be last:
        GPUId(chip_id=0xffff06030500, name="Adreno 7c+ Gen 3"),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        [a6xx_base, a6xx_gen4],
        num_ccu = 2,
        tile_align_w = 32,
        tile_align_h = 16,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        highest_bank_bit = 14,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            RB_RBP_CNTL = 0x0,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x1f],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 1],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 1],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x05008000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00001400],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0x00000006],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF, 0x00000084],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0x02000000],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x1],
        ],
    ))

add_gpus([
        GPUId(660),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        [a6xx_base, a6xx_gen4],
        num_ccu = 3,
        tile_align_w = 96,
        tile_align_h = 16,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        highest_bank_bit = 16,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            RB_RBP_CNTL = 0x0,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x1f],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 2],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 2],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x05008000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00001400],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0x01000000],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF, 0x00000084],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0x02000000],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x1],
        ],
    ))

add_gpus([
        GPUId(chip_id=0x6060201, name="FD644"), # Called A662 in kgsl
        GPUId(chip_id=0xffff06060300, name="FD663"),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        [a6xx_base, a6xx_gen4],
        num_ccu = 3,
        tile_align_w = 96,
        tile_align_h = 16,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 4 * 16,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            RB_RBP_CNTL = 0x0,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x1f],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 2],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 2],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x05008000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00001400],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0x6],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF, 0x00000084],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0x02000000],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 1],
        ],
    ))

add_gpus([
        GPUId(690),
        GPUId(chip_id=0xffff06090000, name="FD690"), # Default no-speedbin fallback
    ], A6xxGPUInfo(
        CHIP.A6XX,
        [a6xx_base, a6xx_gen4, GPUProps(broken_ds_ubwc_quirk = True)],
        num_ccu = 8,
        tile_align_w = 64,
        tile_align_h = 32,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        highest_bank_bit = 16,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x100000,
            RB_DBG_ECO_CNTL_blit = 0x00100000,  # ???
            RB_RBP_CNTL = 0x0,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_SP_UNKNOWN_AAF2, 0x00c00000],
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x1f],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 7],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 7],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x04c00000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00001400],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0x1200000],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF, 0x00000084],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0x2000400],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x1],
        ],
    ))

add_gpus([
        GPUId(702), # KGSL
        GPUId(chip_id=0x00b207002000, name="FD702"), # QRB2210 RB1
        GPUId(chip_id=0xffff07002000, name="FD702"), # Default no-speedbin fallback
    ], A6xxGPUInfo(
        CHIP.A6XX, # NOT a mistake!
        [a6xx_base, a6xx_gen1_low, GPUProps(
            has_cp_reg_write = False,
            has_gmem_fast_clear = True,
            sysmem_per_ccu_depth_cache_size = 8 * 1024, # ??????
            sysmem_per_ccu_color_cache_size = 8 * 1024, # ??????
            gmem_ccu_color_cache_fraction = CCUColorCacheFraction.HALF.value,
            supports_double_threadsize = True,
            prim_alloc_threshold = 0x1,
            storage_16bit = True,
            is_a702 = True,
            )
        ],
        num_ccu = 1,
        tile_align_w = 32,
        tile_align_h = 16,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 16,
        cs_shared_mem_size = 16 * 1024,
        wave_granularity = 1,
        fibers_per_sp = 128 * 16,
        threadsize_base = 16,
        max_waves = 16,
        # has_early_preamble = True,  # for VS/FS but not CS?
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x100000,
            RB_DBG_ECO_CNTL_blit = 0x100000,
            RB_RBP_CNTL = 0x1,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0xf],
            [A6XXRegs.REG_A6XX_PC_POWER_CNTL, 0],
            [A6XXRegs.REG_A6XX_VFD_POWER_CNTL, 0],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x8000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00001400],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF, 0x00000084],
            [A6XXRegs.REG_A6XX_HLSQ_DBG_ECO_CNTL, 0x02000000],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x1],
        ],
    ))

# Based on a6xx_base + a6xx_gen4
a7xx_base = GPUProps(
        has_gmem_fast_clear = True,
        has_hw_multiview = True,
        has_fs_tex_prefetch = True,
        has_sampler_minmax = True,
        has_astc_hdr = True,

        supports_double_threadsize = True,

        sysmem_per_ccu_depth_cache_size = 256 * 1024,
        sysmem_per_ccu_color_cache_size = 64 * 1024,
        gmem_ccu_color_cache_fraction = CCUColorCacheFraction.EIGHTH.value,

        prim_alloc_threshold = 0x7,
        vs_max_inputs_count = 32,
        max_sets = 8,

        reg_size_vec4 = 96,
        # Blob limits it to 128 but we hang with 128
        instr_cache_size = 127,
        supports_multiview_mask = True,
        has_z24uint_s8uint = True,
        tess_use_shared = True,
        storage_16bit = True,
        storage_8bit = True,
        has_tex_filter_cubic = True,
        has_separate_chroma_filter = True,
        has_sample_locations = True,
        has_lpac = True,
        has_getfiberid = True,
        has_movs = True,
        has_dp2acc = True,
        has_dp4acc = True,
        enable_lrz_fast_clear = True,
        has_lrz_dir_tracking = True,
        has_lrz_feedback = True,
        has_per_view_viewport = True,
        line_width_min = 1.0,
        line_width_max = 127.5,
        has_scalar_alu = True,
        has_scalar_predicates = True,
        has_coherent_ubwc_flag_caches = True,
        has_isam_v = True,
        has_ssbo_imm_offsets = True,
        has_early_preamble = True,
        has_attachment_shading_rate = True,
        has_ubwc_linear_mipmap_fallback = True,
        supports_linear_mipmap_threshold_in_blocks = True,
        prede_nop_quirk = True,
        predtf_nop_quirk = True,
        has_sad = True,
        has_bin_mask = True,
        has_sel_b_fneg = True,
        has_pred_bit = True,
        has_pc_dgen_so_cntl = True,
        has_eolm_eogm = True,
    )

a7xx_gen1 = GPUProps(
        supports_uav_ubwc = True,
        fs_must_have_non_zero_constlen_quirk = True,
        enable_tp_ubwc_flag_hint = True,
        reading_shading_rate_requires_smask_quirk = True,
        cs_lock_unlock_quirk = True,
    )

a7xx_gen2 = GPUProps(
        stsc_duplication_quirk = True,
        has_event_write_sample_count = True,
        ubwc_unorm_snorm_int_compatible = True,
        supports_uav_ubwc = True,
        fs_must_have_non_zero_constlen_quirk = True,
        # Most devices with a740 have blob v6xx which doesn't have
        # this hint set. Match them for better compatibility by default.
        enable_tp_ubwc_flag_hint = False,
        has_64b_ssbo_atomics = True,
        has_primitive_shading_rate = True,
        reading_shading_rate_requires_smask_quirk = True,
        has_ray_intersection = True,
        has_hw_bin_scaling = True,
        has_image_processing = True,
    )

a7xx_gen3 = GPUProps(
        has_event_write_sample_count = True,
        load_inline_uniforms_via_preamble_ldgk = True,
        load_shader_consts_via_preamble = True,
        has_gmem_vpc_attr_buf = True,
        sysmem_vpc_attr_buf_size = 0x20000,
        gmem_vpc_attr_buf_size = 0xc000,
        ubwc_unorm_snorm_int_compatible = True,
        supports_uav_ubwc = True,
        has_generic_clear = True,
        r8g8_faulty_fast_clear_quirk = True,
        gs_vpc_adjacency_quirk = True,
        ubwc_all_formats_compatible = True,
        has_compliant_dp4acc = True,
        ubwc_coherency_quirk = True,
        has_persistent_counter = True,
        has_64b_ssbo_atomics = True,
        has_primitive_shading_rate = True,
        has_ray_intersection = True,
        has_sw_fuse = True,
        has_rt_workaround = True,
        has_alias_rt = True,
        has_abs_bin_mask = True,
        new_control_regs = True,
        has_hw_bin_scaling = True,
        has_image_processing = True,
        max_draw_states = 64,
    )

a730_magic_regs = dict(
        RB_DBG_ECO_CNTL = 0x00000000,
        RB_DBG_ECO_CNTL_blit = 0x00000000,  # is it even needed?
        RB_RBP_CNTL = 0x0,
    )

a730_raw_magic_regs = [
        [A6XXRegs.REG_A6XX_UCHE_CACHE_WAYS, 0x00840004],
        [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x1000000],
        [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL1, 0x00040724],

        [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x00001400],
        [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_1, 0x00402400],
        [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_2, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_3, 0x00000000],
        [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF,    0x00000084],
        [A6XXRegs.REG_A7XX_UCHE_UNKNOWN_0E10, 0x00000000],
        [A6XXRegs.REG_A7XX_UCHE_UNKNOWN_0E11, 0x00000040],
        [A6XXRegs.REG_A7XX_SP_HLSQ_DBG_ECO_CNTL, 0x00008000],
        [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0x10000000],
        [A6XXRegs.REG_A6XX_PC_MODE_CNTL,    0x0000003f],  # 0x00001f1f in some tests
        [A6XXRegs.REG_A6XX_PC_DBG_ECO_CNTL, 0x20080000],
        [A6XXRegs.REG_A7XX_PC_UNKNOWN_9E24, 0x21fc7f00],
        [A6XXRegs.REG_A7XX_VFD_DBG_ECO_CNTL, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_ISDB_CNTL, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_AE6A, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_HLSQ_TIMEOUT_THRESHOLD_DP, 0x00000080],
        [A6XXRegs.REG_A7XX_SP_HLSQ_DBG_ECO_CNTL_1, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_HLSQ_MODE_CNTL, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_AB01, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_AB22, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_B310, 0x00000000],

        [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE2,   0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE2+1, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE4,   0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE4+1, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE6,   0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE6+1, 0x00000000],

        [A6XXRegs.REG_A7XX_GRAS_ROTATION_CNTL, 0x00000000],
        [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL,  0x00000800],

        [A6XXRegs.REG_A7XX_RB_UNKNOWN_8E79,   0x00000000],
        [A6XXRegs.REG_A7XX_RB_LRZ_CNTL2,      0x00000000],
        [A6XXRegs.REG_A7XX_RB_CCU_DBG_ECO_CNTL, 0x02080000],
        [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL,  0x02000000],
        [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x3200000],
    ]

a740_magic_regs = dict(
        RB_DBG_ECO_CNTL = 0x00000000,
        RB_DBG_ECO_CNTL_blit = 0x00000000,  # is it even needed?
        RB_RBP_CNTL = 0x0,
    )

a740_raw_magic_regs = [
        [A6XXRegs.REG_A6XX_UCHE_CACHE_WAYS, 0x00040004],
        [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x11100000],
        [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL1, 0x00040724],

        [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x10001400],
        [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_1, 0x00400400],
        [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_2, 0x00430800],
        [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_3, 0x00000000],
        [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF,    0x00000084],
        [A6XXRegs.REG_A7XX_UCHE_UNKNOWN_0E10, 0x00000000],
        [A6XXRegs.REG_A7XX_UCHE_UNKNOWN_0E11, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_HLSQ_DBG_ECO_CNTL, 0x00000000],
        [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0x10000000],
        # Blob uses 0x1f or 0x1f1f, however these values cause vertices
        # corruption in some tests.
        [A6XXRegs.REG_A6XX_PC_MODE_CNTL,    0x0000003f],
        [A6XXRegs.REG_A6XX_PC_DBG_ECO_CNTL, 0x00100000],
        [A6XXRegs.REG_A7XX_PC_UNKNOWN_9E24, 0x21585600],
        [A6XXRegs.REG_A7XX_VFD_DBG_ECO_CNTL, 0x00008000],
        [A6XXRegs.REG_A7XX_SP_ISDB_CNTL, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_AE6A, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_HLSQ_TIMEOUT_THRESHOLD_DP, 0x00000080],
        [A6XXRegs.REG_A7XX_SP_HLSQ_DBG_ECO_CNTL_1, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_HLSQ_MODE_CNTL, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_AB01, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_AB22, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_B310, 0x00000000],

        [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE2,   0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE2+1, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE4,   0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE4+1, 0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE6,   0x00000000],
        [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE6+1, 0x00000000],

        [A6XXRegs.REG_A7XX_GRAS_ROTATION_CNTL, 0x00000000],
        [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL,  0x00004800],

        [A6XXRegs.REG_A7XX_RB_UNKNOWN_8E79,   0x00000000],
        [A6XXRegs.REG_A7XX_RB_LRZ_CNTL2,      0x00000000],
        [A6XXRegs.REG_A7XX_RB_CCU_DBG_ECO_CNTL, 0x02080000],
        [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL,  0x02000000],
        [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0],
    ]

add_gpus([
        # These are named as Adreno730v3 or Adreno725v1.
        GPUId(chip_id=0x07030002, name="FD725"),
        GPUId(chip_id=0xffff07030002, name="FD725"),
    ], A6xxGPUInfo(
        CHIP.A7XX,
        [a7xx_base, a7xx_gen1, GPUProps(cmdbuf_start_a725_quirk = True)],
        num_ccu = 4,
        tile_align_w = 64,
        tile_align_h = 32,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        highest_bank_bit = 16,
        magic_regs = a730_magic_regs,
        raw_magic_regs = a730_raw_magic_regs,
    ))

add_gpus([
        GPUId(chip_id=0x07030001, name="FD730"), # KGSL, no speedbin data
        GPUId(chip_id=0xffff07030001, name="FD730"), # Default no-speedbin fallback
    ], A6xxGPUInfo(
        CHIP.A7XX,
        [a7xx_base, a7xx_gen1],
        num_ccu = 4,
        tile_align_w = 64,
        tile_align_h = 32,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        highest_bank_bit = 16,
        magic_regs = a730_magic_regs,
        raw_magic_regs = a730_raw_magic_regs,
    ))

add_gpus([
        GPUId(chip_id=0x07010000, name="FD710"), # KGSL, no speedbin data
        GPUId(chip_id=0xffff07010000, name="FD710"), # Default no-speedbin fallback
    ], A6xxGPUInfo(
        CHIP.A7XX,
        [a7xx_base, a7xx_gen1],
        num_ccu = 4,
        tile_align_w = 64,
        tile_align_h = 32,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        highest_bank_bit = 16,
        magic_regs = a730_magic_regs,
        raw_magic_regs = a730_raw_magic_regs,
    ))

add_gpus([
        GPUId(chip_id=0x43020000, name="FD720"), # KGSL, no speedbin data
        GPUId(chip_id=0xffff043020000, name="FD720"), # Default no-speedbin fallback
    ], A6xxGPUInfo(
        CHIP.A7XX,
        [a7xx_base, a7xx_gen1],
        num_ccu = 4,
        tile_align_w = 64,
        tile_align_h = 32,
        tile_max_w = 1024,
        tile_max_h = 1024,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        highest_bank_bit = 16,
        magic_regs = a730_magic_regs,
        raw_magic_regs = a730_raw_magic_regs,
    ))

add_gpus([
        GPUId(chip_id=0xffff43030c00, name="Adreno X1-45"),
        GPUId(chip_id=0x43030B00, name="FD735")
    ], A6xxGPUInfo(
        CHIP.A7XX,
        [a7xx_base, a7xx_gen2, GPUProps(enable_tp_ubwc_flag_hint = True)],
        num_ccu = 3,
        tile_align_w = 96,
        tile_align_h = 32,
        tile_max_w = 2016,
        tile_max_h = 2032,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x00000001,
            RB_DBG_ECO_CNTL_blit = 0x00000001,  # is it even needed?
            RB_RBP_CNTL = 0x0,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_UCHE_CACHE_WAYS, 0x00000000],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x11100000],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL1, 0x00040724],

            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS, 0x10001400],
            [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_1, 0x00400400],
            [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_2, 0x00430800],
            [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_3, 0x00000000],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF,    0x00000084],
            [A6XXRegs.REG_A7XX_UCHE_UNKNOWN_0E10, 0x00000000],
            [A6XXRegs.REG_A7XX_UCHE_UNKNOWN_0E11, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_HLSQ_DBG_ECO_CNTL, 0x00000000],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0x10000000],
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x1f],
            [A6XXRegs.REG_A6XX_PC_DBG_ECO_CNTL, 0x00100000],
            [A6XXRegs.REG_A7XX_PC_UNKNOWN_9E24, 0x01585600],
            [A6XXRegs.REG_A7XX_VFD_DBG_ECO_CNTL, 0x00008000],
            [A6XXRegs.REG_A7XX_SP_ISDB_CNTL, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_AE6A, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_HLSQ_TIMEOUT_THRESHOLD_DP, 0x00000080],
            [A6XXRegs.REG_A7XX_SP_HLSQ_DBG_ECO_CNTL_1, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_HLSQ_MODE_CNTL, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_AB01, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_AB22, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_B310, 0x00000000],

            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE2,   0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE2+1, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE4,   0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE4+1, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE6,   0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE6+1, 0x00000000],

            [A6XXRegs.REG_A7XX_GRAS_ROTATION_CNTL, 0x00000000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL,  0x00004800],

            [A6XXRegs.REG_A7XX_RB_UNKNOWN_8E79,   0x00000000],
            [A6XXRegs.REG_A7XX_RB_LRZ_CNTL2,      0x00000000],
            [A6XXRegs.REG_A7XX_RB_CCU_DBG_ECO_CNTL, 0x02080000],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL,  0x02000000],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0],
        ],
    ))

add_gpus([
        GPUId(740), # Deprecated, used for dev kernels.
        GPUId(chip_id=0x43050a01, name="FD740"), # KGSL, no speedbin data
        GPUId(chip_id=0xffff43050a01, name="FD740"), # Default no-speedbin fallback
        GPUId(chip_id=0xffff43050c01, name="Adreno X1-85"),
    ], A6xxGPUInfo(
        CHIP.A7XX,
        [a7xx_base, a7xx_gen2],
        num_ccu = 6,
        tile_align_w = 96,
        tile_align_h = 32,
        tile_max_w = 2016,
        tile_max_h = 2032,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        highest_bank_bit = 16,
        magic_regs = a740_magic_regs,
        raw_magic_regs = a740_raw_magic_regs,
    ))

# Values from blob v676.0
add_gpus([
        GPUId(chip_id=0x43050a00, name="FDA32"), # Adreno A32 (G3x Gen 2)
        GPUId(chip_id=0xffff43050a00, name="FDA32"),
    ], A6xxGPUInfo(
        CHIP.A7XX,
        [a7xx_base, a7xx_gen2, GPUProps(cmdbuf_start_a725_quirk = True)],
        num_ccu = 6,
        tile_align_w = 96,
        tile_align_h = 32,
        tile_max_w = 2016,
        tile_max_h = 2032,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        magic_regs = a740_magic_regs,
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_UCHE_CACHE_WAYS, 0x00040004],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x11100000],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL1, 0x00000700],

            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS,   0x10001400],
            [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_1, 0x00400400],
            [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_2, 0x00430820],
            [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_3, 0x00000000],
            [A6XXRegs.REG_A6XX_UCHE_CLIENT_PF,    0x00000084],
            [A6XXRegs.REG_A7XX_UCHE_UNKNOWN_0E10, 0x00000000],
            [A6XXRegs.REG_A7XX_UCHE_UNKNOWN_0E11, 0x00000080],
            [A6XXRegs.REG_A7XX_SP_HLSQ_DBG_ECO_CNTL, 0x00000000],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0x10000000],
            # Blob uses 0x1f or 0x1f1f, however these values cause vertices
            # corruption in some tests.
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL,    0x0000003f],
            [A6XXRegs.REG_A6XX_PC_DBG_ECO_CNTL, 0x00100000],
            [A6XXRegs.REG_A7XX_PC_UNKNOWN_9E24, 0x21585600],
            [A6XXRegs.REG_A7XX_VFD_DBG_ECO_CNTL, 0x00008000],
            [A6XXRegs.REG_A7XX_SP_ISDB_CNTL, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_AE6A, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_HLSQ_TIMEOUT_THRESHOLD_DP, 0x00000080],
            [A6XXRegs.REG_A7XX_SP_HLSQ_DBG_ECO_CNTL_1, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_HLSQ_MODE_CNTL, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_AB01, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_AB22, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_B310, 0x00000000],

            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE2,   0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE2+1, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE4,   0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE4+1, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE6,   0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE6+1, 0x00000000],

            [A6XXRegs.REG_A7XX_GRAS_ROTATION_CNTL, 0x00000000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL,  0x00004800],

            [A6XXRegs.REG_A7XX_RB_UNKNOWN_8E79,   0x00000000],
            [A6XXRegs.REG_A7XX_RB_LRZ_CNTL2,      0x00000000],
            [A6XXRegs.REG_A7XX_RB_CCU_DBG_ECO_CNTL, 0x02080000],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL,  0x02000000],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0],
        ],
    ))

add_gpus([
        GPUId(chip_id=0x43050b00, name="FD740v3"), # Quest 3
        GPUId(chip_id=0xffff43050b00, name="FD740v3"),
    ], A6xxGPUInfo(
        CHIP.A7XX,
        [a7xx_base, a7xx_gen2, GPUProps(enable_tp_ubwc_flag_hint = True)],
        num_ccu = 6,
        tile_align_w = 96,
        tile_align_h = 32,
        tile_max_w = 2016,
        tile_max_h = 2032,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x00000001,
            RB_DBG_ECO_CNTL_blit = 0x00000000,  # is it even needed?
            RB_RBP_CNTL = 0x0,
        ),
        raw_magic_regs = a740_raw_magic_regs,
    ))

add_gpus([
        GPUId(chip_id=0x43051401, name="FD750"), # KGSL, no speedbin data
        GPUId(chip_id=0xffff43051401, name="FD750"), # Default no-speedbin fallback
    ], A6xxGPUInfo(
        CHIP.A7XX,
        [a7xx_base, a7xx_gen3],
        num_ccu = 6,
        tile_align_w = 96,
        tile_align_h = 32,
        tile_max_w = 2016,
        tile_max_h = 2032,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        highest_bank_bit = 16,
        magic_regs = dict(
            RB_DBG_ECO_CNTL = 0x00000001,
            RB_DBG_ECO_CNTL_blit = 0x00000001,
            RB_RBP_CNTL = 0x0,
        ),
        raw_magic_regs = [
            [A6XXRegs.REG_A6XX_UCHE_CACHE_WAYS, 0x00000000],
            [A6XXRegs.REG_A7XX_UCHE_UNKNOWN_0E10, 0x00000000],
            [A6XXRegs.REG_A7XX_UCHE_UNKNOWN_0E11, 0x00000080],
            [A6XXRegs.REG_A6XX_SP_CHICKEN_BITS,   0x10000400],
            [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_1, 0x00400000],
            [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_2, 0x00431800],
            [A6XXRegs.REG_A7XX_SP_CHICKEN_BITS_3, 0x00800000],
            [A6XXRegs.REG_A7XX_SP_HLSQ_DBG_ECO_CNTL, 0x00000000],
            [A6XXRegs.REG_A6XX_SP_DBG_ECO_CNTL, 0x10000000],
            [A6XXRegs.REG_A6XX_PC_MODE_CNTL, 0x3f1f],
            [A6XXRegs.REG_A6XX_PC_DBG_ECO_CNTL, 0x00100000],
            [A6XXRegs.REG_A7XX_PC_UNKNOWN_9E24, 0x01585600],
            [A6XXRegs.REG_A7XX_VFD_DBG_ECO_CNTL, 0x00008000],
            [A6XXRegs.REG_A7XX_SP_ISDB_CNTL, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_AE6A, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_HLSQ_TIMEOUT_THRESHOLD_DP, 0x00000080],
            [A6XXRegs.REG_A7XX_SP_HLSQ_DBG_ECO_CNTL_1, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_HLSQ_MODE_CNTL, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_AB01, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_AB22, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_B310, 0x00000000],

            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE2,   0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE2+1, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE4,   0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE4+1, 0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE6,   0x00000000],
            [A6XXRegs.REG_A7XX_SP_UNKNOWN_0CE6+1, 0x00000000],

            [A6XXRegs.REG_A7XX_GRAS_ROTATION_CNTL, 0x00000000],
            [A6XXRegs.REG_A6XX_GRAS_DBG_ECO_CNTL,  0x00004800],

            [A6XXRegs.REG_A7XX_RB_LRZ_CNTL2,      0x00000000],
            [A6XXRegs.REG_A7XX_RB_CCU_DBG_ECO_CNTL, 0x02082000],

            [A6XXRegs.REG_A7XX_VPC_FLATSHADE_MODE_CNTL, 1],

            [A6XXRegs.REG_A7XX_SP_PS_OUTPUT_CONST_CNTL, 0],
            [A6XXRegs.REG_A7XX_SP_PS_OUTPUT_CONST_MASK, 0],
            [A6XXRegs.REG_A6XX_TPL1_DBG_ECO_CNTL, 0x11100000],
            [A6XXRegs.REG_A6XX_VPC_DBG_ECO_CNTL, 0x02000000],
            [A6XXRegs.REG_A6XX_UCHE_UNKNOWN_0E12, 0x40000000],
        ],
    ))

a8xx_base = GPUProps(
        has_dp2acc = False,
        reg_size_vec4 = 96,
        has_rt_workaround = False,
        supports_double_threadsize = False,
        has_dual_wave_dispatch = True,
    )

# For a8xx, the chicken bit and most other non-ctx reg
# programming moves into the kernel, and what remains
# should be easier to share between devices
a8xx_base_raw_magic_regs = [
        [A6XXRegs.REG_A8XX_GRAS_BIN_FOVEAT_XY_FDM_OFFSET + 0, 0x00000000],
        [A6XXRegs.REG_A8XX_GRAS_BIN_FOVEAT_XY_FDM_OFFSET + 1, 0x00000000],
        [A6XXRegs.REG_A8XX_GRAS_BIN_FOVEAT_XY_FDM_OFFSET + 2, 0x00000000],
        [A6XXRegs.REG_A8XX_GRAS_BIN_FOVEAT_XY_FDM_OFFSET + 3, 0x00000000],
        [A6XXRegs.REG_A8XX_GRAS_BIN_FOVEAT_XY_FDM_OFFSET + 4, 0x00000000],
        [A6XXRegs.REG_A8XX_GRAS_BIN_FOVEAT_XY_FDM_OFFSET + 5, 0x00000000],

        [A6XXRegs.REG_A6XX_RB_UNKNOWN_8818,   0x00000000],
        [A6XXRegs.REG_A6XX_RB_UNKNOWN_8819,   0x00000000],
        [A6XXRegs.REG_A6XX_RB_UNKNOWN_881A,   0x00000000],
        [A6XXRegs.REG_A6XX_RB_UNKNOWN_881B,   0x00000000],
        [A6XXRegs.REG_A6XX_RB_UNKNOWN_881C,   0x00000000],
        [A6XXRegs.REG_A6XX_RB_UNKNOWN_881D,   0x00000000],
        [A6XXRegs.REG_A6XX_RB_UNKNOWN_881E,   0x00000000],
        [A6XXRegs.REG_A7XX_RB_LRZ_CNTL2,      0x00000000],
        [A6XXRegs.REG_A8XX_RB_RESOLVE_CNTL_5, 0x00000001],

        [A6XXRegs.REG_A7XX_SP_UNKNOWN_AB01,   0x00000000],
        [A6XXRegs.REG_A7XX_SP_HLSQ_MODE_CNTL, 0x00000000],
        [A6XXRegs.REG_A8XX_SP_UNKNOWN_AB23,   0x00000000],

        [A6XXRegs.REG_A6XX_TPL1_PS_ROTATION_CNTL, 0x00000004],
        [A6XXRegs.REG_A6XX_TPL1_PS_SWIZZLE_CNTL, 0x00000000],

        [A6XXRegs.REG_A8XX_PC_UNKNOWN_980B, 0x00800280],
        [A6XXRegs.REG_A8XX_PC_MODE_CNTL,    0x00003f00],
    ]

a8xx_gen1 = GPUProps(
        reg_size_vec4 = 96,
        sysmem_vpc_attr_buf_size = 131072,
        sysmem_vpc_pos_buf_size = 65536,
        sysmem_vpc_bv_pos_buf_size = 32768,
        sysmem_ccu_color_cache_fraction = CCUColorCacheFraction.FULL.value,
        sysmem_per_ccu_color_cache_size = 128 * 1024,
        sysmem_ccu_depth_cache_fraction = CCUColorCacheFraction.FULL.value,
        sysmem_per_ccu_depth_cache_size = 256 * 1024,
        gmem_vpc_attr_buf_size = 49152,
        gmem_vpc_pos_buf_size = 24576,
        gmem_vpc_bv_pos_buf_size = 32768,
        gmem_ccu_color_cache_fraction = CCUColorCacheFraction.EIGHTH.value,
        gmem_per_ccu_color_cache_size = 16 * 1024,
        gmem_ccu_depth_cache_fraction = CCUColorCacheFraction.FULL.value,
        gmem_per_ccu_depth_cache_size = 256 * 1024,

        has_salu_int_narrowing_quirk = True
)

a8xx_gen2 = GPUProps(
        reg_size_vec4 = 128,
        sysmem_vpc_attr_buf_size = 131072,
        sysmem_vpc_pos_buf_size = 65536,
        sysmem_vpc_bv_pos_buf_size = 32768,
        sysmem_ccu_color_cache_fraction = CCUColorCacheFraction.FULL.value,
        sysmem_per_ccu_color_cache_size = 128 * 1024,
        sysmem_ccu_depth_cache_fraction = CCUColorCacheFraction.THREE_QUARTER.value,
        sysmem_per_ccu_depth_cache_size = 192 * 1024,
        gmem_vpc_attr_buf_size = 49152,
        gmem_vpc_pos_buf_size = 24576,
        gmem_vpc_bv_pos_buf_size = 32768,
        gmem_ccu_color_cache_fraction = CCUColorCacheFraction.EIGHTH.value,
        gmem_per_ccu_color_cache_size = 16 * 1024,
        gmem_ccu_depth_cache_fraction = CCUColorCacheFraction.FULL.value,
        gmem_per_ccu_depth_cache_size = 256 * 1024,
        has_fs_tex_prefetch = False,

        has_salu_int_narrowing_quirk = True
)

a8xx_825 = GPUProps(
        sysmem_vpc_attr_buf_size = 131072,
        sysmem_vpc_pos_buf_size = 65536,
        sysmem_vpc_bv_pos_buf_size = 32768,
        sysmem_ccu_color_cache_fraction = CCUColorCacheFraction.FULL.value,
        sysmem_per_ccu_color_cache_size = 128 * 1024,
        sysmem_ccu_depth_cache_fraction = CCUColorCacheFraction.THREE_QUARTER.value,
        sysmem_per_ccu_depth_cache_size = 96 * 1024,
        gmem_vpc_attr_buf_size = 49152,
        gmem_vpc_pos_buf_size = 24576,
        gmem_vpc_bv_pos_buf_size = 32768,
        gmem_ccu_color_cache_fraction = CCUColorCacheFraction.EIGHTH.value,
        gmem_per_ccu_color_cache_size = 16 * 1024,
        gmem_ccu_depth_cache_fraction = CCUColorCacheFraction.FULL.value,
        gmem_per_ccu_depth_cache_size = 128 * 1024,
)

# gen8_6_0
add_gpus([
        GPUId(chip_id=0x44030000, name="FD825"),
    ], A6xxGPUInfo(
        CHIP.A8XX,
        [a7xx_base, a7xx_gen3, a8xx_base, a8xx_825],
        num_ccu = 4,
        num_slices = 2,
        tile_align_w = 96,
        tile_align_h = 32,
        tile_max_w = 16416,
        tile_max_h = 16384,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        magic_regs = dict(),
        raw_magic_regs = a8xx_base_raw_magic_regs,
    ))

add_gpus([
        GPUId(chip_id=0xffff44050000, name="Adreno (TM) 830"),
        GPUId(chip_id=0x44050001, name="Adreno (TM) 830"), # KGSL
    ], A6xxGPUInfo(
        CHIP.A8XX,
        [a7xx_base, a7xx_gen3, a8xx_base, a8xx_gen1],
        num_ccu = 6,
        num_slices = 3,
        tile_align_w = 96,
        tile_align_h = 32,
        tile_max_w = 16416,
        tile_max_h = 16384,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        magic_regs = dict(),
        raw_magic_regs = a8xx_base_raw_magic_regs,
    ))

add_gpus([
        GPUId(chip_id=0x44030a20, name="Adreno (TM) 829"), # KGSL
    ], A6xxGPUInfo(
        CHIP.A8XX,
        [a7xx_base, a7xx_gen3, a8xx_base, a8xx_gen2,
         GPUProps(
            shading_rate_matches_vk = True,  # TODO confirm this
            sysmem_vpc_bv_pos_buf_size = 24576,
         )],
        num_ccu = 4,
        num_slices = 2,
        tile_align_w = 64,
        tile_align_h = 32,
        tile_max_w = 16384,
        tile_max_h = 16384,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        magic_regs = dict(),
        raw_magic_regs = a8xx_base_raw_magic_regs,
    ))

add_gpus([
        GPUId(chip_id=0xffff44050A31, name="Adreno (TM) 840"),
    ], A6xxGPUInfo(
        CHIP.A8XX,
        [a7xx_base, a7xx_gen3, a8xx_base, a8xx_gen2,
         GPUProps(shading_rate_matches_vk = True)],
        num_ccu = 6,
        num_slices = 3,
        tile_align_w = 96,
        tile_align_h = 32,
        tile_max_w = 16416,
        tile_max_h = 16384,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        magic_regs = dict(),
        raw_magic_regs = a8xx_base_raw_magic_regs,
    ))

add_gpus([
        GPUId(chip_id=0xffff44070041, name="Adreno (TM) X2-85"),
    ], A6xxGPUInfo(
        CHIP.A8XX,
        [a7xx_base, a7xx_gen3, a8xx_base, a8xx_gen2],
        num_ccu = 8,
        num_slices = 4,
        tile_align_w = 64,
        tile_align_h = 64,
        tile_max_w = 16384,
        tile_max_h = 16384,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        fibers_per_sp = 128 * 2 * 16,
        magic_regs = dict(),
        raw_magic_regs = a8xx_base_raw_magic_regs,
    ))

if __name__ == "__main__":
    main()
