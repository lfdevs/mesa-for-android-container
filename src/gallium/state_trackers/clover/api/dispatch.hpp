//
// Copyright 2013 Francisco Jerez
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//

#ifndef API_DISPATCH_HPP
#define API_DISPATCH_HPP

#include "CL/cl.h"
#include "CL/cl_ext.h"
#include "CL/cl_egl.h"
#include "CL/cl_gl.h"
#include "CL/cl_icd.h"

namespace clover {
   extern const cl_icd_dispatch _dispatch;

   cl_int
   GetPlatformInfo(cl_platform_id d_platform, cl_platform_info param,
                   size_t size, void *r_buf, size_t *r_size);

   void *
   GetExtensionFunctionAddress(const char *p_name);

   void *
   GetExtensionFunctionAddressForPlatform(cl_platform_id d_platform,
                                          const char *p_name);

   cl_int
   IcdGetPlatformIDsKHR(cl_uint num_entries, cl_platform_id *rd_platforms,
                        cl_uint *rnum_platforms);
}

#endif
