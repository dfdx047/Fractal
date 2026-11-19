#include "Cafe/HW/Latte/Core/LatteTextureTranscodeETC.h"
#include <atomic>
#include <memory>
#include <climits>
#include <algorithm>
#include <bit>
#include <cstring>

// Bit layouts and decoding rules: Khronos Data Format Specification, ETC2/EAC sections. A 64 bit ETC2/EAC block is
// handled here as an integer whose bit 63 is the most significant bit of the first byte (the blocks are big endian).
// Texels inside an ETC2/EAC block are numbered column by column: texel i is at x = i / 4, y = i % 4.

namespace
{
	// ETC1 luminance modifiers (pixel index 0: +a, 1: +b, 2: -a, 3: -b)
	constexpr sint32 kETC1Modifiers[8][2] = { {2, 8}, {5, 17}, {9, 29}, {13, 42}, {18, 60}, {24, 80}, {33, 106}, {47, 183} };
	// ETC2 T and H mode distances
	constexpr sint32 kETC2Distances[8] = { 3, 6, 11, 16, 23, 32, 41, 64 };
	// EAC modifiers (same for the ETC2 alpha channel and R11/RG11), by 3 bit index
	constexpr sint32 kEACModifiers[16][8] = {
		{-3, -6, -9, -15, 2, 5, 8, 14}, {-3, -7, -10, -13, 2, 6, 9, 12}, {-2, -5, -8, -13, 1, 4, 7, 12}, {-2, -4, -6, -13, 1, 3, 5, 12},
		{-3, -6, -8, -12, 2, 5, 7, 11}, {-3, -7, -9, -11, 2, 6, 8, 10}, {-4, -7, -8, -11, 3, 6, 7, 10}, {-3, -5, -8, -11, 2, 4, 7, 10},
		{-2, -6, -8, -10, 1, 5, 7, 9}, {-2, -5, -8, -10, 1, 4, 7, 9}, {-2, -4, -8, -10, 1, 3, 7, 9}, {-2, -5, -7, -10, 1, 4, 6, 9},
		{-3, -4, -7, -10, 2, 3, 6, 9}, {-1, -2, -3, -10, 0, 1, 2, 9}, {-4, -6, -8, -9, 3, 5, 7, 8}, {-3, -5, -7, -9, 2, 4, 6, 8},
	};

	inline sint32 Clamp255(sint32 v)
	{
		return v < 0 ? 0 : (v > 255 ? 255 : v);
	}

	inline sint32 Expand4(sint32 v) { return v * 17; }
	inline sint32 Expand5(sint32 v) { return (v << 3) | (v >> 2); }
	inline sint32 Expand6(sint32 v) { return (v << 2) | (v >> 4); }
	inline sint32 Expand7(sint32 v) { return (v << 1) | (v >> 6); }

	// rounded a / b for b > 0 and any sign of a
	inline sint32 RoundDiv(sint32 a, sint32 b)
	{
		return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
	}

	// nearest N bit value (after bit replication) of each 8 bit value
	struct QuantTables
	{
		uint8 q4[256], q5[256], q6[256], q7[256];
		QuantTables()
		{
			auto build = [](uint8* table, sint32 levels, sint32 (*expand)(sint32)) {
				for (sint32 v = 0; v < 256; v++)
				{
					sint32 best = 0;
					for (sint32 q = 1; q < levels; q++)
						if (std::abs(expand(q) - v) < std::abs(expand(best) - v))
							best = q;
					table[v] = (uint8)best;
				}
			};
			build(q4, 16, Expand4);
			build(q5, 32, Expand5);
			build(q6, 64, Expand6);
			build(q7, 128, Expand7);
		}
	};
	const QuantTables s_quant;

	inline void StoreBigEndian64(uint8* out, uint64 v)
	{
		for (sint32 i = 0; i < 8; i++)
			out[i] = (uint8)(v >> (56 - i * 8));
	}

	inline uint64 Load48(const uint8* p)
	{
		return (uint64)p[0] | ((uint64)p[1] << 8) | ((uint64)p[2] << 16) | ((uint64)p[3] << 24) | ((uint64)p[4] << 32) | ((uint64)p[5] << 40);
	}

	/* EAC (R11, signed R11, ETC2 alpha) */

	enum class EACMode : uint8
	{
		R11Unsigned, // 0..2047, base * 8 + 4 + modifier * multiplier * 8 (multiplier 0: modifier * 1)
		R11Signed,   // -1023..1023, base (-127..127) * 8 + modifier * multiplier * 8 (multiplier 0: modifier * 1)
		Alpha8,      // 0..255, base + modifier * multiplier (multiplier 1..15)
	};

	inline sint32 EAC_Value(EACMode mode, sint32 base, sint32 multiplier, sint32 modifier)
	{
		if (mode == EACMode::Alpha8)
			return Clamp255(base + modifier * multiplier);
		const sint32 step = multiplier ? multiplier * 8 : 1;
		if (mode == EACMode::R11Unsigned)
			return std::clamp(base * 8 + 4 + modifier * step, 0, 2047);
		return std::clamp(base * 8 + modifier * step, -1023, 1023);
	}

	struct EACFit
	{
		sint32 base;
		sint32 multiplier;
		sint32 table;
		uint64 error;
	};

	// the modifiers of each EAC table in ascending order
	struct EACSortedModifiers
	{
		sint32 m[16][8];
		EACSortedModifiers()
		{
			for (sint32 t = 0; t < 16; t++)
			{
				for (sint32 k = 0; k < 8; k++)
					m[t][k] = kEACModifiers[t][k];
				std::sort(m[t], m[t] + 8);
			}
		}
	};
	const EACSortedModifiers s_eacSorted;

