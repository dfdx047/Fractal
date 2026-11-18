#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteDraw.h"
#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/HW/Latte/Core/LattePM4.h"
#include "Cafe/OS/libs/coreinit/coreinit.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "Cafe/OS/libs/TCL/TCL.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "GX2.h"
#include "GX2_Command.h"
#include "Cafe/HW/Latte/Core/LattePerfLog.h"
#include "GX2_Shader.h"
#include "GX2_Misc.h"
#include "OS/libs/coreinit/coreinit_MEM.h"

namespace GX2
{
	GX2PerCoreCBState s_perCoreCBState[Espresso::CORE_COUNT];
}

void gx2WriteGather_submitU32AsBE(uint32 v)
{
	uint32 coreIndex = PPCInterpreter_getCoreIndex(PPCInterpreter_getCurrentInstance());
	if (GX2::s_perCoreCBState[coreIndex].currentWritePtr == nullptr)
		return;
	*(uint32*)(GX2::s_perCoreCBState[coreIndex].currentWritePtr) = _swapEndianU32(v);
	GX2::s_perCoreCBState[coreIndex].currentWritePtr++;
	cemu_assert_debug(GX2::s_perCoreCBState[coreIndex].currentWritePtr <= (GX2::s_perCoreCBState[coreIndex].bufferPtr + GX2::s_perCoreCBState[coreIndex].bufferSizeInU32s));
}

void gx2WriteGather_submitU32AsLE(uint32 v)
{
	uint32 coreIndex = PPCInterpreter_getCoreIndex(PPCInterpreter_getCurrentInstance());
	if (GX2::s_perCoreCBState[coreIndex].currentWritePtr == nullptr)
		return;
	*(uint32*)(GX2::s_perCoreCBState[coreIndex].currentWritePtr) = v;
	GX2::s_perCoreCBState[coreIndex].currentWritePtr++;
	cemu_assert_debug(GX2::s_perCoreCBState[coreIndex].currentWritePtr <= (GX2::s_perCoreCBState[coreIndex].bufferPtr + GX2::s_perCoreCBState[coreIndex].bufferSizeInU32s));
}

void gx2WriteGather_submitU32AsLEArray(uint32* v, uint32 numValues)
{
	uint32 coreIndex = PPCInterpreter_getCoreIndex(PPCInterpreter_getCurrentInstance());
	if (GX2::s_perCoreCBState[coreIndex].currentWritePtr == nullptr)
		return;
	memcpy_dwords(GX2::s_perCoreCBState[coreIndex].currentWritePtr, v, numValues);
	GX2::s_perCoreCBState[coreIndex].currentWritePtr += numValues;
	cemu_assert_debug(GX2::s_perCoreCBState[coreIndex].currentWritePtr <= (GX2::s_perCoreCBState[coreIndex].bufferPtr + GX2::s_perCoreCBState[coreIndex].bufferSizeInU32s));
}

namespace GX2
{

	struct GX2CommandState // mapped to PPC space since the GPU writes here
    {
    	// command pool
		MEMPTR<uint32be> commandPoolBase;
    	uint32 commandPoolSizeInU32s;
		MEMPTR<uint32be> gpuCommandReadPtr;
		// timestamp
		uint64be lastSubmissionTime;
    };

	CafeStaticVar<GX2CommandState> s_commandState{s_gx2StaticData};
	GX2PerCoreCBState s_mainCoreLastCommandState;
	bool s_cbBufferIsInternallyAllocated;

	void GX2Command_StartNewCommandBuffer(uint32 numU32s);

