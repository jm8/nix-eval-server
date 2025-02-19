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
  dependency('protobuf', modules: ['protobuf::libprotobuf']),
  dependency('grpc++', method : 'pkg-config'),
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-expr'),
  dependency('nix-flake'),
  dependency('nix-fetchers'),
  dependency('nix-main'),
]

protoc = find_program('protoc')
grpc_cpp_plugin = find_program('grpc_cpp_plugin')

protoc_gen = generator(
  protoc,
  output : [
    '@BASENAME@.grpc.pb.cc',
    '@BASENAME@.grpc.pb.h',
    '@BASENAME@.pb.cc',
    '@BASENAME@.pb.h',
  ],
  arguments : [
    '--proto_path=@CURRENT_SOURCE_DIR@',
    '--cpp_out=@BUILD_DIR@',
    '--grpc_out=@BUILD_DIR@',
    '--plugin=protoc-gen-grpc=@0@'.format(grpc_cpp_plugin.full_path()),
    '@INPUT@',
  ]
)

schema_files = files('nix-eval-server.proto')
generated_files = protoc_gen.process(schema_files)

sources = files('main.cc') + generated_files

include_dirs = [include_directories('.')]

this_exe = executable(
  meson.project_name(),
  sources,
  dependencies: deps,
  include_directories: include_dirs,
  install: true,
  link_args: ['-lgrpc++_reflection']
)

meson.override_find_program('nix-eval-server', this_exe)
