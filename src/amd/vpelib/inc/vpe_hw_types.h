/* Copyright 2022 Advanced Micro Devices, Inc.
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
 * @file         vpe_hw_types.h
 * @brief        This is the file containing the API hardware structures for the VPE library.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 * Note: do *not* add any types which are *not* used for HW programming.
 * this will ensure separation of Logic layer from HW layer
 ***********************************************************************/

/** @union large_integer
 *  @brief 64 bits integers, either with one 64 bit integer or two 32 bits. Mainly used to store
 *         memory addresses.
 */
union large_integer {
    /**
     * @brief struct of signed integer
     */
    struct {
        uint32_t low_part;  /**< Bits [0:31] of the integer */
        int32_t  high_part; /**< Bits [32:63] of the integer */
    };

    /**
     * @brief struct of unsigned integer
     */
    struct {
        uint32_t low_part;  /**< Bits [0:31] of the integer */
        int32_t  high_part; /**< Bits [32:63] of the integer */
    } u; /**< Structure of one unsigend integer for [0:31] bits of the integer and one signed
          * integer for [32:63].
          */

    int64_t quad_part; /**< One 64 bits integer. */
};

/** @def PHYSICAL_ADDRESS_LOC
 *
 *  @brief Large integer to store memory address
 */
#define PHYSICAL_ADDRESS_LOC union large_integer

/** @enum vpe_plane_addr_type
 *  @brief Plane address types
 */
enum vpe_plane_addr_type {
    VPE_PLN_ADDR_TYPE_GRAPHICS = 0,      /**< For RGB planes */
    VPE_PLN_ADDR_TYPE_VIDEO_PROGRESSIVE, /**< For YCbCr planes */
};

/** @struct vpe_plane_address
 *
 *  @brief The width and height of the surface
 */
struct vpe_plane_address {
    enum vpe_plane_addr_type type; /**< Type of the plane address */
    uint8_t tmz_surface;           /**< uint8_t to determine if the surface is allocated from tmz */
    /** @union
     *  @brief Union of plane address types
     */
    union {
        /** @brief Only used for RGB planes. Struct of two \ref PHYSICAL_ADDRESS_LOC to store
         * address and meta address, and one \ref large_integer to store dcc constant color.
         */
        struct {
            PHYSICAL_ADDRESS_LOC addr;            /**< Address of the plane */
            PHYSICAL_ADDRESS_LOC meta_addr;       /**< Meta address of the plane */
            union large_integer  dcc_const_color; /**< DCC constant color of the plane */
        } grph;

        /** @brief Only used for YUV planes. Struct of four \ref PHYSICAL_ADDRESS_LOC to store
         *  address and meta addresses of both luma and chroma planes, and two \ref large_integer
         *  to store dcc constant color for each plane. For packed YUV formats, the chroma plane
         *  addresses should be blank.
         */
        struct {
            PHYSICAL_ADDRESS_LOC luma_addr;            /**< Address of the luma plane */
            PHYSICAL_ADDRESS_LOC luma_meta_addr;       /**< Meta address of the luma plane */
            union large_integer  luma_dcc_const_color; /**< DCC constant color of the luma plane */

            PHYSICAL_ADDRESS_LOC chroma_addr;          /**< Address of the chroma plane */
            PHYSICAL_ADDRESS_LOC chroma_meta_addr;     /**< Meta address of the chroma plane */
            union large_integer
                chroma_dcc_const_color; /**< DCC constant color of the chroma plane */
        } video_progressive;

    };
};

/** @enum vpe_rotation_angle
 *  @brief Plane clockwise rotation angle
 */
enum vpe_rotation_angle {
    VPE_ROTATION_ANGLE_0 = 0, /**< No rotation */
    VPE_ROTATION_ANGLE_90,    /**< 90 degrees clockwise rotation */
    VPE_ROTATION_ANGLE_180,   /**< 180 degrees clockwise rotation */
    VPE_ROTATION_ANGLE_270,   /**< 270 degrees clockwise rotation */
    VPE_ROTATION_ANGLE_COUNT
};

/** @enum vpe_mirror
 *  @brief Mirroring type
 */
enum vpe_mirror {
    VPE_MIRROR_NONE,       /**< No mirroring */
    VPE_MIRROR_HORIZONTAL, /**< Horizontal mirroring */
    VPE_MIRROR_VERTICAL    /**< Vertical mirroring */
};