	// called from GX2Init. Allocates a 4MB memory chunk from which command buffers are suballocated from
	void GX2Init_commandBufferPool(void* bufferBase, uint32 bufferSize)
	{
		cemu_assert_debug(!s_commandState->commandPoolBase); // should not be allocated already
    	// setup command buffer pool. If not provided allocate a 4MB or custom size buffer
		uint32 poolSize = bufferSize ? bufferSize : 0x400000; // 4MB (can be overwritten by custom GX2Init parameters?)
		if (bufferBase)
		{
			s_commandState->commandPoolBase = (uint32be*)bufferBase;
			s_cbBufferIsInternallyAllocated = false;
		}
		else
		{
			s_commandState->commandPoolBase = (uint32be*)coreinit::_weak_MEMAllocFromDefaultHeapEx(poolSize, 0x100);
			s_cbBufferIsInternallyAllocated = true;
		}
		if (!s_commandState->commandPoolBase)
		{
			cemuLog_log(LogType::Force, "GX2: Failed to allocate command buffer pool");
		}
		s_commandState->commandPoolSizeInU32s = poolSize / sizeof(uint32be);
		s_commandState->gpuCommandReadPtr = s_commandState->commandPoolBase;
		// init per-core command buffer state
		for (uint32 i = 0; i < Espresso::CORE_COUNT; i++)
		{
			s_perCoreCBState[i].bufferPtr = nullptr;
			s_perCoreCBState[i].bufferSizeInU32s = 0;
			s_perCoreCBState[i].currentWritePtr = nullptr;
		}
		// start first command buffer for main core
		GX2Command_StartNewCommandBuffer(0x100);
	}

	void GX2Shutdown_commandBufferPool()
	{
		if (!s_commandState->commandPoolBase)
			return;
		if (s_cbBufferIsInternallyAllocated)
			coreinit::_weak_MEMFreeToDefaultHeap(s_commandState->commandPoolBase.GetPtr());
		s_cbBufferIsInternallyAllocated = false;
		s_commandState->commandPoolBase = nullptr;
		s_commandState->commandPoolSizeInU32s = 0;
		s_commandState->gpuCommandReadPtr = nullptr;
	}

	// current position of where the GPU is reading from. Updated via a memory write command submitted to the GPU
	uint32 GX2Command_GetPoolGPUReadIndex()
	{
		stdx::atomic_ref<MEMPTR<uint32be>> _readPtr(s_commandState->gpuCommandReadPtr);
		MEMPTR<uint32be> currentReadPtr = _readPtr.load();
		cemu_assert_debug(currentReadPtr);
		return (uint32)(currentReadPtr.GetPtr() - s_commandState->commandPoolBase.GetPtr());
	}

	void GX2Command_WaitForNextBufferRetired()
	{
		uint64 retiredTimeStamp = GX2GetRetiredTimeStamp();
		retiredTimeStamp += 1;
		// but cant be higher than the submission timestamp
		stdx::atomic_ref<uint64be> _lastSubmissionTime(s_commandState->lastSubmissionTime);
		uint64 submissionTimeStamp = _lastSubmissionTime.load();
		if (retiredTimeStamp > submissionTimeStamp)
			retiredTimeStamp = submissionTimeStamp;
		GX2WaitTimeStamp(retiredTimeStamp);
	}

	void GX2Command_SetupCoreCommandBuffer(uint32be* buffer, uint32 sizeInU32s, bool isDisplayList)
	{
		uint32 coreIndex = coreinit::OSGetCoreId();
		auto& coreCBState = s_perCoreCBState[coreIndex];
		coreCBState.bufferPtr = buffer;
		coreCBState.bufferSizeInU32s = sizeInU32s;
		coreCBState.currentWritePtr = buffer;
		coreCBState.isDisplayList = isDisplayList;
	}

