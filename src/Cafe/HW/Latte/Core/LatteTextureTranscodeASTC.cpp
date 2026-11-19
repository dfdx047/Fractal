#include "Cafe/HW/Latte/Core/LatteTextureTranscodeASTC.h"

// ASTC encoding as in the Khronos Data Format Specification (ASTC section): block mode, integer sequence encoding,
// color endpoint and weight unquantization, endpoint modes 4 (luminance + alpha), 8 (RGB) and 12 (RGBA), dual plane
// and void extent. Only single partition blocks with a 4x4 weight grid are produced, so each texel has its own weight

namespace
{
	using u128 = unsigned __int128;

	// quantization methods in spec order. levels = 2^bits, 3 * 2^bits (trit) or 5 * 2^bits (quint)
	enum : uint32 { Q2, Q3, Q4, Q5, Q6, Q8, Q10, Q12, Q16, Q20, Q24, Q32, Q40, Q48, Q64, Q80, Q96, Q128, Q160, Q192, Q256 };

	struct AstcQuantInfo
	{
		uint16 levels;
		uint8 bits;
		bool trit;
		bool quint;
	};

	constexpr AstcQuantInfo kQuantInfo[21] = {
		{ 2, 1, false, false }, { 3, 0, true, false }, { 4, 2, false, false }, { 5, 0, false, true }, { 6, 1, true, false },
		{ 8, 3, false, false }, { 10, 1, false, true }, { 12, 2, true, false }, { 16, 4, false, false }, { 20, 2, false, true },
		{ 24, 3, true, false }, { 32, 5, false, false }, { 40, 3, false, true }, { 48, 4, true, false }, { 64, 6, false, false },
		{ 80, 4, false, true }, { 96, 5, true, false }, { 128, 7, false, false }, { 160, 5, false, true }, { 192, 6, true, false },
		{ 256, 8, false, false } };

	constexpr uint32 IseBitCount(uint32 count, uint32 q)
	{
		const AstcQuantInfo& info = kQuantInfo[q];
		return count * info.bits + (info.trit ? (8 * count + 4) / 5 : 0) + (info.quint ? (7 * count + 2) / 3 : 0);
	}

	// color endpoint range of a single partition block: the largest one whose values fit between the 17 configuration
	// bits and the weights (and the 2 bit plane 2 component selector)
	constexpr uint32 ColorQuantFor(uint32 valueCount, uint32 weightQuant, bool dualPlane)
	{
		const sint32 colorBits = 128 - 17 - (sint32)IseBitCount(dualPlane ? 32 : 16, weightQuant) - (dualPlane ? 2 : 0);
		for (sint32 q = Q256; q >= 0; q--)
		{
			if ((sint32)IseBitCount(valueCount, q) <= colorBits)
				return (uint32)q;
		}
		return ~0u;
	}

	// 11 bit block mode of a 4x4 weight grid. Layout with bits 1:0 != 0 and bits 3:2 = 0: grid width B + 4 (B = bits 8:7),
	// height A + 2 (A = bits 6:5), weight range R = (bit 1, bit 0, bit 4) with H = bit 9, dual plane D = bit 10
	constexpr uint32 BlockMode4x4(uint32 weightQuant, bool dualPlane)
	{
		const uint32 h = weightQuant >= 6 ? 1 : 0;
		const uint32 r = weightQuant - 6 * h + 2;
		return ((r >> 1) & 3) | ((r & 1) << 4) | (2 << 5) | (h << 9) | ((dualPlane ? 1u : 0u) << 10);
	}

	// the encodings used below. Weights must take 24 to 96 bits, color ranges must have at least 6 levels
	static_assert(BlockMode4x4(Q4, false) == 0x42 && BlockMode4x4(Q8, false) == 0x53 && BlockMode4x4(Q3, false) == 0x51);
	static_assert(ColorQuantFor(6, Q4, false) == Q256); // BC1 four colors (mode 8)
	static_assert(ColorQuantFor(8, Q4, false) == Q256); // constant alpha (mode 12)
	static_assert(ColorQuantFor(6, Q3, false) == Q256); // BC1 three colors
	static_assert(ColorQuantFor(8, Q3, true) == Q128);  // BC1 transparent + colors, two planes
	static_assert(ColorQuantFor(8, Q4, true) == Q48);   // BC2/BC3 two planes
	static_assert(ColorQuantFor(8, Q8, false) == Q192); // constant color, 8 alpha levels
	static_assert(ColorQuantFor(8, Q16, false) == Q48); // constant color, 16 alpha levels
	static_assert(ColorQuantFor(4, Q16, false) == Q256 && ColorQuantFor(4, Q24, false) == Q256 && ColorQuantFor(4, Q32, false) == Q192); // gray
	static_assert(IseBitCount(16, Q3) >= 24 && IseBitCount(32, Q4) <= 96 && IseBitCount(16, Q32) <= 96);

	// trits of an 8 bit packed group of five (spec: trit decoding)
	void DecodeTrits(uint32 T, uint32 t[5])
	{
		uint32 C;
		if (((T >> 2) & 7) == 7)
		{
			C = ((T >> 5) << 2) | (T & 3);
			t[4] = 2;
			t[3] = 2;
		}
		else
		{
			C = T & 0x1F;
			if (((T >> 5) & 3) == 3)
			{
				t[4] = 2;
				t[3] = (T >> 7) & 1;
			}
			else
			{
				t[4] = (T >> 7) & 1;
				t[3] = (T >> 5) & 3;
			}
		}
		if ((C & 3) == 3)
		{
			t[2] = 2;
			t[1] = (C >> 4) & 1;
			t[0] = (((C >> 3) & 1) << 1) | ((C >> 2) & 1 & ~(C >> 3) & 1);
		}
		else if (((C >> 2) & 3) == 3)
		{
			t[2] = 2;
			t[1] = 2;
			t[0] = C & 3;
		}
		else
		{
			t[2] = (C >> 4) & 1;
			t[1] = (C >> 2) & 3;
			t[0] = (((C >> 1) & 1) << 1) | (C & 1 & ~(C >> 1) & 1);
		}
	}

