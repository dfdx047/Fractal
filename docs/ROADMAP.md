<p align="center">
  <b>English</b> · <a href="ROADMAP.pt-BR.md">Português (Brasil)</a> · <a href="ROADMAP.es.md">Español</a>
</p>

# Fractal roadmap

Status: **v0.2-beta (in preparation)**, November 2026; the [changelog](../CHANGELOG.md) lists the changes of each
release. Every fix listed under *Done* links to the commit that made it, and the numbers come from the measurements
written in those commits.

## Where Fractal comes from

- **Base:** SSimco's Android port of Cemu (`android-port`, fork point `8759323e`, 2026-04-27).
- **Official Cemu merged:** the 220 commits of `cemu-project/Cemu` `main` up to `3b103161` (2026-11-04), in `7c02ce75`.
- **Fractal's own work:** about 240 commits since 2026-11-08: interface, drivers, input, renderer, recompiler, ARM/NEON
  code, game fixes and debugging tools.

Test devices: **Retroid Pocket 5** (Snapdragon 865, Adreno 650), **POCO X6 Pro** (Dimensity 8300 Ultra, Mali-G615 MC6)
and **Xiaomi Mi A3** (Snapdragon 665, Adreno 610). Earlier results also come from the **Anbernic RG Cube** (Unisoc T820,
Mali-G57 MC4), still the reference for weaker Mali devices.

---

## Done

### Game fixes in the shared emulator core

These bugs are in code that Fractal shares with the PC version of Cemu, so they are not specific to Android.