	void GX2Command_StartNewCommandBuffer(uint32 numU32s)
	{
		// On submission command buffers are padded to 32 byte alignment
		// but nowhere is it guaranteed that internal command buffers have their size aligned to 32 byte (even on console, but testing is required)
		// Thus the padding can write out of bounds but this seems to trigger only very rarely in partice. As a workaround we always pad the command buffer size to 32 bytes here
		numU32s = (numU32s + 7) & ~0x7;

		uint32 coreIndex = coreinit::OSGetCoreId();
		auto& coreCBState = s_perCoreCBState[coreIndex];
		numU32s = std::max<uint32>(numU32s, 0x100);
		// grab space from command buffer pool and if necessary wait for it
		uint32be* bufferPtr = nullptr;
		uint32 bufferSizeInU32s = 0;
		uint32 readIndex;
		while (true)
		{
			// try to grab buffer data from first available spot:
			// 1. At the current write location up to the end of the buffer (avoiding an overlap with the read location)
			// 2. From the start of the buffer up to the read location
			readIndex = GX2Command_GetPoolGPUReadIndex();
			uint32be* nextWritePos = coreCBState.bufferPtr ? coreCBState.bufferPtr + coreCBState.bufferSizeInU32s : s_commandState->commandPoolBase.GetPtr();
			uint32 writeIndex = nextWritePos - s_commandState->commandPoolBase;
			uint32 poolSizeInU32s = s_commandState->commandPoolSizeInU32s;
			// readIndex == writeIndex can mean either buffer full or buffer empty
			// we could use GX2GetRetiredTimeStamp() == GX2GetLastSubmittedTimeStamp() to determine if the buffer is truly empty
			// but this can have false negatives since the last submission timestamp is updated independently of the read index
			// so instead we just avoid ever filling the buffer completely
			cemu_assert_debug(readIndex < poolSizeInU32s);
			cemu_assert_debug(writeIndex < poolSizeInU32s);
			if (writeIndex < readIndex)
			{
				// writeIndex has wrapped around
				uint32 wordsAvailable = readIndex - writeIndex;
				if (wordsAvailable > 0)
					wordsAvailable--; // avoid writeIndex becoming equal to readIndex
				if (wordsAvailable >= numU32s)
				{
					bufferPtr = s_commandState->commandPoolBase + writeIndex;
					bufferSizeInU32s = wordsAvailable;
					break;
				}
			}
			else
			{
				uint32 wordsAvailable = poolSizeInU32s - writeIndex;
				if (wordsAvailable > 0)
					wordsAvailable--; // avoid writeIndex becoming equal to readIndex
				if (wordsAvailable >= numU32s)
				{
					bufferPtr = nextWritePos;
					bufferSizeInU32s = wordsAvailable;
					break;
				}
				// not enough space at end of buffer, try to grab from the beginning of the buffer
				wordsAvailable = readIndex;
				if (wordsAvailable > 0)
					wordsAvailable--; // avoid writeIndex becoming equal to readIndex
				if (wordsAvailable >= numU32s)
				{
					bufferPtr = s_commandState->commandPoolBase;
					bufferSizeInU32s = wordsAvailable;
					break;
				}
			}
			GX2Command_WaitForNextBufferRetired();
		}
		cemu_assert_debug(bufferPtr);
		bufferSizeInU32s = std::min<uint32>(numU32s, 0x20000); // size cap
#ifdef CEMU_DEBUG_ASSERT
		uint32 newWriteIndex = ((bufferPtr - s_commandState->commandPoolBase) + bufferSizeInU32s) % s_commandState->commandPoolSizeInU32s;
		cemu_assert_debug(newWriteIndex != readIndex);
#endif
		// setup buffer and make it the current write gather target
		cemu_assert_debug(bufferPtr >= s_commandState->commandPoolBase && (bufferPtr + bufferSizeInU32s) <= s_commandState->commandPoolBase + s_commandState->commandPoolSizeInU32s);
		GX2Command_SetupCoreCommandBuffer(bufferPtr, bufferSizeInU32s, false);
	}

	void GX2Command_SubmitCommandBuffer(uint32be* buffer, uint32 sizeInU32s, MEMPTR<uint32be>* completionGPUReadPointer, bool triggerMarkerInterrupt)
	{
		uint32be cmd[10];
		uint32 cmdLen = 4;
		cmd[0] = pm4HeaderType3(IT_INDIRECT_BUFFER_PRIV, 3);
		cmd[1] = memory_virtualToPhysical(MEMPTR<void>(buffer).GetMPTR());
		cmd[2] = 0x00000000; // address high bits
		cmd[3] = sizeInU32s;
		if (completionGPUReadPointer)
		{
			// append command to update completionGPUReadPointer after the GPU is done with the command buffer
			cmd[4] = pm4HeaderType3(IT_MEM_WRITE, 4);
			cmd[5] = memory_virtualToPhysical(MEMPTR<void>(completionGPUReadPointer).GetMPTR()) | 2;
			cmd[6] = 0x40000;
			cmd[7] = MEMPTR<void>(buffer + sizeInU32s).GetMPTR(); // value to write
			cmd[8] = 0x00000000;
			cmdLen = 9;
		}

		betype<TCL::TCLSubmissionFlag> submissionFlags{};
		if (!triggerMarkerInterrupt)
			submissionFlags |= TCL::TCLSubmissionFlag::NO_MARKER_INTERRUPT;
		submissionFlags |= TCL::TCLSubmissionFlag::USE_RETIRED_MARKER;

		TCL::TCLSubmitToRing(cmd, cmdLen, &submissionFlags, &s_commandState->lastSubmissionTime);
	}

