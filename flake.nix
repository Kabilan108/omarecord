{
  description = "Niri-native screen recording selector and annotation overlay";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;
      forAllSystems = lib.genAttrs [ "x86_64-linux" ];
      packageFor =
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          layerShellPluginDir = "${pkgs.kdePackages.layer-shell-qt}/lib/qt-6/plugins";
          runtimePackages = with pkgs; [ grim niri wl-clipboard ];
        in
        pkgs.stdenv.mkDerivation {
          pname = "omarecord";
          version = "0.1.0";
          src = lib.fileset.toSource {
            root = ./.;
            fileset = lib.fileset.unions [
              ./assets
              ./CMakeLists.txt
              ./LICENSE
              ./src
              ./tests
            ];
          };

          strictDeps = true;
          nativeBuildInputs = with pkgs; [ cmake ninja qt6.wrapQtAppsHook ];
          buildInputs = with pkgs; [ kdePackages.layer-shell-qt libglvnd qt6.qtbase ];

          cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];
          doCheck = true;
          checkPhase = ''
            runHook preCheck
            export HOME="$TMPDIR"
            export XDG_RUNTIME_DIR="$TMPDIR/runtime"
            export FONTCONFIG_FILE="${pkgs.fontconfig.out}/etc/fonts/fonts.conf"
            mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
            QT_QPA_PLATFORM=offscreen ./omarecord-smoke
            runHook postCheck
          '';

          qtWrapperArgs = [ "--prefix PATH : ${lib.makeBinPath runtimePackages}" ];

          postFixup = ''
            test -e "${layerShellPluginDir}/wayland-shell-integration/liblayer-shell.so"
            grep -aFq "${lib.makeBinPath runtimePackages}" "$out/bin/omarecord"
            QT_QPA_PLATFORM=offscreen "$out/bin/omarecord" --version
          '';

          meta = {
            description = "Niri-native screen recording selector and annotation overlay";
            homepage = "https://github.com/Kabilan108/omarecord";
            license = lib.licenses.mit;
            mainProgram = "omarecord";
            platforms = lib.platforms.linux;
          };
        };
    in
    {
      packages = forAllSystems (system: rec {
        omarecord = packageFor system;
        default = omarecord;
      });

      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.omarecord ];
            packages = with pkgs; [ clang-tools gpu-screen-recorder ffmpeg grim nixfmt ];
          };
        }
      );

      checks = forAllSystems (system: { inherit (self.packages.${system}) omarecord; });
    };
}
