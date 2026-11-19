/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2026 DFD */

#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Core/LattePerfLog.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/OS/RPL/rpl_symbol_storage.h"
#include "Common/ExceptionHandler/ExceptionHandler.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "Common/version.h"
#include "util/helpers/helpers.h"
#include "util/helpers/PerformanceHint.h"
#include "Cafe/OS/libs/gx2/GX2_Command.h"

#include <condition_variable>
#include <ctime>
#include <fstream>
#include <unordered_map>

#include <dirent.h>
#include <unistd.h>
#include <sys/system_properties.h>

void PPCRecompilerAArch64Gen_reportInterfaceFunctionsToPerfLog(); // BackendAArch64.cpp

std::string LatteCP_DescribeWait(); // LatteCommandProcessor.cpp

#ifdef FRACTAL_PGO_GENERATE
extern "C" int __llvm_profile_write_file(void);
extern "C" void __llvm_profile_set_filename(const char*);
#endif

namespace PerfLog
{
	using Clock = std::chrono::steady_clock;

	struct FrameSample
	{
		float tMs;  // since session start
		float ftMs; // frametime
	};

	// counters reset every second by the writer thread
	struct Counters
	{
		std::atomic<uint32> shaders{};
		std::atomic<uint32> shadersFromCache{};
		std::atomic<uint32> shadersFailed{};
		std::atomic<uint64> shaderUs{};
		std::atomic<uint32> pipelines{};
		std::atomic<uint32> pipelinesSync{};
		std::atomic<uint32> pipelinesFailed{};
		std::atomic<uint64> pipelineUs{};
		std::atomic<uint64> pipelineSyncUs{};
		std::atomic<uint64> gpuBusyNs{};
		std::atomic<uint64> renderPasses{};
		std::atomic<uint64> draws{};
		std::atomic<uint64> renderPassEnds[(size_t)RenderPassEnd::Count]{};
		std::atomic<uint64> bufferUploadsDirect{};
		std::atomic<uint64> pipelineBarriers{};
		std::atomic<uint64> gpuWaitNs{};
		std::atomic<uint64> gpuWaitNsByReason[(size_t)GpuWaitReason::Count]{};
		std::atomic<uint64> selfDependencyKept{};
		std::atomic<uint64> selfDependencyOtherSubresource{};
		std::atomic<uint64> selfDependencyColor{}, selfDependencyDepth{};
		std::atomic<uint64> selfDependencyReasons[4]{};
		std::atomic<uint64> uploadFallbackReasons[3]{};
		std::atomic<uint64> bufferRenames{};
		std::atomic<uint64> bufferRenameBytes{};
		std::atomic<uint64> bufferRenameSkipped[4]{};
		std::atomic<uint64> imageCopiesViaBuffer{};
		std::atomic<uint64> uploadsInPrologue{};
		std::atomic<uint64> drawDoneCalls{};
		std::atomic<uint64> guestWaitNs[(size_t)GuestWait::Count]{};
		std::atomic<uint64> depthPasses{}, depthPassesUnused{}, depthPassesReadOnly{}, depthStoreSkipped{}, depthStoreRestarts{};
		std::atomic<uint64> renderPassRestarts[(size_t)RenderPassEnd::Count]{};
		std::atomic<uint64> textureReadbacks{};
		std::atomic<uint64> texDecodes{}, texDecodeNs{}, texDecodeMaxNs{}, texDecodeTexels{}, texDecodeBytes{}, texDecodesSplit{}, texUploadNs{};
	};
	static uint64 s_gpuLastEndNs = 0; // only touched by the render thread

	static std::atomic<bool> s_active{false};
	static std::atomic<bool> s_full{false};
	static constexpr size_t kMaxSessions = 20;

	static std::mutex s_eventMutex;
	static std::ofstream s_events;
	static std::unordered_map<std::string, uint64> s_eventCounts;
	static std::vector<std::string> s_eventOrder;
	static fs::path s_sessionDir; // set before s_active
	static Counters s_counters;
	static Clock::time_point s_sessionStart;
	static Clock::time_point s_lastFrame;
	static bool s_hasLastFrame = false;

	static std::mutex s_frameMutex;
	static std::vector<FrameSample> s_frames;

	static std::mutex s_gpuMutex;
	static std::string s_gpuName, s_gpuDriver;

	static std::thread s_writer;
	static std::mutex s_stopMutex;
	static std::condition_variable s_stopCv;
	static bool s_stop = false;

	static std::vector<ScriptedPress> s_inputScript; // set before s_active, read-only afterwards
	static std::unique_ptr<std::atomic<uint64>[]> s_pressFirstFrame; // per press of s_inputScript: 1 + present count when a frame-counted hold began, 0 = not yet
	static std::vector<ScriptedAxes> s_inputAxes; // same
	// SYNC lines of the input script: recorded time, draw threshold, and the session time the sync happened (-1 pending)
	struct ScriptSync { double recordedS; uint32 minDraws; };
	static std::vector<ScriptSync> s_scriptSyncs;
	static std::array<std::atomic<double>, 16> s_scriptSyncAt;
	static std::atomic<uint32> s_frameDraws{};
	static std::atomic<uint64> s_presentCount{};
	static std::array<std::atomic<uint64>, 16> s_scriptSyncFrame;

	static std::mutex s_jitMutex;
	static std::ofstream s_jitMap, s_jitFail, s_jitCode;

	bool IsActive()
	{
		return s_active.load(std::memory_order_relaxed);
	}

	bool IsFull()
	{
		return s_full.load(std::memory_order_relaxed);
	}


	void Event(std::string_view key, std::string_view text)
	{
		if (!IsActive())
			return;
		std::scoped_lock lock(s_eventMutex);
		auto [it, isNew] = s_eventCounts.try_emplace(std::string(key), 0);
		it->second++;
		if (!isNew)
			return;
		s_eventOrder.emplace_back(key);
		cemuLog_log(LogType::Force, "Event [{}]: {}", key, text);
		if (s_events.is_open())
		{
			s_events << fmt::format("{:.1f} [{}] {}\n", SessionSeconds(), key, text);
			s_events.flush();
		}
	}

	void SessionFile(std::string_view name, std::string_view content)
	{
		if (!IsActive())
			return;
		std::ofstream f(s_sessionDir / fs::path(std::string(name)), std::ios::trunc | std::ios::binary);
		f.write(content.data(), (std::streamsize)content.size());
	}

	// oldest session folders go first; files in perf/ (tuning.txt, input scripts) are never touched
	static void PruneSessions(const fs::path& perfDir)
	{
		std::error_code ec;
		std::vector<fs::path> sessions;
		for (const auto& entry : fs::directory_iterator(perfDir, ec))
			if (entry.is_directory(ec) && entry.path().filename().string().size() > 16 && std::isdigit((unsigned char)entry.path().filename().string()[0]))
				sessions.emplace_back(entry.path());
		if (sessions.size() < kMaxSessions)
			return;
		std::sort(sessions.begin(), sessions.end()); // names start with the date and time
		for (size_t i = 0; i + kMaxSessions <= sessions.size(); i++)
			fs::remove_all(sessions[i], ec);
	}

