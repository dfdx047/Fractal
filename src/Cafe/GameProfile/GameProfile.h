#pragma once
#include "config/CemuConfig.h"

struct gameProfileIntegerOption_t
{
	bool isPresent = false;
	sint32 value;
};

class GameProfile
{
	friend class GameProfileWindow;

public:
	static const uint32 kThreadQuantumDefault = 45000;

	bool Load(uint64_t title_id);
	void Save(uint64_t title_id);
	void ResetOptional();
	void Reset();

	[[nodiscard]] uint64 GetTitleId() const { return m_title_id; }
	[[nodiscard]] bool IsLoaded() const { return m_is_loaded; }
	[[nodiscard]] bool IsDefaultProfile() const { return m_is_default; }
	[[nodiscard]] const std::optional<std::string>& GetGameName() const { return m_gameName; }

	[[nodiscard]] const std::optional<bool>& ShouldLoadSharedLibraries() const { return m_loadSharedLibraries; }
	void SetShouldLoadSharedLibraries(bool shouldLoadSharedLibraries) { m_loadSharedLibraries = shouldLoadSharedLibraries; }
	[[nodiscard]] bool StartWithGamepadView() const { return m_startWithPadView; }

	[[nodiscard]] const std::optional<GraphicAPI>& GetGraphicsAPI() const { return m_graphics_api; }
	[[nodiscard]] const AccurateShaderMulOption& GetAccurateShaderMul() const { return m_accurateShaderMul; }
	void SetAccurateShaderMul(AccurateShaderMulOption accurateShaderMulOption) { m_accurateShaderMul = accurateShaderMulOption; }
	// GX2DrawDone waits for texture readbacks and occlusion queries to finish (unset: renderer and global setting decide)
	[[nodiscard]] const std::optional<bool>& GetFullSyncAtGX2DrawDone() const { return m_fullSyncAtGX2DrawDone; }
	void SetFullSyncAtGX2DrawDone(std::optional<bool> fullSync) { m_fullSyncAtGX2DrawDone = fullSync; }
	// Mesa Turnip only: render passes in system memory (TU_DEBUG=sysmem); unset means on. BotW hangs the GPU at boot with tiled (GMEM) rendering on Adreno 650
	[[nodiscard]] const std::optional<bool>& GetTurnipSysmem() const { return m_turnipSysmem; }
	void SetTurnipSysmem(std::optional<bool> v) { m_turnipSysmem = v; }
	[[nodiscard]] const std::optional<PrecompiledShaderOption>& GetPrecompiledShadersState() const { return m_precompiledShaders; }

	[[nodiscard]] uint32 GetThreadQuantum() const { return m_threadQuantum; }
	void SetThreadQuantum(uint32 threadQuantum){ m_threadQuantum = threadQuantum; }
	[[nodiscard]] const std::optional<CPUMode>& GetCPUMode() const { return m_cpuMode; }
	void SetCPUMode(CPUMode cpuMode) { m_cpuMode = cpuMode; }

	[[nodiscard]] bool IsAudioDisabled() const { return m_disableAudio; }

	[[nodiscard]] const std::array< std::optional<std::string>, 8>& GetControllerProfile() const { return m_controllerProfile; }
	void SetControllerProfile(size_t index, std::optional<std::string> profile) { m_controllerProfile.at(index) = std::move(profile); }

  public:
	struct DriverSetting
	{
		DriverSettingMode mode = DriverSettingMode::Global;
		std::optional<std::string> customPath;
	};

	[[nodiscard]] DriverSetting GetDriverSetting() const
	{
		return m_driverSetting;
	}

	void SetDriverSetting(DriverSetting driverSetting)
	{
		m_driverSetting = driverSetting;
	}

  private:
	DriverSetting m_driverSetting;


  private:
	uint64_t m_title_id = 0;
	bool m_is_loaded = false;
	bool m_is_default = true;

	std::optional<std::string> m_gameName{};

	// general settings
	std::optional<bool> m_loadSharedLibraries{}; // = true;
	bool m_startWithPadView = false;

	// graphic settings
	std::optional<GraphicAPI> m_graphics_api{};
	AccurateShaderMulOption m_accurateShaderMul = AccurateShaderMulOption::True;
	std::optional<bool> m_fullSyncAtGX2DrawDone{};
	std::optional<bool> m_turnipSysmem{};
	std::optional<PrecompiledShaderOption> m_precompiledShaders{};
	// cpu settings
	uint32 m_threadQuantum = kThreadQuantumDefault; // values: 20000 45000 60000 80000 100000
	std::optional<CPUMode> m_cpuMode{}; // = CPUModeOption::kSingleCoreRecompiler;
	// audio
	bool m_disableAudio = false;
	// controller settings
	std::array< std::optional<std::string>, 8> m_controllerProfile{};
};
extern std::unique_ptr<GameProfile> g_current_game_profile;

void gameProfile_load();
