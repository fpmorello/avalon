#!/bin/sh -e
# Builds targets of the Switch 2 configuration (see configure.sh).
# The devkitA64 tools must be on PATH: bundled OpenSSL calls them by name.
# When the NRO is built, copies it next to the sources named avalon-<build>.nro
# (build id from src/eden_horizon/version.h) so binaries are identifiable.
#
# Usage: tools/switch2/build.sh [ninja args], e.g. build.sh -k 0 common dynarmic

: "${DEVKITPRO:?Set DEVKITPRO (e.g. /opt/devkitpro)}"
. "$DEVKITPRO/switch2vars.sh"

set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ninja -C "$ROOT/build/switch2" "$@"

NRO="$ROOT/build/switch2/bin/avalon.nro"
if [ -f "$NRO" ]; then
    BUILD=$(sed -n 's/^#define EDEN_HORIZON_BUILD "\([^"]*\)"/\1/p' \
        "$ROOT/src/eden_horizon/version.h")
    if [ -n "$BUILD" ]; then
        cp "$NRO" "$ROOT/avalon-$BUILD.nro"
        echo "built avalon-$BUILD.nro"
    fi
fi
