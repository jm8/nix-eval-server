project('nix-store-tests', 'cpp',
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

deps_private_maybe_subproject = [
  dependency('nix-store'),
  dependency('nix-store-c'),
  dependency('nix-store-test-support'),
]
deps_public_maybe_subproject = [
]
subdir('build-utils-meson/subprojects')

subdir('build-utils-meson/export-all-symbols')
subdir('build-utils-meson/windows-version')

sqlite = dependency('sqlite3', 'sqlite', version : '>=3.6.19')
deps_private += sqlite

rapidcheck = dependency('rapidcheck')
deps_private += rapidcheck

gtest = dependency('gtest', main : true)
deps_private += gtest

gtest = dependency('gmock')
deps_private += gtest

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  '-include', 'config-util.h',
  '-include', 'config-store.h',
  language : 'cpp',
)

subdir('build-utils-meson/common')

sources = files(
  'common-protocol.cc',
  'content-address.cc',
  'derivation-advanced-attrs.cc',
  'derivation.cc',
  'derived-path.cc',
  'downstream-placeholder.cc',
  'http-binary-cache-store.cc',
  'legacy-ssh-store.cc',
  'local-binary-cache-store.cc',
  'local-overlay-store.cc',
  'local-store.cc',
  'machines.cc',
  'nar-info-disk-cache.cc',
  'nar-info.cc',
  'nix_api_store.cc',
  'outputs-spec.cc',
  'path-info.cc',
  'path.cc',
  'references.cc',
  's3-binary-cache-store.cc',
  'serve-protocol.cc',
  'ssh-store.cc',
  'store-reference.cc',
  'uds-remote-store.cc',
  'worker-protocol.cc',
)

include_dirs = [include_directories('.')]


this_exe = executable(
  meson.project_name(),
  sources,
  dependencies : deps_private_subproject + deps_private + deps_other,
  include_directories : include_dirs,
  # TODO: -lrapidcheck, see ../libutil-support/build.meson
  link_args: linker_export_flags + ['-lrapidcheck'],
  # get main from gtest
  install : true,
)

test(
  meson.project_name(),
  this_exe,
  env : {
    '_NIX_TEST_UNIT_DATA': meson.current_source_dir() / 'data',
  },
  protocol : 'gtest',
)
