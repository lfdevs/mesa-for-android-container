project(
    'perfetto-sdk',
    ['cpp'],
)

cpp = meson.get_compiler('cpp')

deps_perfetto = [dependency('threads')]

if host_machine.system() == 'android'
  deps_perfetto += cpp.find_library('log')
endif

lib_perfetto = static_library(
    'perfetto',
    sources: 'perfetto.cc',
    dependencies: deps_perfetto,
    install: false,
    override_options : ['cpp_std=c++17']
)

inc_perfetto = include_directories('.')

dep_perfetto_sdk = declare_dependency(
    link_with: lib_perfetto,
    include_directories: inc_perfetto,
    dependencies: deps_perfetto,
)
