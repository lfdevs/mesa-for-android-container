nv_classes = [
  'cl85b5',
  'cl902d',
  'cl9039',
  'cl906f',
  'cl9097',
  'cl90b5',
  'cl90c0',
  'cl91c0',
  'cl9297',
  'cla040',
  'cla0b5',
  'cla06f',
  'cla097',
  'cla140',
  'cla16f',
  'cla1c0',
  'cla197',
  'cla26f',
  'cla297',
  'clb06f',
  'clb097',
  'clb0b5',
  'clb0c0',
  'clb197',
  'clb1c0',
  'clc06f',
  'clc097',
  'clc197',
  'clc1b5',
  'clc1c0',
  'cla0c0',
  'clc0b5',
  'clc0c0',
  'clc36f',
  'clc397',
  'clc3b5',
  'clc3c0',
  'clc46f',
  'clc56f',
  'clc597',
  'clc5b0',
  'clc5b5',
  'clc5c0',
  'clc697',
  'clc6b5',
  'clc6c0',
  'clc797',
  'clc7b0',
  'clc7b5',
  'clc7c0',
  'clc86f',
  'clc8b5',
  'clc96f',
  'clc997',
  'clc9b0',
  'clc9b5',
  'clc9c0',
  'clca6f',
  'clcab5',
  'clcb97',
  'clcbc0',
  'clcd97',
  'clcdc0',
  'clcec0',
  'clce97',
]

hwref_gens = [
  'maxwell/gm107',
  'pascal/gp100',
  'hopper/gh100',
  'turing/tu102',
]

flat_nv_classes = ''

cl_generated = []
foreach cl : nv_classes
  prev_cl_h = []
  prev_cl_args = []
  foreach prev_cl : nv_classes
    if prev_cl == cl
      break
    endif

    if prev_cl.substring(-2) == cl.substring(-2)
      prev_cl_h = 'nvidia/classes/'+prev_cl+'.h'
      prev_cl_args = ['--prev-in-h', '@INPUT2@']
    endif
  endforeach

  flat_nv_classes = flat_nv_classes + cl + ' '
  cl_generated += custom_target(
    cl + '.h',
    input : ['class_parser.py', 'nvidia/classes/'+cl+'.h', prev_cl_h, 'util.py'],
    output : ['nv_push_'+cl+'.h', 'nv_push_'+cl+'.c'],
    command : [prog_python, '@INPUT0@', '--in-h', '@INPUT1@', prev_cl_args,
               '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@'],
  )
endforeach

nv_push_class_dump_h = custom_target(
  'nv_push_class_dump.h',
  input : ['nv_push_class_dump_h.py', 'nvidia/classes/'+cl+'.h', 'util.py'],
  output : ['nv_push_class_dump.h'],
  command : [prog_python, '@INPUT0@', '--classes', flat_nv_classes, '--out-h', '@OUTPUT0@'],
)

_libnvidia_headers = static_library(
  'nvidia_headers_c',
  ['nv_push.c', cl_generated, nv_push_class_dump_h],
  include_directories : ['.', 'nvidia/classes', inc_include, inc_src],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
)

idep_nvidia_headers = declare_dependency(
  include_directories : include_directories('.', 'nvidia/classes'),
  sources : cl_generated,
  link_with : _libnvidia_headers,
)

executable(
  'nv_push_dump',
  'nv_push_dump.c',
  dependencies : [ idep_mesautil, idep_nvidia_headers ],
  build_by_default : with_tools.contains('nouveau'),
  install : with_tools.contains('nouveau'),
)

nouveau_util_py = files('util.py')
nouveau_util_py_path = meson.current_source_dir()

