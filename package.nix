{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  qt6,
  libmaxminddb,
  yaml-cpp,
  srcOverride ? null,
}:

let
  src =
    if srcOverride != null then
      srcOverride
    else
      lib.cleanSourceWith {
        src = ./.;
        filter = path: type:
          let
            base = baseNameOf path;
          in
            !(base == "build" || base == "result");
      };
in
stdenv.mkDerivation {
  pname = "tunlet";
  version = "0.1.0";
  inherit src;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    qt6.wrapQtAppsHook
  ];

  buildInputs = [
    libmaxminddb
    qt6.qtbase
    qt6.qtsvg
    yaml-cpp
  ];

  cmakeFlags = [
    "-DBUILD_TESTING=OFF"
  ];
}
