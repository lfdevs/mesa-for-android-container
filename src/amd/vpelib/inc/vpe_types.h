/* Copyright 2022-2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

/**
 * @file         vpe_types.h
 * @brief        This is the file containing the API structures for the VPE library.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include "vpe_hw_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vpe;

/** @def MAX_NB_POLYPHASE_COEFFS
 *
 *  @brief Maximum number of filter coefficients for polyphase scaling.
 *  VPE library supports up to 8 taps and 64 phases, only (32+1) phases needed
 */
#define MAX_NB_POLYPHASE_COEFFS (8 * 33)
#define VPE_FROD_MAX_STAGE 3

/** @enum vpe_status
 *  @brief The status of VPE to indicate whether it supports the given job or not.
 */
enum vpe_status {
    VPE_STATUS_OK = 1,                          /**<  VPE supports the job. */
    VPE_STATUS_ERROR,                           /**<  Unknown Error in VPE. */
    VPE_STATUS_NO_MEMORY,                       /**<  VPE is out of memory. */
    VPE_STATUS_NOT_SUPPORTED,                   /**<  VPE is not supported. */
    VPE_STATUS_INPUT_DCC_NOT_SUPPORTED,         /**<  Input DCC is not supported. */
    VPE_STATUS_OUTPUT_DCC_NOT_SUPPORTED,        /**<  Output DCC is not supported. */
    VPE_STATUS_SWIZZLE_NOT_SUPPORTED,           /**<  Swizzle mode is not supported. */
    VPE_STATUS_NUM_STREAM_NOT_SUPPORTED,        /**<  Number of streams is not supported. Too many
                                                   streams. */
    VPE_STATUS_PIXEL_FORMAT_NOT_SUPPORTED,      /**<  Pixel format is not supported. */
    VPE_STATUS_COLOR_SPACE_VALUE_NOT_SUPPORTED, /**<  Input DCC is not supported. */
    VPE_STATUS_SCALING_RATIO_NOT_SUPPORTED,     /**<  Given scaling is not supported. */
    VPE_STATUS_PITCH_ALIGNMENT_NOT_SUPPORTED,   /**<  Given pitch alignment is not supported. */
    VPE_STATUS_ROTATION_NOT_SUPPORTED,          /**<  Given rotation is not supported. */
    VPE_STATUS_MIRROR_NOT_SUPPORTED,            /**<  Given mirror is not supported. */
    VPE_STATUS_ALPHA_BLENDING_NOT_SUPPORTED,    /**<  Alpha blending is not supported. */
    VPE_STATUS_VIEWPORT_SIZE_NOT_SUPPORTED,     /**<  Given viewport size is not supported. */
    VPE_STATUS_LUMA_KEYING_NOT_SUPPORTED,       /**<  Luma keying is not supported. */
    VPE_STATUS_COLOR_KEYING_NOT_SUPPORTED,      /**<  Color keying is not supported. */
    VPE_STATUS_INVALID_KEYER_CONFIG,            /**<  Keying config is invalid. */
    VPE_STATUS_PLANE_ADDR_NOT_SUPPORTED,        /**<  Given plane address is not supported. */
    VPE_STATUS_ADJUSTMENT_NOT_SUPPORTED,        /**<  Color adjustment is not supported. */
    VPE_STATUS_CMD_OVERFLOW_ERROR,              /**<  More than 256 commands/jobs. */
    VPE_STATUS_SEGMENT_WIDTH_ERROR,             /**<  Calculated segment width is not supported. */
    VPE_STATUS_PARAM_CHECK_ERROR,               /**<  Given parametrs is not supported. */
    VPE_STATUS_TONE_MAP_NOT_SUPPORTED,          /**<  Tone mapping is not supported for the given
                                                   job. */
    VPE_STATUS_BAD_TONE_MAP_PARAMS,             /**<  Invalid tone mapping parameters. */
    VPE_STATUS_BAD_HDR_METADATA,                /**<  Ivalid HDR metadata. */
    VPE_STATUS_BUFFER_OVERFLOW,                 /**<  Buffer overflow. */
    VPE_STATUS_BUFFER_UNDERRUN,                 /**<  Buffer does not have enough capacity. */
    VPE_STATUS_BG_COLOR_OUT_OF_RANGE,           /**<  Given backgroud color does not lie in the
                                                   range of output color. */
    VPE_STATUS_REPEAT_ITEM,                     /**<  Descriptor writer is on a repeated job.
                                                   Used internally */
    VPE_STATUS_PATCH_OVER_MAXSIZE,              /**<  Descriptor writer patch size is larger than
                                                   supported path size. */
    VPE_STATUS_INVALID_BUFFER_SIZE,             /**<  Provided buffer size is less than required
                                                   buffer size. */
    VPE_STATUS_SCALER_NOT_SET,                  /**<  Scaler parameters are not set. */
    VPE_STATUS_GEOMETRICSCALING_ERROR,          /**<  Geometric scaling is not supported for the
                                                   given case. */
    VPE_INVALID_HISTOGRAM_SELECTION,
    VPE_STATUS_HISTOGRAM_NOT_SUPPORTED,         /**<  Histogram is not supported. */
    VPE_STATUS_FROD_NOT_SUPPORTED,              /**<  FROD is not supported. */
    VPE_STATUS_LUT_COMPOUND_NOT_SUPPORTED,      /**<  LUT Compound (CSC+1D+3D) is not supported. */
};

/*****************************************************
 * Enum for emitting VPE System Events
 *****************************************************/

/** @enum vpe_event_id
 *  @brief Event IDs are VPE events that can be emitted through
 *         the EventLog callback. For each event ID, the number of params
 *         emitted must by synchronized with handler
 */
enum vpe_event_id {
    VPE_EVENT_CHECK_SUPPORT,     /**< Event emitted by vpe_check_support.
                                      Params:
                                              UInt32 num_streams,
                                              UInt32 target_rect.width,
                                              UInt32 target_rect.height,
                                              UInt32 target_rect.height */
    VPE_EVENT_PLANE_DESC_INPUT,  /**< Event emitted by vpe_plane_desc_input */
    VPE_EVENT_PLANE_DESC_OUTPUT, /**< Event emitted by vpe_plane_desc_output */

    VPE_EVENT_MAX_ID         /**< Max ID represents the number of event IDs supported */
};

/** @enum vpe_ip_level
 *  @brief HW IP level
 */
enum vpe_ip_level {
    VPE_IP_LEVEL_UNKNOWN = (-1),
    VPE_IP_LEVEL_1_0, /**< vpe 1.0 */
    VPE_IP_LEVEL_1_1, /**< vpe 1.1 */
    VPE_IP_LEVEL_2_0, /**< vpe 2.0 */
};

enum vpe_mps_mode {
    VPE_MPS_DISABLED = 0,
    VPE_MPS_BLENDING_ONLY,
    VPE_MPS_ENABLED
};

enum vpe_hist_collection_mode {
    VPE_HISTOGRAM_NONE = 0,     /**< Disable histogram collection in channel 0. */
    VPE_HISTOGRAM_R_Cr,         /**< Create a histogram from R or Cr for RGB/YCbCr input surfaces respectivley. */
    VPE_HISTOGRAM_G_Y,          /**< Create a histogram from G or Y for RGB/YCbCr input surfaces respectivley */
    VPE_HISTOGRAM_B_CB,         /**< Create a histogram from B or Cb for RGB/YCbCr input surfaces respectivley */
    VPE_HISTOGRAM_MAX_RGB_YCbCr, /**< Create a histogram from MAX(R,G,B) or MAX(Y,Cb,Cr) for RGB/YCbCr input surfaces respectivley */
    VPE_HISTOGRAM_RGB_TRANSFORMED_Y, /**< Create a histogram of luma from transformed RGB. If the input surfae is YCbCr, this mode wll default to collecting Y directly. */
    VPE_HISTOGRAM_MIN_RGB_YCbCr, /**< Create a histogram from MIN(R,G,B) or MIN (Y,Cb,Cr) for RGB/YCbCr input surfaces respectivley */
    VPE_HISTOGRAM_LAST_TYPE
};