	double ScriptSeconds(int segment)
	{
		const double now = SessionSeconds();
		if (segment <= 0)
			return now;
		const double at = s_scriptSyncAt[segment - 1].load(std::memory_order_relaxed);
		if (at < 0.0)
			return -1.0;
		// script_frames=1: time after a sync counts presented frames (1/60 s each) instead of the clock, so the same game
		// frame gets the same input on every replay
		static const bool s_frames = Tunable("script_frames", 0) != 0;
		if (s_frames)
			return (double)(s_presentCount.load(std::memory_order_relaxed) - s_scriptSyncFrame[segment - 1].load(std::memory_order_relaxed)) / 60.0 + s_scriptSyncs[segment - 1].recordedS;
		return now - at + s_scriptSyncs[segment - 1].recordedS;
	}

	bool PressHeld(const ScriptedPress& press)
	{
		const double t = ScriptSeconds(press.segment);
		if (t < 0.0 || t < press.startS)
			return false;
		if (press.holdFrames == 0)
			return t < press.endS;
		const size_t index = (size_t)(&press - s_inputScript.data());
		if (index >= s_inputScript.size())
			return false;
		const uint64 now = s_presentCount.load(std::memory_order_relaxed) + 1;
		uint64 first = 0;
		if (!s_pressFirstFrame[index].compare_exchange_strong(first, now, std::memory_order_relaxed))
			return now - first < press.holdFrames;
		return true;
	}

	bool ScriptedTouch(float& x, float& y)
	{
		for (const auto& press : InputScript())
		{
			if ((press.button != "TOUCH" && press.button != "DRAG") || !PressHeld(press))
				continue;
			// DRAG: along the line over the hold time (frame holds jump to the end point)
			float f = 1.0f;
			if (press.holdFrames == 0 && press.endS > press.startS)
				f = (float)std::clamp((ScriptSeconds(press.segment) - press.startS) / (press.endS - press.startS), 0.0, 1.0);
			x = std::clamp(press.x1 + (press.x2 - press.x1) * f, 0.0f, 1.0f);
			y = std::clamp(press.y1 + (press.y2 - press.y1) * f, 0.0f, 1.0f);
			return true;
		}
		return false;
	}

	const std::vector<ScriptedAxes>& InputAxes()
	{
		static const std::vector<ScriptedAxes> empty;
		return IsActive() ? s_inputAxes : empty;
	}

	const std::vector<ScriptedPress>& InputScript()
	{
		static const std::vector<ScriptedPress> empty;
		return IsActive() ? s_inputScript : empty;
	}

	double SessionSeconds()
	{
		return std::chrono::duration<double>(Clock::now() - s_sessionStart).count();
	}

	static std::vector<ScriptedPress> LoadInputScript(uint64 titleId)
	{
		std::vector<ScriptedPress> script;
		fs::path path = ActiveSettings::GetUserDataPath("perf/input_{:016x}.txt", titleId);
		std::error_code ec;
		if (!fs::exists(path, ec))
			path = ActiveSettings::GetUserDataPath("perf/input.txt");
		std::ifstream f(path);
		if (!f)
			return script;
		std::string line;
		while (std::getline(f, line))
		{
			if (const size_t hash = line.find('#'); hash != std::string::npos)
				line.resize(hash);
			std::istringstream ss(line);
			double t;
			std::string button;
			double holdMs = 150.0;
			uint32 holdFrames = 0;
			if (!(ss >> t >> button))
				continue;
			if (button == "AXIS")
			{
				ScriptedAxes a{t, (int)s_scriptSyncs.size()};
				if (ss >> a.lx >> a.ly >> a.rx >> a.ry)
					s_inputAxes.push_back(a);
				continue;
			}
			for (char& ch : button)
				ch = (char)toupper((unsigned char)ch);
			float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
			if (button == "TOUCH")
			{
				if (!(ss >> x1 >> y1))
					continue;
				x2 = x1;
				y2 = y1;
			}
			else if (button == "DRAG")
			{
				if (!(ss >> x1 >> y1 >> x2 >> y2))
					continue;
			}
			if (std::string hold; ss >> hold)
			{
				if (!hold.empty() && (hold.back() == 'f' || hold.back() == 'F'))
					holdFrames = (uint32)std::max(1, atoi(hold.c_str()));
				else
					holdMs = atof(hold.c_str());
			}
			if (button == "SYNC")
			{
				if (s_scriptSyncs.size() < s_scriptSyncAt.size())
					s_scriptSyncs.push_back({t, (uint32)holdMs});
				continue;
			}
			script.push_back({t, t + holdMs / 1000.0, button, (int)s_scriptSyncs.size(), holdFrames, x1, y1, x2, y2});
		}
		s_pressFirstFrame = std::make_unique<std::atomic<uint64>[]>(std::max<size_t>(script.size(), 1));
		cemuLog_log(LogType::Force, "Performance log: input script {} ({} presses)", _pathToUtf8(path), script.size());
		return script;
	}

	void SetGpuInfo(std::string_view gpu, std::string_view driver)
	{
		std::scoped_lock lock(s_gpuMutex);
		s_gpuName = gpu;
		s_gpuDriver = driver;
	}

