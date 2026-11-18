#pragma once
#include <cstdio>
#include <chrono>
#include <vector>

// deterministic pseudo random data (xorshift64*)
struct TestRng
{
	uint64 s;
	explicit TestRng(uint64 seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
	uint64 next()
	{
		s ^= s >> 12;
		s ^= s << 25;
		s ^= s >> 27;
		return s * 0x2545F4914F6CDD1Dull;
	}
	uint32 below(uint32 n) { return (uint32)(next() % n); }
	void fill(void* p, size_t n)
	{
		uint8* b = (uint8*)p;
		for (size_t i = 0; i < n; i++)
			b[i] = (uint8)(next() >> 56);
	}
};

static int g_testFailures = 0;

#define TEST_CHECK(cond, ...) do { if (!(cond)) { g_testFailures++; if (g_testFailures < 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

inline double testNowSeconds()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// runs fn repeatedly for roughly the given time and returns the average seconds per call
template<typename F>
double testBenchmark(F&& fn, double budgetSeconds = 0.25)
{
	fn(); // warm up
	double start = testNowSeconds();
	uint64 runs = 0;
	double elapsed;
	do
	{
		fn();
		runs++;
		elapsed = testNowSeconds() - start;
	} while (elapsed < budgetSeconds);
	return elapsed / (double)runs;
}

inline int testReport(const char* name)
{
	if (g_testFailures == 0)
		printf("%s: OK\n", name);
	else
		printf("%s: %d FAILURES\n", name, g_testFailures);
	return g_testFailures == 0 ? 0 : 1;
}
