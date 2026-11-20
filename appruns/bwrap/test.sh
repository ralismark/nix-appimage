#!/usr/bin/env bash
LOCATION="$(dirname -- "$(readlink -f "${BASH_SOURCE}")")"
$LOCATION/bwrap $(ls / | grep -v -E "dev|proc" | xargs -I % echo --bind /% /% | tr '\n' ' ') --dev-bind /dev /dev --proc /proc --ro-bind $LOCATION/nix /nix $(readlink $LOCATION/entrypoint) $@