	// Searches base, multiplier and table for a set of target values (weight 0: ignored). The targets come from a BC
	// palette, so they sit on an even grid: every table is tried with its full modifier range and its inner ranges
	// (2nd to 7th, 3rd to 6th value) spanning the targets, then the multipliers and bases around the best of those.
	// Targets on the ends of the value range (the fixed 0 and 1 of the BC 4 interpolated mode) can also be reached by
	// the outer modifiers through the clamp, with the inner ones on the other targets: those candidates are tried too.
	// 'wide' tries more (used when the result is cached)
	template<EACMode mode>
	void EAC_FitT(const sint32* allTargets, const uint32* allWeights, sint32 allCount, bool wide, EACFit& best)
	{
		const sint32 valueMin = mode == EACMode::R11Signed ? -1023 : 0;
		const sint32 valueMax = mode == EACMode::R11Unsigned ? 2047 : (mode == EACMode::R11Signed ? 1023 : 255);
		sint32 targets[16];
		uint32 weights[16];
		sint32 count = 0;
		sint32 lo = INT_MAX, hi = INT_MIN, innerLo = INT_MAX, innerHi = INT_MIN;
		for (sint32 i = 0; i < allCount; i++)
		{
			if (allWeights[i] == 0)
				continue;
			const sint32 t = allTargets[i];
			targets[count] = t;
			weights[count] = allWeights[i];
			count++;
			lo = std::min(lo, t);
			hi = std::max(hi, t);
			if (t != valueMin && t != valueMax)
			{
				innerLo = std::min(innerLo, t);
				innerHi = std::max(innerHi, t);
			}
		}
		if (count == 0)
		{
			targets[0] = lo = hi = allTargets[0];
			weights[0] = 1;
			count = 1;
		}
		const bool hasInner = innerLo <= innerHi;
		const bool hasEnds = hasInner && (lo == valueMin || hi == valueMax);
		const sint32 unit = mode == EACMode::Alpha8 ? 1 : 8;
		const sint32 baseMin = mode == EACMode::R11Signed ? -127 : 0;
		const sint32 baseMax = mode == EACMode::R11Signed ? 127 : 255;
		const sint32 multiplierMin = mode == EACMode::Alpha8 ? 1 : 0;

		auto evaluate = [&](sint32 table, sint32 multiplier, sint32 base, uint64 limit) -> uint64 {
			sint32 values[8];
			for (sint32 k = 0; k < 8; k++)
				values[k] = EAC_Value(mode, base, multiplier, kEACModifiers[table][k]);
			uint64 error = 0;
			for (sint32 i = 0; i < count && error < limit; i++)
			{
				sint32 bestDistance = INT_MAX;
				for (sint32 k = 0; k < 8; k++)
					bestDistance = std::min(bestDistance, std::abs(values[k] - targets[i]));
				error += (uint64)weights[i] * (uint64)(bestDistance * bestDistance);
			}
			return error;
		};
		auto consider = [&](sint32 table, sint32 multiplier, sint32 base) -> uint64 {
			const uint64 error = evaluate(table, multiplier, base, best.error);
			if (error < best.error)
			{
				best.error = error;
				best.base = base;
				best.multiplier = multiplier;
				best.table = table;
			}
			return error;
		};
		auto stepOf = [&](sint32 multiplier) { return (mode != EACMode::Alpha8 && multiplier == 0) ? 1 : multiplier * unit; };
		// the base whose value is closest to twice the given value, divided by two
		auto baseFor2 = [&](sint32 value2) -> sint32 {
			sint32 base;
			if (mode == EACMode::R11Unsigned)
				base = RoundDiv(value2 - 8, 16);
			else if (mode == EACMode::R11Signed)
				base = RoundDiv(value2, 16);
			else
				base = RoundDiv(value2, 2);
			return std::clamp(base, baseMin, baseMax);
		};

		// candidate ranges to span: the targets off the ends of the value range, and all of them
		sint32 rangeLo[2], rangeHi[2];
		sint32 numRanges = 1;
		rangeLo[0] = hasInner ? innerLo : lo;
		rangeHi[0] = hasInner ? innerHi : hi;
		if (hasEnds)
		{
			rangeLo[1] = lo;
			rangeHi[1] = hi;
			numRanges = 2;
		}
		// the base that puts the middle of the span (sorted modifiers span..7-span) on the middle of the range
		auto centeredBase = [&](sint32 range, sint32 table, sint32 span, sint32 multiplier) {
			return baseFor2(rangeLo[range] + rangeHi[range] - stepOf(multiplier) * (s_eacSorted.m[table][span] + s_eacSorted.m[table][7 - span]));
		};
		auto spanWidth = [&](sint32 table, sint32 span) { return (s_eacSorted.m[table][7 - span] - s_eacSorted.m[table][span]) * unit; };

		best.error = UINT64_MAX;
		best.base = std::clamp(0, baseMin, baseMax);
		best.multiplier = 1;
		best.table = 0;
		constexpr sint32 kSpans = 3;
		uint64 candidateError[2 * 16 * kSpans];
		for (sint32 range = 0; range < 2; range++)
		{
			for (sint32 table = 0; table < 16; table++)
			{
				for (sint32 span = 0; span < kSpans; span++)
				{
					uint64& error = candidateError[(range * 16 + table) * kSpans + span];
					if (range >= numRanges)
					{
						error = UINT64_MAX;
						continue;
					}
					const sint32 multiplier = std::clamp(RoundDiv(rangeHi[range] - rangeLo[range], spanWidth(table, span)), multiplierMin, 15);
					error = consider(table, multiplier, centeredBase(range, table, span, multiplier));
				}
			}
		}
		if (best.error == 0)
			return;
		if (hasEnds)
		{
			// the smallest multiplier whose outer modifiers clamp to the used ends from the middle of the other targets,
			// with each inner modifier on that middle
			const sint32 middle = (innerLo + innerHi) / 2;
			for (sint32 table = 0; table < 16; table++)
			{
				const sint32* sorted = s_eacSorted.m[table];
				sint32 step = 1;
				if (lo == valueMin)
					step = std::max(step, (middle - valueMin + (-sorted[0]) - 1) / (-sorted[0]));
				if (hi == valueMax)
					step = std::max(step, (valueMax - middle + sorted[7] - 1) / sorted[7]);
				const sint32 multiplier = std::clamp((step + unit - 1) / unit, std::max(multiplierMin, 1), 15);
				for (sint32 k = wide ? 1 : 2; k < (wide ? 7 : 6); k++)
					consider(table, multiplier, baseFor2(2 * (middle - stepOf(multiplier) * sorted[k])));
			}
		}
		const sint32 candidatesToRefine = wide ? 6 : 2;
		for (sint32 n = 0; n < candidatesToRefine; n++)
		{
			sint32 c = 0;
			for (sint32 i = 1; i < 2 * 16 * kSpans; i++)
				if (candidateError[i] < candidateError[c])
					c = i;
			if (candidateError[c] == UINT64_MAX)
				break;
			candidateError[c] = UINT64_MAX;
			const sint32 range = c / (16 * kSpans);
			const sint32 table = (c / kSpans) % 16;
			const sint32 span = c % kSpans;
			const sint32 multiplierFloor = (rangeHi[range] - rangeLo[range]) / spanWidth(table, span);
			for (sint32 multiplier = std::max(multiplierMin, multiplierFloor - 1); multiplier <= std::min(15, multiplierFloor + 2); multiplier++)
			{
				const sint32 baseCenter = centeredBase(range, table, span, multiplier);
				for (sint32 base = std::max(baseMin, baseCenter - 1); base <= std::min(baseMax, baseCenter + 1); base++)
					consider(table, multiplier, base);
			}
		}
		// local search around the best
		for (sint32 pass = 0; pass < 2 && best.error != 0; pass++)
		{
			const EACFit start = best;
			for (sint32 multiplier = std::max(multiplierMin, start.multiplier - 1); multiplier <= std::min(15, start.multiplier + 1); multiplier++)
				for (sint32 base = std::max(baseMin, start.base - 2); base <= std::min(baseMax, start.base + 2); base++)
					consider(start.table, multiplier, base);
			if (best.error == start.error)
				break;
		}
	}

