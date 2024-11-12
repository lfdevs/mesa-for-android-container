# Copyright © 2021 Ilia Mirkin
# SPDX-License-Identifier: MIT

subdir('drm')
subdir('headers')
subdir('winsys')
if with_nouveau_vk
  subdir('compiler')
endif
if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif

subdir('codegen')
if with_nouveau_vk
  subdir('nil')
  subdir('mme')
  subdir('vulkan')
endif
