/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_SCREEN_H_
#define FD6_SCREEN_H_

#include "freedreno_screen.h"
#include "freedreno_common.h"

EXTERNC void fd6_screen_init(struct pipe_screen *pscreen);

#ifdef __cplusplus
template <chip_range_support>
struct FD6_TESS;

template <chip CHIP>
struct FD6_TESS<chip_range(CHIP <= A7XX)> {
   /* the blob seems to always use 8K factor and 128K param sizes, copy them */
   static const size_t FACTOR_SIZE = 8 * 1024;
   static const size_t PARAM_SIZE = 128 * 1024;
   static const size_t BO_SIZE = FACTOR_SIZE + PARAM_SIZE;
};

template <chip CHIP>
struct FD6_TESS<chip_range(CHIP >= A8XX)> {
   /* for gen8, buffers are sized for two draws: */
   static const size_t FACTOR_SIZE = 0x4040;
   static const size_t PARAM_SIZE = 0x40000;
   static const size_t BO_SIZE = FACTOR_SIZE + PARAM_SIZE;
};
#endif

#endif /* FD6_SCREEN_H_ */
