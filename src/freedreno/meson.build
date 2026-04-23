# Copyright © 2018 Rob Clark
# SPDX-License-Identifier: MIT

inc_freedreno = include_directories(['.', './registers', './registers/adreno', './common'])
inc_freedreno_rnn = include_directories('rnn')

rnn_src_path = dir_source_root + '/src/freedreno/registers'
rnn_install_path = get_option('datadir') + '/freedreno/registers'
rnn_path = rnn_src_path + ':' + get_option('prefix') + '/' + rnn_install_path

dep_libarchive = dependency('libarchive', allow_fallback: true, required: false, disabler : true)
dep_libxml2 = dependency('libxml-2.0', allow_fallback: true, required: false)
prog_gzip = find_program('gzip', required: false)

install_fd_decode_tools = dep_libxml2.found() and prog_gzip.found() and \
  with_tools.contains('freedreno')

subdir('registers')
subdir('common')
subdir('isa')
subdir('ir2')
subdir('ir3')
subdir('fdl')

if system_has_kms_drm
  subdir('drm')
endif

subdir('perfcntrs')

if system_has_kms_drm
  subdir('computerator')
endif

if system_has_kms_drm and with_perfetto and (with_datasources.contains('freedreno') or with_datasources.contains('auto'))
  subdir('ds')
endif

# Everything that depends on rnn requires (indirectly) libxml2:
if dep_libxml2.found() and with_tools.contains('freedreno')
  subdir('rnn')
  subdir('decode')
  subdir('qrisc')
elif with_tools.contains('freedreno')
  message('libxml2 not found, skipping tools that depend on it')
endif

if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif

if with_freedreno_vk
  subdir('vulkan')
endif

# 77 is a convention used by Meson and other build systems to skip tests.
SKIP_RETURN_CODE = 77

check_xml_includes = run_command(
  prog_python,
  files('tests/check_xml_includes.py'),
  check: false
)
if check_xml_includes.returncode() == SKIP_RETURN_CODE
  message('Skipped checking freedreno XML includes.')
elif check_xml_includes.returncode() != 0
  error('XML include check found errors:\n' + check_xml_includes.stdout())
endif

test('check-xml-includes',
  prog_python,
  args : files('tests/check_xml_includes.py'),
  suite : ['freedreno'],
)
