#include "cpu_features.h"

#if BOOST_OS_MACOS
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

// wrappers with uniform prototype for implementation-specific x86 CPU id
#if defined(ARCH_X86_64)
#ifdef __GNUC__
#include <cpuid.h>
#endif

inline void cpuid(int cpuInfo[4], int functionId) {
#if defined(_MSC_VER)
	__cpuid(cpuInfo, functionId);
#elif defined(__GNUC__)
	__cpuid(functionId, cpuInfo[0], cpuInfo[1], cpuInfo[2], cpuInfo[3]);
#else
#error No definition for cpuid
#endif
}

inline void cpuidex(int cpuInfo[4], int functionId, int subFunctionId) {
#if defined(_MSC_VER)
	__cpuidex(cpuInfo, functionId, subFunctionId);
#elif defined(__GNUC__)
	__cpuid_count(functionId, subFunctionId, cpuInfo[0], cpuInfo[1], cpuInfo[2], cpuInfo[3]);
#else
#error No definition for cpuidex
#endif
}
#endif

#if defined(__aarch64__)
#if BOOST_OS_LINUX
#include <sys/auxv.h>
#if __has_include(<asm/hwcap.h>)
#include <asm/hwcap.h>
#endif
// AT_HWCAP bits of arm64 Linux (stable kernel ABI)
#ifndef HWCAP_AES
#define HWCAP_AES (1 << 3)
#endif
#ifndef HWCAP_PMULL
#define HWCAP_PMULL (1 << 4)
#endif
#ifndef HWCAP_CRC32
#define HWCAP_CRC32 (1 << 7)
#endif
#ifndef HWCAP_ATOMICS
#define HWCAP_ATOMICS (1 << 8)
#endif
#ifndef HWCAP_ASIMDHP
#define HWCAP_ASIMDHP (1 << 10)
#endif
#ifndef HWCAP_ASIMDDP
#define HWCAP_ASIMDDP (1 << 20)
#endif

std::string getCpuBrandNameLinux()
{
	static auto default_name = "unknown";
	std::ifstream ifstream("/proc/device-tree/model");
	if (!ifstream.is_open())
		return default_name;
	std::stringstream stringstream;
	stringstream << ifstream.rdbuf();
	std::string model = stringstream.str();
	if (model.empty())
		return default_name;
	return model;
}
#if BOOST_PLAT_ANDROID

#include <sys/system_properties.h>

std::string getProperty(const std::string& name)
{
	const prop_info* pi = __system_property_find(name.c_str());
	std::string propValue;
	if (pi == nullptr)
		return {};
	__system_property_read_callback(
		pi,
		[](void* cookie, const char* name, const char* value, uint32_t serial) {
			if (cookie == nullptr)
				return;
			*reinterpret_cast<std::string*>(cookie) = value;
		},
		&propValue);
	return propValue;
}

std::string getCpuBrandNameAndroid()
{
	static auto propertiesNames = {
		"ro.soc.manufacturer",
		"ro.soc.model",
		"ro.boot.hardware.revision",
	};
    std::string tmp;
	for (auto&& propertyName : propertiesNames)
	{
		auto propertyValue = getProperty(propertyName);
		if (!propertyValue.empty())
        {
            if (!tmp.empty())
                tmp.append(", ");
            tmp.append(propertyValue);
        }
	}
	if (tmp.empty())
		return getCpuBrandNameLinux();
    return tmp;
}
#endif // BOOST_PLAT_ANDROID
#endif // BOOST_OS_LINUX
#endif // defined(__aarch64__)

