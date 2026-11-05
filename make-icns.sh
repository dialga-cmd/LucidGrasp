#!/usr/bin/env bash

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

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
ICONSET="$WORK/lucidgrasp.iconset"
mkdir -p "$ICONSET"

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

rm -f "$OUT"
mkdir -p "$(dirname "$OUT")"

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
