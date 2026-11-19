// BC1/BC2/BC3 -> ASTC 4x4 transcoder (GPUs without BCn but with ASTC): decodes the ASTC output with a reference
// decoder written from the spec (astc_reference.h) and compares it with the BC decode, for random blocks with the
// corner cases of bc_test and for blocks of synthetic images encoded by a simple BC encoder (gradients, noise, cutout
// and soft alpha, glyphs). Reports max / mean error and PSNR per encoding path, and the transcoding speed.
// HOST_TEST_OK: no NEON involved, runs on the PC too (HOST=1 build.sh)
#include "Cafe/HW/Latte/Core/LatteTextureLoaderBC.cpp"
#include "Cafe/HW/Latte/Core/LatteTextureTranscodeASTC.cpp"
#include "astc_reference.h"
#include "test_common.h"
#include <map>
#include <string>

/* reference decoder self checks against values published for the ASTC ranges */

static void testReferenceDecoder()
{
	using namespace astc_ref;
	auto rangeOf = [](uint32 levels) -> const Range& { int i = 0; while (kRanges[i].levels != levels) i++; return kRanges[i]; };
	// color unquantization, values in integer sequence order
	const uint32 color6[6] = { 0, 255, 51, 204, 102, 153 };
	for (uint32 v = 0; v < 6; v++)
		TEST_CHECK(UnquantizeColor(rangeOf(6), { v >> 1, v & 1 }) == color6[v], "color range 6 value %u", v);
	const uint32 color12[12] = { 0, 255, 69, 186, 23, 232, 92, 163, 46, 209, 116, 139 };
	for (uint32 v = 0; v < 12; v++)
		TEST_CHECK(UnquantizeColor(rangeOf(12), { v >> 2, v & 3 }) == color12[v], "color range 12 value %u", v);
	// weights
	const uint32 weight6[6] = { 0, 64, 12, 52, 25, 39 };
	for (uint32 v = 0; v < 6; v++)
		TEST_CHECK(UnquantizeWeight(rangeOf(6), { v >> 1, v & 1 }) == weight6[v], "weight range 6 value %u", v);
	const uint32 weight12[12] = { 0, 64, 17, 47, 5, 59, 23, 41, 11, 53, 28, 36 };
	for (uint32 v = 0; v < 12; v++)
		TEST_CHECK(UnquantizeWeight(rangeOf(12), { v >> 2, v & 3 }) == weight12[v], "weight range 12 value %u", v);
	const uint32 weight3[3] = { 0, 32, 64 };
	for (uint32 v = 0; v < 3; v++)
		TEST_CHECK(UnquantizeWeight(rangeOf(3), { v, 0 }) == weight3[v], "weight range 3 value %u", v);
	// every range: unquantized colors cover 0 and 255 and are all distinct, weights cover 0 and 64
	for (const Range& r : kRanges)
	{
		if (r.levels < 6)
			continue; // not allowed for color endpoints
		std::vector<bool> seen(256, false);
		for (uint32 v = 0; v < r.levels; v++)
		{
			uint32 c = UnquantizeColor(r, { v >> r.bits, v & ((1u << r.bits) - 1) });
			TEST_CHECK(!seen[c], "range %u: color %u twice", r.levels, c);
			seen[c] = true;
		}
		TEST_CHECK(seen[0] && seen[255], "range %u: 0 or 255 missing", r.levels);
	}
	// trit/quint packing: every packed value decodes to digits in range, and all 243 / 125 digit tuples appear
	std::vector<bool> tritSeen(243, false), quintSeen(125, false);
	for (uint32 T = 0; T < 256; T++)
	{
		uint32 t[5];
		Trits(T, t);
		uint32 idx = 0;
		for (int i = 4; i >= 0; i--)
		{
			TEST_CHECK(t[i] < 3, "trit");
			idx = idx * 3 + t[i];
		}
		tritSeen[idx] = true;
	}
	for (uint32 Q = 0; Q < 128; Q++)
	{
		uint32 q[3];
		Quints(Q, q);
		TEST_CHECK(q[0] < 5 && q[1] < 5 && q[2] < 5, "quint");
		quintSeen[q[0] + 5 * q[1] + 25 * q[2]] = true;
	}
	for (bool b : tritSeen) TEST_CHECK(b, "trit tuple missing");
	for (bool b : quintSeen) TEST_CHECK(b, "quint tuple missing");

	// a block built by hand: 4x4 grid of 2 bit weights (block mode 0x042), mode 8 from black to white, texel i weight i & 3
	uint8 block[16] = {};
	auto setBits = [&](uint32 pos, uint32 count, uint64 value)
	{
		for (uint32 i = 0; i < count; i++)
			if ((value >> i) & 1)
				block[(pos + i) / 8] |= (uint8)(1 << ((pos + i) % 8));
	};
	setBits(0, 11, 0x042);
	setBits(13, 4, 8);
	const uint8 values[6] = { 0, 255, 0, 255, 0, 255 };
	for (int i = 0; i < 6; i++)
		setBits(17 + 8 * i, 8, values[i]);
	for (int i = 0; i < 16; i++)
	{
		uint32 w = i & 3;
		// weight bit k of the sequence at block bit 127 - k
		setBits(127 - (2 * i), 1, w & 1);
		setBits(127 - (2 * i + 1), 1, w >> 1);
	}
	uint8 out[64];
	TEST_CHECK(DecodeBlock(block, false, out) == Result::Ok, "hand block");
	// weights 0, 21, 43, 64: (65535 * w + 32) >> 6 = 0, 21504, 44031, 65535 -> 0, 84, 171, 255
	const uint8 expected[4] = { 0, 84, 171, 255 };
	for (int i = 0; i < 16; i++)
		for (int c = 0; c < 4; c++)
			TEST_CHECK(out[i * 4 + c] == (c == 3 ? 255 : expected[i & 3]), "hand block texel %d channel %d: %u", i, c, out[i * 4 + c]);
	// void extent
	uint8 ve[16];
	const uint64 lo = 0xFFFFFFFFFFFFFDFCull, hi = 0x1234ABCD5678EF01ull;
	memcpy(ve, &lo, 8);
	memcpy(ve + 8, &hi, 8);
	TEST_CHECK(DecodeBlock(ve, true, out) == Result::Ok && out[0] == 0xEF && out[1] == 0x56 && out[2] == 0xAB && out[3] == 0x12, "void extent");
}