	void GX2Command_PadCurrentBuffer()
	{
		uint32 coreIndex = coreinit::OSGetCoreId();
		auto& coreCBState = s_perCoreCBState[coreIndex];
		if (!coreCBState.currentWritePtr)
			return;
		uint32 writeDistance = (uint32)(coreCBState.currentWritePtr - coreCBState.bufferPtr);
		if ((writeDistance&7) != 0)
		{
			uint32 distanceToPad = 0x8 - (writeDistance & 0x7);
			while (distanceToPad)
			{
				*coreCBState.currentWritePtr = pm4HeaderType2Filler();
				coreCBState.currentWritePtr++;
				distanceToPad--;
			}
		}
	}

	void GX2Command_Flush(uint32 numU32sForNextBuffer, bool triggerMarkerInterrupt)
	{
		uint32 coreIndex = coreinit::OSGetCoreId();
		auto& coreCBState = s_perCoreCBState[coreIndex];
		if (coreCBState.isDisplayList)
		{
			// display list
			cemu_assert_debug((uint32)(coreCBState.currentWritePtr - coreCBState.bufferPtr) < coreCBState.bufferSizeInU32s);
			cemuLog_logDebugOnce(LogType::Force, "GX2 flush called on display list");
		}
		else
		{
			// command buffer
			if (coreCBState.currentWritePtr != coreCBState.bufferPtr)
			{
				// pad the command buffer to 32 byte alignment
				GX2Command_PadCurrentBuffer();
				// submit it to the GPU
				uint32 bufferLength = (uint32)(coreCBState.currentWritePtr - coreCBState.bufferPtr);
				cemu_assert_debug(bufferLength <= coreCBState.bufferSizeInU32s);
				GX2Command_SubmitCommandBuffer(coreCBState.bufferPtr, bufferLength, &s_commandState->gpuCommandReadPtr, triggerMarkerInterrupt);
				GX2Command_StartNewCommandBuffer(numU32sForNextBuffer);
			}
			else
			{
				// current buffer is empty so we dont need to queue it
				if (numU32sForNextBuffer > coreCBState.bufferSizeInU32s)
					GX2Command_StartNewCommandBuffer(numU32sForNextBuffer);
			}
		}
	}

	void GX2Flush()
	{
		GX2Command_Flush(256, true);
	}

	uint64 GX2GetLastSubmittedTimeStamp()
	{
		stdx::atomic_ref<uint64be> _lastSubmissionTime(s_commandState->lastSubmissionTime);
		return _lastSubmissionTime.load();
	}

	// GX2SubmitUserTimeStamp calls per destination address, for hang dumps (a game polling a user timestamp that was never
	// submitted). A small table, written under a spinlock by whichever core submits; read without it by the dump
	struct UserTimeStampNote
	{
		MPTR addr;
		uint32 type;
		uint64 value;
		uint32 core;
		bool inDisplayList;
		uint64 count;
		uint64 lastCall;
		MPTR dlBase; // display list the last call was recorded into
	};
	// recent display list events in call order (begin, end, call inside a list or into the command buffer, direct submit,
	// user timestamp of type 2 recorded into a list), for hang dumps
	struct DisplayListEvent
	{
		char kind; // B begin, E end, C call, D direct, T timestamp
		uint8 core;
		bool inDisplayList; // for C: written into the list being recorded on that core
		MPTR addr;
		uint32 size; // T: low word of the value
		uint64 call;
	};
	static DisplayListEvent s_dlEvents[4096];
	static uint32 s_dlEventCount = 0;
	static UserTimeStampNote s_userTsNotes[16];
	static std::atomic_flag s_userTsLock = ATOMIC_FLAG_INIT;
	static uint64 s_userTsCalls = 0;

