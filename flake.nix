{
  description = "tunlet - sing-box Clash API controller and rule-set editor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        cleanSrc = pkgs.lib.cleanSourceWith {
          src = ./.;
          filter = path: type:
            let
              base = baseNameOf path;
            in
              !(base == "build" || base == "result");
        };
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "tunlet";
          version = "0.1.0";
          src = cleanSrc;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            libmaxminddb
            qt6.qtbase
            qt6.qtsvg
            yaml-cpp
          ];

          cmakeFlags = [
            "-DBUILD_TESTING=OFF"
          ];
        };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            ninja
            pkg-config
            gdb
            libmaxminddb
            qt6.qtbase
            qt6.qtsvg
            yaml-cpp
            catch2_3
          ];
        };
      });
}
