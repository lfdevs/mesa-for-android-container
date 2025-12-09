/*
    Copyright (c) Microsoft Corporation

    Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/
#include "poly/cl/tessellator.h"

KERNEL(64)
libagx_tess_isoline(constant struct poly_tess_params *p,
                    enum poly_tess_mode mode__2)
{
   uint patch = cl_global_id.x;
   poly_tess_isoline_process(p, patch, mode__2);
}

KERNEL(64)
libagx_tess_tri(constant struct poly_tess_params *p, enum poly_tess_mode mode__2)
{
   uint patch = cl_global_id.x;
   poly_tess_tri_process(p, patch, mode__2);
}

KERNEL(64)
libagx_tess_quad(constant struct poly_tess_params *p, enum poly_tess_mode mode__2)
{
   uint patch = cl_global_id.x;
   poly_tess_quad_process(p, patch, mode__2);
}
