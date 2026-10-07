#!/usr/bin/env bash
#
# Builds lucidgrasp.icns from lucidgrasp.png for the macOS application bundle.
#
# This lives in a script rather than in CMakeLists.txt on purpose. Generating the
# icon means shelling out to sips and iconutil, and doing that inside
# execute_process() at configure time makes a failure read as "CMake could not
# configure", with the tool's own message suppressed by OUTPUT_QUIET. That is
# exactly how the first version failed: `iconutil failed to produce
# lucidgrasp.icns`, with no indication of why and nothing in the log to act on.
#
# Kept as a script so it runs where its output is visible, exits with a real
# status, and can be run by hand on a Mac to reproduce a CI failure.
#
# Usage: ./make-icns.sh [source.png] [output.icns]

set -euo pipefail

SRC=${1:-lucidgrasp.png}
OUT=${2:-lucidgrasp.icns}

if [ "$(uname -s)" != "Darwin" ]; then
    echo "make-icns.sh: needs macOS; sips and iconutil exist nowhere else." >&2
    exit 1
fi

if [ ! -f "$SRC" ]; then
    echo "make-icns.sh: no such source image: $SRC" >&2
    exit 1
fi

for tool in sips iconutil; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "make-icns.sh: $tool not found. These ship with macOS; if this is a" >&2
        echo "  bare install, the Command Line Tools are missing (xcode-select --install)." >&2
        exit 1
    fi
done

# iconutil only accepts a directory whose name ends in .iconset, and refuses to
# compile a set that is missing any of its ten required renditions or that holds
# anything else. So it is built in its own scratch directory rather than in the
# build tree, where a stray file would be silently fatal.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
ICONSET="$WORK/lucidgrasp.iconset"
mkdir -p "$ICONSET"

# Each logical size needs a 1x and a 2x rendition; Finder picks between them per
# display density. The 512@2x is the 1024px one that shows up in the Dock.
for size in 16 32 128 256 512; do
    for scale in 1 2; do
        px=$((size * scale))
        suffix=""
        [ "$scale" = "2" ] && suffix="@2x"
        dest="$ICONSET/icon_${size}x${size}${suffix}.png"

        if ! sips -z "$px" "$px" "$SRC" --out "$dest" >/dev/null 2>&1; then
            echo "make-icns.sh: sips failed to render ${px}px ($SRC -> $dest)" >&2
            sips -z "$px" "$px" "$SRC" --out "$dest" || true
            exit 1
        fi
        if [ ! -s "$dest" ]; then
            echo "make-icns.sh: sips reported success but $dest is empty" >&2
            exit 1
        fi
    done
done

expected=10
actual=$(find "$ICONSET" -type f -name '*.png' | wc -l | tr -d ' ')
if [ "$actual" != "$expected" ]; then
    echo "make-icns.sh: iconset has $actual pngs, iconutil requires $expected" >&2
    ls -la "$ICONSET" >&2
    exit 1
fi

# iconutil will not overwrite an existing output file.
rm -f "$OUT"
mkdir -p "$(dirname "$OUT")"

# The output path goes to -o, never as a second positional argument. The
# synopsis is
#
#     iconutil -c {icns | iconset} [ -o file ] file [icon-name]
#
# so that trailing argument is an icon *name*, not a destination. Passing the
# filename there makes iconutil go looking for an icon resource of that name and
# fail with
#
#     Icon resource not found in asset catalog with name 'lucidgrasp.icns'
#
# which reads like a problem with the source images and is not: the iconset was
# complete and correct. This cost two CI runs, the first of which reported
# nothing at all because CMake was swallowing the message.
echo "make-icns.sh: iconutil -c icns -o $OUT $ICONSET"

if ! iconutil -c icns -o "$OUT" "$ICONSET"; then
    echo "make-icns.sh: iconutil failed. Its own message is above; the set it was" >&2
    echo "  given had these files:" >&2
    ls -la "$ICONSET" >&2
    exit 1
fi

if [ ! -s "$OUT" ]; then
    echo "make-icns.sh: iconutil exited 0 but produced no $OUT" >&2
    exit 1
fi

echo "make-icns.sh: wrote $OUT ($(wc -c <"$OUT" | tr -d ' ') bytes) from $SRC"
