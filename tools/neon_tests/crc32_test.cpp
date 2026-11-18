// CRC-32: ARMv8 CRC32 instructions vs the table driven implementation (src/util/crypto/crc32.cpp)
#include "util/crypto/crc32.cpp"
CPUFeaturesImpl::CPUFeaturesImpl() {}
CPUFeaturesImpl g_CPUFeatures;
#include "test_common.h"

#if !defined(__aarch64__)
#error this test is for aarch64
#endif

static uint32 crcSoft(uint32 c, const void* data, size_t length)
{
	g_CPUFeatures.arm64.crc32 = false;
	return crc32_calc(c, data, length);
}

static uint32 crcHw(uint32 c, const void* data, size_t length)
{
	g_CPUFeatures.arm64.crc32 = true;
	return crc32_calc(c, data, length);
}

int main(int argc, char** argv)
{
	std::vector<uint8> buf(1 << 20);
	TestRng rng(3);
	rng.fill(buf.data(), buf.size());
	// known value (CRCTest in crc32.cpp)
	{
		uint8 seq[89];
		for (int i = 0; i < 89; i++)
			seq[i] = (uint8)i;
		TEST_CHECK(crcSoft(0, seq, 89) == 0x3fc61683, "soft known value");
		TEST_CHECK(crcHw(0, seq, 89) == 0x3fc61683, "hw known value");
	}
	// every length up to 300 at every alignment, with chained start values
	for (size_t len = 0; len <= 300; len++)
	{
		for (size_t off = 0; off < 8; off++)
		{
			uint32 seed = (uint32)rng.next();
			TEST_CHECK(crcSoft(seed, buf.data() + off, len) == crcHw(seed, buf.data() + off, len), "len %zu off %zu", len, off);
		}
	}
	for (int i = 0; i < 2000; i++)
	{
		size_t off = rng.below(4096);
		size_t len = rng.below((uint32)(buf.size() - off));
		uint32 seed = (uint32)rng.next();
		TEST_CHECK(crcSoft(seed, buf.data() + off, len) == crcHw(seed, buf.data() + off, len), "random len %zu off %zu", len, off);
	}
	int r = testReport("crc32");
	if (argc < 2 || strcmp(argv[1], "--no-bench") != 0)
	{
		volatile uint32 sink;
		double tSoft = testBenchmark([&] { sink = crcSoft(0, buf.data(), buf.size()); });
		double tHw = testBenchmark([&] { sink = crcHw(0, buf.data(), buf.size()); });
		printf("CRC32 1 MiB: tables %.0f MB/s, crc32x %.0f MB/s (x%.1f)\n", buf.size() / tSoft / 1e6, buf.size() / tHw / 1e6, tSoft / tHw);
	}
	return r;
}