	// packed byte of five trits t0 + 3 t1 + 9 t2 + 27 t3 + 81 t4
	const std::array<uint8, 243> s_tritEncode = []()
	{
		std::array<uint8, 243> table{};
		std::array<bool, 243> found{};
		for (uint32 T = 0; T < 256; T++)
		{
			uint32 t[5];
			DecodeTrits(T, t);
			uint32 index = t[0] + 3 * t[1] + 9 * t[2] + 27 * t[3] + 81 * t[4];
			if (!found[index])
			{
				found[index] = true;
				table[index] = (uint8)T;
			}
		}
		return table;
	}();

	// color endpoint unquantization to 8 bits (spec: color endpoint unquantization)
	uint32 ColorUnquantize(uint32 q, uint32 v)
	{
		const AstcQuantInfo& info = kQuantInfo[q];
		const sint32 n = info.bits;
		if (!info.trit && !info.quint)
		{
			// bit replication
			uint32 r = 0;
			for (sint32 shift = 8 - n; shift > -n; shift -= n)
				r |= shift >= 0 ? (v << shift) : (v >> -shift);
			return r & 0xFF;
		}
		const uint32 m = v & ((1u << n) - 1);
		const uint32 D = v >> n;
		const uint32 A = (m & 1) ? 0x1FF : 0;
		const uint32 b = (m >> 1) & 1, c = (m >> 2) & 1, d = (m >> 3) & 1, e = (m >> 4) & 1, f = (m >> 5) & 1;
		uint32 B = 0, C = 0;
		if (info.trit)
		{
			switch (n)
			{
			case 1: C = 204; break;
			case 2: C = 93; B = b * 0x116; break; // b000b0bb0
			case 3: C = 44; B = c * 0x10A + b * 0x85; break; // cb000cbcb
			case 4: C = 22; B = d * 0x104 + c * 0x82 + b * 0x41; break; // dcb000dcb
			case 5: C = 11; B = e * 0x102 + d * 0x81 + c * 0x40 + b * 0x20; break; // edcb000ed
			case 6: C = 5; B = f * 0x101 + e * 0x80 + d * 0x40 + c * 0x20 + b * 0x10; break; // fedcb000f
			}
		}
		else
		{
			switch (n)
			{
			case 1: C = 113; break;
			case 2: C = 54; B = b * 0x10C; break; // b0000bb00
			case 3: C = 26; B = c * 0x105 + b * 0x82; break; // cb0000cbc
			case 4: C = 13; B = d * 0x102 + c * 0x81 + b * 0x40; break; // dcb0000dc
			case 5: C = 6; B = e * 0x101 + d * 0x80 + c * 0x40 + b * 0x20; break; // edcb0000e
			}
		}
		uint32 T = D * C + B;
		T ^= A;
		return (A & 0x80) | (T >> 2);
	}

	// weight unquantization to 0..64 (spec: weight unquantization), for the ranges used here
	uint32 WeightUnquantize(uint32 q, uint32 v)
	{
		const AstcQuantInfo& info = kQuantInfo[q];
		const sint32 n = info.bits;
		uint32 w;
		if (!info.trit && !info.quint)
		{
			// bit replication to 6 bits
			w = 0;
			for (sint32 shift = 6 - n; shift > -n; shift -= n)
				w |= shift >= 0 ? (v << shift) : (v >> -shift);
			w &= 0x3F;
		}
		else if (n == 0)
		{
			static constexpr uint8 kTrit[3] = { 0, 32, 63 };
			static constexpr uint8 kQuint[5] = { 0, 16, 32, 47, 63 };
			w = info.trit ? kTrit[v] : kQuint[v];
		}
		else
		{
			const uint32 m = v & ((1u << n) - 1);
			const uint32 D = v >> n;
			const uint32 A = (m & 1) ? 0x7F : 0;
			const uint32 b = (m >> 1) & 1, c = (m >> 2) & 1;
			uint32 B = 0, C = 0;
			if (info.trit)
			{
				switch (n)
				{
				case 1: C = 50; break;
				case 2: C = 23; B = b * 0x45; break; // b000b0b
				case 3: C = 11; B = c * 0x42 + b * 0x21; break; // cb000cb
				}
			}
			else
			{
				switch (n)
				{
				case 1: C = 28; break;
				case 2: C = 13; B = b * 0x42; break; // b0000b0
				}
			}
			uint32 T = D * C + B;
			T ^= A;
			w = (A & 0x20) | (T >> 2);
		}
		return w > 32 ? w + 1 : w;
	}

	// per color range used here: 8 bit value -> nearest encodable value (ISE symbol) and symbol -> 8 bit value
	struct ColorQuantTable
	{
		uint8 encode[256];
		uint8 decode[256];
	};

