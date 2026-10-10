#!/usr/bin/env bash
set -euo pipefail

VARIANT="${1:-general}"

case "$VARIANT" in
  general)
    URL="https://github.com/danielgatis/rembg/releases/download/v0.0.0/BiRefNet-general-epoch_244.onnx"
    MD5="7a35a0141cbbc80de11d9c9a28f52697"
    OUT="models/birefnet-general.onnx"
    ;;
  lite)
    URL="https://github.com/danielgatis/rembg/releases/download/v0.0.0/BiRefNet-general-bb_swin_v1_tiny-epoch_232.onnx"
    MD5="4fab47adc4ff364be1713e97b7e66334"
    OUT="models/birefnet-general-lite.onnx"
    ;;
  semantic)
    URL="https://huggingface.co/onnx-community/dinov2-small/resolve/8b1f705a3a7f6f062f6bdd21986c1583d3ef105d/onnx/model_int8.onnx"
    MD5="70279b6f33ef8a85966ef8f8493a3f2b"
    OUT="models/dinov2_small_int8.onnx"
    ;;
  *)
    echo "usage: $0 [general|lite|semantic]" >&2
    exit 2
    ;;
esac

mkdir -p models
TMP="${OUT}.part"
curl -sSL -o "$TMP" "$URL"
echo "$MD5  $TMP" | md5sum -c -
mv "$TMP" "$OUT"
echo "model downloaded to $OUT"