	void FramePresented()
	{
		if (!IsActive())
			return;
		const auto now = Clock::now();
		const uint32 frameDraws = s_frameDraws.exchange(0, std::memory_order_relaxed);
		const uint64 presentIndex = s_presentCount.fetch_add(1, std::memory_order_relaxed) + 1;
		// shot_every_s=N: a screenshot every N seconds as shot_<seconds>.ppm in the session folder, to see what a replayed
		// input script did without a window on the desktop
		static const sint64 s_shotEvery = Tunable("shot_every_s", 0);
		if (s_shotEvery > 0 && g_renderer)
		{
			static double s_nextShot = 0.0;
			const double t = std::chrono::duration<double>(now - s_sessionStart).count();
			if (t >= s_nextShot)
			{
				s_nextShot = t + (double)s_shotEvery;
				const fs::path file = s_sessionDir / fmt::format("shot_{:04.0f}.ppm", t);
				g_renderer->RequestScreenshot([file](const std::vector<uint8>& rgb, int w, int h, bool mainWindow) -> std::optional<std::string> {
					if (!mainWindow)
						return std::nullopt;
					std::ofstream f(file, std::ios::binary | std::ios::trunc);
					const std::string header = fmt::format("P6\n{} {}\n255\n", w, h);
					f.write(header.data(), (std::streamsize)header.size());
					f.write((const char*)rgb.data(), (std::streamsize)std::min<size_t>(rgb.size(), (size_t)w * h * 3));
					return std::nullopt;
				});
			}
		}
		// input script: the next pending SYNC happens on the first frame with enough draws
		for (size_t i = 0; i < s_scriptSyncs.size(); i++)
		{
			if (s_scriptSyncAt[i].load(std::memory_order_relaxed) >= 0.0)
				continue;
			if (frameDraws >= s_scriptSyncs[i].minDraws)
			{
				const double at = std::chrono::duration<double>(now - s_sessionStart).count();
				s_scriptSyncFrame[i] = presentIndex;
				s_scriptSyncAt[i] = at;
				cemuLog_log(LogType::Force, "Performance log: input script sync {} at {:.2f} s ({} draws, recorded at {:.2f} s)", i + 1, at, frameDraws, s_scriptSyncs[i].recordedS);
			}
			break;
		}
		if (s_hasLastFrame)
		{
			FrameSample sample;
			sample.tMs = std::chrono::duration<float, std::milli>(now - s_sessionStart).count();
			sample.ftMs = std::chrono::duration<float, std::milli>(now - s_lastFrame).count();
			std::scoped_lock lock(s_frameMutex);
			s_frames.push_back(sample);
		}
		s_lastFrame = now;
		s_hasLastFrame = true;
	}

	void ShaderCompiled(uint64 durationUs, bool fromCache, bool failed)
	{
		if (!IsActive())
			return;
		s_counters.shaders++;
		if (fromCache)
			s_counters.shadersFromCache++;
		if (failed)
			s_counters.shadersFailed++;
		s_counters.shaderUs += durationUs;
	}

	void PipelineCompiled(uint64 durationUs, bool renderThread, bool failed)
	{
		if (!IsActive())
			return;
		s_counters.pipelines++;
		s_counters.pipelineUs += durationUs;
		if (renderThread)
		{
			s_counters.pipelinesSync++;
			s_counters.pipelineSyncUs += durationUs;
		}
		if (failed)
			s_counters.pipelinesFailed++;
	}

	// ---------------- system readings (Linux/Android /proc and /sys) ----------------

	static std::string ReadSmallFile(const char* path)
	{
		std::ifstream f(path);
		if (!f)
			return {};
		std::string s;
		std::getline(f, s);
		return s;
	}

	struct ThreadTicks
	{
		std::string name;
		uint64 ticks;
	};

	static std::unordered_map<int, ThreadTicks> ReadThreadTicks()
	{
		std::unordered_map<int, ThreadTicks> result;
		DIR* dir = opendir("/proc/self/task");
		if (!dir)
			return result;
		while (dirent* entry = readdir(dir))
		{
			if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
				continue;
			const int tid = atoi(entry->d_name);
			std::string stat = ReadSmallFile(fmt::format("/proc/self/task/{}/stat", tid).c_str());
			// "tid (comm) state ..." -- comm can hold spaces and parentheses, so cut at the last ')'
			const size_t open = stat.find('(');
			const size_t close = stat.rfind(')');
			if (open == std::string::npos || close == std::string::npos || close < open)
				continue;
			ThreadTicks t;
			t.name = stat.substr(open + 1, close - open - 1);
			// after ") ": field 3 (state) ... utime is field 14, stime field 15
			std::istringstream rest(stat.substr(close + 2));
			std::string field;
			uint64 utime = 0, stime = 0;
			for (int i = 3; i <= 15 && rest >> field; i++)
			{
				if (i == 14)
					utime = std::strtoull(field.c_str(), nullptr, 10);
				else if (i == 15)
					stime = std::strtoull(field.c_str(), nullptr, 10);
			}
			t.ticks = utime + stime;
			result.emplace(tid, std::move(t));
		}
		closedir(dir);
		return result;
	}

	static uint64 ReadRssMb()
	{
		std::string statm = ReadSmallFile("/proc/self/statm");
		std::istringstream ss(statm);
		uint64 size = 0, resident = 0;
		ss >> size >> resident;
		return resident * (uint64)sysconf(_SC_PAGESIZE) / (1024 * 1024);
	}

	static int CpuCount()
	{
		return (int)sysconf(_SC_NPROCESSORS_CONF);
	}

	// current clock of each CPU in MHz; empty when offline or not readable
	static std::string ReadCpuFreqs(int cpuCount)
	{
		std::string out;
		for (int i = 0; i < cpuCount; i++)
		{
			std::string v = ReadSmallFile(fmt::format("/sys/devices/system/cpu/cpu{}/cpufreq/scaling_cur_freq", i).c_str());
			if (i > 0)
				out += ',';
			if (!v.empty())
				out += fmt::format("{}", std::strtoull(v.c_str(), nullptr, 10) / 1000);
		}
		return out;
	}

	static std::string AndroidProp(const char* name)
	{
		char value[PROP_VALUE_MAX] = {};
		__system_property_get(name, value);
		return value;
	}

	static std::string CsvText(std::string s)
	{
		for (char& c : s)
			if (c == ',' || c == '\n' || c == '\r' || c == '"')
				c = ' ';
		return s;
	}

	// ---------------- writer ----------------

	static void WriteInfo(const fs::path& dir)
	{
		std::ofstream f(dir / "info.txt", std::ios::trunc);
		const auto& config = GetConfig();
		std::string gpu, driver;
		{
			std::scoped_lock lock(s_gpuMutex);
			gpu = s_gpuName;
			driver = s_gpuDriver;
		}
		f << "build: " << BUILD_VERSION_WITH_NAME_STRING << "\n";
		f << "title_id: " << fmt::format("{:016x}", CafeSystem::GetForegroundTitleId()) << "\n";
		f << "title: " << CafeSystem::GetForegroundTitleName() << "\n";
		f << "device: " << AndroidProp("ro.product.manufacturer") << " " << AndroidProp("ro.product.model") << "\n";
		f << "soc: " << AndroidProp("ro.soc.manufacturer") << " " << AndroidProp("ro.soc.model") << " (" << AndroidProp("ro.board.platform") << ")\n";
		f << "android: " << AndroidProp("ro.build.version.release") << "\n";
		f << "cpus: " << CpuCount() << "\n";
		f << "gpu: " << gpu << "\n";
		f << "driver: " << driver << "\n";
		f << "custom_driver: " << (config.custom_driver_path.GetValue().empty() ? "none" : config.custom_driver_path.GetValue()) << "\n";
		f << "async_compile: " << (config.async_compile.GetValue() ? 1 : 0) << "\n";
		f << "vsync: " << config.vsync.GetValue() << "\n";
		f << "accurate_barriers: " << (config.vk_accurate_barriers.GetValue() ? 1 : 0) << "\n";
		f << "cpu_mode: " << (int)config.cpu_mode.GetValue() << "\n";
		f << "upscale_filter: " << config.upscale_filter.GetValue() << "\n";
	}