enum hist_channels {
    hist_channel1 = 0,
    hist_channel2,
    hist_channel3,
    hist_max_channel
};

static const enum vpe_hist_collection_mode channel_hist_allowed_mode[hist_max_channel][2] = {
    {VPE_HISTOGRAM_R_Cr, VPE_HISTOGRAM_MAX_RGB_YCbCr},
    {VPE_HISTOGRAM_G_Y,  VPE_HISTOGRAM_RGB_TRANSFORMED_Y},
    {VPE_HISTOGRAM_B_CB, VPE_HISTOGRAM_MIN_RGB_YCbCr} };

/****************************************
 * Plane Caps
 ****************************************/

/** @struct vpe_pixel_format_support
 *  @brief Capability to support formats
 */
struct vpe_pixel_format_support {
    uint32_t argb_packed_32b : 1; /**< Packed RGBA formats 32-bits per pixel */
    uint32_t nv12            : 1; /**< planar 4:2:0 8-bits */
    uint32_t fp16            : 1; /**< Floating point RGB 16-bits */
    uint32_t p010            : 1; /**< planar 4:2:0 10-bits */
    uint32_t p016            : 1; /**< planar 4:2:0 16-bits */
    uint32_t ayuv            : 1; /**< packed 4:4:4 8-bits */
    uint32_t yuy2            : 1; /**< packed 4:2:2 8-bits */
    uint32_t y210            : 1; /**< packed 4:2:2 10-bit */
    uint32_t y216            : 1; /**< packed 4:2:2 16-bit */
    uint32_t p210            : 1; /**< planar 4:2:2 10-bit */
    uint32_t p216            : 1; /**< planar 4:2:2 16-bit */
    uint32_t rgb8_planar     : 1; /**< planar RGB 8-bit */
    uint32_t rgb16_planar    : 1; /**< planar RGB 16-bit */
    uint32_t yuv8_planar     : 1; /**< planar YUV 16-bit */
    uint32_t yuv16_planar    : 1; /**< planar YUV 16-bit */
    uint32_t fp16_planar     : 1; /**< planar RGB 8-bit */
    uint32_t rgbe            : 1; /**< shared exponent R9G9B9E5 */
    uint32_t rgb111110_fix   : 1; /**< fixed R11G11B10 */
    uint32_t rgb111110_float : 1; /**< float R11G11B10 */
    uint32_t argb_packed_64b : 1; /**< Packed RGBA formats 64-bits per pixel */
};

/** @struct vpe_plane_caps
 *  @brief Capability to support given plane
 */
struct vpe_plane_caps {
    uint32_t per_pixel_alpha : 1; /**< Per-pixel alpha */

    struct vpe_pixel_format_support
        input_pixel_format_support;  /**< Input pixel format capability */
    struct vpe_pixel_format_support
        output_pixel_format_support; /**< Output pixel format capability */

    uint32_t max_upscale_factor;     /**< Maximum upscaling factor (dst/src) x 1000.
                                        E.g. 1080p -> 4k is 4000 */
    uint32_t max_downscale_factor;   /**< Maximum downscaling factor (dst/src) x 1000.
                                        E.g. 4k -> 1080p is 250 */

    uint32_t pitch_alignment;        /**< Pitch alignment in bytes */
    uint32_t addr_alignment;         /**< Plane address alignment in bytes */
    uint32_t max_viewport_width;     /**< Maximum viewport size */
    uint32_t max_viewport_width_64bpp; /**< Maximum viewport size for 64bpp formats with 90/270
                                          degrees rotation */
};

/*************************
 * Color management caps
 *************************/

/** @struct vpe_rom_curve_caps
 *  @brief Capability to support given transfer function
 */
struct vpe_rom_curve_caps {
    uint32_t srgb     : 1; /**< SRGB Gamma */
    uint32_t bt2020   : 1; /**< BT 2020 */
    uint32_t gamma2_2 : 1; /**< Gamma 2.2 */
    uint32_t pq       : 1; /**< Perceptual Quantizer */
    uint32_t hlg      : 1; /**< Hybrid log-gamma */
};

/** @struct dpp_color_caps
 *  @brief Color management caps for dpp layer
 */
struct dpp_color_caps {
    uint32_t                  pre_csc    : 1; /**< pre CSC */
    uint32_t                  luma_key   : 1; /**< luma key */
    uint32_t                  color_key  : 1; /**< color key */
    uint32_t                  dgam_ram   : 1; /**< Dgam */
    uint32_t                  post_csc   : 1; /**< before gamut remap */
    uint32_t                  gamma_corr : 1; /**< Gamut correction */
    uint32_t                  hw_3dlut   : 1; /**< HW 3D LUT */
    uint32_t                  ogam_ram   : 1; /**< Ogam */
    uint32_t                  ocsc       : 1; /**< OCSC */
    struct vpe_rom_curve_caps dgam_rom_caps;  /**< Dgam Rom Caps */
};

/** @struct lut_caps
 *  @brief LUT (Look-Up Table) capabilities
 *  This structure defines the capabilities for LUT shaper and 3D LUTs.
 */
struct vpe_lut_caps {
    struct {
        uint32_t dma_data      : 1;    /**< DMA data support */
        uint32_t dma_config    : 1;    /**< DMA configuration support */
        uint32_t non_monotonic : 1;    /**< Non-monotonic LUT support */
        uint16_t data_alignment;       /**< Data alignment in bytes */
        uint16_t config_alignment;     /**< Configuration alignment in bytes */
        uint16_t config_padding;       /**< Configuration padding in bytes */
        uint16_t data_size;            /**< Data size in bytes */
        uint16_t config_size;          /**< Configuration size in bytes */
        uint16_t data_pts_per_channel; /**< Number data of points per channel */
    } lut_shaper_caps;

    struct {
        uint32_t data_dim_9  : 1; /**< Support for 9x9x9 3D LUT */
        uint32_t data_dim_17 : 1; /**< Support for 17x17x17 3D LUT */
        uint32_t data_dim_33 : 1; /**< Support for 33x33x33 3D LUT */
        union {
            struct {
                uint32_t dma_dim_9  : 1; /**< DMA support for 9x9x9 3D LUT */
                uint32_t dma_dim_17 : 1; /**< DMA support for 17x17x17 3D LUT */
                uint32_t dma_dim_33 : 1; /**< DMA support for 33x33x33 3D LUT */
            };
            uint32_t dma;                /**< Any DMA support if set */
        };
        uint16_t alignment;              /**< 3D lUT Alignment in bytes */
    } lut_3dlut_caps;

    uint32_t lut_3d_compound : 1; /**< Support for 3D LUT compound */
};
/** @struct mpc_color_caps
 *  @brief Color management caps for mpc layer
 */
struct mpc_color_caps {
    uint32_t gamut_remap         : 1; /**< Gamut remap */
    uint32_t ogam_ram            : 1; /**< Ogam */
    uint32_t ocsc                : 1; /**< OCSC */
    uint32_t shared_3d_lut       : 1; /**< can be in either dpp or mpc, but single instance */
    uint32_t global_alpha        : 1; /**< e.g. top plane 30 %. bottom 70 % */
    uint32_t top_bottom_blending : 1; /**< two-layer blending */