# Only generate Rust bindings for NVK
if with_nouveau_vk
  cl_rs_generated = []
  foreach cl : nv_classes
    cl_rs_generated += custom_target(
      'nvh_classes_' + cl + '.rs',
      input : ['class_parser.py', 'nvidia/classes/'+cl+'.h', 'util.py'],
      output : ['nvh_classes_'+cl+'.rs'],
      command : [prog_python, '@INPUT0@', '--in-h', '@INPUT1@',
                 '--out-rs', '@OUTPUT0@'],
    )

    prev_cl_h = []
    prev_cl_args = []
    foreach prev_cl : nv_classes
      if prev_cl == cl
        break
      endif

      if prev_cl.substring(-2) == cl.substring(-2)
        prev_cl_h = 'nvidia/classes/'+prev_cl+'.h'
        prev_cl_args = ['--prev-in-h', '@INPUT2@']
      endif
    endforeach

    cl_rs_generated += custom_target(
      'nvh_' + cl + '_mthd.rs',
      input : ['class_parser.py', 'nvidia/classes/'+cl+'.h', prev_cl_h, 'util.py'],
      output : ['nvh_classes_'+cl+'_mthd.rs'],
      command : [prog_python, '@INPUT0@', '--in-h', '@INPUT1@',
                 prev_cl_args, '--out-rs-mthd', '@OUTPUT0@'],
    )

    if cl.endswith('c0') and fs.is_file('nvidia/classes/'+cl+'qmd.h')
      cl_rs_generated += custom_target(
        'nvh_classes_' + cl + '_qmd.rs',
        input : ['struct_parser.py', 'nvidia/classes/'+cl+'qmd.h', 'util.py'],
        output : ['nvh_classes_'+cl+'_qmd.rs'],
        command : [prog_python, '@INPUT0@', '--in-h', '@INPUT1@',
                   '--out-rs', '@OUTPUT0@']
      )
    endif

    if cl.endswith('97')
      cl_rs_generated += custom_target(
        'nvh_classes_' + cl + '_tex.rs',
        input : ['struct_parser.py', 'nvidia/classes/'+cl+'tex.h', 'util.py'],
        output : ['nvh_classes_'+cl+'_tex.rs'],
        command : [prog_python, '@INPUT0@', '--in-h', '@INPUT1@',
                   '--out-rs', '@OUTPUT0@']
      )
    endif

    if fs.is_file(f'nvidia/classes/@cl@sph.h')
      cl_rs_generated += custom_target(
        input : ['struct_parser.py', f'nvidia/classes/@cl@sph.h', 'util.py'],
        output : [f'nvh_classes_@cl@_sph.rs'],
        command : [prog_python, '@INPUT0@', '--in-h', '@INPUT1@',
                   '--out-rs', '@OUTPUT0@']
      )
    endif
  endforeach

  foreach family_gen : hwref_gens
    family = fs.parent(family_gen)
    gen = fs.name(family_gen)
    cl_rs_generated += rust.bindgen(
      input : ['nvidia/hwref/'+family+'/'+gen+'/dev_mmu.h'],
      output : 'nvh_hwref_'+gen+'_mmu.rs',
      args : ['--allowlist-var', 'NV_MMU_.*'],
    )
  endforeach

  _nvidia_headers_lib_rs = custom_target(
    'lib.rs',
    input : ['lib_rs_gen.py', 'util.py'],
    output : ['lib.rs'],
    command : [prog_python, '@INPUT0@', '--out-rs', '@OUTPUT0@',
               cl_rs_generated]
  )

  _nvidia_headers_rs = static_library(
    'nvidia_headers',
    _nvidia_headers_lib_rs,
    gnu_symbol_visibility : 'hidden',
    rust_abi : 'rust',
  )

  idep_nvidia_headers_rs = declare_dependency(
    link_with : _nvidia_headers_rs,
  )

  _libnv_push_rs = static_library(
    'nv_push_rs',
    files('nv_push_rs/lib.rs'),
    gnu_symbol_visibility: 'hidden',
    rust_abi: 'rust',
    dependencies: [idep_nvidia_headers_rs],
  )

  idep_nv_push_rs = declare_dependency(
    link_with : _libnv_push_rs,
  )
endif