	void EAC_Fit(EACMode mode, const sint32* targets, const uint32* weights, sint32 count, bool wide, EACFit& best)
	{
		if (mode == EACMode::R11Unsigned)
			EAC_FitT<EACMode::R11Unsigned>(targets, weights, count, wide, best);
		else if (mode == EACMode::R11Signed)
			EAC_FitT<EACMode::R11Signed>(targets, weights, count, wide, best);
		else
			EAC_FitT<EACMode::Alpha8>(targets, weights, count, wide, best);
	}

	// index of the EAC value nearest to each target, and its distance
	void EAC_NearestIndices(EACMode mode, const EACFit& fit, const sint32* targets, sint32 count, uint8* indices, sint32* distances)
	{
		sint32 values[8];
		for (sint32 k = 0; k < 8; k++)
			values[k] = EAC_Value(mode, fit.base, fit.multiplier, kEACModifiers[fit.table][k]);
		for (sint32 i = 0; i < count; i++)
		{
			sint32 bestK = 0;
			for (sint32 k = 1; k < 8; k++)
				if (std::abs(values[k] - targets[i]) < std::abs(values[bestK] - targets[i]))
					bestK = k;
			indices[i] = (uint8)bestK;
			distances[i] = std::abs(values[bestK] - targets[i]);
		}
	}

	inline bool BC_SixInterpolated(EACMode mode, uint8 e0, uint8 e1)
	{
		return mode == EACMode::R11Signed ? (sint8)e0 > (sint8)e1 : e0 > e1;
	}

	// The 8 values of a BC3 alpha / BC4 / BC5 channel palette as EAC targets: the float palette of the BC decoders
	// (LatteTextureLoaderBC.cpp) scaled to the EAC range and rounded
	void BC_PaletteTargets(EACMode mode, uint8 e0, uint8 e1, sint32 targets[8])
	{
		float p[8];
		float lowValue;
		if (mode == EACMode::R11Signed)
		{
			p[0] = std::max((float)(sint8)e0 / 127.0f, -1.0f);
			p[1] = std::max((float)(sint8)e1 / 127.0f, -1.0f);
			lowValue = -1.0f;
		}
		else
		{
			p[0] = (float)e0 / 255.0f;
			p[1] = (float)e1 / 255.0f;
			lowValue = 0.0f;
		}
		if (BC_SixInterpolated(mode, e0, e1))
		{
			for (sint32 i = 0; i < 6; i++)
				p[2 + i] = ((6 - i) * p[0] + (1 + i) * p[1]) / 7.0f;
		}
		else
		{
			for (sint32 i = 0; i < 4; i++)
				p[2 + i] = ((4 - i) * p[0] + (1 + i) * p[1]) / 5.0f;
			p[6] = lowValue;
			p[7] = 1.0f;
		}
		const float scale = mode == EACMode::R11Unsigned ? 2047.0f : (mode == EACMode::R11Signed ? 1023.0f : 255.0f);
		for (sint32 i = 0; i < 8; i++)
			targets[i] = (sint32)std::lround(p[i] * scale);
	}

	// Cached EAC parameters for each endpoint pair of a mode, fit to all 8 palette entries with the same weight. In the
	// 4 interpolated mode there is a second fit without the fixed 0 and 1 entries, for blocks that don't use them.
	// Entry: bit 63 valid, 0-7 base, 8-11 multiplier, 12-15 table, 16-39 EAC index of each palette entry, 40-55 error
	// class of each palette entry (2 bits: up to 1, 2 or 4 steps of the 8 bit decode, or more)
	std::atomic<uint64> s_eacCache[3][2][65536];

	inline uint32 EAC_ErrorClass(EACMode mode, sint32 distance)
	{
		const sint32 step = mode == EACMode::Alpha8 ? 1 : 8;
		if (distance <= step)
			return 0;
		if (distance <= 2 * step)
			return 1;
		if (distance <= 4 * step)
			return 2;
		return 3;
	}

