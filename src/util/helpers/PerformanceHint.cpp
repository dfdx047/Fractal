#include "util/helpers/PerformanceHint.h"
#include "Cafe/HW/Latte/Core/LattePerfLog.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>

namespace PerfHint
{
	// NDK APerformanceHint* (libandroid.so, API 33+; the app's minSdk is lower, so they are looked up at runtime)
	struct APerformanceHintManager;
	struct APerformanceHintSession;
	using FnGetManager = APerformanceHintManager* (*)();
	using FnCreateSession = APerformanceHintSession* (*)(APerformanceHintManager*, const int32_t*, size_t, int64_t);
	using FnUpdateTarget = int (*)(APerformanceHintSession*, int64_t);
	using FnReportActual = int (*)(APerformanceHintSession*, int64_t);
	using FnCloseSession = void (*)(APerformanceHintSession*);
	using FnSetThreads = int (*)(APerformanceHintSession*, const pid_t*, size_t); // API 34
	using FnSetPreferPowerEfficiency = int (*)(APerformanceHintSession*, bool); // API 35

	static struct
	{
		bool loaded = false;
		FnGetManager getManager = nullptr;
		FnCreateSession createSession = nullptr;
		FnUpdateTarget updateTarget = nullptr;
		FnReportActual reportActual = nullptr;
		FnCloseSession closeSession = nullptr;
		FnSetThreads setThreads = nullptr;
		FnSetPreferPowerEfficiency setPreferPowerEfficiency = nullptr;
		APerformanceHintManager* manager = nullptr;
	}s_api;

	constexpr int kRoles = (int)Role::Count;
	constexpr uint64 kVsyncNs = 16683333; // the Wii U TV output runs at 59.94 Hz
	constexpr int kWindow = 60; // frames used to find the game's frame period

	static std::array<std::atomic<int32_t>, kRoles> s_tids{};
	static std::array<std::atomic<uint64>, kRoles> s_idleNs{};
	static std::atomic<bool> s_threadsChanged{false};
	static std::mutex s_sessionMutex;
	static APerformanceHintSession* s_session = nullptr;

	// frame bookkeeping, only touched by the thread that presents frames
	static uint64 s_lastFrameNs = 0;
	static std::array<uint64, kRoles> s_lastIdle{};
	static std::array<uint64, kWindow> s_intervals{};
	static std::array<uint64, kWindow> s_busy{};
	static int s_windowFill = 0, s_windowPos = 0, s_framesSinceTarget = 0;
	static uint64 s_targetNs = 0;
	static uint64 s_statActualSum = 0, s_statFrames = 0, s_statOverTarget = 0;
	static uint64 s_nextSessionAttemptNs = 0;

	// off by default: on the POCO X6 Pro (Mali) Bayonetta 2 fell from 33.2 to 26.3 fps and on the Odin 3 the AC IV 1% low
	// from 57 to 46-50 fps (the prime cores ran at ~3.25 instead of 3.76 GHz); WD on the Odin 3 used 7% less power, the
	// Retroid Pocket 5 and the Mi A3 did not change (19/11)
	static bool Enabled()
	{
		static const bool s_enabled = PerfLog::Tunable("adpf", 0) != 0;
		return s_enabled;
	}

	static bool LoadApi()
	{
		static std::once_flag s_once;
		std::call_once(s_once, []() {
			void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
			if (!lib)
				return;
			s_api.getManager = (FnGetManager)dlsym(lib, "APerformanceHint_getManager");
			s_api.createSession = (FnCreateSession)dlsym(lib, "APerformanceHint_createSession");
			s_api.updateTarget = (FnUpdateTarget)dlsym(lib, "APerformanceHint_updateTargetWorkDuration");
			s_api.reportActual = (FnReportActual)dlsym(lib, "APerformanceHint_reportActualWorkDuration");
			s_api.closeSession = (FnCloseSession)dlsym(lib, "APerformanceHint_closeSession");
			s_api.setThreads = (FnSetThreads)dlsym(lib, "APerformanceHint_setThreads");
			s_api.setPreferPowerEfficiency = (FnSetPreferPowerEfficiency)dlsym(lib, "APerformanceHint_setPreferPowerEfficiency");
			if (!s_api.getManager || !s_api.createSession || !s_api.updateTarget || !s_api.reportActual || !s_api.closeSession)
			{
				cemuLog_log(LogType::Force, "ADPF: not available on this Android version");
				return;
			}
			s_api.manager = s_api.getManager();
			if (!s_api.manager)
			{
				cemuLog_log(LogType::Force, "ADPF: no performance hint manager on this device");
				return;
			}
			s_api.loaded = true;
		});
		return s_api.loaded;
	}

	uint64 NowNs()
	{
		timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return (uint64)ts.tv_sec * 1000000000ull + (uint64)ts.tv_nsec;
	}

	bool IsActive()
	{
		return s_session != nullptr;
	}

	void RegisterCurrentThread(Role role)
	{
		if (!Enabled())
			return;
		s_tids[(int)role].store((int32_t)gettid());
		s_threadsChanged.store(true);
	}

	void AddIdleNs(Role role, uint64 ns)
	{
		s_idleNs[(int)role].fetch_add(ns, std::memory_order_relaxed);
	}

	static void CloseSessionLocked()
	{
		if (s_session)
		{
			s_api.closeSession(s_session);
			s_session = nullptr;
		}
	}

