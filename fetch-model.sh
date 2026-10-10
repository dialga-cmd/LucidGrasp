#!/usr/bin/env bash
set -euo pipefail

VARIANT="${1:-semantic}"

case "$VARIANT" in
  semantic)
    URL="https://huggingface.co/onnx-community/dinov2-small/resolve/8b1f705a3a7f6f062f6bdd21986c1583d3ef105d/onnx/model_int8.onnx"
    MD5="70279b6f33ef8a85966ef8f8493a3f2b"
    OUT="models/dinov2_small_int8.onnx"
    ;;
  *)
    echo "usage: $0 [semantic]" >&2
    exit 2
    ;;
esac

mkdir -p models
TMP="${OUT}.part"
curl -sSL -o "$TMP" "$URL"
echo "$MD5  $TMP" | md5sum -c -
mv "$TMP" "$OUT"
echo "model downloaded to $OUT"