	static void WriterThread(fs::path dir)
	{
		SetThreadName("PerfLog");
		std::ofstream seconds(dir / "seconds.csv", std::ios::trunc);
		std::ofstream threads(dir / "threads.csv", std::ios::trunc);
		std::ofstream frames(dir / "frames.csv", std::ios::trunc);
		const int cpuCount = CpuCount();
		std::string freqHeader;
		for (int i = 0; i < cpuCount; i++)
			freqHeader += fmt::format(",cpu{}_mhz", i);
		seconds << "t_s,frames,fps,ft_avg_ms,ft_p99_ms,low1_fps,ft_max_ms,"
				   "shaders,shaders_cached,shaders_failed,shader_ms,"
				   "pipelines,pipelines_sync,pipelines_failed,pipeline_ms,pipeline_sync_ms,gpu_busy_ms,render_passes,draws,"
				   "rpe_other,rpe_fbo,rpe_buffer_upload,rpe_buffer_copy,rpe_texture_upload,rpe_clear,rpe_copy,rpe_readback,rpe_query,rpe_submit,rpe_self_dependency,buffer_uploads_direct,barriers,gpu_wait_ms,readbacks,selfdep_kept,selfdep_other_subresource,selfdep_color,selfdep_depth,selfdep_why_drawwrites,selfdep_why_passwrote,selfdep_why_otherbarrier,selfdep_why_vertex,upload_fb_blocked,upload_fb_current,upload_fb_inflight,buffer_renames,buffer_rename_kb,rename_skip_streamout,rename_skip_size,rename_skip_heap,rename_skip_busy,copies_via_buffer,uploads_prologue,depth_passes,depth_unused,depth_readonly,depth_store_skipped,depth_store_restarts,drawdone,gw_other_ms,gw_present_ms,gw_readback_ms,gw_query_ms,gw_flush_ms,gw_pool_ms,gw_uniform_ms,gw_linearcopy_ms,"
				   "rs_other,rs_fbo,rs_buffer_upload,rs_buffer_copy,rs_texture_upload,rs_clear,rs_copy,rs_readback,rs_query,rs_submit,rs_self_dependency,wait_timestamp_ms,wait_flip_ms,wait_vsync_ms,wait_cp_fence_ms,"
				   "tex_decodes,tex_decode_ms,tex_decode_max_ms,tex_decode_ktexels,tex_decode_kb,tex_decodes_split,tex_upload_ms,"
				   "cpu_process_pct,rss_mb" << freqHeader << "\n";
		threads << "t_s,tid,name,cpu_pct\n";
		frames << "t_ms,ft_ms\n";

		const double ticksPerSecond = (double)sysconf(_SC_CLK_TCK);
		auto prevTicks = ReadThreadTicks();
		auto prevTime = Clock::now();
		bool infoWritten = false;
		sint32 zeroFrameSeconds = 0;
		bool hangDumped = false;
		bool framesSeen = false;
		std::vector<FrameSample> batch;
		std::vector<float> sorted;

		while (true)
		{
			{
				std::unique_lock lock(s_stopMutex);
				if (s_stopCv.wait_for(lock, std::chrono::seconds(1), [] { return s_stop; }))
					break;
			}
			const auto now = Clock::now();
			const double dt = std::chrono::duration<double>(now - prevTime).count();
			prevTime = now;
			const double tS = std::chrono::duration<double>(now - s_sessionStart).count();

			if (!infoWritten)
			{
				// the renderer reports the GPU after the session starts
				WriteInfo(dir);
				infoWritten = true;
			}

			batch.clear();
			{
				std::scoped_lock lock(s_frameMutex);
				batch.swap(s_frames);
			}
			sorted.clear();
			double ftSum = 0.0;
			float ftMax = 0.0f;
			for (const auto& s : batch)
			{
				frames << fmt::format("{:.1f},{:.3f}\n", s.tMs, s.ftMs);
				sorted.push_back(s.ftMs);
				ftSum += s.ftMs;
				ftMax = std::max(ftMax, s.ftMs);
			}
			std::sort(sorted.begin(), sorted.end());
			const size_t n = sorted.size();
			const double fps = dt > 0.0 ? (double)n / dt : 0.0;
			// after hang_dump_s (tuning, default 30) seconds in a row without a frame once frames were shown, write the emulated
			// threads and the stacks of the waiting ones to log.txt, once per session: what a freeze waits on, from any player's log
			if (const sint32 hangDumpS = (sint32)Tunable("hang_dump_s", 30); hangDumpS > 0)
			{
				framesSeen = framesSeen || n != 0;
				zeroFrameSeconds = n ? 0 : zeroFrameSeconds + 1;
				if (framesSeen && zeroFrameSeconds == hangDumpS && !hangDumped)
				{
					hangDumped = true;
					Event("freeze", fmt::format("no frame for {} s, emulated threads written to log.txt", hangDumpS));
					cemuLog_log(LogType::Force, "Perf log: no frame for {} s, GPU: {}; {}; GX2 timestamps submitted {} retired {}", hangDumpS,
						LatteCP_DescribeWait(), DescribeRenderStage(), GX2::GX2GetLastSubmittedTimeStamp(), GX2::GX2GetRetiredTimeStamp());
					cemuLog_log(LogType::Force, "Perf log: {}", GX2::GX2Command_DescribePending());
					// diagnostics (perf/tuning.txt hle_syms=1): the OS functions' stub addresses, to name the import thunks a
					// game's frozen loop calls (they jump to 0x00E0xxxx)
					if (Tunable("hle_syms", 0))
					{
						std::string line;
						auto& map = rplSymbolStorage_lockSymbolMap();
						std::vector<std::pair<uint32, std::string>> syms;
						for (auto& it : map)
							if (it.first < 0x01000000 && it.second)
								syms.emplace_back(it.first, fmt::format("{}.{}", (const char*)it.second->libName, (const char*)it.second->symbolName));
						rplSymbolStorage_unlockSymbolMap();
						std::sort(syms.begin(), syms.end());
						for (auto& [a, n] : syms)
							line += fmt::format(" {:x}={}", a, n);
						cemuLog_log(LogType::Force, "Perf log: HLE symbols:{}", line);
					}
					cemuLog_log(LogType::Force, "Perf log: emulated threads:");
					ExceptionHandler_LogThreadList(true);
				}
			}
			// diagnostics: perf/tuning.txt thread_dump_every_s=N writes the emulated threads and their stacks every N seconds,
			// to see where a thread spends a long loading screen
			if (static const sint32 dumpEveryS = (sint32)Tunable("thread_dump_every_s", 0); dumpEveryS > 0)
			{
				static sint32 s_dumpCounter = 0;
				if (++s_dumpCounter % dumpEveryS == 0)
				{
					cemuLog_log(LogType::Force, "Perf log: thread dump at {} s, GPU: {}; {}; GX2 timestamps submitted {} retired {}", s_dumpCounter,
						LatteCP_DescribeWait(), DescribeRenderStage(), GX2::GX2GetLastSubmittedTimeStamp(), GX2::GX2GetRetiredTimeStamp());
					cemuLog_log(LogType::Force, "Perf log: {}", GX2::GX2Command_DescribePending());
					// diagnostics (perf/tuning.txt hle_syms=1): the OS functions' stub addresses, to name the import thunks a
					// game's frozen loop calls (they jump to 0x00E0xxxx)
					if (Tunable("hle_syms", 0))
					{
						std::string line;
						auto& map = rplSymbolStorage_lockSymbolMap();
						std::vector<std::pair<uint32, std::string>> syms;
						for (auto& it : map)
							if (it.first < 0x01000000 && it.second)
								syms.emplace_back(it.first, fmt::format("{}.{}", (const char*)it.second->libName, (const char*)it.second->symbolName));
						rplSymbolStorage_unlockSymbolMap();
						std::sort(syms.begin(), syms.end());
						for (auto& [a, n] : syms)
							line += fmt::format(" {:x}={}", a, n);
						cemuLog_log(LogType::Force, "Perf log: HLE symbols:{}", line);
					}
					ExceptionHandler_LogThreadList(true);
				}
			}
			const double ftAvg = n ? ftSum / (double)n : 0.0;
			// 99th percentile frametime; "1% low" fps is its inverse
			const double ftP99 = n ? sorted[std::min(n - 1, (size_t)std::ceil(0.99 * (double)n) - 1)] : 0.0;
			const double low1 = ftP99 > 0.0 ? 1000.0 / ftP99 : 0.0;

			const uint32 shaders = s_counters.shaders.exchange(0);
			const uint32 shadersCached = s_counters.shadersFromCache.exchange(0);
			const uint32 shadersFailed = s_counters.shadersFailed.exchange(0);
			const uint64 shaderUs = s_counters.shaderUs.exchange(0);
			const uint32 pipelines = s_counters.pipelines.exchange(0);
			const uint32 pipelinesSync = s_counters.pipelinesSync.exchange(0);
			const uint32 pipelinesFailed = s_counters.pipelinesFailed.exchange(0);
			const uint64 pipelineUs = s_counters.pipelineUs.exchange(0);
			const uint64 pipelineSyncUs = s_counters.pipelineSyncUs.exchange(0);

			auto ticks = ReadThreadTicks();
			double processPct = 0.0;
			for (const auto& [tid, t] : ticks)
			{
				auto it = prevTicks.find(tid);
				const uint64 before = it != prevTicks.end() ? it->second.ticks : 0;
				if (t.ticks < before)
					continue;
				const double pct = (double)(t.ticks - before) / ticksPerSecond / dt * 100.0;
				processPct += pct;
				if (pct >= 0.5)
					threads << fmt::format("{:.0f},{},{},{:.1f}\n", tS, tid, CsvText(t.name), pct);
			}
			prevTicks = std::move(ticks);

			std::string freqs = ReadCpuFreqs(cpuCount);
			const uint64 gpuBusyNs = s_counters.gpuBusyNs.exchange(0);
			const uint64 renderPasses = s_counters.renderPasses.exchange(0);
			const uint64 draws = s_counters.draws.exchange(0);
			std::string passEnds;
			for (auto& c : s_counters.renderPassEnds)
				passEnds += fmt::format("{},", c.exchange(0));
			passEnds += fmt::format("{},", s_counters.bufferUploadsDirect.exchange(0));
			passEnds += fmt::format("{},", s_counters.pipelineBarriers.exchange(0));
			passEnds += fmt::format("{:.1f},", s_counters.gpuWaitNs.exchange(0) / 1e6);
			passEnds += fmt::format("{},", s_counters.textureReadbacks.exchange(0));
			passEnds += fmt::format("{},", s_counters.selfDependencyKept.exchange(0));
			passEnds += fmt::format("{},", s_counters.selfDependencyOtherSubresource.exchange(0));
			passEnds += fmt::format("{},{},", s_counters.selfDependencyColor.exchange(0), s_counters.selfDependencyDepth.exchange(0));
			for (auto& c : s_counters.selfDependencyReasons)
				passEnds += fmt::format("{},", c.exchange(0));
			for (auto& c : s_counters.uploadFallbackReasons)
				passEnds += fmt::format("{},", c.exchange(0));
			passEnds += fmt::format("{},{},", s_counters.bufferRenames.exchange(0), s_counters.bufferRenameBytes.exchange(0) / 1024);
			for (auto& c : s_counters.bufferRenameSkipped)
				passEnds += fmt::format("{},", c.exchange(0));
			passEnds += fmt::format("{},", s_counters.imageCopiesViaBuffer.exchange(0));
			passEnds += fmt::format("{},", s_counters.uploadsInPrologue.exchange(0));
			passEnds += fmt::format("{},{},{},", s_counters.depthPasses.exchange(0), s_counters.depthPassesUnused.exchange(0), s_counters.depthPassesReadOnly.exchange(0));
			passEnds += fmt::format("{},{},", s_counters.depthStoreSkipped.exchange(0), s_counters.depthStoreRestarts.exchange(0));
			passEnds += fmt::format("{},", s_counters.drawDoneCalls.exchange(0));
			for (auto& c : s_counters.gpuWaitNsByReason)
				passEnds += fmt::format("{:.1f},", c.exchange(0) / 1e6);
			for (auto& c : s_counters.renderPassRestarts)
				passEnds += fmt::format("{},", c.exchange(0));
			for (auto& c : s_counters.guestWaitNs)
				passEnds += fmt::format("{:.1f},", c.exchange(0) / 1e6);
			passEnds += fmt::format("{},{:.2f},{:.2f},{},{},{},{:.2f},", s_counters.texDecodes.exchange(0), s_counters.texDecodeNs.exchange(0) / 1e6,
									s_counters.texDecodeMaxNs.exchange(0) / 1e6, s_counters.texDecodeTexels.exchange(0) / 1000, s_counters.texDecodeBytes.exchange(0) / 1024,
									s_counters.texDecodesSplit.exchange(0), s_counters.texUploadNs.exchange(0) / 1e6);
			seconds << fmt::format("{:.0f},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{},{},{},{:.1f},{},{},{},{:.1f},{:.1f},{:.1f},{},{},{}{:.1f},{}{}{}\n",
								   tS, n, fps, ftAvg, ftP99, low1, ftMax,
								   shaders, shadersCached, shadersFailed, shaderUs / 1000.0,
								   pipelines, pipelinesSync, pipelinesFailed, pipelineUs / 1000.0, pipelineSyncUs / 1000.0, gpuBusyNs / 1e6, renderPasses, draws, passEnds,
								   processPct, ReadRssMb(), cpuCount ? "," : "", freqs);
			seconds.flush();
			threads.flush();
			frames.flush();
		}
	}

#ifdef FRACTAL_PGO_GENERATE
	// instrumented build (FRACTAL_PGO=generate): the counters go to <user data>/pgo/fractal-<pid>.profraw every 30 s and at
	// the end of a game, since Android usually kills the emulation process without running its exit handlers
	static void PgoWriteProfile()
	{
		static std::mutex s_pgoMutex;
		std::scoped_lock lock(s_pgoMutex);
		__llvm_profile_write_file();
	}