/* BC test data */

// random blocks with the corner cases of bc_test (equal / ordered endpoints, extreme alpha endpoints)
static void randomBlock(TestRng& rng, uint8* b, sint32 blockBytes)
{
	rng.fill(b, blockBytes);
	uint8* color = b + (blockBytes == 16 ? 8 : 0);
	switch (rng.below(10))
	{
	case 0: color[2] = color[0]; color[3] = color[1]; break; // equal colors
	case 1: { uint16 t; memcpy(&t, color, 2); uint16 u = t ^ (uint16)(1 << rng.below(16)); memcpy(color + 2, &u, 2); break; } // nearly equal
	case 2: memset(color + 4, (uint8)(0x55 * rng.below(4)), 4); break; // one index everywhere
	case 3: color[0] = color[1] = 0xFF; color[2] = color[3] = 0; break; // white / black
	case 4: color[0] = color[1] = 0; color[2] = color[3] = 0xFF; break; // black / white (BC1 three colors)
	default: break;
	}
	if (blockBytes == 16)
	{
		switch (rng.below(10))
		{
		case 0: b[1] = b[0]; break; // equal alpha endpoints
		case 1: b[0] = 0xFF; b[1] = 0x00; break;
		case 2: b[0] = 0x00; b[1] = 0xFF; break; // 6 value mode with 0 and 255
		case 3: memset(b, 0xFF, 8); break; // opaque
		case 4: memset(b + 2, 0, 6); break; // one alpha index
		default: break;
		}
	}
}

struct Image
{
	sint32 w, h;
	std::vector<float> rgba; // 0..1
	float* at(sint32 x, sint32 y) { return &rgba[(y * w + x) * 4]; }
};

