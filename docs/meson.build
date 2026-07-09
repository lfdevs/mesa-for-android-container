get_option('html-docs').require(
  run_command(prog_python, '-c',
  '''
try:
  from packaging.version import Version
except:
  from distutils.version import StrictVersion as Version
import hawkmoth
assert Version(hawkmoth.__version__) >= Version("0.16.0")
  ''', check: false).returncode() == 0,
  error_message: 'hawkmoth >= 0.16.0 required'
)

sphinx_args = []
if get_option('werror')
  sphinx_args += '-W'
endif

radv_drirc_rst = custom_target(
  'radv_drirc_rst',
  input  : [
    join_paths(dir_source_root, 'src/amd/vulkan/radv_drirc_gen.py'),
    drirc_gen,
    join_paths(dir_source_root, 'src/amd/vulkan/00-radv-defaults.conf'),
  ],
  output : 'radv_drirc.rst',
  command : [
    prog_python, '@INPUT0@',
    '--import-path', join_paths(dir_source_root, 'src/util'),
    '--rst', '@OUTPUT@',
    '--validate', '@INPUT2@',
  ],
)

html_docs = custom_target(
  'html',
  depfile: 'docs.d',
  depend_files: files('conf.py'),
  command: [sphinx, '-b', 'html', '-q',
            '-Ddepfile=@DEPFILE@',
            sphinx_args,
            meson.current_source_dir(), '@OUTPUT@'],
  env: [
    'MESA_BUILD_ROOT=' + meson.project_build_root()
  ],
  depends: [
    u_format_gen_h,
    radv_drirc_rst,
  ],
  output: 'html',
  build_by_default: get_option('html-docs').enabled(),
)

html_docs_path = get_option('html-docs-path')
if html_docs_path == ''
  html_docs_path = join_paths(get_option('datadir'), 'doc', 'mesa')
endif

install_subdir(html_docs.full_path(),
  install_dir: html_docs_path,
  exclude_files: ['.buildinfo'],
  exclude_directories: ['.doctrees'],
  strip_directory: true
)
