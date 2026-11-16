{
  fuse3,
  lz4,
  lzo,
  pkg-config,
  squashfuse,
  stdenv,
  xz,
  zlib,
  zstd,

  # from flake input
  src,
}:

let
  fuse3' = fuse3.overrideAttrs (old: {
    patches = (old.patches or [ ]) ++ [
      # this doesn't work -- causes fuse: failed to exec fusermount: Permission denied
      # "${src}/patches/libfuse/mount.c.diff"
    ];
  });

  squashfuse' = squashfuse.overrideAttrs (old: {
    postInstall = (old.postInstall or "") + ''
      cp *.h -t $out/include/squashfuse/
    '';
  });
in
stdenv.mkDerivation {
  pname = "appimage-type2-runtime";
  version = "0-unstable-${builtins.substring 0 4 src.lastModifiedDate}-${
    builtins.substring 4 2 src.lastModifiedDate
  }-${builtins.substring 6 2 src.lastModifiedDate}-${src.shortRev}";

  inherit src;

  nativeBuildInputs = [ pkg-config ];
  buildInputs = [
    fuse3'
    squashfuse'
    zstd
    zlib
    xz
    lz4
    lzo
  ];

  configurePhase = ''
    $PKG_CONFIG --cflags fuse3 > cflags
  '';

  buildPhase = ''
    $CC src/runtime/runtime.c -o $out \
      -D_FILE_OFFSET_BITS=64 -DGIT_COMMIT='"0000000"' \
      $(cat cflags) \
      -std=gnu99 -Os -ffunction-sections -fdata-sections -Wl,--gc-sections -static -Wall -Werror \
      -lsquashfuse -lsquashfuse_ll -lfuse3 -lzstd -lz -llzma -llz4 -llzo2 \
      -T src/runtime/data_sections.ld

    # Add AppImage Type 2 Magic Bytes to runtime
    printf %b '\x41\x49\x02' > magic_bytes
    dd if=magic_bytes of=$out bs=1 count=3 seek=8 conv=notrunc status=none
  '';

  dontFixup = true;
}
