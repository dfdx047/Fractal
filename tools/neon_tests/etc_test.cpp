// BCn to ETC2/EAC transcoding (GPUs without BCn): the transcoded blocks, decoded by an independent reference decoder
// (etc_reference.h), against the RGBA8/R8/RG8 decode of the BC blocks. Reports max/mean error, PSNR and speed.
// Hard checks: BC1 transparency is kept exactly, constant blocks stay (nearly) exact, the transcoders write only their
// block. Real textures can be added with: etc_test --dds file.dds ... (DXT1/3/5, ATI1/2, BC4U/S, BC5U/S, DX10 BC1-5)
// HOST_TEST_OK: everything is portable C++ (HOST=1 build.sh)
#include "Cafe/HW/Latte/Core/LatteTextureLoaderBC.cpp"
#include "Cafe/HW/Latte/Core/LatteTextureTranscodeETC.cpp"
#include "etc_reference.h"
#include "test_common.h"
#include <cstdlib>

struct ErrorStats
{
	double sse = 0;
	double absSum = 0;
	double maxAbs = 0;
	uint64 count = 0;
	void add(double d)
	{
		sse += d * d;
		absSum += std::abs(d);
		maxAbs = std::max(maxAbs, std::abs(d));
		count++;
	}
	double psnr(double peak) const
	{
		if (count == 0 || sse == 0)
			return 99.0;
		return 10.0 * std::log10(peak * peak / (sse / (double)count));
	}
	void print(const char* name, double peak) const
	{
		printf("  %-34s max %6.2f  mean %6.3f  PSNR %6.2f dB  (%llu values)\n", name, maxAbs, count ? absSum / (double)count : 0.0, psnr(peak), (unsigned long long)count);
	}
};

static void resetCaches()
{
	t_colorCache.reset();
	for (auto& modeCache : s_eacCache)
		for (auto& variant : modeCache)
			for (auto& e : variant)
				e.store(0, std::memory_order_relaxed);
}

/* the transcoders of a format on an array of blocks */

enum Format { F_BC1 = 1, F_BC2, F_BC3, F_BC4, F_BC5 };

static sint32 blockBytesOf(int format)
{
	return (format == F_BC1 || format == F_BC4) ? 8 : 16;
}

static void transcodeBlocks(int format, bool snorm, const uint8* in, uint8* out, size_t numBlocks)
{
	const sint32 bytes = blockBytesOf(format);
	for (size_t i = 0; i < numBlocks; i++)
	{
		const uint8* b = in + i * bytes;
		uint8* o = out + i * bytes;
		switch (format)
		{
		case F_BC1: transcodeBC1Block_ToETC2_RGB8A1(b, o); break;
		case F_BC2: transcodeBC2Block_ToETC2_RGBA8(b, o); break;
		case F_BC3: transcodeBC3Block_ToETC2_RGBA8(b, o); break;
		case F_BC4: transcodeBC4Block_ToEAC_R11(b, o, snorm); break;
		case F_BC5: transcodeBC5Block_ToEAC_RG11(b, o, snorm); break;
		}
	}
}

/* quality */

struct QualityResult
{
	ErrorStats vsDecode;      // vs the RGBA8/R8/RG8 decode of the BC block (what the GPU samples today)
	ErrorStats vsExact;       // vs the exact BC palette value (float decode)
	ErrorStats alpha;         // alpha channel (BC2/BC3), vs the RGBA8 decode
	uint64 alphaMismatch = 0; // BC1: transparent <-> opaque changes (must be 0)
};

// BC4 or one BC5 channel: EAC value (float) against the R8 byte and the float palette, in 8 bit steps
static void compareEACChannel(const uint8* bcGroup, const uint8* eacBlock, bool snorm, bool bc5, QualityResult& r)
{
	float exact[16];
	if (bc5)
	{
		float rg[32];
		uint8 tmp[16];
		memcpy(tmp, bcGroup, 8);
		memcpy(tmp + 8, bcGroup, 8);
		if (snorm)
			decodeBC5Block_SNORM(tmp, rg);
		else
			decodeBC5Block_UNORM(tmp, rg);
		for (int i = 0; i < 16; i++)
			exact[i] = rg[i * 2];
	}
	else
	{
		uint8 tmp[8];
		memcpy(tmp, bcGroup, 8);
		if (snorm)
			decodeBC4Block_SNORM(tmp, exact);
		else
			decodeBC4Block_UNORM(tmp, exact);
	}
	int eac[16];
	etc_ref::DecodeEAC(eacBlock, snorm ? etc_ref::EAC_R11_SNORM : etc_ref::EAC_R11_UNORM, eac);
	const double scale8 = snorm ? 127.0 : 255.0;
	for (int i = 0; i < 16; i++)
	{
		const double eacValue = snorm ? (double)eac[i] / 1023.0 : (double)eac[i] / 2047.0;
		const uint8 byte = bcDecodedToByte(exact[i], snorm);
		const double byteValue = snorm ? (double)(sint8)byte : (double)byte;
		r.vsDecode.add(eacValue * scale8 - byteValue);
		r.vsExact.add((eacValue - (double)exact[i]) * scale8);
	}
}

