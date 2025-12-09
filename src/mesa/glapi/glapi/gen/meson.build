# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

glapi_mapi_tmp_h = custom_target(
  'glapi_mapi_tmp.h',
  input : ['../../mapi_abi.py', 'gl_and_es_API.xml', '../../../../glx/libgl-symbols.txt'],
  output : 'glapi_mapi_tmp.h',
  command : [prog_python, '@INPUT0@', '--printer', 'glapi', '--gl_symbols', '@INPUT2@', '@INPUT1@'],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

glapi_gentable_c = custom_target(
  'glapi_gentable.c',
  input : ['gl_gentable.py', 'gl_and_es_API.xml'],
  output : 'glapi_gentable.c',
  command : [prog_python, '@INPUT0@', '-f', '@INPUT1@'],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

main_enums_c = custom_target(
  'enums.c',
  input : ['gl_enums.py', files('../registry/gl.xml')],
  output : 'enums.c',
  command : [prog_python, '@INPUT0@', '-f', '@INPUT1@'],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

main_api_exec_c = custom_target(
  'api_exec_init.c',
  input : ['api_exec_init.py', 'gl_and_es_API.xml'],
  output : 'api_exec_init.c',
  command : [prog_python, '@INPUT0@', '-f', '@INPUT1@'],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

main_api_exec_decl_h = custom_target(
  'api_exec_decl.h',
  input : ['api_exec_decl_h.py', 'gl_and_es_API.xml'],
  output : 'api_exec_decl.h',
  command : [prog_python, '@INPUT0@', '-f', '@INPUT1@'],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

main_api_save_init_h = custom_target(
  'api_save_init.h',
  input : ['api_save_init_h.py', 'gl_and_es_API.xml'],
  output : 'api_save_init.h',
  command : [prog_python, '@INPUT0@', '-f', '@INPUT1@'],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

main_api_save_h = custom_target(
  'api_save.h',
  input : ['api_save_h.py', 'gl_and_es_API.xml'],
  output : 'api_save.h',
  command : [prog_python, '@INPUT0@', '-f', '@INPUT1@'],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

main_api_beginend_init_h = custom_target(
  'api_beginend_init.h',
  input : ['api_beginend_init_h.py', 'gl_and_es_API.xml'],
  output : 'api_beginend_init.h',
  command : [prog_python, '@INPUT0@', '-f', '@INPUT1@'],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

main_api_hw_select_init_h = custom_target(
  'api_hw_select_init.h',
  input : ['api_hw_select_init_h.py', 'gl_API.xml'],
  output : 'api_hw_select_init.h',
  command : [prog_python, '@INPUT0@', '-f', '@INPUT1@'],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

main_dispatch_h = custom_target(
  'dispatch.h',
  input : ['gl_table.py', 'gl_and_es_API.xml'],
  output : 'dispatch.h',
  command : [prog_python, '@INPUT0@', '-f', '@INPUT1@', '-m', 'dispatch'],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

idep_dispatch_h = declare_dependency(sources : [main_dispatch_h])

main_marshal_generated_h = custom_target(
  'marshal_generated.h',
  input : ['marshal_generated_h.py', 'gl_and_es_API.xml'],
  output : 'marshal_generated.h',
  command : [prog_python, '@INPUT0@', '@INPUT1@', sizeof_pointer],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

main_unmarshal_table_c = custom_target(
  'unmarshal_table.c',
  input : ['unmarshal_table_c.py', 'gl_and_es_API.xml'],
  output : 'unmarshal_table.c',
  command : [prog_python, '@INPUT0@', '@INPUT1@', sizeof_pointer],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

main_marshal_generated_c = []
foreach x : ['0', '1', '2', '3', '4', '5', '6', '7']
  main_marshal_generated_c += custom_target(
    'marshal_generated' + x + '.c',
    input : ['marshal_generated_c.py', 'gl_and_es_API.xml'],
    output : 'marshal_generated' + x + '.c',
    command : [prog_python, '@INPUT0@', '@INPUT1@', x, '8', sizeof_pointer],
    depend_files : glapi_xml_py_deps,
    capture : true,
  )
endforeach

glx_generated = []

foreach x : [['indirect.c', 'proto'], ['indirect.h', 'init_h'], ['indirect_init.c', 'init_c']]
  glx_generated += custom_target(
    x[0],
    input : ['glX_proto_send.py', 'gl_API.xml', '../../../../glx/libgl-symbols.txt'],
    output : x[0],
    command : [prog_python, '@INPUT0@', '-f', '@INPUT1@', '-m', x[1], '-s', '@INPUT2@'],
    depend_files : glapi_xml_py_deps,
    capture : true,
  )
endforeach

foreach x : [['indirect_size.h', ['-m', 'size_h', '--header-tag', '_INDIRECT_SIZE_H_']],
             ['indirect_size.c', ['-m', 'size_c']]]
  glx_generated += custom_target(
    x[0],
    input : ['glX_proto_size.py', 'gl_API.xml'],
    output : x[0],
    command : [prog_python, '@INPUT0@', '-f', '@INPUT1@', '--only-set', x[1]],
    depend_files : glapi_xml_py_deps,
    capture : true,
  )
endforeach
glx_indirect_size_h = glx_generated[3]