    uint32_t dma_3d_lut : 1; /**< DMA mode support for 3D LUT, Legacy interface, will be replaced by
                          vpe_lut_caps*/
    uint32_t yuv_linear_blend : 1; /**< Support for linear blending of 3D LUT YUV output */
    struct {
        uint32_t dim_9 : 1;  /**< 3D LUT support for 9x9x9 ,Legacy interface, will be replaced by
                                vpe_lut_caps*/
        uint32_t dim_17 : 1; /**< 3D LUT support for 17x17x17, Legacy interface, will be replaced by
                                vpe_lut_caps */
        uint32_t dim_33 : 1; /**< 3D LUT support for 33x33x33, Legacy interface, will be replaced by
                                vpe_lut_caps */
    } lut_dim_caps;

    struct {
        uint32_t lut_3d_17 : 1;   /**< 3D LUT 17x17x17 container fastload support, default 0,Legacy
                                     interface, will be replaced by vpe_lut_caps */
        uint32_t lut_3d_33 : 1;   /**< 3D LUT 33x33x33 container fastload support, default 0,Legacy
                                     interface, will be replaced by vpe_lut_caps */
    } fast_load_caps;

    struct vpe_lut_caps lut_caps; /**< LUT capabilities for shaper and 3D LUT configurations. */
};

/** @struct vpe_color_caps
 *  @brief VPE color management caps
 */
struct vpe_color_caps {
    struct dpp_color_caps dpp; /**< DPP color caps */
    struct mpc_color_caps mpc; /**< MPC color caps */
};

/** @struct vpe_caps
 *  @brief VPE Capabilities
 *  Those depend on the condition like input format
 *  shall be queried by @ref vpe_check_support_funcs
 */
struct vpe_caps {
    struct vpe_size max_input_size;      /**< Maximum input size */
    struct vpe_size min_input_size;      /**< Minimum input size */
    struct vpe_size max_output_size;     /**< Maximum output size */
    struct vpe_size min_output_size;     /**< Minimum output size */
    uint32_t        max_downscale_ratio; /**< max downscaling ratio (src/dest) x 100.
                                              E.g. 4k -> 1080p is 400 */
    uint64_t lut_size;                   /**< 3dlut size */

    uint32_t rotation_support       : 1; /**< rotation support */
    uint32_t h_mirror_support       : 1; /**< horizontal mirror support */
    uint32_t v_mirror_support       : 1; /**< vertical mirror support */
    uint32_t is_apu                 : 1; /**< is APU */
    uint32_t bg_color_check_support : 1; /**< background color check support */

    uint32_t prefer_external_scaler_coef : 1; /**< prefer external scaler coeff */

    /** resource capability */
    struct {
        uint32_t num_dpp;             /**< num of dpp */
        uint32_t num_opp;             /**< num of opp */
        uint32_t num_mpc_3dlut;       /**< num of mpc 3dlut */
        uint32_t num_cdc_be;          /**< num of cdc backend */
        uint32_t num_queue;           /**< num of hw queue */
    } resource_caps;                  /**< resource capability */

    struct vpe_color_caps color_caps; /**< Color management caps */
    struct vpe_plane_caps plane_caps; /**< Plane capabilities */

    uint32_t input_dcc_support      : 1; /**< Input DCC support */
    uint32_t input_internal_dcc     : 1; /**< Input internal DCC */
    uint32_t output_dcc_support     : 1; /**< Output DCC support */
    uint32_t output_internal_dcc    : 1; /**< Output internal DCC */
    uint32_t histogram_support      : 1; /**< Histogram support */
    uint32_t frod_support           : 1; /**< FROD support */
    uint32_t alpha_blending_support : 1; /**< Alpha blending support */
    uint32_t easf_support           : 1; /**< edge adaptive scaling support */
    struct {
        bool support;      /**< iSharp support */
        struct {
            uint32_t min;  /**< iSharp min level */
            uint32_t max;  /**< iSharp max level */
            uint32_t step; /**< iSharp level steps */
        } range;
    } isharp_caps;
    struct {
        uint32_t opaque        : 1;
        uint32_t bg_color      : 1;
        uint32_t destination   : 1;
        uint32_t source_stream : 1;
    } alpha_fill_caps;
};

/***********************************
 * Conditional Capabilities
 ***********************************/
/** @struct vpe_dcc_surface_param
 *  @brief DCC surface parameters
 */
struct vpe_dcc_surface_param {
    struct vpe_size               surface_size; /**< surface size */
    enum vpe_surface_pixel_format format;       /**< surface format */
    enum vpe_swizzle_mode_values  swizzle_mode; /**< swizzle mode */
    enum vpe_scan_direction       scan;         /**< scan direction */
    enum vpe_mirror               mirror;       /**< mirror */
};

/** @struct vpe_dcc_setting
 *  @brief DCC Settings
 */
struct vpe_dcc_setting {
    unsigned int max_compressed_blk_size;   /**< max compressed block size */
    unsigned int max_uncompressed_blk_size; /**< max uncompressed block size */
    bool         independent_64b_blks;      /**< independent 64b blocks */

    /** DCC controls */
    struct {
        uint32_t dcc_256_64_64             : 1; /**< DCC 256 64 64 */
        uint32_t dcc_128_128_uncontrained  : 1; /**< DCC 128 128 unconstrained */
        uint32_t dcc_256_128_128           : 1; /**< DCC 256 128 128 */
        uint32_t dcc_256_256_unconstrained : 1; /**< DCC 256 256 unconstrained */
    } dcc_controls;
};

/** @struct vpe_surface_dcc_cap
 *  @brief DCC Capabilities
 */
struct vpe_surface_dcc_cap {
    /**
     * @brief Union of graphics and video dcc settings
     */
    union {
        /** graph dcc setting */
        struct {
            struct vpe_dcc_setting rgb; /**< dcc setting for RGB */
        } grph;

        /** video dcc settings */
        struct {
            struct vpe_dcc_setting luma;   /**< dcc setting for luma */
            struct vpe_dcc_setting chroma; /**< dcc setting for chroma */
        } video;
    };

    bool capable;             /**< DCC capable */
    bool const_color_support; /**< DCC const color support */

    bool is_internal_dcc;
};

/****************************************
 * VPE Init Param
 ****************************************/
/** @brief Log function
 * @param[in] log_ctx  given in the struct @ref vpe_init_data
 * @param[in] fmt      format string
 */
typedef void (*vpe_log_func_t)(void *log_ctx, const char *fmt, ...);

/** @brief Sys Event function
 * @param[in] event_id event to emit to system log
 */
typedef void (*vpe_sys_event_func_t)(enum vpe_event_id event_id, ...);

/** @brief system memory zalloc, allocated memory initailized with 0
 *
 * @param[in] mem_ctx  given in the struct @ref vpe_init_data
 * @param[in] size     number of bytes
 * @return             allocated memory
 */
typedef void *(*vpe_zalloc_func_t)(void *mem_ctx, size_t size);

/** @brief system memory free
 * @param[in] mem_ctx  given in the struct @ref vpe_init_data
 * @param[in] ptr      number of bytes
 */
typedef void (*vpe_free_func_t)(void *mem_ctx, void *ptr);

/** @struct vpe_callback_funcs
 *  @brief Callback functions.
 */
struct vpe_callback_funcs {
    void          *log_ctx; /**< optional. provided by the caller and pass back to callback */
    vpe_log_func_t log;     /**< Logging function */