/** @enum vpe_scan_direction
 *  @brief Plane memory scan pattern
 */
enum vpe_scan_direction {
    VPE_SCAN_PATTERN_0_DEGREE =
        0, /**< Left to Right, Top to Bottom. 0 Degree Rotation and no Mirroring */
    VPE_SCAN_PATTERN_90_DEGREE =
        1, /**< Bottom to Top, Left to Right. 90 Degree Rotation and no Mirroring */
    VPE_SCAN_PATTERN_180_DEGREE =
        2, /**< Right to Left, Bottom to Top. 180 Degree Rotation and no Mirroring */
    VPE_SCAN_PATTERN_270_DEGREE =
        3, /**< Top to Bottom, Right to Left. 270 Degree Rotation and no Mirroring */
};

/** @struct vpe_size
 *  @brief The width and height of the surface
 */
struct vpe_size {
    uint32_t width;  /**< Width of the surface in pixels */
    uint32_t height; /**< Height of the surface in pixels */
};

/** @struct vpe_rect
 *  @brief A rectangle used in vpe is specified by the position of the left most top corner of the
 *         rectangle and the width and height of the rectangle.
 */
struct vpe_rect {
    int32_t  x;      /**< The x coordinate of the left most top corner */
    int32_t  y;      /**< The y coordinate of the left most top corner */
    uint32_t width;  /**< Width of the surface in pixels */
    uint32_t height; /**< Height of the rectangle in pixels */
};

/** @struct vpe_plane_size
 *  @brief Size and pitch alignment for vpe surface plane(s)
 */
struct vpe_plane_size {
    struct vpe_rect surface_size;    /**< Plane rectangle */
    struct vpe_rect chroma_size;     /**< Chroma plane rectangle for semi-planar YUV formats */
    uint32_t        surface_pitch;   /**< Horizintal pitch alignment of the plane in pixels */
    uint32_t        chroma_pitch;    /**< Horizintal pitch alignment of the chroma plane for
                                        semi-planar YUV formats in pixels */
    uint32_t surface_aligned_height; /**< Vertical alignment of the plane in pixels */
    uint32_t chrome_aligned_height;  /**< Vertical alignment of the chroma plane for semi-planar
                                        YUV formats in pixels */
};

/** @struct vpe_plane_dcc_param
 *  @brief dcc params
 */
struct vpe_plane_dcc_param {
    bool enable;                     /**< Enable DCC */

    union {
        /** @brief DCC params for source, required for display DCC only */
        struct {
            uint32_t meta_pitch;           /**< DCC meta surface pitch in bytes */
            bool     independent_64b_blks; /**< DCC independent 64 byte blocks */
            uint8_t  dcc_ind_blk;          /**< DCC independent block size */

            uint32_t meta_pitch_c;         /**< DCC meta surface pitch for chroma plane in bytes */
            bool     independent_64b_blks_c; /**< DCC independent 64 byte blocks for chroma plane */
            uint8_t  dcc_ind_blk_c;          /**< DCC independent block size for chroma plane */
        } src;

    };
};

/** @enum vpe_surface_pixel_format
 *  @brief Surface formats
 *
 * The order of components are MSB to LSB. For example, for VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB1555,
 * the most significant bit is reserved for alpha and the 5 least significant bits are reserved for
 * the blue channel, i.e.
 *
 * <pre>
 * MSB _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ LSB
 *     A R R R R R G G G G G B B B B B
 * </pre>
 */
enum vpe_surface_pixel_format {
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BEGIN = 0,
    /*16 bpp*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB1555,
    /*16 bpp*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB565,
    /*32 bpp*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB8888,
    /*32 bpp swaped*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR8888,
    /*32 bpp alpha rotated*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA8888,
    /*32 bpp swaped & alpha rotated*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA8888,

    VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010,
    /*swaped*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010,
    /*alpha rotated*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA1010102,
    /*swaped & alpha rotated*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA1010102,

    /*64 bpp */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616,
    /*float*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F,
    /*swaped & float*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F,
    /*alpha rotated*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616F,
    /*swaped & alpha rotated*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616F,

    VPE_SURFACE_PIXEL_FORMAT_GRPH_XRGB8888,
    /*swaped*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_XBGR8888,
    /*rotated*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBX8888,
    /*swaped & rotated*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRX8888,
    /*grow graphics here if necessary */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FIX,
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FIX,
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FLOAT,
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FLOAT,
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBE,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_BEGIN,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr = VPE_SURFACE_PIXEL_FORMAT_VIDEO_BEGIN,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_16bpc_YCrCb,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_16bpc_YCbCr,
    VPE_SURFACE_PIXEL_FORMAT_SUBSAMPLE_END = VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_16bpc_YCbCr,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb2101010,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA1010102,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_YCrCbA8888,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb8888,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA8888,
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCbCr8888, //seems to be dummy, not part of surface pixel register values
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_END = VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCbCr8888,
    VPE_SURFACE_PIXEL_FORMAT_INVALID

