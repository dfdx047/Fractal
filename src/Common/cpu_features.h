#if defined(__GNUC__) && defined(__aarch64__)
// ARMv8.0-A only guarantees FP and ASIMD (NEON). Everything else is optional and is selected at runtime
#define ATTRIBUTE_ARM_AES __attribute__((target("aes")))
#define ATTRIBUTE_ARM_CRC __attribute__((target("crc")))
#define ATTRIBUTE_AVX2
#define ATTRIBUTE_SSE41
#define ATTRIBUTE_AESNI
#elif defined(__GNUC__)
#define ATTRIBUTE_AVX2 __attribute__((target("avx2")))
#define ATTRIBUTE_SSE41 __attribute__((target("sse4.1")))
#define ATTRIBUTE_AESNI __attribute__((target("aes")))
#else
#define ATTRIBUTE_AVX2
#define ATTRIBUTE_SSE41
#define ATTRIBUTE_AESNI
#endif

#include <string>

class CPUFeaturesImpl
{
public:
	CPUFeaturesImpl();

	std::string GetCPUName(); // empty if not available
	std::string GetCommaSeparatedExtensionList();

	struct
	{
		bool ssse3{ false };
		bool sse4_1{ false };
		bool avx{ false };
		bool avx2{ false };
		bool lzcnt{ false };
		bool movbe{ false };
		bool bmi2{ false };
		bool fastPext{ false }; // use PEXT instead of shifts and masks
		bool aesni{ false };
		bool invariant_tsc{ false };
	}x86;
	struct
	{
		bool aes{ false }; // AESE/AESD/AESMC/AESIMC (Crypto Extension)
		bool pmull{ false };
		bool crc32{ false }; // CRC32B/H/W/X (optional in ARMv8.0, mandatory from ARMv8.1)
		bool atomics{ false }; // LSE (ARMv8.1), used through the compiler's outline atomics
		bool fp16{ false };
		bool dotprod{ false };
	}arm64;
private:
	std::string m_cpuBrandName;
};

extern CPUFeaturesImpl g_CPUFeatures;
