{ lib
, runCommand
, squashfsTools
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

  # Workaround for writeClosure bug.
  #
  # Due to a bug in Nix, writeClosure with a path *under* a nix store path (e.g.
  # /nix/store/...-hello/bin/hello) raises the error "path '$program' is not in
  # the Nix store".
  #
  # See: https://github.com/ralismark/nix-appimage/issues/16
  # See: https://github.com/NixOS/nixpkgs/issues/316652
  # See: https://github.com/NixOS/nix/pull/10549
  #
  # This should be fixed in the latest version of Nix, however version where
  # this bug is present are still common, so we work around it by using the old
  # implementation of writeReferencesToFile from
  # https://github.com/NixOS/nixpkgs/blob/e99021ff754a204e38df619ac908ac92885636a4/pkgs/build-support/trivial-builders/default.nix#L628-L640
  writeReferencesToFile = path: runCommand "runtime-deps"
    {
      exportReferencesGraph = [ "graph" path ];
    }
    ''
      touch $out
      while read path; do
        echo $path >> $out
        read dummy
        read nrRefs
        for ((i = 0; i < nrRefs; i++)); do read ref; done
      done < graph
    '';
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
    "$(cat ${writeReferencesToFile program})"
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