	static void PgoStart()
	{
		static std::once_flag s_once;
		std::call_once(s_once, []() {
			const fs::path dir = ActiveSettings::GetUserDataPath("pgo");
			std::error_code ec;
			fs::create_directories(dir, ec);
			static const std::string s_file = _pathToUtf8(dir / "fractal-%p.profraw");
			__llvm_profile_set_filename(s_file.c_str());
			std::thread([]() {
				while (true)
				{
					std::this_thread::sleep_for(std::chrono::seconds(30));
					PgoWriteProfile();
				}
			}).detach();
			cemuLog_log(LogType::Force, "PGO: instrumented build, profile written to {}", s_file);
		});
	}
#endif

	void SessionStart()
	{
#ifdef FRACTAL_PGO_GENERATE
		PgoStart();
#endif
		if (IsActive())
			return;
		s_full = GetConfig().perf_log.GetValue();
		const std::time_t now = std::time(nullptr);
		std::tm tm{};
		localtime_r(&now, &tm);
		char stamp[32];
		std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
		const fs::path dir = ActiveSettings::GetUserDataPath("perf") / fmt::format("{}_{:016x}", stamp, CafeSystem::GetForegroundTitleId());
		std::error_code ec;
		fs::create_directories(dir.parent_path(), ec);
		PruneSessions(dir.parent_path());
		fs::create_directories(dir, ec);
		if (ec)
		{
			cemuLog_log(LogType::Force, "Performance log: cannot create {}: {}", _pathToUtf8(dir), ec.message());
			return;
		}
		cemuLog_log(LogType::Force, "Performance log ({}): {}", s_full ? "full" : "light", _pathToUtf8(dir));
		s_sessionDir = dir;
		{
			std::scoped_lock lock(s_eventMutex);
			s_eventCounts.clear();
			s_eventOrder.clear();
			s_events.open(dir / "events.txt", std::ios::trunc);
		}
		s_counters.shaders = 0;
		s_counters.shadersFromCache = 0;
		s_counters.shadersFailed = 0;
		s_counters.shaderUs = 0;
		s_counters.pipelines = 0;
		s_counters.pipelinesSync = 0;
		s_counters.pipelinesFailed = 0;
		s_counters.pipelineUs = 0;
		s_counters.pipelineSyncUs = 0;
		s_counters.gpuBusyNs = 0;
		s_counters.renderPasses = 0;
		s_counters.draws = 0;
		for (auto& c : s_counters.renderPassEnds)
			c = 0;
		s_counters.bufferUploadsDirect = 0;
		s_counters.pipelineBarriers = 0;
		s_counters.gpuWaitNs = 0;
		s_counters.textureReadbacks = 0;
		for (auto* c : {&s_counters.texDecodes, &s_counters.texDecodeNs, &s_counters.texDecodeMaxNs, &s_counters.texDecodeTexels, &s_counters.texDecodeBytes, &s_counters.texDecodesSplit, &s_counters.texUploadNs})
			*c = 0;
		s_counters.selfDependencyKept = 0;
		s_counters.selfDependencyOtherSubresource = 0;
		s_counters.selfDependencyColor = 0;
		s_counters.selfDependencyDepth = 0;
		for (auto& c : s_counters.selfDependencyReasons)
			c = 0;
		for (auto& c : s_counters.uploadFallbackReasons)
			c = 0;
		s_counters.bufferRenames = 0;
		s_counters.bufferRenameBytes = 0;
		for (auto& c : s_counters.bufferRenameSkipped)
			c = 0;
		s_counters.imageCopiesViaBuffer = 0;
		s_counters.uploadsInPrologue = 0;
		s_counters.depthPasses = 0;
		s_counters.depthPassesUnused = 0;
		s_counters.depthPassesReadOnly = 0;
		s_counters.depthStoreSkipped = 0;
		s_counters.depthStoreRestarts = 0;
		s_counters.drawDoneCalls = 0;
		for (auto& c : s_counters.guestWaitNs)
			c = 0;
		for (auto& c : s_counters.gpuWaitNsByReason)
			c = 0;
		for (auto& c : s_counters.renderPassRestarts)
			c = 0;
		s_gpuLastEndNs = 0;
		{
			std::scoped_lock lock(s_frameMutex);
			s_frames.clear();
		}
		s_scriptSyncs.clear();
		s_inputAxes.clear();
		for (auto& a : s_scriptSyncAt)
			a = -1.0;
		s_inputScript = LoadInputScript(CafeSystem::GetForegroundTitleId());
		{
			std::scoped_lock lock(s_jitMutex);
			if (s_full) // a line per recompiled function, only needed to profile
				s_jitMap.open(dir / "jitmap.txt", std::ios::trunc);
			s_jitFail.open(dir / "jitfail.txt", std::ios::trunc);
			// tuning jit_code=1: also keep the host code of each function, for per-instruction profiles
			if (Tunable("jit_code", 0) != 0)
				s_jitCode.open(dir / "jitcode.bin", std::ios::trunc | std::ios::binary);
		}
		PPCRecompilerAArch64Gen_reportInterfaceFunctionsToPerfLog();
		s_sessionStart = Clock::now();
		s_hasLastFrame = false;
		s_stop = false;
		s_active = true;
		s_writer = std::thread(WriterThread, dir);
	}

