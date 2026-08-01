"""Embebe FIRMWARE_VERSION en builds locales (pio run/upload sin pasar por CI).

Se salta si ya viene fijado por PLATFORMIO_BUILD_FLAGS (asi construye
tools/build_firmware.sh los releases via semantic-release, con la version
exacta calculada), para no pisarlo ni duplicar el -D.

Version resultante: <ultimo tag bump de patch>-dev.<hash corto>, p.ej. si el
ultimo tag es v1.1.2 y HEAD esta en 31a304b, produce "1.1.3-dev.31a304b"
(identificador de pre-release semver, deja claro que no es un release real
y de que commit sale). Si no hay tags o git no esta disponible, no define
nada y se usa el fallback "0.0.0-dev" de src/Version.h.
"""

import os
import re
import subprocess

Import("env")  # noqa: F821


def _git(args, cwd):
    try:
        return subprocess.check_output(
            ["git"] + args, cwd=cwd, stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        return None


def _dev_version(project_dir):
    tag = _git(["describe", "--tags", "--abbrev=0"], project_dir)
    commit = _git(["rev-parse", "--short", "HEAD"], project_dir)
    if not tag or not commit:
        return None
    match = re.match(r"v?(\d+)\.(\d+)\.(\d+)", tag)
    if not match:
        return None
    major, minor, patch = (int(x) for x in match.groups())
    return f"{major}.{minor}.{patch + 1}-dev.{commit}"


if "FIRMWARE_VERSION" not in os.environ.get("PLATFORMIO_BUILD_FLAGS", ""):
    version = _dev_version(env.subst("$PROJECT_DIR"))  # noqa: F821
    if version:
        env.Append(BUILD_FLAGS=[f'-DFIRMWARE_VERSION=\\"{version}\\"'])  # noqa: F821
