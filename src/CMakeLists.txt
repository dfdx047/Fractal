project(cemuMain)

option(CEMU_CXX_FLAGS "Additional flags used for compiling Cemu source code")
separate_arguments(CEMU_CXX_FLAGS NATIVE_COMMAND ${CEMU_CXX_FLAGS})
if(CEMU_CXX_FLAGS)
	add_compile_options(${CEMU_CXX_FLAGS})
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message( FATAL_ERROR "Pointers are not 64bit" )
endif()

add_compile_definitions(VK_USE_PLATFORM_ANDROID_KHR)

# warnings
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
	add_compile_options(-Wno-ambiguous-reversed-operator)
endif()
add_compile_options(-Wno-multichar -Wno-invalid-offsetof -Wno-switch -Wno-ignored-attributes -Wno-deprecated-enum-enum-conversion)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# it's not straight forward to reuse the PCH from CemuCommon, so every target builds its own
function(cemu_use_precompiled_header target_name)
	target_precompile_headers(${target_name} PRIVATE "${CMAKE_SOURCE_DIR}/src/Common/precompiled.h")
endfunction()

add_subdirectory(Common)
add_subdirectory(gui)
add_subdirectory(Cafe)
add_subdirectory(Cemu)
add_subdirectory(config)
add_subdirectory(input)
add_subdirectory(audio)
add_subdirectory(util)
add_subdirectory(imgui)
add_subdirectory(resource)

add_library(CemuBin STATIC
	main.cpp
)

cemu_use_precompiled_header(CemuBin)

target_link_libraries(CemuBin PRIVATE
	CemuAudio
	CemuCafe
	CemuCommon
	CemuComponents
	CemuConfig
	CemuGui
	CemuInput
	CemuUtil
)

add_subdirectory(android/app/src/main/cpp)
