let
  nixpkgs = fetchGit { url = "https://github.com/NixOS/nixpkgs/"; rev = "f3f86d47220e2c2332d60f6bd0a89b76a628529d"; };
  pkgs = import nixpkgs { config = {}; overlays = []; };

in

pkgs.mkShell {
  packages = with pkgs; [
    cmake
    gcc11
    ninja
    python3
    valgrind
  ];
}
