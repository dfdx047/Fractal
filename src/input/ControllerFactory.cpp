#include "input/ControllerFactory.h"

#include "input/emulated/VPADController.h"
#include "input/emulated/ProController.h"
#include "input/emulated/ClassicController.h"
#include "input/emulated/WiimoteController.h"

#include "input/api/Keyboard/KeyboardController.h"
#include "input/api/DSU/DSUController.h"



#include "input/api/Android/AndroidController.h"
#include "input/api/Device/DeviceController.h"


ControllerPtr ControllerFactory::CreateController(InputAPI::Type api, std::string_view uuid,
                                                  std::string_view display_name)
{
	switch (api)
	{
#if HAS_KEYBOARD
	case InputAPI::Keyboard:
		return std::make_shared<KeyboardController>();
#endif
#if HAS_DSU
	case InputAPI::DSUClient:
		{
			const auto index = ConvertString<uint32>(uuid);
			return std::make_shared<DSUController>(index);
		}
#endif
	case InputAPI::Android:
		return std::make_shared<AndroidController>(uuid, display_name);
	case InputAPI::Device:
		return std::make_shared<DeviceController>();
	default:
		throw std::invalid_argument(fmt::format("unhandled controller api: {}", api));
	}
	/*
	case InputAPI::WGIGamepad: break;
	case InputAPI::WGIRawController: break;
	*/
}

EmulatedControllerPtr
ControllerFactory::CreateEmulatedController(size_t player_index, EmulatedController::Type type)
{
	switch (type)
	{
	case EmulatedController::Type::VPAD:
		return std::make_shared<VPADController>(player_index);
	case EmulatedController::Type::Pro:
		return std::make_shared<ProController>(player_index);
	case EmulatedController::Type::Classic:
		return std::make_shared<ClassicController>(player_index);
	case EmulatedController::Type::Wiimote:
		return std::make_shared<WiimoteController>(player_index);
	default:
		throw std::runtime_error(fmt::format("unknown emulated controller type: {}", type));
	}
}

ControllerProviderPtr ControllerFactory::CreateControllerProvider(InputAPI::Type api, const ControllerProviderSettings& settings)
{
	switch (api)
	{
#if HAS_KEYBOARD
	case InputAPI::Keyboard:
		return std::make_shared<KeyboardControllerProvider>();
#endif
#if HAS_DSU
	case InputAPI::DSUClient:
		{
			try
			{
				const auto& dsu_settings = dynamic_cast<const DSUProviderSettings&>(settings);
				return std::make_shared<DSUControllerProvider>(dsu_settings);
			}
			catch (const std::bad_cast&)
			{
				cemuLog_log(LogType::Force, "failing to cast ControllerProviderSettings class to DSUControllerProvider");
				return std::make_shared<DSUControllerProvider>();
			}
		}

#endif
	case InputAPI::Android:
		return std::make_shared<AndroidControllerProvider>();
	case InputAPI::Device:
		return std::make_shared<DeviceControllerProvider>();
	default:
		cemu_assert_debug(false);
		return {};
	}
}
