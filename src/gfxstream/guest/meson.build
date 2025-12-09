# Copyright 2022 Android Open Source Project
# SPDX-License-Identifier: MIT

#===============#
# Options       #
#===============#

with_vulkan_icd_dir = get_option('vulkan-icd-dir')
if with_vulkan_icd_dir == ''
    with_vulkan_icd_dir = join_paths(get_option('datadir'), 'vulkan/icd.d')
endif

#===============#
# Configuration #
#===============#
gfxstream_guest_args = ['-D_FILE_OFFSET_BITS=64']

# Our internal guest build
if host_machine.system() == 'windows'
    gfxstream_guest_args += '-DWINDOWS_GUEST_BUILD'
else
    gfxstream_guest_args += '-DLINUX_GUEST_BUILD'
endif
# Include the gfxstream private VkStructureType definitions
gfxstream_guest_args += '-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT'

gfxstream_guest_args += '-DVK_BASE_VERSION_1_0'
gfxstream_guest_args += '-DVK_GRAPHICS_VERSION_1_0'
gfxstream_guest_args += '-DVK_COMPUTE_VERSION_1_0'

gfxstream_guest_args += '-DVK_BASE_VERSION_1_1'
gfxstream_guest_args += '-DVK_GRAPHICS_VERSION_1_1'
gfxstream_guest_args += '-DVK_COMPUTE_VERSION_1_1'

gfxstream_guest_args += '-DVK_BASE_VERSION_1_2'
gfxstream_guest_args += '-DVK_GRAPHICS_VERSION_1_2'
gfxstream_guest_args += '-DVK_COMPUTE_VERSION_1_2'

gfxstream_guest_args += '-DVK_BASE_VERSION_1_3'
gfxstream_guest_args += '-DVK_GRAPHICS_VERSION_1_3'
gfxstream_guest_args += '-DVK_COMPUTE_VERSION_1_3'

gfxstream_guest_args += '-DVK_BASE_VERSION_1_4'
gfxstream_guest_args += '-DVK_GRAPHICS_VERSION_1_4'
gfxstream_guest_args += '-DVK_COMPUTE_VERSION_1_4'

#===============#
# Includes      #
#===============#

inc_vulkan_headers = include_directories('../../../include/vulkan')
inc_vulkan_enc = include_directories('vulkan_enc')

#================#
# Subdirectories #
#================#
subdir('iostream')
subdir('platform')
subdir('GoldfishAddressSpace')
subdir('connection-manager')
subdir('vulkan_enc')
subdir('vulkan')
