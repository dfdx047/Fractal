add_library(CemuAndroid SHARED
        AndroidFilesystemCallbacks.cpp
        AndroidInputHelpers.cpp
        CompressTitleCallbacks.cpp
        GameTitleLoader.cpp
        Image.cpp
        JNIUtils.cpp
        NativeAccount.cpp
        NativeActiveSettings.cpp
        NativeEmulatedUSBDevices.cpp
        NativeEmulation.cpp
        NativeFrameGen.cpp
        NativeGameTitles.cpp
        NativeShaderCache.cpp
        NativeGraphicPacks.cpp
        NativeInput.cpp
        NativeLib.cpp
        NativeLocalization.cpp
        NativeLogging.cpp
        NativeSettings.cpp
        NativeSwkbd.cpp
        WuaConverter.cpp
)

cemu_use_precompiled_header(CemuAndroid)

target_link_libraries(CemuAndroid PRIVATE
        -landroid
        CemuCommon
        CemuAudio
        CemuComponents
        CemuCafe
        CemuBin
        CemuGui
        ZArchive::zarchive
        stb
)

# Third-party archives stay out of the dynamic symbol table, so only the code the emulator calls is kept. The
# emulator's own symbols remain exported because the crash log's stack trace names functions through them
# (dladdr); -Bsymbolic-functions still binds calls between them directly instead of through the PLT.
target_link_options(CemuAndroid PRIVATE
        "-Wl,--exclude-libs,libadrenotools.a:liblinkernsbypass.a:libboost_container.a:libboost_context.a:libboost_iostreams.a:libboost_nowide.a:libboost_program_options.a:libboost_random.a:libboost_system.a:libbz2.a:liblzma.a:libcrypto.a:libssl.a:libcurl.a:libcubeb.a:libfmt.a:libglm.a:libglslang.a:libih264d.a:libimguiImpl.a:libpugixml.a:libusb-1.0.a:libxbyak_aarch64.a:libz.a:libzarchive.a:libzstd.a:libstb.a"
        "-Wl,-Bsymbolic-functions"
)