static void compareColor(int format, const uint8* bcBlock, const uint8* etcBlock, QualityResult& r)
{
	uint8 ref[16 * 4];
	switch (format)
	{
	case F_BC1: decodeBC1Block_ToRGBA8(bcBlock, ref, 16, 4, 4); break;
	case F_BC2: decodeBC2Block_ToRGBA8(bcBlock, ref, 16, 4, 4); break;
	default: decodeBC3Block_ToRGBA8(bcBlock, ref, 16, 4, 4); break;
	}
	float exact[64];
	switch (format)
	{
	case F_BC1: decodeBC1Block((uint8*)bcBlock, exact); break;
	case F_BC2: decodeBC2Block_UNORM((uint8*)bcBlock, exact); break;
	default: decodeBC3Block_UNORM((uint8*)bcBlock, exact); break;
	}
	uint8 etc[16][4];
	if (format == F_BC1)
		etc_ref::DecodeETC2(etcBlock, true, etc);
	else
	{
		etc_ref::DecodeETC2(etcBlock + 8, false, etc);
		int alpha[16];
		etc_ref::DecodeEAC(etcBlock, etc_ref::EAC_ALPHA8, alpha);
		for (int i = 0; i < 16; i++)
		{
			etc[i][3] = (uint8)alpha[i];
			r.alpha.add((double)alpha[i] - (double)ref[i * 4 + 3]);
		}
	}
	for (int i = 0; i < 16; i++)
	{
		const uint8* d = ref + i * 4;
		if (format == F_BC1)
		{
			const bool refTransparent = d[3] == 0;
			const bool etcTransparent = etc[i][3] == 0;
			if (refTransparent != etcTransparent || (etcTransparent && (etc[i][0] | etc[i][1] | etc[i][2])) || (!etcTransparent && etc[i][3] != 255))
				r.alphaMismatch++;
			if (refTransparent)
				continue;
		}
		for (int c = 0; c < 3; c++)
		{
			r.vsDecode.add((double)etc[i][c] - (double)d[c]);
			r.vsExact.add((double)etc[i][c] - (double)exact[i * 4 + c] * 255.0);
		}
	}
}

static QualityResult evaluate(int format, bool snorm, const std::vector<uint8>& blocks)
{
	QualityResult r;
	const sint32 bytes = blockBytesOf(format);
	const size_t numBlocks = blocks.size() / bytes;
	std::vector<uint8> out(blocks.size() + 16, 0xA5);
	transcodeBlocks(format, snorm, blocks.data(), out.data(), numBlocks);
	for (int i = 0; i < 16; i++)
		TEST_CHECK(out[blocks.size() + i] == 0xA5, "format %d wrote past its blocks", format);
	for (size_t i = 0; i < numBlocks; i++)
	{
		const uint8* b = blocks.data() + i * bytes;
		const uint8* o = out.data() + i * bytes;
		if (format == F_BC4)
			compareEACChannel(b, o, snorm, false, r);
		else if (format == F_BC5)
		{
			compareEACChannel(b, o, snorm, true, r);
			compareEACChannel(b + 8, o + 8, snorm, true, r);
		}
		else
			compareColor(format, b, o, r);
	}
	return r;
}

static void printQuality(const char* name, int format, bool snorm, const QualityResult& r)
{
	printf("%s\n", name);
	const double peak = snorm ? 254.0 : 255.0;
	if (format == F_BC4 || format == F_BC5)
	{
		r.vsDecode.print(snorm ? "vs R8/RG8 SNORM decode (8 bit steps)" : "vs R8/RG8 decode (8 bit steps)", peak);
		r.vsExact.print("vs exact BC value", peak);
	}
	else
	{
		r.vsDecode.print("RGB vs RGBA8 decode", 255.0);
		r.vsExact.print("RGB vs exact BC value", 255.0);
		if (format != F_BC1)
			r.alpha.print("alpha vs RGBA8 decode", 255.0);
		else
			printf("  transparency changes: %llu\n", (unsigned long long)r.alphaMismatch);
	}
}

