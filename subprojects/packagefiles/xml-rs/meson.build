project(
  'xml',
  'rust',
  version : '1.2.1',
  license : 'MIT',
  meson_version : '>= 1.7.0',
)

rc = meson.get_compiler('rust')

if rc.version().version_compare('< 1.71')
  error('Minimum rustc supported version is 1.71')
endif

lib = static_library(
  'xml',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021', 'warning_level=0'],
  rust_abi : 'rust',
  native : true,
)

dep_xml_rs = declare_dependency(
  link_with : [lib],
)
