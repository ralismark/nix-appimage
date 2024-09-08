# nix-appimage

Create an AppImage, bundling a derivation and all its dependencies into a single-file executable.
Like [nix-bundle](https://github.com/matthewbauer/nix-bundle), but much faster and without the glibc dependency.

## Getting started

To use this, you will need to have [Nix](https://nixos.org/) available.
Then, run this via the [nix bundle](https://nixos.org/manual/nix/unstable/command-ref/new-cli/nix3-bundle.html) interface, replacing `nixpkgs.hello` with the flake you want to build:

```
$ nix bundle --bundler github:ralismark/nix-appimage nixpkgs#hello
```

This produces `hello.AppImage`, which prints "Hello world!" when run:

```
$ ./hello.AppImage
Hello, world!
```

If you get a `entrypoint ... is not executable` error, or want to specify a different binary to run, you can instead use the `./bundle` script:

```
$ ./bundle dnsutils /bin/dig # or ./bundle dnsutils dig
$ ./dig.AppImage -v
DiG 9.18.14
```

You can also use nix-appimage as a nix library -- the flake provides `lib.<system>.mkAppImage` which supports more options.
See mkAppImage.nix for details.

## Caveats

OpenGL apps not being able to run on non-NixOS systems is a **known problem**, see https://github.com/NixOS/nixpkgs/issues/9415 and https://github.com/ralismark/nix-appimage/issues/5.
You'll need to use something like [nixGL](https://github.com/guibou/nixGL).
Addressing this problem outright is _out of scope_ for this project, however PRs to integrate solutions (e.g. nixGL) are welcome.

Additionally, the produced file isn't a _fully_ conforming AppImage.
For example, it's missing the relevant .desktop file and icons -- this doesn't affect the running of bundled apps in any way, but might cause issues with showing up correctly in application launchers (e.g. rofi).
Please open an issue if this is something you want.

The current implementation also has some limitations:

- This requires Linux User Namespaces (i.e. `CAP_SYS_USER_NS`), which are available since Linux 3.8 (released in 2013), but may not be enabled for security reasons.
- Plain files in the root directory aren't visible to the bundled app.

## Under The Hood

nix-appimage creates [type 2 AppImages](https://github.com/AppImage/AppImageSpec/blob/ce1910e6443357e3406a40d458f78ba3f34293b8/draft.md#type-2-image-format), which are essentially just a binary, known as the Runtime, concatenated with a squashfs file system.
When the AppImage is run, the runtime simply mounts the squashfs somewhere and runs the contained `AppRun`.
The squashfs contains all the files needed to run the program:

- `nix/store/...`, containing the closure of the bundled program
- `entrypoint`, a symlink to the actual executable, e.g. `/nix/store/q9cqc10sw293xpx3hca4qpsmbg7hsgzy-hello-2.12.1/bin/hello`
- `AppRun`, which gets started after the squashfs is mounted.
  This isn't the actual bundled executable, but a wrapper that makes the bundled nix/store file visible under /nix/store before executing `entrypoint`.

Runtimes are included within the flake as `packages.<system>.appimage-runtimes.<name>`.
Currently supported are:

- `appimage-type2-runtime` (default)
  This is [AppImage/type2-runtime](https://github.com/AppImage/type2-runtime), a static runtime maintained by the official AppImage team.
- `appimagecrafters`.
  This is [AppImageCrafers/appimage-runtime](https://github.com/AppImageCrafters/appimage-runtime), a similar static runtime that was the old default for nix-appimage.

AppRuns are included within the flake as `packages.<system>.appimage-appruns.<name>`.
Currently supported are:

- `userns-chroot` (default).
  This uses Linux User Namespaces and chroot to make /nix/store appear to have the bundled files, similar to [nix-user-chroot](https://github.com/nix-community/nix-user-chroot).
  There is a known problem of plain files in the root folder not being visible to the bundled app when using this AppRun.
