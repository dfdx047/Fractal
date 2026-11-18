add_library(CemuResource)

cemu_use_precompiled_header(CemuResource)

# .S files are used for resource embedding, we can drop this once C/C++ #embed is generally available
enable_language(ASM)

target_sources(CemuResource PRIVATE
	embedded/fontawesome.S
	CafeDefaultFont.cpp
)

target_include_directories(CemuResource PUBLIC "../")

target_link_libraries(CemuResource PRIVATE CemuCommon)
