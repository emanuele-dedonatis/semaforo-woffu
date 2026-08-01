#!/usr/bin/env bash
# Compila el firmware embebiendo la versión de release y genera los assets
# (firmware.bin, version.txt) que publica @semantic-release/github.
# Uso: tools/build_firmware.sh <version>
set -euo pipefail

VERSION="$1"

echo "$VERSION" > version.txt

PLATFORMIO_BUILD_FLAGS="-D FIRMWARE_VERSION=\\\"$VERSION\\\"" pio run -e esp32dev

cp .pio/build/esp32dev/firmware.bin firmware.bin