// synthetic images. kind: 0 gradient, 1 natural (sines + noise), 2 noise, 3 cutout (natural + binary alpha), 4 soft
// alpha, 5 glyphs (constant color, antialiased alpha shapes)
static Image makeImage(TestRng& rng, int kind, sint32 size)
{
	Image img{ size, size, std::vector<float>(size * size * 4) };
	float f[8];
	for (float& v : f)
		v = 0.01f + (float)rng.below(1000) / 1000.0f * 0.2f;
	const float glyphColor[3] = { (float)rng.below(256) / 255.0f, (float)rng.below(256) / 255.0f, (float)rng.below(256) / 255.0f };
	auto noise = [&](float amount) { return ((float)rng.below(1000) / 1000.0f - 0.5f) * amount; };
	auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
	for (sint32 y = 0; y < size; y++)
	{
		for (sint32 x = 0; x < size; x++)
		{
			float* p = img.at(x, y);
			float u = (float)x / size, v = (float)y / size;
			switch (kind)
			{
			case 0:
				p[0] = u; p[1] = v; p[2] = 1.0f - 0.5f * (u + v); p[3] = 1.0f;
				break;
			case 2:
				for (int c = 0; c < 4; c++) p[c] = (float)rng.below(256) / 255.0f;
				p[3] = 1.0f;
				break;
			default:
			{
				for (int c = 0; c < 3; c++)
					p[c] = clamp01(0.5f + 0.25f * sinf(x * f[c] + y * f[c + 3]) + 0.15f * sinf((x + y) * f[6] * (c + 1)) + noise(0.08f));
				p[3] = 1.0f;
				float d = sinf(x * f[6] * 0.7f) * cosf(y * f[7] * 0.7f);
				if (kind == 3)
					p[3] = d > 0.1f ? 1.0f : 0.0f;
				else if (kind == 4)
					p[3] = clamp01(0.5f + 0.5f * d + noise(0.05f));
				else if (kind == 5)
				{
					p[0] = glyphColor[0]; p[1] = glyphColor[1]; p[2] = glyphColor[2];
					p[3] = clamp01((d - 0.2f) * 4.0f);
				}
				break;
			}
			}
		}
	}
	return img;
}

static uint16 to565(const float* c)
{
	uint32 r = (uint32)lroundf(std::clamp(c[0], 0.0f, 1.0f) * 31.0f);
	uint32 g = (uint32)lroundf(std::clamp(c[1], 0.0f, 1.0f) * 63.0f);
	uint32 b = (uint32)lroundf(std::clamp(c[2], 0.0f, 1.0f) * 31.0f);
	return (uint16)((r << 11) | (g << 5) | b);
}

static void from565(uint16 c, float* out)
{
	out[0] = ((c >> 11) & 31) / 31.0f;
	out[1] = ((c >> 5) & 63) / 63.0f;
	out[2] = (c & 31) / 31.0f;
}

// simple BC color block: bounding box endpoints, nearest palette index. threeColor: c0 <= c1 mode, texels with
// transparent[i] get index 3
static void encodeColorBlock(float texels[16][4], bool threeColor, const bool transparent[16], uint8* out)
{
	float lo[3] = { 1, 1, 1 }, hi[3] = { 0, 0, 0 };
	for (int i = 0; i < 16; i++)
	{
		if (transparent && transparent[i])
			continue;
		for (int c = 0; c < 3; c++)
		{
			lo[c] = std::min(lo[c], texels[i][c]);
			hi[c] = std::max(hi[c], texels[i][c]);
		}
	}
	if (lo[0] > hi[0])
	{
		for (int c = 0; c < 3; c++) lo[c] = hi[c] = 0;
	}
	uint16 c0 = to565(hi), c1 = to565(lo);
	if (!threeColor && c0 < c1) std::swap(c0, c1);
	if (threeColor && c0 > c1) std::swap(c0, c1);
	float pal[4][3];
	from565(c0, pal[0]);
	from565(c1, pal[1]);
	for (int c = 0; c < 3; c++)
	{
		if (c0 > c1)
		{
			pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
			pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
		}
		else
		{
			pal[2][c] = (pal[0][c] + pal[1][c]) / 2;
			pal[3][c] = 0;
		}
	}
	uint32 indices = 0;
	for (int i = 0; i < 16; i++)
	{
		int best = 0;
		if (transparent && transparent[i])
			best = 3;
		else
		{
			float bestD = 1e9f;
			for (int k = 0; k < (c0 > c1 ? 4 : 3); k++)
			{
				float d = 0;
				for (int c = 0; c < 3; c++) d += (pal[k][c] - texels[i][c]) * (pal[k][c] - texels[i][c]);
				if (d < bestD) { bestD = d; best = k; }
			}
		}
		indices |= (uint32)best << (2 * i);
	}
	memcpy(out, &c0, 2);
	memcpy(out + 2, &c1, 2);
	memcpy(out + 4, &indices, 4);
}

