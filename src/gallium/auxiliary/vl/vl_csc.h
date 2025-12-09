/**************************************************************************
 *
 * Copyright 2009 Younes Manton.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef vl_csc_h
#define vl_csc_h

#include "util/format/u_format.h"
#include "pipe/p_video_enums.h"

typedef float vl_csc_matrix[3][4];

void vl_csc_get_rgbyuv_matrix(enum pipe_video_vpp_matrix_coefficients coefficients,
                              enum pipe_format in_format, enum pipe_format out_format,
                              enum pipe_video_vpp_color_range in_color_range,
                              enum pipe_video_vpp_color_range out_color_range,
                              vl_csc_matrix *matrix);

void vl_csc_get_primaries_matrix(enum pipe_video_vpp_color_primaries in_color_primaries,
                                 enum pipe_video_vpp_color_primaries out_color_primaries,
                                 vl_csc_matrix *matrix);

#endif /* vl_csc_h */