	void GX2Command_NoteUserTimeStamp(MPTR addr, uint64 value, uint32 type)
	{
		const uint32 core = coreinit::OSGetCoreId();
		while (s_userTsLock.test_and_set(std::memory_order_acquire)) {}
		s_userTsCalls++;
		UserTimeStampNote* slot = nullptr;
		for (auto& e : s_userTsNotes)
		{
			if (e.addr == addr || e.addr == MPTR_NULL)
			{
				slot = &e;
				break;
			}
		}
		if (!slot) // table full: replace the least recently used address
		{
			slot = &s_userTsNotes[0];
			for (auto& e : s_userTsNotes)
				if (e.lastCall < slot->lastCall)
					slot = &e;
			slot->count = 0;
		}
		slot->addr = addr;
		slot->type = type;
		slot->value = value;
		slot->core = core;
		slot->inDisplayList = s_perCoreCBState[core].isDisplayList;
		slot->count++;
		slot->lastCall = s_userTsCalls;
		slot->dlBase = slot->inDisplayList ? memory_getVirtualOffsetFromPointer(s_perCoreCBState[core].bufferPtr) : MPTR_NULL;
		if (type == 2)
			s_dlEvents[s_dlEventCount++ % 4096] = {'T', (uint8)core, slot->inDisplayList, addr, (uint32)value, s_userTsCalls};
		s_userTsLock.clear(std::memory_order_release);
	}

	MPTR GX2Command_ReadPointerPhysAddress()
	{
		return s_commandState ? memory_virtualToPhysical(MEMPTR<void>(&s_commandState->gpuCommandReadPtr).GetMPTR()) : MPTR_NULL;
	}

	bool GX2Command_CpEvents()
	{
		static const bool s_on = PerfLog::Tunable("cp_events", 0) != 0; // read once the tuning file is loaded
		return s_on;
	}

	void GX2Command_NoteGpuEvent(char kind, MPTR addr, uint32 value)
	{
		while (s_userTsLock.test_and_set(std::memory_order_acquire)) {}
		s_userTsCalls++;
		s_dlEvents[s_dlEventCount++ % 4096] = {kind, 9, false, addr, value, s_userTsCalls};
		s_userTsLock.clear(std::memory_order_release);
	}

	static void GX2Command_NoteDisplayListEvent(char kind, MPTR addr, uint32 size)
	{
		const uint32 core = coreinit::OSGetCoreId();
		while (s_userTsLock.test_and_set(std::memory_order_acquire)) {}
		s_userTsCalls++;
		s_dlEvents[s_dlEventCount++ % 4096] = {kind, (uint8)core, s_perCoreCBState[core].isDisplayList, addr, size, s_userTsCalls};
		s_userTsLock.clear(std::memory_order_release);
	}

	std::string GX2Command_DescribePending()
	{
		std::string s = "GX2 unsubmitted words per core:";
		for (uint32 c = 0; c < Espresso::CORE_COUNT; c++)
		{
			const auto& st = s_perCoreCBState[c];
			s += fmt::format(" [{}{}]", st.currentWritePtr ? (uint32)(st.currentWritePtr - st.bufferPtr) : 0, st.isDisplayList ? " DL" : "");
		}
		s += fmt::format("; user timestamps ({} calls, value / memory as the game reads it, call age):", s_userTsCalls);
		for (const auto& e : s_userTsNotes)
		{
			if (e.addr == MPTR_NULL)
				continue;
			uint64 mem = 0;
			if (memory_isAddressRangeAccessible(e.addr, 8))
			{
				const uint32be* w = (const uint32be*)memory_getPointerFromVirtualOffset(e.addr);
				mem = ((uint64)(uint32)w[1] << 32) | (uint32)w[0]; // IT_MEM_WRITE stores the low word first
			}
			s += fmt::format(" [{:08x} type {} core {}{} n {} last {:x} mem {:x} age {}", e.addr, e.type, e.core, e.inDisplayList ? " DL" : "",
				e.count, e.value, mem, s_userTsCalls - e.lastCall);
			if (e.dlBase != MPTR_NULL)
				s += fmt::format(" in dl {:08x}", e.dlBase);
			s += "]";
		}
		// B<core> base, E<core> base size, C<core>[>] target size ('>' = written into the list being recorded), D<core> base size,
		// T<core> address value
		s += "; display list events (oldest first):";
		const uint32 shown = GX2Command_CpEvents() ? 1500 : 128;
		for (uint32 k = s_dlEventCount > shown ? s_dlEventCount - shown : 0; k < s_dlEventCount; k++)
		{
			const auto& d = s_dlEvents[k % 4096];
			s += fmt::format(" {}{}{}:{:08x}/{:x}", d.kind, d.core, (d.kind == 'C' && d.inDisplayList) ? ">" : "", d.addr, d.size);
		}
		return s;
	}

