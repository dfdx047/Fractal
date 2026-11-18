#!/bin/bash
# Builds the standalone NEON/ARMv8 tests as static aarch64 executables with the NDK (same -O2 as the app's
# RelWithDebInfo build). These tests compare each NEON path with the scalar code it replaces, bit for bit.
#
#   tools/neon_tests/build.sh [output dir]          (default: build/neon_tests)
#   tools/neon_tests/run_device.sh <adb serial>     pushes to /data/local/tmp/neon_tests and runs them
#
# Tests that also have a portable path (texture decoding) can be built for the host with HOST=1.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/build/neon_tests}"
mkdir -p "$OUT"
: "${NDK:=$HOME/Android/Sdk/ndk/29.0.14206865}"
if [ "$HOST" = "1" ]; then
	CXX=(clang++)
	SUFFIX=_host
else
	CXX=("$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" --target=aarch64-linux-android30 -static)
	SUFFIX=
fi
FLAGS=(-O2 -std=gnu++20 -include "$ROOT/tools/neon_tests/shim.h" -I "$ROOT/src" -I "$ROOT/tools/neon_tests" -Wno-ignored-attributes)
for t in "$ROOT"/tools/neon_tests/*_test.cpp; do
	name="$(basename "$t" .cpp)"
	if [ "$HOST" = "1" ] && ! grep -q "HOST_TEST_OK" "$t"; then
		continue
	fi
	echo "building $name$SUFFIX"
	"${CXX[@]}" "${FLAGS[@]}" -o "$OUT/$name$SUFFIX" "$t"
done
