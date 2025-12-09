# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

subdir('gtest')
subdir('c11/impl')
subdir('android_stub')
subdir('util')

inc_mesa = include_directories('mesa')
inc_src = include_directories('.')
inc_gallium = include_directories('gallium/include')
inc_gallium_aux = include_directories('gallium/auxiliary')
inc_amd_common = include_directories('amd/common', 'amd/common/nir')
inc_tool = include_directories('tool')
inc_virtio = include_directories('virtio')
inc_virtio_gpu = include_directories('virtio/virtio-gpu')
inc_frontends = include_directories('gallium/frontends')
pps_datasources = []
pps_includes = []

inc_llvmpipe = include_directories('gallium/drivers/llvmpipe')

if dep_llvm.found()
  inc_amd_common_llvm = include_directories('amd/llvm')
else
  inc_amd_common_llvm = []
endif

glsl_util_files = files(
    'mesa/main/extensions_table.c',
    'mesa/program/prog_parameter.c',
    'mesa/program/symbol_table.c',
    'mesa/program/dummy_errors.c',
)

sha1_h = custom_target(
  'git_sha1.h',
  output : 'git_sha1.h',
  command : [prog_python, git_sha1_gen_py, '--output', '@OUTPUT@'],
  build_by_default : true,
  build_always_stale : true, # commit sha1 can change without having touched these files
)

if cc.get_argument_syntax() == 'msvc'
  subdir('getopt')
else
  idep_getopt = null_dep
endif
if with_platform_x11
  subdir('x11')
endif
if with_gallium_or_lvp or with_gbm or with_platform_wayland
  subdir('loader')
endif
subdir('compiler')
if with_poly
  subdir('poly')
endif
if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif
if with_imgui
  subdir('imgui')
endif
if with_wayland_bind_display
  subdir('egl/wayland/wayland-drm')
endif
if with_any_vk or with_gallium_zink
  subdir('vulkan')
endif
if with_any_intel
  subdir('intel')
endif
if with_virtio_vk
  subdir('virtio/vulkan')
endif
if with_vdrm
  subdir('virtio/vdrm')
endif
if with_gallium_radeonsi or with_amd_vk or with_gallium_r300 or with_gallium_r600
  subdir('amd')
endif
if with_any_broadcom
  subdir('broadcom')
endif
if with_gallium_etnaviv
  subdir('etnaviv')
endif
if with_gallium_freedreno or with_freedreno_vk or with_tools.contains('freedreno')
  subdir('freedreno')
endif
if with_imagination_vk or with_tools.contains('imagination')
  subdir('imagination')
endif
if with_gallium_panfrost or with_gallium_lima or with_panfrost_vk or with_tools.contains('panfrost')
  subdir('panfrost')
endif
if with_microsoft_clc or with_gallium_d3d12 or with_spirv_to_dxil or with_microsoft_vk
  subdir('microsoft')
endif
if with_any_nouveau
  subdir('nouveau')
endif
if with_gfxstream_vk
  dep_virtgpu_kumquat_ffi = null_dep
  if with_virtgpu_kumquat
    subdir('util/rust')
    subdir('virtio/protocols')
    subdir('virtio/virtgpu_kumquat')
    subdir('virtio/virtgpu_kumquat_ffi')
  endif
  subdir('gfxstream')
endif
if with_gallium_asahi or with_asahi_vk or with_tools.contains('asahi')
  subdir('asahi')
endif
if with_kosmickrisp_vk
  subdir('kosmickrisp')
endif
if with_gallium
  subdir('mesa')
endif
if with_gallium_or_lvp
  subdir('gallium')
endif
# These require libgallium (shared_glapi_lib)
if with_gallium and (with_glx != 'disabled' or with_egl)
  if with_gles1 and not with_glvnd
    subdir('mesa/glapi/es1api')
  endif
  if with_gles2 and not with_glvnd
    subdir('mesa/glapi/es2api')
  endif
  if with_tests
    subdir('mesa/main/tests')
    subdir('mesa/state_tracker/tests')
  endif
endif
if with_glx == 'dri'
  subdir('glx')
endif

if with_gbm
  if get_option('libgbm-external')
    dep_gbm = dependency('gbm')
  else
    subdir('gbm')
  endif
else
  dep_gbm = null_dep
endif

if with_egl
  subdir('egl')
endif
if with_gallium and with_gbm
  if with_glx == 'dri' or with_platform_x11 or with_platform_xcb
    subdir('gallium/targets/dril')
  endif
endif

if with_gbm and with_dri2
  subdir('gbm/backends/dri')
endif

# This must be after at least mesa, glx, and gallium, since libgl will be
# defined in one of those subdirs depending on the glx provider.
if with_glx != 'disabled' and not with_glvnd
  pkg.generate(
    name : 'gl',
    description : 'Mesa OpenGL Library',
    version : meson.project_version(),
    libraries : libgl,
    libraries_private : gl_priv_libs,
    requires_private : gl_priv_reqs,
    variables : ['glx_tls=yes'],
  )
endif

subdir('tool')
