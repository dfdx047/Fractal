# NOTICE

**Fractal** is a Wii U emulator for Android, maintained by DFD.
It is a fork of other projects and keeps their license: the
**Mozilla Public License 2.0** (see `LICENSE.txt`). Original license
headers in existing files are kept as they are.

## Credits

### Cemu — cemu-project
- https://github.com/cemu-project/Cemu
- The emulator core (PowerPC recompiler, Latte GPU emulation, Cafe OS HLE,
  audio, input) and most of the code in this repository.

### Cemu Android port — SSimco
- https://github.com/SSimco/Cemu (branch `android-port`)
- The Android port that Fractal is forked from: the Android build setup,
  JNI layer, Kotlin/Jetpack Compose app and ARM64 support work.
- Fork point: commit `8759323e` (2026-04-27).

### Cemu Plus — danielcamilo1
- https://github.com/danielcamilo1/cemu-plus
- Changes brought in from Cemu Plus:
  - `c44f90c1`: lock for 64-bit coreinit atomics on misaligned addresses
    (SIGBUS fix on ARM64, e.g. Star Fox Zero).
  - `40b46380`: crash details written to log.txt/logcat and Android
    tombstone (native part only).
  - `d0301dbe`: AMD FSR 1 and Snapdragon GSR upscale filters.
  - `5a189b9c`: FSR RCAS pass, GSR EdgeDir variant and sharpness setting
    (upscaling part only).
  - `86476991`: textures whose data is outside of mapped memory are cleared
    instead of crashing.
  - `1490fda6`: Cemu log also written to logcat on Android.
  - `916ed53e` (with native fixes from `b9ed9b1c`): Lossless Scaling frame
    generation (`src/Cafe/HW/Latte/Renderer/Vulkan/FrameGen/`, the Android
    Lossless.dll picker and settings). Ported from Eden / lsfg-vk.

## Third-party components

Lossless Scaling frame generation (`src/Cafe/HW/Latte/Renderer/Vulkan/FrameGen/`):
Copyright 2025 lsfg-vk, Copyright 2026 Eden Emulator Project, ported to Cemu by
Cemu Plus. These files are under the **GNU General Public License 3.0 or later**
(see their SPDX headers), so a build that includes them also has to follow the
GPL-3.0 terms (MPL-2.0 code may be combined with GPL code). Fractal does not include or
download Lossless Scaling or its `Lossless.dll`: the user selects their own copy,
whose shaders are only read.

Performance HUD (`src/android/app/src/main/java/dev/dfd/fractal/emulation/hud/`): based on
WinlatorHUD from Winlator-Ludashi by StevenMXZ (https://github.com/StevenMXZ/Winlator-Ludashi), under the
**GNU General Public License 3.0** (see the SPDX headers of those files), so a build that includes them also follows
the GPL-3.0 terms.

Third-party libraries under `dependencies/` and those pulled through vcpkg
keep their own licenses. See each component's license file.

AMD FidelityFX Super Resolution 1: Copyright (c) 2021 Advanced Micro Devices, Inc. (MIT).
Snapdragon Game Super Resolution 1: Copyright (c) 2025 Qualcomm Innovation Center, Inc. (BSD-3-Clause).

Fonts bundled with the Android app (`src/android/app/src/main/res/font`), both
under the SIL Open Font License 1.1 (full text in `res/raw/license_ofl_*.txt`):

- Space Grotesk: Copyright 2020 The Space Grotesk Project Authors (https://github.com/floriankarsten/space-grotesk)
- Inter: Copyright 2020 The Inter Project Authors (https://github.com/rsms/inter)

## Files added by Fractal

New files written for Fractal may carry an MPL-2.0 header naming DFD as the
author. Modifications to existing files keep the original header.
