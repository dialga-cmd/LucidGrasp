#!/usr/bin/env bash
set -euo pipefail

VERSION=1.28.3

case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)
    ARCH=linux-x64
    EXT=tgz
    ;;
  Linux-aarch64)
    ARCH=linux-aarch64
    EXT=tgz
    ;;
  Darwin-arm64)
    ARCH=osx-arm64
    EXT=tgz
    ;;
  Darwin-x86_64)
    # ONNX Runtime stopped publishing x86_64 macOS builds after 1.19.x, so
    # Intel Macs are served by the last release that still shipped one.
    ARCH=osx-x86_64
    VERSION=1.19.2
    EXT=tgz
    ;;
  MINGW*|MSYS*|CYGWIN*)
    ARCH=win-x64
    EXT=zip
    ;;
  *)
    echo "no prebuilt ONNX Runtime for $(uname -s)-$(uname -m); see https://github.com/microsoft/onnxruntime/releases" >&2
    exit 1
    ;;
esac

NAME="onnxruntime-${ARCH}-${VERSION}"
URL="https://github.com/microsoft/onnxruntime/releases/download/v${VERSION}/${NAME}.${EXT}"

mkdir -p third_party/onnxruntime
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

case "$EXT" in
  tgz)
    curl -sSL "$URL" | tar -xz -C "$TMP"
    ;;
  zip)
    curl -sSL -o "$TMP/pkg.zip" "$URL"
    unzip -q "$TMP/pkg.zip" -d "$TMP"
    ;;
esac

cp -a "$TMP/$NAME/." third_party/onnxruntime/
echo "ONNX Runtime ${VERSION} (${ARCH}) installed in third_party/onnxruntime"