    /*grow 444 video here if necessary */
};

/** @enum vpe_swizzle_mode_values
 *  @brief Surface swizzle modes
 */
enum vpe_swizzle_mode_values {
    VPE_SW_LINEAR   = 0,  /**< Linear swizzle mode */
    VPE_SW_256B_S   = 1,  /**< 256B_S swizzle mode */
    VPE_SW_256B_D   = 2,  /**< 256B_D swizzle mode */
    VPE_SW_256B_R   = 3,  /**< 256B_R swizzle mode */
    VPE_SW_4KB_Z    = 4,  /**< 4KB_Z swizzle mode */
    VPE_SW_4KB_S    = 5,  /**< 4KB_S swizzle mode */
    VPE_SW_4KB_D    = 6,  /**< 4KB_D swizzle mode */
    VPE_SW_4KB_R    = 7,  /**< 4KB_R swizzle mode */
    VPE_SW_64KB_Z   = 8,  /**< 64KB_Z swizzle mode */
    VPE_SW_64KB_S   = 9,  /**< 64KB_S swizzle mode */
    VPE_SW_64KB_D   = 10, /**< 64KB_D swizzle mode */
    VPE_SW_64KB_R   = 11, /**< 64KB_R swizzle mode */
    VPE_SW_VAR_Z    = 12, /**< VAR_Z swizzle mode */
    VPE_SW_VAR_S    = 13, /**< VAR_S swizzle mode */
    VPE_SW_VAR_D    = 14, /**< VAR_D swizzle mode */
    VPE_SW_VAR_R    = 15, /**< VAR_R swizzle mode */
    VPE_SW_64KB_Z_T = 16, /**< 64KB_Z_T swizzle mode */
    VPE_SW_64KB_S_T = 17, /**< 64KB_S_T swizzle mode */
    VPE_SW_64KB_D_T = 18, /**< 64KB_D_T swizzle mode */
    VPE_SW_64KB_R_T = 19, /**< 64KB_R_T swizzle mode */
    VPE_SW_4KB_Z_X  = 20, /**< 4KB_Z_X swizzle mode */
    VPE_SW_4KB_S_X  = 21, /**< 4KB_S_X swizzle mode */
    VPE_SW_4KB_D_X  = 22, /**< 4KB_D_X swizzle mode */
    VPE_SW_4KB_R_X  = 23, /**< 4KB_R_X swizzle mode */
    VPE_SW_64KB_Z_X = 24, /**< 64KB_Z_X swizzle mode */
    VPE_SW_64KB_S_X = 25, /**< 64KB_S_X swizzle mode */
    VPE_SW_64KB_D_X = 26, /**< 64KB_D_X swizzle mode */
    VPE_SW_64KB_R_X = 27, /**< 64KB_R_X swizzle mode */
    VPE_SW_VAR_Z_X  = 28, /**< SW VAR Z X */
    VPE_SW_VAR_S_X  = 29, /**< SW VAR S X */
    VPE_SW_VAR_D_X  = 30, /**< SW VAR D X */
    VPE_SW_VAR_R_X  = 31, /**< SW VAR R X */
    VPE_SW_MAX      = 32,
    VPE_SW_UNKNOWN  = VPE_SW_MAX
};

/** @struct vpe_scaling_taps
 *  @brief Number of taps used for scaling
 *
 * If the number of taps are set to 0, VPElib internally chooses the best tap based on the scaling
 * ratio.
 */
struct vpe_scaling_taps {
    uint32_t v_taps;   /**< Number of vertical taps */
    uint32_t h_taps;   /**< Number of horizontal taps */
    uint32_t v_taps_c; /**< Number of vertical taps for chroma plane */
    uint32_t h_taps_c; /**< Number of horizontal taps for chroma plane */
};

#ifdef __cplusplus
}
#endif
