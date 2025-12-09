# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

libi915drm = static_library(
  'i915drm',
  files(
    'i915_drm_batchbuffer.c', 'i915_drm_buffer.c', 'i915_drm_fence.c',
    'i915_drm_winsys.c'
  ),
  include_directories : [
    inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_gallium_drivers
  ],
  link_with : [libintel_common],
  dependencies : [dep_libdrm, dep_libdrm_intel, idep_intel_dev_info_gen, idep_intel_dev_wa, idep_mesautil],
)
