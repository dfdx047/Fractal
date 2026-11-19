#pragma once
// Reference ASTC LDR decoder for 4x4 blocks, written from the Khronos Data Format Specification (ASTC section) for the
// transcoder tests. It reads the block bit by bit and shares no code or tables with the encoder. It covers what the
// transcoder may produce and a little more: every 2D block mode with a weight grid up to 4x4, one or two planes, one
// partition, endpoint modes 0, 4, 8 and 12, and LDR void extent blocks. Anything else is reported as unsupported.
// Blocks the spec calls illegal decode to the error color (magenta) and are reported as errors.
#include <cstdint>

namespace astc_ref
{
	enum class Result { Ok, Error, Unsupported };

	struct Range
	{
		uint32 levels;
		uint32 bits;
		bool trit;
		bool quint;
	};

	// all ranges in increasing order (color endpoints pick the largest that fits)
	static const Range kRanges[21] = {
		{ 2, 1, false, false }, { 3, 0, true, false }, { 4, 2, false, false }, { 5, 0, false, true }, { 6, 1, true, false },
		{ 8, 3, false, false }, { 10, 1, false, true }, { 12, 2, true, false }, { 16, 4, false, false }, { 20, 2, false, true },
		{ 24, 3, true, false }, { 32, 5, false, false }, { 40, 3, false, true }, { 48, 4, true, false }, { 64, 6, false, false },
		{ 80, 4, false, true }, { 96, 5, true, false }, { 128, 7, false, false }, { 160, 5, false, true }, { 192, 6, true, false },
		{ 256, 8, false, false } };

	inline uint32 SequenceBits(const Range& r, uint32 count)
	{
		uint32 bits = r.bits * count;
		if (r.trit)
			bits += (8 * count + 4) / 5;
		if (r.quint)
			bits += (7 * count + 2) / 3;
		return bits;
	}

	// bits of a 128 bit block, LSB first, optionally mirrored (weights are stored from bit 127 down)
	struct BlockBits
	{
		uint8 bit[128];
		BlockBits(const uint8* block, bool reversed)
		{
			for (int i = 0; i < 128; i++)
			{
				int src = reversed ? 127 - i : i;
				bit[i] = (block[src / 8] >> (src % 8)) & 1;
			}
		}
		uint32 Get(uint32 pos, uint32 count) const
		{
			uint32 v = 0;
			for (uint32 i = 0; i < count; i++)
				v |= (uint32)bit[pos + i] << i;
			return v;
		}
	};

	// reads a bounded integer sequence: bits past its end read as zero
	struct SequenceReader
	{
		const BlockBits& bits;
		uint32 pos, end;
		uint32 Read(uint32 count)
		{
			uint32 v = 0;
			for (uint32 i = 0; i < count; i++, pos++)
				if (pos < end)
					v |= (uint32)bits.bit[pos] << i;
			return v;
		}
	};

	inline uint32 Bit(uint32 v, int i) { return (v >> i) & 1; }

	// spec: trit decoding of the 8 bit T
	inline void Trits(uint32 T, uint32 t[5])
	{
		uint32 C;
		if (Bit(T, 4) && Bit(T, 3) && Bit(T, 2))
		{
			C = (Bit(T, 7) << 4) | (Bit(T, 6) << 3) | (Bit(T, 5) << 2) | (Bit(T, 1) << 1) | Bit(T, 0);
			t[4] = 2;
			t[3] = 2;
		}
		else
		{
			C = T & 31;
			if (Bit(T, 6) && Bit(T, 5))
			{
				t[4] = 2;
				t[3] = Bit(T, 7);
			}
			else
			{
				t[4] = Bit(T, 7);
				t[3] = (Bit(T, 6) << 1) | Bit(T, 5);
			}
		}
		if (Bit(C, 1) && Bit(C, 0))
		{
			t[2] = 2;
			t[1] = Bit(C, 4);
			t[0] = (Bit(C, 3) << 1) | (Bit(C, 2) & (1 - Bit(C, 3)));
		}
		else if (Bit(C, 3) && Bit(C, 2))
		{
			t[2] = 2;
			t[1] = 2;
			t[0] = (Bit(C, 1) << 1) | Bit(C, 0);
		}
		else
		{
			t[2] = Bit(C, 4);
			t[1] = (Bit(C, 3) << 1) | Bit(C, 2);
			t[0] = (Bit(C, 1) << 1) | (Bit(C, 0) & (1 - Bit(C, 1)));
		}
	}