    vpe_sys_event_func_t sys_event; /**< System event function */

    void             *mem_ctx; /**< optional. provided by the caller and pass back to callback */
    vpe_zalloc_func_t zalloc;  /**< Memory allocation */
    vpe_free_func_t   free;    /**< Free memory. In sync with @ref vpe_zalloc_func_t */
};

/** @struct vpe_mem_low_power_enable_options
 *  @brief Component activation on low power mode. Only used for debugging.
 */
struct vpe_mem_low_power_enable_options {
    /** override flags */
    struct {
        uint32_t dscl : 1; /**< DSCL */
        uint32_t cm   : 1; /**< CM */
        uint32_t mpc  : 1; /**< MPC */
    } flags;

    /** enable bits */
    struct {
        uint32_t dscl : 1; /**< DSCL */
        uint32_t cm   : 1; /**< CM */
        uint32_t mpc  : 1; /**< MPC */
    } bits;
};

/** @enum vpe_expansion_mode
 *  @brief Color component expansion mode
 */
enum vpe_expansion_mode {
    VPE_EXPANSION_MODE_DYNAMIC, /**< Dynamic expansion */
    VPE_EXPANSION_MODE_ZERO     /**< Zero expansion */
};

/** @enum vpe_clamping_range
 *  @brief Color clamping
 */
enum vpe_clamping_range {
    VPE_CLAMPING_FULL_RANGE = 0,             /**< No Clamping */
    VPE_CLAMPING_LIMITED_RANGE_8BPC,         /**< 8  bpc: Clamping 1  to FE */
    VPE_CLAMPING_LIMITED_RANGE_10BPC,        /**< 10 bpc: Clamping 4  to 3FB */
    VPE_CLAMPING_LIMITED_RANGE_12BPC,        /**< 12 bpc: Clamping 10 to FEF */
    VPE_CLAMPING_LIMITED_RANGE_PROGRAMMABLE, /**< Programmable. Use programmable clampping value on
                                                FMT_CLAMP_COMPONENT_R/G/B. */
};

/** @struct vpe_clamping_params
 *  @brief Upper and lower bound of each color channel for clamping.
 */
struct vpe_clamping_params {
    enum vpe_clamping_range clamping_range;          /**< Clamping range */
    uint32_t                r_clamp_component_upper; /**< Red channel upper bound */
    uint32_t                b_clamp_component_upper; /**< Blue channel upper bound */
    uint32_t                g_clamp_component_upper; /**< Green channel upper bound */
    uint32_t                r_clamp_component_lower; /**< Red channel lower bound */
    uint32_t                b_clamp_component_lower; /**< Blue channel lower bound */
    uint32_t                g_clamp_component_lower; /**< Green channel lower bound */
};

/** @struct vpe_visual_confirm
 *  @brief Configurable parameters for visual confirm bar
 */
struct vpe_visual_confirm {
    /** @brief confirm value
     */
    union {
        /** @brief confirm fields
         */
        struct {
            uint32_t input_format  : 1; /**< input format, 0: disable, 1: enable*/
            uint32_t output_format : 1; /**< output format, 0: disable, 1: enable*/
            uint32_t pipe_idx : 1;      /**< pipe index, 0: disable, 1: enable*/
            uint32_t reserved : 29;     /**< reserved */
        };
        uint32_t value; /**< confirm value */
    };
};

/** @struct vpe_debug_options
 *  @brief Configurable parameters for debugging purpose
 */
struct vpe_debug_options {

    /** Struct to specify whether the debug flag for that
     *  corresponding field should be honored.
     */
    struct {
        uint32_t cm_in_bypass             : 1; /**< Color management bypass */
        uint32_t vpcnvc_bypass            : 1; /**< VPCNVC bypass */
        uint32_t mpc_bypass               : 1; /**< MPC bypass */
        uint32_t identity_3dlut           : 1; /**< Identity 3dlut */
        uint32_t sce_3dlut                : 1; /**< SCE 3dlut */
        uint32_t disable_reuse_bit        : 1; /**< Disable reuse bit */
        uint32_t bg_color_fill_only       : 1; /**< Background color fill only */
        uint32_t assert_when_not_support  : 1; /**< Assert when not supported */
        uint32_t bypass_gamcor            : 1; /**< Bypass gamcor */
        uint32_t bypass_ogam              : 1; /**< Bypass ogam */
        uint32_t bypass_dpp_gamut_remap   : 1; /**< Bypass dpp gamut remap */
        uint32_t bypass_post_csc          : 1; /**< Bypass post csc */
        uint32_t bypass_blndgam           : 1; /**< Bypass blndgam */
        uint32_t clamping_setting         : 1; /**< Clamping setting */
        uint32_t expansion_mode           : 1; /**< Color component expansion mode */
        uint32_t bypass_per_pixel_alpha   : 1; /**< Per-pixel alpha bypass */
        uint32_t dpp_crc_ctrl             : 1; /**< DPP CRC control */
        uint32_t opp_pipe_crc_ctrl        : 1; /**< OPP pipe CRC control */
        uint32_t mpc_crc_ctrl             : 1; /**< MPC CRC control */
        uint32_t bg_bit_depth             : 1; /**< Background color bit depth. */
        uint32_t visual_confirm           : 1; /**< visual confirm */
        uint32_t skip_optimal_tap_check   : 1; /**< Skip optimal tap check */
        uint32_t disable_lut_caching      : 1; /**< disable config caching for all luts */
        uint32_t disable_performance_mode : 1; /**< disable performance mode */
        uint32_t multi_pipe_segmentation_policy : 1; /**< policy for when to use MPS feature */
        uint32_t opp_background_gen             : 1; /**< generate bg color in opp (default mpc) */
        uint32_t subsampling_quality            : 1; /**< subsample quality */
        uint32_t disable_3dlut_fl               : 1; /**< disable 3dlut fastloading */
        uint32_t reserved : 4;
    } flags;                                  /**< debug flags */

    // valid only if the corresponding flag is set
    uint32_t cm_in_bypass             : 1; /**< Color management bypass */
    uint32_t vpcnvc_bypass            : 1; /**< VPCNVC bypass */
    uint32_t mpc_bypass               : 1; /**< MPC bypass */
    uint32_t identity_3dlut           : 1; /**< Identity 3dlut */
    uint32_t sce_3dlut                : 1; /**< SCE 3dlut */
    uint32_t disable_reuse_bit        : 1; /**< Disable reuse bit */
    uint32_t bg_color_fill_only       : 1; /**< Background color fill only */
    uint32_t assert_when_not_support  : 1; /**< Assert when not supported */
    uint32_t bypass_gamcor            : 1; /**< Bypass gamcor */
    uint32_t bypass_ogam              : 1; /**< Bypass ogam */
    uint32_t bypass_dpp_gamut_remap   : 1; /**< Bypass dpp gamut remap */
    uint32_t bypass_post_csc          : 1; /**< Bypass post csc */
    uint32_t bypass_blndgam           : 1; /**< Bypass blndgam */
    uint32_t clamping_setting         : 1; /**< Clamping setting */
    uint32_t bypass_per_pixel_alpha   : 1; /**< Per-pixel alpha bypass */
    uint32_t dpp_crc_ctrl             : 1; /**< DPP CRC control */
    uint32_t opp_pipe_crc_ctrl        : 1; /**< OPP pipe CRC control */
    uint32_t mpc_crc_ctrl             : 1; /**< MPC CRC control */
    uint32_t skip_optimal_tap_check   : 1; /**< Skip optimal tap check */
    uint32_t disable_lut_caching      : 1; /**< disable config caching for all luts */
    uint32_t disable_performance_mode : 1; /**< disable performance mode */
    uint32_t multi_pipe_segmentation_policy : 2; /**< policy mode for when to use MPS */
    uint32_t opp_background_gen             : 1; /**< switch bg gen to OPP */
    uint32_t subsampling_quality            : 2; /**< subsample quality */
    uint32_t disable_3dlut_fl               : 1; /**< disable 3dlut fastloading */
    uint32_t bg_bit_depth;                /**< Background color bit depth. */

