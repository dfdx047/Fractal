#include "WindowSystem.h"
#include "util/crypto/aes128.h"
#include "Cafe/OS/RPL/rpl.h"
#include "Cafe/OS/libs/gx2/GX2.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "config/CemuConfig.h"
#include "config/NetworkSettings.h"
#include "config/LaunchSettings.h"
#include "input/InputManager.h"

#include "Cafe/CafeSystem.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cafe/TitleList/SaveList.h"

#include "Common/ExceptionHandler/ExceptionHandler.h"

#include "util/helpers/helpers.h"
#include "config/ActiveSettings.h"

#include "Cafe/IOSU/legacy/iosu_crypto.h"
#include "Cafe/OS/libs/vpad/vpad.h"

#include "audio/IAudioAPI.h"
#include "audio/IAudioInputAPI.h"

#ifdef HAS_SDL
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif

std::atomic_bool g_isGPUInitFinished = false;

void CemuCommonInit()
{
	// crypto init
	AES128_init();
	// init PPC timer
	// call this as early as possible because it measures frequency of RDTSC using an asynchronous thread over 3 seconds
	PPCTimer_init();

    ExceptionHandler_Init();
	// read config
	GetConfigHandle().Load();
	if (NetworkConfig::XMLExists())
		n_config.Load();
	// parallelize expensive init code
	std::future<int> futureInitAudioAPI = std::async(std::launch::async, []{ IAudioAPI::InitializeStatic(); IAudioInputAPI::InitializeStatic(); return 0; });
	std::future<int> futureInitGraphicPacks = std::async(std::launch::async, []{ GraphicPack2::LoadAll(); return 0; });
	InputManager::instance().load();
	futureInitAudioAPI.wait();
	futureInitGraphicPacks.wait();
	// init Cafe system
	CafeSystem::Initialize();
	// init title list
	CafeTitleList::Initialize(ActiveSettings::GetUserDataPath("title_list_cache.xml"));
	for (auto& it : GetConfig().game_paths)
		CafeTitleList::AddScanPath(_utf8ToPath(it));
	fs::path mlcPath = ActiveSettings::GetMlcPath();
	if (!mlcPath.empty())
		CafeTitleList::SetMLCPath(mlcPath);
	CafeTitleList::Refresh();
	// init save list
	CafeSaveList::Initialize();
	if (!mlcPath.empty())
	{
		CafeSaveList::SetMLCPath(mlcPath);
		CafeSaveList::Refresh();
	}
}

// LaunchSettings: the command line options that print to a console
void requireConsole()
{
}
