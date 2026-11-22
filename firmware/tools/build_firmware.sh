#!/usr/bin/env bash
# Compila el firmware embebiendo la versión de release y genera los assets
# (firmware.bin, version.txt) que publica @semantic-release/github.
# Uso (desde la raiz del repo, como lo invoca semantic-release):
#   firmware/tools/build_firmware.sh <version>
# Los assets se dejan en el directorio actual (raiz del repo), que es donde
# los busca .releaserc.json.
set -euo pipefail

VERSION="$1"
OUT_DIR="$(pwd)"
FIRMWARE_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo "$VERSION" > "$OUT_DIR/version.txt"

cd "$FIRMWARE_DIR"
PLATFORMIO_BUILD_FLAGS="-D FIRMWARE_VERSION=\\\"$VERSION\\\"" pio run -e esp32dev

cp .pio/build/esp32dev/firmware.bin "$OUT_DIR/firmware.bin"