	// spec: quint decoding of the 7 bit Q
	inline void Quints(uint32 Q, uint32 q[3])
	{
		if (Bit(Q, 2) && Bit(Q, 1) && !Bit(Q, 6) && !Bit(Q, 5))
		{
			q[2] = (Bit(Q, 0) << 2) | ((Bit(Q, 4) & (1 - Bit(Q, 0))) << 1) | (Bit(Q, 3) & (1 - Bit(Q, 0)));
			q[1] = 4;
			q[0] = 4;
			return;
		}
		uint32 C;
		if (Bit(Q, 2) && Bit(Q, 1))
		{
			q[2] = 4;
			C = (Bit(Q, 4) << 4) | (Bit(Q, 3) << 3) | ((1 - Bit(Q, 6)) << 2) | ((1 - Bit(Q, 5)) << 1) | Bit(Q, 0);
		}
		else
		{
			q[2] = (Bit(Q, 6) << 1) | Bit(Q, 5);
			C = Q & 31;
		}
		if ((C & 7) == 5)
		{
			q[1] = 4;
			q[0] = (Bit(C, 4) << 1) | Bit(C, 3);
		}
		else
		{
			q[1] = (Bit(C, 4) << 1) | Bit(C, 3);
			q[0] = C & 7;
		}
	}

	// decoded integers as (high part: trit/quint or 0, low bits)
	struct IseValue
	{
		uint32 D, m;
	};

	inline void DecodeSequence(const BlockBits& bits, uint32 start, const Range& r, uint32 count, IseValue* out)
	{
		SequenceReader rd{ bits, start, start + SequenceBits(r, count) };
		const uint32 n = r.bits;
		uint32 i = 0;
		while (i < count)
		{
			if (r.trit)
			{
				uint32 m[5], T = 0, t[5];
				m[0] = rd.Read(n); T |= rd.Read(2);
				m[1] = rd.Read(n); T |= rd.Read(2) << 2;
				m[2] = rd.Read(n); T |= rd.Read(1) << 4;
				m[3] = rd.Read(n); T |= rd.Read(2) << 5;
				m[4] = rd.Read(n); T |= rd.Read(1) << 7;
				Trits(T, t);
				for (int j = 0; j < 5 && i < count; j++, i++)
					out[i] = { t[j], m[j] };
			}
			else if (r.quint)
			{
				uint32 m[3], Q = 0, q[3];
				m[0] = rd.Read(n); Q |= rd.Read(3);
				m[1] = rd.Read(n); Q |= rd.Read(2) << 3;
				m[2] = rd.Read(n); Q |= rd.Read(2) << 5;
				Quints(Q, q);
				for (int j = 0; j < 3 && i < count; j++, i++)
					out[i] = { q[j], m[j] };
			}
			else
			{
				out[i] = { 0, rd.Read(n) };
				i++;
			}
		}
	}

	// spec: color endpoint unquantization to 8 bits
	inline uint32 UnquantizeColor(const Range& r, IseValue v)
	{
		const uint32 n = r.bits;
		if (!r.trit && !r.quint)
		{
			// replicate the n bits until 8 bits are filled
			uint32 out = 0;
			for (int i = 7; i >= 0; i--)
			{
				int src = (int)n - 1 - ((7 - i) % (int)n);
				out |= Bit(v.m, src) << i;
			}
			return out;
		}
		const uint32 a = Bit(v.m, 0), b = Bit(v.m, 1), c = Bit(v.m, 2), d = Bit(v.m, 3), e = Bit(v.m, 4), f = Bit(v.m, 5);
		const uint32 A = a ? 0x1FF : 0;
		uint32 B = 0, C = 0;
		// B as 9 bit strings from the spec table, MSB first
		auto pattern = [](std::initializer_list<uint32> bitsMsbFirst)
		{
			uint32 x = 0;
			for (uint32 bit : bitsMsbFirst)
				x = (x << 1) | bit;
			return x;
		};
		if (r.trit)
		{
			switch (n)
			{
			case 1: B = 0; C = 204; break;
			case 2: B = pattern({ b, 0, 0, 0, b, 0, b, b, 0 }); C = 93; break;
			case 3: B = pattern({ c, b, 0, 0, 0, c, b, c, b }); C = 44; break;
			case 4: B = pattern({ d, c, b, 0, 0, 0, d, c, b }); C = 22; break;
			case 5: B = pattern({ e, d, c, b, 0, 0, 0, e, d }); C = 11; break;
			case 6: B = pattern({ f, e, d, c, b, 0, 0, 0, f }); C = 5; break;
			}
		}
		else
		{
			switch (n)
			{
			case 1: B = 0; C = 113; break;
			case 2: B = pattern({ b, 0, 0, 0, 0, b, b, 0, 0 }); C = 54; break;
			case 3: B = pattern({ c, b, 0, 0, 0, 0, c, b, c }); C = 26; break;
			case 4: B = pattern({ d, c, b, 0, 0, 0, 0, d, c }); C = 13; break;
			case 5: B = pattern({ e, d, c, b, 0, 0, 0, 0, e }); C = 6; break;
			}
		}
		uint32 T = v.D * C + B;
		T = T ^ A;
		return (A & 0x80) | (T >> 2);
	}

