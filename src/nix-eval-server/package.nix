{ lib
, stdenv
, mkMesonExecutable

, nix-store
, nix-expr
, nix-main
, nix-flake

, protobuf
, grpc

# Configuration Options

, version
}:

let
  inherit (lib) fileset;
in

mkMesonExecutable (finalAttrs: {
  pname = "nix";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions ([
    ../../build-utils-meson
    ../../.version
    ./.version
    ./meson.build
  ] ++ lib.concatMap
    (dir: [
      (fileset.fileFilter (file: file.hasExt "cc") dir)
      (fileset.fileFilter (file: file.hasExt "hh") dir)
      (fileset.fileFilter (file: file.hasExt "md") dir)
      (fileset.fileFilter (file: file.hasExt "proto") dir)
    ])
    [
      ./.
    ]
  );

  buildInputs = [
    nix-store
    nix-expr
    nix-main
    nix-flake
    protobuf
    grpc
  ];

  nativeBuildInputs = [
    protobuf
    grpc
  ];

  preConfigure =
    # "Inline" .version so it's not a symlink, and includes the suffix.
    # Do the meson utils, without modification.
    ''
      chmod u+w ./.version
      echo ${version} > ../../../.version
    '';

  postInstall = 
    ''
      mkdir -p $out/share
      cp $src/src/nix-eval-server/nix-eval-server.proto $out/share
    '';

  mesonFlags = [
  ];

  env = lib.optionalAttrs (stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux")) {
    LDFLAGS = "-fuse-ld=gold";
  };

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  };

})
