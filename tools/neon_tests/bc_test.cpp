// BCn to RGBA8/R8/RG8 (GPUs without BCn): new palette + NEON decoders vs the previous float decoders converted texel by
// texel (bc_reference_old.h holds the old code verbatim). Also checks that the float decoders still give the same floats.
// HOST_TEST_OK: the portable path can be checked on the PC too (HOST=1 build.sh)
#include "Cafe/HW/Latte/Core/LatteTextureLoaderBC.cpp"
#include "bc_reference_old.h"
#include "test_common.h"

using oldFloatFn = void(uint8*, float*);

// the texel loops of the old TextureDecoder_BCx_To_* classes, on a linear array of blocks
static void oldDecodeRGBA8(int format, uint8* blocks, sint32 blockBytes, sint32 width, sint32 height, uint8* outputData)
{
	sint32 blocksPerRow = (width + 3) / 4;
	for (sint32 y = 0; y < height; y += 4)
	{
		for (sint32 x = 0; x < width; x += 4)
		{
			uint8* blockData = blocks + ((y / 4) * blocksPerRow + (x / 4)) * blockBytes;
			sint32 blockSizeX = (std::min)(4, width - x);
			sint32 blockSizeY = (std::min)(4, height - y);
			float rgbaBlock[4 * 4 * 4];
			if (format == 1)
				old_bc::decodeBC1Block(blockData, rgbaBlock);
			else if (format == 2)
				old_bc::decodeBC2Block_UNORM(blockData, rgbaBlock);
			else
				old_bc::decodeBC3Block_UNORM(blockData, rgbaBlock);
			for (sint32 py = 0; py < blockSizeY; py++)
			{
				sint32 yc = y + py;
				for (sint32 px = 0; px < blockSizeX; px++)
				{
					sint32 pixelOffset = (x + px + yc * width) * 4;
					float red = rgbaBlock[(px + py * 4) * 4 + 0];
					float green = rgbaBlock[(px + py * 4) * 4 + 1];
					float blue = rgbaBlock[(px + py * 4) * 4 + 2];
					float alpha = rgbaBlock[(px + py * 4) * 4 + 3];
					if (format == 3)
					{
						*(outputData + pixelOffset + 0) = (uint8)(red * 255);
						*(outputData + pixelOffset + 1) = (uint8)(green * 255);
						*(outputData + pixelOffset + 2) = (uint8)(blue * 255);
						*(outputData + pixelOffset + 3) = (uint8)(alpha * 255);
					}
					else
					{
						*(outputData + pixelOffset + 0) = red * 255;
						*(outputData + pixelOffset + 1) = green * 255;
						*(outputData + pixelOffset + 2) = blue * 255;
						*(outputData + pixelOffset + 3) = alpha * 255;
					}
				}
			}
		}
	}
}

template<oldFloatFn fn, bool isBC5>
static void oldDecodeR8RG8(uint8* blocks, sint32 width, sint32 height, uint8* outputData, bool snorm)
{
	sint32 blocksPerRow = (width + 3) / 4;
	sint32 blockBytes = isBC5 ? 16 : 8;
	for (sint32 y = 0; y < height; y += 4)
	{
		for (sint32 x = 0; x < width; x += 4)
		{
			uint8* blockData = blocks + ((y / 4) * blocksPerRow + (x / 4)) * blockBytes;
			sint32 blockSizeX = (std::min)(4, width - x);
			sint32 blockSizeY = (std::min)(4, height - y);
			float block[4 * 4 * 2];
			fn(blockData, block);
			for (sint32 py = 0; py < blockSizeY; py++)
			{
				sint32 yc = y + py;
				for (sint32 px = 0; px < blockSizeX; px++)
				{
					if (isBC5)
					{
						sint32 pixelOffset = (x + px + yc * width) * 2;
						outputData[pixelOffset + 0] = bcDecodedToByte(block[(px + py * 4) * 2 + 0], snorm);
						outputData[pixelOffset + 1] = bcDecodedToByte(block[(px + py * 4) * 2 + 1], snorm);
					}
					else
						outputData[x + px + yc * width] = bcDecodedToByte(block[px + py * 4], snorm);
				}
			}
		}
	}
}