	// spec: weight unquantization to 0..64
	inline uint32 UnquantizeWeight(const Range& r, IseValue v)
	{
		const uint32 n = r.bits;
		uint32 w;
		if (!r.trit && !r.quint)
		{
			w = 0;
			for (int i = 5; i >= 0; i--)
			{
				int src = (int)n - 1 - ((5 - i) % (int)n);
				w |= Bit(v.m, src) << i;
			}
		}
		else if (n == 0)
		{
			static const uint32 trit0[3] = { 0, 32, 63 };
			static const uint32 quint0[5] = { 0, 16, 32, 47, 63 };
			w = r.trit ? trit0[v.D] : quint0[v.D];
		}
		else
		{
			const uint32 a = Bit(v.m, 0), b = Bit(v.m, 1), c = Bit(v.m, 2);
			const uint32 A = a ? 0x7F : 0;
			uint32 B = 0, C = 0;
			auto pattern = [](std::initializer_list<uint32> bitsMsbFirst)
			{
				uint32 x = 0;
				for (uint32 bit : bitsMsbFirst)
					x = (x << 1) | bit;
				return x;
			};
			if (r.trit)
			{
				switch (n)
				{
				case 1: B = 0; C = 50; break;
				case 2: B = pattern({ b, 0, 0, 0, b, 0, b }); C = 23; break;
				case 3: B = pattern({ c, b, 0, 0, 0, c, b }); C = 11; break;
				}
			}
			else
			{
				switch (n)
				{
				case 1: B = 0; C = 28; break;
				case 2: B = pattern({ b, 0, 0, 0, 0, b, 0 }); C = 13; break;
				}
			}
			uint32 T = v.D * C + B;
			T = T ^ A;
			w = (A & 0x20) | (T >> 2);
		}
		if (w > 32)
			w++;
		return w;
	}

	struct BlockMode
	{
		uint32 gridW, gridH;
		bool dualPlane;
		uint32 rangeIndex; // into kRanges
	};

