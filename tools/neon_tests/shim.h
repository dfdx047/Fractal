// Minimal stand-in for src/Common/precompiled.h so emulator source files can be compiled into the standalone tests
// Force-included with -include, see build.sh
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <array>
#include <bit>

#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_X86_64
#include <immintrin.h>
#endif

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using sint8 = int8_t;
using sint16 = int16_t;
using sint32 = int32_t;
using sint64 = int64_t;

#define cemu_assert_debug(x) ((void)0)
#define cemu_assert(x) ((void)0)
#define FORCE_INLINE inline __attribute__((always_inline))

inline uint16 _swapEndianU16(uint16 v) { return __builtin_bswap16(v); }
inline uint32 _swapEndianU32(uint32 v) { return __builtin_bswap32(v); }
inline uint64 _swapEndianU64(uint64 v) { return __builtin_bswap64(v); }