    struct vpe_mem_low_power_enable_options
        enable_mem_low_power; /**< Component activation on low power mode. Only used for debugging.
                               */
    enum vpe_expansion_mode    expansion_mode;        /**< Color component expansion mode */
    struct vpe_clamping_params clamping_params;       /**< Color clamping */
    struct vpe_visual_confirm  visual_confirm_params; /**< Visual confirm bar parameters */
};

/** @struct vpe_init_data
 *  @brief VPE ip info and debug/callback functions
 */
struct vpe_init_data {
    uint8_t                   ver_major;     /**< vpe major version */
    uint8_t                   ver_minor;     /**< vpe minor version */
    uint8_t                   ver_rev;       /**< vpe revision version */
    struct vpe_callback_funcs funcs;         /**< function callbacks */
    struct vpe_debug_options  debug;         /**< debug options */
    const struct vpe_engine  *engine_handle; /**< vpe instance */
};

/*****************************************************
 * Structures for build VPE command
 *****************************************************/

/** @enum vpe_pixel_encoding
 *  @brief Color space format
 */
enum vpe_pixel_encoding {
    VPE_PIXEL_ENCODING_YCbCr, /**< YCbCr Color space format */
    VPE_PIXEL_ENCODING_RGB,   /**< RGB Color space format */
    VPE_PIXEL_ENCODING_COUNT
};

/** @enum vpe_color_range
 *  @brief Color Range
 */
enum vpe_color_range {
    VPE_COLOR_RANGE_FULL,   /**< Full range */
    VPE_COLOR_RANGE_STUDIO, /**< Studio/limited range */
    VPE_COLOR_RANGE_COUNT
};

/** @enum vpe_chroma_cositing
 *  @brief Chroma Cositing
 *
 * The position of the chroma for the chroma sub-sampled pixel formats.
 */
enum vpe_chroma_cositing {
    VPE_CHROMA_COSITING_NONE,    /**< No cositing */
    VPE_CHROMA_COSITING_LEFT,    /**< Left cositing */
    VPE_CHROMA_COSITING_TOPLEFT, /**< Top-left cositing */
    VPE_CHROMA_COSITING_COUNT
};

/** @enum vpe_color_primaries
 *  @brief Color Primaries
 */
enum vpe_color_primaries {
    VPE_PRIMARIES_BT601,  /**< BT. 601, Rec. 601 */
    VPE_PRIMARIES_BT709,  /**< BT. 709, Rec. 709 */
    VPE_PRIMARIES_BT2020, /**< BT. 2020, Rec. 2020 */
    VPE_PRIMARIES_JFIF,   /**< JPEG File Interchange Format */
    VPE_PRIMARIES_CUSTOM, /**< Custom / user controlled */
    VPE_PRIMARIES_COUNT
};

/** @enum vpe_transfer_function
 *  @brief Gamma Transfer function
 */
enum vpe_transfer_function {
    VPE_TF_G22,           /**< Gamma 2.2 */
    VPE_TF_G24,           /**< Gamma 2.4 */
    VPE_TF_G10,           /**< Linear */
    VPE_TF_PQ,            /**< Perceptual Quantizer */
    VPE_TF_PQ_NORMALIZED, /**< Normalized Perceptual Quantizer */
    VPE_TF_HLG,           /**< Hybrid Log-Gamma */
    VPE_TF_SRGB,          /**< Standard RGB */
    VPE_TF_BT709,         /**< BT 709 */
    VPE_TF_CUSTOM,        /**< Custom / user controlled */
    VPE_TF_COUNT
};

/** @enum vpe_alpha_mode
 *  @brief Alpha mode of the stream.
 */
enum vpe_alpha_mode {
    VPE_ALPHA_OPAQUE,        /**< Opaque. In this mode, If output has alpha channel, it is set to
                              * maximum value. For FP16 format it is set to 125.0f,
                              * and 2^(AlphaChannelBitDepth)-1 for other formats.
                              */
    VPE_ALPHA_BGCOLOR,       /**< If the output has alpha channel, sets the output alpha to be the
                              * alpha value of the user-provided background color.
                              */
    VPE_ALPHA_DESTINATION,   /**< If the output has alpha channel, sets the output alpha to be the
                              * alpha value of the destination pixel.
                              */
    VPE_ALPHA_SOURCE_STREAM, /**< If the output has alpha channel, sets the output alpha to be the
                              * alpha value of the source pixel.
                              */
};

/** @struct vpe_color_space
 *  @brief Color space parameters.
 */
struct vpe_color_space {
    enum vpe_pixel_encoding    encoding;  /**< Color space format. RGBA vs. YCbCr */
    enum vpe_color_range       range;     /**< Color range. Full vs. Studio */
    enum vpe_transfer_function tf;        /**< Transfer Function/Gamma */
    enum vpe_chroma_cositing   cositing;  /**< Chroma Cositing */
    enum vpe_color_primaries   primaries; /**< Color primaries */
};

/** @struct vpe_color_rgba
 *  @brief Color value of each channel for RGBA color space formats.
 *
 *  Component values are in the range: 0.0f - 1.0f
 */
struct vpe_color_rgba {
    float r; /**< Red Channel*/
    float g; /**< Green Channel*/
    float b; /**< Blue Channel*/
    float a; /**< Alpha Channel*/
};

/** @struct vpe_color_ycbcra
 *  @brief Color value of each channel for YCbCr color space formats.
 *
 *  Component values are in the range: 0.0f - 1.0f
 */
struct vpe_color_ycbcra {
    float y;  /**< Luminance/Luma Channel */
    float cb; /**< Blue-difference Chrominance/Chroma Channel */
    float cr; /**< Red-difference Chrominance/Chroma Channel */
    float a;  /**< Alpha Channel */
};

/** @struct vpe_color
 *  @brief Color value of each pixel
 */
struct vpe_color {
    bool is_ycbcr; /**< Set if the color space format is YCbCr.
                      If Ture, use @ref vpe_color_rgba. If False, use @ref vpe_color_ycbcra. */
    /** @brief color values
     */
    union {
        struct vpe_color_rgba   rgba;   /**< RGBA value */
        struct vpe_color_ycbcra ycbcra; /**< YCbCr value */
    };
};

/** @struct vpe_color_adjust
 * @brief Color adjustment values
 * @pre
 * Adjustment     Min      Max    default   step
 *
 * Brightness  -100.0f,  100.0f,   0.0f,    0.1f
 *
 * Contrast       0.0f,    2.0f,    1.0f,   0.01f
 *
 * Hue         -180.0f,  180.0f,   0.0f,    1.0f
 *
 * Saturation     0.0f,    3.0f,   1.0f,    0.01f
 */
struct vpe_color_adjust {
    float brightness; /**< Brightness */
    float contrast;   /**< Contrast */
    float hue;        /**< Hue */
    float saturation; /**< Saturation */
};

/** @struct vpe_surface_info
 *  @brief Surface address and properties
 *
 */
struct vpe_surface_info {

    struct vpe_plane_address     address;     /**< Address */
    union {
        enum vpe_swizzle_mode_values swizzle; /**< Swizzle mode */
    };