	// spec: 2D block mode layout. Returns false for reserved modes
	inline bool DecodeBlockMode(uint32 mode, BlockMode& bm)
	{
		const uint32 R0 = Bit(mode, 4);
		uint32 R1, R2, A = (mode >> 5) & 3, B = (mode >> 7) & 3;
		uint32 D = Bit(mode, 10), H = Bit(mode, 9);
		if ((mode & 3) != 0)
		{
			R1 = Bit(mode, 0);
			R2 = Bit(mode, 1);
			switch ((mode >> 2) & 3)
			{
			case 0: bm.gridW = B + 4; bm.gridH = A + 2; break;
			case 1: bm.gridW = B + 8; bm.gridH = A + 2; break;
			case 2: bm.gridW = A + 2; bm.gridH = B + 8; break;
			default:
				if (Bit(mode, 8))
				{
					bm.gridW = Bit(mode, 7) + 2;
					bm.gridH = A + 2;
				}
				else
				{
					bm.gridW = A + 2;
					bm.gridH = Bit(mode, 7) + 6;
				}
				break;
			}
		}
		else
		{
			R1 = Bit(mode, 2);
			R2 = Bit(mode, 3);
			if ((mode & 0xF) == 0)
				return false; // reserved
			switch ((mode >> 7) & 3)
			{
			case 0: bm.gridW = 12; bm.gridH = A + 2; break;
			case 1: bm.gridW = A + 2; bm.gridH = 12; break;
			case 2:
				bm.gridW = A + 6;
				bm.gridH = ((mode >> 9) & 3) + 6;
				D = 0;
				H = 0;
				break;
			default:
				if (A == 0)
				{
					bm.gridW = 6;
					bm.gridH = 10;
				}
				else if (A == 1)
				{
					bm.gridW = 10;
					bm.gridH = 6;
				}
				else
					return false; // reserved (A = 3 with R = 111 is void extent, handled before)
				break;
			}
		}
		const uint32 R = (R2 << 2) | (R1 << 1) | R0;
		if (R < 2)
			return false;
		// weight ranges: H=0: 2, 3, 4, 5, 6, 8 levels; H=1: 10, 12, 16, 20, 24, 32 levels
		static const uint32 levelsH0[8] = { 0, 0, 2, 3, 4, 5, 6, 8 };
		static const uint32 levelsH1[8] = { 0, 0, 10, 12, 16, 20, 24, 32 };
		const uint32 levels = H ? levelsH1[R] : levelsH0[R];
		bm.rangeIndex = 0;
		while (kRanges[bm.rangeIndex].levels != levels)
			bm.rangeIndex++;
		bm.dualPlane = D != 0;
		return true;
	}

	inline void ErrorColor(uint8 out[64])
	{
		for (int i = 0; i < 16; i++)
		{
			out[i * 4 + 0] = 255;
			out[i * 4 + 1] = 0;
			out[i * 4 + 2] = 255;
			out[i * 4 + 3] = 255;
		}
	}

	// UNORM16 value of a channel to the 8 bit value compared in the tests: sRGB textures use the top 8 bits (then
	// converted to linear by the GPU), UNORM textures the value / 65535, here rounded to 8 bits
	inline uint8 ToByte(uint32 c16, bool srgb)
	{
		return srgb ? (uint8)(c16 >> 8) : (uint8)((c16 * 255 + 32767) / 65535);
	}

