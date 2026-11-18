#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#define VKFUNC_DEFINE
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include <numeric> // for std::iota

#include <dlfcn.h>

#define VULKAN_API_CPU_BENCHMARK 0	// if 1, Cemu will log the CPU time spent per Vulkan API function

bool g_vulkan_available = false;

#if VULKAN_API_CPU_BENCHMARK != 0
uint64 s_vulkanBenchmarkLastResultsTime = 0;

struct VulkanBenchmarkFuncInfo
{
	std::string funcName;
	uint64 cycles;
	uint32 numCalls;
};

std::vector<VulkanBenchmarkFuncInfo*> s_vulkanBenchmarkFuncs;

template<typename TRet, typename... Args>
auto VkWrapperFuncGenTest(TRet (*func)(Args...), const char* name)
{
	static VulkanBenchmarkFuncInfo _FuncInfo;
	static auto _FuncPtrCopy = func;
	TRet (*newFunc)(Args...);
	if constexpr(std::is_void_v<TRet>)
	{
		newFunc = +[](Args... args) { uint64 t = __rdtsc(); _mm_mfence(); _FuncPtrCopy(args...); _mm_mfence(); _FuncInfo.cycles += (__rdtsc() - t); _FuncInfo.numCalls++; };
	}
	else
		newFunc = +[](Args... args) -> TRet { uint64 t = __rdtsc(); _mm_mfence(); TRet r = _FuncPtrCopy(args...); _mm_mfence(); _FuncInfo.cycles += (__rdtsc() - t); _FuncInfo.numCalls++; return r; };
	if(func && func != newFunc)
		_FuncPtrCopy = func;
	if(_FuncInfo.funcName.empty())
	{
		_FuncInfo = {.funcName = name, .cycles = 0, .numCalls = 0};
		s_vulkanBenchmarkFuncs.emplace_back(&_FuncInfo);
	}
	return newFunc;
};
#endif

// called when a TV SwapBuffers is called
void VulkanBenchmarkPrintResults()
{
#if VULKAN_API_CPU_BENCHMARK != 0
	// note: This could be done by hooking vk present functions
	uint64 currentCycle = __rdtsc();
	uint64 elapsedCycles = currentCycle - s_vulkanBenchmarkLastResultsTime;
	s_vulkanBenchmarkLastResultsTime = currentCycle;
	double elapsedCyclesDbl = (double)elapsedCycles;
	cemuLog_log(LogType::Force, "--- Vulkan API CPU benchmark ---");
	cemuLog_log(LogType::Force, "Elapsed cycles this frame: {:} | Current cycle {:} | NumFunc {:}", elapsedCycles, currentCycle, s_vulkanBenchmarkFuncs.size());

	// sum up total time of Vulkan calls
	uint64 totalVkCycles = 0;
	for (auto& it : s_vulkanBenchmarkFuncs)
	{
		totalVkCycles += it->cycles;
	}
	cemuLog_log(LogType::Force, "Total Vulkan time: {:.4}%", ((double)totalVkCycles / elapsedCyclesDbl) * 100.0);

	std::vector<sint32> sortedIndices(s_vulkanBenchmarkFuncs.size());
	std::iota(sortedIndices.begin(), sortedIndices.end(), 0);
	std::sort(sortedIndices.begin(), sortedIndices.end(),
			  [](int32_t a, int32_t b) {
				  return s_vulkanBenchmarkFuncs[a]->cycles > s_vulkanBenchmarkFuncs[b]->cycles;
			  });
	for (sint32 idx : sortedIndices)
	{
		auto& func = s_vulkanBenchmarkFuncs[idx];
		if(func->cycles == 0)
			return;
		cemuLog_log(LogType::Force, "{}: {} cycles ({:.4}%) {} calls", func->funcName.c_str(), func->cycles, ((double)func->cycles / elapsedCyclesDbl) * 100.0, func->numCalls);
		func->cycles = 0;
		func->numCalls = 0;
	}
#endif
}


void* g_vulkan_so = nullptr;

bool SupportsLoadingCustomDriver()
{
	// Adreno (kgsl), Mali (kbase) or Samsung Xclipse (sgpu, a DRM render node). libadrenotools' custom driver path only
	// swaps the Vulkan HAL the system loader opens, so it also works for Mali drivers that are HAL modules (PanVK,
	// malisx2) and for the RADV ports for Xclipse.
	std::error_code ec;
	return fs::exists("/dev/kgsl-3d0", ec) || fs::exists("/dev/mali0", ec) || fs::exists("/dev/dri/renderD128", ec);
}


