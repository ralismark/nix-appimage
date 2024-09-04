{
  description = "A basic AppImage bundler";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    flake-compat = {
      url = "github:edolstra/flake-compat";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = (import nixpkgs { inherit system; }).pkgsStatic;
      in
      rec {
        packages.nixpkgs = pkgs;

        # runtimes are an executable that mount the squashfs part of the appimage and start AppRun
        packages.appimage-runtimes = {
          appimagecrafters = pkgs.callPackage ./runtimes/appimagecrafters { };
        };

        # appruns contain an AppRun executable that does setup and launches entrypoint
        packages.appimage-appruns = {
          userns-chroot = pkgs.callPackage ./appruns/userns-chroot { };
        };

        lib.mkAppImage = pkgs.callPackage ./mkAppImage.nix {
          mkappimage-runtime = packages.appimage-runtimes.appimagecrafters;
          mkappimage-apprun = packages.appimage-appruns.userns-chroot;
        };

        bundlers.default = drv:
          if drv.type == "app" then
            lib.mkAppImage
              {
                program = drv.program;
                squashfsArgs = if drv ? squashfsArgs then drv.squashfsArgs else [];
              }
          else if drv.type == "derivation" then
            lib.mkAppImage
              {
                program = pkgs.lib.getExe drv;
                squashfsArgs = if drv ? squashfsArgs then drv.squashfsArgs else [];
              }
          else builtins.abort "don't know how to build ${drv.type}; only know app and derivation";
      });
}
