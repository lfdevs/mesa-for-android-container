# Copyright © 2017 Dylan Baker
# SPDX-License-Identifier: MIT

files_libnouveau_codegen = files(
  'nv50_ir.cpp',
  'nv50_ir_bb.cpp',
  'nv50_ir_build_util.cpp',
  'nv50_ir_build_util.h',
  'nv50_ir_driver.h',
  'nv50_ir_emit_nv50.cpp',
  'nv50_ir_from_nir.cpp',
  'nv50_ir_graph.cpp',
  'nv50_ir_graph.h',
  'nv50_ir.h',
  'nv50_ir_inlines.h',
  'nv50_ir_lowering_helper.cpp',
  'nv50_ir_lowering_helper.h',
  'nv50_ir_lowering_nv50.cpp',
  'nv50_ir_peephole.cpp',
  'nv50_ir_print.cpp',
  'nv50_ir_ra.cpp',
  'nv50_ir_serialize.cpp',
  'nv50_ir_ssa.cpp',
  'nv50_ir_target.cpp',
  'nv50_ir_target.h',
  'nv50_ir_target_nv50.cpp',
  'nv50_ir_target_nv50.h',
  'nv50_ir_util.cpp',
  'nv50_ir_util.h',
  'nv50_ir_emit_gv100.cpp',
  'nv50_ir_emit_gv100.h',
  'nv50_ir_emit_gk110.cpp',
  'nv50_ir_emit_gm107.cpp',
  'nv50_ir_emit_nvc0.cpp',
  'nv50_ir_lowering_gv100.cpp',
  'nv50_ir_lowering_gv100.h',
  'nv50_ir_lowering_gm107.cpp',
  'nv50_ir_lowering_gm107.h',
  'nv50_ir_lowering_nvc0.cpp',
  'nv50_ir_lowering_nvc0.h',
  'nv50_ir_target_gv100.cpp',
  'nv50_ir_target_gv100.h',
  'nv50_ir_target_gm107.cpp',
  'nv50_ir_target_gm107.h',
  'nv50_ir_target_nvc0.cpp',
  'nv50_ir_target_nvc0.h',
)

libnouveau_codegen = static_library(
  'nouveau_codegen',
  files_libnouveau_codegen,
  include_directories : [
    inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_mapi, inc_mesa,
  ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_libdrm, idep_mesautil, idep_nir_headers],
)

idep_nouveau_codegen = declare_dependency(
  sources : [ 'nv50_ir_driver.h' ],
  include_directories : include_directories('.'),
)
