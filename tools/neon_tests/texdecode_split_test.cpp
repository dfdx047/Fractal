// Texture decoding split into row ranges on a worker pool (LatteTextureDecodeSplit): the real TextureDecoder classes on
// tiled surfaces (2D tiled thin1 fast path and generic path, 1D tiled, linear aligned, slice > 0), serial decode vs the
// split decode with 1-7 workers, every byte compared. Then a benchmark of BC1/BC3/BC5 mip chains (GPUs without BCn)
// serial vs the pool, and the cost of one empty pool loop.
//   texdecode_split_test [bench seconds per case, default 0.3]
// HOST_TEST_OK: the portable path can be checked on the PC too (HOST=1 build.sh)
#include <mutex>
#include <memory>
#include <functional>
#define DEFINE_ENUM_FLAG_OPERATORS(T)
#define cemu_assert_unimplemented() ((void)0)
#define assert_dbg() ((void)0)
#define cemu_assert_suspicious() ((void)0)
typedef uint32 DWORD;
// tools/neon_tests/stubs replaces Latte.h, GX2_Surface.h and tga.h with empty headers
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.cpp"
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib_Coord.cpp"
#include "Cafe/HW/Latte/Core/LatteTextureLoaderBC.cpp"
#include "Cafe/HW/Latte/Core/LatteTextureDecodeSplit.cpp"
#include "test_common.h"

using namespace Latte;

struct Surface
{
	std::vector<uint8> data;
	LatteTextureLoaderCtx ctx;
};

// a mip (width x height texels) of a surface whose element is bpp bits (64/128 for BC blocks), as LatteTextureLoader_begin
// sets it up; random contents
static void MakeSurface(Surface& s, sint32 width, sint32 height, bool compressed, uint32 bpp, E_HWTILEMODE tileMode, uint32 slice, uint32 swizzle, TestRng& rng)
{
	LatteTextureLoaderCtx& c = s.ctx;
	memset(&c, 0, sizeof(c));
	c.width = width;
	c.height = height;
	c.fullHeight = height;
	c.stepX = c.stepY = compressed ? 4 : 1;
	c.bpp = bpp;
	c.tileMode = tileMode;
	c.sliceIndex = slice;
	c.pipeSwizzle = swizzle & 1;
	c.bankSwizzle = (swizzle >> 1) & 3;
	const sint32 elemsX = (width + c.stepX - 1) / c.stepX;
	const sint32 elemsY = (height + c.stepY - 1) / c.stepY;
	c.pitch = (elemsX + 63) & ~63;
	c.surfaceInfoHeight = (elemsY + 63) & ~63;
	const uint32 depth = slice + 1;
	c.surfaceInfoDepth = depth;
	// the linear fast path places slices fullHeight (texels) * pitch apart: allocate for that too
	const size_t bytes = (size_t)c.pitch * std::max<sint32>(c.surfaceInfoHeight, height) * (bpp / 8) * (depth + 1) + 65536;
	s.data.resize(bytes);
	rng.fill(s.data.data(), bytes);
	c.inputData = s.data.data();
	SetupCachedSurfaceAddrInfo(&c.computeAddrInfo, slice, 0, bpp, c.pitch, c.surfaceInfoHeight, depth, 1, tileMode, false, c.pipeSwizzle, c.bankSwizzle);
}

struct DecoderCase
{
	const char* name;
	TextureDecoder* decoder;
	bool compressed;
	uint32 bpp;
};

static std::vector<uint8> DecodeSerial(TextureDecoder* d, LatteTextureLoaderCtx ctx, uint8 fill = 0xCD)
{
	ctx.decodedTexelCountX = d->getTexelCountX(&ctx);
	ctx.decodedTexelCountY = d->getTexelCountY(&ctx);
	std::vector<uint8> out(d->calculateImageSize(&ctx) + 64, fill);
	d->decode(&ctx, out.data());
	return out;
}