// the loops of the new decoders (LatteTextureLoader.h)
static void newDecode(int format, bool snorm, uint8* blocks, sint32 blockBytes, sint32 width, sint32 height, uint8* outputData)
{
	sint32 blocksPerRow = (width + 3) / 4;
	sint32 bpp = format <= 3 ? 4 : (format == 4 ? 1 : 2);
	const sint32 outputPitch = width * bpp;
	for (sint32 y = 0; y < height; y += 4)
	{
		for (sint32 x = 0; x < width; x += 4)
		{
			uint8* blockData = blocks + ((y / 4) * blocksPerRow + (x / 4)) * blockBytes;
			sint32 blockSizeX = (std::min)(4, width - x);
			sint32 blockSizeY = (std::min)(4, height - y);
			uint8* out = outputData + (x + y * width) * bpp;
			switch (format)
			{
			case 1: decodeBC1Block_ToRGBA8(blockData, out, outputPitch, blockSizeX, blockSizeY); break;
			case 2: decodeBC2Block_ToRGBA8(blockData, out, outputPitch, blockSizeX, blockSizeY); break;
			case 3: decodeBC3Block_ToRGBA8(blockData, out, outputPitch, blockSizeX, blockSizeY); break;
			case 4: decodeBC4Block_ToR8(blockData, out, outputPitch, blockSizeX, blockSizeY, snorm); break;
			case 5: decodeBC5Block_ToRG8(blockData, out, outputPitch, blockSizeX, blockSizeY, snorm); break;
			}
		}
	}
}

static void oldDecode(int format, bool snorm, uint8* blocks, sint32 blockBytes, sint32 width, sint32 height, uint8* outputData)
{
	if (format <= 3)
		oldDecodeRGBA8(format, blocks, blockBytes, width, height, outputData);
	else if (format == 4)
	{
		if (snorm)
			oldDecodeR8RG8<old_bc::decodeBC4Block_SNORM, false>(blocks, width, height, outputData, true);
		else
			oldDecodeR8RG8<old_bc::decodeBC4Block_UNORM, false>(blocks, width, height, outputData, false);
	}
	else
	{
		if (snorm)
			oldDecodeR8RG8<old_bc::decodeBC5Block_SNORM, true>(blocks, width, height, outputData, true);
		else
			oldDecodeR8RG8<old_bc::decodeBC5Block_UNORM, true>(blocks, width, height, outputData, false);
	}
}

// random blocks with extra weight on the corner cases (equal / ordered endpoints, -128/-127 signed endpoints)
static void fillBlocks(TestRng& rng, uint8* blocks, size_t numBlocks, sint32 blockBytes)
{
	rng.fill(blocks, numBlocks * blockBytes);
	for (size_t i = 0; i < numBlocks; i++)
	{
		uint8* b = blocks + i * blockBytes;
		for (sint32 group = 0; group < blockBytes; group += 8)
		{
			uint8* g = b + group;
			switch (rng.below(8))
			{
			case 0: g[1] = g[0]; break; // equal endpoints (alpha/BC4/BC5)
			case 1: g[0] = 0x80; g[1] = 0x81; break; // -128 / -127
			case 2: g[0] = 0x81; g[1] = 0x80; break;
			case 3: g[0] = 0xFF; g[1] = 0x00; break;
			case 4: g[2] = g[0]; g[3] = g[1]; break; // equal 565 colors (BC1 at +0)
			default: break;
			}
			if (rng.below(8) == 0)
			{
				// equal 565 colors of BC2/BC3 (at +8)
				if (blockBytes == 16)
				{
					b[10] = b[8];
					b[11] = b[9];
				}
			}
		}
	}
}

