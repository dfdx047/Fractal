#pragma once
// Reference ETC2/EAC block decoders for the tests, written from the Khronos Data Format Specification (ETC2/EAC
// sections) independently of the encoder in LatteTextureTranscodeETC.cpp. Output texels are row by row (x + y * 4),
// like the BC decoders.

namespace etc_ref
{
	inline uint64 LoadBE64(const uint8* p)
	{
		uint64 v = 0;
		for (int i = 0; i < 8; i++)
			v = (v << 8) | p[i];
		return v;
	}

	inline uint32 Bits(uint64 v, int lowBit, int count)
	{
		return (uint32)((v >> lowBit) & ((1ull << count) - 1));
	}

	inline int SignExtend3(uint32 v)
	{
		return (v & 4) ? (int)v - 8 : (int)v;
	}

	inline int Clamp(int v, int lo, int hi)
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}

	// spec table 'intensity modifier sets for alpha', stored as the four positive values: index 4-7 = +m[0..3],
	// index 0-3 = -(m[0..3]) - 1
	static const int kAlphaPositive[16][4] = {
		{2, 5, 8, 14}, {2, 6, 9, 12}, {1, 4, 7, 12}, {1, 3, 5, 12}, {2, 5, 7, 11}, {2, 6, 8, 10}, {3, 6, 7, 10}, {2, 4, 7, 10},
		{1, 5, 7, 9}, {1, 4, 7, 9}, {1, 3, 7, 9}, {1, 4, 6, 9}, {2, 3, 6, 9}, {0, 1, 2, 9}, {3, 5, 7, 8}, {2, 4, 6, 8},
	};

	inline int AlphaModifier(int table, int index)
	{
		const int m = kAlphaPositive[table][index & 3];
		return (index & 4) ? m : -m - 1;
	}

	enum EACKind
	{
		EAC_R11_UNORM, // value 0..2047 (float = v / 2047)
		EAC_R11_SNORM, // value -1023..1023 (float = v / 1023)
		EAC_ALPHA8,    // value 0..255 (ETC2 RGBA8 alpha)
	};

	inline void DecodeEAC(const uint8* block, EACKind kind, int out[16])
	{
		const uint64 v = LoadBE64(block);
		const int baseU = (int)Bits(v, 56, 8);
		const int multiplier = (int)Bits(v, 52, 4);
		const int table = (int)Bits(v, 48, 4);
		for (int x = 0; x < 4; x++)
		{
			for (int y = 0; y < 4; y++)
			{
				const int pixel = x * 4 + y; // 'a' is (0,0), 'b' is (0,1), ...
				const int index = (int)Bits(v, 45 - 3 * pixel, 3);
				const int modifier = AlphaModifier(table, index);
				int value;
				if (kind == EAC_ALPHA8)
					value = Clamp(baseU + modifier * multiplier, 0, 255);
				else if (kind == EAC_R11_UNORM)
					value = Clamp(baseU * 8 + 4 + (multiplier ? modifier * multiplier * 8 : modifier), 0, 2047);
				else
				{
					int baseS = (int)(int8_t)(uint8)baseU;
					if (baseS == -128)
						baseS = -127;
					value = Clamp(baseS * 8 + (multiplier ? modifier * multiplier * 8 : modifier), -1023, 1023);
				}
				out[x + y * 4] = value;
			}
		}
	}

	static const int kETC1Table[8][2] = { {2, 8}, {5, 17}, {9, 29}, {13, 42}, {18, 60}, {24, 80}, {33, 106}, {47, 183} };
	static const int kDistance[8] = { 3, 6, 11, 16, 23, 32, 41, 64 };

	inline int Ext4(int v) { return (v << 4) | v; }
	inline int Ext5(int v) { return (v << 3) | (v >> 2); }
	inline int Ext6(int v) { return (v << 2) | (v >> 4); }
	inline int Ext7(int v) { return (v << 1) | (v >> 6); }

	// ETC2 RGB8 (punchthrough false) or RGB8A1 (punchthrough true). Output RGBA
	inline void DecodeETC2(const uint8* block, bool punchthrough, uint8 out[16][4])
	{
		const uint64 v = LoadBE64(block);
		const bool flip = Bits(v, 32, 1) != 0;
		const bool diffOrOpaque = Bits(v, 33, 1) != 0;
		const bool opaque = punchthrough ? diffOrOpaque : true;
		const int R = (int)Bits(v, 59, 5), dR = SignExtend3(Bits(v, 56, 3));
		const int G = (int)Bits(v, 51, 5), dG = SignExtend3(Bits(v, 48, 3));
		const int B = (int)Bits(v, 43, 5), dB = SignExtend3(Bits(v, 40, 3));
		enum { INDIVIDUAL, DIFFERENTIAL, T_MODE, H_MODE, PLANAR } mode;
		if (!punchthrough && !diffOrOpaque)
			mode = INDIVIDUAL;
		else if (R + dR < 0 || R + dR > 31)
			mode = T_MODE;
		else if (G + dG < 0 || G + dG > 31)
			mode = H_MODE;
		else if (B + dB < 0 || B + dB > 31)
			mode = PLANAR;
		else
			mode = DIFFERENTIAL;

		for (int x = 0; x < 4; x++)
		{
			for (int y = 0; y < 4; y++)
			{
				const int pixel = x * 4 + y;
				const int index = (int)((Bits(v, 16 + pixel, 1) << 1) | Bits(v, pixel, 1));
				int rgb[3] = {};
				bool transparent = false;
				if (mode == INDIVIDUAL || mode == DIFFERENTIAL)
				{
					const int sub = flip ? (y >= 2) : (x >= 2);
					int base[3];
					if (mode == INDIVIDUAL)
					{
						base[0] = Ext4((int)Bits(v, sub ? 56 : 60, 4));
						base[1] = Ext4((int)Bits(v, sub ? 48 : 52, 4));
						base[2] = Ext4((int)Bits(v, sub ? 40 : 44, 4));
					}
					else
					{
						base[0] = Ext5(R + (sub ? dR : 0));
						base[1] = Ext5(G + (sub ? dG : 0));
						base[2] = Ext5(B + (sub ? dB : 0));
					}
					const int table = (int)Bits(v, sub ? 34 : 37, 3);
					int modifier;
					switch (index)
					{
					case 0: modifier = kETC1Table[table][0]; break;
					case 1: modifier = kETC1Table[table][1]; break;
					case 2: modifier = -kETC1Table[table][0]; break;
					default: modifier = -kETC1Table[table][1]; break;
					}
					if (!opaque)
					{
						// punchthrough, opaque bit 0: modifiers of index 0 and 2 are zero, index 2 is transparent
						if (index == 0 || index == 2)
							modifier = 0;
						transparent = index == 2;
					}
					for (int c = 0; c < 3; c++)
						rgb[c] = Clamp(base[c] + modifier, 0, 255);
				}
				else if (mode == T_MODE)
				{
					const int c1[3] = { Ext4((int)((Bits(v, 59, 2) << 2) | Bits(v, 56, 2))), Ext4((int)Bits(v, 52, 4)), Ext4((int)Bits(v, 48, 4)) };
					const int c2[3] = { Ext4((int)Bits(v, 44, 4)), Ext4((int)Bits(v, 40, 4)), Ext4((int)Bits(v, 36, 4)) };
					const int d = kDistance[(Bits(v, 34, 2) << 1) | Bits(v, 32, 1)];
					for (int c = 0; c < 3; c++)
					{
						switch (index)
						{
						case 0: rgb[c] = c1[c]; break;
						case 1: rgb[c] = Clamp(c2[c] + d, 0, 255); break;
						case 2: rgb[c] = c2[c]; break;
						default: rgb[c] = Clamp(c2[c] - d, 0, 255); break;
						}
					}
					transparent = !opaque && index == 2;
				}
				else if (mode == H_MODE)
				{
					const int r1 = (int)Bits(v, 59, 4);
					const int g1 = (int)((Bits(v, 56, 3) << 1) | Bits(v, 52, 1));
					const int b1 = (int)((Bits(v, 51, 1) << 3) | (Bits(v, 47, 3)));
					const int r2 = (int)Bits(v, 43, 4);
					const int g2 = (int)Bits(v, 39, 4);
					const int b2 = (int)Bits(v, 35, 4);
					const int order = ((r1 << 8) | (g1 << 4) | b1) >= ((r2 << 8) | (g2 << 4) | b2) ? 1 : 0;
					const int d = kDistance[(Bits(v, 34, 1) << 2) | (Bits(v, 32, 1) << 1) | order];
					const int c1[3] = { Ext4(r1), Ext4(g1), Ext4(b1) };
					const int c2[3] = { Ext4(r2), Ext4(g2), Ext4(b2) };
					for (int c = 0; c < 3; c++)
					{
						switch (index)
						{
						case 0: rgb[c] = Clamp(c1[c] + d, 0, 255); break;
						case 1: rgb[c] = Clamp(c1[c] - d, 0, 255); break;
						case 2: rgb[c] = Clamp(c2[c] + d, 0, 255); break;
						default: rgb[c] = Clamp(c2[c] - d, 0, 255); break;
						}
					}
					transparent = !opaque && index == 2;
				}
				else
				{
					const int o[3] = { Ext6((int)Bits(v, 57, 6)), Ext7((int)((Bits(v, 56, 1) << 6) | Bits(v, 49, 6))),
						Ext6((int)((Bits(v, 48, 1) << 5) | (Bits(v, 43, 2) << 3) | Bits(v, 39, 3))) };
					const int h[3] = { Ext6((int)((Bits(v, 34, 5) << 1) | Bits(v, 32, 1))), Ext7((int)Bits(v, 25, 7)), Ext6((int)Bits(v, 19, 6)) };
					const int vv[3] = { Ext6((int)Bits(v, 13, 6)), Ext7((int)Bits(v, 6, 7)), Ext6((int)Bits(v, 0, 6)) };
					for (int c = 0; c < 3; c++)
						rgb[c] = Clamp((x * (h[c] - o[c]) + y * (vv[c] - o[c]) + 4 * o[c] + 2) >> 2, 0, 255);
				}
				uint8* t = out[x + y * 4];
				if (transparent)
				{
					t[0] = t[1] = t[2] = t[3] = 0;
				}
				else
				{
					t[0] = (uint8)rgb[0];
					t[1] = (uint8)rgb[1];
					t[2] = (uint8)rgb[2];
					t[3] = 255;
				}
			}
		}
	}
}
