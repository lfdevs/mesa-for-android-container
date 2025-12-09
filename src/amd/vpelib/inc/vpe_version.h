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
 * @file         vpe_version.h
 * @brief        This is the file containing the information and definitions for VPE versioning.
 */
/** @cond */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define VPELIB_API_VERSION_MAJOR 1                /**< VPE API major version number */
#define VPELIB_API_VERSION_MINOR 0                /**< VPE API minor version number */

#define VPELIB_API_VERSION_MAJOR_SHIFT 16         /**< VPE API major version mumber shift */
#define VPELIB_API_VERSION_MINOR_SHIFT 0          /**< VPE API minor version mumber shift */
#define VPELIB_API_VERSION_MAJOR_MASK  0xFFFF0000 /**< VPE API major version mumber mask */
#define VPELIB_API_VERSION_MINOR_MASK  0x0000FFFF /**< VPE API minor version mumber mask */

/** @macro VPELIB_GET_API_MAJOR
 *  @brief GET VPE API major version
 */
#define VPELIB_GET_API_MAJOR(version)                                                              \
    ((version & VPELIB_API_VERSION_MAJOR_MASK) >> VPELIB_API_VERSION_MAJOR_SHIFT)

/** @macro VPELIB_GET_API_MINOR
 *  @brief GET VPE API minor version
 */
#define VPELIB_GET_API_MINOR(version)                                                              \
    ((version & VPELIB_API_VERSION_MINOR_MASK) >> VPELIB_API_VERSION_MINOR_SHIFT)

/** @macro VPE_VERSION
 *  @brief compose VPE version number
 */
#define VPE_VERSION(major, minor, rev_id)     (((major) << 16) | ((minor) << 8) | (rev_id))
/** @macro VPE_VERSION_MAJ
 *  @brief GET VPE major version
 */
#define VPE_VERSION_MAJ(ver)                  ((ver) >> 16)
/** @macro VPE_VERSION_MIN
 *  @brief GET VPE minor version
 */
#define VPE_VERSION_MIN(ver)                  (((ver) >> 8) & 0xFF)
/** @macro VPE_VERSION_REV
 *  @brief GET VPE revision version
 */
#define VPE_VERSION_REV(ver)                  ((ver) & 0xFF)
/** @macro VPE_VERSION_6_1_0
 *  @brief check if VPE version is 6.1.0
 */
#define VPE_VERSION_6_1_0(ver)                ((ver) == VPE_VERSION(6, 1, 0) || (ver) == VPE_VERSION(6, 1, 3))
/** @macro VPE_VERSION_6_1_1
 *  @brief check if VPE version is 6.1.1
 */
#define VPE_VERSION_6_1_1(ver)                (((ver) == VPE_VERSION(6, 1, 1)) || ((ver) == VPE_VERSION(6, 1, 2)))

#ifdef __cplusplus
}
#endif
/** @endcond */