static std::vector<uint8> DecodePool(TextureDecoder* d, LatteTextureLoaderCtx ctx, ParallelForPool& pool, sint64 minTexels, bool* split)
{
	ctx.decodedTexelCountX = d->getTexelCountX(&ctx);
	ctx.decodedTexelCountY = d->getTexelCountY(&ctx);
	std::vector<uint8> out(d->calculateImageSize(&ctx) + 64, 0xCD);
	*split = LatteTextureLoader_DecodeSplit(d, &ctx, out.data(), pool, minTexels);
	if (!*split)
		d->decode(&ctx, out.data());
	return out;
}

int main(int argc, char** argv)
{
	const double benchSeconds = argc > 1 ? atof(argv[1]) : 0.3;
	const DecoderCase cases[] = {
		{"BC1->RGBA8", TextureDecoder_BC1_To_R8G8B8A8::getInstance(), true, 64},
		{"BC2->RGBA8", TextureDecoder_BC2_To_R8G8B8A8::getInstance(), true, 128},
		{"BC3->RGBA8", TextureDecoder_BC3_To_R8G8B8A8::getInstance(), true, 128},
		{"BC4->R8", TextureDecoder_BC4_To_R8<decodeBC4Block_UNORM>::getInstance(), true, 64},
		{"BC5->RG8", TextureDecoder_BC5_To_R8G8<decodeBC5Block_SNORM>::getInstance(), true, 128},
		{"BC1->RGBA32F", TextureDecoder_BC1_UNORM_uncompress::getInstance(), true, 64},
		{"BC1 copy", TextureDecoder_BC1::getInstance(), true, 64},
		{"BC3 copy", TextureDecoder_BC3::getInstance(), true, 128},
		{"RGBA8 copy", TextureDecoder_R8_G8_B8_A8::getInstance(), false, 32},
		{"RG16F copy", TextureDecoder_R16_G16_FLOAT::getInstance(), false, 32},
		{"RGBA16F copy", TextureDecoder_R16_G16_B16_A16_FLOAT::getInstance(), false, 64},
		{"R8 copy", TextureDecoder_R8::getInstance(), false, 8},
		{"null32", TextureDecoder_NullData32::getInstance(), false, 32},
	};
	const E_HWTILEMODE tileModes[] = {E_HWTILEMODE::TM_2D_TILED_THIN1, E_HWTILEMODE::TM_1D_TILED_THIN1, E_HWTILEMODE::TM_LINEAR_ALIGNED, E_HWTILEMODE::TM_2D_TILED_THIN2};
	const sint32 sizes[][2] = {{256, 256}, {1024, 512}, {1000, 600}, {1366, 766}, {96, 257}, {37, 300}, {64, 64}, {33, 31}, {8, 1024}, {1, 1}};

	// correctness: every case, forced split (minTexels 0)
	std::unique_ptr<ParallelForPool> pools[] = {std::make_unique<ParallelForPool>(1), std::make_unique<ParallelForPool>(2), std::make_unique<ParallelForPool>(3), std::make_unique<ParallelForPool>(7)};
	TestRng rng(1234);
	int compared = 0, splitCount = 0;
	for (const auto& dc : cases)
	{
		for (E_HWTILEMODE tm : tileModes)
		{
			for (const auto& sz : sizes)
			{
				for (uint32 slice : {0u, 1u})
				{
					Surface s;
					MakeSurface(s, sz[0], sz[1], dc.compressed, dc.bpp, tm, slice, (uint32)rng.below(8), rng);
					const std::vector<uint8> ref = DecodeSerial(dc.decoder, s.ctx);
					// the decoder writes every byte of calculateImageSize() (the upload buffer isn't cleared before
					// decoding) and nothing after it
					const std::vector<uint8> ref00 = DecodeSerial(dc.decoder, s.ctx, 0x00);
					const size_t imageBytes = ref.size() - 64;
					bool tailKept = true;
					for (size_t i = imageBytes; i < ref.size(); i++)
						tailKept = tailKept && ref[i] == 0xCD && ref00[i] == 0x00;
					TEST_CHECK(std::equal(ref.begin(), ref.begin() + imageBytes, ref00.begin()) && tailKept, "%s tm %d %dx%d slice %u: decoder leaves bytes unwritten or writes past its image", dc.name, (int)tm, sz[0], sz[1], slice);
					for (auto& pool : pools)
					{
						bool split = false;
						const std::vector<uint8> got = DecodePool(dc.decoder, s.ctx, *pool, 0, &split);
						compared++;
						splitCount += split ? 1 : 0;
						TEST_CHECK(got == ref, "%s tm %d %dx%d slice %u workers %d split %d: output differs", dc.name, (int)tm, sz[0], sz[1], slice, pool->GetWorkerCount(), split);
					}
				}
			}
		}
	}
	printf("compared %d decodes, %d of them split\n", compared, splitCount);
	TEST_CHECK(splitCount > compared / 2, "too few decodes were split (%d of %d)", splitCount, compared);

	// pool loops back to back with uneven work: all iterations run exactly once
	for (auto& pool : pools)
	{
		for (int round = 0; round < 2000; round++)
		{
			const int count = 1 + round % 23;
			std::vector<std::atomic<int>> hits(count);
			pool->Run(count, [&](int i) {
				if ((i + round) % 5 == 0)
					std::this_thread::yield();
				hits[i].fetch_add(1);
			});
			for (int i = 0; i < count; i++)
				TEST_CHECK(hits[i].load() == 1, "pool %d round %d iteration %d ran %d times", pool->GetWorkerCount(), round, i, hits[i].load());
		}
	}

	// cost of one loop on the pool: with nothing to do (the caller takes every iteration before a worker wakes up), and
	// with one 100 us iteration per thread, where the time over 100 us is what waking the workers and waiting costs
	for (auto& pool : pools)
	{
		const double empty = testBenchmark([&] { pool->Run(pool->GetWorkerCount() * 3 + 3, [](int) {}); }, benchSeconds);
		const double busy = testBenchmark([&] {
			pool->Run(pool->GetWorkerCount() + 1, [](int) {
				const double end = testNowSeconds() + 100e-6;
				while (testNowSeconds() < end)
					;
			});
		}, benchSeconds);
		printf("pool loop, %d workers: empty %.1f us, one 100 us iteration per thread %.1f us\n", pool->GetWorkerCount(), empty * 1e6, busy * 1e6);
	}

	// benchmark: whole mip chains, 2D tiled, threshold 256x256 texels as in the emulator
	const sint64 minTexels = 256 * 256;
	const DecoderCase benchCases[] = {cases[0], cases[2], cases[4], cases[6], cases[8]};
	for (sint32 baseSize : {1024, 2048})
	{
		for (const auto& dc : benchCases)
		{
			std::vector<Surface> mips;
			for (sint32 size = baseSize; size >= 1; size /= 2)
			{
				mips.emplace_back();
				MakeSurface(mips.back(), size, size, dc.compressed, dc.bpp, E_HWTILEMODE::TM_2D_TILED_THIN1, 0, 0, rng);
			}
			std::vector<uint8> out(DecodeSerial(dc.decoder, mips[0].ctx).size());
			auto run = [&](ParallelForPool* pool) {
				for (auto& m : mips)
				{
					LatteTextureLoaderCtx ctx = m.ctx;
					ctx.decodedTexelCountX = dc.decoder->getTexelCountX(&ctx);
					ctx.decodedTexelCountY = dc.decoder->getTexelCountY(&ctx);
					if (!pool || !LatteTextureLoader_DecodeSplit(dc.decoder, &ctx, out.data(), *pool, minTexels))
						dc.decoder->decode(&ctx, out.data());
				}
			};
			const double serial = testBenchmark([&] { run(nullptr); }, benchSeconds);
			printf("%4d mip chain %-12s serial %7.3f ms", baseSize, dc.name, serial * 1e3);
			for (auto& pool : pools)
			{
				const double t = testBenchmark([&] { run(pool.get()); }, benchSeconds);
				printf(" | %d+1 threads %7.3f ms (%.2fx)", pool->GetWorkerCount(), t * 1e3, serial / t);
			}
			printf("\n");
		}
	}
	return testReport("texdecode_split_test");
}
