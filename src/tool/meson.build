# Copyright © 2021 Collabora, Ltd.
# Author: Antonio Caggiano <antonio.caggiano@collabora.com>
# SPDX-License-Identifier: MIT

if with_perfetto and dep_libdrm.found()
  subdir('pps')
endif

if with_tools.contains('dlclose-skip')
  subdir('dlclose-skip')
endif

if with_tools.contains('gamma')
  subdir('gamma')
endif