constexpr auto CUSTOM_DRIVER_LIB_NAME = "custom_vulkan.so";

#include <adrenotools/driver.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include "config/ActiveSettings.h"
#include "Cafe/GameProfile/GameProfile.h"

// Fractal: custom driver guard. "custom_driver_pending" (the driver dir) is written before the driver is
// loaded and removed after the first presented frame; if it is still there later, starting with that
// driver crashed or failed. "custom_driver_failed" means it could not be loaded and the system driver
// was used instead. The app reads both, switches back to the system driver and tells the user.
constexpr auto CUSTOM_DRIVER_PENDING_MARKER = "custom_driver_pending";
constexpr auto CUSTOM_DRIVER_FAILED_MARKER = "custom_driver_failed";

static bool s_skip_custom_driver = false;
static bool s_custom_driver_in_use = false;

// marker contents: driver dir, then "game" or "global" (where the driver was chosen), then the title id
static void write_custom_driver_marker(const char* name, const std::string& driver_path)
{
	const auto& driverSetting = g_current_game_profile->GetDriverSetting();
	bool fromGame = driverSetting.mode == DriverSettingMode::Custom && driverSetting.customPath == driver_path;
	std::ofstream out(ActiveSettings::GetInternalPath(name), std::ios::trunc);
	out << driver_path << '\n' << (fromGame ? "game" : "global") << '\n' << fmt::format("{:016x}", g_current_game_profile->GetTitleId()) << '\n';
}

static void remove_custom_driver_marker(const char* name)
{
	std::error_code ec;
	fs::remove(ActiveSettings::GetInternalPath(name), ec);
}

static void custom_driver_failed(const std::string& driver_path)
{
	remove_custom_driver_marker(CUSTOM_DRIVER_PENDING_MARKER);
	write_custom_driver_marker(CUSTOM_DRIVER_FAILED_MARKER, driver_path);
	cemuLog_log(LogType::Force, "Custom driver failed, using the system driver");
}

std::string get_custom_driver_lib_name(const fs::path& driver_path)
{
	static constexpr auto LIB_NAME_MEMBER = "libraryName";
	std::ifstream in(driver_path / "meta.json");
	if (!in.is_open())
		return {};
	rapidjson::IStreamWrapper str(in);
	rapidjson::Document doc;
	doc.ParseStream(str);

	if (!doc.HasMember(LIB_NAME_MEMBER) || !doc[LIB_NAME_MEMBER].IsString())
		return {};

	std::string lib_name = doc[LIB_NAME_MEMBER].GetString();

	std::error_code ec;
	if (!fs::exists(driver_path / lib_name, ec))
		return {};

	return lib_name;
}

std::optional<std::string> get_custom_driver_path()
{
	const auto& driverSetting = g_current_game_profile->GetDriverSetting();
	if (driverSetting.mode == DriverSettingMode::System)
	{
		return {};
	}

	std::error_code ec;
	if (driverSetting.mode == DriverSettingMode::Custom &&
		driverSetting.customPath.has_value() &&
		fs::exists(driverSetting.customPath.value(), ec))
	{
		return driverSetting.customPath;
	}

	return GetConfig().custom_driver_path;
}