CPUFeaturesImpl::CPUFeaturesImpl()
{
#if defined(__aarch64__)
#if BOOST_PLAT_ANDROID
	m_cpuBrandName = getCpuBrandNameAndroid();
#elif BOOST_OS_LINUX
	m_cpuBrandName = getCpuBrandNameLinux();
#endif
#if BOOST_OS_LINUX
	// optional ARMv8 extensions as reported by the kernel. Code using them is selected at runtime so that the baseline
	// stays ARMv8.0-A and every arm64-v8a phone keeps working
	const unsigned long hwcap = getauxval(AT_HWCAP);
	arm64.aes = (hwcap & HWCAP_AES) != 0;
	arm64.pmull = (hwcap & HWCAP_PMULL) != 0;
	arm64.crc32 = (hwcap & HWCAP_CRC32) != 0;
	arm64.atomics = (hwcap & HWCAP_ATOMICS) != 0;
	arm64.fp16 = (hwcap & HWCAP_ASIMDHP) != 0;
	arm64.dotprod = (hwcap & HWCAP_ASIMDDP) != 0;
#elif BOOST_OS_MACOS
	// every Apple silicon CPU is ARMv8.4-A or newer with the crypto extension
	arm64.aes = arm64.pmull = arm64.crc32 = arm64.atomics = arm64.fp16 = arm64.dotprod = true;
#endif
#endif

#if BOOST_OS_MACOS
	std::string cpuName;
	size_t size = 0;

	if (sysctlbyname("machdep.cpu.brand_string", nullptr, &size, nullptr, 0) == 0 && size > 0)
	{
		std::vector<char> buffer(size);

		if (sysctlbyname("machdep.cpu.brand_string", buffer.data(), &size, nullptr, 0) == 0 && size > 0)
		{
			cpuName.assign(buffer.data());
		}
	}

	strncpy(m_cpuBrandName, cpuName.c_str(), sizeof(m_cpuBrandName) - 1);
	m_cpuBrandName[sizeof(m_cpuBrandName) - 1] = '\0';
#elif defined(ARCH_X86_64)
	int cpuInfo[4];
	cpuid(cpuInfo, 0x80000001);
	x86.lzcnt = ((cpuInfo[2] >> 5) & 1) != 0;
	cpuid(cpuInfo, 0x1);
	x86.movbe = ((cpuInfo[2] >> 22) & 1) != 0;
	x86.avx = ((cpuInfo[2] >> 28) & 1) != 0;
	x86.aesni = ((cpuInfo[2] >> 25) & 1) != 0;
	x86.ssse3 = ((cpuInfo[2] >> 9) & 1) != 0;
	x86.sse4_1 = ((cpuInfo[2] >> 19) & 1) != 0;
	uint32 family = ((cpuInfo[0] >> 8) & 0xF);
	if (family == 0xF)
		family += ((cpuInfo[0] >> 20) & 0xFF);
	cpuidex(cpuInfo, 0x7, 0);
	x86.avx2 = ((cpuInfo[1] >> 5) & 1) != 0;
	x86.bmi2 = ((cpuInfo[1] >> 8) & 1) != 0;
	// PEXT is slow on older AMD CPUs
	cpuid(cpuInfo, 0);
	const bool isIntel = cpuInfo[1] == 0x756E6547 && cpuInfo[3] == 0x49656E69 && cpuInfo[2] == 0x6C65746E; // "GenuineIntel"
	const bool isAMD = cpuInfo[1] == 0x68747541 && cpuInfo[3] == 0x69746E65 && cpuInfo[2] == 0x444D4163; // "AuthenticAMD"
	x86.fastPext = x86.bmi2 && (isIntel || (isAMD && family >= 0x19));
	cpuid(cpuInfo, 0x80000007);
	x86.invariant_tsc = ((cpuInfo[3] >> 8) & 1);
	// get CPU brand name
	uint32_t nExIds, i = 0;
	char cpuBrandName[0x40]{ 0 };
	memset(cpuBrandName, 0, sizeof(cpuBrandName));
	cpuid(cpuInfo, 0x80000000);
	nExIds = (uint32_t)cpuInfo[0];
	for (uint32_t i = 0x80000000; i <= nExIds; ++i)
	{
		cpuid(cpuInfo, i);
		if (i == 0x80000002)
			memcpy(cpuBrandName, cpuInfo, sizeof(cpuInfo));
		else if (i == 0x80000003)
			memcpy(cpuBrandName + 16, cpuInfo, sizeof(cpuInfo));
		else if (i == 0x80000004)
			memcpy(cpuBrandName + 32, cpuInfo, sizeof(cpuInfo));
	}
	m_cpuBrandName = cpuBrandName;
#endif
}

std::string CPUFeaturesImpl::GetCPUName()
{
	return { m_cpuBrandName };
}

std::string CPUFeaturesImpl::GetCommaSeparatedExtensionList()
{
	std::string tmp;
	auto appendExt = [&tmp](const char* str)
	{
		if (!tmp.empty())
			tmp.append(", ");
		tmp.append(str);
	};
	if (x86.ssse3)
		appendExt("SSSE3");
	if (x86.sse4_1)
		appendExt("SSE4.1");
	if (x86.avx)
		appendExt("AVX");
	if (x86.avx2)
		appendExt("AVX2");
	if (x86.lzcnt)
		appendExt("LZCNT");
	if (x86.movbe)
		appendExt("MOVBE");
	if (x86.bmi2)
		appendExt("BMI2");
	if (x86.aesni)
		appendExt("AES-NI");
	if(x86.invariant_tsc)
		appendExt("INVARIANT-TSC");
#if defined(__aarch64__)
	appendExt("NEON");
	if (arm64.aes)
		appendExt("AES");
	if (arm64.pmull)
		appendExt("PMULL");
	if (arm64.crc32)
		appendExt("CRC32");
	if (arm64.atomics)
		appendExt("LSE");
	if (arm64.fp16)
		appendExt("FP16");
	if (arm64.dotprod)
		appendExt("DOTPROD");
#endif
	return tmp;
}

CPUFeaturesImpl g_CPUFeatures;