/* block sets */

// random blocks with the corner cases of bc_test (equal / ordered endpoints, -128/-127 signed endpoints)
static std::vector<uint8> randomBlocks(TestRng& rng, int format, size_t numBlocks)
{
	const sint32 bytes = blockBytesOf(format);
	std::vector<uint8> blocks(numBlocks * bytes);
	rng.fill(blocks.data(), blocks.size());
	for (size_t i = 0; i < numBlocks; i++)
	{
		uint8* b = blocks.data() + i * bytes;
		for (sint32 group = 0; group < bytes; group += 8)
		{
			uint8* g = b + group;
			switch (rng.below(8))
			{
			case 0: g[1] = g[0]; break;
			case 1: g[0] = 0x80; g[1] = 0x81; break;
			case 2: g[0] = 0x81; g[1] = 0x80; break;
			case 3: g[0] = 0xFF; g[1] = 0x00; break;
			case 4: g[2] = g[0]; g[3] = g[1]; break;
			default: break;
			}
		}
	}
	return blocks;
}

// smooth content: endpoints close to each other (gradients, typical of real textures), random indices
static std::vector<uint8> smoothBlocks(TestRng& rng, int format, size_t numBlocks)
{
	const sint32 bytes = blockBytesOf(format);
	std::vector<uint8> blocks(numBlocks * bytes);
	rng.fill(blocks.data(), blocks.size());
	for (size_t i = 0; i < numBlocks; i++)
	{
		uint8* b = blocks.data() + i * bytes;
		const sint32 spread = 1 + rng.below(24);
		auto near8 = [&](uint8 v) { return (uint8)std::clamp((sint32)v + (sint32)rng.below(2 * spread + 1) - spread, 0, 255); };
		if (format == F_BC4 || format == F_BC5 || format == F_BC3)
		{
			b[1] = near8(b[0]);
			if (format == F_BC5)
				b[9] = near8(b[8]);
		}
		if (format == F_BC1 || format == F_BC2 || format == F_BC3)
		{
			uint8* c = b + (format == F_BC1 ? 0 : 8);
			const uint16 c0 = c[0] | (c[1] << 8);
			sint32 r = (c0 >> 11) & 31, g = (c0 >> 5) & 63, bl = c0 & 31;
			const sint32 s = spread / 6;
			r = std::clamp(r + (sint32)rng.below(2 * s + 1) - s, 0, 31);
			g = std::clamp(g + (sint32)rng.below(4 * s + 1) - 2 * s, 0, 63);
			bl = std::clamp(bl + (sint32)rng.below(2 * s + 1) - s, 0, 31);
			const uint16 c1 = (uint16)((r << 11) | (g << 5) | bl);
			c[2] = (uint8)c1;
			c[3] = (uint8)(c1 >> 8);
		}
	}
	return blocks;
}

/* DDS files */

struct DDSImage
{
	std::string name;
	int format;
	bool snorm;
	int width, height;
	std::vector<uint8> blocks;
};

static bool loadDDS(const char* path, DDSImage& img)
{
	FILE* f = fopen(path, "rb");
	if (!f)
		return false;
	uint8 header[148];
	if (fread(header, 1, 128, f) != 128 || memcmp(header, "DDS ", 4) != 0)
	{
		fclose(f);
		return false;
	}
	auto u32 = [&](int off) { uint32 v; memcpy(&v, header + off, 4); return v; };
	img.height = (int)u32(12);
	img.width = (int)u32(16);
	img.snorm = false;
	char fourcc[5] = {};
	memcpy(fourcc, header + 84, 4);
	std::string fc = fourcc;
	img.format = 0;
	if (fc == "DXT1") img.format = F_BC1;
	else if (fc == "DXT3") img.format = F_BC2;
	else if (fc == "DXT5") img.format = F_BC3;
	else if (fc == "ATI1" || fc == "BC4U") img.format = F_BC4;
	else if (fc == "BC4S") { img.format = F_BC4; img.snorm = true; }
	else if (fc == "ATI2" || fc == "BC5U") img.format = F_BC5;
	else if (fc == "BC5S") { img.format = F_BC5; img.snorm = true; }
	else if (fc == "DX10")
	{
		if (fread(header + 128, 1, 20, f) != 20)
		{
			fclose(f);
			return false;
		}
		switch (u32(128))
		{
		case 71: case 72: img.format = F_BC1; break;
		case 74: case 75: img.format = F_BC2; break;
		case 77: case 78: img.format = F_BC3; break;
		case 80: img.format = F_BC4; break;
		case 81: img.format = F_BC4; img.snorm = true; break;
		case 83: img.format = F_BC5; break;
		case 84: img.format = F_BC5; img.snorm = true; break;
		}
	}
	if (img.format == 0)
	{
		fclose(f);
		return false;
	}
	const size_t size = (size_t)((img.width + 3) / 4) * ((img.height + 3) / 4) * blockBytesOf(img.format);
	img.blocks.resize(size);
	const bool ok = fread(img.blocks.data(), 1, size, f) == size;
	fclose(f);
	img.name = path;
	return ok;
}

