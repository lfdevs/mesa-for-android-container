# Copyright © 2017 Intel Corporation

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

gen_knobs_cpp = custom_target(
  'gen_knobs.cpp',
  input : ['gen_knobs.py'],
  output : 'gen_knobs.cpp',
  command : [prog_python2, '@INPUT0@', '--output', '@OUTPUT@', '--gen_cpp'],
  depend_files : files(
    'knob_defs.py', 'gen_common.py',
    'templates/gen_knobs.cpp',
  ),
)

gen_knobs_h = custom_target(
  'gen_knobs.h',
  input : ['gen_knobs.py'],
  output : 'gen_knobs.h',
  command : [prog_python2, '@INPUT0@', '--output', '@OUTPUT@', '--gen_h'],
  depend_files : files(
    'knob_defs.py', 'gen_common.py',
    'templates/gen_knobs.h',
  ),
)

gen_builder_hpp = custom_target(
  'gen_builder.hpp',
  input : [
    'gen_llvm_ir_macros.py',
    join_paths(
      dep_llvm.get_configtool_variable('includedir'), 'llvm', 'IR',
      'IRBuilder.h'
    )
  ],
  output : 'gen_builder.hpp',
  command : [
    prog_python2, '@INPUT0@', '--input', '@INPUT1@', '--output', '@OUTPUT@',
    '--gen_h', '--output-dir', meson.current_build_dir()
  ],
  depend_files : files(
    'templates/gen_builder.hpp',
    'gen_common.py',
  ),
  build_by_default : true,
)

gen_builder_x86_hpp = custom_target(
  'gen_builder_x86.hpp',
  input : 'gen_llvm_ir_macros.py',
  output : 'gen_builder_x86.hpp',
  command : [
    prog_python2, '@INPUT0@', '--gen_x86_h', '--output', '@OUTPUT@',
    '--output-dir', meson.current_build_dir()
  ],
  depend_files : files(
    'templates/gen_builder.hpp',
    'gen_common.py',
  ),
)

# The generators above this are needed individually, while the below generators
# are all inputs to the same lib, so they don't need unique names.
files_swr_common += [
  gen_builder_hpp, gen_builder_x86_hpp, gen_knobs_h, gen_knobs_cpp
]

foreach x : [[swr_context_files, 'gen_swr_context_llvm.h'],
             [swr_state_files, 'gen_state_llvm.h']]
  files_swr_common += custom_target(
    x[1],
    input : ['gen_llvm_types.py', x[0]],
    output : x[1],
    command : [prog_python2, '@INPUT0@', '--input', '@INPUT1@', '--output', '@OUTPUT@'],
    depend_files : files(
      'templates/gen_llvm.hpp',
      'gen_common.py',
    ),
  )
endforeach

foreach x : [['gen_ar_event.hpp', '--gen_event_hpp'],
             ['gen_ar_event.cpp', '--gen_event_cpp'],
             ['gen_ar_eventhandler.hpp', '--gen_eventhandler_h'],
             ['gen_ar_eventhandlerfile.hpp', '--gen_eventhandlerfile_h']]
  files_swr_common += custom_target(
    x[0],
    input : ['gen_archrast.py', swr_event_proto_files],
    output : x[0],
    command : [
      prog_python2, '@INPUT0@', '--proto', '@INPUT1@', '--output', '@OUTPUT@',
      x[1],
    ],
    depend_files : files(
      join_paths('templates', x[0]),
      'gen_common.py',
    ),
  )
endforeach

files_swr_common += custom_target(
  'gen_backend_pixel',
  input : 'gen_backends.py',
  output : [
    'gen_BackendPixelRate0.cpp', 'gen_BackendPixelRate1.cpp',
    'gen_BackendPixelRate2.cpp', 'gen_BackendPixelRate3.cpp',
    'gen_BackendPixelRate.hpp',
  ],
  command : [
    prog_python2, '@INPUT@',
    '--outdir', meson.current_build_dir(),
    '--dim', '5', '2', '3', '2', '2', '2',
    '--numfiles', '4',
    '--cpp', '--hpp',
  ],
  depend_files : files(
    'templates/gen_backend.cpp',
    'templates/gen_header_init.hpp',
  ),
)

files_swr_common += custom_target(
  'gen_backend_raster',
  input : 'gen_backends.py',
  output : [
    'gen_rasterizer0.cpp', 'gen_rasterizer1.cpp',
    'gen_rasterizer2.cpp', 'gen_rasterizer3.cpp',
    'gen_rasterizer.hpp',
  ],
  command : [
    prog_python2, '@INPUT@',
    '--outdir', meson.current_build_dir(),
    '--rast',
    '--dim', '5', '2', '2', '3', '5', '2',
    '--numfiles', '4',
    '--cpp', '--hpp',
  ],
  depend_files : files(
    'templates/gen_rasterizer.cpp',
    'templates/gen_header_init.hpp',
  ),
)
