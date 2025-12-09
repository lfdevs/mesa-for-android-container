# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

inc_intel = include_directories('.')
inc_intel_compiler = include_directories('compiler')

with_intel_elk = get_option('intel-elk')

intel_common_args = []
if with_intel_elk
  intel_common_args += ['-DINTEL_USE_ELK']
else
  if with_gallium_crocus or with_intel_hasvk
    error('Cannot build Crocus or Hasvk with meson option -Dintel-elk=false set.')
  endif
endif

subdir('mda')
subdir('genxml')
subdir('dev')
if with_intel_hasvk or with_intel_vk or with_gallium_crocus or with_gallium_iris
  subdir('blorp')
endif
subdir('isl')
subdir('common')
subdir('compiler')
subdir('decoder')
if with_intel_vk or with_gallium_iris
  subdir('shaders')
endif
if with_intel_hasvk or with_intel_vk or with_gallium_crocus or with_gallium_iris or with_intel_tools
  subdir('perf')
endif
if with_intel_hasvk or with_intel_vk or with_gallium_iris
  subdir('ds')
endif
if with_intel_tools
  subdir('tools')
  subdir('executor')
endif
if get_option('vulkan-layers').contains('intel-nullhw')
  subdir('nullhw-layer')
endif
if with_intel_vk
  subdir('vulkan')
endif
if with_intel_hasvk
  subdir('vulkan_hasvk')
endif
