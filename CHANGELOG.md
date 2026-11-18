# Changelog

All notable changes to Fractal. Commit references point to `github.com/dfdx047/Fractal/commit/<hash>`; the
[roadmap](docs/ROADMAP.md) has the full list with the problem behind each fix.

Translations (release notes): v0.2-beta [Português (Brasil)](docs/release-notes/v0.2-beta.md#português-brasil) ·
[Español](docs/release-notes/v0.2-beta.md#español); v0.1-beta
[Português (Brasil)](docs/release-notes/v0.1-beta.md#português-brasil) ·
[Español](docs/release-notes/v0.1-beta.md#español).

## v0.2-beta (unreleased)

### Added

- **Performance HUD:** FPS, CPU and GPU load, CPU and battery temperatures, RAM, power draw, battery level and charging
  and the GPU model over the game, drawn in the look of each app theme (Solid, Liquid Glass, Blur, Material You,
  Synthwave). Ported from StevenMXZ's Winlator-Ludashi HUD (GPL-3.0). Items, size, background opacity, row or column
  and position (presets or dragged with a finger) in the overlay settings and in the in-game menu. GPU usage comes from
  the emulator's own command buffer timestamps on phones that hide their GPU load (Mi A3, Kirby: fps unchanged,
  28.9 vs 29.1). The classic overlay's frame time, frame generation, draw calls and Fractal's own CPU and RAM use are
  HUD items too (`bc5372e2`, `b558e01a`, `378222ec`).
- **Graphic packs and cheats in the in-game menu:** the running game's packs, cheats and mods; turning a pack on or off
  or changing a preset applies it to the running game like Cemu's graphic packs window on PC. Packs with texture rules
  (most resolution packs) still need a restart, and a message says which of the two happened (issue #5) (`c6522627`).
- **Game settings in the in-game menu:** the running game's profile opens with the game paused, changes are saved at once
  and **Restart game** relaunches the title so they take effect (`98364ff2`).
- **Community shader caches** ([Fractal_Shaders](https://github.com/dfdx047/Fractal_Shaders)): a section in each game's
  settings downloads the shared cache, checks its size and version, backs up the local cache and merges the new entries
  into it. Sharing is anonymous (no GitHub account, each install sends only a random id) and every submission is
  reviewed before it is merged. Tested with Deus Ex: shared from the POCO, downloaded on the RP5, which then built all 92
  large shaders on the loading screen instead of during play (`63968db1`, `c75e10c7`).
- **Data folder of your choice** (Settings › System › Emulator data folder): a folder in internal storage or on an SD
  card instead of `Android/data`, which newer Android versions hide and which is deleted with the app. The data is moved
  (with progress when copied), the default folder is used again if the chosen one becomes unusable, and going back can
  move the data back or leave it (`5f607b21`, `af271be2`).
- **Mod install** from a .zip or a folder in the graphic packs screen. Packs with `rules.txt` go to `graphicPacks/mods`;
  console mods (SDCafiine `content`/`aoc`/`code` folders) ask for the game and get a `rules.txt` (`5f607b21`).
- **Samsung Xclipse GPUs** (Exynos 2200/2400/2500, 1480/1580) are detected and get RADV custom drivers in the drivers
  screen (JimVulkan, paulowesll, JesusChapman, aaditya-v-more, nowhere02). Not tested on an Xclipse device yet; if a
  driver fails, the driver guard goes back to the system driver (`8da0d804`).
- faux123's universal PanVK package for Mali-G57/G77/G610/G710 class GPUs in the driver list (`45be70a4`).
- **Controller type per game:** "Player 1 controller" (GamePad / Pro / Classic / Wiimote) in the game settings. About
  20 s into a game, a one-time hint when the game may ignore the setup (a Pro Controller in a game that reads the
  GamePad, or the reverse), with a button that sets it for this game. "Nintendo button layout" option; controller slots
  named "Player N · type" (`5e6c4e42`).
- **Resolution hint:** after a minute of play at a median under 22 fps, a game whose graphic packs have a resolution
  option is offered 640x360 once, with a restart. On an Adreno 610: Kirby and the Rainbow Curse 12 → 27 fps, NSMBU
  20 → 34, Captain Toad 16 → 30 (`08c67298`).
- **Menu music and sounds:** library music per theme (an intro, then a gapless loop) and menu sound effects, each with
  on/off and volume; your own music file and effects (up to 5 s each) in Appearance › Sounds
  (`98364ff2`, `f1d24be9`, `c8e0da47`).
- **Debug report offer:** after a crash or a freeze, the next launch offers to share the debug report and says where to
  post it (issue #2) (`40d57923`).

### Performance

- **ARMv8 extensions and NEON:** optional ARMv8 extensions (AES, PMULL, CRC32, LSE atomics, FP16, dot product) are
  detected at runtime, so the build stays ARMv8.0-A for every arm64-v8a phone (`b1078675`). Measured on the POCO X6 Pro
  (Dimensity 8300):
  - AES-128 with the Crypto Extension for encrypted disc reads (WUD/WUX sectors, NUS content): 32 MB/s → 4.1 GB/s for a
    32 KiB sector (`852801bb`).
  - CRC32 instructions for the checks of every RPX/RPL section at load: 1.2 → 13.6 GB/s (`64efc3e5`).
  - Index buffer maximum that skips the primitive restart index, with NEON: 30000 u16 indices 51.5 → 4.3 µs, u32
    35.6 → 8.4 µs (`bd6af3dc`).
  - BC1-BC5 texture decoding on the CPU (GPUs without BCn, such as Mali) through an 8 bit palette per block and NEON
    table lookups, bit exact with the old code: 1024x1024 BC1 4.0 → 1.5 ms, BC3 5.7 → 2.8, BC4 3.8 → 1.3, BC5 7.0 → 3.2
    (`c4e6b0aa`).
- **Big cores:** the emulated CPU cores and the GPU command thread stay off the little cluster (CPUs below 75% of the
  largest `cpu_capacity`); Android put them on the Cortex-A55 of the Snapdragon 865 often enough to cost a lot. Amazing
  Spider-Man 2 on the Retroid Pocket 5: 34.9 → 40.3 fps with CPU use 262 → 193 %. No change on the POCO X6 Pro and the
  Mi A3; nothing is pinned on CPUs without a little cluster (Snapdragon 8 Elite). `big_cores=0` in `perf/tuning.txt`
  turns it off (`03b83590`).
- **Recompiler:** `addo`/`subfo`/`addco`/`subfco`, `mfxer`/`mtxer`, `fcmpo`, `bdnzf`-style branches, conditional far
  branches, `subfme`, `divwo`/`divwuo` and `bcl 20` no longer send functions to the interpreter, and a huge contiguous
  function is translated in windows instead of whole for every new entry. Teslagrad on the Mi A3: host code 118 → 39 MB,
  1 % low 3.7 → 10.3 fps, interpreted functions 770 → 157 → 0; Disney Infinity 2.0: 145 → 0 (`66f59e89`, `85b29bd9`).

### Fixed

- **Super Smash Bros. for Wii U:** froze loading a stage on the RP5. `OSMutex` now has priority inheritance, as Cafe OS
  does. Default profile on the system driver: with Turnip the RP5 rebooted at the end of 4 of 4 matches
  (`5568a685`, `614d2531`).
- **Splinter Cell: Blacklist:** crash 27 s into the game (copy into a 1D surface) (`dd40e27e`); locked at 20 fps on every
  device and stuck on the mission loading screen on the RP5: occlusion query results now reach the game while the GPU is
  idle, 30 fps (its cap) on the POCO and the RP5 (`1557a268`); `OSGetMapVirtAddrRange` and `OSGetDataPhysAddrRange`
  implemented (`ad5d1dc1`).
- **Monster Hunter 3 Ultimate:** crash or freeze 245 s into a new game, when the opening movie ends (a race in the game's
  streaming loader); 6/6 runs reach the village (`dd4aa043`).
- **Batman: Arkham City:** froze for good after New Game ("Saving...") on every device: occlusion queries that ended with
  no host query running never finished (`e5dc06a1`).
- **Breath of the Wild:** closed while loading a community shader cache (issue #3): fetch shaders with 5-8 or 13-16
  fetches were misread; upstream Cemu has the same check (`42a2fbd7`).
- **New Super Mario Bros. U:** crash 57 s into the boot on slower phones (Mi A3), without the "Title Screen Crash Fix"
  pack (`ff719783`).
- **Resident Evil Revelations:** crash on Mali (system driver and PanVK) right after the opening (`71f3a05a`).
- **Yoshi's Woolly World:** on the Mali driver Yoshi and the enemies were missing and black patches covered the yarn:
  streamout through `VK_EXT_transform_feedback` where vertex pipeline stores are missing, and BC4/BC5 SNORM textures
  (normal maps, in any game) decoded correctly on the CPU (`95904874`). Default profile on the system driver: Turnip T30
  hung the RP5 GPU in World 1-1 (`5c8f856d`).
- **Pikmin 3:** Qualcomm driver crash on shaders that read and write registers by index (`ab851c1a`).
- **Twilight Princess HD:** black minimap on the Qualcomm driver (`a95f1b41`).
- **Cars 3, Ninja Gaiden 3, Disney Infinity 3.0:** draws skipped on the Qualcomm driver because rect emulation pipelines
  failed (Cars 3: 25 → 0) (`eca58396`).
- **Sonic Lost World:** crash (SIGBUS) after the title screen (`70eb6796`); crash or hang when skipping the opening movie
  in multi-core, so its default profile uses the single-core recompiler and the system driver (PanVK rebooted the POCO)
  (`fdbc093c`, `122d88bb`).
- **Rayman Legends:** hang on the loading screen in multi-core, so its profile is single-core (RP5 17.3 vs 17.0 fps,
  nothing lost) and uses the system driver (RP5: Turnip 17 vs 59 fps; POCO: PanVK 18 vs 29.6) (`fdbc093c`, `1f6476e3`).
- **Paper Mario: Color Splash:** colored dots over the picture on Turnip and PanVK (`70eb6796`).
- **Kirby and the Rainbow Curse:** Turnip in system memory mode rebooted the RP5 entering Green Valley 1-1; default
  profile on the system driver, Turnip without sysmem (`803fed48`).
- **FIFA 13:** crash 8 s after start on Turnip T19 in system memory mode; profile without sysmem (`8d168fa9`).
- **LEGO Batman 2:** black flicker every other frame on every GPU (`de5d2869`).
- **Teslagrad:** stopped (SIGTRAP) when saving (`8daaafec`).
- **Disney Infinity 3.0:** stopped in `getsockname` (`6b27c9ae`) and when the buffer cache filled with streamout data
  (`146fdb6e`, `eca58396`).
- **Call of Duty: Ghosts:** crash when the game relaunches itself at first boot (`e2030e99`).
- **ZombiU (USA):** black screen with update v32 (also on Cemu for PC); the title now starts without that update
  (`b6cf7c8b`).
- **Hot Wheels: World's Best Driver:** freeze on a load (RP5 1 run in 3): two races in the game's render thread
  handshake; RP5 8 of 8 and POCO 5 of 5 runs without a freeze (`dfde2b64`).
- **The Binding of Isaac: Rebirth (USA):** 13 to 43 thousand broken GPU command buffers a run (Cemu's state reload
  workaround only knew the other release) and a crash on the POCO; now none, 58.7 fps on the RP5. The picture is still
  black, see known issues (`226c1868`).
- **Raster ops on the Adreno and Mali vendor drivers:** pipelines with a logic op other than COPY turned it on although
  the driver lacks the feature (invalid in Vulkan, result up to the driver); they now draw as COPY there. Turnip and
  PanVK are unchanged (`a0b305c5`).
- **PanVK:** every game crashed on its first frame with faux123's PanVK (`VK_KHR_present_wait` without
  `vkWaitForPresentKHR`) (`45be70a4`).
- **All games:** settings changed in the game menu were overwritten by the library (`e91a7121`); the app no longer closes
  at start on ROMs that deny `Android/data` and falls back to its private folder (issue #1) (`42a2fbd7`, `5f607b21`);
  the ARM64 recompiler fuses multiply-add like Espresso and the interpreter (`fec3837f`); draws that sample their own
  color attachment read a copy on every driver (`70eb6796`).

### Changed

- **Android only:** the desktop code is gone (wxWidgets GUI, OpenGL and Metal renderers, x86-64 recompiler, Windows,
  macOS and desktop Linux code, SDL/XInput/DirectInput input, desktop audio backends, Discord presence): 572 files,
  about 103 thousand lines. The native library is 22.7 → 18.4 MB (third-party symbols no longer exported), only arm64
  libraries are packaged, unused resources are dropped at build time and graphic packs download without Ktor
  (`9aa94b36`, `ee6e63b1`, `d07f65ea`, `54b44032`, `4d4403c3`, `4b0f2868`, `56d83cd7`, `054ddf0f`, `bc69967a`,
  `10bd5abd`, `5e0236a6`).
- Dialog buttons look like the app's buttons and share the dialog width, or stack when a label doesn't fit
  (`af271be2`).
- The classic overlay is renamed and points to the performance HUD (`378222ec`).

### Known issues

- **The Binding of Isaac: Rebirth (USA):** runs (sound, no crash) with a black picture on every phone tested.
- **FIFA 13:** crashes in the game's audio thread 10-20 s after boot on the RP5 (4 of 6 runs), with every driver:
  in multi-core the audio thread mixes before the output channels exist (single-core never gets past the black boot
  screen). The pitch and the stadium are black on every GPU (Adreno, Mali, Turnip, PanVK); official Cemu shows a black
  screen too (cemu-project/Cemu#1907).
- **Mario Kart 8 on phones with 3-4 GB of RAM:** on the Mi A3 (3.6 GB) the race load ends the game: Android kills it for
  memory on the device's driver, and Turnip T19 restarts the phone.
- **Shader caches from another phone:** Watch Dogs on the AYN Odin 3 (Adreno 830) closed at boot with a pipeline cache
  made on another device (the Turnip Gen8 and Qualcomm compilers crash on some of its pipelines); with its own cache it
  plays (900 s, 45-60 fps).

### Tools

- 79 new benchmark scenes (`tools/perf/scenes`), GamePad touch and frame-held presses in input scripts, write watch for
  recompiled code (`watch_jit=1`), NEON bit-exact tests (`tools/neon_tests`), a separate GPU wait for linear surface
  copies in the perf log, and `FRACTAL_TEST_BUILD=1` for a "Fractal Teste" build that installs next to the official app
  (`6a78d980`, `e79e82bd`, `52ff15de`, `a4363e5f`, "Perf scenes" commits).

## v0.1-beta, updated build (2026-11-15, version code 2)

### Added

- **GamePad screen on a second display:** dual screen handhelds (AYN Thor) and external monitors get the GamePad screen
  through a Presentation, with touch; GamePad screen scaling (keep aspect ratio / stretch) and size (25-100%) in the
  in-game menu; GamePad choices remembered between games. Based on SapphireRhodonite's `android-port-dual` (`f29f2800`).
- **Frame generation** (Lossless Scaling, ported from Cemu Plus, from lsfg-vk and Eden) with the user's own
  `Lossless.dll`: fixed 2x/3x/4x or adaptive to 60/90/120 FPS, motion estimation resolution, import and settings in
  Settings and in the in-game menu. Off by default; when off the Vulkan device is created as before
  (`0a4f24ce`, `1fa22cec`, `eb14d852`).
- Perf tools from the AC IV investigation, all off by default (`c23ca3b5`).

### Fixed

- **Assassin's Creed IV:** the recompiler's `fsel` copied only ps0, leaving garbage in ps1 that a frame time routine read;
  the jungle blackout is much rarer, not gone (`b8d27fdf`).
- Back button on handhelds whose gamepad sends it (AYN Odin 3) opens the in-game menu (`026b580a`).
- Releasing the GamePad swapchain while paused no longer hangs the UI (`2171c12e`).
- A GamePad surface created before the renderer (shown from the start, second display) no longer crashes (`f29f2800`).

### License

- The frame generation code (`src/Cafe/HW/Latte/Renderer/Vulkan/FrameGen/`) is GPL-3.0-or-later; builds that include it
  follow the GPL-3.0 terms (see `NOTICE.md`).

## v0.1-beta (2026-11-15) - first public beta

Based on SSimco's Android port of Cemu (`8759323e`) with official Cemu `main` merged up to `3b103161` (2026-11-04).

### Added

- **New interface:** five themes (Solid, Liquid Glass, Blur, Material You, Synthwave), static and animated wallpapers or
  a custom image, intro animation per theme, Sierpinski app icon (`73fd0c96`, `4489bc72`, `a59f82f8`).
- **Library:** grid, carousel and list views, play time, last played, sorting, favorites; welcome setup; appearance
  screen (`80712467`, `ecf40b3b`).
- **Per-game settings:** Resolution & FPS options detected from the community graphic packs, driver, graphic packs, saves
  and shader cache sections; save and shader cache export/import (`ecf40b3b`, `d95f0191`, `9a359b76`).
- **GPU drivers page:** catalog of Turnip builds for Adreno and experimental PanVK/malisx2 drivers for Mali from their
  GitHub releases, one-tap install, global or per-game choice, automatic fallback to the system driver
  (`3927b698`, `7ce49626`, `9e8cf0c3`, `0386ec86`).
- **On-screen controls:** profiles, macros, turbo, toggle, slide between buttons, edit mode, theme colors, per-button
  style, shortcut buttons (`c813042e`, `9c15535f`, `5af1e981`).
- **Controllers:** automatic mapping of connected gamepads, HOME on Mode/Guide, hotkeys for pause, screenshot, TV/PAD
  swap, overlay, FPS, aspect ratio, upscale filter and motion (`841f97a3`, `e1698c63`, `b343cab3`, `571c9d87`).
- **In-game side menu** with display, graphics, controls, pause and screenshots (`d6bba2e5`).
- **Gamepad navigation** and a compact button legend on every screen (`4008a538` to `572b8a9e`).
- **Languages:** English, Português (Brasil) and Español, picked from the device on first run (`c9d8a940`, `a74e2a35`).
- **Upscaling:** AMD FSR 1, Snapdragon GSR and RCAS sharpening, from Cemu Plus (`2c5dba03`, `8328081a`).
- **Debug report:** light performance log for every session, `events.txt`, freeze watchdog, crash notes, previous log
  kept, **Share debug report** in the library options (`be081807`).
- Performance log, benchmark scenes and profiling tools in `tools/perf`.

### Changed

- Vulkan renderer tuned for tile-based mobile GPUs: fewer render pass breaks for buffer uploads, occlusion queries,
  texture uploads and barriers; framebuffer compression kept; one pipeline cache per driver; 1-2 pipeline compile
  threads on Android (`2c1895ac`, `7b36890f`, `d9606be8`, `ec46fba8`, `2eeceaea`, `017be0ef`, `2e43b0b7`, `9893cf72`).
- Turnip: completion marker instead of blocking fence checks; system memory rendering by default
  (`65047547`, `7f8d5a0f`).
- ARM64 recompiler: cycle counter in a register, code arena, shorter code; more instructions recompiled
  (`23b10251`, `a26051bf`, `9107127b`).
- The main core and the GPU thread sleep when idle and while paused (`ff38f5b4`, `bf0944a4`).
- Settings home grouped in sections; older screens restyled (`b7428b86`, `383af9d2`).

### Fixed

- **The Amazing Spider-Man:** no picture; frames copied into the scan buffer with `GX2CopySurface` are now shown (`79f2f691`).
- **Watch Dogs:** crashes at boot and in gameplay, frozen picture from the prologue, long freeze entering gameplay, white
  stadium on Adreno, freeze after long sessions (`cb5321e6`, `d316661a`, `8b69778e`, `2eef8ff5`, `6d80adae`, `9b6ee09b`,
  `69ac8320`, `f8dbb223`, `210b613e`).
- **Injustice: Gods Among Us:** GPU hang and stretched fighters (`2eef8ff5`).
- **Need for Speed: Most Wanted U:** engine sound pops, crash entering the city; Bloom Fix pack on by default
  (`44cdd26b`, `1feba3d6`, `0386ec86`).
- **Ninja Gaiden 3: Razor's Edge:** freeze after a few minutes, recompiler crash, blurred picture on Turnip, allocator
  crash (`8eaae97a`, `a26051bf`, `64c4b75c`).
- **Tekken Tag Tournament 2:** Qualcomm driver crash on the first fight (`1293cd7d`).
- **Assassin's Creed IV:** colored blocks on the Qualcomm driver (`64c4b75c`).
- **Xenoblade Chronicles X:** Turnip GPU hang, Adreno 650 submission failure (`2a72a16b`, `017be0ef`).
- **Bayonetta 2:** crash/hang at boot (`613dc67b`).
- **Pokkén Tournament:** crash leaving menus (`8ea62197`).
- **Breath of the Wild:** Turnip hang at boot; per-game `GX2DrawDone` sync off (`525a6907`, `951eb1e7`).
- **Star Fox Zero:** SIGBUS on misaligned atomics, from Cemu Plus (`c35755ec`).
- **Mario Kart 8:** sound mixer crash, from official Cemu (`c62e6977`).
- **All games:** precompiled pipelines built with the wrong state (`69ac8320`); settings and game list lost after a
  reboot (`9db92543`); error dialogs that could not be closed with a gamepad (`5879e1f5`); quick on-screen taps lost
  (`8929b786`); flush-to-zero not enabled on Android (`b385304f`).

### Known issues

See [In progress](docs/ROADMAP.md#in-progress) in the roadmap.
