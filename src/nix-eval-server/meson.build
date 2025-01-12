project(
  'nix-eval-server',
  'cpp',
  default_options: [
    'cpp_std=c++2a',
    'warning_level=1',
    'debug=true',
    'optimization=2',
    'errorlogs=true',
    'localstatedir=/nix/var',
  ],
  meson_version: '>= 1.1',
  license: 'LGPL-2.1-or-later',
)

add_project_arguments(
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  '-include', 'config-expr.hh',
  '-include', 'config-main.hh',
  language : 'cpp',
)

cxx = meson.get_compiler('cpp')

deps = [
  dependency('capnp'),
  dependency('capnp-rpc'),
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-expr'),
  dependency('nix-flake'),
  dependency('nix-fetchers'),
  dependency('nix-main'),
  # dependency('nix-cmd'),
]

find_program('capnp')
capnp_shell_script = find_program('capnproto_build.sh')
capnp_generator = generator(
  capnp_shell_script,
  output: ['@BASENAME@.capnp.c++', '@BASENAME@.capnp.h'],
  arguments: ['@INPUT@', '@OUTPUT0@'],
)
schema_files = files('nix-eval-server.capnp')
generated_files = capnp_generator.process(schema_files)

sources = files('main.cc') + generated_files

include_dirs = [include_directories('.')]

this_exe = executable(
  meson.project_name(),
  sources,
  dependencies: deps,
  include_directories: include_dirs,
  install: true,
)

meson.override_find_program('nix-eval-server', this_exe)
