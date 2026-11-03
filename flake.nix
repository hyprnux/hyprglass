{
  description = "Hyprglass Hyprland plugin";

  nixConfig = {
    extra-substituters = [
      "https://hyprland.cachix.org"
    ];
    extra-trusted-public-keys = [
      "hyprland.cachix.org-1:a7pgxzMz7+chwVL3/pzj6jIBMioiJM7ypFP8PwtkuGc="
    ];
  };

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    hyprland.url = "github:hyprwm/Hyprland";
  };

  outputs =
    {
      self,
      nixpkgs,
      hyprland,
    }:
    let

      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forEachSystem = nixpkgs.lib.genAttrs systems;

      baseVersion = "1.1.0";
      version =
        if (self ? shortRev) then
          "${baseVersion}+unstable-${self.shortRev}"

        else if (self ? dirtyShortRev) then
          "${baseVersion}+unstable-${self.dirtyShortRev}"

        else
          baseVersion;

    in
    {
      packages = forEachSystem (
        system:
        let

          pkgs = nixpkgs.legacyPackages.${system};

          hyprlandPkg =
            if (hyprland ? packages.${system}.hyprland) then
              hyprland.packages.${system}.hyprland

            else if (hyprland ? packages.${system}.default) then
              hyprland.packages.${system}.default

            else if (hyprland ? packages.${system}.hyprland-unwrapped) then
              hyprland.packages.${system}.hyprland-unwrapped

            else
              pkgs.hyprland;

        in
        {
          default = pkgs.callPackage ./nix/package.nix {
            hyprland = hyprlandPkg;
            inherit version;
          };

          hyprglass = self.packages.${system}.default;

          hyprglass-nixpkgs = pkgs.callPackage ./nix/package.nix {
            hyprland = pkgs.hyprland;
            inherit version;
          };
        }
      );

      overlays.default = final: prev: {
        hyprlandPlugins = (prev.hyprlandPlugins or { }) // {
          hyprglass = final.callPackage ./nix/package.nix {
            hyprland =
              let

                system = final.stdenv.hostPlatform.system;

              in
              if (hyprland ? packages.${system}.hyprland) then
                hyprland.packages.${system}.hyprland

              else if (hyprland ? packages.${system}.default) then
                hyprland.packages.${system}.default

              else if (hyprland ? packages.${system}.hyprland-unwrapped) then
                hyprland.packages.${system}.hyprland-unwrapped

              else
                final.hyprland or prev.hyprland;

            inherit version;
          };
        };
      };
    };
}