	// called with s_sessionMutex held, on the presenting thread
	static void UpdateSessionThreads(uint64 now)
	{
		std::array<int32_t, kRoles> tids{};
		size_t count = 0;
		for (auto& t : s_tids)
		{
			const int32_t tid = t.load();
			if (tid > 0)
				tids[count++] = tid;
		}
		if (count == 0)
			return;
		const int64_t target = (int64_t)(s_targetNs ? s_targetNs : kVsyncNs);
		if (s_session && s_api.setThreads)
		{
			if (s_api.setThreads(s_session, (const pid_t*)tids.data(), count) == 0)
				return;
		}
		CloseSessionLocked();
		s_session = s_api.createSession(s_api.manager, tids.data(), count, target);
		if (!s_session)
		{
			s_nextSessionAttemptNs = now + 5000000000ull; // a thread may have exited; try again in 5 s
			cemuLog_log(LogType::Force, "ADPF: creating the hint session for {} threads failed", count);
			return;
		}
		s_targetNs = (uint64)target;
		static const bool s_powerEfficiency = PerfLog::Tunable("adpf_power_eff", 0) != 0;
		bool powerEff = false;
		if (s_powerEfficiency && s_api.setPreferPowerEfficiency)
			powerEff = s_api.setPreferPowerEfficiency(s_session, true) == 0;
		cemuLog_log(LogType::Force, "ADPF: hint session for {} threads (CPU cores + GPU thread), target {:.2f} ms{}", count, (double)target / 1e6,
			powerEff ? ", prefers power efficiency" : "");
	}

	static uint64 Median(const std::array<uint64, kWindow>& values, int n)
	{
		std::array<uint64, kWindow> sorted;
		std::copy(values.begin(), values.begin() + n, sorted.begin());
		std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.begin() + n);
		return sorted[n / 2];
	}

	void FramePresented(uint32 swapInterval)
	{
		if (!Enabled() || !LoadApi())
			return;
		const uint64 now = NowNs();
		std::array<uint64, kRoles> idle;
		for (int i = 0; i < kRoles; i++)
			idle[i] = s_idleNs[i].load(std::memory_order_relaxed);
		const uint64 interval = s_lastFrameNs ? now - s_lastFrameNs : 0;
		const std::array<uint64, kRoles> lastIdle = s_lastIdle;
		s_lastFrameNs = now;
		s_lastIdle = idle;
		// first frame, or a pause / loading screen without frames: start measuring again
		if (interval == 0 || interval > 500000000ull)
			return;

		// work of the busiest thread in this frame
		uint64 actual = 0;
		for (int i = 0; i < kRoles; i++)
		{
			if (s_tids[i].load(std::memory_order_relaxed) <= 0)
				continue;
			const uint64 idleDelta = std::min(idle[i] - lastIdle[i], interval);
			actual = std::max(actual, interval - idleDelta);
		}
		actual = std::max<uint64>(actual, 100000); // at least 0.1 ms

		std::scoped_lock lock(s_sessionMutex);
		if ((s_threadsChanged.exchange(false) || !s_session) && now >= s_nextSessionAttemptNs)
			UpdateSessionThreads(now);
		if (!s_session)
			return;

		s_intervals[s_windowPos] = interval;
		s_busy[s_windowPos] = actual;
		s_windowPos = (s_windowPos + 1) % kWindow;
		s_windowFill = std::min(s_windowFill + 1, kWindow);

		// the frame period the game wants: a game that keeps up and has time left between frames shows its own rate (30 fps
		// titles wait for the second vsync themselves even with swap interval 1); a game that is busy all frame long needs
		// the rate it asks for
		if (++s_framesSinceTarget >= 30 && s_windowFill >= 30)
		{
			s_framesSinceTarget = 0;
			const uint64 medInterval = Median(s_intervals, s_windowFill);
			const uint64 medBusy = Median(s_busy, s_windowFill);
			uint64 periods;
			if (medBusy * 4 < medInterval * 3)
				periods = std::max<uint64>(1, (medInterval + kVsyncNs / 2) / kVsyncNs);
			else
				periods = std::max<uint32>(1, std::min<uint32>(swapInterval, 4));
			static const uint64 s_marginPct = (uint64)std::clamp<sint64>(PerfLog::Tunable("adpf_margin_pct", 90), 50, 100);
			const uint64 target = periods * kVsyncNs * s_marginPct / 100;
			if (target != s_targetNs)
			{
				if (s_api.updateTarget(s_session, (int64_t)target) == 0)
				{
					cemuLog_log(LogType::Force, "ADPF: target {:.2f} ms (frame period {} vsync, median frame {:.2f} ms, busiest thread {:.2f} ms)",
						(double)target / 1e6, periods, (double)medInterval / 1e6, (double)medBusy / 1e6);
					s_targetNs = target;
				}
			}
		}

		s_api.reportActual(s_session, (int64_t)actual);
		s_statActualSum += actual;
		s_statFrames++;
		if (actual > s_targetNs)
			s_statOverTarget++;
		if (s_statFrames >= 1800)
		{
			cemuLog_log(LogType::Force, "ADPF: last {} frames: busiest thread {:.2f} ms on average, target {:.2f} ms, {}% of frames over it",
				s_statFrames, (double)s_statActualSum / s_statFrames / 1e6, (double)s_targetNs / 1e6, s_statOverTarget * 100 / s_statFrames);
			s_statActualSum = s_statFrames = s_statOverTarget = 0;
		}
	}

	void Reset()
	{
		std::scoped_lock lock(s_sessionMutex);
		if (s_api.loaded)
			CloseSessionLocked();
		for (auto& t : s_tids)
			t.store(0);
		s_lastFrameNs = 0;
		s_windowFill = s_windowPos = s_framesSinceTarget = 0;
		s_targetNs = 0;
		s_statActualSum = s_statFrames = s_statOverTarget = 0;
		s_nextSessionAttemptNs = 0;
	}
}