	// decodes a 4x4 block to RGBA8 (row major, texel (x, y) at (y * 4 + x) * 4)
	inline Result DecodeBlock(const uint8* block, bool srgb, uint8 out[64])
	{
		const BlockBits bits(block, false);
		const uint32 mode = bits.Get(0, 11);
		if ((mode & 0x1FF) == 0x1FC)
		{
			// void extent: bit 9 = HDR, bits 10-11 reserved ones, extent coordinates (all ones = none), UNORM16 RGBA
			if (bits.Get(9, 1) != 0)
				return Result::Unsupported;
			if (bits.Get(10, 2) != 3)
			{
				ErrorColor(out);
				return Result::Error;
			}
			uint32 rgba[4];
			for (int c = 0; c < 4; c++)
				rgba[c] = bits.Get(64 + 16 * c, 16);
			for (int i = 0; i < 16; i++)
				for (int c = 0; c < 4; c++)
					out[i * 4 + c] = ToByte(rgba[c], srgb);
			return Result::Ok;
		}
		BlockMode bm;
		if (!DecodeBlockMode(mode, bm) || bm.gridW > 4 || bm.gridH > 4)
		{
			ErrorColor(out);
			return Result::Error;
		}
		const uint32 partitions = bits.Get(11, 2) + 1;
		if (partitions != 1)
			return Result::Unsupported;
		const uint32 cem = bits.Get(13, 4);
		const Range& weightRange = kRanges[bm.rangeIndex];
		const uint32 weightCount = bm.gridW * bm.gridH * (bm.dualPlane ? 2 : 1);
		const uint32 weightBits = SequenceBits(weightRange, weightCount);
		if (weightCount > 64 || weightBits < 24 || weightBits > 96)
		{
			ErrorColor(out);
			return Result::Error;
		}
		uint32 ccs = 0;
		uint32 belowWeights = 128 - weightBits;
		if (bm.dualPlane)
		{
			belowWeights -= 2;
			ccs = bits.Get(belowWeights, 2);
		}
		const sint32 colorBits = (sint32)belowWeights - 17;
		const uint32 valueCount = ((cem >> 2) + 1) * 2;
		sint32 colorRange = -1;
		for (int i = 0; i < 21; i++)
			if ((sint32)SequenceBits(kRanges[i], valueCount) <= colorBits)
				colorRange = i;
		if (colorRange < 0 || kRanges[colorRange].levels < 6)
		{
			ErrorColor(out);
			return Result::Error;
		}
		if (cem != 0 && cem != 4 && cem != 8 && cem != 12)
			return Result::Unsupported;

		IseValue colorIse[8];
		DecodeSequence(bits, 17, kRanges[colorRange], valueCount, colorIse);
		uint32 v[8];
		for (uint32 i = 0; i < valueCount; i++)
			v[i] = UnquantizeColor(kRanges[colorRange], colorIse[i]);

		uint32 e0[4], e1[4];
		auto set = [](uint32* e, uint32 r, uint32 g, uint32 b, uint32 a) { e[0] = r; e[1] = g; e[2] = b; e[3] = a; };
		auto blueContract = [](uint32* e, uint32 r, uint32 g, uint32 b, uint32 a) { e[0] = (r + b) >> 1; e[1] = (g + b) >> 1; e[2] = b; e[3] = a; };
		switch (cem)
		{
		case 0: set(e0, v[0], v[0], v[0], 255); set(e1, v[1], v[1], v[1], 255); break;
		case 4: set(e0, v[0], v[0], v[0], v[2]); set(e1, v[1], v[1], v[1], v[3]); break;
		case 8:
		case 12:
		{
			const uint32 a0 = cem == 12 ? v[6] : 255, a1 = cem == 12 ? v[7] : 255;
			if (v[1] + v[3] + v[5] >= v[0] + v[2] + v[4])
			{
				set(e0, v[0], v[2], v[4], a0);
				set(e1, v[1], v[3], v[5], a1);
			}
			else
			{
				blueContract(e0, v[1], v[3], v[5], a1);
				blueContract(e1, v[0], v[2], v[4], a0);
			}
			break;
		}
		}

		// weights: integer sequence read from the mirrored block, unquantized, then the grid infill to the 4x4 texels
		const BlockBits mirrored(block, true);
		IseValue weightIse[64];
		DecodeSequence(mirrored, 0, weightRange, weightCount, weightIse);
		const uint32 planes = bm.dualPlane ? 2 : 1;
		uint32 grid[2][16];
		for (uint32 i = 0; i < bm.gridW * bm.gridH; i++)
			for (uint32 p = 0; p < planes; p++)
				grid[p][i] = UnquantizeWeight(weightRange, weightIse[i * planes + p]);
		const uint32 Ds = (1024 + 2) / 3, Dt = (1024 + 2) / 3; // block size 4
		for (uint32 t = 0; t < 4; t++)
		{
			for (uint32 s = 0; s < 4; s++)
			{
				const uint32 gs = (Ds * s * (bm.gridW - 1) + 32) >> 6;
				const uint32 gt = (Dt * t * (bm.gridH - 1) + 32) >> 6;
				const uint32 js = gs >> 4, fs = gs & 15, jt = gt >> 4, ft = gt & 15;
				const uint32 w11 = (fs * ft + 8) >> 4;
				const uint32 w10 = ft - w11, w01 = fs - w11, w00 = 16 - fs - ft + w11;
				uint32 weight[2];
				for (uint32 p = 0; p < planes; p++)
				{
					auto gw = [&](uint32 x, uint32 y) -> uint32 { return (x < bm.gridW && y < bm.gridH) ? grid[p][y * bm.gridW + x] : 0; };
					weight[p] = (gw(js, jt) * w00 + gw(js + 1, jt) * w01 + gw(js, jt + 1) * w10 + gw(js + 1, jt + 1) * w11 + 8) >> 4;
				}
				for (uint32 c = 0; c < 4; c++)
				{
					const uint32 w = (bm.dualPlane && c == ccs) ? weight[1] : weight[0];
					// LDR: 8 bit endpoints to 16 bits, sRGB with 0x80 as the low byte
					const uint32 c0 = srgb ? ((e0[c] << 8) | 0x80) : (e0[c] << 8) | e0[c];
					const uint32 c1 = srgb ? ((e1[c] << 8) | 0x80) : (e1[c] << 8) | e1[c];
					const uint32 value = (c0 * (64 - w) + c1 * w + 32) >> 6;
					out[(t * 4 + s) * 4 + c] = ToByte(value, srgb);
				}
			}
		}
		return Result::Ok;
	}
}
