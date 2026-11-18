#!/bin/bash
# Pushes the tests built by build.sh to an arm64 device and runs them (each one takes a few seconds at most)
#   tools/neon_tests/run_device.sh <adb serial> [test names...] [-- test args]
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${OUT:-$ROOT/build/neon_tests}"
SERIAL="$1"
shift
TESTS=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do TESTS+=("$1"); shift; done
[ "$1" = "--" ] && shift
if [ ${#TESTS[@]} -eq 0 ]; then
	for f in "$OUT"/*_test; do TESTS+=("$(basename "$f")"); done
fi
adb -s "$SERIAL" shell mkdir -p /data/local/tmp/neon_tests
for t in "${TESTS[@]}"; do
	adb -s "$SERIAL" push -q "$OUT/$t" /data/local/tmp/neon_tests/
	adb -s "$SERIAL" shell chmod 755 "/data/local/tmp/neon_tests/$t"
	adb -s "$SERIAL" shell "/data/local/tmp/neon_tests/$t" "$@"
done