    struct vpe_plane_size         plane_size; /**< Pitch */
    struct vpe_plane_dcc_param    dcc;        /**< DCC parameters */
    enum vpe_surface_pixel_format format;     /**< Surface pixel format */

    struct vpe_color_space cs;                /**< Surface color space */
};

/** @struct vpe_blend_info
 *  @brief Blending parameters
 */
struct vpe_blend_info {
    bool  blending;             /**< Enable blending */
    bool  pre_multiplied_alpha; /**< Is the pixel value pre-multiplied with alpha */
    bool  global_alpha;         /**< Enable global alpha */
    float global_alpha_value;   /**< Global alpha value. In range of 0.0-1.0 */
};

/** @struct vpe_sharpness_range
 *  @brief Specifies the sharpness to be applied by the scaler (DSCL)
 */
struct vpe_sharpness_range {
    int sdr_rgb_min; /**< SDR RGB sharpness min */
    int sdr_rgb_max; /**< SDR RGB sharpness max */
    int sdr_rgb_mid; /**< SDR RGB sharpness mid */
    int sdr_yuv_min; /**< SDR YUV sharpness min */
    int sdr_yuv_max; /**< SDR YUV sharpness max */
    int sdr_yuv_mid; /**< SDR YUV sharpness mid */
    int hdr_rgb_min; /**< HDR RGB sharpness min */
    int hdr_rgb_max; /**< HDR RGB sharpness max */
    int hdr_rgb_mid; /**< HDR RGB sharpness mid */
};

/** @struct vpe_adaptive_sharpness
 *  @brief Adaptive sharpness parameters
 */
struct vpe_adaptive_sharpness {
    bool                       enable;          /**< Enable adaptive sharpness */
    unsigned int               sharpness_level; /**< Sharpness level */
    struct vpe_sharpness_range sharpness_range; /**< Sharpness range */
};

/** @struct vpe_scaling_info
 *  @brief Data needs to calculate scaling data.
 */
struct vpe_scaling_info {
    struct vpe_rect         src_rect; /**< Input frame/stream rectangle*/
    struct vpe_rect         dst_rect; /**< Output rectangle on the destination surface. */
    struct vpe_scaling_taps taps;     /**< Number of taps to be used for scaler.
                                       * If taps are set to 0, vpe internally calculates the
                                       * required number of taps based on the scaling ratio.
                                       */
    // Adaptive scaling and sharpening params
    struct vpe_adaptive_sharpness adaptive_sharpeness; /**< Adaptive scaler sharpness mode. */
    bool                          enable_easf;         /**< Enable edge adaptive scaling */
    bool                          prefer_easf;         /**< Edge adaptive scaling is prefered if
                                                          can be performed. */
};

/** @struct vpe_scaling_filter_coeffs
 *  @brief Filter coefficients for polyphase scaling
 *
 *  If the number of taps are set to be 0, vpe internally calculates the number of taps and filter
 * coefficients based on the scaling ratio.
 */
struct vpe_scaling_filter_coeffs {

    struct vpe_scaling_taps taps;                             /**< Number of taps for polyphase
                                                                 scaling */
    unsigned int nb_phases;                                   /**< Number of phases for polyphase
                                                                 scaling */
    uint16_t horiz_polyphase_coeffs[MAX_NB_POLYPHASE_COEFFS]; /**< Filter coefficients for
                                                                 horizontal polyphase scaling */
    uint16_t vert_polyphase_coeffs[MAX_NB_POLYPHASE_COEFFS];  /**< Filter coefficients for
                                                                 vertical polyphase scaling */
};

struct vpe_frod_param {
    union {
        uint8_t enable_frod;
    };
};

/** @struct vpe_hdr_metadata
 *  @brief HDR metadata
 */
struct vpe_hdr_metadata {
    uint16_t redX;          /**< Red point chromaticity X-value */
    uint16_t redY;          /**< Red point chromaticity Y-value */
    uint16_t greenX;        /**< Green point chromaticity X-value */
    uint16_t greenY;        /**< Green point chromaticity Y-value */
    uint16_t blueX;         /**< Blue point chromaticity X-value */
    uint16_t blueY;         /**< Blue point chromaticity Y-value */
    uint16_t whiteX;        /**< White point chromaticity X-value */
    uint16_t whiteY;        /**< White point chromaticity Y-value */

    uint32_t min_mastering; /**< Minimum luminance for HDR frame/stream in 1/10000 nits */
    uint32_t max_mastering; /**< Maximum luminance for HDR frame/stream in nits */
    uint32_t max_content;   /**< Maximum stream's content light level */
    uint32_t avg_content;   /**< Frame's average light level */
};

/** @struct vpe_reserved_param
 *  @brief Reserved parameter
 */
struct vpe_reserved_param {
    void    *param; /**< Reserved parameter */
    uint32_t size;  /**< Size of the reserved parameter */
};

/** @struct vpe_lut_mem_layout
 *  @brief vpe 3D-LUT memory layout
 */
enum vpe_lut_type {
    VPE_LUT_TYPE_CPU = 0, /**< CPU accessible 3D LUT data, 3 channel, 16 bits depth per channel */
    VPE_LUT_TYPE_GPU_1D_PACKED =
        1, /**< GPU accessible 3D LUT data, 1D packed, 4 channel, 16 bits depth per channel */
    VPE_LUT_TYPE_GPU_3D_SWIZZLE =
        2, /**< GPU accessible 3D LUT data, 3D surface 4 channel, 16 bits depth per channel */
};

// Track offset of bkgr streams relative to first stream (alpha)
enum vpe_bkgr_stream_offset {
    VPE_BKGR_STREAM_ALPHA_OFFSET        = 0, /**< background stream alpha offset */
    VPE_BKGR_STREAM_VIDEO_OFFSET        = 1, /**< background stream video offset */
    VPE_BKGR_STREAM_BACKGROUND_OFFSET   = 2, /**< background stream background offset */
    VPE_BKGR_STREAM_INTERMEDIATE_OFFSET = 3, /**< background stream intermediate offset */
};

/** @struct vpe_3dlut_compound
 * This structure encapsulates auxiliary parameters required for describing a 3D LUT (Look-Up Table)
 * operation - whether this is the 3d lut compound case, cositing info for upsampling, 3dlut output
 * CS, and 3x4 csc matrix.
 *
 * @var vpe_3dlut_compound::enabled
 *      Indicates if the LUT Compound is enabled.
 * @var vpe_3dlut_compound::upsampledChromaInput
 *      Chroma cositing mode for upsampling input.
 * @var vpe_3dlut_compound::primaries3D
 *      Color primaries for the 3D LUT output.
 *      Note that this is different from stream input/output color space.
 * @var vpe_3dlut_compound::pCscMatrix
 *      3x4 color space conversion matrix.
 */
struct vpe_3dlut_compound {
    bool                     enabled;
    enum vpe_chroma_cositing upsampled_chroma_input;
    enum vpe_color_primaries primaries_3D;
    struct vpe_color_space   out_cs_3D;

    float pCscMatrix[3][4];
};

struct vpe_dma_shaper {
    bool      enabled;
    uint64_t  data;            /**< Accessible to GPU. */
    uint64_t  config_data;     /**< Accessible to GPU. */
    uint32_t *data_cpu;        /**< Accessible to CPU. */
    uint32_t *config_data_cpu; /**< Accessible to CPU. */
    uint8_t   tmz;             /**< tmz bits for shaper */
};

