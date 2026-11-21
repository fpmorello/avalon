#!/bin/sh -e
# Builds and runs the standalone host tests of the Switch 2 port.
#
# The Switch-selected code paths (see the __SWITCH__ guards) that can run on a host
# are forced on with the matching EDEN_* macros, so the host binary exercises the
# exact code the NRO build gets. Build scratch stays outside the repository (mktemp).
#
# Usage: tools/switch2/tests/run.sh [test source name without extension]

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

TEST=${1:-sparse_chunked_test}
CXX=${CXX:-clang++}
WARN="-Wall -Wextra"

# ASan catches leaks and use-after-free of the chunked backing.
$CXX -std=c++23 $WARN -fsanitize=address -DEDEN_CHUNKED_SPARSE_VECTOR \
    -I "$ROOT/src" -I "$ROOT/src/dynarmic/src" \
    "$ROOT/tools/switch2/tests/$TEST.cpp" -o "$OUT/${TEST}_asan"
"$OUT/${TEST}_asan"

# TSan catches chunk-allocation races between writers (not available on every host).
if $CXX -std=c++23 -fsanitize=thread -DEDEN_CHUNKED_SPARSE_VECTOR \
        -I "$ROOT/src" -I "$ROOT/src/dynarmic/src" \
        "$ROOT/tools/switch2/tests/$TEST.cpp" -o "$OUT/${TEST}_tsan" 2>/dev/null; then
    "$OUT/${TEST}_tsan"
else
    echo "note: TSan build unavailable, skipped"
fi