/* hard checks */

static void testConstantBlocks()
{
	// BC4/BC5: every value as a constant block (equal endpoints) must stay within half an 8 bit step
	for (int snorm = 0; snorm < 2; snorm++)
	{
		double worst = 0;
		for (int v = 0; v < 256; v++)
		{
			uint8 bc4[8] = { (uint8)v, (uint8)v, 0, 0, 0, 0, 0, 0 };
			uint8 eac[8];
			transcodeBC4Block_ToEAC_R11(bc4, eac, snorm != 0);
			QualityResult r;
			compareEACChannel(bc4, eac, snorm != 0, false, r);
			worst = std::max(worst, r.vsExact.maxAbs);
		}
		TEST_CHECK(worst <= 0.5, "BC4 %s constant block error %.3f", snorm ? "SNORM" : "UNORM", worst);
	}
	// BC1: every 565 color as a solid block must stay within 3 per channel (ETC2 planar / ETC1 precision)
	double worst = 0;
	for (uint32 c = 0; c < 0x10000; c++)
	{
		uint8 bc1[8] = { (uint8)c, (uint8)(c >> 8), (uint8)c, (uint8)(c >> 8), 0, 0, 0, 0 };
		uint8 etc[8];
		transcodeBC1Block_ToETC2_RGB8A1(bc1, etc);
		QualityResult r;
		compareColor(F_BC1, bc1, etc, r);
		worst = std::max(worst, r.vsExact.maxAbs);
		TEST_CHECK(r.alphaMismatch == 0, "BC1 solid %04x alpha", c);
	}
	TEST_CHECK(worst <= 3.0, "BC1 solid block error %.3f", worst);
	printf("BC1 solid colors: max error vs exact %.2f\n", worst);
	// BC2: a constant alpha level must stay exact
	for (int a = 0; a < 16; a++)
	{
		uint8 bc2[16] = {};
		memset(bc2, a * 17, 8);
		uint8 etc[16];
		transcodeBC2Block_ToETC2_RGBA8(bc2, etc);
		int alpha[16];
		etc_ref::DecodeEAC(etc, etc_ref::EAC_ALPHA8, alpha);
		for (int t = 0; t < 16; t++)
			TEST_CHECK(alpha[t] == a * 17, "BC2 constant alpha %d decoded as %d", a * 17, alpha[t]);
	}
}

// BC1 three color blocks with every transparency pattern kind: transparent texels must stay exactly transparent
static void testTransparency(TestRng& rng)
{
	QualityResult r;
	for (int i = 0; i < 200000; i++)
	{
		uint8 bc1[8];
		rng.fill(bc1, 8);
		uint16 c0 = bc1[0] | (bc1[1] << 8), c1 = bc1[2] | (bc1[3] << 8);
		if (c0 > c1)
			std::swap(c0, c1);
		memcpy(bc1, &c0, 2);
		memcpy(bc1 + 2, &c1, 2);
		if (i % 4 == 0)
			memset(bc1 + 4, 0xFF, 4); // all transparent
		uint8 etc[8];
		transcodeBC1Block_ToETC2_RGB8A1(bc1, etc);
		compareColor(F_BC1, bc1, etc, r);
	}
	TEST_CHECK(r.alphaMismatch == 0, "BC1 transparency changed for %llu texels", (unsigned long long)r.alphaMismatch);
	printQuality("BC1 three color + transparent (random)", F_BC1, false, r);
}

/* speed */