struct vpe_dma_3dlut {
    uint64_t                      data;      /**< Accessible to GPU. Only for fast load */
    enum vpe_surface_pixel_format format;    /**< DMA lut data format */
    enum vpe_3dlut_mem_align      mem_align; /**< DMA lut memory alignment */
    float                         bias;      /**< DMA lut bias */
    float                         scale;     /**< DMA lut scale */
    uint8_t                       tmz;       /**< tmz bits for 3dlut */
};

struct vpe_dma_info {
    struct vpe_dma_3dlut  lut3d;  /**< DMA 3D LUT parameters */
    struct vpe_dma_shaper shaper; /**< DMA shaper parameters */
};

/** @struct vpe_tonemap_params
 *  @brief Tone mapping parameters
 */
struct vpe_tonemap_params {
    uint64_t UID;                                    /**< Unique ID for tonemap params provided by
                                                      * user. If tone mapping is not needed, set
                                                      * to 0, otherwise, each update to the
                                                      * tonemap parameter should use a new ID to
                                                      * signify a tonemap update.
                                                      */
    enum vpe_transfer_function shaper_tf;            /**< Shaper LUT transfer function */
    enum vpe_transfer_function lut_out_tf;           /**< Output transfer function */
    enum vpe_color_primaries   lut_in_gamut;         /**< Input color primary */
    enum vpe_color_primaries   lut_out_gamut;        /**< Output color primary */
    uint16_t                   input_pq_norm_factor; /**< Perceptual Quantizer normalization
                                                        factor. */
    uint16_t                   lut_dim;              /**< Size of one dimension of the 3D-LUT data*/
    uint16_t lut_container_dim; /**< Size of one dimension of the 3D-LUT container*/
    enum vpe_lut_type lut_type; /**< LUT data type. If type is GPU, use vpe_dma_info */
    uint16_t *lut_data;         /**< Accessible to CPU */
    bool      enable_3dlut;     /**< Enable/Disable 3D-LUT */
};

/** @enum vpe_keyer_mode
 *  @brief Dictates the behavior of keyer's generated alpha
 */
enum vpe_keyer_mode {
    VPE_KEYER_MODE_RANGE_00 = 0, /**< (Default) if in range -> generated alpha = 00 */
    VPE_KEYER_MODE_RANGE_FF,     /**< if in_range -> generated alpha = FF */
    VPE_KEYER_MODE_FORCE_00,     /**< ignore range setting, force generating alpha = 00 */
    VPE_KEYER_MODE_FORCE_FF,     /**< ignore range setting, force generating alpha = FF */
};

/** @struct vpe_color_keyer
 *  @brief Input Parameters for Color keyer.
 *  bounds should be programmed to 0.0 <= 1.0, with lower < upper
 *  if format does not have alpha (RGBx) when using the color keyer, alpha should be programmed to
 *  lower=0.0, upper=1.0
 */
struct vpe_color_keyer {
    bool  enable_color_key; /**< Enable Color Key. Mutually Exclusive with Luma Key */
    float lower_g_bound;    /**< Green Low Bound.  */
    float upper_g_bound;    /**< Green High Bound. */
    float lower_b_bound;    /**< Blue Low Bound.   */
    float upper_b_bound;    /**< Blue High Bound.  */
    float lower_r_bound;    /**< Red Low Bound.    */
    float upper_r_bound;    /**< Red High Bound.   */
    float lower_a_bound; /**< Alpha Low Bound. Program 0.0f if no alpha channel in input format.*/
    float upper_a_bound; /**< Alpha High Bound. Program 1.0f if no alpha channel in input format.*/
};

/** @struct vpe_histogram
*   @brief Histogram collection parameters
*   VPE can collect up to 3 separate histograms with 256 bins each.
*   Internally there are two binning modes. One for integer input surface formats and one for float input surface formats.
*
*  Integer Mode : Pixels are evenly binned with each bin having a width of(2 ^ bitdepth) - 1 / 256
*
*  Float Mode : The internal float format used for binning is fp16(1.5.10).The bin indeces are first divided
*  into two major groups. Bins 0 - 127 are for postivie pixels, bins 128 - 255 are for negative pixels.
*  Each major group is further subdivided into 32 exponent bin groups. (A mantissa of 5 gives 32 possible values)
*  Finally, the bin groups of size 4 are index by the two MSB of the mantissa to determine the bin index of the pixel.
*/
struct vpe_collection_param {
    enum vpe_hist_collection_mode   hist_types;/**< histogram collection types*/
    struct vpe_surface_info         hist_output;/**< histogram output surface*/
};

struct vpe_histogram_param {
    struct vpe_collection_param hist_collection_param[hist_max_channel];/**< histogram collection parameters: type and output surface*/
    uint32_t hist_format; /**< histogram collection data format:0 for integer, 1 and 2 for fp16 */
    uint32_t hist_dsets;  /**< number of histogram data sets: 0, 1, 2 */
};
/** @struct vpe_stream
 *  @brief Input stream/frame properties to be passed to vpelib
 */
struct vpe_stream {
    struct vpe_surface_info surface_info;                      /**< Stream plane information. */
    struct vpe_scaling_info scaling_info;                      /**< Scaling information. */
    struct vpe_blend_info   blend_info;                        /**< Alpha blending */
    struct vpe_color_adjust color_adj;                         /**< Color adjustment. Brightness,
                                                                  contrast, hue and saturation.*/
    struct vpe_tonemap_params        tm_params;                /**< Tone mapping parameters*/
    struct vpe_hdr_metadata          hdr_metadata;             /**< HDR metadata */
    struct vpe_dma_info       dma_info;                        /**< DMA / fast load params */
    struct vpe_3dlut_compound lut_compound;                    /**< 3D LUT compound params */
    struct vpe_scaling_filter_coeffs polyphase_scaling_coeffs; /**< Filter coefficients for
                                                                  polyphase scaling. */
    enum vpe_rotation_angle rotation;                          /**< Rotation angle of the
                                                                  stream/frame */
    bool horizontal_mirror;                                    /**< Set if the stream is flipped
                                                                  horizontally */
    bool vertical_mirror;                                      /**< Set if the stream is flipped
                                                                  vertically */
    bool use_external_scaling_coeffs;                          /**< Use provided polyphase scaling
                                                                * filter coefficients.
                                                                * See @ref vpe_scaling_filter_coeffs
                                                                */
    bool enable_luma_key;                                      /**< Enable luma keying. Only
                                                                * works if vpe version supports
                                                                * luma keying.
                                                                */
    float lower_luma_bound;                                    /**< Lowest range of the luma */
    float upper_luma_bound;                                    /**< Highest range of the luma */
    struct vpe_color_keyer color_keyer; /**< Enable Luma Keying & Set Parameters. */
    enum vpe_keyer_mode    keyer_mode;  /**< Set Keyer Behavior.
                                         * Used for both Luma & Color Keying.
                                         */
    struct vpe_surface_info intermediate_surface; /**< Intermediate stream for two pass operations
                                                   * this surface is allocated by caller.
                                                   * Set addr to 0 if unused */
    struct vpe_histogram_param       hist_params; /**< Parameters related to the histogram collection*/
    struct vpe_reserved_param reserved_param;     /**< Reserved parameter for input surface */

