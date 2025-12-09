/*
 * SPDX-FileCopyrightText: Copyright 2020-2022 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>

#ifndef MLW_ENCODE_H
#define MLW_ENCODE_H

#ifdef _MSC_VER
  #define MLW_ENCODE_EXPORTED __declspec(dllexport)
#else
  #define MLW_ENCODE_EXPORTED __attribute__((visibility("default")))
#endif

#if __cplusplus
extern "C"
{
#endif

MLW_ENCODE_EXPORTED
int mlw_encode(int16_t *inbuf, int inbuf_size, uint8_t **outbuf, int verbose);

MLW_ENCODE_EXPORTED
void mlw_free_outbuf(uint8_t *outbuf);

MLW_ENCODE_EXPORTED
int mlw_reorder_encode(
    int ifm_ublock_depth,
    int ofm_ublock_depth,
    int ofm_depth,
    int kernel_height,
    int kernel_width,
    int ifm_depth,
    int* brick_strides,
    int16_t* inbuf,
    int ofm_block_depth,
    int is_depthwise,
    int is_partkernel,
    int ifm_bitdepth,
    int decomp_h,
    int decomp_w,
    uint8_t **outbuf,
    int64_t* padded_length,
    int verbose);

#if __cplusplus
}
#endif

#endif
