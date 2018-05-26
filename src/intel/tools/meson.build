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

aubinator = executable(
  'aubinator',
  files('aubinator.c', 'disasm.c', 'gen_batch_decoder.c',
        'gen_disasm.h', 'intel_aub.h'),
  dependencies : [dep_expat, dep_zlib, dep_dl, dep_thread, dep_m],
  include_directories : [inc_common, inc_intel],
  link_with : [libintel_common, libintel_compiler, libintel_dev, libmesa_util],
  c_args : [c_vis_args, no_override_init_args],
  build_by_default : with_tools.contains('intel'),
  install : with_tools.contains('intel'),
)

aubinator_error_decode = executable(
  'aubinator_error_decode',
  files('aubinator_error_decode.c', 'disasm.c', 'gen_disasm.h',
        'gen_batch_decoder.c'),
  dependencies : [dep_zlib, dep_thread],
  include_directories : [inc_common, inc_intel],
  link_with : [libintel_common, libintel_compiler, libintel_dev, libmesa_util],
  c_args : [c_vis_args, no_override_init_args],
  build_by_default : with_tools.contains('intel'),
  install : with_tools.contains('intel'),
)

if with_tools.contains('intel')
  sanitize_data = configuration_data()
  sanitize_data.set(
    'install_libexecdir',
    join_paths(get_option('prefix'), get_option('libexecdir'))
  )
  configure_file(
    input: 'intel_sanitize_gpu.in',
    output: '@BASENAME@',
    install_dir: get_option('bindir'),
    configuration: sanitize_data,
  )

  libintel_sanitize_gpu = shared_library(
    'intel_sanitize_gpu',
    files('intel_sanitize_gpu.c'),
    dependencies : [dep_dl, dep_thread],
    include_directories : [inc_common, inc_intel, inc_drm_uapi],
    link_with : [libintel_common, libmesa_util],
    c_args : [c_vis_args, no_override_init_args],
    install_dir: get_option('libexecdir'),
    install: true,
  )
endif
