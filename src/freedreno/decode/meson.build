# Copyright © 2020 Google, Inc
# SPDX-License-Identifier: MIT

if with_tests
  diff = find_program('diff')
endif

cffdump_pkt_handler_h = custom_target(
  'cffdump-pkt-handler.h',
  input : [files_xxd, 'cffdump-pkt-handler.lua'],
  output : 'cffdump-pkt-handler.h',
  command : [prog_python, '@INPUT@', '@OUTPUT@', '-n', 'cffdump_pkt_handler_lua_src'],
)

# Shared cmdstream decoding:
libfreedreno_cffdec = static_library(
  'freedreno_cffdec',
  [
    'buffers.c',
    'buffers.h',
    'cffdec.c',
    'cffdec.h',
    'pager.c',
    'pager.h',
    'rnnutil.c',
    'rnnutil.h',
    'script.c',
    'script.h',
    'util.h',
    cffdump_pkt_handler_h,
    freedreno_pm4_xml_header_file,
  ],
  include_directories: [
    inc_freedreno,
    inc_freedreno_rnn,
    inc_include,
    inc_src,
  ],
  c_args : [ no_override_init_args ],
  gnu_symbol_visibility: 'hidden',
  dependencies: [
    dep_lua,
  ],
  link_with: [
    libfreedreno_rnn,
    libfreedreno_ir2,  # for disasm_a2xx
    libfreedreno_ir3,  # for disasm_a3xx
    _libmesa_util,
  ],
  build_by_default: false,
)

libfreedreno_io = static_library(
  'freedreno_io',
  [
    'io.c',
    'io.h',
  ],
  include_directories: [],
  c_args : [no_override_init_args],
  gnu_symbol_visibility: 'hidden',
  dependencies: [
    dep_libarchive,
  ],
  build_by_default: false,
)

foreach kmd : freedreno_kmds
  if not (kmd in ['kgsl', 'msm', 'wsl'])
    warning('replay not supported for ' + kmd + ' KMD')
    continue
  endif

  # Only append the KMD name if there are multiple KMDs
  if freedreno_kmds.length() == 1
    replay_name = 'replay'
  else
    replay_name = 'replay-' + kmd
  endif

  replay_flags = []
  replay_deps = []
  if kmd == 'kgsl'
    replay_flags += '-DFD_REPLAY_KGSL'
  elif kmd == 'msm'
    replay_flags += '-DFD_REPLAY_MSM'
    replay_deps += dep_libdrm
  elif kmd == 'wsl'
    replay_flags += '-DFD_REPLAY_WSL'
  endif

  replay = executable(
    replay_name,
    [
      'replay.c',
      freedreno_pm4_xml_header_file,
    ],
    include_directories: [
      inc_freedreno,
      inc_include,
      inc_src,
    ],
    c_args : [no_override_init_args, replay_flags],
    gnu_symbol_visibility: 'hidden',
    dependencies: replay_deps,
    link_with: [
      libfreedreno_cffdec,
      libfreedreno_io,
    ],
    build_by_default: with_tools.contains('freedreno'),
    install: install_fd_decode_tools,
  )
endforeach

rddecompiler = executable(
  'rddecompiler',
  [
    'rddecompiler.c',
    freedreno_xml_header_files,
  ],
  include_directories: [
    inc_freedreno,
    inc_freedreno_rnn,
    inc_include,
    inc_src,
  ],
  c_args : [no_override_init_args],
  gnu_symbol_visibility: 'hidden',
  dependencies: [
    dep_libdrm,
  ],
  link_with: [
    libfreedreno_cffdec,
    libfreedreno_io,
  ],
  build_by_default: with_tools.contains('freedreno'),
  install: install_fd_decode_tools,
)

generate_rd = executable(
  'generate_rd',
  [
    'generate-rd.cc',
    freedreno_xml_header_files,
  ],
  include_directories: [
    inc_freedreno,
    inc_freedreno_rnn,
    inc_include,
    inc_src,
  ],
  link_with: [
    libfreedreno_cffdec,
  ],
  dependencies : [
    idep_nir,
  ],
  install: install_fd_decode_tools,
)

if is_disabler(rddecompiler)
  warning('libarchive not found, not building replay or rddecompiler')
endif

cffdump = executable(
  'cffdump',
  [
    'cffdump.c',
    freedreno_pm4_xml_header_file,
  ],
  include_directories: [
    inc_freedreno,
    inc_freedreno_rnn,
    inc_include,
    inc_src,
  ],
  c_args : [no_override_init_args],
  gnu_symbol_visibility: 'hidden',
  dependencies: [
    dep_lua,
  ],
  link_with: [
    libfreedreno_cffdec,
    libfreedreno_io,
  ],
  build_by_default: with_tools.contains('freedreno'),
  install: install_fd_decode_tools,
)