| Game | What was wrong | Fix | Commit |
|---|---|---|---|
| The Amazing Spider-Man | No picture at any time ("no display" on the Cemu compatibility wiki). The game copies the finished frame into the scan buffer with `GX2CopySurface` instead of `GX2CopyColorBufferToScanBuffer` | Copies into the TV/GamePad scan buffer ranges are presented. Playable on the Fractal PC build (intro, title, chapter 1) | [`79f2f691`](https://github.com/dfdx047/Fractal/commit/79f2f691) |
| Watch Dogs | Random crashes at boot: the game's allocator needs a 128 KiB aligned arena and got 16 KiB alignment. Official Cemu has the same problem (64 KiB on Windows) | Guest memory reserved at a 4 GB aligned host address | [`cb5321e6`](https://github.com/dfdx047/Fractal/commit/cb5321e6) |
| Watch Dogs | Crash at boot: a thread polls one manager and uses another that the main thread creates microseconds later | Game patch: the thread waits for the right manager | [`d316661a`](https://github.com/dfdx047/Fractal/commit/d316661a) |
| Watch Dogs | Gameplay crash: the game runs GPU visibility jobs with transform feedback and reads the results on the CPU; Cemu only kept them on the GPU | Streamout results copied back to guest memory before the CPU can observe them; streamout offset sources follow the packet | [`8b69778e`](https://github.com/dfdx047/Fractal/commit/8b69778e) |
| Watch Dogs | Gameplay crash: indexed streamout draws wrote outputs at the index value instead of in index buffer order | Indexed streamout draws drawn from vertices gathered in index order | [`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5) |
| Watch Dogs | Picture frozen from the prologue on (checkerboard, stacked subtitles, frozen "Shoot Maurice" tutorial): the final upscale and FXAA rects were culled | No face culling for rect primitives | [`6d80adae`](https://github.com/dfdx047/Fractal/commit/6d80adae) |
| Watch Dogs | 38-68 s freeze entering gameplay; streamout shaders compiled again for every bound pixel shader | Vertex shaders with discarded rasterization don't depend on the pixel shader; large shaders cached by source. Now 9 s | [`9b6ee09b`](https://github.com/dfdx047/Fractal/commit/9b6ee09b), [`69ac8320`](https://github.com/dfdx047/Fractal/commit/69ac8320) |
| Watch Dogs | Freeze after long sessions (around 1000 s on the RP5, 1250 s on PC): the game checks that the GPU is parked on a wait for about 10 µs, then fills the next slot of its GPU job list. The command processor had already passed the wait and ran the slot empty, so a visibility job never finished. Official Cemu has the same race | The command processor checks the wait again right before its first effect; release/acquire fences around display lists. 3000 s without a freeze on PC and on the RP5 | [`210b613e`](https://github.com/dfdx047/Fractal/commit/210b613e) |
| Injustice: Gods Among Us | GPU hang: shader loops without a break became unbounded | Loop constants emulated (`IT_SET_LOOP_CONST`, loop variables in uniforms) | [`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5) |
| Injustice: Gods Among Us | Stretched fighters (GPU skinning through streamout) | Streamout offsets taken from the packet, counted in DWORDs, without the draw's base vertex | [`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5) |
| Need for Speed: Most Wanted U | Engine sound pops and crackles while accelerating | `AXDecodeAdpcmData` implemented (it was missing), full-precision biquad history, ADPCM loop context, low-pass filter overflow | [`44cdd26b`](https://github.com/dfdx047/Fractal/commit/44cdd26b) |
| Every game | Pipelines precompiled at boot were built with the GPU state of that moment, so their driver cache never matched and they were compiled again in game. Official Cemu has the same bug | Pipeline state built from the registers passed to the compiler | [`69ac8320`](https://github.com/dfdx047/Fractal/commit/69ac8320) |
| Every game | Buffer cache: a uniform buffer with size register `0xFFFFFFFF` became a zero-sized node that corrupted the range tree | Size checked | [`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5) |
| Super Smash Bros. for Wii U | Froze loading a stage on the RP5: a low-priority thread held the mutex the loader waited for while a higher-priority thread kept the core busy, so the owner never ran again. Cemu had no priority inheritance for `OSMutex` | `OSLockMutex` lends the waiter's priority to the owner (and down the chain of owners), `OSUnlockMutex` and `OSWaitCond` give it back, as Cafe OS does | [`5568a685`](https://github.com/dfdx047/Fractal/commit/5568a685) |
| Splinter Cell: Blacklist | Crash 27 s into the game: the destination of a copy into a 1D surface (512x1 R32F) was looked up through a 2D view, which the texture cache refuses | 1D and 1D-array copy targets created as 2D and 2D-array, like cubemap targets | [`dd40e27e`](https://github.com/dfdx047/Fractal/commit/dd40e27e) |
| Splinter Cell: Blacklist | Locked at 20 fps on every device and, on the RP5, stuck on the mission loading screen: occlusion query results reached guest memory only on the virtual vsync (or every 8 queries), and the game waits on 3 queries per frame | The command processor's idle loop finishes pending queries as soon as the renderer has them. 30 fps (its cap) on the POCO and the RP5, and the RP5 gets in game | [`1557a268`](https://github.com/dfdx047/Fractal/commit/1557a268) |
| Splinter Cell: Blacklist | `OSGetMapVirtAddrRange` and `OSGetDataPhysAddrRange` were unimplemented, so the game read whatever its output variables held | They return the `OSAllocVirtAddr` range and MEM2 | [`ad5d1dc1`](https://github.com/dfdx047/Fractal/commit/ad5d1dc1) |
| Batman: Arkham City | Froze for good after New Game ("Saving...") on every device: GX2 occlusion queries that ended while no host query was running never finished | With nothing in flight and no host query active, every query event issued so far counts as finished; a query ended twice or begun again early is handled | [`e5dc06a1`](https://github.com/dfdx047/Fractal/commit/e5dc06a1) |
| The Legend of Zelda: Breath of the Wild | Closed while loading a community shader cache (GitHub issue #3): fetch shaders whose clause has 5-8 or 13-16 fetches went to the legacy parser. Upstream Cemu has the same check | Mask fixed; all 5981 vertex shaders of that cache take the CF path | [`42a2fbd7`](https://github.com/dfdx047/Fractal/commit/42a2fbd7) |
| LEGO Batman 2: DC Super Heroes | Black flicker every other frame on every GPU: the game swaps twice per frame and copies to the TV once, and the second swap presented a black image | Once a game picture was presented, a swap without a copy presents nothing, like the console's scan buffer | [`de5d2869`](https://github.com/dfdx047/Fractal/commit/de5d2869) |
| Teslagrad | Stopped (SIGTRAP) when saving: `SetEndOfFile` on Unix/Android was a stub, also upstream | The file is resized to the current position | [`8daaafec`](https://github.com/dfdx047/Fractal/commit/8daaafec) |
| Disney Infinity 3.0 | Stopped in `getsockname` with a socket the emulator doesn't know | Returns -1 with `EINVAL`, or the host error | [`6b27c9ae`](https://github.com/dfdx047/Fractal/commit/6b27c9ae) |
| Disney Infinity 3.0, Cars 3 | Stopped when the 164 MB buffer cache heap filled up with streamout ranges | As a last resort, streamout ranges unused for 2 frames are dropped (POCO 3 of 3 and RP5 runs without a crash); the allocation is retried only after a failure | [`146fdb6e`](https://github.com/dfdx047/Fractal/commit/146fdb6e), [`eca58396`](https://github.com/dfdx047/Fractal/commit/eca58396) |
| ZombiU (USA) | Black screen after launch with update v32, on Cemu for PC too. The Cemu wiki says to remove the update, which a .wua does not allow | The title starts without that update (logged); intro and gameplay on the POCO | [`b6cf7c8b`](https://github.com/dfdx047/Fractal/commit/b6cf7c8b) |

Also in shared code: the recompiler now handles `psq_lx`/`psq_stx` and their update forms, which official Cemu sends to
the interpreter ([`9107127b`](https://github.com/dfdx047/Fractal/commit/9107127b); in The Amazing Spider-Man 2 these
are the audio sample converters that kept a core near 100 % during loading).

**Assassin's Creed IV: Black Flag:** in our tests the official Cemu 2.6 release (AppImage) stopped at "loading..." with
no CPU use, while the Fractal PC build and the Retroid Pocket 5 play the opening cutscene and the ship deck. We have not
identified which change makes the difference.

### Game fixes for Android, ARM64 and mobile GPU drivers

| Game | What was wrong | Fix | Commit |
|---|---|---|---|
| Ninja Gaiden 3: Razor's Edge | Black screen after a few minutes on Android: a race in the game's own event flag code left two threads waiting forever | The game's event flag "set" no longer resets an event the other thread just signalled | [`8eaae97a`](https://github.com/dfdx047/Fractal/commit/8eaae97a) |
| Ninja Gaiden 3: Razor's Edge | Crash: functions of more than 1 MB of ARM64 code put branches out of range | Cold code emitted between segments; an encoding error leaves the function interpreted instead of aborting | [`a26051bf`](https://github.com/dfdx047/Fractal/commit/a26051bf) |
| Ninja Gaiden 3: Razor's Edge | Turnip: the whole picture blurred by the depth of field reading a stale depth buffer | Barrier at the end of each render pass on Turnip | [`64c4b75c`](https://github.com/dfdx047/Fractal/commit/64c4b75c) |
| Ninja Gaiden 3: Razor's Edge | Crash in its own allocator with zlib-ng as the host zlib | zlib state allocated from the default heap | [`8eaae97a`](https://github.com/dfdx047/Fractal/commit/8eaae97a) |
| Assassin's Creed IV: Black Flag | Black world (HUD only) in the jungle, near the bridge: the recompiler's `fsel` copied only the first half of the register and the game read garbage in the second half in a frame time routine | `fsel` copies the whole register, like the interpreter. The blackouts are much rarer but not gone (see In progress) | [`b8d27fdf`](https://github.com/dfdx047/Fractal/commit/b8d27fdf) |
| Tekken Tag Tournament 2 | Qualcomm system driver crashed compiling a 4x4 gather shader on the first fight | Indexed GPR writes emitted as a switch instead of a dynamically indexed GPR array | [`1293cd7d`](https://github.com/dfdx047/Fractal/commit/1293cd7d) |
| Assassin's Creed IV: Black Flag | Qualcomm system driver: green/magenta/blue blocks over particles, loading screens and cutscenes. The bloom samples the texture it renders to, which is undefined without `VK_EXT_attachment_feedback_loop_layout` | Such draws sample a copy of the texture | [`64c4b75c`](https://github.com/dfdx047/Fractal/commit/64c4b75c) |
| Watch Dogs | White stadium and bloom on Adreno: Adreno returns NaN from min/max/clamp when one operand is NaN | NaN-safe min/max/clamp on by default on Qualcomm GPUs | [`f8dbb223`](https://github.com/dfdx047/Fractal/commit/f8dbb223) |
| Xenoblade Chronicles X | Turnip: GPU hang about 36 s into a new game, every run | `R8G8B8A8_UNORM` images kept mutable (no UBWC) on Turnip | [`2a72a16b`](https://github.com/dfdx047/Fractal/commit/2a72a16b) |
| Xenoblade Chronicles X | Adreno 650: command buffer submission failed after ~160 s (copies between compressed images of different formats) | Those copies go through a staging buffer | [`017be0ef`](https://github.com/dfdx047/Fractal/commit/017be0ef) |
| Bayonetta 2 | Crash 3 s after boot (RG Cube), hang on the boot screen (RP5): the ARM64 backend never loaded the core index (`mfspr UPIR`) | SPRs above 998 loaded by name | [`613dc67b`](https://github.com/dfdx047/Fractal/commit/613dc67b) |
| Pokkén Tournament | Crash every time it left a menu: thread-local storage read on one host thread and used on another (dynamic TLS on Android) | Scheduler and fiber TLS read on the thread that runs the code. Reaches battles at 60 fps on the RP5 | [`8ea62197`](https://github.com/dfdx047/Fractal/commit/8ea62197) |
| Need for Speed: Most Wanted U | Crash entering the city: descriptor sets exhausted (over 160K texture/sampler combinations) | Descriptor pools grow on demand, sets unused for 600 frames are freed | [`1feba3d6`](https://github.com/dfdx047/Fractal/commit/1feba3d6) |
| Need for Speed: Most Wanted U | White, blown-out foliage and lights | The community "Bloom Fix" graphic pack is on by default (root cause still open) | [`0386ec86`](https://github.com/dfdx047/Fractal/commit/0386ec86) |
| The Legend of Zelda: Breath of the Wild | Turnip: GPU hang at boot with tiled rendering | Turnip renders in system memory by default (per-game option) | [`525a6907`](https://github.com/dfdx047/Fractal/commit/525a6907), [`7f8d5a0f`](https://github.com/dfdx047/Fractal/commit/7f8d5a0f) |
| The Legend of Zelda: Breath of the Wild | Render thread waited 15.6 ms per frame for a full sync at every `GX2DrawDone` | Per-game option, off in BotW's default profiles: RP5 9.4 → 11.1 fps | [`951eb1e7`](https://github.com/dfdx047/Fractal/commit/951eb1e7) |
| Star Fox Zero | SIGBUS on misaligned 64-bit atomics on ARM64 | Lock for misaligned atomics (from Cemu Plus) | [`c35755ec`](https://github.com/dfdx047/Fractal/commit/c35755ec) |
| Mario Kart 8 | Crash (sound mixer state) | `snd_user` reset brought in from official Cemu before the full merge | [`c62e6977`](https://github.com/dfdx047/Fractal/commit/c62e6977) |
| Games with an error dialog | Stuck on handhelds: the error dialog buttons could not be focused without a mouse | A and B close the dialog | [`5879e1f5`](https://github.com/dfdx047/Fractal/commit/5879e1f5) |
| All games | Settings and the game list became zeros after a reboot shortly after saving | Files are synced to storage before replacing the old ones | [`9db92543`](https://github.com/dfdx047/Fractal/commit/9db92543) |
| Every game on PanVK | faux123's PanVK (Mali-G615) crashed every game on its first frame: it reports `VK_KHR_present_wait`, which Mesa's Android WSI does not implement | `present_wait` off on PanVK; Twilight Princess HD runs at its 30 fps cap | [`45be70a4`](https://github.com/dfdx047/Fractal/commit/45be70a4) |
| Pikmin 3 | Qualcomm driver crashed creating pipelines for shaders that both read and write GPRs by index | On Qualcomm these shaders keep separate GPR variables: reads through a select chain, writes through a switch | [`ab851c1a`](https://github.com/dfdx047/Fractal/commit/ab851c1a) |
| Yoshi's Woolly World | Mali driver: Yoshi and the enemies missing. Streamout was written with storage buffer stores from the vertex shader, which the driver does not support | Streamout through `VK_EXT_transform_feedback` on drivers without vertex pipeline stores | [`95904874`](https://github.com/dfdx047/Fractal/commit/95904874) |
| Yoshi's Woolly World (any game with BC4/BC5 SNORM textures on Mali) | Black patches over the yarn: BC5 SNORM normal maps decoded on the CPU as unsigned bytes, BC4 SNORM as UNORM | Stored as two's complement bytes, SNORM endpoints mapped correctly | [`95904874`](https://github.com/dfdx047/Fractal/commit/95904874) |
| Yoshi's Woolly World | Turnip T30 hung the RP5 GPU in World 1-1 | Default profile: system driver | [`5c8f856d`](https://github.com/dfdx047/Fractal/commit/5c8f856d) |
| Super Smash Bros. for Wii U | Turnip (T19 and T30) rebooted the RP5 at the end of 4 of 4 matches | Default profile: system driver (RP5 42.6 fps, about what Turnip gave; POCO 55 fps against 37 on PanVK) | [`5568a685`](https://github.com/dfdx047/Fractal/commit/5568a685), [`614d2531`](https://github.com/dfdx047/Fractal/commit/614d2531) |
| Kirby and the Rainbow Curse | Turnip in system memory mode rebooted the RP5 entering Green Valley 1-1 in 4 of 5 runs | Default profile: system driver (60.0 fps on the RP5), Turnip without sysmem | [`803fed48`](https://github.com/dfdx047/Fractal/commit/803fed48) |
| Sonic Lost World | SIGBUS right after the title screen: `stwcx.` on an odd address became `CASAL`, which needs 4 byte alignment on ARM64 | Unaligned addresses take a plain compare and store, in the recompiler and the interpreter | [`70eb6796`](https://github.com/dfdx047/Fractal/commit/70eb6796) |
| Sonic Lost World, Rayman Legends | Multi-core: Sonic crashes or hangs when the opening movie is skipped, Rayman hangs on its loading screen (POCO: 4 of 4 and 3 of 4 runs) | Default profiles: single-core recompiler. Rayman is bound by one thread and loses nothing (RP5 17.3 vs 17.0 fps) | [`fdbc093c`](https://github.com/dfdx047/Fractal/commit/fdbc093c) |
| Rayman Legends | Mesa drivers keep the GPU at 99 % in the first level: Turnip 17 fps against 59 on the Adreno 650 driver, PanVK 18 against 29.6 on the Mali-G615 driver | Default profile: system driver | [`1f6476e3`](https://github.com/dfdx047/Fractal/commit/1f6476e3) |
| Sonic Lost World | PanVK rebooted the POCO two times out of two | Default profile: system driver (48 fps on the Mali-G615 driver; 53 on the RP5, as fast as Turnip) | [`122d88bb`](https://github.com/dfdx047/Fractal/commit/122d88bb) |
| Paper Mario: Color Splash | Colored dots over the picture on Turnip and PanVK: a pass samples the color attachment it renders to, which is undefined even with `VK_EXT_attachment_feedback_loop_layout` | The draw samples a copy taken before it, on every driver. Same speed on the POCO (30.1 fps, cap 30) | [`70eb6796`](https://github.com/dfdx047/Fractal/commit/70eb6796) |
| Monster Hunter 3 Ultimate | Crash (or freeze) 245 s into a new game, when the opening movie ends: the game's streaming loader reports a file as ready while a reopen is still pending. Single-core crashed every time on the RP5 and the POCO | Title fix: "ready" also needs no pending command. 6/6 runs reach the village | [`dd4aa043`](https://github.com/dfdx047/Fractal/commit/dd4aa043) |
| New Super Mario Bros. U | Crash 57 s into the boot on a slow CPU (Mi A3, 6/6 runs): the Mii head render queue was walked before another thread filled in a model | Title fix that skips only the entry without a model (the community "Title Screen Crash Fix" pack skips the whole queue). Mi A3: 2/2 runs reach the opening movie | [`ff719783`](https://github.com/dfdx047/Fractal/commit/ff719783) |
| Resident Evil Revelations | Crash on Mali (system driver and PanVK) right after the opening: the A2_B10_G10_R10 render target had no Vulkan format | Mapped to `A2B10G10R10_UNORM_PACK32` | [`71f3a05a`](https://github.com/dfdx047/Fractal/commit/71f3a05a) |
| The Legend of Zelda: Twilight Princess HD | Black minimap on the Qualcomm driver: the rect emulation geometry shader did not declare the same `gl_PerVertex` as the vertex shaders | Same block declared in both stages | [`a95f1b41`](https://github.com/dfdx047/Fractal/commit/a95f1b41) |
| Cars 3, Ninja Gaiden 3, Disney Infinity 3.0 | Draws skipped on the Qualcomm driver (Cars 3: 25 pipelines, Ninja Gaiden 3: 24, Disney Infinity 3.0: 2): the rect geometry shader left pixel shader inputs unwritten | The geometry shader writes zero dummies for them. Cars 3: 0 failed pipelines | [`eca58396`](https://github.com/dfdx047/Fractal/commit/eca58396) |
| FIFA 13 | Turnip T19 in system memory mode crashed the game 8 s after start (5 of 5 runs on the RP5) | Default profile: Turnip without sysmem (T19 56.5 fps) | [`8d168fa9`](https://github.com/dfdx047/Fractal/commit/8d168fa9) |
| Call of Duty: Ghosts | Crash when the game relaunches itself at its first boot (POCO): Android had no system implementation for title relaunch and exit | The renderer is built again on the same surfaces; an exit is logged | [`e2030e99`](https://github.com/dfdx047/Fractal/commit/e2030e99) |
| Hot Wheels: World's Best Driver | Freeze on a load (RP5 1 run in 3): two races in the game's own render thread handshake, made likely by preempted host threads | Title fix for both. RP5 8 of 8 and POCO 5 of 5 runs without a freeze | [`dfde2b64`](https://github.com/dfdx047/Fractal/commit/dfde2b64) |
| Teslagrad | 770 functions in the interpreter and the recompiler thread at 99 % on the Adreno 610 | More instructions recompiled, huge functions translated in windows (see the recompiler section): 1 % low 3.7 → 10.3 fps on the Mi A3, then 0 functions left in the interpreter | [`66f59e89`](https://github.com/dfdx047/Fractal/commit/66f59e89), [`85b29bd9`](https://github.com/dfdx047/Fractal/commit/85b29bd9) |
| All games | Settings changed in the game menu were overwritten by the library's older copy | Every library save reloads the file first when it changed on disk | [`e91a7121`](https://github.com/dfdx047/Fractal/commit/e91a7121) |
| All games | The app closed at start on custom ROMs that deny `Android/data` (GitHub issue #1) | Folder errors are logged instead of ending the app; the default folder falls back to the app's private folder | [`42a2fbd7`](https://github.com/dfdx047/Fractal/commit/42a2fbd7), [`5f607b21`](https://github.com/dfdx047/Fractal/commit/5f607b21) |

### Renderer (Vulkan) for tile-based mobile GPUs

Adreno and Mali store and reload every attachment when a render pass ends, so most of this work avoids breaking passes.

- Buffer uploads and occlusion queries no longer end the render pass; the buffer cache lives in host-visible memory on
  unified memory GPUs ([`2c1895ac`](https://github.com/dfdx047/Fractal/commit/2c1895ac)).
- Vertex work keeps overlapping across render target barriers; per-attachment self-dependency tracking
  ([`7b36890f`](https://github.com/dfdx047/Fractal/commit/7b36890f), [`d9606be8`](https://github.com/dfdx047/Fractal/commit/d9606be8)).
- Busy buffer cache nodes move to new memory instead of ending the pass
  ([`ec46fba8`](https://github.com/dfdx047/Fractal/commit/ec46fba8), [`a74fbb56`](https://github.com/dfdx047/Fractal/commit/a74fbb56)).
- Texture uploads go into a prologue command buffer instead of ending the pass
  ([`2eeceaea`](https://github.com/dfdx047/Fractal/commit/2eeceaea)).
- Framebuffer compression (UBWC on Adreno, AFBC on Mali) kept on color images
  ([`017be0ef`](https://github.com/dfdx047/Fractal/commit/017be0ef)).
- Turnip: `vkGetFenceStatus` blocks on KGSL, so finished command buffers are found with a completion marker
  ([`65047547`](https://github.com/dfdx047/Fractal/commit/65047547)).
- Depth clears through a render pass instead of `vkCmdClearDepthStencilImage`, which costs ~3.5 ms per 1080p clear on
  the Adreno 610 driver ([`aabeea76`](https://github.com/dfdx047/Fractal/commit/aabeea76)).
- One driver pipeline cache per driver, kept when switching drivers
  ([`2e43b0b7`](https://github.com/dfdx047/Fractal/commit/2e43b0b7)).
- Pipelines compiled on 1-2 threads on Android instead of up to 7, which cut cold-cache stutter
  ([`9893cf72`](https://github.com/dfdx047/Fractal/commit/9893cf72)).
- Swapchain picks a supported composite alpha (Mesa PanVK) ([`c1f4cd8e`](https://github.com/dfdx047/Fractal/commit/c1f4cd8e)).
- A draw that samples its own color attachment reads a copy on every driver, also where
  `VK_EXT_attachment_feedback_loop_layout` exists ([`70eb6796`](https://github.com/dfdx047/Fractal/commit/70eb6796)).
- Streamout through `VK_EXT_transform_feedback` on drivers without vertex pipeline stores (Mali driver)
  ([`95904874`](https://github.com/dfdx047/Fractal/commit/95904874)).
- Occlusion query results reach the game while the GPU is idle; queries that end with no host query running finish
  ([`1557a268`](https://github.com/dfdx047/Fractal/commit/1557a268), [`e5dc06a1`](https://github.com/dfdx047/Fractal/commit/e5dc06a1)).
- A swap with nothing copied to the screen keeps the last picture ([`de5d2869`](https://github.com/dfdx047/Fractal/commit/de5d2869)).
- Buffer cache: streamout ranges dropped as a last resort when the heap is full ([`146fdb6e`](https://github.com/dfdx047/Fractal/commit/146fdb6e), [`eca58396`](https://github.com/dfdx047/Fractal/commit/eca58396)).
- No `present_wait` on PanVK ([`45be70a4`](https://github.com/dfdx047/Fractal/commit/45be70a4)); `A2_B10_G10_R10` render targets mapped ([`71f3a05a`](https://github.com/dfdx047/Fractal/commit/71f3a05a)).

### PowerPC → ARM64 recompiler

- Cycle counter kept in a register, functions packed in one code arena, shorter jumps and immediates, flag branches,
  zero-offset memory access without an add ([`23b10251`](https://github.com/dfdx047/Fractal/commit/23b10251)).
- `mfspr UPIR` ([`d61aee38`](https://github.com/dfdx047/Fractal/commit/d61aee38), [`613dc67b`](https://github.com/dfdx047/Fractal/commit/613dc67b)),
  `ps_nabs`, `dcbt` ([`a26051bf`](https://github.com/dfdx047/Fractal/commit/a26051bf)), `psq_lx`/`psq_stx`/`psq_lux`/`psq_stux`,
  `eieio`, `dcbi` ([`9107127b`](https://github.com/dfdx047/Fractal/commit/9107127b)) no longer fall back to the interpreter.
- Huge functions stay within ARM64 branch range ([`a26051bf`](https://github.com/dfdx047/Fractal/commit/a26051bf)).
- Flush-to-zero actually enabled on Android ([`b385304f`](https://github.com/dfdx047/Fractal/commit/b385304f)).
- `stwcx.` on an unaligned address ([`70eb6796`](https://github.com/dfdx047/Fractal/commit/70eb6796)).
- Fused multiply-add (`fmadd`/`fmsub`/`fnmsub` and single forms) like Espresso and the interpreter; before, `a*c - round(a*c)`
  was zero only in recompiled code ([`fec3837f`](https://github.com/dfdx047/Fractal/commit/fec3837f)).
- `addo`/`subfo`/`addco`/`subfco`, `mfxer`/`mtxer`, `fcmpo`, `bdnzf`-style branches and conditional far branches; a
  huge contiguous function is translated in windows (in Teslagrad a 955 KB switch was translated 22 times)
  ([`66f59e89`](https://github.com/dfdx047/Fractal/commit/66f59e89)).
- `subfme`, `divwo`/`divwuo` and `bcl 20`: Disney Infinity 2.0 145 → 0 and Teslagrad 157 → 0 functions in the
  interpreter ([`85b29bd9`](https://github.com/dfdx047/Fractal/commit/85b29bd9)).

### ARMv8 extensions and NEON

- Optional ARMv8 extensions (AES, PMULL, CRC32, LSE atomics, FP16, dot product) detected at runtime; the build stays
  ARMv8.0-A for every arm64-v8a phone ([`b1078675`](https://github.com/dfdx047/Fractal/commit/b1078675)).
- AES-128 with the Crypto Extension for encrypted disc reads ([`852801bb`](https://github.com/dfdx047/Fractal/commit/852801bb)), CRC32 instructions for RPX/RPL loading
  ([`64efc3e5`](https://github.com/dfdx047/Fractal/commit/64efc3e5)), NEON index buffer maximum ([`bd6af3dc`](https://github.com/dfdx047/Fractal/commit/bd6af3dc)) and BC1-BC5 decoding through a palette per block with
  NEON table lookups for GPUs without BCn ([`c4e6b0aa`](https://github.com/dfdx047/Fractal/commit/c4e6b0aa)). Numbers in *Measured gains*.
- `tools/neon_tests` compares each NEON path with the scalar code bit for bit ([`852801bb`](https://github.com/dfdx047/Fractal/commit/852801bb)).

### Android only

- The desktop code is gone: wxWidgets GUI, OpenGL and Metal renderers, x86-64 recompiler, Windows, macOS and desktop
  Linux code, desktop input and audio backends, Discord presence (572 files, about 103 thousand lines). The native
  library is 22.7 → 18.4 MB with third-party symbols no longer exported, only arm64 libraries are packaged, unused
  resources are dropped at build time and graphic packs download with `HttpURLConnection` instead of Ktor ([`9aa94b36`](https://github.com/dfdx047/Fractal/commit/9aa94b36), [`ee6e63b1`](https://github.com/dfdx047/Fractal/commit/ee6e63b1), [`d07f65ea`](https://github.com/dfdx047/Fractal/commit/d07f65ea), [`54b44032`](https://github.com/dfdx047/Fractal/commit/54b44032), [`4d4403c3`](https://github.com/dfdx047/Fractal/commit/4d4403c3), [`4b0f2868`](https://github.com/dfdx047/Fractal/commit/4b0f2868), [`bc69967a`](https://github.com/dfdx047/Fractal/commit/bc69967a), [`10bd5abd`](https://github.com/dfdx047/Fractal/commit/10bd5abd), [`5e0236a6`](https://github.com/dfdx047/Fractal/commit/5e0236a6)).

### Shader decompiler

- Loop constants ([`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5)).
- Indexed GPR writes as a switch ([`1293cd7d`](https://github.com/dfdx047/Fractal/commit/1293cd7d)).
- R600 NaN rules for min/max/clamp on Adreno ([`f8dbb223`](https://github.com/dfdx047/Fractal/commit/f8dbb223)).
- Streamout vertex shaders independent from the bound pixel shader, SPIR-V cache by source for large shaders
  ([`9b6ee09b`](https://github.com/dfdx047/Fractal/commit/9b6ee09b)).
- Indexed GPR reads and writes without the GPR array on Qualcomm ([`ab851c1a`](https://github.com/dfdx047/Fractal/commit/ab851c1a)).
- Transform feedback outputs for streamout ([`95904874`](https://github.com/dfdx047/Fractal/commit/95904874)).
- Rect emulation geometry shader: same `gl_PerVertex` as the vertex shaders, zero dummies for pixel shader inputs
  ([`a95f1b41`](https://github.com/dfdx047/Fractal/commit/a95f1b41), [`eca58396`](https://github.com/dfdx047/Fractal/commit/eca58396)).
- Fetch shaders whose clause has 5-8 or 13-16 fetches detected ([`42a2fbd7`](https://github.com/dfdx047/Fractal/commit/42a2fbd7)).

### System and audio

- AX audio fixes ([`44cdd26b`](https://github.com/dfdx047/Fractal/commit/44cdd26b)).
- The main core and the GPU thread sleep instead of spinning when idle; the main core sleeps while the game is paused
  ([`ff38f5b4`](https://github.com/dfdx047/Fractal/commit/ff38f5b4), [`bf0944a4`](https://github.com/dfdx047/Fractal/commit/bf0944a4)).
- `GX2Init` argument parsing ([`f8dbb223`](https://github.com/dfdx047/Fractal/commit/f8dbb223)).
- `OSMutex` priority inheritance ([`5568a685`](https://github.com/dfdx047/Fractal/commit/5568a685)); `OSGetMapVirtAddrRange` and `OSGetDataPhysAddrRange`
  ([`ad5d1dc1`](https://github.com/dfdx047/Fractal/commit/ad5d1dc1)); file truncation on Android ([`8daaafec`](https://github.com/dfdx047/Fractal/commit/8daaafec)); `getsockname` errors ([`6b27c9ae`](https://github.com/dfdx047/Fractal/commit/6b27c9ae)).
- Title relaunch and exit on Android ([`e2030e99`](https://github.com/dfdx047/Fractal/commit/e2030e99)); a title starts without an update known to keep it from loading
  ([`b6cf7c8b`](https://github.com/dfdx047/Fractal/commit/b6cf7c8b)).
- Title fixes as a table of exact game code patterns (Ninja Gaiden 3, Monster Hunter 3 Ultimate, New Super Mario Bros.
  U, Hot Wheels) ([`dd4aa043`](https://github.com/dfdx047/Fractal/commit/dd4aa043), [`ff719783`](https://github.com/dfdx047/Fractal/commit/ff719783), [`dfde2b64`](https://github.com/dfdx047/Fractal/commit/dfde2b64)).

### Interface

- Five themes, design tokens, bundled fonts, glass surfaces, static and animated wallpapers
  ([`73fd0c96`](https://github.com/dfdx047/Fractal/commit/73fd0c96)); intro animation per theme
  ([`4489bc72`](https://github.com/dfdx047/Fractal/commit/4489bc72)); Sierpinski app icon
  ([`a59f82f8`](https://github.com/dfdx047/Fractal/commit/a59f82f8)).
- Library with grid, carousel and list, welcome setup, appearance screen
  ([`80712467`](https://github.com/dfdx047/Fractal/commit/80712467)); play time, last played, sorting, save and shader
  cache export/import, per-game screen ([`ecf40b3b`](https://github.com/dfdx047/Fractal/commit/ecf40b3b)).
- Resolution & FPS options detected from any game's graphic packs
  ([`d95f0191`](https://github.com/dfdx047/Fractal/commit/d95f0191), [`9a359b76`](https://github.com/dfdx047/Fractal/commit/9a359b76)).
- Settings grouped in sections, legacy screens in the new style
  ([`b7428b86`](https://github.com/dfdx047/Fractal/commit/b7428b86), [`383af9d2`](https://github.com/dfdx047/Fractal/commit/383af9d2)).
- Gamepad navigation and a compact button legend on every screen (`4008a538` to `572b8a9e`), focus restored when going
  back (`eb7e614e` to `7dc5303c`), portrait phone layout ([`94e60896`](https://github.com/dfdx047/Fractal/commit/94e60896)).
- Themed in-game side menu ([`d6bba2e5`](https://github.com/dfdx047/Fractal/commit/d6bba2e5)).
- English, Português (Brasil) and Español for every app string
  ([`c9d8a940`](https://github.com/dfdx047/Fractal/commit/c9d8a940), [`a74e2a35`](https://github.com/dfdx047/Fractal/commit/a74e2a35)).

- **GamePad on the second screen:** dual-screen handhelds (AYN Thor) and external displays get the GamePad screen with
  touch; its own scaling and size in the in-game menu; choices saved between games (based on SapphireRhodonite's
  `android-port-dual` branch) ([`f29f2800`](https://github.com/dfdx047/Fractal/commit/f29f2800)).
- The back button of handhelds whose controller sends it (AYN Odin 3) opens the in-game menu ([`026b580a`](https://github.com/dfdx047/Fractal/commit/026b580a)).
- **Performance HUD** in the look of each theme: FPS, frame time, CPU and GPU load, temperatures, RAM, power draw and
  battery, with items, size, opacity, layout and position in the settings and the in-game menu; ported from
  StevenMXZ's Winlator-Ludashi HUD (GPL-3.0); GPU usage from command buffer timestamps
  ([`bc5372e2`](https://github.com/dfdx047/Fractal/commit/bc5372e2), [`b558e01a`](https://github.com/dfdx047/Fractal/commit/b558e01a), [`378222ec`](https://github.com/dfdx047/Fractal/commit/378222ec)).
- Graphic packs and cheats in the in-game menu, applied to the running game (GitHub issue #5) ([`c6522627`](https://github.com/dfdx047/Fractal/commit/c6522627)); the
  game's settings in the in-game menu with **Restart game** ([`98364ff2`](https://github.com/dfdx047/Fractal/commit/98364ff2)).
- Community shader caches: download per game and anonymous sharing, reviewed before merging
  ([`63968db1`](https://github.com/dfdx047/Fractal/commit/63968db1), [`c75e10c7`](https://github.com/dfdx047/Fractal/commit/c75e10c7)).
- Data folder in a folder of the user's choice (internal storage or SD card); mod install from a .zip or a folder
  ([`5f607b21`](https://github.com/dfdx047/Fractal/commit/5f607b21)).
- A lower resolution offered once when a game runs well below full speed ([`08c67298`](https://github.com/dfdx047/Fractal/commit/08c67298)).
- Menu music per theme with a gapless loop, menu sounds, and the user's own music and effects
  ([`98364ff2`](https://github.com/dfdx047/Fractal/commit/98364ff2), [`f1d24be9`](https://github.com/dfdx047/Fractal/commit/f1d24be9), [`c8e0da47`](https://github.com/dfdx047/Fractal/commit/c8e0da47)).
- Dialog buttons share the dialog width or stack ([`af271be2`](https://github.com/dfdx047/Fractal/commit/af271be2)).

### GPU drivers

- Driver catalog from GitHub releases, one-tap install, per-driver cache clearing
  ([`3927b698`](https://github.com/dfdx047/Fractal/commit/3927b698)); experimental Mali drivers, more sources, automatic
  fallback to the system driver ([`7ce49626`](https://github.com/dfdx047/Fractal/commit/7ce49626),
  [`9e8cf0c3`](https://github.com/dfdx047/Fractal/commit/9e8cf0c3)); page by developer with MrPurple666's Turnip first,
  drivers and graphic packs in the welcome setup ([`0386ec86`](https://github.com/dfdx047/Fractal/commit/0386ec86)).
- Samsung Xclipse GPUs detected and offered RADV custom drivers from five sources; not tested on an Xclipse device yet
  ([`8da0d804`](https://github.com/dfdx047/Fractal/commit/8da0d804)).
- faux123's universal PanVK package for Mali ([`45be70a4`](https://github.com/dfdx047/Fractal/commit/45be70a4)).
- Default game profiles pick the driver that doesn't crash or is faster (Smash, Yoshi's Woolly World, Kirby and the
  Rainbow Curse, Rayman Legends, Sonic Lost World, FIFA 13; see the table above).

### Input

- On-screen controls: resolution-independent layouts, profiles, macros, turbo, toggle, slide, edit mode, theme colors,
  styles and shortcut buttons ([`c813042e`](https://github.com/dfdx047/Fractal/commit/c813042e),
  [`9c15535f`](https://github.com/dfdx047/Fractal/commit/9c15535f), [`5af1e981`](https://github.com/dfdx047/Fractal/commit/5af1e981)).
- Physical controllers mapped automatically ([`841f97a3`](https://github.com/dfdx047/Fractal/commit/841f97a3),
  [`e1698c63`](https://github.com/dfdx047/Fractal/commit/e1698c63)); HOME on the Mode/Guide button
  ([`b343cab3`](https://github.com/dfdx047/Fractal/commit/b343cab3)); hotkeys ([`571c9d87`](https://github.com/dfdx047/Fractal/commit/571c9d87)).
- Quick taps on on-screen buttons no longer lost ([`8929b786`](https://github.com/dfdx047/Fractal/commit/8929b786)).
- Controller type per game (GamePad / Pro / Classic / Wiimote), a one-time hint when the game may ignore the setup,
  Nintendo button layout option ([`5e6c4e42`](https://github.com/dfdx047/Fractal/commit/5e6c4e42)).

### Upscaling

- AMD FSR 1 and Snapdragon GSR, RCAS sharpening (from Cemu Plus)
  ([`2c5dba03`](https://github.com/dfdx047/Fractal/commit/2c5dba03), [`8328081a`](https://github.com/dfdx047/Fractal/commit/8328081a)).

### Frame generation

- Lossless Scaling frame generation (from Cemu Plus, which took it from lsfg-vk and Eden) with the user's own
  `Lossless.dll`: fixed 2x/3x/4x multiplier or adaptive up to 60/90/120 FPS, motion estimation resolution, import and
  options in the settings and in the in-game menu. When off, it changes nothing in Vulkan device creation
  ([`0a4f24ce`](https://github.com/dfdx047/Fractal/commit/0a4f24ce), [`1fa22cec`](https://github.com/dfdx047/Fractal/commit/1fa22cec), [`eb14d852`](https://github.com/dfdx047/Fractal/commit/eb14d852)).
- Releasing the GamePad swapchain while the game is paused no longer hangs the interface ([`2171c12e`](https://github.com/dfdx047/Fractal/commit/2171c12e)).

### Debugging

- Debug report: a light performance log for every session, `events.txt` with each problem found, a freeze watchdog,
  crash notes naming what the thread was doing, the previous log kept, and **Share debug report** in the app
  ([`be081807`](https://github.com/dfdx047/Fractal/commit/be081807)).
- Crash and freeze logs with guest code, host registers and thread stacks
  ([`c7923402`](https://github.com/dfdx047/Fractal/commit/c7923402), [`fcb9863e`](https://github.com/dfdx047/Fractal/commit/fcb9863e),
  [`f8dbb223`](https://github.com/dfdx047/Fractal/commit/f8dbb223)).
- Performance log, scripted benchmark scenes and profiling tools (`tools/perf`).
- The debug report is offered on the next launch after a crash (GitHub issue #2) ([`40d57923`](https://github.com/dfdx047/Fractal/commit/40d57923)).
- Write watch for recompiled code (`watch_jit=1`) ([`e79e82bd`](https://github.com/dfdx047/Fractal/commit/e79e82bd)); separate GPU wait for linear surface copies in
  the perf log ([`52ff15de`](https://github.com/dfdx047/Fractal/commit/52ff15de)); GamePad touch and frame-held presses in input scripts
  ([`6a78d980`](https://github.com/dfdx047/Fractal/commit/6a78d980), [`5568a685`](https://github.com/dfdx047/Fractal/commit/5568a685)); 73 new benchmark scenes.
- `FRACTAL_TEST_BUILD=1` builds "Fractal Teste", which installs next to the official app ([`a4363e5f`](https://github.com/dfdx047/Fractal/commit/a4363e5f)).

### Measured gains

From the A/B measurements in the commit messages (same scene, same device, warm shader cache). Conditions differ
between commits (driver, CPU profile), so compare rows only within themselves.

| Change | Device | Before → after | Commit |
|---|---|---|---|
| No render pass end for buffer uploads / occlusion queries | RG Cube, Mario Kart 8 | 16.0 → 23.0 fps | [`2c1895ac`](https://github.com/dfdx047/Fractal/commit/2c1895ac) |
| Vertex work overlaps render target barriers | RG Cube, Mario Kart 8 | 24.3 → 30.2 fps | [`7b36890f`](https://github.com/dfdx047/Fractal/commit/7b36890f) |
| Framebuffer compression kept | RP5, Super Mario 3D World | 57.0 → 59.8 fps, 1 % low 30 → 44 | [`017be0ef`](https://github.com/dfdx047/Fractal/commit/017be0ef) |
| Recompiler: cycle counter in a register, code arena | RP5, Mario Kart 8 | 47.8 → 50.0-50.4 fps | [`23b10251`](https://github.com/dfdx047/Fractal/commit/23b10251) |
| Depth clear through a render pass | Mi A3, Shovel Knight | 34 → 52 fps | [`aabeea76`](https://github.com/dfdx047/Fractal/commit/aabeea76) |
| Per-game `GX2DrawDone` sync | RP5, Breath of the Wild | 9.4 → 11.1 fps | [`951eb1e7`](https://github.com/dfdx047/Fractal/commit/951eb1e7) |
| 1-2 pipeline compile threads | Mi A3, SM3DW, cold cache | 17.0 s → 7.5 s in frames over 100 ms | [`9893cf72`](https://github.com/dfdx047/Fractal/commit/9893cf72) |
| Precompiled pipelines match the driver cache | Watch Dogs | 38-68 s → 9 s freeze entering gameplay | [`69ac8320`](https://github.com/dfdx047/Fractal/commit/69ac8320) |
| Occlusion query results while the GPU is idle | RP5 and POCO, Splinter Cell: Blacklist | 20 → 30 fps (game cap) | [`1557a268`](https://github.com/dfdx047/Fractal/commit/1557a268) |
| Recompiler: overflow forms, far branches, windows for huge functions | Mi A3, Teslagrad | 1 % low 3.7 → 10.3 fps; host code 118 → 39 MB | [`66f59e89`](https://github.com/dfdx047/Fractal/commit/66f59e89) |
| Resolution hint (640x360) | Mi A3: Kirby and the Rainbow Curse / NSMBU / Captain Toad | 12 → 27 / 20 → 34 / 16 → 30 fps | [`08c67298`](https://github.com/dfdx047/Fractal/commit/08c67298) |
| System driver instead of Mesa in Rayman Legends | RP5 (Turnip) / POCO (PanVK) | 17 → 59 fps / 18 → 29.6 fps | [`1f6476e3`](https://github.com/dfdx047/Fractal/commit/1f6476e3) |
| AES-128 with the Crypto Extension | POCO, 32 KiB sector | 32 MB/s → 4.1 GB/s | [`852801bb`](https://github.com/dfdx047/Fractal/commit/852801bb) |
| CRC32 instructions | POCO | 1.2 → 13.6 GB/s | [`64efc3e5`](https://github.com/dfdx047/Fractal/commit/64efc3e5) |
| NEON index buffer maximum | POCO, 30000 indices | u16 51.5 → 4.3 µs, u32 35.6 → 8.4 µs | [`bd6af3dc`](https://github.com/dfdx047/Fractal/commit/bd6af3dc) |
| BCn decoding through a palette, with NEON | POCO, 1024x1024 | BC1 4.0 → 1.5 ms, BC3 5.7 → 2.8, BC4 3.8 → 1.3, BC5 7.0 → 3.2 | [`c4e6b0aa`](https://github.com/dfdx047/Fractal/commit/c4e6b0aa) |

---

## In progress

- **CPU profiling on Mali:** on the POCO X6 Pro (Bayonetta 2, Breath of the Wild, Xenoblade X, AC III) the main
  PowerPC core's idle loop spends about half its time reading the clock and taking the timer and audio locks. Making
  that wait lock free gave no fps on the RP5, the POCO or the Mi A3, so it was left out; next is the render thread.
- **Need for Speed: Most Wanted U on the POCO:** one crash in game with an earlier build, not seen again in 7 runs.
- **The Binding of Isaac: Rebirth (USA):** runs (sound, no crash, 58.7 fps on the RP5) with a black picture on every
  phone; the GamePad view and the logic op fix don't change it.
- **FIFA 13 on the RP5:** a crash in the game's audio thread (reads address 0) in about 3 of 10 runs, with every driver.
- **Splinter Cell: Blacklist:** in missions only the sky and the ground are drawn (the Cemu compatibility wiki lists the
  same problem). Ruled out so far: occlusion queries, the hi-Z pyramid and the memory mapping queries.
- **Assassin's Creed III on the POCO:** nearby terrain and rocks are flat dark blue; in multi-core the game does not send
  the terrain draw, single-core draws it.
- **Watch Dogs on Turnip:** a GPU page fault in a streamout write can hang the GPU after about 25 minutes. The system
  driver is not affected.
- **Watch Dogs overexposed on Android:** from the Maurice room on, the auto exposure can lock too bright in about half
  of the sessions, on Turnip and on the system driver; the PC build is not affected. Ruled out so far: NaN/Inf in pixel
  shaders, skipped draws during async pipeline compilation and texture readback.
- **Watch Dogs on Mali** (RG Cube): the render thread gets stuck compiling the very large streamout shaders; blocky menu
  and black intro.
- **Assassin's Creed IV, jungle blackout:** after the `fsel` fix the world still turns black in about 4 out of 10 passes
  over the bridge (PC and Android). Ruled out so far: the frame time function in the interpreter, 25-bit rounding of
  `frC` in multiplications and the linear reinterpretation of the occlusion map. Next step: capture the occlusion map
  frame by frame around the blackout.
- **Assassin's Creed IV, cutscenes:** comparing with the PC build; the long swords hang straight down the same way on PC
  and on the RP5, not yet confirmed as a bug.
- **Odin 2 Mini on Turnip:** Tekken Tag Tournament 2 is black at the opening with MrPurple T30; Batman and Watch Dogs
  hung in some of the runs. StevenMXZ's Turnip 27.0 R2 crashes inside the driver compiling Ninja Gaiden 3 pipelines.
- **Ninja Gaiden 3 on Turnip:** dark floating heads and a blown-out sky remain after the depth of field fix.
- **Need for Speed: Most Wanted U:** finding the root cause of the white foliage (the community pack hides it).
- **Xenoblade Chronicles X on the Qualcomm system driver:** characters drawn as white silhouettes (driver bug); use
  Turnip for now.
- **Mali in general:** the POCO X6 Pro (Mali-G615) is now a main test device; the RG Cube, kept in its economy CPU
  mode, stays the reference for weaker Mali devices.

## Planned

- Fixed-scene measurements on the Odin 3 and the AYN Thor.
- Testing the Samsung Xclipse custom drivers on an Exynos device.
- Frame generation on the GamePad screen too, and a quick button for it in the in-game menu.
- Wider compatibility testing on the main Wii U games and a public compatibility list.
- An in-game "mark a problem" button that saves a screenshot and a marker in the log for bug reports.
- Streamout on Mali drivers that have neither vertex pipeline stores nor transform feedback (for example through a
  compute pass).
- GPU features found missing while testing: compute draw calls, alpha-to-mask (`GX2SetAlphaToMaskReg`),
  `GX2ExpandDepthBuffer`, and `SQ_VTX_SEMANTIC_CLEAR`, which is never applied.
- More CPU-side performance: Breath of the Wild and Mario Kart 8 on the RP5 are limited by the main PowerPC core and the
  render thread, not by the GPU.
- Keep following official Cemu `main`.
