add_library(CemuInput
	InputManager.cpp
	InputManager.h
	ControllerFactory.cpp
	ControllerFactory.h
	api/ControllerState.h
	api/Controller.cpp
	api/Controller.h
	api/ControllerState.cpp
	api/InputAPI.h
	api/ControllerProvider.h
	api/DSU/DSUController.h
	api/DSU/DSUControllerProvider.cpp
	api/DSU/DSUController.cpp
	api/DSU/DSUControllerProvider.h
	api/DSU/DSUMessages.h
	api/DSU/DSUMessages.cpp
	api/Keyboard/KeyboardControllerProvider.h
	api/Keyboard/KeyboardControllerProvider.cpp
	api/Keyboard/KeyboardController.cpp
	api/Keyboard/KeyboardController.h
	emulated/ProController.cpp
	emulated/EmulatedController.h
	emulated/EmulatedController.cpp
	emulated/ProController.h
	emulated/WPADController.cpp
	emulated/WPADController.h
	emulated/WiimoteController.h
	emulated/VPADController.cpp
	emulated/WiimoteController.cpp
	emulated/VPADController.h
	emulated/ClassicController.cpp
	emulated/ClassicController.h
)

cemu_use_precompiled_header(CemuInput)

target_sources(CemuInput PRIVATE
		api/Android/AndroidController.cpp
		api/Android/AndroidController.h
		api/Android/AndroidControllerProvider.cpp
		api/Android/AndroidControllerProvider.h
		api/Android/ControllerManager.cpp
		api/Android/ControllerManager.h
		api/Device/DeviceController.cpp
		api/Device/DeviceController.h
		api/Device/DeviceControllerProvider.cpp
		api/Device/DeviceControllerProvider.h
	)

target_include_directories(CemuInput PUBLIC "../")

target_link_libraries(CemuInput PRIVATE
	CemuCommon
	CemuGui
)
