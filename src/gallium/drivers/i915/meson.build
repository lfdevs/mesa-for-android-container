# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

files_i915 = files(
  'i915_batchbuffer.h',
  'i915_batch.h',
  'i915_blit.c',
  'i915_blit.h',
  'i915_clear.c',
  'i915_context.c',
  'i915_context.h',
  'i915_debug.c',
  'i915_debug_fp.c',
  'i915_debug.h',
  'i915_debug_private.h',
  'i915_flush.c',
  'i915_fpc_emit.c',
  'i915_fpc.h',
  'i915_fpc_optimize.c',
  'i915_fpc_translate.c',
  'i915_prim_emit.c',
  'i915_prim_vbuf.c',
  'i915_public.h',
  'i915_query.c',
  'i915_query.h',
  'i915_reg.h',
  'i915_resource_buffer.c',
  'i915_resource.c',
  'i915_resource.h',
  'i915_resource_texture.c',
  'i915_screen.c',
  'i915_screen.h',
  'i915_state.c',
  'i915_state_derived.c',
  'i915_state_dynamic.c',
  'i915_state_emit.c',
  'i915_state_fpc.c',
  'i915_state.h',
  'i915_state_immediate.c',
  'i915_state_inlines.h',
  'i915_state_sampler.c',
  'i915_state_static.c',
  'i915_surface.c',
  'i915_surface.h',
  'i915_winsys.h',
)

libi915 = static_library(
  'i915',
  files_i915,
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_include, inc_src, inc_gallium, inc_gallium_aux],
  dependencies : [idep_nir, idep_mesautil],
)

driver_i915 = declare_dependency(
  compile_args : '-DGALLIUM_I915',
  link_with : [libi915, libi915drm],
)