	ColorQuantTable BuildColorQuantTable(uint32 q)
	{
		ColorQuantTable t{};
		const uint32 levels = kQuantInfo[q].levels;
		for (uint32 v = 0; v < levels; v++)
			t.decode[v] = (uint8)ColorUnquantize(q, v);
		for (uint32 x = 0; x < 256; x++)
		{
			uint32 best = 0, bestError = ~0u;
			for (uint32 v = 0; v < levels; v++)
			{
				uint32 error = (uint32)std::abs((sint32)t.decode[v] - (sint32)x);
				if (error < bestError)
				{
					bestError = error;
					best = v;
				}
			}
			t.encode[x] = (uint8)best;
		}
		return t;
	}

	const ColorQuantTable s_color48 = BuildColorQuantTable(Q48);
	const ColorQuantTable s_color128 = BuildColorQuantTable(Q128);
	const ColorQuantTable s_color192 = BuildColorQuantTable(Q192);
	const ColorQuantTable s_color256 = BuildColorQuantTable(Q256);

	inline const ColorQuantTable& ColorTable(uint32 q)
	{
		switch (q)
		{
		case Q48: return s_color48;
		case Q128: return s_color128;
		case Q192: return s_color192;
		default: return s_color256;
		}
	}

	// weight levels of a range (0..64) with their ISE symbols
	struct WeightLevels
	{
		uint32 count;
		uint8 value[32];
		uint8 symbol[32];
	};

	WeightLevels BuildWeightLevels(uint32 q)
	{
		WeightLevels l{};
		l.count = kQuantInfo[q].levels;
		for (uint32 v = 0; v < l.count; v++)
		{
			l.value[v] = (uint8)WeightUnquantize(q, v);
			l.symbol[v] = (uint8)v;
		}
		return l;
	}

	const WeightLevels s_weights4 = BuildWeightLevels(Q4);
	const WeightLevels s_weights8 = BuildWeightLevels(Q8);
	const WeightLevels s_weights16 = BuildWeightLevels(Q16);
	const WeightLevels s_weights24 = BuildWeightLevels(Q24);
	const WeightLevels s_weights32 = BuildWeightLevels(Q32);

	inline const WeightLevels& WeightTable(uint32 q)
	{
		switch (q)
		{
		case Q4: return s_weights4;
		case Q8: return s_weights8;
		case Q16: return s_weights16;
		case Q24: return s_weights24;
		default: return s_weights32;
		}
	}

	// the values the decoder interpolates (UNORM16) between two 8 bit endpoints at each weight level of a range
	struct InterpolatedLevels
	{
		const WeightLevels& levels;
		sint32 value[32];

		InterpolatedLevels(uint32 a0, uint32 a1, const WeightLevels& l) : levels(l)
		{
			for (uint32 i = 0; i < l.count; i++)
			{
				const uint32 w = l.value[i];
				value[i] = (sint32)((a0 * 257 * (64 - w) + a1 * 257 * w + 32) >> 6);
			}
		}

		// weight symbol closest to an 8 bit target, the squared UNORM16 error to errorOut
		uint32 Best(uint32 target, uint64* errorOut = nullptr) const
		{
			const sint32 t16 = (sint32)(target * 257);
			uint32 best = 0;
			sint32 bestError = std::abs(value[0] - t16);
			for (uint32 i = 1; i < levels.count; i++)
			{
				const sint32 error = std::abs(value[i] - t16);
				if (error < bestError)
				{
					bestError = error;
					best = i;
				}
			}
			if (errorOut)
				*errorOut = (uint64)((sint64)bestError * bestError);
			return levels.symbol[best];
		}
	};

	inline uint32 ReverseBits32(uint32 v)
	{
#if __has_builtin(__builtin_bitreverse32)
		return __builtin_bitreverse32(v);
#else
		v = ((v >> 1) & 0x55555555) | ((v & 0x55555555) << 1);
		v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) << 2);
		v = ((v >> 4) & 0x0F0F0F0F) | ((v & 0x0F0F0F0F) << 4);
		return __builtin_bswap32(v);