static void testFloatDecoders(TestRng& rng)
{
	uint8 block[16];
	float a[64], b[64];
	for (int i = 0; i < 200000; i++)
	{
		fillBlocks(rng, block, 1, 16);
		memset(a, 0, sizeof(a));
		memset(b, 0, sizeof(b));
		old_bc::decodeBC1Block(block, a); decodeBC1Block(block, b);
		TEST_CHECK(memcmp(a, b, 64 * 4) == 0, "float BC1");
		old_bc::decodeBC2Block_UNORM(block, a); decodeBC2Block_UNORM(block, b);
		TEST_CHECK(memcmp(a, b, 64 * 4) == 0, "float BC2");
		old_bc::decodeBC3Block_UNORM(block, a); decodeBC3Block_UNORM(block, b);
		TEST_CHECK(memcmp(a, b, 64 * 4) == 0, "float BC3");
		old_bc::decodeBC4Block_UNORM(block, a); decodeBC4Block_UNORM(block, b);
		TEST_CHECK(memcmp(a, b, 16 * 4) == 0, "float BC4 UNORM");
		old_bc::decodeBC4Block_SNORM(block, a); decodeBC4Block_SNORM(block, b);
		TEST_CHECK(memcmp(a, b, 16 * 4) == 0, "float BC4 SNORM");
		old_bc::decodeBC5Block_UNORM(block, a); decodeBC5Block_UNORM(block, b);
		TEST_CHECK(memcmp(a, b, 32 * 4) == 0, "float BC5 UNORM");
		old_bc::decodeBC5Block_SNORM(block, a); decodeBC5Block_SNORM(block, b);
		TEST_CHECK(memcmp(a, b, 32 * 4) == 0, "float BC5 SNORM");
	}
}

// every endpoint pair of the 8 value palettes (BC3 alpha, BC4, BC5 channels) and 1M BC1/BC2/BC3 color pairs, each
// block holding every index, decoded as a full block (NEON path) and as a 3x3 edge block (scalar path)
static void testPalettesExhaustive(TestRng& rng)
{
	// texel p uses code p & 7 (3 bit) and p & 3 (2 bit)
	uint64 codes3 = 0;
	for (int p = 0; p < 16; p++)
		codes3 |= (uint64)(p & 7) << (3 * p);
	uint32 codes2 = 0;
	for (int p = 0; p < 16; p++)
		codes2 |= (uint32)(p & 3) << (2 * p);
	auto compareBlock = [](int format, bool snorm, uint8* block, const char* what, uint32 e)
	{
		for (sint32 size : { 4, 3 })
		{
			uint8 outOld[64], outNew[64];
			memset(outOld, 0x5A, sizeof(outOld));
			memset(outNew, 0x5A, sizeof(outNew));
			oldDecode(format, snorm, block, format == 1 || format == 4 ? 8 : 16, size, size, outOld);
			newDecode(format, snorm, block, format == 1 || format == 4 ? 8 : 16, size, size, outNew);
			TEST_CHECK(memcmp(outOld, outNew, sizeof(outOld)) == 0, "%s endpoints %08x size %d", what, e, size);
		}
	};
	uint8 block[16];
	for (uint32 e = 0; e < 0x10000; e++)
	{
		rng.fill(block, 16);
		// 8 value palettes
		for (int g = 0; g < 2; g++)
		{
			block[g * 8 + 0] = (uint8)e;
			block[g * 8 + 1] = (uint8)(e >> 8);
			for (int i = 0; i < 6; i++)
				block[g * 8 + 2 + i] = (uint8)(codes3 >> (8 * i));
		}
		compareBlock(3, false, block, "BC3 alpha", e);
		compareBlock(4, false, block, "BC4 UNORM", e);
		compareBlock(4, true, block, "BC4 SNORM", e);
		compareBlock(5, false, block, "BC5 UNORM", e);
		compareBlock(5, true, block, "BC5 SNORM", e);
		// colors: c0 = e, c1 random
		for (int rep = 0; rep < 16; rep++)
		{
			uint16 c1 = (uint16)rng.next();
			if (rep == 0)
				c1 = (uint16)e; // equal colors
			uint8 bc1[8];
			memcpy(bc1, &e, 2);
			memcpy(bc1 + 2, &c1, 2);
			memcpy(bc1 + 4, &codes2, 4);
			compareBlock(1, false, bc1, "BC1", e | (c1 << 16));
			memcpy(block + 8, &e, 2);
			memcpy(block + 10, &c1, 2);
			memcpy(block + 12, &codes2, 4);
			compareBlock(2, false, block, "BC2", e | (c1 << 16));
			compareBlock(3, false, block, "BC3 color", e | (c1 << 16));
		}
	}
}