	void SessionEnd()
	{
#ifdef FRACTAL_PGO_GENERATE
		PgoWriteProfile();
#endif
		PerfHint::Reset();
		if (!IsActive())
			return;
		s_active = false;
		{
			std::scoped_lock lock(s_stopMutex);
			s_stop = true;
		}
		s_stopCv.notify_all();
		if (s_writer.joinable())
			s_writer.join();
		{
			// how often each problem happened
			std::scoped_lock lock(s_eventMutex);
			if (s_events.is_open())
			{
				if (!s_eventOrder.empty())
					s_events << "-- counts\n";
				for (const auto& key : s_eventOrder)
					s_events << fmt::format("{} {}\n", s_eventCounts[key], key);
				s_events.close();
			}
		}
		std::scoped_lock lock(s_jitMutex);
		s_jitMap.close();
		s_jitFail.close();
		s_jitCode.close();
	}

	void RenderPassBegin()
	{
		if (IsActive())
			s_counters.renderPasses.fetch_add(1, std::memory_order_relaxed);
	}

	sint64 Tunable(std::string_view key, sint64 defaultValue)
	{
		static std::once_flag s_once;
		static std::unordered_map<std::string, sint64> s_values;
		std::call_once(s_once, [] {
			std::ifstream file(ActiveSettings::GetUserDataPath("perf/tuning.txt"));
			std::string line;
			while (std::getline(file, line))
			{
				const size_t eq = line.find('=');
				if (eq == std::string::npos || line.empty() || line[0] == '#')
					continue;
				try
				{
					s_values[line.substr(0, eq)] = std::stoll(line.substr(eq + 1));
				}
				catch (...)
				{
				}
			}
			for (auto& [k, v] : s_values)
				cemuLog_log(LogType::Force, "PerfLog: tuning {}={}", k, v);
		});
		auto it = s_values.find(std::string(key));
		return it != s_values.end() ? it->second : defaultValue;
	}

