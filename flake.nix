{
  description = "A very basic flake";

  inputs = {
    nixpkgs.url = "nixpkgs/nixos-22.05"; # or "github:nixos/nixpkgs/nixpkgs-unstable"

    flake-utils = {
      url = "github:numtide/flake-utils";
      inputs.nixpkgs.follows = "nixpkgs";
    };

    appimage-runtime = {
      url = "github:AppImageCrafters/appimage-runtime";
      flake = false;
    };

    squashfuse = {
      url = "github:vasi/squashfuse";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, appimage-runtime, squashfuse }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = (import nixpkgs {
          inherit system;
        }).pkgsStatic;

        # nixpkgs has an old version so we need to package our own
        own-squashfuse = pkgs.stdenv.mkDerivation {
          name = "squashfuse";
          src = squashfuse;
          nativeBuildInputs = with pkgs; [ autoreconfHook libtool pkg-config ];
          buildInputs = with pkgs; [ lz4 xz zlib lzo zstd fuse ];
        };
      in
      rec {
        packages.apprun = pkgs.runCommandCC "AppRun" { } ''
          $CC ${./apprun.c} -o $out
        '';

        packages.runtime =
          let
            git-commit = "0000000";
          in
          pkgs.runCommandCC "runtime"
            {
              nativeBuildInputs = with pkgs; [ pkg-config ];
              buildInputs = with pkgs; [
                fuse
                own-squashfuse
                zstd
                zlib
                lzma
                lz4
                lzo
              ];
            } ''
            NIX_CFLAGS_COMPILE="$(pkg-config --cflags fuse) $NIX_CFLAGS_COMPILE"
            $CC ${./runtime.c} -o $out \
              -I./include -D_FILE_OFFSET_BITS=64 -DGIT_COMMIT='"${git-commit}"' \
              -lfuse -lsquashfuse_ll -lzstd -lz -llzma -llz4 -llzo2 \
              -T ${appimage-runtime}/src/data_sections.ld
            printf "AI2" > magic_bytes

            # Add AppImage Type 2 Magic Bytes to runtime
            dd if=magic_bytes of=$out bs=1 count=3 seek=8 conv=notrunc status=none
          '';

        mkappimage = { drv, entrypoint, name }:
          let
            arch = builtins.head (builtins.split "-" system);
            closure = pkgs.writeReferencesToFile drv;
            extras = [
              "AppRun f 555 0 0 cat ${packages.apprun}"
              "entrypoint s 555 0 0 ${entrypoint}"
            ];
            extra-args = pkgs.lib.concatMapStrings (x: " -p \"${x}\"") extras;
          in
          pkgs.runCommand "${name}-${arch}.AppImage"
            {
              nativeBuildInputs = with pkgs; [ squashfsTools ];
            } ''
            mksquashfs $(cat ${closure}) $out \
              -no-strip \
              -offset $(stat -L -c%s ${packages.runtime}) \
              -comp zstd \
              -all-root \
              -noappend \
              -b 1M \
              ${extra-args}
            dd if=${packages.runtime} of=$out conv=notrunc
            chmod +x $out
          '';

        bundlers.toAppImage = drv:
          let
            handler = {
              app = mkappimage {
                drv = drv.program;
                entrypoint = drv.program;
                name = "app";
              };
              derivation = mkappimage {
                drv = drv;
                # TODO figure this out from drv.meta.mainProgram or something
                entrypoint = "/bin/sh";
                name = drv.name;
              };
            };
          in
          assert pkgs.lib.assertMsg (handler ? ${drv.type}) "don't know how to make app image for type '${drv.type}'";
          handler.${drv.type};
      });
}
