{
  description = "A basic AppImage bundler";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    flake-compat = {
      url = "github:nixos/flake-compat";
      flake = false;
    };

    # non-nix repo pins
    appimage-type2-runtime = {
      url = "github:appimage/type2-runtime";
      flake = false;
    };
    appimagecrafters-appimage-runtime = {
      url = "github:appimagecrafters/appimage-runtime";
      flake = false;
    };
  };

  outputs =
    inputs@{ nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = (import nixpkgs { inherit system; }).pkgsStatic;
      in
      rec {
        # runtimes are an executable that mount the squashfs part of the appimage and start AppRun
        appimage-runtimes = {
          appimagecrafters = pkgs.callPackage ./runtimes/appimagecrafters {
            src = inputs.appimagecrafters-appimage-runtime;
          };
          appimage-type2-runtime = pkgs.callPackage ./runtimes/appimage-type2-runtime {
            src = inputs.appimage-type2-runtime;
          };
        };

        # appruns contain an AppRun executable that does setup and launches entrypoint
        appimage-appruns = {
          userns-chroot = pkgs.callPackage ./appruns/userns-chroot { };
          bwrap = pkgs.callPackage ./appruns/bwrap { };
        };

        lib.mkAppImage = pkgs.callPackage ./mkAppImage.nix {
          default-runtime = appimage-runtimes.appimage-type2-runtime;
          default-apprun = appimage-appruns.userns-chroot;
        };

        bundlers.default =
          drv:
          if drv.type == "app" then
            lib.mkAppImage {
              program = drv.program;
            }
          else if drv.type == "derivation" then
            lib.mkAppImage {
              program = pkgs.lib.getExe drv;
            }
          else
            builtins.abort "don't know how to build ${drv.type}; only know app and derivation";

        # test all these work with $ nix flake check
        checks =
          let
            # use regular (non-static) nixpkgs
            pkgs = import nixpkgs { inherit system; };

            # checks to run on all combinations of runtime x apprun
            matrixChecks = {
              hello-is-static =
                runtime: apprun:
                let
                  hello-appimage = lib.mkAppImage {
                    inherit runtime apprun;
                    program = pkgs.lib.getExe pkgs.hello;
                  };
                in
                pkgs.runCommand "check-hello-is-static"
                  {
                    nativeBuildInputs = [ (pkgs.lib.getBin pkgs.stdenv.cc.libc) ];
                  }
                  ''
                    (! ldd ${hello-appimage} 2>&1) | grep "not a dynamic executable"
                    touch $out
                  '';
            };
          in
          # expand out matrixChecks
          appimage-runtimes
          // appimage-appruns
          // (pkgs.lib.pipe
            {
              runtime = pkgs.lib.attrsToList appimage-runtimes;
              apprun = pkgs.lib.attrsToList appimage-appruns;
              check = pkgs.lib.attrsToList matrixChecks;
            }
            [
              pkgs.lib.cartesianProduct
              (map (
                {
                  runtime,
                  apprun,
                  check,
                }:
                {
                  name = "${runtime.name}_${apprun.name}_${check.name}";
                  value = check.value runtime.value apprun.value;
                }
              ))
              pkgs.lib.listToAttrs
            ]
          )
          // {
          };

        formatter = (import nixpkgs { inherit system; }).nixfmt-tree;
      }
    );
}