static void encodeBC3Alpha(float texels[16][4], bool sixValueMode, uint8* out)
{
	int lo = 255, hi = 0;
	int a[16];
	for (int i = 0; i < 16; i++)
	{
		a[i] = (int)lroundf(texels[i][3] * 255.0f);
		if (sixValueMode && (a[i] == 0 || a[i] == 255))
			continue;
		lo = std::min(lo, a[i]);
		hi = std::max(hi, a[i]);
	}
	if (lo > hi) lo = hi = 0;
	int pal[8];
	if (!sixValueMode)
	{
		out[0] = (uint8)hi; out[1] = (uint8)lo;
	}
	else
	{
		out[0] = (uint8)lo; out[1] = (uint8)hi;
	}
	int a0 = out[0], a1 = out[1];
	pal[0] = a0; pal[1] = a1;
	if (a0 > a1)
		for (int k = 1; k <= 6; k++) pal[k + 1] = ((7 - k) * a0 + k * a1) / 7;
	else
	{
		for (int k = 1; k <= 4; k++) pal[k + 1] = ((5 - k) * a0 + k * a1) / 5;
		pal[6] = 0; pal[7] = 255;
	}
	uint64 codes = 0;
	for (int i = 0; i < 16; i++)
	{
		int best = 0;
		for (int k = 1; k < 8; k++)
			if (std::abs(pal[k] - a[i]) < std::abs(pal[best] - a[i])) best = k;
		codes |= (uint64)best << (3 * i);
	}
	memcpy(out + 2, &codes, 6);
}

// BC blocks of an image. format 1: BC1 (alpha < 0.5 -> transparent, three color mode in such blocks), 2: BC2, 3: BC3
static void encodeImage(TestRng& rng, Image& img, int format, std::vector<uint8>& blocks)
{
	const sint32 blockBytes = format == 1 ? 8 : 16;
	for (sint32 by = 0; by < img.h; by += 4)
	{
		for (sint32 bx = 0; bx < img.w; bx += 4)
		{
			float texels[16][4];
			bool transparent[16];
			bool anyTransparent = false;
			for (int i = 0; i < 16; i++)
			{
				memcpy(texels[i], img.at(bx + (i & 3), by + (i >> 2)), 16);
				transparent[i] = texels[i][3] < 0.5f;
				anyTransparent |= transparent[i];
			}
			size_t offset = blocks.size();
			blocks.resize(offset + blockBytes);
			uint8* b = blocks.data() + offset;
			if (format == 1)
				encodeColorBlock(texels, anyTransparent, anyTransparent ? transparent : nullptr, b);
			else
			{
				encodeColorBlock(texels, false, nullptr, b + 8);
				if (format == 3)
					encodeBC3Alpha(texels, rng.below(4) == 0, b);
				else
				{
					uint64 nibbles = 0;
					for (int i = 0; i < 16; i++)
						nibbles |= (uint64)lroundf(texels[i][3] * 15.0f) << (4 * i);
					memcpy(b, &nibbles, 8);
				}
			}
		}
	}
}

/* error statistics */

struct ErrorStats
{
	uint64 blocks = 0;
	uint64 texels = 0;
	uint32 maxRgb = 0, maxAlpha = 0, maxPremul = 0;
	double sumAbs = 0, sumSq = 0; // RGBA vs exact
	double sumSqRef = 0; // RGBA vs the existing RGBA8 decoder
	double sumSqPremul = 0;
	uint32 maxSrgb = 0; // sRGB decode vs exact (sRGB-encoded bytes)