struct FormatCase
{
	const char* name;
	int format;
	bool snorm;
	sint32 blockBytes;
	sint32 bpp;
};

static const FormatCase s_formats[] = {
	{ "BC1->RGBA8", 1, false, 8, 4 },
	{ "BC2->RGBA8", 2, false, 16, 4 },
	{ "BC3->RGBA8", 3, false, 16, 4 },
	{ "BC4_UNORM->R8", 4, false, 8, 1 },
	{ "BC4_SNORM->R8", 4, true, 8, 1 },
	{ "BC5_UNORM->RG8", 5, false, 16, 2 },
	{ "BC5_SNORM->RG8", 5, true, 16, 2 },
};

static void testImages(TestRng& rng)
{
	const sint32 sizes[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13, 16, 31, 64, 100 };
	for (const FormatCase& f : s_formats)
	{
		for (sint32 w : sizes)
		{
			for (sint32 h : sizes)
			{
				for (int rep = 0; rep < 3; rep++)
				{
					size_t numBlocks = (size_t)((w + 3) / 4) * ((h + 3) / 4);
					std::vector<uint8> blocks(numBlocks * f.blockBytes);
					fillBlocks(rng, blocks.data(), numBlocks, f.blockBytes);
					// one guard row/column worth of bytes after the image catches out of bounds writes
					std::vector<uint8> outOld((size_t)w * h * f.bpp + 64, 0xA5), outNew((size_t)w * h * f.bpp + 64, 0xA5);
					oldDecode(f.format, f.snorm, blocks.data(), f.blockBytes, w, h, outOld.data());
					newDecode(f.format, f.snorm, blocks.data(), f.blockBytes, w, h, outNew.data());
					TEST_CHECK(outOld == outNew, "%s %dx%d", f.name, w, h);
				}
			}
		}
	}
}

static void benchmark()
{
	const sint32 w = 1024, h = 1024;
	TestRng rng(11);
	for (const FormatCase& f : s_formats)
	{
		size_t numBlocks = (size_t)(w / 4) * (h / 4);
		std::vector<uint8> blocks(numBlocks * f.blockBytes);
		fillBlocks(rng, blocks.data(), numBlocks, f.blockBytes);
		std::vector<uint8> out((size_t)w * h * f.bpp);
		double tOld = testBenchmark([&] { oldDecode(f.format, f.snorm, blocks.data(), f.blockBytes, w, h, out.data()); }, 0.3);
		double tNew = testBenchmark([&] { newDecode(f.format, f.snorm, blocks.data(), f.blockBytes, w, h, out.data()); }, 0.3);
		printf("%-15s 1024x1024: old %6.2f ms, new %6.2f ms (x%.1f)\n", f.name, tOld * 1e3, tNew * 1e3, tOld / tNew);
	}
}

int main(int argc, char** argv)
{
	TestRng rng(2);
	testFloatDecoders(rng);
	testPalettesExhaustive(rng);
	testImages(rng);
	int r = testReport("bc");
	if (argc < 2 || strcmp(argv[1], "--no-bench") != 0)
		benchmark();
	return r;
}
