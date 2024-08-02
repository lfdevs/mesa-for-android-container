# Copyright © 2018 Intel Corporation
# SPDX-License-Identifier: MIT

foreach t : ['pipe_barrier_test', 'u_cache_test', 'u_half_test',
             'translate_test', 'u_prim_verts_test']
  exe = executable(
    t,
    '@0@.c'.format(t),
    include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
    link_with : libgallium,
    dependencies : idep_mesautil,
    install : false,
  )
  if (t == 'translate_test') # translate_test have parameters.
    # FIXME: translate_test default|generic are failing
    # test('translate_test default', exe, args : [ 'default' ])
    # test('translate_test generic', exe, args : [ 'generic' ])
    if ['x86', 'x86_64'].contains(host_machine.cpu_family())
      foreach arg : ['x86', 'nosse', 'sse', 'sse2', 'sse3', 'sse4.1']
        test('translate_test ' + arg, exe, args : [ arg ])
      endforeach
    endif
  elif t != 'u_cache_test' # u_cache_test is slow
    test(t, exe, suite: 'gallium',
         should_fail : meson.get_external_property('xfail', '').contains(t),
    )
  endif
endforeach