	void RenderPassEnded(RenderPassEnd reason)
	{
		if (IsActive())
			s_counters.renderPassEnds[(size_t)reason].fetch_add(1, std::memory_order_relaxed);
	}

	void RenderPassRestarted(RenderPassEnd previousEnd)
	{
		if (IsActive())
			s_counters.renderPassRestarts[(size_t)previousEnd].fetch_add(1, std::memory_order_relaxed);
	}

	void BufferUploadDirect()
	{
		if (IsActive())
			s_counters.bufferUploadsDirect.fetch_add(1, std::memory_order_relaxed);
	}

	static thread_local GpuWaitReason t_gpuWaitReason = GpuWaitReason::Other;

	static std::atomic<const char*> s_renderStage{nullptr};
	static std::atomic<uint64> s_renderStageArg{0};
	static std::atomic<sint64> s_renderStageSinceMs{0};

	static sint64 _steadyMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	static thread_local bool t_isRenderThread = false;

	void MarkRenderThread()
	{
		t_isRenderThread = true;
	}

	RenderStage::RenderStage(const char* what, uint64 arg)
	{
		m_active = t_isRenderThread;
		if (!m_active)
			return;
		m_prev = s_renderStage.load(std::memory_order_relaxed);
		m_prevArg = s_renderStageArg.load(std::memory_order_relaxed);
		m_prevSince = s_renderStageSinceMs.load(std::memory_order_relaxed);
		s_renderStageArg.store(arg, std::memory_order_relaxed);
		s_renderStageSinceMs.store(_steadyMs(), std::memory_order_relaxed);
		s_renderStage.store(what, std::memory_order_release);
	}

	RenderStage::~RenderStage()
	{
		if (!m_active)
			return;
		s_renderStageArg.store(m_prevArg, std::memory_order_relaxed);
		s_renderStageSinceMs.store(m_prevSince, std::memory_order_relaxed);
		s_renderStage.store(m_prev, std::memory_order_release);
	}

	std::string DescribeRenderStage()
	{
		const char* what = s_renderStage.load(std::memory_order_acquire);
		if (!what)
			return "render thread: not in a tracked wait";
		return fmt::format("render thread: {} {:016x} for {} ms", what, s_renderStageArg.load(), _steadyMs() - s_renderStageSinceMs.load());
	}

	GpuWaitScope::GpuWaitScope(GpuWaitReason reason) : m_prev(t_gpuWaitReason)
	{
		// the blocking linear copy waits through the generic readback wait: keep the more specific outer tag
		if (t_gpuWaitReason != GpuWaitReason::LinearCopy)
			t_gpuWaitReason = reason;
	}

	GpuWaitScope::~GpuWaitScope()
	{
		t_gpuWaitReason = m_prev;
	}

	void GpuWait(uint64 ns)
	{
		PerfHint::AddIdleNs(PerfHint::Role::Gpu, ns); // the GPU thread waits for the host GPU: idle for the CPU hint session
		if (!IsActive())
			return;
		s_counters.gpuWaitNs.fetch_add(ns, std::memory_order_relaxed);
		s_counters.gpuWaitNsByReason[(size_t)t_gpuWaitReason].fetch_add(ns, std::memory_order_relaxed);
	}

	void TextureReadbackScheduled()
	{
		if (IsActive())
			s_counters.textureReadbacks.fetch_add(1, std::memory_order_relaxed);
	}

	void TextureDecoded(uint64 ns, uint64 texels, uint64 bytes, bool split)
	{
		if (!IsActive())
			return;
		s_counters.texDecodes.fetch_add(1, std::memory_order_relaxed);
		s_counters.texDecodeNs.fetch_add(ns, std::memory_order_relaxed);
		s_counters.texDecodeTexels.fetch_add(texels, std::memory_order_relaxed);
		s_counters.texDecodeBytes.fetch_add(bytes, std::memory_order_relaxed);
		if (split)
			s_counters.texDecodesSplit.fetch_add(1, std::memory_order_relaxed);
		uint64 prevMax = s_counters.texDecodeMaxNs.load(std::memory_order_relaxed);
		while (ns > prevMax && !s_counters.texDecodeMaxNs.compare_exchange_weak(prevMax, ns, std::memory_order_relaxed))
			;
	}

	void TextureUploadRecorded(uint64 ns)
	{
		if (IsActive())
			s_counters.texUploadNs.fetch_add(ns, std::memory_order_relaxed);
	}

	void SelfDependencyOtherSubresource()
	{
		if (IsActive())
			s_counters.selfDependencyOtherSubresource.fetch_add(1, std::memory_order_relaxed);
	}

	void SelfDependencyAspects(bool color, bool depth)
	{
		if (!IsActive())
			return;
		if (color)
			s_counters.selfDependencyColor.fetch_add(1, std::memory_order_relaxed);
		if (depth)
			s_counters.selfDependencyDepth.fetch_add(1, std::memory_order_relaxed);
	}

	void SelfDependencyReason(int reason)
	{
		if (IsActive() && reason >= 0 && reason < 4)
			s_counters.selfDependencyReasons[reason].fetch_add(1, std::memory_order_relaxed);
	}

	void ImageCopyViaBuffer()
	{
		if (IsActive())
			s_counters.imageCopiesViaBuffer.fetch_add(1, std::memory_order_relaxed);
	}

	void DepthAttachmentUse(bool tested, bool written)
	{
		if (!IsActive())
			return;
		s_counters.depthPasses.fetch_add(1, std::memory_order_relaxed);
		if (!tested && !written)
			s_counters.depthPassesUnused.fetch_add(1, std::memory_order_relaxed);
		else if (!written)
			s_counters.depthPassesReadOnly.fetch_add(1, std::memory_order_relaxed);
	}

