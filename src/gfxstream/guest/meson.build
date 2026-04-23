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
gfxstream_guest_args = cpp_msvc_compat_args + ['-D_FILE_OFFSET_BITS=64']

# Our internal guest build
if host_machine.system() == 'windows'
    gfxstream_guest_args += '-DWINDOWS_GUEST_BUILD'
elif host_machine.system() != 'fuchsia'
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

if with_gfxstream_emulated_android
    gfxstream_guest_args += '-DEND2END_TESTS'
endif

if with_platform_android
    gfxstream_guest_args += '-DGFXSTREAM_ENABLE_GUEST_GOLDFISH'
endif

#====================================#
#   Standard deps                    #
#====================================#
gfxstream_deps = [
    dep_libdrm,
    idep_mesautil,
    idep_vulkan_wsi_headers,
    idep_vulkan_lite_runtime,
    idep_vulkan_util_headers,
    idep_vulkan_wsi,
    dep_valgrind,
]

if with_perfetto
    gfxstream_deps += dep_perfetto
endif

#====================================#
#   Android deps (mostly Goldfish)   #
#   Only works with meson2hermetic   #
#====================================#
dep_android_base = null_dep
dep_android_arect = null_dep
dep_android_aemu_base = null_dep
dep_android_aemu_gl_codec = null_dep
dep_android_aemu_qemupipe = null_dep
dep_android_aemu_ringbuffer = null_dep
dep_android_aemu_rc_encoder = null_dep
if with_platform_android or with_gfxstream_emulated_android
    gfxstream_guest_args += '-DVK_USE_PLATFORM_ANDROID_KHR'
    dep_android_base = dependency('android-base')
    dep_android_arect = dependency('android-arect')
    dep_android_aemu_base = dependency('android-aemu-base')
    dep_android_aemu_gl_codec = dependency('android-aemu-gl-codec')
    dep_android_aemu_qemupipe = dependency('android-aemu-qemupipe')
    dep_android_aemu_ringbuffer = dependency('android-aemu-ringbuffer')
    dep_android_aemu_rc_encoder = dependency('android-aemu-rc-encoder')

    gfxstream_deps += dep_android
    gfxstream_deps += [
        dep_android_base,
        dep_android_arect,
        dep_android_aemu_base,
        dep_android_aemu_gl_codec,
        dep_android_aemu_ringbuffer,
        dep_android_aemu_rc_encoder,
        dep_android_aemu_qemupipe,
    ]
endif

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

if with_platform_android or with_gfxstream_emulated_android
    subdir('android')
endif

subdir('vulkan_enc')
subdir('vulkan')
