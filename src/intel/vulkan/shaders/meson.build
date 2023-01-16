# Copyright © 2022 Intel Corporation

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

float64_spv_h = custom_target(
  'float64_spv.h',
  input : [glsl2spirv, float64_glsl_file],
  output : 'float64_spv.h',
  command : [
    prog_python, '@INPUT@', '@OUTPUT@',
    prog_glslang,
    '--create-entry', 'main',
    '--vn', 'float64_spv_source',
    '--glsl-version', '450',
    '-Olib',
  ]
)

generated_draws_spv_h = custom_target(
  'generated_draws_spv.h',
  input : [glsl2spirv, 'generated_draws.glsl'],
  output : 'generated_draws_spv.h',
  command : [
    prog_python, '@INPUT@', '@OUTPUT@',
    prog_glslang,
    '--vn', 'generated_draws_spv_source',
    '--glsl-version', '450',
    '--stage', 'frag',
  ]
)

generated_draws_count_spv_h = custom_target(
  'generated_draws_count_spv.h',
  input : [glsl2spirv, 'generated_draws_count.glsl'],
  output : 'generated_draws_count_spv.h',
  command : [
    prog_python, '@INPUT@', '@OUTPUT@',
    prog_glslang,
    '--vn', 'generated_draws_count_spv_source',
    '--glsl-version', '450',
    '--stage', 'frag',
  ]
)
