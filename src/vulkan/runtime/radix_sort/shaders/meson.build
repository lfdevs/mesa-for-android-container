# Copyright © 2022 Konstantin Seurer

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

radix_sort_shaders = [
  [
    'init.comp',
    'u64_init',
    ['-DRS_KEYVAL_DWORDS=2'],
  ],
  [
    'init.comp',
    'u96_init',
    ['-DRS_KEYVAL_DWORDS=3'],
  ],
  [
    'fill.comp',
    'fill',
    [],
  ],
  [
    'histogram.comp',
    'u64_histogram',
    ['-DRS_KEYVAL_DWORDS=2'],
  ],
  [
    'histogram.comp',
    'u96_histogram',
    ['-DRS_KEYVAL_DWORDS=3'],
  ],
  [
    'prefix.comp',
    'u64_prefix',
    ['-DRS_KEYVAL_DWORDS=2'],
  ],
  [
    'prefix.comp',
    'u96_prefix',
    ['-DRS_KEYVAL_DWORDS=3'],
  ],
  [
    'scatter_0_even.comp',
    'u64_scatter_0_even',
    ['-DRS_KEYVAL_DWORDS=2'],
  ],
  [
    'scatter_0_even.comp',
    'u96_scatter_0_even',
    ['-DRS_KEYVAL_DWORDS=3'],
  ],
  [
    'scatter_0_odd.comp',
    'u64_scatter_0_odd',
    ['-DRS_KEYVAL_DWORDS=2'],
  ],
  [
    'scatter_0_odd.comp',
    'u96_scatter_0_odd',
    ['-DRS_KEYVAL_DWORDS=3'],
  ],
  [
    'scatter_1_even.comp',
    'u64_scatter_1_even',
    ['-DRS_KEYVAL_DWORDS=2'],
  ],
  [
    'scatter_1_even.comp',
    'u96_scatter_1_even',
    ['-DRS_KEYVAL_DWORDS=3'],
  ],
  [
    'scatter_1_odd.comp',
    'u64_scatter_1_odd',
    ['-DRS_KEYVAL_DWORDS=2'],
  ],
  [
    'scatter_1_odd.comp',
    'u96_scatter_1_odd',
    ['-DRS_KEYVAL_DWORDS=3'],
  ],
  [
    'scatter_2_even.comp',
    'u96_scatter_2_even',
    ['-DRS_KEYVAL_DWORDS=3'],
  ],
  [
    'scatter_2_odd.comp',
    'u96_scatter_2_odd',
    ['-DRS_KEYVAL_DWORDS=3'],
  ],
]

shader_include_files = files(
  'bufref.h',
  'prefix_limits.h',
  'prefix.h',
  'push.h',
  'scatter.glsl',
  'config.h',
)

radix_sort_spv = []
foreach s : radix_sort_shaders
  radix_sort_spv += custom_target(
    s[1] + '.spv.h',
    input : s[0],
    output : s[1] + '.spv.h',
    command : [
      prog_glslang, '-V', '--target-env', 'spirv1.5', '-x', '-o', '@OUTPUT@', '@INPUT@'
    ] + s[2] + glslang_quiet + (with_mesa_debug ? ['-g'] : []),
    depend_files: shader_include_files)
endforeach
