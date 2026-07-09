libimgui_core = static_library(
  'imgui_core',
  files('imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp'),
  cpp_args : cpp.get_supported_arguments('-w', '-Wno-error'),
  install : false
)

libimgui_core_dep = declare_dependency(
  link_with : libimgui_core,
  include_directories : include_directories('.', is_system : true)
)

if with_tools.contains('gamma')
  libimgui_sdl3_vulkan = static_library(
    'imgui_sdl3_vulkan',
    files(
      'backends/imgui_impl_sdl3.cpp',
      'backends/imgui_impl_vulkan.cpp',
      'imgui.cpp',
      'imgui_draw.cpp',
      'imgui_tables.cpp',
      'imgui_widgets.cpp',
    ),
    cpp_args : cpp.get_supported_arguments('-w', '-Wno-error'),
    install : false
  )

  libimgui_sdl3_vulkan_dep = declare_dependency(
    link_with : libimgui_sdl3_vulkan,
    include_directories : include_directories('.', is_system : true)
  )
endif