    /** @brief stream feature flags
     */
    struct {
        uint32_t hdr_metadata      : 1; /**< Set if hdr meta data is available */
        uint32_t geometric_scaling : 1; /**< Enables geometric scaling.
                                         * Support 1 input stream only.
                                         * If set, gamut/gamma remapping will be disabled,
                                         * as well as blending.
                                         * Destination rect must equal to target rect.
                                         */
        /**
         * Flags for Background Replacement (BKGR) and Alpha Combine feature
         *
         * BKGR requires 3 or 4 inputs:
         * For one pass:
         * AlphaStream, VideoStream, BackgroundStream
         * For two pass:
         * AlphaStream, VideoStream, BackgroundStream, Intermediate Surface
         *
         * For two-pass BKGR, an intermediate surface is required to store results of first pass
         *
         * stream[i] is the alpha stream passed as NV12.
         * Format must be VPE_SURFACE_PIXEL_FORMAT_VIDEO_ALPHA_THRU_LUMA
         *     is_alpha_combine = 1; is_alpha_plane = 1; is_background_plane = 0;
         *
         * stream[i+1] is the video stream that will have its background removed (and replaced if
         * BKGR) is_alpha_combine = 1; is_alpha_plane = 0; is_background_plane = 0;
         *
         * If only doing alpha combine, only first 2 streams are required. For BKGR:
         * stream[i+2] is the background stream
         *     is_alpha_combine = 0; is_alpha_plane = 0; is_background_plane = 1;
         *
         * If two pass: stream[i+3] is the intermediate surface. Format == FP16
         *     if src stream downscaling: Size == dst rect (downscaled src rect)
         *     else:                      Size == src rect
         * for one pass we don't need this stream
         *
         * Ordering also tracked in enum vpe_bkgr_stream_offset
         */
        uint32_t is_background_plane : 1; /**< is this stream the new background */
        uint32_t is_alpha_combine    : 1; /**< set if part of the alpha combine operation */
        uint32_t is_alpha_plane      : 1; /**< is this the alpha through luma plane */
        uint32_t reserved            : 27; /**< reserved */
    } flags; /**< Data flags */
};

/** @enum predication_polarity
 *  @brief Predication polarity
 */
enum predication_polarity {
    PREDICATION_OP_EQUAL_ZERO = 0, /**< Enables predication if all 64-bits are zero. */
    PREDICATION_OP_NOT_EQUAL_ZERO =
        1, /**< Enables predication if at least one of the 64-bits are not zero.*/
};

/** @struct vpe_predication_info
 *  @brief Predication info
 */
struct vpe_predication_info {
    bool                      enable;   /**< Enable predication */
    uint64_t                  gpu_va;   /**< GPU start address of the buffer */
    enum predication_polarity polarity; /**< Predication polarity */
};

/** @struct vpe_build_param
 *  @brief Build parametrs for vpelib. Must get populated before vpe_check_support() call.
 */
struct vpe_build_param {
    uint32_t                num_streams;          /**< Number of source streams */
    struct vpe_stream      *streams;              /**< List of input streams */
    struct vpe_surface_info dst_surface;          /**< Destination/Output surface */
    struct vpe_rect         target_rect;          /**< rectangle in target surface to be blt'd.
                                                       Ranges out of target_rect won't be touched */
    struct vpe_color    bg_color;                 /**< Background Color */
    enum vpe_alpha_mode alpha_mode;               /**< Alpha Mode. Output alpha in the output
                                                       surface */
    struct vpe_hdr_metadata   hdr_metadata;       /**< HDR Metadata */
    struct vpe_reserved_param dst_reserved_param; /**< Reserved parameter for destination surface */
    struct vpe_predication_info predication_info; /**< Predication info */

    /** Data flags */
    struct {
        uint32_t hdr_metadata : 1;  /**< Set if hdr meta data is available */
        uint32_t reserved     : 31; /**< reserved */
    } flags;

    uint16_t num_instances;      /**< Number of instances for the collaboration mode */
    bool     collaboration_mode; /**< Collaboration mode. If set, multiple instances of VPE being
                                    used. */
    bool                    enable_frod;
    struct vpe_surface_info frod_surface[VPE_FROD_MAX_STAGE]; /**< FROD outputs */
    struct vpe_frod_param   frod_param;                       /**< FROD parameters */
};

/** @struct vpe_bufs_req
 *  @brief Command buffer and Embedded buffer required sizes reported through vpe_check_support()
 *
 * Once the operation is supported,
 * it returns the required memory for storing
 * 1. command buffer
 * 2. embedded buffer
 *    - Pointed by the command buffer content.
 *    - Shall be free'ed together with command buffer once
 *      command is finished.
 */
struct vpe_bufs_req {
    uint64_t cmd_buf_size; /**< total command buffer size for all vpe commands */
    uint64_t emb_buf_size; /**< total size for storing all embedded data */
};

/** @struct vpe_buf
 *  @brief Buffer information
 */
struct vpe_buf {
    uint64_t gpu_va; /**< GPU start address of the buffer */
    uint64_t cpu_va; /**< CPU start address of the buffer */
    uint64_t size;   /**< Size of the buffer */
    bool     tmz;    /**< allocated from tmz */
};

/** @struct vpe_build_bufs
 *  @brief Command buffer and Embedded buffer
 */
struct vpe_build_bufs {
    struct vpe_buf cmd_buf; /**< Command buffer. gpu_va is optional */
    struct vpe_buf emb_buf; /**< Embedded buffer */
};

/** @struct vpe_check_support_funcs
 *  @brief  check support functions
 */
struct vpe_check_support_funcs {
    /** @brief
     * Check if the input surface format is supported
     *
     * @param[in] format  input format
     * @return true if supported
     */
    bool (*check_input_format)(enum vpe_surface_pixel_format format);

    /** @brief
     * Check if the output surface format is supported
     *
     * @param[in] format  output format
     * @return true if supported
     */
    bool (*check_output_format)(enum vpe_surface_pixel_format format);

    /** @brief
     * Check if the input color space is supported
     *
     * @param[in] format  input format
     * @param[in] vcs     input color space
     * @return true if supported
     */
    bool (*check_input_color_space)(
        enum vpe_surface_pixel_format format, const struct vpe_color_space *vcs);

    /** @brief
     * Check if the output color space is supported
     *
     * @param[in] format  output format
     * @param[in] vcs     output color space
     * @return true if supported
     */
    bool (*check_output_color_space)(
        enum vpe_surface_pixel_format format, const struct vpe_color_space *vcs);

    /** @brief
     * Get DCC support and setting according to the format,
     * scan direction and swizzle mode for output.
     *
     * @param[in]      params        surface properties
     * @param[in/out]  cap           dcc capable result and related settings
     * @return true if supported
     */
    bool (*get_dcc_compression_output_cap)(
        const struct vpe_dcc_surface_param *params, struct vpe_surface_dcc_cap *cap);

    /** @brief
     * Get DCC support and setting according to the format,
     * scan direction and swizzle mode for input.
     *
     * @param[in]      params        surface properties
     * @param[in/out]  cap           dcc capable result and related settings
     * @return true if supported
     */
    bool (*get_dcc_compression_input_cap)(
        const struct vpe_dcc_surface_param *params, struct vpe_surface_dcc_cap *cap);
};

/** @struct vpe
 *  @brief VPE instance created through vpelib entry function vpe_create()
 */
struct vpe {
    uint32_t                       version;     /**< API version */
    enum vpe_ip_level              level;       /**< HW IP level */
    struct vpe_caps               *caps;        /**< general static chip caps */
    struct vpe_check_support_funcs check_funcs; /**< vpe check format support funcs */
};

/** @struct vpe_engine
 *  @brief VPE engine information
 */
struct vpe_engine {
    uint32_t                       api_version; /**< API version */
    enum vpe_ip_level              ip_level;    /**< HW IP level */
    const struct vpe_caps         *caps;        /**< general static chip caps */
    struct vpe_check_support_funcs check_funcs; /**< vpe check format support funcs */
};

#ifdef __cplusplus
}
#endif
