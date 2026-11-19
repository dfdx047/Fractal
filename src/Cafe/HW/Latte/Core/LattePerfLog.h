/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2026 DFD */

#pragma once

// Performance log: one folder per game session under <user data>/perf/ with
//   info.txt     device, GPU, driver, build and the settings that affect speed
//   seconds.csv  one row per second: fps, frametime (avg, 99th percentile = 1% low, max),
//                shaders and pipelines compiled and the time spent on them, CPU and RAM
//   threads.csv  CPU use of each thread, one row per thread per second
//   frames.csv   every frame: time since the session started and frametime
//   events.txt   problems found while playing, each kind once with the time it first happened and the count at
//                the end (failed pipelines and shaders, freezes, workarounds that kicked in): for bug reports
// Files are flushed every second, so a killed app keeps everything up to the last second.
// Always on for every game session ("light": nothing that costs measurable time). The "PerformanceLog" setting
// turns on the full log: GPU timestamps, the recompiler map (jitmap.txt) and buffer upload diagnostics.
// Only the newest sessions are kept (perf/ holds at most kMaxSessions session folders).
namespace PerfLog
{
	void SessionStart();
	void SessionEnd();
	bool IsActive();
	bool IsFull(); // the "PerformanceLog" setting: also the diagnostics that cost some time
	// a problem worth a bug report: logged to log.txt and events.txt the first time "key" is seen, counted afterwards
	void Event(std::string_view key, std::string_view text);
	// writes a file into the session folder (e.g. the GLSL of a shader that failed to compile); nothing without a session
	void SessionFile(std::string_view name, std::string_view content);

	// once per emulated frame
	void FramePresented();
	// a shader module was created (fromCache: SPIR-V came from the precompiled cache)
	void ShaderCompiled(uint64 durationUs, bool fromCache, bool failed);
	// vkCreateGraphicsPipelines returned (renderThread: the game waited for it)
	void PipelineCompiled(uint64 durationUs, bool renderThread, bool failed);

	void SetGpuInfo(std::string_view gpu, std::string_view driver);
	// a command buffer ran on the GPU from startNs to endNs (GPU clock); overlapping work is counted once
	void GpuWork(uint64 startNs, uint64 endNs);
	// performance HUD: asks for command buffer timestamps (cheap: two per command buffer) to show the GPU load on any
	// device, also where the system hides the GPU load files. GpuLoadPercent: busy share of the time since the last
	// call, -1 when there are no timestamps (not wanted yet, or the driver has none)
	void SetGpuLoadWanted(bool wanted);
	bool GpuLoadWanted();
	int GpuLoadPercent();
	// render thread: one per render pass begun / per draw submitted to the host API
	// experiment knobs: "key=value" lines in <user data>/perf/tuning.txt, read once per process (default if absent)
	sint64 Tunable(std::string_view key, sint64 defaultValue);

	void RenderPassBegin();
	// why the renderer ended a render pass (each end costs attachment stores and loads on tiled GPUs)
	enum class RenderPassEnd : uint8 { Other, Fbo, BufferUpload, BufferCopy, TextureUpload, Clear, Copy, Readback, Query, Submit, SelfDependency, Count };
	void RenderPassEnded(RenderPassEnd reason);
	void RenderPassRestarted(RenderPassEnd previousEnd); // a render pass began on the same framebuffer the previous one ended on
	void PipelineBarrier(); // render target barrier between draws
	void SelfDependencyKept();
	void SelfDependencyAspects(bool color, bool depth); // diagnostics: which attachment aspects self dependencies sample
	void SelfDependencyReason(int reason); // diagnostics: why the render pass could not be kept (0..3)
	void ImageCopyViaBuffer(); // image copy between formats that both have framebuffer compression went through a buffer
	void TextureUploadInPrologue(); // texture upload recorded ahead of the command buffer instead of ending the render pass
	void DrawDone(); // the guest called GX2DrawDone
	enum class GuestWait { Timestamp, Flip, Vsync, CpFence, Count }; // Timestamp: GX2WaitTimeStamp, also reached from GX2DrawDone; CpFence: the command processor waiting in WAIT_REG_MEM for a value the CPU writes
	void GuestWaited(GuestWait what, uint64 ns); // host time a guest thread spent blocked in a GX2 wait
	void DepthAttachmentUse(bool tested, bool written); // at the end of a render pass with a depth attachment: did a draw test or write it
	void DepthStoreSkipped(bool restarted); // a render pass began without the depth/stencil store, or had to restart with it
	void BufferCacheRename(uint32 bytes); // a buffer cache node moved to new GPU memory instead of ending the render pass
	void BufferCacheRenameSkipped(int reason); // 0 streamout data, 1 node too large, 2 no heap space, 3 new allocation still in use
	void BufferUploadFallback(int reason); // diagnostics: why a buffer upload could not be written directly (0 blocked, 1 used by the command buffer being recorded, 2 used by a submitted one)
	void SelfDependencyOtherSubresource(); // sampled another mip/slice of an attachment: no longer treated as a self dependency // a draw sampled an attachment nobody wrote in this render pass: the pass was kept
	void GpuWait(uint64 ns); // the GPU thread blocked waiting for a command buffer to finish
	enum class GpuWaitReason : uint8 { Other, Present, Readback, Query, Flush, CommandBufferPool, UniformRing, LinearCopy, Count };
	// tags the GPU waits of the current thread until destroyed (diagnostics: which sync point blocks)
	struct GpuWaitScope
	{
		explicit GpuWaitScope(GpuWaitReason reason);
		~GpuWaitScope();
		GpuWaitReason m_prev;
	};
	// render thread breadcrumb for hang dumps: the blocking step the render thread is in (a string literal) and since when,
	// e.g. a driver shader compiler or a fence that never returns. Set around host waits and synchronous pipeline compiles
	struct RenderStage
	{
		explicit RenderStage(const char* what, uint64 arg = 0);
		~RenderStage();
		bool m_active;
		const char* m_prev;
		uint64 m_prevArg;
		sint64 m_prevSince;
	};
	std::string DescribeRenderStage();
	// called once by the Latte thread: only that thread updates the render stage (pipeline cache loading threads also
	// compile with isRenderThread set, and their nested scopes would leave a stale stage behind)
	void MarkRenderThread();
	void TextureReadbackScheduled();
	// GPU thread: a texture slice was decoded on the CPU (LatteTextureLoader), ns spent in the decoder (split: on the
	// worker pool too), texels of the slice, bytes the decoder wrote. seconds.csv: tex_decodes, tex_decode_ms,
	// tex_decode_max_ms (longest single decode: a stutter), tex_decode_ktexels, tex_decode_kb, tex_decodes_split
	void TextureDecoded(uint64 ns, uint64 texels, uint64 bytes, bool split);
	// GPU thread: time spent recording the upload of a decoded slice (staging copy included). seconds.csv: tex_upload_ms
	void TextureUploadRecorded(uint64 ns);
	void BufferUploadDirect(); // buffer cache upload written straight into GPU memory (no copy, no render pass end)
	void DrawCall();

