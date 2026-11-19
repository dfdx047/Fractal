#pragma once

// Android Dynamic Performance Framework (APerformanceHintManager, Android 13+): the emulated CPU cores and the GPU thread
// form one hint session, and every presented frame reports how long the busiest of them actually worked against the
// frame time the game needs. The CPU governor can then give just the clocks the game needs instead of reacting to load
// alone (spinning idle loops look like full load; sleeping ones made it drop clocks too far on the Retroid Pocket 5).
// perf/tuning.txt: adpf=1 turns it on (off by default, see PerformanceHint.cpp), adpf_power_eff=1 asks for power
// efficiency (Android 15+), adpf_margin_pct (default 90) is the share of the frame period given as the target.
namespace PerfHint
{
	enum class Role : int
	{
		PpcCore0 = 0,
		PpcCore1 = 1,
		PpcCore2 = 2,
		Gpu = 3,
		Count = 4,
	};

	// call on the thread itself
	void RegisterCurrentThread(Role role);
	// time the thread had nothing to do (idle loop, waiting for commands or for the GPU)
	void AddIdleNs(Role role, uint64 ns);
	// once per presented frame; swapInterval = the game's GX2 swap interval (vsyncs per frame it asks for)
	void FramePresented(uint32 swapInterval);
	// game ends: closes the session
	void Reset();
	// for the idle loops: a monotonic timestamp in ns, cheap enough to call around every wait
	uint64 NowNs();
	bool IsActive();
}