#endif
	}

	inline uint64 ReverseBits64(uint64 v)
	{
#if __has_builtin(__builtin_bitreverse64)
		return __builtin_bitreverse64(v);
#else
		return ((uint64)ReverseBits32((uint32)v) << 32) | ReverseBits32((uint32)(v >> 32));
#endif
	}

	inline void StoreBlock(uint8* out, u128 block)
	{
		const uint64 lo = (uint64)block, hi = (uint64)(block >> 64);
		memcpy(out, &lo, 8);
		memcpy(out + 8, &hi, 8);
	}

	// count values of range q as an integer sequence, LSB first (spec: integer sequence encoding). Trits are packed in
	// groups of five: m0 T[1:0] m1 T[3:2] m2 T[4] m3 T[6:5] m4 T[7]; the bits of a missing tail are cut off
	u128 IseEncode(const uint8* values, uint32 count, uint32 q)
	{
		const AstcQuantInfo& info = kQuantInfo[q];
		const uint32 n = info.bits;
		u128 stream = 0;
		uint32 pos = 0;
		if (info.trit)
		{
			const uint32 mask = (1u << n) - 1;
			for (uint32 i = 0; i < count; i += 5)
			{
				uint32 m[5] = {}, t[5] = {};
				for (uint32 j = 0; j < 5 && i + j < count; j++)
				{
					m[j] = values[i + j] & mask;
					t[j] = values[i + j] >> n;
				}
				const uint64 T = s_tritEncode[t[0] + 3 * t[1] + 9 * t[2] + 27 * t[3] + 81 * t[4]];
				// one group is at most 5 * 6 + 8 = 38 bits
				uint64 group = m[0];
				uint32 p = n;
				group |= (T & 3) << p; p += 2;
				group |= (uint64)m[1] << p; p += n;
				group |= ((T >> 2) & 3) << p; p += 2;
				group |= (uint64)m[2] << p; p += n;
				group |= ((T >> 4) & 1) << p; p += 1;
				group |= (uint64)m[3] << p; p += n;
				group |= ((T >> 5) & 3) << p; p += 2;
				group |= (uint64)m[4] << p; p += n;
				group |= (T >> 7) << p; p += 1;
				stream |= (u128)group << pos;
				pos += p;
			}
		}
		else
		{
			for (uint32 i = 0; i < count; i++, pos += n)
				stream |= (u128)values[i] << pos;
		}
		return stream & (((u128)1 << IseBitCount(count, q)) - 1);
	}

	// IseEncode of 8 values of a trit range with n bits (n <= 6: at most 61 bits): a group of five and one of three
	template<uint32 n>
	inline uint64 IseEncodeTrits8(const uint8* v)
	{
		constexpr uint32 mask = (1u << n) - 1;
		const uint64 T0 = s_tritEncode[(v[0] >> n) + 3 * (v[1] >> n) + 9 * (v[2] >> n) + 27 * (v[3] >> n) + 81 * (v[4] >> n)];
		const uint64 T1 = s_tritEncode[(v[5] >> n) + 3 * (v[6] >> n) + 9 * (v[7] >> n)];
		uint64 s = 0;
		uint32 p = 0;
		auto put = [&](uint64 value, uint32 bits)
		{
			s |= value << p;
			p += bits;
		};
		put(v[0] & mask, n); put(T0 & 3, 2); put(v[1] & mask, n); put((T0 >> 2) & 3, 2); put(v[2] & mask, n);
		put((T0 >> 4) & 1, 1); put(v[3] & mask, n); put((T0 >> 5) & 3, 2); put(v[4] & mask, n); put(T0 >> 7, 1);
		// second group: three values, T1[7:5] is zero for zero t3 and t4 and cut off
		put(v[5] & mask, n); put(T1 & 3, 2); put(v[6] & mask, n); put((T1 >> 2) & 3, 2); put(v[7] & mask, n); put((T1 >> 4) & 1, 1);
		return s;
	}
	static_assert(IseBitCount(8, Q48) == 8 * 4 + 13 && IseBitCount(8, Q192) == 8 * 6 + 13);

	// the weight sequence is stored bit reversed from bit 127 down
	inline u128 PlaceWeights(u128 stream)
	{
		return ((u128)ReverseBits64((uint64)stream) << 64) | ReverseBits64((uint64)(stream >> 64));
	}

	// a single partition block with a 4x4 weight grid. Two planes: the second one is alpha (component selector 3)
	struct BlockParams
	{
		uint32 cem; // 4 (luminance + alpha), 8 (RGB) or 12 (RGBA)
		uint32 weightQuant;
		bool dualPlane;
		uint8 values[8]; // color endpoint ISE symbols: l0 l1 a0 a1 (mode 4), r0 r1 g0 g1 b0 b1 (a0 a1)
		uint8 weights[32]; // weight ISE symbols per texel; two planes: texel i at 2i (RGB) and 2i + 1 (alpha)
	};

	void WriteBlock(const BlockParams& p, uint8* out)
	{
		const uint32 weightCount = p.dualPlane ? 32 : 16;
		const uint32 weightBits = IseBitCount(weightCount, p.weightQuant);
		const uint32 valueCount = p.cem == 12 ? 8 : (p.cem == 8 ? 6 : 4);
		u128 block = BlockMode4x4(p.weightQuant, p.dualPlane) | (p.cem << 13); // partition count - 1 (bits 11-12) = 0
		block |= IseEncode(p.values, valueCount, ColorQuantFor(valueCount, p.weightQuant, p.dualPlane)) << 17;
		block |= PlaceWeights(IseEncode(p.weights, weightCount, p.weightQuant));
		if (p.dualPlane)
			block |= (u128)3 << (128 - weightBits - 2);
		StoreBlock(out, block);
	}

	// constant color block (void extent without extent coordinates), UNORM16 per component
	inline void WriteVoidExtent(uint32 r, uint32 g, uint32 b, uint32 a, uint8* out)
	{
		const uint64 lo = 0xFFFFFFFFFFFFFDFCull; // block mode 0x1FC, LDR, reserved bits 10-11 set, all extents ones
		const uint64 hi = (uint64)(r * 257) | ((uint64)(g * 257) << 16) | ((uint64)(b * 257) << 32) | ((uint64)(a * 257) << 48);
		memcpy(out, &lo, 8);
		memcpy(out + 8, &hi, 8);
	}

	struct Rgb
	{
		uint32 r, g, b;
		bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
		uint32 Sum() const { return r + g + b; }
	};

	// RGB565 to 8 bits per channel by bit replication (as BCn hardware does)
	inline Rgb Expand565(uint32 c)
	{
		const uint32 r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
		return { (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2) };
	}

	// 8 bit color of a BC palette entry: 0 = c0, 1 = c1, 2 and 3 = the interpolants (four color mode: 2/3 and 1/3,
	// three color mode: entry 2 is the midpoint)
	inline Rgb PaletteColor(const Rgb& e0, const Rgb& e1, uint32 index, bool threeColor)
	{
		if (index == 0)
			return e0;
		if (index == 1)
			return e1;
		if (threeColor)
			return { (e0.r + e1.r + 1) / 2, (e0.g + e1.g + 1) / 2, (e0.b + e1.b + 1) / 2 };
		if (index == 2)
			return { (2 * e0.r + e1.r + 1) / 3, (2 * e0.g + e1.g + 1) / 3, (2 * e0.b + e1.b + 1) / 3 };
		return { (e0.r + 2 * e1.r + 1) / 3, (e0.g + 2 * e1.g + 1) / 3, (e0.b + 2 * e1.b + 1) / 3 };
	}

	// BC four color indices (2 bits per texel, texel i at bit 2i) as 2 bit ASTC weights: index 0, 1, 2, 3 = c0, c1,
	// 2/3 c0 + 1/3 c1, 1/3 c0 + 2/3 c1 -> weight 0, 3, 1, 2 (0, 64, 21, 43 of 64)
	inline uint32 FourColorWeights(uint32 indices)
	{
		const uint32 lo = indices & 0x55555555;
		const uint32 hi = (indices >> 1) & 0x55555555;
		return (lo << 1) | (lo ^ hi);
	}

	// 16 groups of 2 bits to 16 groups of 4 bits (the upper 2 bits of each zero)
	inline uint64 Spread2Bits(uint32 x)
	{
		uint64 v = x;
		v = (v | (v << 16)) & 0x0000FFFF0000FFFFull;
		v = (v | (v << 8)) & 0x00FF00FF00FF00FFull;
		v = (v | (v << 4)) & 0x0F0F0F0F0F0F0F0Full;
		v = (v | (v << 2)) & 0x3333333333333333ull;
		return v;
	}

	// one plane, 2 bit weights from the BC indices, exact 8 bit endpoints (the common case: no search, no tables).
	// alpha < 0: mode 8 (RGB, alpha 1), otherwise mode 12 with that alpha on both endpoints
	inline void WriteFourColorBlock(Rgb e0, Rgb e1, uint32 indices, sint32 alpha, uint8* out)
	{
		uint32 weights = FourColorWeights(indices);
		// with a smaller RGB sum on the second endpoint the decoder swaps them and applies blue contraction
		if (e1.Sum() < e0.Sum())
		{
			std::swap(e0, e1);
			weights = ~weights; // 3 - w
		}
		uint64 colors = e0.r | (e1.r << 8) | (e0.g << 16) | (e1.g << 24) | ((uint64)e0.b << 32) | ((uint64)e1.b << 40);
		uint32 cem = 8;
		if (alpha >= 0)
		{
			colors |= ((uint64)alpha << 48) | ((uint64)alpha << 56);
			cem = 12;
		}
		// 2 bit weights in texel order, the stream reversed into the top 32 bits
		const u128 block = BlockMode4x4(Q4, false) | (cem << 13) | ((u128)colors << 17) | ((u128)ReverseBits32(weights) << 96);
		StoreBlock(out, block);
	}

	// RGB endpoints as ISE symbols of a color range, in the order the decoder wants (endpoint with the smaller RGB sum of
	// unquantized values first, no blue contraction). Returns true if c0 and c1 were swapped
	inline bool QuantizeEndpointsRgb(const ColorQuantTable& t, const Rgb& c0, const Rgb& c1, uint8 values[8])
	{
		uint8 s0[3] = { t.encode[c0.r], t.encode[c0.g], t.encode[c0.b] };
		uint8 s1[3] = { t.encode[c1.r], t.encode[c1.g], t.encode[c1.b] };
		const uint32 sum0 = t.decode[s0[0]] + t.decode[s0[1]] + t.decode[s0[2]];
		const uint32 sum1 = t.decode[s1[0]] + t.decode[s1[1]] + t.decode[s1[2]];
		const bool swap = sum1 < sum0;
		if (swap)
			std::swap(s0, s1);
		for (uint32 i = 0; i < 3; i++)
		{
			values[i * 2 + 0] = s0[i];
			values[i * 2 + 1] = s1[i];
		}
		return swap;
	}

	// BC1 transparent black with two or three opaque colors: two planes, RGB on the c0-c1 line with 3 level weights
	// (0, 1/2, 1) and alpha 0 or 1, 7 bit endpoints. The RGB plane can't reach black off that line, transparent texels
	// take the darker endpoint. Weights: 32 trits = 7 packed bytes (52 bits), colors 8 x 7 bits, selector at bit 74
	void WriteBC1TransparentBlock(const Rgb& e0, const Rgb& e1, uint32 indices, uint8* out)
	{
		const ColorQuantTable& t = s_color128;
		uint8 values[8];
		const bool swap = QuantizeEndpointsRgb(t, e0, e1, values);
		values[6] = swap ? 127 : 0; // alpha 255 / 0
		values[7] = swap ? 0 : 127;
		uint64 colors = 0;
		for (uint32 i = 0; i < 8; i++)
			colors |= (uint64)values[i] << (7 * i);
		// per BC index: (RGB weight, alpha weight) as trits, before a swap: 0 -> c0, 1 -> c1, 2 -> midpoint, 3 -> darker
		// endpoint with alpha 0
		const uint32 darker = e0.Sum() <= e1.Sum() ? 0 : 2;
		uint8 rgbWeight[4] = { 0, 2, 1, (uint8)darker };
		uint8 alphaWeight[4] = { 2, 2, 2, 0 };
		if (swap)
		{
			for (uint32 k = 0; k < 4; k++)
			{
				rgbWeight[k] = 2 - rgbWeight[k];
				alphaWeight[k] = 2 - alphaWeight[k];
			}
		}
		uint8 trits[35] = {};
		for (uint32 i = 0; i < 16; i++)
		{
			const uint32 index = (indices >> (2 * i)) & 3;
			trits[2 * i + 0] = rgbWeight[index];
			trits[2 * i + 1] = alphaWeight[index];
		}
		uint64 stream = 0;
		for (uint32 g = 0; g < 7; g++)
		{
			const uint8* tg = trits + g * 5;
			stream |= (uint64)s_tritEncode[tg[0] + 3 * tg[1] + 9 * tg[2] + 27 * tg[3] + 81 * tg[4]] << (8 * g);
		}
		stream &= (1ull << 52) - 1; // the last group holds 2 trits: 4 bits
		const u128 block = BlockMode4x4(Q3, true) | (12u << 13) | ((u128)colors << 17) | ((u128)3 << 74) | ((u128)ReverseBits64(stream) << 64);
		StoreBlock(out, block);
	}

	// BC1 three color mode (c0 <= c1): c0, c1, their midpoint and transparent black
	void TranscodeBC1ThreeColor(const Rgb& e0, const Rgb& e1, uint32 indices, uint8* out)
	{
		uint32 used = 0;
		for (uint32 i = 0; i < 16; i++)
			used |= 1u << ((indices >> (2 * i)) & 3);
		const uint32 usedOpaque = used & 7;
		if (usedOpaque == 0)
		{
			WriteVoidExtent(0, 0, 0, 0, out);
			return;
		}
		if (e0 == e1 || __builtin_popcount(usedOpaque) == 1)
		{
			// one opaque color
			const Rgb c = PaletteColor(e0, e1, (uint32)__builtin_ctz(usedOpaque), true);
			if (!(used & 8))
			{
				WriteVoidExtent(c.r, c.g, c.b, 255, out);
				return;
			}
			// with transparent black: RGBA endpoints (0,0,0,0) and (c,255), exact. The first endpoint has the smaller RGB
			// sum, never swapped
			BlockParams p{};
			p.cem = 12;
			p.weightQuant = Q4;
			const uint8 values[8] = { 0, (uint8)c.r, 0, (uint8)c.g, 0, (uint8)c.b, 0, 255 };
			memcpy(p.values, values, 8);
			for (uint32 i = 0; i < 16; i++)
				p.weights[i] = ((indices >> (2 * i)) & 3) == 3 ? 0 : 3;
			WriteBlock(p, out);
			return;
		}
		if (used & 8)
		{
			WriteBC1TransparentBlock(e0, e1, indices, out);
			return;
		}
		// c0, c1 and the midpoint: 3 level weights 0, 1/2, 1, exact endpoints
		static constexpr uint8 kThreeColorWeight[4] = { 0, 2, 1, 0 };
		BlockParams p{};
		p.cem = 8;
		p.weightQuant = Q3;
		const bool swap = e1.Sum() < e0.Sum();
		const Rgb& a = swap ? e1 : e0;
		const Rgb& b = swap ? e0 : e1;
		const uint8 values[6] = { (uint8)a.r, (uint8)b.r, (uint8)a.g, (uint8)b.g, (uint8)a.b, (uint8)b.b };
		memcpy(p.values, values, 6);
		for (uint32 i = 0; i < 16; i++)
		{
			const uint32 w = kThreeColorWeight[(indices >> (2 * i)) & 3];
			p.weights[i] = (uint8)(swap ? 2 - w : w);
		}
		WriteBlock(p, out);
	}

	// alpha of a BC2/BC3 block: palette of 8 bit values and the palette entry of each texel
	struct AlphaBlock
	{
		uint8 palette[16];
		uint8 index[16];
		uint32 used; // bit mask of the palette entries in use
	};

	// constant color, varying alpha: one plane driven by alpha. Candidate encodings, the one with the smallest squared
	// error is kept: RGBA endpoints with 8 alpha levels (192 level endpoints) or 16 levels (48 level endpoints), alpha
	// endpoints at the block's extremes or (BC2) at 0 and 255 where the 16 levels fall on BC2's 4 bit steps; gray colors
	// also luminance + alpha endpoints (mode 4) with 16, 24 or 32 levels
	void WriteConstantColorBlock(const Rgb& c, const AlphaBlock& alpha, uint32 alphaMin, uint32 alphaMax, bool isBC3, uint8* out)
	{
		struct Candidate
		{
			uint32 cem, weightQuant, a0, a1;
		};
		Candidate candidates[4];
		uint32 candidateCount = 0;
		const bool gray = c.r == c.g && c.g == c.b;
		if (isBC3)
		{
			candidates[candidateCount++] = { 12, Q8, alphaMin, alphaMax };
			candidates[candidateCount++] = { 12, Q16, alphaMin, alphaMax };
			if (gray)
			{
				candidates[candidateCount++] = { 4, Q24, alphaMin, alphaMax };
				candidates[candidateCount++] = { 4, Q32, alphaMin, alphaMax };
			}
		}
		else
		{
			candidates[candidateCount++] = { 12, Q16, alphaMin, alphaMax };
			candidates[candidateCount++] = { 12, Q16, 0, 255 };
			if (gray)
			{
				candidates[candidateCount++] = { 4, Q16, 0, 255 };
				candidates[candidateCount++] = { 4, Q32, alphaMin, alphaMax };
			}
		}
		uint32 count[16] = {};
		for (uint32 i = 0; i < 16; i++)
			count[alpha.index[i]]++;
		BlockParams best{};
		uint64 bestError = ~0ull;
		for (uint32 n = 0; n < candidateCount; n++)
		{
			const Candidate& cand = candidates[n];
			BlockParams p{};
			p.cem = cand.cem;
			p.weightQuant = cand.weightQuant;
			const ColorQuantTable& t = ColorTable(ColorQuantFor(cand.cem == 4 ? 4 : 8, cand.weightQuant, false));
			uint64 error = 0;
			auto colorError = [&](uint32 v, uint8 symbol)
			{
				const sint64 d = ((sint64)t.decode[symbol] - (sint64)v) * 257;
				return (uint64)(d * d) * 16;
			};
			uint32 aSym0 = t.encode[cand.a0], aSym1 = t.encode[cand.a1];
			if (cand.cem == 4)
			{
				p.values[0] = p.values[1] = t.encode[c.r];
				p.values[2] = (uint8)aSym0;
				p.values[3] = (uint8)aSym1;
				error += 3 * colorError(c.r, p.values[0]);
			}
			else
			{
				// both endpoints have the same RGB, never swapped
				p.values[0] = p.values[1] = t.encode[c.r];
				p.values[2] = p.values[3] = t.encode[c.g];
				p.values[4] = p.values[5] = t.encode[c.b];
				p.values[6] = (uint8)aSym0;
				p.values[7] = (uint8)aSym1;
				error += colorError(c.r, p.values[0]) + colorError(c.g, p.values[2]) + colorError(c.b, p.values[4]);
			}
			const InterpolatedLevels levels(t.decode[aSym0], t.decode[aSym1], WeightTable(cand.weightQuant));
			uint8 entryWeight[16];
			uint64 maxEntryError = 0;
			for (uint32 m = alpha.used; m != 0; m &= m - 1)
			{
				const uint32 k = (uint32)__builtin_ctz(m);
				uint64 e;
				entryWeight[k] = (uint8)levels.Best(alpha.palette[k], &e);
				error += e * count[k];
				maxEntryError = std::max(maxEntryError, e);
			}
			for (uint32 i = 0; i < 16; i++)
				p.weights[i] = entryWeight[alpha.index[i]];
			if (error < bestError)
			{
				bestError = error;
				best = p;
			}
			// every alpha within 1 of its target (BC3's 8 levels between a0 and a1): good enough, skip the rest
			if (maxEntryError <= 257 * 257)
				break;
		}
		WriteBlock(best, out);
	}

	// two planes with 2 bit weights: RGB from the BC indices, alpha fitted to 4 levels between its extremes. 48 level
	// endpoints (45 bits from bit 17), selector at bit 62, 64 bits of interleaved weights
	void WriteDualPlaneBlock(const Rgb& e0, const Rgb& e1, uint32 indices, const AlphaBlock& alpha, uint32 paletteSize, uint32 alphaMin, uint32 alphaMax, uint8* out)
	{
		const ColorQuantTable& t = s_color48;
		uint8 values[8];
		const bool swap = QuantizeEndpointsRgb(t, e0, e1, values);
		values[6] = t.encode[alphaMin];
		values[7] = t.encode[alphaMax];
		// alphaMin <= alphaMax: the 4 levels ascend, the nearest one is the count of midpoints below the target
		const InterpolatedLevels levels(t.decode[values[6]], t.decode[values[7]], s_weights4);
		const sint32 mid0 = (levels.value[0] + levels.value[1]) / 2;
		const sint32 mid1 = (levels.value[1] + levels.value[2]) / 2;
		const sint32 mid2 = (levels.value[2] + levels.value[3]) / 2;
		uint8 entryWeight[16];
		for (uint32 k = 0; k < paletteSize; k++)
		{
			const sint32 t16 = alpha.palette[k] * 257;
			entryWeight[k] = (uint8)((t16 > mid0) + (t16 > mid1) + (t16 > mid2));
		}
		uint32 alphaWeights = 0;
		for (uint32 i = 0; i < 16; i++)
			alphaWeights |= (uint32)entryWeight[alpha.index[i]] << (2 * i);
		const uint32 colorWeights = FourColorWeights(indices) ^ (swap ? 0xFFFFFFFFu : 0u);
		const uint64 stream = Spread2Bits(colorWeights) | (Spread2Bits(alphaWeights) << 2);
		const u128 block = BlockMode4x4(Q4, true) | (12u << 13) | ((u128)IseEncodeTrits8<4>(values) << 17) | ((u128)3 << 62) | ((u128)ReverseBits64(stream) << 64);
		StoreBlock(out, block);
	}

	struct ColorBlock
	{
		Rgb e0, e1;
		uint32 indices;
		bool constant; // all texels have the same color
		Rgb constantColor;
	};

	// color block of BC2/BC3 at +8 (always four colors)
	inline ColorBlock ReadColorBlock(const uint8* block)
	{
		ColorBlock c;
		c.e0 = Expand565(block[8] | (block[9] << 8));
		c.e1 = Expand565(block[10] | (block[11] << 8));
		memcpy(&c.indices, block + 12, 4);
		const uint32 firstIndex = c.indices & 3;
		c.constant = c.e0 == c.e1 || c.indices == firstIndex * 0x55555555u;
		c.constantColor = c.constant ? PaletteColor(c.e0, c.e1, firstIndex, false) : Rgb{};
		return c;
	}

	inline void WriteConstantAlphaBlock(const ColorBlock& c, uint32 alpha, uint8* out)
	{
		if (c.constant)
			WriteVoidExtent(c.constantColor.r, c.constantColor.g, c.constantColor.b, alpha, out);
		else
			WriteFourColorBlock(c.e0, c.e1, c.indices, alpha == 255 ? -1 : (sint32)alpha, out);
	}

	void TranscodeBC23(const ColorBlock& c, const AlphaBlock& alpha, bool isBC3, uint8* out)
	{
		uint32 alphaMin = 255, alphaMax = 0;
		for (uint32 m = alpha.used; m != 0; m &= m - 1)
		{
			const uint32 a = alpha.palette[__builtin_ctz(m)];
			alphaMin = std::min(alphaMin, a);
			alphaMax = std::max(alphaMax, a);
		}
		if (alphaMin == alphaMax)
			WriteConstantAlphaBlock(c, alphaMin, out);
		else if (c.constant)
			WriteConstantColorBlock(c.constantColor, alpha, alphaMin, alphaMax, isBC3, out);
		else
			WriteDualPlaneBlock(c.e0, c.e1, c.indices, alpha, isBC3 ? 8 : 16, alphaMin, alphaMax, out);
	}
}

