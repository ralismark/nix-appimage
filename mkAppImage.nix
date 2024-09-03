{ lib
, runCommand
, squashfsTools
, writeClosure
, writeTextFile

  # mkappimage-specific, passed from flake.nix
, mkappimage-runtime # runtimes are an executable that mount the squashfs part of the appimage and start AppRun
, mkappimage-apprun # appruns contain an AppRun executable that does setup and launches entrypoint
}:

# actual arguments
{ program # absolute path of executable to start

  # output name
, pname ? (lib.last (builtins.split "/" program))
, name ? "${pname}.AppImage"

  # advanced appimage configuration
, squashfsArgs ? [ ] # additional arguments to pass to mksquashfs
}:

let
  commonArgs = [
    "-offset $(stat -L -c%s ${lib.escapeShellArg mkappimage-runtime})" # squashfs comes after the runtime
    "-all-root" # chown to root
  ] ++ squashfsArgs;
in
runCommand name
{
  nativeBuildInputs = [ squashfsTools ];
} ''
  if ! test -x ${program}; then
    echo "entrypoint '${program}' is not executable"
    exit 1
  fi

  mksquashfs ${builtins.concatStringsSep " " ([
    # first run of mksquashfs copies the nix/store closure and additional files
    "$(cat ${writeClosure [ program ]})"
    "$out"

    # additional files
    (lib.concatMapStrings (x: " -p ${lib.escapeShellArg x}") [
      # symlink entrypoint to the executable to run
      "entrypoint s 555 0 0 ${program}"
    ])

    "-no-strip" # don't strip leading dirs, to preserve the fact that everything's in the nix store
  ] ++ commonArgs)}

  mksquashfs ${builtins.concatStringsSep " " ([
    # second run of mksquashfs adds the apprun
    # no -no-strip since we *do* want to strip leading dirs now
    "${mkappimage-apprun}"
    "$out"
    "-no-recovery" # i don't know what a recovery file is but it gives "No such file or directory"
  ] ++ commonArgs)}

  # add the runtime to the start
  dd if=${lib.escapeShellArg mkappimage-runtime} of=$out conv=notrunc

  # make executable
  chmod 755 $out
''
