// AES-128: ARMv8 Crypto Extension path vs the software implementation (src/util/crypto/aes128.cpp)
#include "util/crypto/aes128.cpp"
#include "test_common.h"

CPUFeaturesImpl::CPUFeaturesImpl() {}
CPUFeaturesImpl g_CPUFeatures;

#if !defined(__aarch64__)
#error this test is for aarch64
#endif

static void testKnownAnswer()
{
	// FIPS-197 appendix C.1
	uint8 key[16], plain[16];
	for (int i = 0; i < 16; i++)
	{
		key[i] = (uint8)i;
		plain[i] = (uint8)(i * 0x11);
	}
	const uint8 expected[16] = { 0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };
	uint8 outSoft[16], outArm[16];
	__soft__AES128_ECB_encrypt(plain, key, outSoft);
	__armv8__AES128_ECB_encrypt(plain, key, outArm);
	TEST_CHECK(memcmp(outSoft, expected, 16) == 0, "soft ECB known answer");
	TEST_CHECK(memcmp(outArm, expected, 16) == 0, "armv8 ECB known answer");
	uint8 decArm[16], decSoft[16];
	__armv8__AES128_CBC_decrypt(decArm, (uint8*)expected, 16, key, nullptr);
	__soft__AES128_CBC_decrypt(decSoft, (uint8*)expected, 16, key, nullptr);
	TEST_CHECK(memcmp(decArm, plain, 16) == 0, "armv8 CBC known answer");
	TEST_CHECK(memcmp(decSoft, plain, 16) == 0, "soft CBC known answer");
}

static void testRandom()
{
	TestRng rng(1);
	std::vector<uint8> src(0x10000 + 64), a(0x10000 + 64), b(0x10000 + 64);
	for (int iter = 0; iter < 3000; iter++)
	{
		uint32 length;
		if (iter < 300)
			length = (uint32)iter; // every small length, including partial trailing blocks
		else if (iter < 400)
			length = (uint32)(iter - 300) * 16;
		else
			length = rng.below(0x10000);
		uint8 key[16], iv[16];
		rng.fill(key, 16);
		rng.fill(iv, 16);
		bool useIv = rng.below(4) != 0;
		bool inPlace = rng.below(2) != 0;
		rng.fill(src.data(), src.size());
		// ECB encrypt (single block)
		{
			uint8 eSoft[16], eArm[16];
			__soft__AES128_ECB_encrypt(src.data(), key, eSoft);
			__armv8__AES128_ECB_encrypt(src.data(), key, eArm);
			TEST_CHECK(memcmp(eSoft, eArm, 16) == 0, "ECB encrypt mismatch iter %d", iter);
			// in place
			uint8 blk[16];
			memcpy(blk, src.data(), 16);
			__armv8__AES128_ECB_encrypt(blk, key, blk);
			TEST_CHECK(memcmp(eSoft, blk, 16) == 0, "ECB encrypt in-place mismatch iter %d", iter);
		}
		// CBC decrypt, the trailing partial block is processed as a whole block by both implementations
		uint32 touched = (length + 15) & ~15u;
		if (inPlace)
		{
			memcpy(a.data(), src.data(), src.size());
			memcpy(b.data(), src.data(), src.size());
			__soft__AES128_CBC_decrypt(a.data(), a.data(), length, key, useIv ? iv : nullptr);
			__armv8__AES128_CBC_decrypt(b.data(), b.data(), length, key, useIv ? iv : nullptr);
		}
		else
		{
			memset(a.data(), 0xCD, a.size());
			memset(b.data(), 0xCD, b.size());
			__soft__AES128_CBC_decrypt(a.data(), src.data(), length, key, useIv ? iv : nullptr);
			__armv8__AES128_CBC_decrypt(b.data(), src.data(), length, key, useIv ? iv : nullptr);
		}
		TEST_CHECK(memcmp(a.data(), b.data(), a.size()) == 0, "CBC decrypt mismatch iter %d length %u inPlace %d iv %d", iter, length, inPlace, useIv);
		(void)touched;
	}
	// overlapping buffers shifted by one block go through the single block loop
	{
		uint8 key[16], iv[16];
		rng.fill(key, 16);
		rng.fill(iv, 16);
		rng.fill(src.data(), src.size());
		memcpy(a.data(), src.data(), src.size());
		memcpy(b.data(), src.data(), src.size());
		__soft__AES128_CBC_decrypt(a.data(), a.data() + 16, 4096, key, iv);
		__armv8__AES128_CBC_decrypt(b.data(), b.data() + 16, 4096, key, iv);
		TEST_CHECK(memcmp(a.data(), b.data(), a.size()) == 0, "CBC decrypt overlapping mismatch");
	}
}

static void benchmark()
{
	std::vector<uint8> buf(0x8000); // one disc sector
	TestRng rng(7);
	rng.fill(buf.data(), buf.size());
	uint8 key[16], iv[16];
	rng.fill(key, 16);
	rng.fill(iv, 16);
	double tSoft = testBenchmark([&] { __soft__AES128_CBC_decrypt(buf.data(), buf.data(), (uint32)buf.size(), key, iv); });
	double tArm = testBenchmark([&] { __armv8__AES128_CBC_decrypt(buf.data(), buf.data(), (uint32)buf.size(), key, iv); });
	printf("CBC decrypt 32 KiB: soft %.1f MB/s, armv8 %.1f MB/s (x%.1f)\n", buf.size() / tSoft / 1e6, buf.size() / tArm / 1e6, tSoft / tArm);
	uint8 blk[16] = {};
	double eSoft = testBenchmark([&] { __soft__AES128_ECB_encrypt(blk, key, blk); });
	double eArm = testBenchmark([&] { __armv8__AES128_ECB_encrypt(blk, key, blk); });
	printf("ECB encrypt 1 block incl. key expansion: soft %.0f ns, armv8 %.0f ns\n", eSoft * 1e9, eArm * 1e9);
}

int main(int argc, char** argv)
{
	AES128_init(); // builds the InvMixColumns table of the software implementation
	testKnownAnswer();
	testRandom();
	int r = testReport("aes128");
	if (argc < 2 || strcmp(argv[1], "--no-bench") != 0)
		benchmark();
	return r;
}
