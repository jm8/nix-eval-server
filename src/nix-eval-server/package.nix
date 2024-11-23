{ lib
, stdenv
, mkMesonExecutable

, nix-store
, nix-expr
, nix-main
, nix-flake

, capnp

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
    ./capnproto_build.sh
  ] ++ lib.concatMap
    (dir: [
      (fileset.fileFilter (file: file.hasExt "cc") dir)
      (fileset.fileFilter (file: file.hasExt "hh") dir)
      (fileset.fileFilter (file: file.hasExt "md") dir)
      (fileset.fileFilter (file: file.hasExt "capnp") dir)
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
    capnp
  ];

  nativeBuildInputs = [
    capnp
  ];

  preConfigure =
    # "Inline" .version so it's not a symlink, and includes the suffix.
    # Do the meson utils, without modification.
    ''
      chmod u+w ./.version
      echo ${version} > ../../../.version
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