static void benchmarkFormat(const char* name, int format, bool snorm, const std::vector<uint8>& blocks)
{
	const sint32 bytes = blockBytesOf(format);
	const size_t numBlocks = blocks.size() / bytes;
	std::vector<uint8> out(blocks.size());
	// cold: empty EAC cache and color block cache, once
	resetCaches();
	double t0 = testNowSeconds();
	transcodeBlocks(format, snorm, blocks.data(), out.data(), numBlocks);
	double cold = testNowSeconds() - t0;
	// warm: EAC endpoint cache filled; the color block cache is cleared every run (the encoder runs for every block
	// that isn't a recent repeat)
	double warm = testBenchmark([&] { t_colorCache.reset(); transcodeBlocks(format, snorm, blocks.data(), out.data(), numBlocks); }, 0.3);
	printf("  speed %-28s cold %7.2f Mblocks/s, warm %7.2f Mblocks/s (%zu blocks)\n", name, numBlocks / cold / 1e6, numBlocks / warm / 1e6, numBlocks);
}

int main(int argc, char** argv)
{
	TestRng rng(5);
	bool bench = true;
	std::vector<const char*> ddsFiles;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--no-bench") == 0)
			bench = false;
		else if (strcmp(argv[i], "--dds") == 0)
		{
			while (i + 1 < argc && argv[i + 1][0] != '-')
				ddsFiles.push_back(argv[++i]);
		}
	}

	testConstantBlocks();
	testTransparency(rng);

	struct Case { const char* name; int format; bool snorm; };
	const Case cases[] = {
		{ "BC4_UNORM -> EAC R11", F_BC4, false }, { "BC4_SNORM -> EAC R11 SNORM", F_BC4, true },
		{ "BC5_UNORM -> EAC RG11", F_BC5, false }, { "BC5_SNORM -> EAC RG11 SNORM", F_BC5, true },
		{ "BC1 -> ETC2 RGB8A1", F_BC1, false }, { "BC2 -> ETC2 RGBA8", F_BC2, false }, { "BC3 -> ETC2 RGBA8", F_BC3, false },
	};
	for (const Case& c : cases)
	{
		auto rnd = randomBlocks(rng, c.format, 200000);
		auto smooth = smoothBlocks(rng, c.format, 200000);
		printf("== %s\n", c.name);
		printQuality("random blocks", c.format, c.snorm, evaluate(c.format, c.snorm, rnd));
		printQuality("smooth blocks (close endpoints)", c.format, c.snorm, evaluate(c.format, c.snorm, smooth));
		if (c.format == F_BC4 || c.format == F_BC5)
		{
			// every endpoint pair, texel i using code i & 7 (all 8 palette entries)
			std::vector<uint8> all(65536 * 8);
			uint64 codes = 0;
			for (int p = 0; p < 16; p++)
				codes |= (uint64)(p & 7) << (3 * p);
			for (uint32 e = 0; e < 65536; e++)
			{
				all[e * 8 + 0] = (uint8)e;
				all[e * 8 + 1] = (uint8)(e >> 8);
				for (int i = 0; i < 6; i++)
					all[e * 8 + 2 + i] = (uint8)(codes >> (8 * i));
			}
			printQuality("every endpoint pair, all 8 entries", F_BC4, c.snorm, evaluate(F_BC4, c.snorm, all));
		}
		if (bench)
		{
			benchmarkFormat("random", c.format, c.snorm, rnd);
			benchmarkFormat("smooth", c.format, c.snorm, smooth);
		}
	}

	// real textures
	for (const char* path : ddsFiles)
	{
		DDSImage img;
		if (!loadDDS(path, img))
		{
			printf("can't load %s\n", path);
			continue;
		}
		const char* base = strrchr(path, '/');
		printf("== %s (%dx%d, BC%d%s)\n", base ? base + 1 : path, img.width, img.height, img.format, img.snorm ? " SNORM" : "");
		printQuality("as is", img.format, img.snorm, evaluate(img.format, img.snorm, img.blocks));
		if (bench)
			benchmarkFormat("as is", img.format, img.snorm, img.blocks);
		if (img.format == F_BC3)
		{
			// the alpha half is a BC4 UNORM block, the color half a BC1 block when color0 > color1
			const size_t n = img.blocks.size() / 16;
			std::vector<uint8> alpha(n * 8), color;
			for (size_t i = 0; i < n; i++)
			{
				memcpy(alpha.data() + i * 8, img.blocks.data() + i * 16, 8);
				const uint8* c = img.blocks.data() + i * 16 + 8;
				if ((c[0] | (c[1] << 8)) > (c[2] | (c[3] << 8)))
					color.insert(color.end(), c, c + 8);
			}
			printQuality("alpha half as BC4", F_BC4, false, evaluate(F_BC4, false, alpha));
			printQuality("color half as BC1 (4 color blocks)", F_BC1, false, evaluate(F_BC1, false, color));
			if (bench)
				benchmarkFormat("color half as BC1", F_BC1, false, color);
		}
	}
	return testReport("etc");
}