	uint64 GX2GetRetiredTimeStamp()
	{
		uint64be ts = 0;
		TCL::TCLTimestamp(TCL::TCLTimestampId::TIMESTAMP_LAST_BUFFER_RETIRED, &ts);
		return ts;
	}

	bool GX2WaitTimeStamp(uint64 tsWait)
	{
		// handle GPU timeout here? But for now we timeout after 60 seconds
		const auto waitStart = std::chrono::steady_clock::now();
		TCL::TCLWaitTimestamp(TCL::TCLTimestampId::TIMESTAMP_LAST_BUFFER_RETIRED, tsWait, Espresso::TIMER_CLOCK * 60);
		PerfLog::GuestWaited(PerfLog::GuestWait::Timestamp, std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - waitStart).count());
		return true;
	}

	/*
	 * Guarantees that the requested amount of space is available on the current command buffer
	 * If the space is not available, the current command buffer is pushed to the GPU and a new one is allocated
	 */
	void GX2ReserveCmdSpace(uint32 reservedFreeSpaceInU32)
	{
		uint32 coreIndex = coreinit::OSGetCoreId();
		auto& coreCBState = s_perCoreCBState[coreIndex];
		if (coreCBState.currentWritePtr == nullptr)
			return;
		uint32 writeDistance = (uint32)(coreCBState.currentWritePtr - coreCBState.bufferPtr);
		if (writeDistance + reservedFreeSpaceInU32 > coreCBState.bufferSizeInU32s)
		{
			GX2Command_Flush(reservedFreeSpaceInU32, true);
		}
	}

	static std::string _CurrentThreadName()
	{
		OSThread_t* t = coreinit::OSGetCurrentThread();
		return (t && !t->threadName.IsNull()) ? std::string(t->threadName.GetPtr()) : std::string("?");
	}

	void GX2WriteGather_beginDisplayList(PPCInterpreter_t* hCPU, MPTR buffer, uint32 maxSize)
	{
		uint32 coreIndex = PPCInterpreter_getCoreIndex(hCPU);
		// diagnostics: a display list begun while another one is being recorded on the same core replaces it, and its end
		// does not return to the outer one (job schedulers run nested jobs while they wait)
		static std::atomic<bool> s_nestedReported{false};
		if (s_perCoreCBState[coreIndex].isDisplayList && !s_nestedReported.exchange(true))
			PerfLog::Event("gx2_dl_nested", fmt::format("display list begun on core {} while another is recorded ({}): the outer one loses its commands", coreIndex, _CurrentThreadName()));
		if (coreIndex == sGX2MainCoreIndex)
		{
			GX2Command_PadCurrentBuffer();
			cemu_assert_debug(!s_perCoreCBState[coreIndex].isDisplayList);
			s_mainCoreLastCommandState = s_perCoreCBState[coreIndex];
		}
		GX2Command_SetupCoreCommandBuffer(MEMPTR<uint32be>(buffer), maxSize/4, true);
		GX2Command_NoteDisplayListEvent('B', buffer, maxSize);
	}

	uint32 GX2WriteGather_endDisplayList(PPCInterpreter_t* hCPU, MPTR buffer)
	{
		uint32 coreIndex = coreinit::OSGetCoreId();
		auto& coreCBState = s_perCoreCBState[coreIndex];
		GX2Command_PadCurrentBuffer();
		uint32 finalWriteIndex = coreCBState.currentWritePtr - coreCBState.bufferPtr;
		GX2Command_NoteDisplayListEvent('E', memory_getVirtualOffsetFromPointer(coreCBState.bufferPtr), finalWriteIndex * 4);
		cemu_assert_debug(finalWriteIndex <= coreCBState.bufferSizeInU32s);
		// if we are on the main GX2 core then restore the GPU command buffer
		if (coreIndex == sGX2MainCoreIndex)
		{
  			coreCBState = s_mainCoreLastCommandState;
		}
		else
		{
			coreCBState.bufferPtr = nullptr;
			coreCBState.currentWritePtr = nullptr;
			coreCBState.bufferSizeInU32s = 0;
			coreCBState.isDisplayList = false;
		}
		return finalWriteIndex * 4;
	}

	bool GX2GetCurrentDisplayList(MEMPTR<uint32be>* displayListAddr, uint32be* displayListSize)
	{
		uint32 coreIndex = coreinit::OSGetCoreId();
		auto& coreCBState = s_perCoreCBState[coreIndex];
		if (!coreCBState.isDisplayList)
			return false;
		if (displayListAddr)
			*displayListAddr = coreCBState.bufferPtr;
		if (displayListSize)
			*displayListSize = coreCBState.bufferSizeInU32s * sizeof(uint32be);
		return true;
	}

	// returns true if we are writing to a display list
	bool GX2GetDisplayListWriteStatus()
	{
		uint32 coreIndex = coreinit::OSGetCoreId();
		return s_perCoreCBState[coreIndex].isDisplayList;
	}

	void GX2BeginDisplayList(MEMPTR<void> displayListAddr, uint32 size)
	{
		GX2WriteGather_beginDisplayList(PPCInterpreter_getCurrentInstance(), displayListAddr.GetMPTR(), size);
	}

	void GX2BeginDisplayListEx(MEMPTR<void> displayListAddr, uint32 size, bool profiling)
	{
		GX2WriteGather_beginDisplayList(PPCInterpreter_getCurrentInstance(), displayListAddr.GetMPTR(), size);
	}

	uint32 GX2EndDisplayList(MEMPTR<void> displayListAddr)
	{
		cemu_assert_debug(displayListAddr != nullptr);
		uint32 displayListSize = GX2WriteGather_endDisplayList(PPCInterpreter_getCurrentInstance(), displayListAddr.GetMPTR());
		// the console flushes the list to memory here; on weakly ordered hosts (ARM) a flag the title writes next must not
		// become visible to the GPU thread before the list contents (pairs with the acquire after a CP memory wait)
		std::atomic_thread_fence(std::memory_order_release);
		return displayListSize;
	}

	void GX2CallDisplayList(MPTR addr, uint32 size)
	{
		cemu_assert_debug((size&3) == 0);
		GX2Command_NoteDisplayListEvent('C', addr, size);
		// write PM4 command
		GX2ReserveCmdSpace(4);
		gx2WriteGather_submit(pm4HeaderType3(IT_INDIRECT_BUFFER_PRIV, 3),
			memory_virtualToPhysical(addr),
			0, // high address bits
			size / 4);
	}

	void GX2DirectCallDisplayList(void* addr, uint32 size)
	{
		// this API submits to TCL directly and bypasses write-gatherer
		// its basically a way to manually submit a command buffer to the GPU
		uint32 coreIndex = coreinit::OSGetCoreId();
		GX2Command_NoteDisplayListEvent('D', memory_getVirtualOffsetFromPointer(addr), size);
		if (coreIndex != sGX2MainCoreIndex)
		{
			cemuLog_logDebugOnce(LogType::Force, "GX2DirectCallDisplayList() called on non-main GX2 core");
		}
		if (!s_perCoreCBState[coreIndex].isDisplayList)
		{
			static std::atomic<bool> s_coreCommandsReported{false};
			if (coreIndex != sGX2MainCoreIndex && s_perCoreCBState[coreIndex].currentWritePtr != s_perCoreCBState[coreIndex].bufferPtr &&
				!s_coreCommandsReported.exchange(true))
				PerfLog::Event("gx2_core_commands", fmt::format("GX2 commands written outside a display list on core {} ({}), submitted by GX2DirectCallDisplayList", coreIndex, _CurrentThreadName()));
			// make sure any preceeding commands are submitted first
			GX2Command_Flush(0x100, false);
		}
		GX2Command_SubmitCommandBuffer(static_cast<uint32be*>(addr), size / 4, nullptr, false);
	}

	void GX2CopyDisplayList(MEMPTR<uint32be*> addr, uint32 size)
	{
		GX2Command_NoteDisplayListEvent('Y', addr.GetMPTR(), size);
		// copy display list to write gather
		uint32* displayListDWords = (uint32*)addr.GetPtr();
		uint32 dwordCount = size / 4;
		if (dwordCount > 0)
		{
			GX2ReserveCmdSpace(dwordCount);
			gx2WriteGather_submitU32AsLEArray(displayListDWords, dwordCount);
		}
	}

	enum class GX2_PATCH_TYPE : uint32
	{
		FETCH_SHADER			= 1,
		VERTEX_SHADER			= 2,
		GEOMETRY_COPY_SHADER	= 3,
		GEOMETRY_SHADER			= 4,
		PIXEL_SHADER			= 5,
		COMPUTE_SHADER			= 6
	};

	void GX2PatchDisplayList(uint32be* displayData, GX2_PATCH_TYPE patchType, uint32 patchOffset, void* obj)
	{
		cemu_assert_debug((patchOffset & 3) == 0);

		if (patchType == GX2_PATCH_TYPE::VERTEX_SHADER)
		{
			GX2VertexShader* vertexShader = (GX2VertexShader*)obj;
			displayData[patchOffset / 4 + 2] = memory_virtualToPhysical(vertexShader->GetProgramAddr()) >> 8;
		}
		else if (patchType == GX2_PATCH_TYPE::PIXEL_SHADER)
		{
			GX2PixelShader_t* pixelShader = (GX2PixelShader_t*)obj;
			displayData[patchOffset / 4 + 2] = memory_virtualToPhysical(pixelShader->GetProgramAddr()) >> 8;
		}
		else if (patchType == GX2_PATCH_TYPE::FETCH_SHADER)
		{
			GX2FetchShader* fetchShader = (GX2FetchShader*)obj;
			displayData[patchOffset / 4 + 2] = memory_virtualToPhysical(fetchShader->GetProgramAddr()) >> 8;
		}
		else if (patchType == GX2_PATCH_TYPE::GEOMETRY_COPY_SHADER)
		{
			GX2GeometryShader_t* geometryShader = (GX2GeometryShader_t*)obj;
			displayData[patchOffset / 4 + 2] = memory_virtualToPhysical(geometryShader->GetCopyProgramAddr()) >> 8;
		}
		else if (patchType == GX2_PATCH_TYPE::GEOMETRY_SHADER)
		{
			GX2GeometryShader_t* geometryShader = (GX2GeometryShader_t*)obj;
			displayData[patchOffset / 4 + 2] = memory_virtualToPhysical(geometryShader->GetGeometryProgramAddr()) >> 8;
		}
		else
		{
			cemuLog_log(LogType::Force, "GX2PatchDisplayList(): unsupported patchType {}", (uint32)patchType);
			cemu_assert_debug(false);
		}
	}

	void GX2CommandInit()
	{
		for (auto& state : s_perCoreCBState)
			state = {};
		s_mainCoreLastCommandState = {};
		cafeExportRegister("gx2", GX2Flush, LogType::GX2);

		cafeExportRegister("gx2", GX2GetLastSubmittedTimeStamp, LogType::GX2);
		cafeExportRegister("gx2", GX2GetRetiredTimeStamp, LogType::GX2);
		cafeExportRegister("gx2", GX2WaitTimeStamp, LogType::GX2);

		cafeExportRegister("gx2", GX2BeginDisplayList, LogType::GX2);
		cafeExportRegister("gx2", GX2BeginDisplayListEx, LogType::GX2);
		cafeExportRegister("gx2", GX2EndDisplayList, LogType::GX2);

		cafeExportRegister("gx2", GX2GetCurrentDisplayList, LogType::GX2);
		cafeExportRegister("gx2", GX2GetDisplayListWriteStatus, LogType::GX2);

		cafeExportRegister("gx2", GX2CallDisplayList, LogType::GX2);
		cafeExportRegister("gx2", GX2DirectCallDisplayList, LogType::GX2);
		cafeExportRegister("gx2", GX2CopyDisplayList, LogType::GX2);

		cafeExportRegister("gx2", GX2PatchDisplayList, LogType::GX2);
	}

    void GX2CommandResetToDefaultState()
    {
		if (!s_commandState.IsAllocated())
			return;
		s_commandState->commandPoolBase = nullptr;
		s_commandState->commandPoolSizeInU32s = 0;
		s_commandState->gpuCommandReadPtr = nullptr;
		s_cbBufferIsInternallyAllocated = false;
    }

}
