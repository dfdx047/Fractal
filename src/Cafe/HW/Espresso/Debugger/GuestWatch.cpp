#include "Cafe/HW/Espresso/Debugger/GuestWatch.h"
#include "Cafe/HW/Espresso/PPCState.h"
#include "Cafe/HW/Latte/Core/LattePerfLog.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "Common/ExceptionHandler/ExceptionHandler.h"

#include <mutex>
#include <set>
#include "config/ActiveSettings.h"

#if BOOST_PLAT_ANDROID
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#endif

namespace GuestWatch
{
	uint32 g_lo = 0;
	uint32 g_hi = 0;

	struct Entry
	{
		uint64 seq;
		uint32 pc;
		uint32 lr;
		uint32 thread;
		uint32 addr;
		uint32 value;
		uint8 size;
		char kind;
	};

	static std::mutex s_mutex;
	static std::vector<Entry> s_ring;
	static uint64 s_seq = 0;
	static bool s_pairs = false;
	static bool s_logEach = false;
	static bool s_logFirstPerPC = false; // watch_log=2: only the first write of each pc/address pair
	static std::set<uint64> s_seenPCs;
	static uint32 s_dupLogs = 0;
	static uint32 s_lastDupMask = 0;

	static void Add(uint32 pc, uint32 lr, uint32 thread, uint32 addr, uint32 size, uint32 value, char kind);
	static uint32 CurrentThread();

#if BOOST_PLAT_ANDROID
	// watch_jit=1: recompiled code writes guest memory with host stores the hooks above never see. The host pages of the
	// range are made read only; a store to them faults, HandleFault decodes it (STR/STUR, register or immediate
	// offset), logs it when it touches the range, performs it and skips the instruction. Stores it can't decode
	// (atomics, pairs, libc copies) open the pages for a moment; a helper thread closes them again
	static uintptr_t s_pageLo = 0, s_pageHi = 0; // host addresses, page aligned
	static std::atomic<bool> s_pagesProtected{false};
	static std::atomic<uint32> s_openCount{0};

	static void ProtectPages(bool on)
	{
		mprotect((void*)s_pageLo, s_pageHi - s_pageLo, on ? PROT_READ : (PROT_READ | PROT_WRITE));
		s_pagesProtected.store(on);
	}

	static void StartPageWatch()
	{
		const uintptr_t pageSize = (uintptr_t)sysconf(_SC_PAGESIZE);
		s_pageLo = ((uintptr_t)(memory_base + g_lo)) & ~(pageSize - 1);
		s_pageHi = (((uintptr_t)(memory_base + g_hi)) + pageSize - 1) & ~(pageSize - 1);
		std::thread([] {
			// the range is mapped once the game's memory is set up; then keep the pages closed
			while (!memory_isAddressRangeAccessible(g_lo, g_hi - g_lo))
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			cemuLog_log(LogType::Force, "GuestWatch: page watch on host {:x}-{:x}", s_pageLo, s_pageHi);
			while (true)
			{
				if (!s_pagesProtected.load())
					ProtectPages(true);
				std::this_thread::sleep_for(std::chrono::microseconds(500));
			}
		}).detach();
	}

	static uint64 RegValue(const mcontext_t& mc, uint32 r, bool is64)
	{
		if (r == 31)
			return 0; // xzr for the value register; sp is never the base of a guest access
		return is64 ? mc.regs[r] : (mc.regs[r] & 0xFFFFFFFF);
	}