	uint64 EAC_CachedFit(EACMode mode, uint8 e0, uint8 e1, bool withoutFixedEntries)
	{
		std::atomic<uint64>& slot = s_eacCache[(sint32)mode][withoutFixedEntries ? 1 : 0][e0 | (e1 << 8)];
		uint64 entry = slot.load(std::memory_order_relaxed);
		if (entry & (1ull << 63))
			return entry;
		sint32 targets[8];
		BC_PaletteTargets(mode, e0, e1, targets);
		uint32 weights[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
		if (withoutFixedEntries)
			weights[6] = weights[7] = 0;
		EACFit fit;
		EAC_Fit(mode, targets, weights, 8, true, fit);
		uint8 indices[8];
		sint32 distances[8];
		EAC_NearestIndices(mode, fit, targets, 8, indices, distances);
		entry = (1ull << 63) | (uint64)(fit.base & 0xFF) | ((uint64)fit.multiplier << 8) | ((uint64)fit.table << 12);
		for (sint32 i = 0; i < 8; i++)
		{
			entry |= (uint64)indices[i] << (16 + i * 3);
			entry |= (uint64)EAC_ErrorClass(mode, distances[i]) << (40 + i * 2);
		}
		// every thread computes the same entry, so a race only repeats the work
		slot.store(entry, std::memory_order_relaxed);
		return entry;
	}

	// one 8 byte BC3 alpha / BC4 / BC5 channel group to an EAC block
	uint64 EAC_TranscodeGroup(EACMode mode, const uint8* group, bool bc5)
	{
		uint8 e0 = group[0];
		uint8 e1 = group[1];
		// BC5 SNORM compares the endpoints after -128 became -1.0 like -127 (BC5_ComputeChannelPalette_SNORM)
		if (bc5 && mode == EACMode::R11Signed)
		{
			if (e0 == 0x80)
				e0 = 0x81;
			if (e1 == 0x80)
				e1 = 0x81;
		}
		const uint64 codes = Load48(group + 2);
		uint32 usedMask = 0;
		uint32 counts[8] = {};
		for (sint32 t = 0; t < 16; t++)
		{
			const uint32 code = (uint32)(codes >> (t * 3)) & 7;
			usedMask |= 1u << code;
			counts[code]++;
		}
		const bool sixInterpolated = BC_SixInterpolated(mode, e0, e1);
		const bool withoutFixedEntries = !sixInterpolated && (usedMask & 0xC0) == 0;
		const uint64 entry = EAC_CachedFit(mode, e0, e1, withoutFixedEntries);
		sint32 base = (sint32)(entry & 0xFF);
		sint32 multiplier = (sint32)(entry >> 8) & 0xF;
		sint32 table = (sint32)(entry >> 12) & 0xF;
		uint32 indexMap = (uint32)(entry >> 16) & 0xFFFFFF;
		// palette entries the cached fit misses by more than 2 steps
		uint32 badMask = 0;
		for (sint32 i = 0; i < 8; i++)
			if (((entry >> (40 + i * 2)) & 3) >= 2)
				badMask |= 1u << i;
		// A block that uses only a few of the entries the cached fit was made for, some of them poorly served, gets its
		// own fit of the entries it uses, weighted by texel count: two entries or less, a part of the ramp up to 4/7 of
		// it, or, with the fixed 0 and 1 of the 4 interpolated mode, not all entries. Blocks that use most of the ramp
		// (normal maps) keep the cached fit: it is close to the best there and a new fit costs ~50x the time
		bool refit = false;
		if ((usedMask & badMask) != 0)
		{
			if (withoutFixedEntries || sixInterpolated)
			{
				// position of each palette entry on the ramp
				static const uint8 kPosition6[8] = { 0, 7, 1, 2, 3, 4, 5, 6 };
				static const uint8 kPosition4[8] = { 0, 5, 1, 2, 3, 4, 0, 5 };
				const uint8* position = sixInterpolated ? kPosition6 : kPosition4;
				sint32 first = 7, last = 0;
				for (sint32 i = 0; i < 8; i++)
				{
					if (usedMask & (1u << i))
					{
						first = std::min(first, (sint32)position[i]);
						last = std::max(last, (sint32)position[i]);
					}
				}
				refit = std::popcount(usedMask) <= 2 || (last - first) * 7 <= 4 * (sixInterpolated ? 7 : 5);
			}
			else
				refit = usedMask != 0xFF;
		}
		if (refit)
		{
			sint32 targets[8];
			BC_PaletteTargets(mode, e0, e1, targets);
			EACFit fit;
			EAC_Fit(mode, targets, counts, 8, false, fit);
			uint8 indices[8];
			sint32 distances[8];
			EAC_NearestIndices(mode, fit, targets, 8, indices, distances);
			base = fit.base & 0xFF;
			multiplier = fit.multiplier;
			table = fit.table;
			indexMap = 0;
			for (sint32 i = 0; i < 8; i++)
				indexMap |= (uint32)indices[i] << (i * 3);
		}
		uint64 block = ((uint64)base << 56) | ((uint64)multiplier << 52) | ((uint64)table << 48);
		for (sint32 t = 0; t < 16; t++)
		{
			const uint32 code = (uint32)(codes >> (t * 3)) & 7;
			const uint64 index = (indexMap >> (code * 3)) & 7;
			const sint32 etcTexel = (t & 3) * 4 + (t >> 2); // BC: row by row, EAC: column by column
			block |= index << (45 - etcTexel * 3);
		}
		return block;
	}

	/* ETC2 RGB */

	struct ETCColorTarget
	{
		sint32 rgb[4][3];        // palette colors
		uint8 entry[16];         // palette entry of each texel, ETC texel order
		uint8 transparentMask;   // palette entries that are transparent (BC1 three color mode, entry 3)
	};

	struct ETCEncoderState
	{
		const ETCColorTarget* target;
		bool punchthrough;      // ETC2 RGB8A1: no individual mode, bit 33 is the opaque flag
		bool transparentMode;   // punchthrough with transparent texels: opaque flag 0, pixel index 2 is transparent
		uint32 usedMask;
		uint32 counts[4];
		uint32 subblockCounts[2][2][4]; // [flip][subblock][entry]
		uint64 bestError;
		uint64 bestBits;
	};

	inline sint32 SubblockOfTexel(sint32 texel, sint32 flip)
	{
		return flip ? ((texel & 3) >> 1) : (texel >> 3);
	}

	inline uint64 ColorError(const sint32* a, sint32 r, sint32 g, sint32 b)
	{
		const sint32 dr = a[0] - r, dg = a[1] - g, db = a[2] - b;
		return (uint64)(dr * dr + dg * dg + db * db);
	}

	uint64 PackIndices(const uint8 texelIndex[16])
	{
		uint32 lsb = 0, msb = 0;
		for (sint32 i = 0; i < 16; i++)
		{
			lsb |= (uint32)(texelIndex[i] & 1) << i;
			msb |= (uint32)(texelIndex[i] >> 1) << i;
		}
		return ((uint64)msb << 16) | lsb;
	}

	// weighted mean color of the opaque palette entries with counts
	void MeanColor(const ETCColorTarget& target, const uint32* counts, uint32 entryMask, sint32 mean[3])
	{
		uint32 total = 0;
		sint32 sum[3] = {};
		for (sint32 e = 0; e < 4; e++)
		{
			if (!(entryMask & (1u << e)) || counts[e] == 0 || (target.transparentMask & (1u << e)))
				continue;
			total += counts[e];
			for (sint32 c = 0; c < 3; c++)
				sum[c] += target.rgb[e][c] * (sint32)counts[e];
		}
		for (sint32 c = 0; c < 3; c++)
			mean[c] = total ? (sum[c] + (sint32)total / 2) / (sint32)total : 0;
	}

	// best ETC1 modifier table for one subblock with the given base color: error (clamped like the decoder), table and
	// the pixel index of each palette entry
	uint64 ETC1_SubblockFit(const ETCEncoderState& st, const uint32* counts, const sint32 base[3], uint32& tableOut, uint8 entryIndex[4])
	{
		const ETCColorTarget& target = *st.target;
		uint64 best = UINT64_MAX;
		for (uint32 table = 0; table < 8; table++)
		{
			const sint32 a = kETC1Modifiers[table][0];
			const sint32 b = kETC1Modifiers[table][1];
			// transparent mode: index 0 is the base color itself, index 2 is transparent
			const sint32 modifiers[4] = { st.transparentMode ? 0 : a, b, -a, -b };
			uint64 error = 0;
			uint8 indices[4] = {};
			for (sint32 e = 0; e < 4 && error < best; e++)
			{
				if (counts[e] == 0)
					continue;
				if (target.transparentMask & (1u << e))
				{
					indices[e] = 2;
					continue;
				}
				uint64 entryBest = UINT64_MAX;
				for (sint32 k = 0; k < 4; k++)
				{
					if (k == 2 && st.transparentMode)
						continue;
					const sint32 m = modifiers[k];
					const uint64 err = ColorError(target.rgb[e], Clamp255(base[0] + m), Clamp255(base[1] + m), Clamp255(base[2] + m));
					if (err < entryBest)
					{
						entryBest = err;
						indices[e] = (uint8)k;
					}
				}
				error += entryBest * counts[e];
			}
			if (error < best)
			{
				best = error;
				tableOut = table;
				memcpy(entryIndex, indices, 4);
			}
		}
		return best;
	}

	void ConsiderCandidate(ETCEncoderState& st, uint64 error, uint64 bits)
	{
		if (error < st.bestError)
		{
			st.bestError = error;
			st.bestBits = bits;
		}
	}

	// ETC1 differential mode (both flips) and, for ETC2 RGB8, individual mode. Each subblock's base color is the mean of
	// its colors
	void ETC_TryETC1Modes(ETCEncoderState& st)
	{
		const ETCColorTarget& target = *st.target;
		for (sint32 flip = 0; flip < 2; flip++)
		{
			sint32 mean[2][3];
			MeanColor(target, st.subblockCounts[flip][0], 0xF, mean[0]);
			MeanColor(target, st.subblockCounts[flip][1], 0xF, mean[1]);
			// a subblock that only has transparent texels takes the other one's color, which keeps the delta small
			for (sint32 s = 0; s < 2; s++)
			{
				uint32 opaque = 0;
				for (sint32 e = 0; e < 4; e++)
					if (!(target.transparentMask & (1u << e)))
						opaque += st.subblockCounts[flip][s][e];
				if (opaque == 0)
					memcpy(mean[s], mean[s ^ 1], sizeof(mean[s]));
			}
			// differential: 555 base and 333 delta
			{
				sint32 q[2][3], base[2][3], delta[3];
				for (sint32 c = 0; c < 3; c++)
				{
					q[0][c] = s_quant.q5[mean[0][c]];
					q[1][c] = s_quant.q5[mean[1][c]];
					delta[c] = std::clamp(q[1][c] - q[0][c], -4, 3);
					q[1][c] = q[0][c] + delta[c];
					base[0][c] = Expand5(q[0][c]);
					base[1][c] = Expand5(q[1][c]);
				}
				uint32 table[2];
				uint8 entryIndex[2][4];
				uint64 error = ETC1_SubblockFit(st, st.subblockCounts[flip][0], base[0], table[0], entryIndex[0]);
				if (error < st.bestError)
				{
					error += ETC1_SubblockFit(st, st.subblockCounts[flip][1], base[1], table[1], entryIndex[1]);
					if (error < st.bestError)
					{
						uint8 texelIndex[16];
						for (sint32 i = 0; i < 16; i++)
							texelIndex[i] = entryIndex[SubblockOfTexel(i, flip)][target.entry[i]];
						const uint64 opaqueBit = st.transparentMode ? 0 : 1;
						uint64 bits = ((uint64)q[0][0] << 59) | ((uint64)(delta[0] & 7) << 56) | ((uint64)q[0][1] << 51) | ((uint64)(delta[1] & 7) << 48) |
							((uint64)q[0][2] << 43) | ((uint64)(delta[2] & 7) << 40) | ((uint64)table[0] << 37) | ((uint64)table[1] << 34) |
							(opaqueBit << 33) | ((uint64)flip << 32) | PackIndices(texelIndex);
						ConsiderCandidate(st, error, bits);
					}
				}
			}
			// individual: two 444 base colors
			if (!st.punchthrough)
			{
				sint32 q[2][3], base[2][3];
				for (sint32 c = 0; c < 3; c++)
				{
					q[0][c] = s_quant.q4[mean[0][c]];
					q[1][c] = s_quant.q4[mean[1][c]];
					base[0][c] = Expand4(q[0][c]);
					base[1][c] = Expand4(q[1][c]);
				}
				uint32 table[2];
				uint8 entryIndex[2][4];
				uint64 error = ETC1_SubblockFit(st, st.subblockCounts[flip][0], base[0], table[0], entryIndex[0]);
				if (error < st.bestError)
				{
					error += ETC1_SubblockFit(st, st.subblockCounts[flip][1], base[1], table[1], entryIndex[1]);
					if (error < st.bestError)
					{
						uint8 texelIndex[16];
						for (sint32 i = 0; i < 16; i++)
							texelIndex[i] = entryIndex[SubblockOfTexel(i, flip)][target.entry[i]];
						uint64 bits = ((uint64)q[0][0] << 60) | ((uint64)q[1][0] << 56) | ((uint64)q[0][1] << 52) | ((uint64)q[1][1] << 48) |
							((uint64)q[0][2] << 44) | ((uint64)q[1][2] << 40) | ((uint64)table[0] << 37) | ((uint64)table[1] << 34) |
							((uint64)flip << 32) | PackIndices(texelIndex);
						ConsiderCandidate(st, error, bits);
					}
				}
			}
		}
	}

	// planar mode: least squares fit of color(x, y) = O + x (H - O) / 4 + y (V - O) / 4 per channel. Opaque blocks only
	void ETC_TryPlanar(ETCEncoderState& st)
	{
		const ETCColorTarget& target = *st.target;
		sint32 quantized[3][3]; // [channel][O, H, V]
		for (sint32 c = 0; c < 3; c++)
		{
			sint32 sum = 0, sumX = 0, sumY = 0;
			for (sint32 i = 0; i < 16; i++)
			{
				const sint32 v = target.rgb[target.entry[i]][c];
				sum += v;
				sumX += (i >> 2) * v;
				sumY += (i & 3) * v;
			}
			// sum over texels of (x - 1.5)^2 is 20
			const float slopeX = ((float)sumX - 1.5f * (float)sum) / 20.0f;
			const float slopeY = ((float)sumY - 1.5f * (float)sum) / 20.0f;
			const float o = (float)sum / 16.0f - 1.5f * (slopeX + slopeY);
			const float values[3] = { o, o + 4.0f * slopeX, o + 4.0f * slopeY };
			for (sint32 k = 0; k < 3; k++)
			{
				const sint32 v = Clamp255((sint32)std::lround(values[k]));
				quantized[c][k] = c == 1 ? s_quant.q7[v] : s_quant.q6[v];
			}
		}
		sint32 o[3], h[3], v[3];
		for (sint32 c = 0; c < 3; c++)
		{
			auto expand = c == 1 ? Expand7 : Expand6;
			o[c] = expand(quantized[c][0]);
			h[c] = expand(quantized[c][1]);
			v[c] = expand(quantized[c][2]);
		}
		uint64 error = 0;
		for (sint32 i = 0; i < 16 && error < st.bestError; i++)
		{
			const sint32 x = i >> 2, y = i & 3;
			sint32 rgb[3];
			for (sint32 c = 0; c < 3; c++)
				rgb[c] = Clamp255((x * (h[c] - o[c]) + y * (v[c] - o[c]) + 4 * o[c] + 2) >> 2);
			error += ColorError(target.rgb[target.entry[i]], rgb[0], rgb[1], rgb[2]);
		}
		if (error >= st.bestError)
			return;
		const uint64 ro = quantized[0][0], go = quantized[1][0], bo = quantized[2][0];
		const uint64 rh = quantized[0][1], gh = quantized[1][1], bh = quantized[2][1];
		const uint64 rv = quantized[0][2], gv = quantized[1][2], bv = quantized[2][2];
		uint64 bits = (ro << 57) | ((go >> 6) << 56) | ((go & 0x3F) << 49) | ((bo >> 5) << 48) | (((bo >> 3) & 3) << 43) | ((bo & 7) << 39) |
			((rh >> 1) << 34) | (1ull << 33) | ((rh & 1) << 32) | (gh << 25) | (bh << 19) | (rv << 13) | (gv << 6) | bv;
		// R and G must not overflow (R + dR, G + dG in 0..31), B must: bits 63, 55, 47-45 and 42 are free
		if ((bits >> 58) & 1) // dR negative
			bits |= 1ull << 63;
		if ((bits >> 50) & 1) // dG negative
			bits |= 1ull << 55;
		if (((bo >> 3) & 3) + ((bo >> 1) & 3) < 4)
			bits |= 1ull << 42; // B = 0..3, dB = -4..-1
		else
			bits |= 7ull << 45; // B = 28..31, dB = 0..3
		ConsiderCandidate(st, error, bits);
	}

	// T and H modes: two 444 colors and a distance along the gray axis. The palette entries (ordered along the BC
	// color line) are split in two groups whose means become the base colors
	void ETC_TryTH(ETCEncoderState& st)
	{
		const ETCColorTarget& target = *st.target;
		// opaque used entries in line order (BC palette: color0, 2/3 color0 + 1/3 color1, ..., color1), duplicates merged
		sint32 list[4];
		uint32 listCounts[4];
		sint32 listSize = 0;
		static const sint32 lineOrder[4] = { 0, 2, 3, 1 };
		for (sint32 n = 0; n < 4; n++)
		{
			const sint32 e = lineOrder[n];
			if (st.counts[e] == 0 || (target.transparentMask & (1u << e)))
				continue;
			bool merged = false;
			for (sint32 j = 0; j < listSize && !merged; j++)
			{
				if (memcmp(target.rgb[list[j]], target.rgb[e], sizeof(target.rgb[e])) == 0)
				{
					listCounts[j] += st.counts[e];
					merged = true;
				}
			}
			if (!merged)
			{
				list[listSize] = e;
				listCounts[listSize] = st.counts[e];
				listSize++;
			}
		}
		if (listSize < 2)
			return;

		uint64 bestError = st.bestError;
		bool bestIsH = false;
		sint32 bestC[2][3] = {};
		sint32 bestDistance = 0;
		uint8 bestListIndex[4] = {};

		// paints: T [C1, C2 + d, C2, C2 - d], H [C1 + d, C1 - d, C2 + d, C2 - d]. Index 2 is transparent in transparent mode
		auto evaluate = [&](bool isH, const sint32 q1[3], const sint32 q2[3], sint32 distanceIndex) {
			const sint32 d = kETC2Distances[distanceIndex];
			sint32 c1[3], c2[3];
			for (sint32 c = 0; c < 3; c++)
			{
				c1[c] = Expand4(q1[c]);
				c2[c] = Expand4(q2[c]);
			}
			sint32 paints[4][3];
			for (sint32 c = 0; c < 3; c++)
			{
				if (isH)
				{
					paints[0][c] = Clamp255(c1[c] + d);
					paints[1][c] = Clamp255(c1[c] - d);
				}
				else
				{
					paints[0][c] = c1[c];
					paints[1][c] = Clamp255(c2[c] + d);
				}
				paints[2][c] = isH ? Clamp255(c2[c] + d) : c2[c];
				paints[3][c] = Clamp255(c2[c] - d);
			}
			uint64 error = 0;
			uint8 listIndex[4];
			for (sint32 j = 0; j < listSize && error < bestError; j++)
			{
				uint64 entryBest = UINT64_MAX;
				for (sint32 k = 0; k < 4; k++)
				{
					if (k == 2 && st.transparentMode)
						continue;
					const uint64 err = ColorError(target.rgb[list[j]], paints[k][0], paints[k][1], paints[k][2]);
					if (err < entryBest)
					{
						entryBest = err;
						listIndex[j] = (uint8)k;
					}
				}
				error += entryBest * listCounts[j];
			}
			if (error < bestError)
			{
				bestError = error;
				bestIsH = isH;
				memcpy(bestC[0], q1, sizeof(bestC[0]));
				memcpy(bestC[1], q2, sizeof(bestC[1]));
				bestDistance = distanceIndex;
				memcpy(bestListIndex, listIndex, sizeof(listIndex));
			}
		};

		auto groupMean = [&](uint32 listMask, sint32 q[3]) {
			uint32 counts[4] = {};
			uint32 entryMask = 0;
			for (sint32 j = 0; j < listSize; j++)
			{
				if (listMask & (1u << j))
				{
					counts[list[j]] = listCounts[j];
					entryMask |= 1u << list[j];
				}
			}
			sint32 mean[3];
			MeanColor(target, counts, entryMask, mean);
			for (sint32 c = 0; c < 3; c++)
				q[c] = s_quant.q4[mean[c]];
		};

		const uint32 allMask = (1u << listSize) - 1;
		// T: one entry is C1, the others are around C2
		for (sint32 j = 0; j < listSize; j++)
		{
			sint32 q1[3], q2[3];
			groupMean(1u << j, q1);
			groupMean(allMask & ~(1u << j), q2);
			for (sint32 distanceIndex = 0; distanceIndex < 8; distanceIndex++)
				evaluate(false, q1, q2, distanceIndex);
		}
		// H: the line is split in two. The lowest distance bit is (C1 >= C2), so each order covers half the distances
		for (sint32 split = 1; split < listSize; split++)
		{
			sint32 qa[3], qb[3];
			groupMean((1u << split) - 1, qa);
			groupMean(allMask & ~((1u << split) - 1), qb);
			const uint32 a = (uint32)((qa[0] << 8) | (qa[1] << 4) | qa[2]);
			const uint32 b = (uint32)((qb[0] << 8) | (qb[1] << 4) | qb[2]);
			for (sint32 distanceIndex = 0; distanceIndex < 8; distanceIndex++)
			{
				const bool firstIsC1 = (uint32)(a >= b) == (uint32)(distanceIndex & 1);
				if (firstIsC1)
					evaluate(true, qa, qb, distanceIndex);
				else if (a != b)
					evaluate(true, qb, qa, distanceIndex);
			}
		}
		if (bestError >= st.bestError)
			return;

		uint8 entryIndex[4] = { 2, 2, 2, 2 }; // transparent entries
		for (sint32 e = 0; e < 4; e++)
		{
			if (target.transparentMask & (1u << e))
				continue;
			for (sint32 j = 0; j < listSize; j++)
				if (memcmp(target.rgb[list[j]], target.rgb[e], sizeof(target.rgb[e])) == 0)
					entryIndex[e] = bestListIndex[j];
		}
		uint8 texelIndex[16];
		for (sint32 i = 0; i < 16; i++)
			texelIndex[i] = entryIndex[target.entry[i]];
		const uint64 opaqueBit = st.transparentMode ? 0 : 1;
		const uint64 r1 = bestC[0][0], g1 = bestC[0][1], b1 = bestC[0][2];
		const uint64 r2 = bestC[1][0], g2 = bestC[1][1], b2 = bestC[1][2];
		uint64 bits;
		if (!bestIsH)
		{
			bits = ((r1 >> 2) << 59) | ((r1 & 3) << 56) | (g1 << 52) | (b1 << 48) | (r2 << 44) | (g2 << 40) | (b2 << 36) |
				((uint64)(bestDistance >> 1) << 34) | (opaqueBit << 33) | ((uint64)(bestDistance & 1) << 32) | PackIndices(texelIndex);
			// R + dR must overflow: bits 63-61 and 58 are free
			if ((r1 >> 2) + (r1 & 3) < 4)
				bits |= 1ull << 58; // R = 0..3, dR = -4..-1
			else
				bits |= 7ull << 61; // R = 28..31, dR = 0..3
		}
		else
		{
			bits = (r1 << 59) | ((g1 >> 1) << 56) | ((g1 & 1) << 52) | ((b1 >> 3) << 51) | (((b1 >> 1) & 3) << 48) | ((b1 & 1) << 47) |
				(r2 << 43) | ((g2 >> 1) << 40) | ((g2 & 1) << 39) | (b2 << 35) | ((uint64)(bestDistance >> 2) << 34) | (opaqueBit << 33) |
				((uint64)((bestDistance >> 1) & 1) << 32) | PackIndices(texelIndex);
			// R + dR must not overflow (bit 63 free), G + dG must (bits 55-53 and 50 free)
			if ((g1 >> 3) & 1) // dR negative
				bits |= 1ull << 63;
			const uint64 gLow = ((g1 & 1) << 1) | (b1 >> 3);
			const uint64 dgLow = (b1 >> 1) & 3;
			if (gLow + dgLow < 4)
				bits |= 1ull << 50;
			else
				bits |= 7ull << 53;
		}
		ConsiderCandidate(st, bestError, bits);
	}

	// T/H are only tried when the other modes leave more than this squared error per opaque texel (RMS ~3.5 per channel)
	constexpr uint64 kTHThresholdPerTexel = 36;

	uint64 ETC2_EncodeColor(const ETCColorTarget& target, bool punchthrough)
	{
		ETCEncoderState st{};
		st.target = &target;
		st.punchthrough = punchthrough;
		for (sint32 i = 0; i < 16; i++)
		{
			const sint32 e = target.entry[i];
			st.usedMask |= 1u << e;
			st.counts[e]++;
			st.subblockCounts[0][SubblockOfTexel(i, 0)][e]++;
			st.subblockCounts[1][SubblockOfTexel(i, 1)][e]++;
		}
		const uint32 usedTransparent = st.usedMask & target.transparentMask;
		st.transparentMode = punchthrough && usedTransparent != 0;
		if (st.transparentMode && usedTransparent == st.usedMask)
		{
			// fully transparent: differential mode, opaque flag 0, every pixel index 2
			return 0xFFFF0000ull;
		}
		st.bestError = UINT64_MAX;
		st.bestBits = 0;
		ETC_TryETC1Modes(st);
		if (!st.transparentMode && st.bestError > 0)
			ETC_TryPlanar(st);
		uint32 opaqueTexels = 0;
		for (sint32 e = 0; e < 4; e++)
			if (!(target.transparentMask & (1u << e)))
				opaqueTexels += st.counts[e];
		if (st.bestError > kTHThresholdPerTexel * opaqueTexels)
			ETC_TryTH(st);
		return st.bestBits;
	}

	// 8 bit palette of a BC 565 color block: the float palette of the BC decoders rounded (the RGBA8 decode truncates)
	void BC_ColorTarget(const uint8* colorBlock, bool allowThreeColor, ETCColorTarget& target)
	{
		const uint32 c0 = colorBlock[0] | (colorBlock[1] << 8);
		const uint32 c1 = colorBlock[2] | (colorBlock[3] << 8);
		const sint32 e[2][3] = { { (sint32)(c0 >> 11) & 0x1F, (sint32)(c0 >> 5) & 0x3F, (sint32)c0 & 0x1F }, { (sint32)(c1 >> 11) & 0x1F, (sint32)(c1 >> 5) & 0x3F, (sint32)c1 & 0x1F } };
		const sint32 maxValue[3] = { 31, 63, 31 };
		const bool fourColors = !allowThreeColor || c0 > c1;
		for (sint32 c = 0; c < 3; c++)
		{
			const sint32 m = maxValue[c];
			// round(v * 255 / m) and round((k0 * v0 + k1 * v1) * 255 / (m * (k0 + k1)))
			auto mix = [m](sint32 numerator, sint32 parts) { return (numerator * 255 * 2 + m * parts) / (2 * m * parts); };
			target.rgb[0][c] = mix(e[0][c], 1);
			target.rgb[1][c] = mix(e[1][c], 1);
			if (fourColors)
			{
				target.rgb[2][c] = mix(2 * e[0][c] + e[1][c], 3);
				target.rgb[3][c] = mix(e[0][c] + 2 * e[1][c], 3);
			}
			else
			{
				target.rgb[2][c] = mix(e[0][c] + e[1][c], 2);
				target.rgb[3][c] = 0;
			}
		}
		target.transparentMask = fourColors ? 0 : (1u << 3);
		for (sint32 y = 0; y < 4; y++)
		{
			const uint8 rowBits = colorBlock[4 + y];
			for (sint32 x = 0; x < 4; x++)
				target.entry[x * 4 + y] = (rowBits >> (x * 2)) & 3;
		}
	}

	// encoded blocks of recently seen BC color blocks, per thread (flat areas and repeated tiles are common)
	struct ColorBlockCache
	{
		static constexpr sint32 kSize = 4096;
		uint64 key[2][kSize];
		uint64 value[2][kSize];
		uint8 valid[2][kSize];
	};
	thread_local std::unique_ptr<ColorBlockCache> t_colorCache;

	uint64 ETC2_TranscodeColorBlock(const uint8* colorBlock, bool punchthrough)
	{
		uint64 key;
		memcpy(&key, colorBlock, sizeof(key));
		if (!t_colorCache)
			t_colorCache = std::make_unique<ColorBlockCache>(); // valid[] zeroed by value initialization
		ColorBlockCache& cache = *t_colorCache;
		const sint32 kind = punchthrough ? 1 : 0;
		const uint32 slot = (uint32)((key * 0x9E3779B97F4A7C15ull) >> 52);
		if (cache.valid[kind][slot] && cache.key[kind][slot] == key)
			return cache.value[kind][slot];
		ETCColorTarget target;
		BC_ColorTarget(colorBlock, punchthrough, target);
		const uint64 bits = ETC2_EncodeColor(target, punchthrough);
		cache.key[kind][slot] = key;
		cache.value[kind][slot] = bits;
		cache.valid[kind][slot] = 1;
		return bits;
	}

	// BC2: explicit 4 bit alpha per texel to ETC2 alpha (8 distinct values at most)
	uint64 EAC_TranscodeBC2Alpha(const uint8* alphaBlock)
	{
		sint32 targets[16];
		uint32 weights[16] = {};
		for (sint32 v = 0; v < 16; v++)
			targets[v] = v * 17;
		uint8 texelCode[16];
		for (sint32 t = 0; t < 16; t++)
		{
			texelCode[t] = (alphaBlock[t / 2] >> ((t & 1) * 4)) & 0xF;
			weights[texelCode[t]]++;
		}
		// one alpha for the whole block (usually opaque): table 13 has a zero modifier at index 4
		if (weights[texelCode[0]] == 16)
		{
			uint64 block = ((uint64)targets[texelCode[0]] << 56) | (1ull << 52) | (13ull << 48);
			for (sint32 t = 0; t < 16; t++)
				block |= 4ull << (45 - t * 3);
			return block;
		}
		EACFit fit;
		EAC_Fit(EACMode::Alpha8, targets, weights, 16, false, fit);
		uint8 indices[16];
		sint32 distances[16];
		EAC_NearestIndices(EACMode::Alpha8, fit, targets, 16, indices, distances);
		uint64 block = ((uint64)fit.base << 56) | ((uint64)fit.multiplier << 52) | ((uint64)fit.table << 48);
		for (sint32 t = 0; t < 16; t++)
		{
			const sint32 etcTexel = (t & 3) * 4 + (t >> 2);
			block |= (uint64)indices[texelCode[t]] << (45 - etcTexel * 3);
		}
		return block;
	}
}

void transcodeBC4Block_ToEAC_R11(const uint8* bc4, uint8* eac, bool isSigned)
{
	StoreBigEndian64(eac, EAC_TranscodeGroup(isSigned ? EACMode::R11Signed : EACMode::R11Unsigned, bc4, false));
}

void transcodeBC5Block_ToEAC_RG11(const uint8* bc5, uint8* eac, bool isSigned)
{
	const EACMode mode = isSigned ? EACMode::R11Signed : EACMode::R11Unsigned;
	StoreBigEndian64(eac, EAC_TranscodeGroup(mode, bc5, true));
	StoreBigEndian64(eac + 8, EAC_TranscodeGroup(mode, bc5 + 8, true));
}

void transcodeBC1Block_ToETC2_RGB8A1(const uint8* bc1, uint8* etc)
{
	StoreBigEndian64(etc, ETC2_TranscodeColorBlock(bc1, true));
}

void transcodeBC2Block_ToETC2_RGBA8(const uint8* bc2, uint8* etc)
{
	StoreBigEndian64(etc, EAC_TranscodeBC2Alpha(bc2));
	StoreBigEndian64(etc + 8, ETC2_TranscodeColorBlock(bc2 + 8, false));
}

void transcodeBC3Block_ToETC2_RGBA8(const uint8* bc3, uint8* etc)
{
	StoreBigEndian64(etc, EAC_TranscodeGroup(EACMode::Alpha8, bc3, false));
	StoreBigEndian64(etc + 8, ETC2_TranscodeColorBlock(bc3 + 8, false));
}
