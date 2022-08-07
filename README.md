# nix-appimage

Create an AppImage, bundling a derivation and all its dependencies into a single-file executable.
Like [nix-bundle](https://github.com/matthewbauer/nix-bundle), but much faster and without the glibc dependency.

## Getting started

To use this, you will need to have [Nix](https://nixos.org/) available.
Then, run this via the [nix bundle](https://nixos.org/manual/nix/unstable/command-ref/new-cli/nix3-bundle.html) interface, replacing `nixpkgs.hello` with the flake you want to build:

```
$ nix bundle --bundler github:ralismark/nix-appimage nixpkgs#hello
```

This produces `hello-2.12.1-x86_64.AppImage`, which prints "Hello world!" when run:

```
$ ./hello-2.12.1-x86_64.AppImage
Hello, world!
```

## Caveats

- The produced file isn't a fully conforming AppImage.
For example, it's missing the relevant .desktop file and icons.
Please open an issue if this is something you want.

- This requires Linux User Namespaces (i.e. `CAP_SYS_USER_NS`), which should be available since Linux 3.8.

- Plain files in the root directory aren't visible to the bundled app.

## How it works

This uses [AppImageCrafers/appimage-runtime](https://github.com/AppImageCrafters/appimage-runtime) as the runtime instead of the default, which allows us to use musl instead of glibc and produce a fully static binary.
This launches our own AppRun, which creates a user namespaces and mounts the bundled /nix directory into the appropriate place, and launches the entrypoint symlink.

<!-- TODO more information here -->

## References

This project wouldn't be possible without the groundwork already laid out in [nix-bundle](https://github.com/matthewbauer/nix-bundle), and a lot here is inspired by what's done there.

If you want more related reading material, see:

- [AppImage type 2 spec](https://github.com/AppImage/AppImageSpec/blob/ce1910e6443357e3406a40d458f78ba3f34293b8/draft.md#target-systems)
- https://github.com/AppImage/AppImageKit/issues/877
- https://github.com/matthewbauer/nix-bundle/pull/76
- https://github.com/NixOS/bundlers/pull/1
