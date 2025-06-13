#!/bin/sh
set -eu

program=$1
mkdir extras

# get the derivation's root directory

IFS=/ read -r _ nix store hash rest <<EOF
$program
EOF

if [ "$nix" != nix ] || [ "$store" != store ]; then
	echo "entrypoint '${program}' is not in the nix store" >&2
	exit 1
fi

drv="/nix/store/$hash"

# find desktop file

desktop=

# try find one whose Exec= matches $program
for d in "$drv/share/applications/"*.desktop; do
	if ! [ -e "$d" ]; then
		# if we don't match any files then it's whatever
		break
	fi

	if sed -nEe '/^Exec=/{ s/^Exec= *([^ ]*).*$/\1/; s#.*/##; p }' "$d" |
		grep --fixed-strings --line-regexp --quiet "$(basename "$program")"
	then
		if [ -z "$desktop" ]; then
			desktop=$d
		else
			echo "multiple .desktop entries found; giving up" >&2
			exit
		fi
	fi
done

if [ -z "$desktop" ]; then
	echo "no .desktop found; giving up" >&2
	exit
fi

# copy desktop file and icons

cp -L "$desktop" extras/
if [ -e "$drv/share/icons/hicolor" ]; then
	mkdir -p extras/usr/share/icons
	cp -Lr --no-preserve=all "$drv/share/icons/hicolor" extras/usr/share/icons
fi

# create DirIcon

grep ^Icon= "$desktop" | while IFS="=" read -r _ icon; do
	for size in 128x128 64x64 scalable; do
		for f in "${drv}/share/icons/hicolor/$size/apps/${icon}".*; do
			[ -e "$f" ] || continue

			# .DirIcon SHOULD be a 256x256 PNG, and the app could have non-PNG
			# icons, so ideally we'd use a tool like imagemagick to create it. But that's a big dependency, and this is close enough?

			cp -Lr --no-preserve=all "$f" extras/.DirIcon
			exit
		done
	done
done