void* load_custom_driver()
{
	std::optional<std::string> driver_path = get_custom_driver_path();

	if (!driver_path.has_value() || driver_path->empty())
		return nullptr;
	std::string driver_name = get_custom_driver_lib_name(driver_path.value());
	if (driver_name.empty())
	{
		custom_driver_failed(driver_path.value());
		return nullptr;
	}
	write_custom_driver_marker(CUSTOM_DRIVER_PENDING_MARKER, driver_path.value());

	std::error_code ec;
	fs::copy(fs::path(driver_path.value()) / driver_name, ActiveSettings::GetInternalPath(CUSTOM_DRIVER_LIB_NAME), fs::copy_options::overwrite_existing, ec);
	// Qualcomm proprietary drivers ship the libraries the Vulkan library links against (notgsl.so, notllvm-qgl.so, ...)
	// next to it: they must sit in the same dir as the copied library, which is where adrenotools looks for them
	for (const auto& entry : fs::directory_iterator(fs::path(driver_path.value()), ec))
	{
		const auto& file = entry.path();
		if (entry.is_regular_file(ec) && file.extension() == ".so" && file.filename() != driver_name)
			fs::copy(file, ActiveSettings::GetInternalPath(file.filename().string()), fs::copy_options::overwrite_existing, ec);
	}

	// test hook: perf/driver_env.txt holds KEY=VALUE lines (e.g. TU_DEBUG=sysmem) exported before the driver reads its environment
	if (std::ifstream envFile(ActiveSettings::GetUserDataPath("perf/driver_env.txt")); envFile)
	{
		std::string line;
		while (std::getline(envFile, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
				line.pop_back();
			const size_t eq = line.find('=');
			if (line.empty() || line[0] == '#' || eq == std::string::npos || eq == 0)
				continue;
			setenv(line.substr(0, eq).c_str(), line.substr(eq + 1).c_str(), 1);
			cemuLog_log(LogType::Force, "Custom driver env: {}", line);
		}
	}
	// Turnip renders in system memory instead of tiles unless the game profile says otherwise (TU_DEBUG only affects Mesa
	// Turnip, read at instance creation). Adreno 650, Turnip 26.0 R8: MK8 +6 %, SM3DW +8 %, Bayonetta 2 tie, and BotW
	// stops hanging the GPU at boot
	if (g_current_game_profile->GetTurnipSysmem().value_or(true))
	{
		const char* prev = getenv("TU_DEBUG");
		std::string flags = (prev && *prev) ? std::string(prev) + ",sysmem" : std::string("sysmem");
		setenv("TU_DEBUG", flags.c_str(), 1);
		cemuLog_log(LogType::Force, "Custom driver env: TU_DEBUG={} (game profile turnipSysmem)", flags);
	}

	void* vulkan_so = adrenotools_open_libvulkan(
		RTLD_NOW | RTLD_LOCAL,
		ADRENOTOOLS_DRIVER_CUSTOM,
		nullptr,
		(ActiveSettings::GetNativeLibPath().string() + "/").c_str(),
		(ActiveSettings::GetInternalPath().string() + "/").c_str(),
		CUSTOM_DRIVER_LIB_NAME,
		nullptr,
		nullptr);
	if (!vulkan_so)
	{
		cemuLog_log(LogType::Force, "Failed to load custom driver");
		custom_driver_failed(driver_path.value());
		return nullptr;
	}
	cemuLog_log(LogType::Force, "Loaded custom driver");
	return vulkan_so;
}


void* dlopen_vulkan_loader()
{
	static void* vulkan_so = nullptr;
	s_custom_driver_in_use = false;
	if (!s_skip_custom_driver)
	{
		vulkan_so = load_custom_driver();
		if (vulkan_so)
		{
			s_custom_driver_in_use = true;
			return vulkan_so;
		}
	}
	vulkan_so = dlopen("libvulkan.so", RTLD_NOW);
	if(!vulkan_so)
		vulkan_so = dlopen("libvulkan.so.1", RTLD_NOW);
	return vulkan_so;
}

bool InitializeGlobalVulkan()
{
	g_vulkan_so = dlopen_vulkan_loader();

	if (g_vulkan_available)
		return true;

	if (!g_vulkan_so)
	{
		cemuLog_log(LogType::Force, "Vulkan loader not available.");
		return false;
	}

	void* vulkan_so = g_vulkan_so;

	#define VKFUNC_INIT
	#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

	if(!vkEnumerateInstanceVersion)
	{
		cemuLog_log(LogType::Force, "vkEnumerateInstanceVersion not available. Outdated graphics driver or Vulkan runtime?");
		if (s_custom_driver_in_use)
		{
			// the custom driver is not usable: retry with the system driver
			custom_driver_failed(get_custom_driver_path().value_or(""));
			dlclose(g_vulkan_so);
			g_vulkan_so = nullptr;
			s_skip_custom_driver = true;
			return InitializeGlobalVulkan();
		}
		return false;
	}

	g_vulkan_available = true;
	return true;
}

void CustomDriverFramePresented()
{
	static bool s_done = false;
	if (s_done)
		return;
	s_done = true;
	if (s_custom_driver_in_use)
		remove_custom_driver_marker(CUSTOM_DRIVER_PENDING_MARKER);
}

bool InitializeInstanceVulkan(VkInstance instance)
{
	void* vulkan_so = g_vulkan_so;
	if (!vulkan_so)
		return false;

	#define VKFUNC_INSTANCE_INIT
	#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

	return true;
}

bool InitializeDeviceVulkan(VkDevice device)
{
	void* vulkan_so = g_vulkan_so;
	if (!vulkan_so)
		return false;

	#define VKFUNC_DEVICE_INIT
	#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

#if VULKAN_API_CPU_BENCHMARK != 0
	#define VKFUNC_DEFINE_CUSTOM(__func) __func = VkWrapperFuncGenTest(__func, #__func)
	#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#endif

	return true;
}