	// PPC recompiler, for profiling (simpleperf samples in JIT code are mapped back to PPC functions):
	//   jitmap.txt   "<host address> <host size> <ppc address> <ppc size>" per recompiled function
	//   jitfail.txt  "<ppc address> <ppc size> <stage>" per function left to the interpreter
	void JitFunction(uint32 ppcAddress, uint32 ppcSize, const void* hostCode, uint32 hostSize);
	void JitFailure(uint32 ppcAddress, uint32 ppcSize, const char* stage);
	// generated code that is not a PPC function (enter/leave thunks): "<host address> <host size> 0 <name>"
	void JitGlue(const void* hostCode, uint32 hostSize, const char* name);

	// Input script for reproducible benchmark scenes: <user data>/perf/input_<titleid>.txt (or
	// input.txt), one press per line: "<seconds since the game started> <button> [hold ms, default 150]"; a hold written
	// as "<n>f" lasts n presented frames from the first frame its time has come, so cursors and menus that move a step
	// per game frame travel the same distance on a device that renders the menu at 24 fps.
	// Buttons: A B X Y L R ZL ZR PLUS MINUS UP DOWN LEFT RIGHT, and LUP LDOWN LLEFT LRIGHT RUP RDOWN
	// RLEFT RRIGHT for the sticks. Only read while the performance log is on; the GamePad (VPAD)
	// presses the buttons that are held at the current time.
	// GamePad touch screen: "<seconds> TOUCH <x> <y> [hold]" touches one point, "<seconds> DRAG <x1> <y1> <x2> <y2> [hold]"
	// moves the touch from the first point to the second over the hold (stylus games: Kirby and the Rainbow Curse).
	// Coordinates go from 0 to 1 over the GamePad screen, from its top left corner
	// "<seconds> SYNC <draws>" lines: the presses after it wait until a frame with at least <draws> draws was presented
	// (after the previous sync) and are timed from that frame, so a recorded walk survives load times that vary
	struct ScriptedPress
	{
		double startS;
		double endS;
		std::string button;
		int segment = 0; // number of SYNC lines before this press
		uint32 holdFrames = 0; // "<n>f" holds; 0 = held until endS
		float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f; // TOUCH and DRAG
	};
	// position (0..1) of the scripted GamePad touch held right now, false when nothing touches the screen
	bool ScriptedTouch(float& x, float& y);
	const std::vector<ScriptedPress>& InputScript();
	// whether a press of InputScript() is held right now
	bool PressHeld(const ScriptedPress& press);
	// "<seconds> AXIS <lx> <ly> <rx> <ry>" lines: analog stick positions from that time on (recorded with input_record=1)
	struct ScriptedAxes
	{
		double t;
		int segment;
		float lx, ly, rx, ry;
	};
	const std::vector<ScriptedAxes>& InputAxes();
	// current time for the presses of a segment (same scale as startS), or a negative value while its sync is pending
	double ScriptSeconds(int segment);
	double SessionSeconds();
}

// measures the scope it lives in
class PerfLogTimer
{
public:
	PerfLogTimer() : m_start(std::chrono::steady_clock::now()) {}
	uint64 ElapsedUs() const
	{
		return (uint64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - m_start).count();
	}
private:
	std::chrono::steady_clock::time_point m_start;
};