	bool HandleFault(void* faultAddr, void* context)
	{
		if (s_pageHi == 0)
			return false;
		const uintptr_t fa = (uintptr_t)faultAddr;
		if (fa < s_pageLo || fa >= s_pageHi)
			return false;
		ucontext_t* uc = (ucontext_t*)context;
		mcontext_t& mc = uc->uc_mcontext;
		const uint32 insn = *(uint32*)mc.pc;
		const uint32 sizeLog2 = insn >> 30;
		const uint32 rt = insn & 31, rn = (insn >> 5) & 31;
		uintptr_t addr = 0;
		bool decoded = false;
		if ((insn & 0x3FE00C00) == 0x38200800) // STR (register offset)
		{
			const uint32 rm = (insn >> 16) & 31, option = (insn >> 13) & 7, s = (insn >> 12) & 1;
			uint64 off = mc.regs[rm];
			if (option == 2) off = (uint32)off;                 // uxtw
			else if (option == 6) off = (uint64)(sint64)(sint32)(uint32)off; // sxtw
			addr = mc.regs[rn] + (off << (s ? sizeLog2 : 0));
			decoded = true;
		}
		else if ((insn & 0x3FC00000) == 0x39000000) // STR (unsigned immediate)
		{
			addr = mc.regs[rn] + (((insn >> 10) & 0xFFF) << sizeLog2);
			decoded = true;
		}
		else if ((insn & 0x3FE00C00) == 0x38000000) // STUR
		{
			sint64 imm = (sint64)((insn >> 12) & 0x1FF);
			if (imm & 0x100) imm -= 0x200;
			addr = mc.regs[rn] + imm;
			decoded = true;
		}
		const uint32 size = 1u << sizeLog2;
		const uint32 guestAddr = (uint32)(fa - (uintptr_t)memory_base);
		PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
		const uint32 lr = hCPU ? hCPU->spr.LR : 0;
		const uint32 thread = hCPU ? CurrentThread() : 0;
		if (!decoded || addr != fa)
		{
			// let it through: open the pages, the helper thread closes them again
			ProtectPages(false);
			if (guestAddr < g_hi && guestAddr + 4 > g_lo && s_openCount.fetch_add(1) < 64)
				cemuLog_log(LogType::Force, "GuestWatch page: undecoded store {:08x} at host pc {:x} to [{:08x}] lr {:08x} thr {:08x}", insn, (uint64)mc.pc, guestAddr, lr, thread);
			return true;
		}
		const uint64 value = RegValue(mc, rt, sizeLog2 == 3);
		ProtectPages(false);
		memcpy((void*)addr, &value, size);
		ProtectPages(true);
		if (guestAddr < g_hi && guestAddr + size > g_lo)
		{
			uint32 guestValue = 0;
			if (size == 4) guestValue = _swapEndianU32((uint32)value);
			else if (size == 2) guestValue = _swapEndianU16((uint16)value);
			else if (size == 1) guestValue = (uint8)value;
			else guestValue = _swapEndianU32((uint32)value);
			Add(0, lr, thread, guestAddr, size, guestValue, 'J');
			cemuLog_log(LogType::Force, "GuestWatch page: [{:08x}]={:08x} size {} host pc {:x} lr {:08x} thr {:08x}", guestAddr, guestValue, size, (uint64)mc.pc, lr, thread);
		}
		mc.pc += 4;
		return true;
	}
#else
	static void StartPageWatch() {}
	bool HandleFault(void*, void*) { return false; }
#endif

	void Init()
	{
		const sint64 lo = PerfLog::Tunable("watch_lo", 0);
		const sint64 size = PerfLog::Tunable("watch_size", 0);
		if (lo <= 0 || size <= 0)
			return;
		const sint64 ringSize = std::max<sint64>(PerfLog::Tunable("watch_ring", 4096), 64);
		s_ring.resize((size_t)ringSize);
		s_pairs = PerfLog::Tunable("watch_pairs", 0) != 0;
		s_logEach = PerfLog::Tunable("watch_log", 0) == 1; // every write to the log (low volume ranges only)
		s_logFirstPerPC = PerfLog::Tunable("watch_log", 0) == 2;
		g_lo = (uint32)lo;
		g_hi = (uint32)(lo + size);
		cemuLog_log(LogType::Force, "GuestWatch: watching 0x{:08x}-0x{:08x}, ring {}, pairs {}", g_lo, g_hi, ringSize, s_pairs);
		if (PerfLog::Tunable("watch_jit", 0) != 0)
			StartPageWatch();
	}

	static void FormatEntry(const Entry& e, char* buf, size_t bufSize)
	{
		snprintf(buf, bufSize, "w#%llu %c%u [%08x]=%08x pc %08x lr %08x thr %08x", (unsigned long long)e.seq, e.kind, (uint32)e.size, e.addr, e.value, e.pc, e.lr, e.thread);
	}