void transcodeBC1BlockToASTC4x4(const uint8* bc1Block, uint8* astcBlock)
{
	const uint32 c0 = bc1Block[0] | (bc1Block[1] << 8);
	const uint32 c1 = bc1Block[2] | (bc1Block[3] << 8);
	uint32 indices;
	memcpy(&indices, bc1Block + 4, 4);
	const Rgb e0 = Expand565(c0);
	const Rgb e1 = Expand565(c1);
	if (c0 > c1)
		WriteFourColorBlock(e0, e1, indices, -1, astcBlock);
	else
		TranscodeBC1ThreeColor(e0, e1, indices, astcBlock);
}

void transcodeBC2BlockToASTC4x4(const uint8* bc2Block, uint8* astcBlock)
{
	// explicit 4 bit alpha, texel i in the nibble at bit 4i: palette entry k = k * 17
	uint64 bits;
	memcpy(&bits, bc2Block, 8);
	const ColorBlock color = ReadColorBlock(bc2Block);
	if (bits == (bits & 0xF) * 0x1111111111111111ull)
	{
		WriteConstantAlphaBlock(color, (uint32)(bits & 0xF) * 17, astcBlock); // one alpha value (opaque blocks)
		return;
	}
	AlphaBlock alpha;
	alpha.used = 0;
	for (uint32 k = 0; k < 16; k++)
		alpha.palette[k] = (uint8)(k * 17);
	for (uint32 i = 0; i < 16; i++)
	{
		alpha.index[i] = (uint8)((bits >> (4 * i)) & 0xF);
		alpha.used |= 1u << alpha.index[i];
	}
	TranscodeBC23(color, alpha, false, astcBlock);
}

