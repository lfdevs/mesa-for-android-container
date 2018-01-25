# Copyright © 2017 Dylan Baker

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

files_libradeon = files(
  'r600_buffer_common.c',
  'r600_cs.h',
  'r600_gpu_load.c',
  'r600_perfcounter.c',
  'r600_pipe_common.c',
  'r600_pipe_common.h',
  'r600_query.c',
  'r600_query.h',
  'r600_texture.c',
  'radeon_uvd.c',
  'radeon_uvd.h',
  'radeon_vcn_enc_1_2.c',
  'radeon_vcn_enc.c',
  'radeon_vcn_enc.h',
  'radeon_vcn_dec.c',
  'radeon_vcn_dec.h',
  'radeon_vce_40_2_2.c',
  'radeon_vce_50.c',
  'radeon_vce_52.c',
  'radeon_vce.c',
  'radeon_vce.h',
  'radeon_video.c',
  'radeon_video.h',
  'radeon_winsys.h',
)

libradeon = static_library(
  'radeon',
  files_libradeon,
  c_args : ['-Wstrict-overflow=0', c_vis_args],
  dependencies : [dep_llvm, dep_clock],
  include_directories : [
    inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_gallium_drivers, inc_amd_common,
  ],
)
