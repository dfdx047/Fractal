#include "Cafe/OS/common/OSCommon.h"
#include "coreinit_Scheduler.h"

// accessed only from the TLS_WORKAROUND_NOINLINE functions below: a PPC thread's fiber can resume on another host
// thread, and code inlined into a caller would keep using the thread pointer read before the switch. On Android the
// library is dlopen'ed (dynamic TLS, per-thread offsets), so that stale pointer plus the new offset is a wild address:
// Pokkén Tournament crashed in __FSUpdateQueue -> FSUnlockMutex -> __OSLockScheduler when leaving a menu
thread_local sint32 s_schedulerLockCount = 0;

#include <pthread.h>
pthread_mutex_t s_ptmSchedulerLock;

TLS_WORKAROUND_NOINLINE void __OSLockScheduler(void* obj)
{
	pthread_mutex_lock(&s_ptmSchedulerLock);
	s_schedulerLockCount++;
	cemu_assert_debug(s_schedulerLockCount <= 1); // >= 2 should not happen. Scheduler lock does not allow recursion
}

TLS_WORKAROUND_NOINLINE bool __OSHasSchedulerLock()
{
	return s_schedulerLockCount > 0;
}

TLS_WORKAROUND_NOINLINE bool __OSTryLockScheduler(void* obj)
{
	bool r;
	r = pthread_mutex_trylock(&s_ptmSchedulerLock) == 0;
	if (r)
	{
		s_schedulerLockCount++;
		return true;
	}
	return false;
}

TLS_WORKAROUND_NOINLINE void __OSUnlockScheduler(void* obj)
{
	s_schedulerLockCount--;
	cemu_assert_debug(s_schedulerLockCount >= 0);
	pthread_mutex_unlock(&s_ptmSchedulerLock);
}

namespace coreinit
{
	uint32 OSIsInterruptEnabled()
	{
		PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
		if (hCPU == nullptr)
			return 0;

		return hCPU->coreInterruptMask;
	}

	// disables interrupts and scheduling
	uint32 OSDisableInterrupts()
	{
		PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
		if (hCPU == nullptr)
			return 0;
		uint32 prevInterruptMask = hCPU->coreInterruptMask;
		if (hCPU->coreInterruptMask != 0)
		{
			// we have no efficient method to turn off scheduling completely, so instead we just increase the remaining cycles
			if (hCPU->remainingCycles >= 0x40000000)
				cemuLog_log(LogType::Force, "OSDisableInterrupts(): Warning - Interrupts already disabled but the mask was still set? remCycles {:08x} LR {:08x}", hCPU->remainingCycles, hCPU->spr.LR);
			hCPU->remainingCycles += 0x40000000;
		}
		hCPU->coreInterruptMask = 0;
		return prevInterruptMask;
	}

	uint32 OSRestoreInterrupts(uint32 interruptMask)
	{
		PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
		if (hCPU == nullptr)
			return 0;
		uint32 prevInterruptMask = hCPU->coreInterruptMask;
		if (hCPU->coreInterruptMask == 0 && interruptMask != 0)
		{
			hCPU->remainingCycles -= 0x40000000;
		}
		hCPU->coreInterruptMask = interruptMask;
		return prevInterruptMask;
	}

	uint32 OSEnableInterrupts()
	{
		PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
		uint32 prevInterruptMask = hCPU->coreInterruptMask;
		OSRestoreInterrupts(1);
		return prevInterruptMask;
	}

	void InitializeSchedulerLock()
	{
		pthread_mutexattr_t ma;
		pthread_mutexattr_init(&ma);
		pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&s_ptmSchedulerLock, &ma);
		cafeExportRegister("coreinit", __OSLockScheduler, LogType::Placeholder);
		cafeExportRegister("coreinit", __OSUnlockScheduler, LogType::Placeholder);

		cafeExportRegister("coreinit", OSDisableInterrupts, LogType::CoreinitThread);
		cafeExportRegister("coreinit", OSEnableInterrupts, LogType::CoreinitThread);
		cafeExportRegister("coreinit", OSRestoreInterrupts, LogType::CoreinitThread);
	}
};
