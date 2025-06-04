{ fetchFromGitHub
, fuse3
, lz4
, xz
, lzo
, pkg-config
, squashfuse
, stdenv
, zlib
, zstd
}:

stdenv.mkDerivation {
  pname = "appimagecrafters-runtime";
  version = "unstable-2022-05-12";

  src = fetchFromGitHub {
    owner = "AppImageCrafters";
    repo = "appimage-runtime";
    rev = "6500a1ef68e039caba2ebab1c7ed74c2ea9e67a5";
    hash = "sha256-uxQBDy/JA7uEboTOUmGaZ2FAKY/0dQ9c0A0N8+J+a7I=";
  };

  nativeBuildInputs = [ pkg-config ];
  buildInputs = [
    fuse3
    squashfuse
    zstd
    zlib
    xz
    lz4
    lzo
  ];

  prePatch = ''
    sed -e '/sqfs_usage/s/);/, true\0/' -i src/main.c
  '';

  patches = [
    # basename() patch from
    # https://github.com/AppImageCrafters/appimage-runtime/pull/14/commits/23f655a9313a6b962e072f12534982b925ecb8f7
    ./basename.patch 
  ];

  configurePhase = ''
    $PKG_CONFIG --cflags fuse3 > cflags
  '';

  buildPhase = ''
    # extra includes to make things work
    mkdir -p include/squashfuse
    echo "#include_next <squashfuse/squashfuse.h>" > include/squashfuse/squashfuse.h
    cp ${squashfuse.src}/fuseprivate.h -t include/squashfuse

    $CC src/main.c -o $out \
      -D_FILE_OFFSET_BITS=64 -DGIT_COMMIT='"0000000"' \
      -I./include \
      $(cat cflags) \
      -lfuse3 -lsquashfuse_ll -lzstd -lz -llzma -llz4 -llzo2 \
      -T src/data_sections.ld

    # Add AppImage Type 2 Magic Bytes to runtime
    printf %b '\x41\x49\x02' > magic_bytes
    dd if=magic_bytes of=$out bs=1 count=3 seek=8 conv=notrunc status=none
  '';

  dontFixup = true;
}

# runCommandCC "appimage-runtime" {
#
# } ''
# NIX_CFLAGS_COMPILE="$(pkg-config --cflags fuse) $NIX_CFLAGS_COMPILE"
#
# # extra includes to make things work
# mkdir -p include/squashfuse
# echo "#include_next <squashfuse/squashfuse.h>" > include/squashfuse/squashfuse.h
# cp ${squashfuse.src}/fuseprivate.h -t include/squashfuse
#
# $CC $src/src/main.c -o $out \
#   -I./include -D_FILE_OFFSET_BITS=64 -DGIT_COMMIT='"0000000"' \
#   -lfuse -lsquashfuse_ll -lzstd -lz -lxz -llz4 -llzo2 \
#   -T $src/src/data_sections.ld
#
# # Add AppImage Type 2 Magic Bytes to runtime
# printf %b '\x41\x49\x02' > magic_bytes
# dd if=magic_bytes of=$out bs=1 count=3 seek=8 conv=notrunc status=none
# ''