	void add(const ErrorStats& o)
	{
		blocks += o.blocks; texels += o.texels;
		maxRgb = std::max(maxRgb, o.maxRgb); maxAlpha = std::max(maxAlpha, o.maxAlpha); maxPremul = std::max(maxPremul, o.maxPremul);
		maxSrgb = std::max(maxSrgb, o.maxSrgb);
		sumAbs += o.sumAbs; sumSq += o.sumSq; sumSqRef += o.sumSqRef; sumSqPremul += o.sumSqPremul;
	}
	static double psnr(double sumSq, uint64 n) { return sumSq == 0 ? 999.0 : 10.0 * log10(255.0 * 255.0 / (sumSq / (double)n)); }
	void print(const char* label) const
	{
		const uint64 n = texels * 4;
		printf("  %-28s %8llu blocks  max rgb %3u a %3u premul %3u srgb %3u  mean %.3f  PSNR %6.2f dB (vs RGBA8 decoder %6.2f, premul %6.2f)\n",
			label, (unsigned long long)blocks, maxRgb, maxAlpha, maxPremul, maxSrgb, sumAbs / (double)n, psnr(sumSq, n), psnr(sumSqRef, n), psnr(sumSqPremul, n));
	}
};

// which encoding the transcoder chose, from the block bits
static std::string pathOf(const uint8* astc)
{
	uint64 lo;
	memcpy(&lo, astc, 8);
	if ((lo & 0x1FF) == 0x1FC)
		return "void extent";
	astc_ref::BlockMode bm;
	if (!astc_ref::DecodeBlockMode((uint32)(lo & 0x7FF), bm))
		return "invalid";
	char s[64];
	snprintf(s, sizeof(s), "mode %u, %u level weights%s", (uint32)((lo >> 13) & 15), astc_ref::kRanges[bm.rangeIndex].levels, bm.dualPlane ? " x2" : "");
	return s;
}

using transcodeFn = void(const uint8*, uint8*);

static void exactDecode(int format, uint8* block, uint8 out[64])
{
	float f[64];
	if (format == 1) decodeBC1Block(block, f);
	else if (format == 2) decodeBC2Block_UNORM(block, f);
	else decodeBC3Block_UNORM(block, f);
	for (int i = 0; i < 64; i++)
		out[i] = (uint8)lroundf(f[i] * 255.0f);
}

static void measureBlock(int format, uint8* block, std::map<std::string, ErrorStats>& stats)
{
	static transcodeFn* const fns[4] = { nullptr, transcodeBC1BlockToASTC4x4, transcodeBC2BlockToASTC4x4, transcodeBC3BlockToASTC4x4 };
	uint8 astc[16];
	fns[format](block, astc);
	uint8 exact[64], ref8[64], unorm[64], srgb[64];
	exactDecode(format, block, exact);
	if (format == 1) decodeBC1Block_ToRGBA8(block, ref8, 16, 4, 4);
	else if (format == 2) decodeBC2Block_ToRGBA8(block, ref8, 16, 4, 4);
	else decodeBC3Block_ToRGBA8(block, ref8, 16, 4, 4);
	astc_ref::Result r1 = astc_ref::DecodeBlock(astc, false, unorm);
	astc_ref::Result r2 = astc_ref::DecodeBlock(astc, true, srgb);
	TEST_CHECK(r1 == astc_ref::Result::Ok && r2 == astc_ref::Result::Ok, "BC%d block decodes to an %s ASTC block (%s)", format,
		r1 == astc_ref::Result::Error ? "illegal" : "unsupported", pathOf(astc).c_str());
	ErrorStats& s = stats[pathOf(astc)];
	s.blocks++;
	s.texels += 16;
	for (int i = 0; i < 16; i++)
	{
		for (int c = 0; c < 4; c++)
		{
			const int e = std::abs((int)unorm[i * 4 + c] - (int)exact[i * 4 + c]);
			const int er = (int)unorm[i * 4 + c] - (int)ref8[i * 4 + c];
			// premultiplied: RGB scaled by alpha, so the RGB of transparent texels doesn't count
			const int pu = c == 3 ? unorm[i * 4 + 3] : (unorm[i * 4 + c] * unorm[i * 4 + 3] + 127) / 255;
			const int pe = c == 3 ? exact[i * 4 + 3] : (exact[i * 4 + c] * exact[i * 4 + 3] + 127) / 255;
			if (c == 3) s.maxAlpha = std::max<uint32>(s.maxAlpha, e);
			else s.maxRgb = std::max<uint32>(s.maxRgb, e);
			s.maxPremul = std::max<uint32>(s.maxPremul, std::abs(pu - pe));
			s.maxSrgb = std::max<uint32>(s.maxSrgb, std::abs((int)srgb[i * 4 + c] - (int)exact[i * 4 + c]));
			s.sumAbs += e;
			s.sumSq += e * e;
			s.sumSqRef += er * er;
			s.sumSqPremul += (pu - pe) * (pu - pe);
		}
	}
}