	// called with s_mutex held
	static void CheckPairs(const Entry& last)
	{
		const uint32 count = (g_hi - g_lo) / 8;
		uint32 dupMask = 0;
		for (uint32 i = 0; i < count && i < 32; i++)
		{
			const uint32 pi = memory_readU32(g_lo + i * 8);
			if (pi < 0x10000) // skips lock words and counters
				continue;
			for (uint32 j = i + 1; j < count && j < 32; j++)
			{
				if (memory_readU32(g_lo + j * 8) == pi)
					dupMask |= (1u << i) | (1u << j);
			}
		}
		const bool isNew = (dupMask & ~s_lastDupMask) != 0;
		s_lastDupMask = dupMask;
		if (!isNew || s_dupLogs >= 8)
			return;
		s_dupLogs++;
		char buf[160];
		cemuLog_log(LogType::Force, "GuestWatch: DUPLICATE pointer entries (mask {:08x}) after write:", dupMask);
		FormatEntry(last, buf, sizeof(buf));
		cemuLog_log(LogType::Force, "  {}", buf);
		std::string line;
		for (uint32 i = 0; i < count && i < 32; i++)
		{
			snprintf(buf, sizeof(buf), "%08x/%08x ", memory_readU32(g_lo + i * 8), memory_readU32(g_lo + i * 8 + 4));
			line += buf;
		}
		cemuLog_log(LogType::Force, "  entries: {}", line);
		cemuLog_log(LogType::Force, "  previous writes:");
		const size_t n = std::min<size_t>(s_ring.size(), std::min<uint64>(s_seq, 96));
		for (size_t k = n; k > 0; k--)
		{
			const Entry& e = s_ring[(s_seq - k) % s_ring.size()];
			FormatEntry(e, buf, sizeof(buf));
			cemuLog_log(LogType::Force, "  {}", buf);
		}
	}

	static void Add(uint32 pc, uint32 lr, uint32 thread, uint32 addr, uint32 size, uint32 value, char kind)
	{
		std::lock_guard _l(s_mutex);
		Entry& e = s_ring[s_seq % s_ring.size()];
		e.seq = s_seq;
		e.pc = pc;
		e.lr = lr;
		e.thread = thread;
		e.addr = addr;
		e.value = value;
		e.size = (uint8)std::min<uint32>(size, 255);
		e.kind = kind;
		s_seq++;
		if (s_logFirstPerPC && s_seenPCs.size() < 256 && s_seenPCs.emplace(((uint64)pc << 32) | addr).second)
		{
			char buf[160];
			FormatEntry(e, buf, sizeof(buf));
			cemuLog_log(LogType::Force, "GuestWatch first: {}", buf);
		}
		if (s_logEach)
		{
			char buf[160];
			FormatEntry(e, buf, sizeof(buf));
			cemuLog_log(LogType::Force, "GuestWatch: {}", buf);
		}
		if (s_pairs)
			CheckPairs(e);
	}

	static uint32 CurrentThread()
	{
		return memory_getVirtualOffsetFromPointer(coreinit::OSGetCurrentThread());
	}

	// callers record after the store, so the pair check sees the new state
	void Record(PPCInterpreter_t* hCPU, uint32 addr, uint32 size, uint32 value, char kind)
	{
		Add(hCPU->instructionPointer, hCPU->spr.LR, CurrentThread(), addr, size, value, kind);
	}

	void RecordHLE(uint32 addr, uint32 size, uint32 value, char kind)
	{
		PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
		Add(hCPU ? hCPU->instructionPointer : 0, hCPU ? hCPU->spr.LR : 0, hCPU ? CurrentThread() : 0, addr, size, value, kind);
	}

	void DumpToCrashLog()
	{
		if (g_hi == 0)
			return;
		std::unique_lock _l(s_mutex, std::try_to_lock); // the crash may have happened while holding it
		CrashLog_WriteLine("");
		char buf[200];
		snprintf(buf, sizeof(buf), "GuestWatch 0x%08x-0x%08x, %llu writes, last %zu:", g_lo, g_hi, (unsigned long long)s_seq, (size_t)std::min<uint64>(s_seq, s_ring.size()));
		CrashLog_WriteLine(buf);
		// whole ring, oldest first, to files/watch_ring.bin (Entry as in GuestWatch.cpp, 32 bytes, little endian)
		const size_t n = std::min<uint64>(s_seq, s_ring.size());
		const std::string binPath = _pathToUtf8(ActiveSettings::GetUserDataPath("watch_ring.bin"));
		if (FILE* f = fopen(binPath.c_str(), "wb"))
		{
			for (size_t k = n; k > 0; k--)
				fwrite(&s_ring[(s_seq - k) % s_ring.size()], sizeof(Entry), 1, f);
			fclose(f);
			snprintf(buf, sizeof(buf), "ring written to %s (%zu entries of %zu bytes)", binPath.c_str(), n, sizeof(Entry));
			CrashLog_WriteLine(buf);
		}
		const size_t shown = std::min<size_t>(n, 64);
		for (size_t k = shown; k > 0; k--)
		{
			FormatEntry(s_ring[(s_seq - k) % s_ring.size()], buf, sizeof(buf));
			CrashLog_WriteLine(buf);
		}
	}
}
