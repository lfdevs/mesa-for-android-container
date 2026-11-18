if with_android_stub
  stub_libs = []
  lib_names = ['log', 'nativewindow', 'sync']

  if with_libbacktrace
    lib_names += ['backtrace']
  endif

  foreach lib : lib_names
    stub_libs += shared_library(
      lib,
      files(lib + '_stub.cpp'),
      include_directories : inc_include,
      install : false,
    )
  endforeach

  stub_static_libs = [
    static_library(
      'hardware',
      files('hardware_stub.cpp'),
      include_directories : inc_include,
      install : false,
    )
  ]

  dep_android = declare_dependency(
    link_with : stub_libs + stub_static_libs,
  )
endif
