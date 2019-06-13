# Copyright © 2018 Lima Project
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

files_lima = files(
  'ir/gp/gpir.h',
  'ir/gp/nir.c',
  'ir/gp/node.c',
  'ir/gp/lower.c',
  'ir/gp/scheduler.c',
  'ir/gp/instr.c',
  'ir/gp/codegen.h',
  'ir/gp/codegen.c',
  'ir/gp/reduce_scheduler.c',
  'ir/gp/value_regalloc.c',
  'ir/gp/physical_regalloc.c',
  'ir/gp/disasm.c',

  'ir/pp/ppir.h',
  'ir/pp/nir.c',
  'ir/pp/node.c',
  'ir/pp/lower.c',
  'ir/pp/scheduler.c',
  'ir/pp/instr.c',
  'ir/pp/regalloc.c',
  'ir/pp/codegen.h',
  'ir/pp/codegen.c',
  'ir/pp/node_to_instr.c',
  'ir/pp/disasm.c',

  'ir/lima_nir_lower_uniform_to_scalar.c',

  'ir/lima_ir.h',

  'lima_screen.c',
  'lima_screen.h',
  'lima_context.c',
  'lima_context.h',
  'lima_resource.c',
  'lima_resource.h',
  'lima_state.c',
  'lima_draw.c',
  'lima_program.c',
  'lima_query.c',
  'lima_bo.c',
  'lima_bo.h',
  'lima_submit.c',
  'lima_submit.h',
  'lima_util.c',
  'lima_util.h',
  'lima_texture.c',
  'lima_texture.h',
  'lima_fence.c',
  'lima_fence.h',
  'lima_tiling.c',
  'lima_tiling.h',
)

liblima = static_library(
  'lima',
  files_lima,
  include_directories : [
    inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_gallium_drivers,
  ],
  dependencies : [dep_libdrm, idep_nir_headers],
)

driver_lima = declare_dependency(
  compile_args : '-DGALLIUM_LIMA',
  link_with : [liblima, liblimawinsys],
  dependencies : idep_nir,
)