crashdec = executable(
  'crashdec',
  [
    'crashdec.c',
    'crashdec.h',
    'crashdec-hfi.c',
    'crashdec-mempool.c',
    'crashdec-prefetch.c',
    freedreno_pm4_xml_header_file,
  ],
  include_directories: [
    inc_freedreno,
    inc_freedreno_rnn,
    inc_include,
    inc_src,
  ],
  gnu_symbol_visibility: 'hidden',
  dependencies: [
    dep_lua,
  ],
  link_with: [
    libfreedreno_cffdec,
  ],
  build_by_default: with_tools.contains('freedreno'),
  install: install_fd_decode_tools,
)

if with_tests
  # dump only a single frame, and single tile pass, to keep the
  # reference output size managable
  cffdump_tests = [
    ['fd-clouds', ['--frame', '0', '--once']],
    ['es2gears-a320', ['--frame', '0', '--once']],
    ['glxgears-a420', ['--frame', '1', '--once']],
    ['compute-a540', ['--once']],
    ['dEQP-GLES2.functional.texture.specification.basic_teximage2d.rgba16f_2d', ['--once']],
    ['dEQP-VK.draw.indirect_draw.indexed.indirect_draw_count.triangle_list', ['--frame', '0', '--once']],
    # Test a lua script to ensure we don't break scripting API
    ['shadow', ['--script', files('scripts/parse-submits.lua')]],
  ]
  foreach cffdump_test: cffdump_tests
    name = cffdump_test[0]
    args = cffdump_test[1]

    log = custom_target(name + '.log',
      output: name + '.log',
      command: [cffdump, '--unit-test', args, files('../tests/traces/' + name + '.rd.gz')],
      capture: true,
    )
    test('cffdump-' + name,
      diff,
      args: ['-u', files('../tests/reference/' + name + '.log'), log],
      suite: 'freedreno',
      workdir: dir_source_root
    )

  endforeach
endif

if is_disabler(crashdec)
  warning('lua or libarchive not found, not building cffdump')
endif

dumpregs = executable(
  'dumpregs',
  [ 'dumpregs.c' ],
  include_directories: [
    inc_freedreno,
    inc_freedreno_rnn,
    inc_include,
    inc_src,
  ],
  gnu_symbol_visibility: 'hidden',
  dependencies: [],
  link_with: [
    libfreedreno_cffdec,
  ],
  build_by_default: with_tools.contains('freedreno'),
  install: false,
)

checkregs = executable(
  'checkregs',
  [ 'checkregs.c' ],
  include_directories: [
    inc_freedreno,
    inc_freedreno_rnn,
    inc_include,
    inc_src,
  ],
  gnu_symbol_visibility: 'hidden',
  dependencies: [],
  link_with: [
    libfreedreno_cffdec,
  ],
  build_by_default: with_tools.contains('freedreno'),
  install: false,
)

if with_tests
  crashdec_tests = [
    ['crash', ['-sf']],
    ['crash_prefetch', ['-sf']],
    ['prefetch-test', ['-sf']],
  ]
  foreach crashdec_test: crashdec_tests
    name = crashdec_test[0]
    args = crashdec_test[1]

    log = custom_target(name + '.log',
      output: name + '.log',
      command: [crashdec, args, files('../tests/traces/' + name + '.devcore')],
      capture: true,
      env: {'GALLIUM_DUMP_CPU': 'false'},
    )

    test('crashdec-' + name,
      diff,
      args: ['-u', files('../tests/reference/' + name + '.log'), log],
      suite: 'freedreno',
      workdir: dir_source_root
    )
  endforeach
  foreach gen : ['a6xx', 'a7xx', 'a8xx']
    test('checkregs-' + gen,
      checkregs,
      args: gen,
      suite: 'freedreno',
      workdir: dir_source_root,
    )
  endforeach
endif

pgmdump = executable(
  'pgmdump',
  'pgmdump.c',
  include_directories: [
    inc_freedreno,
    inc_include,
    inc_src,
  ],
  gnu_symbol_visibility: 'hidden',
  dependencies: [],
  link_with: [
    libfreedreno_cffdec,
    libfreedreno_io,
    libfreedreno_ir2,  # for disasm_a2xx
    libfreedreno_ir3,  # for disasm_a3xx
  ],
  build_by_default: with_tools.contains('freedreno'),
  install: false,
)
pgmdump2 = executable(
  'pgmdump2',
  'pgmdump2.c',
  include_directories: [
    inc_freedreno,
    inc_include,
    inc_src,
  ],
  gnu_symbol_visibility: 'hidden',
  dependencies: [],
  link_with: [
    libfreedreno_cffdec,
    libfreedreno_io,
    libfreedreno_ir2,  # for disasm_a2xx
    libfreedreno_ir3,  # for disasm_a3xx
  ],
  build_by_default: with_tools.contains('freedreno'),
  install: false,
)

if is_disabler(pgmdump)
  warning('libarchive not found, not building pgmdump')
endif