static const char* kFormatNames[4] = { "", "BC1", "BC2", "BC3" };

static void report(const char* title, int format, const std::map<std::string, ErrorStats>& stats, ErrorStats* totalOut = nullptr)
{
	printf("%s %s\n", kFormatNames[format], title);
	ErrorStats total;
	for (auto& [path, s] : stats)
	{
		s.print(path.c_str());
		total.add(s);
	}
	total.print("all");
	if (totalOut)
		*totalOut = total;
}

// upper bounds of the error per path, so a regression is caught. Derivation in the comments
static void checkBounds(int format, const std::map<std::string, ErrorStats>& stats)
{
	for (auto& [path, s] : stats)
	{
		uint32 maxRgb = 255, maxAlpha = 255, maxPremul = 255;
		if (path == "void extent")
			maxRgb = maxAlpha = maxPremul = 1; // interpolants rounded to 8 bits
		else if (path == "mode 8, 4 level weights" || path == "mode 12, 4 level weights")
		{
			// exact endpoints; weights 21/64 and 43/64 instead of 1/3 and 2/3: |c1 - c0| / 192 <= 1.33 plus rounding.
			// Mode 12 with 4 levels is also BC1 "transparent + one color": exact
			maxRgb = 2;
			maxAlpha = 1;
			maxPremul = 2;
		}
		else if (path == "mode 8, 3 level weights")
			maxRgb = maxAlpha = maxPremul = 1; // exact endpoints and midpoint
		else if (path == "mode 12, 3 level weights x2")
		{
			// BC1 transparent + colors: 7 bit endpoints (+-1) and the midpoint; transparent texels keep the darker
			// endpoint's RGB (only premultiplied RGB is bounded)
			maxAlpha = 0;
			maxPremul = 2;
		}
		else if (path == "mode 12, 8 level weights" || path == "mode 12, 16 level weights" || path.rfind("mode 4, ", 0) == 0)
		{
			// constant color, alpha driving one plane (the candidate with the smallest error). RGB: 192 or 48 level
			// endpoints (+-0.7 / +-2.7) plus the rounding of the BC interpolant; gray: 256 or 192 levels.
			// Alpha: BC3's 8 levels between a0 and a1 fit 8 weight levels within ~2, but the six value mode (4 steps of
			// 1/5 between a0 and a1, plus 0 and 255) doesn't fit any evenly spaced levels between 0 and 255: up to 19
			// measured on random blocks. Bounds are the measured maxima (regression guard)
			maxRgb = path == "mode 12, 16 level weights" ? 4 : 2;
			maxAlpha = 19;
		}
		else if (path == "mode 12, 4 level weights x2")
		{
			// BC2/BC3 alpha and color varying: 48 level endpoints (+-3 at the endpoints, ~4 on interpolants); alpha
			// reduced to 4 levels between its extremes: up to 1/6 of the block's alpha range (BC3) plus rounding, more
			// for BC2's 16 levels. Bounded by the range check in testAlphaRangeBound
			maxRgb = 5;
		}
		else
		{
			TEST_CHECK(false, "%s: unexpected path %s", kFormatNames[format], path.c_str());
			continue;
		}
		TEST_CHECK(s.maxRgb <= maxRgb, "%s %s: rgb error %u > %u", kFormatNames[format], path.c_str(), s.maxRgb, maxRgb);
		TEST_CHECK(s.maxAlpha <= maxAlpha, "%s %s: alpha error %u > %u", kFormatNames[format], path.c_str(), s.maxAlpha, maxAlpha);
		TEST_CHECK(s.maxPremul <= maxPremul, "%s %s: premultiplied error %u > %u", kFormatNames[format], path.c_str(), s.maxPremul, maxPremul);
	}
}

