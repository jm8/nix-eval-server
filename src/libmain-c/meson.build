project('nix-main-c', 'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++2a',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'debug=true',
    'optimization=2',
    'errorlogs=true', # Please print logs for tests that fail
  ],
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

subdir('build-utils-meson/deps-lists')

configdata = configuration_data()

deps_private_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-main'),
]
deps_public_maybe_subproject = [
  dependency('nix-util-c'),
  dependency('nix-store-c'),
]
subdir('build-utils-meson/subprojects')

# TODO rename, because it will conflict with downstream projects
configdata.set_quoted('PACKAGE_VERSION', meson.project_version())

config_h = configure_file(
  configuration : configdata,
  output : 'config-main.h',
)

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.

  # From C++ libraries, only for internals
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  '-include', 'config-main.hh',

  # From C libraries, for our public, installed headers too
  '-include', 'config-util.h',
  '-include', 'config-store.h',
  '-include', 'config-main.h',
  language : 'cpp',
)

subdir('build-utils-meson/common')

sources = files(
  'nix_api_main.cc',
)

include_dirs = [include_directories('.')]

headers = [config_h] + files(
  'nix_api_main.h',
)

subdir('build-utils-meson/export-all-symbols')
subdir('build-utils-meson/windows-version')

this_library = library(
  'nixmainc',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args: linker_export_flags,
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

libraries_private = []

subdir('build-utils-meson/export')
