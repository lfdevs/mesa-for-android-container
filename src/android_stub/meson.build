if with_android_stub
  _libmesa_android_stub = static_library(
    'mesa_android_stub',
    files('android_stub.cpp'),
    include_directories : inc_include,
    gnu_symbol_visibility : 'hidden',
  )

  dep_android = declare_dependency(
    link_with : _libmesa_android_stub,
  )
endif
