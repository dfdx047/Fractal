#include "Cafe/HW/Espresso/Const.h"
#include "config/ActiveSettings.h"
#include "util/helpers/fspinlock.h"
#include "util/highresolutiontimer/HighResolutionTimer.h"
#include "Cafe/HW/Espresso/PPCState.h"
#include "Cafe/HW/Latte/Core/LattePerfLog.h"
#include <unordered_map>
#include <dlfcn.h>


uint64 _rdtscLastMeasure = 0;
uint64 _rdtscFrequency = 0;

struct uint128_t
{
	uint64 low;
	uint64 high;
};

static_assert(sizeof(uint128_t) == 16);

uint128_t _rdtscAcc{};

uint64 muldiv64(uint64 a, uint64 b, uint64 d)
{
	uint64 diva = a / d;
	uint64 moda = a % d;
	uint64 divb = b / d;
	uint64 modb = b % d;
	return diva * b + moda * divb + moda * modb / d;
}

uint64 PPCTimer_estimateRDTSCFrequency()
{

	_mm_mfence();
	uint64 tscStart = __rdtsc();
	unsigned int startTime = GetTickCount();
	HRTick startTick = HighResolutionTimer::now().getTick();
	// wait roughly 3 seconds
	while (true)
	{
		if ((GetTickCount() - startTime) >= 3000)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	_mm_mfence();
	HRTick stopTick = HighResolutionTimer::now().getTick();
	uint64 tscEnd = __rdtsc();
	// derive frequency approximation from measured time difference
	uint64 tsc_diff = tscEnd - tscStart;
	uint64 hrtFreq = 0;
	uint64 hrtDiff = HighResolutionTimer::getTimeDiffEx(startTick, stopTick, hrtFreq);
	uint64 tsc_freq = muldiv64(tsc_diff, hrtFreq, hrtDiff);

	// uint64 freqMultiplier = tsc_freq / hrtFreq;
	//cemuLog_log(LogType::Force, "RDTSC measurement test:");
	//cemuLog_log(LogType::Force, "TSC-diff:   0x{:016x}", tsc_diff);
	//cemuLog_log(LogType::Force, "TSC-freq:   0x{:016x}", tsc_freq);
	//cemuLog_log(LogType::Force, "HPC-diff:   0x{:016x}", qpc_diff);
	//cemuLog_log(LogType::Force, "HPC-freq:   0x{:016x}", (uint64)qpc_freq.QuadPart);
	//cemuLog_log(LogType::Force, "Multiplier: 0x{:016x}", freqMultiplier);

	return tsc_freq;
}

int PPCTimer_initThread()
{
	_rdtscFrequency = PPCTimer_estimateRDTSCFrequency();
	return 0;
}

void PPCTimer_init()
{
	std::thread t(PPCTimer_initThread);
	t.detach();
	_rdtscLastMeasure = __rdtsc();
}

uint64 _tickSummary = 0;

void PPCTimer_start()
{
	_rdtscLastMeasure = __rdtsc();
	_tickSummary = 0;
}

uint64 PPCTimer_getRawTsc()
{
	return __rdtsc();
}

uint64 PPCTimer_microsecondsToTsc(uint64 us)
{
	return (us * _rdtscFrequency) / 1000000ULL;
}

uint64 PPCTimer_tscToMicroseconds(uint64 us)
{
	uint128_t r{};
	r.low = _umul128(us, 1000000ULL, &r.high);

	uint64 remainder;
	const uint64 microseconds = _udiv128(r.high, r.low, _rdtscFrequency, &remainder);

	return microseconds;
}

bool PPCTimer_isReady()
{
	return _rdtscFrequency != 0;
}

void PPCTimer_waitForInit()
{
	while (!PPCTimer_isReady()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

FSpinlock sTimerSpinlock;

// diagnostics (perf/tuning.txt timer_stats=1): how often the guest clock is read and from where. Each read of the host
// counter costs ~205 ns on the Retroid Pocket 5 (its kernel traps EL0 cntvct_el0 reads), ~17 ns elsewhere
struct TimerReadStats
{
	uint64 calls = 0;
	uint64 lastDump = 0;
	std::unordered_map<uint64, uint32> sites; // (host return address offset << 32) | guest LR
};
static TimerReadStats* s_timerStats = nullptr;

static void TimerStats_Record(void* hostRet, uint64 now)
{
	TimerReadStats& st = *s_timerStats;
	st.calls++;
	if ((st.calls & 31) == 0)
	{
		Dl_info info{};
		uint64 hostOff = (uint64)hostRet;
		if (dladdr(hostRet, &info) && info.dli_fbase)
			hostOff -= (uint64)info.dli_fbase;
		auto* cpu = PPCInterpreter_getCurrentInstance();
		uint32 guestLR = cpu ? cpu->spr.LR : 0;
		st.sites[(hostOff << 32) | guestLR]++;
	}
	if (st.lastDump == 0)
		st.lastDump = now;
	const uint64 elapsed = now - st.lastDump;
	if (_rdtscFrequency && elapsed >= _rdtscFrequency * 10)
	{
		const double secs = (double)elapsed / (double)_rdtscFrequency;
		std::vector<std::pair<uint64, uint32>> v(st.sites.begin(), st.sites.end());
		std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
		cemuLog_log(LogType::Force, "TimerStats: {:.0f} guest clock reads/s", st.calls / secs);
		for (size_t i = 0; i < std::min<size_t>(v.size(), 16); i++)
			cemuLog_log(LogType::Force, "TimerStats: {:8.0f}/s host +{:x} guest LR {:08x}", v[i].second * 32 / secs, v[i].first >> 32, (uint32)v[i].first);
		st.sites.clear();
		st.calls = 0;
		st.lastDump = now;
	}
}

// thread safe
uint64 PPCTimer_getFromRDTSC()
{
	static const bool s_statsEnabled = [] { if (PerfLog::Tunable("timer_stats", 0) == 0) return false; s_timerStats = new TimerReadStats(); return true; }();
	sTimerSpinlock.lock();
	_mm_mfence();
	uint64 rdtscCurrentMeasure = __rdtsc();
	if (s_statsEnabled) [[unlikely]]
		TimerStats_Record(__builtin_return_address(0), rdtscCurrentMeasure);
	uint64 rdtscDif = rdtscCurrentMeasure - _rdtscLastMeasure;
	// optimized max(rdtscDif, 0) without conditionals
	rdtscDif = rdtscDif & ~(uint64)((sint64)rdtscDif >> 63);

	uint128_t diff{};
	diff.low = _umul128(rdtscDif, Espresso::CORE_CLOCK, &diff.high);

	if(rdtscCurrentMeasure > _rdtscLastMeasure)
		_rdtscLastMeasure = rdtscCurrentMeasure; // only travel forward in time

	uint8 c = 0;
	// requires casting because of long / long long nonesense
	c = _addcarry_u64(c, _rdtscAcc.low, diff.low, (unsigned long long*)&_rdtscAcc.low);
	_addcarry_u64(c, _rdtscAcc.high, diff.high, (unsigned long long*)&_rdtscAcc.high);

	uint64 remainder;
	uint64 elapsedTick = _udiv128(_rdtscAcc.high, _rdtscAcc.low, _rdtscFrequency, &remainder);

	_rdtscAcc.low = remainder;
	_rdtscAcc.high = 0;

	// timer scaling
	elapsedTick <<= 3ull; // *8
	uint8 timerShiftFactor = ActiveSettings::GetTimerShiftFactor();
	elapsedTick >>= timerShiftFactor;

	_tickSummary += elapsedTick;

	sTimerSpinlock.unlock();
	return _tickSummary;
}
