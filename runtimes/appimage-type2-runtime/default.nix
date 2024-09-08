{ fetchFromGitHub
, stdenv
, fuse3
, pkg-config
, squashfuse
, zstd
, zlib
, lzma
, lz4
, lzo
}:

let
  src = fetchFromGitHub {
    owner = "AppImage";
    repo = "type2-runtime";
    rev = "01164bfcbc8dd2bd0d7e3706f97035108d6b91ba";
    hash = "sha256-GR3LMuWMSafQmc2RQyveue3sq+HYBtl+VkcZVYMS0CI=";
  };

  fuse3' = fuse3.overrideAttrs (old: {
    patches = (old.patches or []) ++ [
      # this doesn't work -- causes fuse: failed to exec fusermount: Permission denied
      # "${src}/patches/libfuse/mount.c.diff"
    ];
  });

  squashfuse' = (squashfuse.override {
    fuse = fuse3';
  }).overrideAttrs (old: {
    postInstall = (old.postInstall or "") + ''
      cp *.h -t $out/include/squashfuse/
    '';
  });
in
stdenv.mkDerivation {
  pname = "appimage-type2-runtime";
  version = "unstable-2024-08-17";

  inherit src;

  nativeBuildInputs = [ pkg-config ];
  buildInputs = [
    fuse3'
    squashfuse'
    zstd
    zlib
    lzma
    lz4
    lzo
  ];

  patchPhase = ''
    sed -e '/sqfs_usage/s/);/, true\0/' -i src/runtime/runtime.c
  '';

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
