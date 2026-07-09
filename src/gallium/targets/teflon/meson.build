libteflon = shared_library(
  'teflon',
  [ 'teflon_target.c' ],
  include_directories : [ inc_src, inc_util, inc_include, inc_gallium, inc_gallium_aux, inc_gallium_winsys, inc_gallium_drivers ],
  link_whole : [ libteflon_st ],
  link_with : [libpipe_loader_static, libws_null, libwsw, libswdri, libswkmsdri, libgallium ],
  gnu_symbol_visibility : 'hidden',
  link_args : ld_args_build_id,
  dependencies : [
    driver_etnaviv,
    driver_rocket,
    driver_ethosu,
    idep_nir,
    idep_mesautil,
  ],
  install : true,
)

if with_tests
  tensorflow_lite = shared_library(
    'tensorflow-lite-stub',
    [ 'tflite-stub.c' ],
    include_directories : [ inc_include ],
    link_args : [ '-Wl,-soname,libtensorflow-lite.so' ],
    install : false,
  )

  executable(
    'test_teflon',
    'test_teflon.cpp',
    'test_executor.cpp',
    'tflite-ref-ops.cc',
    dependencies : [ idep_mesautil, idep_gtest ],
    link_with : [ tensorflow_lite ],
    include_directories : [ inc_include, inc_util ],
  )
endif