// two plane BC2/BC3 blocks: the alpha error stays within 1/6 of the block's alpha range (4 levels between the
// extremes) plus the endpoint quantization (48 levels: +-3) and rounding
static void testAlphaRangeBound(TestRng& rng)
{
	for (int format : { 2, 3 })
	{
		uint32 worstExcess = 0;
		for (int n = 0; n < 200000; n++)
		{
			uint8 block[16];
			randomBlock(rng, block, 16);
			uint8 astc[16], out[64], exact[64];
			(format == 2 ? transcodeBC2BlockToASTC4x4 : transcodeBC3BlockToASTC4x4)(block, astc);
			if (pathOf(astc) != "mode 12, 4 level weights x2")
				continue;
			astc_ref::DecodeBlock(astc, false, out);
			exactDecode(format, block, exact);
			int lo = 255, hi = 0;
			for (int i = 0; i < 16; i++) { lo = std::min<int>(lo, exact[i * 4 + 3]); hi = std::max<int>(hi, exact[i * 4 + 3]); }
			const int bound = (hi - lo + 5) / 6 + 4;
			for (int i = 0; i < 16; i++)
			{
				const int e = std::abs((int)out[i * 4 + 3] - (int)exact[i * 4 + 3]);
				if (e > bound)
					worstExcess = std::max<uint32>(worstExcess, e - bound);
			}
		}
		TEST_CHECK(worstExcess == 0, "%s two planes: alpha error over range / 6 + 4 by %u", kFormatNames[format], worstExcess);
	}
}

// the in place whole-texture functions give the same blocks as the single block ones
static void testInPlace(TestRng& rng)
{
	for (int format = 1; format <= 3; format++)
	{
		const sint32 blockBytes = format == 1 ? 8 : 16;
		const size_t count = 1001;
		std::vector<uint8> src(count * blockBytes), buffer(count * 16), expected(count * 16);
		for (size_t i = 0; i < count; i++)
			randomBlock(rng, src.data() + i * blockBytes, blockBytes);
		memcpy(buffer.data(), src.data(), src.size());
		static transcodeFn* const fns[4] = { nullptr, transcodeBC1BlockToASTC4x4, transcodeBC2BlockToASTC4x4, transcodeBC3BlockToASTC4x4 };
		for (size_t i = 0; i < count; i++)
			fns[format](src.data() + i * blockBytes, expected.data() + i * 16);
		if (format == 1) transcodeBC1ToASTC4x4InPlace(buffer.data(), count);
		else if (format == 2) transcodeBC2ToASTC4x4InPlace(buffer.data(), count);
		else transcodeBC3ToASTC4x4InPlace(buffer.data(), count);
		TEST_CHECK(memcmp(buffer.data(), expected.data(), expected.size()) == 0, "%s in place", kFormatNames[format]);
	}
}

