# Copyright © 2021 Ilia Mirkin
# SPDX-License-Identifier: MIT

subdir('drm')
subdir('headers')
if with_nouveau_vk
  subdir('compiler')
endif
subdir('winsys')
if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif

subdir('codegen')
if with_nouveau_vk
  subdir('nil')
  subdir('mme')
  subdir('vulkan')
endif
