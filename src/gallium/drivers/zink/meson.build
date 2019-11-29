# Copyright © 2018 Collabora Ltd

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

files_libzink = files(
  'nir_to_spirv/nir_to_spirv.c',
  'nir_to_spirv/spirv_builder.c',
  'zink_batch.c',
  'zink_compiler.c',
  'zink_context.c',
  'zink_fence.c',
  'zink_framebuffer.c',
  'zink_pipeline.c',
  'zink_program.c',
  'zink_query.c',
  'zink_render_pass.c',
  'zink_resource.c',
  'zink_screen.c',
  'zink_state.c',
  'zink_surface.c',
)

libzink = static_library(
  'zink',
  files_libzink,
  c_args : c_vis_args,
  include_directories : inc_common,
  dependencies: [dep_vulkan, idep_nir_headers],
)

driver_zink = declare_dependency(
  compile_args : '-DGALLIUM_ZINK',
  link_with : [libzink],
)
