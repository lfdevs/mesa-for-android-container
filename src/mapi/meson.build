# Copyright © 2017-2019 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

files_mapi_util = files(
  'u_current.c',
  'u_current.h',
  'u_execmem.c',
  'u_execmem.h',
)

genCommon_py = files('new/genCommon.py')
glapi_gen_mapi_py = files('new/gen_gldispatch_mapi.py')
mapi_abi_py = files('mapi_abi.py')

subdir('glapi')
if with_shared_glapi
  subdir('shared-glapi')
else
  libglapi = []
endif
if with_gles1
  if not with_glvnd
    subdir('es1api')
  elif not glvnd_has_headers_and_pc_files
    pkg.generate(
      name : 'glesv1_cm',
      filebase : 'glesv1_cm',
      description : 'Mesa OpenGL ES 1.1 CM library',
      version : meson.project_version(),
      libraries : '-L${libdir} -lGLESv1_CM',
      libraries_private : gl_priv_libs,
    )
  endif
endif
if with_gles2
  if not with_glvnd
    subdir('es2api')
  elif not glvnd_has_headers_and_pc_files
    pkg.generate(
      name : 'glesv2',
      filebase : 'glesv2',
      description : 'Mesa OpenGL ES 2.0 library',
      version : meson.project_version(),
      libraries : '-L${libdir} -lGLESv2',
      libraries_private : gl_priv_libs,
    )
  endif
endif