static void benchmark(const char* label, int format, const std::vector<uint8>& blocks)
{
	const sint32 blockBytes = format == 1 ? 8 : 16;
	const size_t count = blocks.size() / blockBytes;
	std::vector<uint8> buffer(count * 16);
	std::vector<uint8> rgba(count * 64);
	double tTranscode = testBenchmark([&]()
	{
		memcpy(buffer.data(), blocks.data(), blocks.size());
		if (format == 1) transcodeBC1ToASTC4x4InPlace(buffer.data(), count);
		else if (format == 2) transcodeBC2ToASTC4x4InPlace(buffer.data(), count);
		else transcodeBC3ToASTC4x4InPlace(buffer.data(), count);
	});
	// the RGBA8 decoder on the same blocks, as a 4 block wide strip per row of 1024 texels
	const sint32 blocksPerRow = 256;
	double tRgba = testBenchmark([&]()
	{
		for (size_t i = 0; i < count; i++)
		{
			uint8* in = (uint8*)blocks.data() + i * blockBytes;
			uint8* out = rgba.data() + ((i / blocksPerRow) * 4 * 1024 + (i % blocksPerRow) * 4) * 4;
			if (format == 1) decodeBC1Block_ToRGBA8(in, out, 1024 * 4, 4, 4);
			else if (format == 2) decodeBC2Block_ToRGBA8(in, out, 1024 * 4, 4, 4);
			else decodeBC3Block_ToRGBA8(in, out, 1024 * 4, 4, 4);
		}
	});
	printf("  %s %-10s %7.1f M blocks/s transcode (%.2f ms per 1024x1024), RGBA8 decode %7.1f M blocks/s (%.2f ms)\n",
		kFormatNames[format], label, count / tTranscode / 1e6, tTranscode / count * 65536 * 1000, count / tRgba / 1e6, tRgba / count * 65536 * 1000);
}

int main(int argc, char** argv)
{
	TestRng rng(1234);
	testReferenceDecoder();
	testInPlace(rng);

	// random blocks with corner cases
	const int randomCount = 300000;
	std::map<std::string, ErrorStats> randomStats[4], imageStats[4];
	for (int format = 1; format <= 3; format++)
	{
		const sint32 blockBytes = format == 1 ? 8 : 16;
		for (int n = 0; n < randomCount; n++)
		{
			uint8 block[16];
			randomBlock(rng, block, blockBytes);
			measureBlock(format, block, randomStats[format]);
		}
	}
	// every BC1 endpoint pair with ordered palettes is too much; every c0 with 64 random c1, each block using all indices
	for (uint32 c0 = 0; c0 < 0x10000; c0 += 1)
	{
		for (int rep = 0; rep < 4; rep++)
		{
			uint8 block[8];
			uint16 c1 = rep == 0 ? (uint16)c0 : (uint16)rng.next();
			uint32 indices = rep < 2 ? 0xE4E4E4E4u : (uint32)rng.next();
			memcpy(block, &c0, 2);
			memcpy(block + 2, &c1, 2);
			memcpy(block + 4, &indices, 4);
			measureBlock(1, block, randomStats[1]);
		}
	}

	// synthetic images
	const char* kindNames[6] = { "gradient", "natural", "noise", "cutout", "soft alpha", "glyphs" };
	std::vector<uint8> imageBlocks[4];
	for (int kind = 0; kind < 6; kind++)
	{
		for (int seed = 0; seed < 4; seed++)
		{
			Image img = makeImage(rng, kind, 256);
			for (int format = 1; format <= 3; format++)
			{
				std::vector<uint8> blocks;
				encodeImage(rng, img, format, blocks);
				const sint32 blockBytes = format == 1 ? 8 : 16;
				for (size_t i = 0; i < blocks.size(); i += blockBytes)
					measureBlock(format, blocks.data() + i, imageStats[format]);
				imageBlocks[format].insert(imageBlocks[format].end(), blocks.begin(), blocks.end());
			}
		}
	}
	(void)kindNames;

	printf("error vs the exact BC decode (8 bit, rounded), ASTC decoded as UNORM; srgb = ASTC decoded as sRGB vs the same bytes\n");
	for (int format = 1; format <= 3; format++)
	{
		report("random blocks", format, randomStats[format]);
		report("synthetic images (gradient, natural, noise, cutout, soft alpha, glyphs)", format, imageStats[format]);
		checkBounds(format, randomStats[format]);
		checkBounds(format, imageStats[format]);
	}
	testAlphaRangeBound(rng);

	printf("speed (host, single thread, 65536+ blocks, includes the memcpy of the BC blocks into the buffer)\n");
	for (int format = 1; format <= 3; format++)
	{
		std::vector<uint8> random(65536 * (format == 1 ? 8 : 16));
		for (size_t i = 0; i < random.size(); i += (format == 1 ? 8 : 16))
			randomBlock(rng, random.data() + i, format == 1 ? 8 : 16);
		benchmark("random", format, random);
		benchmark("images", format, imageBlocks[format]);
	}
	return testReport("bc_astc_test");
}
