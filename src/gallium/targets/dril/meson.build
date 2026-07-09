# Copyright © 2017 Dylan Baker
# Copyright © 2018 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# TODO: support non-static targets
# Static targets are always enabled in autotools (unless you modify
# configure.ac)

dril_c_args = []
dril_ld_args = []
dril_link_depends = []
dril_drivers = []

dri_sym = configure_file(input : 'dri.sym.in', output : 'dri.sym', configuration : sym_config)

if with_ld_version_script
  dril_ld_args += ['-Wl,--version-script', join_paths(meson.current_build_dir(), 'dri.sym')]
  dril_link_depends += dri_sym
endif
if with_ld_dynamic_list
  dril_ld_args += ['-Wl,--dynamic-list', join_paths(meson.current_source_dir(), '../dri.dyn')]
  dril_link_depends += files('../dri.dyn')
endif

dril_dri = shared_library(
  'dril_dri',
  files('dril_target.c'),
  include_directories : [
    inc_include, inc_src, inc_mesa, inc_gallium, inc_gallium_aux, inc_util, inc_gallium_drivers,
    inc_gallium_winsys,
  ],
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_build_id, ld_args_gc_sections, dril_ld_args],
  link_depends : dril_link_depends,
  link_with : [
    libgallium,
  ],
  dependencies : [
    idep_mesautil,
    dep_gbm,
  ],
  # Will be deleted during installation, see install_megadrivers.py
  install : true,
  install_dir : dri_drivers_path,
  name_suffix : libname_suffix,
)

foreach d : [[with_gallium_kmsro, [
               'apple',
               'armada-drm',
               'exynos',
               'gm12u320',
               'hdlcd',
               'hx8357d',
               'ili9163',
               'ili9225',
               'ili9341',
               'ili9486',
               'imx-drm',
               'imx-dcss',
               'imx-lcdif',
               'ingenic-drm',
               'kirin',
               'komeda',
               'mali-dp',
               'mcde',
               'mediatek',
               'meson',
               'mi0283qt',
               'mxsfb-drm',
               'panel-mipi-dbi',
               'pl111',
               'rcar-du',
               'repaper',
               'rockchip',
               'rzg2l-du',
               'ssd130x',
               'st7586',
               'st7735r',
               'sti',
               'stm',
               'sun4i-drm',
               'udl',
               'vkms',
               'zynqmp-dpsub',
             ]],
             [with_gallium_radeonsi, ['radeonsi']],
             [with_gallium_nouveau, ['nouveau']],
             [with_gallium_freedreno, ['msm', 'kgsl']],
             [with_gallium_swrast, ['swrast']],
             [with_gallium_swrast and with_gallium_drisw_kms, ['kms_swrast']],
             [with_gallium_v3d, ['v3d']],
             [with_gallium_vc4, ['vc4']],
             [with_gallium_panfrost, ['panfrost', 'panthor']],
             [with_gallium_etnaviv, ['etnaviv']],
             [with_gallium_tegra, ['tegra']],
             [with_gallium_crocus, ['crocus']],
             [with_gallium_iris, ['iris']],
             [with_gallium_i915, ['i915']],
             [with_gallium_r300, ['r300']],
             [with_gallium_r600, ['r600']],
             [with_gallium_svga, ['vmwgfx']],
             [with_gallium_virgl or
               (with_gallium_freedreno and freedreno_kmds.contains('virtio')) or
               (with_gallium_radeonsi and with_amdgpu_virtio),
               ['virtio_gpu']],
             [with_gallium_lima, ['lima']],
             [with_gallium_d3d12, ['d3d12']],
             [with_gallium_zink, ['zink']],
             [with_gallium_asahi, ['asahi']]]
  if d[0]
    foreach name : d[1]
      dril_drivers += '@0@_dri.@1@'.format(name, libname_suffix)
    endforeach
  endif
endforeach

# This only works on Unix-like oses, which is probably fine for dri
if prog_ln.found()
  foreach d : dril_drivers
    custom_target(
      d,
      output : d,
      command : [prog_ln, '-sf', dril_dri.full_path(), '@OUTPUT@'],
      build_by_default : true,
    )
  endforeach
endif

if dril_drivers.length() > 0
  meson.add_install_script(
    install_megadrivers,
    dril_dri.full_path(),
    dri_drivers_path,
    dril_drivers,
    install_tag : 'runtime',
  )
endif
