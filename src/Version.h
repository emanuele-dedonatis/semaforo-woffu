#pragma once

// Embebida en tiempo de compilación como build flag en releases (ver
// tools/build_firmware.sh); en builds locales de desarrollo se usa este valor
// por defecto.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0-dev"
#endif
