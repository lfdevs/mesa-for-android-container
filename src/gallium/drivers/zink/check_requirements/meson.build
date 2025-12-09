gen_profiles_solution = subproject('Vulkan-Profiles').get_variable('gen_profiles_solution')

vulkan_profiles_hpp = custom_target(
  'vulkan_profiles.hpp',
  output : 'vulkan_profiles.hpp',
  input : [
    gen_profiles_solution,
    '../VP_ZINK_requirements.json',
    vk_api_xml,
  ],
  command : [
    prog_python,
    '@INPUT0@',

    # The program awkwardly demands the input folder and file name as
    # different arguments, so cut off the trailing file name using '/..'
    '--input', '@INPUT1@/..',
    '--input-filenames', 'VP_ZINK_requirements.json',

    # Output is similar, but the file name is implicit
    '--output-library-inc', '@OUTPUT@/..',

    '--registry', '@INPUT2@',
    '--config', 'debug',
  ],
)

vulkan_dep = dependency('vulkan')

executable('zink_check_requirements',
  [vulkan_profiles_hpp, 'zink_check_requirements.cpp'],
  dependencies: [
    vulkan_dep,
  ],
  include_directories : [inc_include],
  install : false,
)
