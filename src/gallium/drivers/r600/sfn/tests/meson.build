
r600_test_lib = static_library('r600_test', 'sfn_test_shaders.cpp',
   cpp_args: r600_cpp_args,
   override_options: ['cpp_std=c++17'],
   include_directories : [ inc_src, inc_mapi, inc_mesa, inc_include,
                           inc_compiler, inc_gallium, inc_gallium_aux, inc_amd_common,
                           inc_gallium_drivers, ],
   dependencies : [idep_gtest, dep_thread, dep_llvm, idep_nir,
                     idep_nir_headers],

)

r600_test_dep = declare_dependency(
    include_directories : [ inc_src, inc_mapi, inc_mesa, inc_include,
                            inc_compiler, inc_gallium, inc_gallium_aux, inc_amd_common,
                            inc_gallium_drivers, ],
    link_with : [ libr600, libmesa, libgalliumvl,  libgallium, libradeonwinsys, r600_test_lib],
    dependencies : [idep_gtest, dep_thread, dep_llvm, idep_nir,
                    idep_nir_headers]
)



if with_tests
   foreach t : ['valuefactory', 'value', 'instr', 'instrfromstring', 'liverange',
                'optimizer', 'shaderfromstring' ]
   test(
       t,
       executable('test-@0@-r600-sfn'.format(t),
                  ['sfn_@0@_test.cpp'.format(t)],
                  dependencies : [r600_test_dep],
                  override_options: ['cpp_std=c++17']
                  ),
       suite : ['r600']
    )
    endforeach
endif
