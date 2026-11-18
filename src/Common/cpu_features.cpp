#include "cpu_features.h"

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

CPUFeaturesImpl::CPUFeaturesImpl()
{
	m_cpuBrandName = getCpuBrandNameAndroid();
	// optional ARMv8 extensions as reported by the kernel. Code using them is selected at runtime so that the baseline
	// stays ARMv8.0-A and every arm64-v8a phone keeps working
	const unsigned long hwcap = getauxval(AT_HWCAP);
	arm64.aes = (hwcap & HWCAP_AES) != 0;
	arm64.pmull = (hwcap & HWCAP_PMULL) != 0;
	arm64.crc32 = (hwcap & HWCAP_CRC32) != 0;
	arm64.atomics = (hwcap & HWCAP_ATOMICS) != 0;
	arm64.fp16 = (hwcap & HWCAP_ASIMDHP) != 0;
	arm64.dotprod = (hwcap & HWCAP_ASIMDDP) != 0;
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
	return tmp;
}

CPUFeaturesImpl g_CPUFeatures;