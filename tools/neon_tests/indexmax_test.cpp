// Index buffer maximum with primitive restart: NEON vs scalar (src/Cafe/HW/Latte/Core/LatteIndicesMax.h)
#include <climits>
#include <limits>
#include "Common/betype.h"
#include "Cafe/HW/Latte/Core/LatteIndicesMax.h"
#include "test_common.h"

#if !defined(__aarch64__)
#error this test is for aarch64
#endif

template<typename T, typename TInput, bool isBigEndian>
static void testType(const char* name, TestRng& rng)
{
	std::vector<T> buf(70000 + 64);
	for (int iter = 0; iter < 4000; iter++)
	{
		uint32 count = iter < 200 ? (uint32)iter + 1 : rng.below(70000) + 1;
		uint32 offset = rng.below(8); // unaligned starts
		// value distribution: small, full range, many restarts, only restarts
		uint32 mode = rng.below(4);
		uint32 restart = (rng.below(3) == 0) ? (uint32)rng.next() & (uint32)std::numeric_limits<T>::max() : (uint32)std::numeric_limits<T>::max();
		for (uint32 i = 0; i < count + 8; i++)
		{
			T v;
			if (mode == 0)
				v = (T)rng.below(1000);
			else if (mode == 1)
				v = (T)rng.next();
			else if (mode == 2)
				v = rng.below(3) == 0 ? (T)restart : (T)rng.next();
			else
				v = (T)restart;
			if (rng.below(64) == 0)
				v = (T)restart;
			buf[offset + i] = isBigEndian ? (T)SwapEndian(v) : v;
		}
		uint32 rScalar = 0xDEADBEEF, rNeon = 0xCAFEBABE;
		_LatteIndices_alternativeCalculateIndexMax<T, TInput>(buf.data() + offset, count, restart, rScalar);
		LatteIndices_alternativeCalculateIndexMax_NEON<T, isBigEndian>(buf.data() + offset, count, restart, rNeon);
		TEST_CHECK(rScalar == rNeon, "%s count %u mode %u restart %08x: scalar %08x neon %08x", name, count, mode, restart, rScalar, rNeon);
	}
}

template<typename T, typename TInput, bool isBigEndian>
static void bench(const char* name)
{
	std::vector<T> buf(30000); // a large draw
	TestRng rng(9);
	for (auto& v : buf)
		v = rng.below(8) == 0 ? (T)std::numeric_limits<T>::max() : (T)rng.below(60000);
	uint32 restart = (uint32)std::numeric_limits<T>::max();
	volatile uint32 sink;
	double tS = testBenchmark([&] { uint32 r; _LatteIndices_alternativeCalculateIndexMax<T, TInput>(buf.data(), (uint32)buf.size(), restart, r); sink = r; }, 0.15);
	double tN = testBenchmark([&] { uint32 r; LatteIndices_alternativeCalculateIndexMax_NEON<T, isBigEndian>(buf.data(), (uint32)buf.size(), restart, r); sink = r; }, 0.15);
	printf("%s 30000 indices: scalar %.1f us, neon %.1f us (x%.1f)\n", name, tS * 1e6, tN * 1e6, tS / tN);
}

int main(int argc, char** argv)
{
	TestRng rng(5);
	testType<uint16, uint16be, true>("u16be", rng);
	testType<uint32, uint32be, true>("u32be", rng);
	testType<uint16, uint16, false>("u16le", rng);
	testType<uint32, uint32, false>("u32le", rng);
	int r = testReport("indexmax");
	if (argc < 2 || strcmp(argv[1], "--no-bench") != 0)
	{
		bench<uint16, uint16be, true>("u16be");
		bench<uint32, uint32be, true>("u32be");
	}
	return r;
}