	void DepthStoreSkipped(bool restarted)
	{
		if (IsActive())
			(restarted ? s_counters.depthStoreRestarts : s_counters.depthStoreSkipped).fetch_add(1, std::memory_order_relaxed);
	}

	void DrawDone()
	{
		if (IsActive())
			s_counters.drawDoneCalls.fetch_add(1, std::memory_order_relaxed);
	}

	void GuestWaited(GuestWait what, uint64 ns)
	{
		if (IsActive())
			s_counters.guestWaitNs[(size_t)what].fetch_add(ns, std::memory_order_relaxed);
	}

	void TextureUploadInPrologue()
	{
		if (IsActive())
			s_counters.uploadsInPrologue.fetch_add(1, std::memory_order_relaxed);
	}

	void BufferCacheRename(uint32 bytes)
	{
		if (!IsActive())
			return;
		s_counters.bufferRenames.fetch_add(1, std::memory_order_relaxed);
		s_counters.bufferRenameBytes.fetch_add(bytes, std::memory_order_relaxed);
	}

	void BufferCacheRenameSkipped(int reason)
	{
		if (IsActive() && reason >= 0 && reason < 4)
			s_counters.bufferRenameSkipped[reason].fetch_add(1, std::memory_order_relaxed);
	}

	void BufferUploadFallback(int reason)
	{
		if (IsActive() && reason >= 0 && reason < 3)
			s_counters.uploadFallbackReasons[reason].fetch_add(1, std::memory_order_relaxed);
	}

	void SelfDependencyKept()
	{
		if (IsActive())
			s_counters.selfDependencyKept.fetch_add(1, std::memory_order_relaxed);
	}

	void PipelineBarrier()
	{
		if (IsActive())
			s_counters.pipelineBarriers.fetch_add(1, std::memory_order_relaxed);
	}

	void DrawCall()
	{
		if (IsActive())
		{
			s_counters.draws.fetch_add(1, std::memory_order_relaxed);
			s_frameDraws.fetch_add(1, std::memory_order_relaxed);
		}
	}

	// GPU load for the performance HUD: command buffer timestamps also run without the full log while the HUD asks
	static std::atomic<bool> s_gpuLoadWanted{false};
	static std::atomic<uint64> s_gpuLoadBusyNs{0};
	static std::atomic<bool> s_gpuLoadSampled{false};

	void SetGpuLoadWanted(bool wanted)
	{
		s_gpuLoadWanted.store(wanted, std::memory_order_relaxed);
	}

	bool GpuLoadWanted()
	{
		return s_gpuLoadWanted.load(std::memory_order_relaxed);
	}

	int GpuLoadPercent()
	{
		static std::chrono::steady_clock::time_point s_last{};
		const auto now = std::chrono::steady_clock::now();
		const uint64 busyNs = s_gpuLoadBusyNs.exchange(0, std::memory_order_relaxed);
		const auto last = s_last;
		s_last = now;
		if (!s_gpuLoadSampled.load(std::memory_order_relaxed) || last.time_since_epoch().count() == 0)
			return -1;
		const double wallNs = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count();
		if (wallNs <= 0.0)
			return -1;
		return (int)std::clamp(busyNs * 100.0 / wallNs + 0.5, 0.0, 100.0);
	}

	void GpuWork(uint64 startNs, uint64 endNs)
	{
		if (!IsActive() && !GpuLoadWanted())
			return;
		constexpr uint64 kSecondNs = 1000000000ull;
		if (endNs - startNs > kSecondNs)
			return; // not a real command buffer duration (bad timestamp)
		if (s_gpuLastEndNs > endNs + kSecondNs)
			s_gpuLastEndNs = 0; // the GPU clock went backwards (or an earlier timestamp was bogus)
		const uint64 from = std::max(startNs, s_gpuLastEndNs);
		if (endNs > from)
		{
			if (IsActive())
				s_counters.gpuBusyNs += endNs - from;
			if (GpuLoadWanted())
			{
				s_gpuLoadBusyNs.fetch_add(endNs - from, std::memory_order_relaxed);
				s_gpuLoadSampled.store(true, std::memory_order_relaxed);
			}
		}
		s_gpuLastEndNs = std::max(s_gpuLastEndNs, endNs);
	}

	// jitcode.bin records: uint64 host address, uint32 size, uint32 ppc address, then the code bytes
	static void WriteJitCode(const void* hostCode, uint32 hostSize, uint32 ppcAddress)
	{
		if (!s_jitCode.is_open())
			return;
		uint64 address = (uint64)(uintptr_t)hostCode;
		s_jitCode.write((const char*)&address, 8);
		s_jitCode.write((const char*)&hostSize, 4);
		s_jitCode.write((const char*)&ppcAddress, 4);
		s_jitCode.write((const char*)hostCode, hostSize);
		s_jitCode.flush();
	}

	void JitFunction(uint32 ppcAddress, uint32 ppcSize, const void* hostCode, uint32 hostSize)
	{
		if (!IsActive())
			return;
		std::scoped_lock lock(s_jitMutex);
		if (!s_jitMap.is_open())
			return;
		s_jitMap << fmt::format("{:x} {:x} {:08x} {}\n", (uint64)(uintptr_t)hostCode, hostSize, ppcAddress, ppcSize);
		s_jitMap.flush();
		WriteJitCode(hostCode, hostSize, ppcAddress);
	}

	void JitGlue(const void* hostCode, uint32 hostSize, const char* name)
	{
		std::scoped_lock lock(s_jitMutex);
		if (!s_jitMap.is_open())
			return;
		s_jitMap << fmt::format("{:x} {:x} 0 {}\n", (uint64)(uintptr_t)hostCode, hostSize, name);
		s_jitMap.flush();
		WriteJitCode(hostCode, hostSize, 0);
	}

	void JitFailure(uint32 ppcAddress, uint32 ppcSize, const char* stage)
	{
		if (!IsActive())
			return;
		std::scoped_lock lock(s_jitMutex);
		if (!s_jitFail.is_open())
			return;
		// the game's own symbol, when its executable exports one (many do): tells which part of the game runs slowly
		std::string symbol;
		if (RPLStoredSymbol* sym = rplSymbolStorage_getByClosestAddress(ppcAddress); sym && ppcAddress >= 0x01000000 && ppcAddress - sym->address < 0x1000)
			symbol = fmt::format(" {}.{}+0x{:x}", (const char*)sym->libName, (const char*)sym->symbolName, ppcAddress - sym->address);
		s_jitFail << fmt::format("{:08x} {} {}{}\n", ppcAddress, ppcSize, stage, symbol);
		Event("recompiler_fallback", fmt::format("function {:08x}{} ({} bytes) runs in the interpreter, {} (all in jitfail.txt)", ppcAddress, symbol, ppcSize, stage));
		s_jitFail.flush();
	}
}