void transcodeBC3BlockToASTC4x4(const uint8* bc3Block, uint8* astcBlock)
{
	uint64 codes = 0;
	memcpy(&codes, bc3Block + 2, 6);
	const uint32 a0 = bc3Block[0], a1 = bc3Block[1];
	const ColorBlock color = ReadColorBlock(bc3Block);
	// one alpha value (opaque blocks): every texel on entry 0, or a0 == a1 and only entries 0 and 1 (bits 1-2 of each
	// 3 bit code clear)
	if (codes == 0 || (a0 == a1 && (codes & 0xDB6DB6DB6DB6ull) == 0))
	{
		WriteConstantAlphaBlock(color, a0, astcBlock);
		return;
	}
	AlphaBlock alpha;
	alpha.used = 0;
	for (uint32 i = 0; i < 16; i++)
	{
		alpha.index[i] = (uint8)((codes >> (3 * i)) & 7);
		alpha.used |= 1u << alpha.index[i];
	}
	// 8 bit palette, rounded: a0, a1 and 6 interpolants /7, or 4 interpolants /5 followed by 0 and 255
	alpha.palette[0] = (uint8)a0;
	alpha.palette[1] = (uint8)a1;
	if (a0 > a1)
	{
		for (uint32 k = 1; k <= 6; k++)
			alpha.palette[k + 1] = (uint8)(((7 - k) * a0 + k * a1 + 3) / 7);
	}
	else
	{
		for (uint32 k = 1; k <= 4; k++)
			alpha.palette[k + 1] = (uint8)(((5 - k) * a0 + k * a1 + 2) / 5);
		alpha.palette[6] = 0;
		alpha.palette[7] = 255;
	}
	TranscodeBC23(color, alpha, true, astcBlock);
}

void transcodeBC1ToASTC4x4InPlace(uint8* buffer, size_t blockCount)
{
	for (size_t i = blockCount; i > 0; i--)
	{
		uint8 bc1[8];
		memcpy(bc1, buffer + (i - 1) * 8, 8);
		transcodeBC1BlockToASTC4x4(bc1, buffer + (i - 1) * 16);
	}
}

void transcodeBC2ToASTC4x4InPlace(uint8* buffer, size_t blockCount)
{
	for (size_t i = 0; i < blockCount; i++)
	{
		uint8 bc2[16];
		memcpy(bc2, buffer + i * 16, 16);
		transcodeBC2BlockToASTC4x4(bc2, buffer + i * 16);
	}
}

void transcodeBC3ToASTC4x4InPlace(uint8* buffer, size_t blockCount)
{
	for (size_t i = 0; i < blockCount; i++)
	{
		uint8 bc3[16];
		memcpy(bc3, buffer + i * 16, 16);
		transcodeBC3BlockToASTC4x4(bc3, buffer + i * 16);
	}
}
