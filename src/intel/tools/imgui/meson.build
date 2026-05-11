libintel_imgui_deps = [
  dependency('epoxy'),
  dependency('glfw3'),
  dependency('gl'),
]

libintel_imgui = static_library(
  'intel_imgui',
  files('imgui_impl_glfw.cpp', 'imgui_impl_opengl3.cpp', 'intel_imgui.cpp'),
  include_directories: inc_src,
  dependencies : [libintel_imgui_deps, libimgui_core_dep],
  cpp_args : ['-w'],
  install: false
)

libintel_imgui_dep = declare_dependency(
  link_with: libintel_imgui,
  dependencies: [libimgui_core_dep, libintel_imgui_deps],
  include_directories: include_directories('.')
)
