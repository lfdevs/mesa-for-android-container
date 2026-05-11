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
    VPE_PLN_ADDR_TYPE_PLANAR,            /**< For RGB 3-planar case */
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

        /** @brief Only used for RGB 3-planar case. Each plane is a struct of two \ref
         *  PHYSICAL_ADDRESS_LOC to store address and meta address, and one \ref large_integer to
         *  store dcc constant color.
         */
        struct {
            PHYSICAL_ADDRESS_LOC y_g_addr;             /**< Address of the Y/G plane */
            PHYSICAL_ADDRESS_LOC y_g_meta_addr;        /**< Meta address of the Y/G plane */
            union large_integer  y_g_dcc_const_color;  /**< DCC constant color of the Y/G plane */

            PHYSICAL_ADDRESS_LOC cb_b_addr;            /**< Address of the Cb/B plane */
            PHYSICAL_ADDRESS_LOC cb_b_meta_addr;       /**< Meta address of the Cb/B plane */
            union large_integer  cb_b_dcc_const_color; /**< DCC constant color of the Cb/B plane */

            PHYSICAL_ADDRESS_LOC cr_r_addr;            /**< Address of the Cr/R plane */
            PHYSICAL_ADDRESS_LOC cr_r_meta_addr;       /**< Meta address of the Cr/R plane */
            union large_integer  cr_r_dcc_const_color; /**< DCC constant color of the Cr/R plane */
        } planar;
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
    VPE_SCAN_PATTERN_0_DEGREE_H_MIRROR = 4, /**< Right to Left, Top to Bottom. 0 Degree Rotation and
                                               HMirror or 180 Degree Rotation and VMirror */
    VPE_SCAN_PATTERN_90_DEGREE_V_MIRROR = 5,  /**< Bottom to Top, Right to Left. 270 Degree Rotation
                                                 and HMirror or 90 Degree Rotation and VMirror */
    VPE_SCAN_PATTERN_180_DEGREE_H_MIRROR = 6, /**< Left to Right, Bottom to Top. 180 Degree Rotation
                                                 and HMirror or 0 Degree Rotation and VMirror */
    VPE_SCAN_PATTERN_270_DEGREE_V_MIRROR = 7, /**< Top to Bottom, Left to Right. 90 Degree Rotation
                                                 and HMirror or 270 Degree Rotation and VMirror */
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
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB1555,        /**< RGB 16 bpp A1 R5 G5 B5 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB565,          /**< RGB 16 bpp no alpha R5 G6 B5 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB8888,        /**< RGB 32 bpp A8 R8 G8 B8 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR8888,        /**< Swapped RGB 32 bpp A8 B8 G8 R8 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA8888,        /**< Alpha rotated RGB 32 bpp R8 G8 B8 A8 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA8888,        /**< Swapped and alpha rotated RGB 32 bpp
                                                      B8 G8 R8 A8 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010,     /**< RGB 32 bpp A2 R10 G10 B10 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010,     /**< Swapped RGB 32 bpp A2 B10 G10 R10 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA1010102,     /**< Alpha rotated RGB 32 bpp R10 G10 B10 A2*/
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA1010102,     /**< Swapped and alpha rotated RGB 32 bpp
                                                      A2 B10 G10 R10 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616,    /**< RGB 64 bpp A16 R16 G16 B16 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616,    /**< RGB 64 bpp A16 B16 G16 R16 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616,    /**< RGB 64 bpp R16 G16 B16 A16 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616,    /**< RGB 64 bpp B16 G16 R16 A16 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F,   /**< Floating point RGB 64 bpp A16 R16 G16 B16 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F,   /**< Floating point swapped RGB 64 bpp
                                                     A16 B16 G16 R16 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616F,   /**< Floating point alpha rotated RGB 64 bpp
                                                     R16 G16 R16 A16 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616F,   /**< Floating point swapped and alpha rotated
                                                     RGB 64 bpp B16 G16 R16 A16 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_XRGB8888,        /**< Opaque RGB 32 bpp X8 (ignored) R8 G8 B8 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_XBGR8888,        /**< Opaque swapped RGB 32 bpp X8 B8 G8 R8 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBX8888,        /**< Opaque rotated RGB 32 bpp R8 G8 B8 X8 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRX8888,        /**< Opaque rotated and swapped RGB 32 bpp
                                                      B8 G8 R8 X8 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FIX,   /**< RGB 32 bpp UNORM R11 G11 B10 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FIX,   /**< Swapped RGB 32 bpp UNORM R11 G11 B10 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FLOAT, /**< Floating point RGB 32 bpp R11 G11 B10 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FLOAT, /**< Swapped Floating point RGB 32 bpp
                                                      R11 G11 B10 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBE,            /**< Shared Exponent RGB 32 bpp R9 G9 B9 E5 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_UNORM, /**< RGB 64 bpp UNORM A16 R16 G16 B16 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_UNORM, /**< RGB 64 bpp UNORM R16 G16 B16 A16 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_SNORM, /**< RGB 64 bpp SNORM A16 R16 G16 B16 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_SNORM, /**< RGB 64 bpp SNORM R16 G16 B16 A16 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_R8,                 /**< Monochrome 8 bpp R8 */
    VPE_SURFACE_PIXEL_FORMAT_GRPH_R16,                /**< Monochrome 16 bpp R16 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_BEGIN,          /**< Start of YCbCr formats. Used internally.*/
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr =
        VPE_SURFACE_PIXEL_FORMAT_VIDEO_BEGIN,      /**< Planar YUV 4:2:0 8 bpc Y Cb Cr, AKA NV12*/
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb,      /**< Semi-Planar YUV 4:2:0 8 bpc Y Cr Cb, AKA
                                                      NV21 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr,  /**< Semi-Planar YUV 4:2:0 10 bpc Y Cb Cr, AKA
                                                        P010 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb,  /**< Semi-Planar YUV 4:2:0 10 bpc Y Cr Cb */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCrCb,  /**< Semi-Planar YUV 4:2:0 12 bpc Y Cr Cb */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCbCr,  /**< Semi-Planar YUV 4:2:0 12 bpc Y Cb Cr, AKA
                                                        P016 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrYCb,       /**< Packed YUV 4:2:2 8 bpc Y Cr Y Cb */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbYCr,       /**< Packed YUV 4:2:2 8 bpc Y Cb Y Cr */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CrYCbY,       /**< Packed YUV 4:2:2 8 bpc Cr Y Cb Y */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CbYCrY,       /**< Packed YUV 4:2:2 8 bpc Cb Y Cr Y */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrYCb, /**< Packed YUV 4:2:2 10 bpc Y Cr Y Cb */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbYCr, /**< Packed YUV 4:2:2 10 bpc Y Cb Y Cr */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CrYCbY, /**< Packed YUV 4:2:2 10 bpc Cr Y Cb Y */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CbYCrY, /**< Packed YUV 4:2:2 10 bpc Cb Y Cr Y */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrYCb, /**< Packed YUV 4:2:2 12 bpc Y Cr Y Cb */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbYCr, /**< Packed YUV 4:2:2 12 bpc Y Cb Y Cr */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CrYCbY, /**< Packed YUV 4:2:2 12 bpc Cr Y Cb Y */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CbYCrY, /**< Packed YUV 4:2:2 12 bpc Cb Y Cr Y */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrCb,        /**< Semi-Planar YUV 4:2:2 8 bpc Y Cr Cb */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbCr,        /**< Semi-Planar YUV 4:2:2 8 bpc Y Cb Cr */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrCb,  /**< Semi-Planar YUV 4:2:2 10 bpc Y Cr Cb */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbCr,  /**< Semi-Planar YUV 4:2:2 10 bpc Y Cb Cr */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrCb,  /**< Semi-Planar YUV 4:2:2 12 bpc Y Cr Cb */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbCr,  /**< Semi-Planar YUV 4:2:2 12 bpc Y Cb Cr */
    VPE_SURFACE_PIXEL_FORMAT_SUBSAMPLE_END =
        VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbCr, /**< End of chroma sub-sampled formats.
                                                           Used internally */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb12121212,  /**< Y416 64 bpp A12 Cr12 Y12 Cb12 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA12121212,  /**< A-rotated Y416 64 bpp Cr12 Y12 Cb12 A12 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_ALPHA_THRU_LUMA, /**< Alpha plane 8bpc passed as YUV 4:2:0 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrCbYA8888,      /**< AYUV 32 bpp 8 bpc Cb8 Cr8 Y8 A8*/
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb2101010, /**< Y410 32 bpp A2 Cr10 Y10 Cb10 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA1010102, /**< A-rotated Y410 32 bpp Cr10 Y10 Cb10 A2 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888,    /**< AYUV 32 bpp 8 bpc A8 Y8 Cr8 Cb8 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_YCrCbA8888,    /**< A-rotated AYUV 32 bpp 8 bpc Y8 Cr8 Cb8 A8 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb8888,    /**< Cr first AYUV 32 bpp 8 bpc A8 Cr8 Y8 Cb8 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA8888,    /**< Alpha rotated Cr first AYUV 32 bpp 8 bpc
                                                     Cr8 Y8 Cb8 A8 */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCbCr8888,    /**< AYUV 32 bpp 8 bpc A8 Y8 Cb8 Cbr */
    VPE_SURFACE_PIXEL_FORMAT_VIDEO_END =
        VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCbCr8888, /**< End of YCbCr formats. Used internally. */

    VPE_SURFACE_PIXEL_FORMAT_PLANAR_BEGIN,               /**< Full 3 Plane Formats */
    VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_RGB =           /**< Planar RGB 8bpc */
        VPE_SURFACE_PIXEL_FORMAT_PLANAR_BEGIN,
    VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_YCbCr,          /**< Planar YCbCr 8bpc */
    VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB,           /**< Planar RGB 16bpc */
    VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_YCbCr,         /**< Planar YCbCr 16bpc */
    VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB_FLOAT,     /**< Planar RGB FP16 */
    VPE_SURFACE_PIXEL_FORMAT_PLANAR_END =
        VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB_FLOAT, /**< End of PLANAR formats. Used
                                                            internally. */
    VPE_SURFACE_PIXEL_FORMAT_INVALID               /**< Used for the formats which are not among
                                                      the recognized formats. */
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

/** @enum vpe_3dlut_mem_align
 *  @brief 3DLUT dma buffer alignment
 */
enum vpe_3dlut_mem_align {
    VPE_3DLUT_ALIGNMENT_128 = 0, /**< 32 bytes alignment */
    VPE_3DLUT_ALIGNMENT_256 = 1, /**< 64 bytes alignment */
};
#ifdef __cplusplus
}
#endif
