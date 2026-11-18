#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "Cafe/HW/Espresso/PPCState.h"
#include "Cafe/HW/Espresso/Debugger/GDBStub.h"
#include "ExceptionHandler.h"
#include "Cafe/HW/Espresso/Debugger/GuestWatch.h"
#include "Cafe/OS/RPL/rpl_symbol_storage.h"

#include <android/log.h>

bool crashLogCreated = false;

bool CrashLog_Create()
{
    if (crashLogCreated)
        return false; // only create one crashlog
    crashLogCreated = true;
    cemuLog_createLogFile(true);
    CrashLog_SetOutputChannels(true, true);
    return true;
}

static bool s_writeToStdErr{true};
static bool s_writeToLogTxt{true};

void CrashLog_SetOutputChannels(bool writeToStdErr, bool writeToLogTxt)
{
    s_writeToStdErr = writeToStdErr;
    s_writeToLogTxt = writeToLogTxt;
}

// stderr isn't shown anywhere on Android, so the crash log goes to logcat instead, one line at a time
static std::string s_logcatLine;

static void CrashLog_WriteToLogcat(std::string_view text, bool newLine)
{
    s_logcatLine.append(text);
    if (!newLine)
        return;
    std::string_view remaining = s_logcatLine;
    while (true)
    {
        size_t lineEnd = remaining.find('\n');
        std::string line{remaining.substr(0, lineEnd)};
        if (lineEnd == std::string_view::npos)
        {
            if (!line.empty())
                __android_log_write(ANDROID_LOG_ERROR, "Cemu", line.c_str());
            break;
        }
        __android_log_write(ANDROID_LOG_ERROR, "Cemu", line.c_str());
        remaining.remove_prefix(lineEnd + 1);
    }
    s_logcatLine.clear();
}

// outputs to both stderr and log.txt
void CrashLog_WriteLine(std::string_view text, bool newLine)
{
    if(s_writeToLogTxt)
        cemuLog_writeLineToLog(text, false, newLine);
    if(s_writeToStdErr)
    {
        CrashLog_WriteToLogcat(text, newLine);
    }
}

void CrashLog_WriteHeader(const char* header)
{
    CrashLog_WriteLine("-----------------------------------------");
    CrashLog_WriteLine("   ", false);
    CrashLog_WriteLine(header);
    CrashLog_WriteLine("-----------------------------------------");
}

void ExceptionHandler_LogGeneralInfo()
{
    char dumpLine[1024];
    // info about game
    CrashLog_WriteLine("");
    CrashLog_WriteHeader("Game info");
    if (CafeSystem::IsTitleRunning())
    {
        CrashLog_WriteLine("Game: ", false);
        CrashLog_WriteLine(CafeSystem::GetForegroundTitleName());
        // title id
        CrashLog_WriteLine(fmt::format("TitleId: {:016x}", CafeSystem::GetForegroundTitleId()));
        // rpx hash
        sprintf(dumpLine, "RPXHash: %08x (Update: %08x)", CafeSystem::GetRPXHashBase(), CafeSystem::GetRPXHashUpdated());
        CrashLog_WriteLine(dumpLine);
    }
    else
    {
        CrashLog_WriteLine("Not running");
    }
    // info about active PPC instance:
    CrashLog_WriteLine("");
    CrashLog_WriteHeader("Active PPC instance");
	PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
    if (hCPU)
    {
        OSThread_t* currentThread = coreinit::OSGetCurrentThread();
        uint32 threadPtr = memory_getVirtualOffsetFromPointer(coreinit::OSGetCurrentThread());
        sprintf(dumpLine, "IP 0x%08x LR 0x%08x Thread 0x%08x", hCPU->instructionPointer, hCPU->spr.LR, threadPtr);
        CrashLog_WriteLine(dumpLine);

        // GPR info
        CrashLog_WriteLine("");
        auto gprs = hCPU->gpr;
        sprintf(dumpLine, "r0 =%08x r1 =%08x r2 =%08x r3 =%08x r4 =%08x r5 =%08x r6 =%08x r7 =%08x", gprs[0], gprs[1], gprs[2], gprs[3], gprs[4], gprs[5], gprs[6], gprs[7]);
        CrashLog_WriteLine(dumpLine);
        sprintf(dumpLine, "r8 =%08x r9 =%08x r10=%08x r11=%08x r12=%08x r13=%08x r14=%08x r15=%08x", gprs[8], gprs[9], gprs[10], gprs[11], gprs[12], gprs[13], gprs[14], gprs[15]);
        CrashLog_WriteLine(dumpLine);
        sprintf(dumpLine, "r16=%08x r17=%08x r18=%08x r19=%08x r20=%08x r21=%08x r22=%08x r23=%08x", gprs[16], gprs[17], gprs[18], gprs[19], gprs[20], gprs[21], gprs[22], gprs[23]);
        CrashLog_WriteLine(dumpLine);
        sprintf(dumpLine, "r24=%08x r25=%08x r26=%08x r27=%08x r28=%08x r29=%08x r30=%08x r31=%08x", gprs[24], gprs[25], gprs[26], gprs[27], gprs[28], gprs[29], gprs[30], gprs[31]);
        CrashLog_WriteLine(dumpLine);

        // guest code around IP and LR (disassemble offline, e.g. llvm-objdump --triple=powerpc)
        for (MPTR center : {hCPU->instructionPointer, hCPU->spr.LR})
        {
            CrashLog_WriteLine("");
            sprintf(dumpLine, "PPC code around 0x%08x:", center);
            CrashLog_WriteLine(dumpLine);
            const MPTR base = (center & ~3u) - 0x180;
            for (uint32 i = 0; i < 0x200 / 32; i++)
            {
                const MPTR lineAddr = base + i * 32;
                if (!memory_isAddressRangeAccessible(lineAddr, 32))
                    continue;
                sprintf(dumpLine, "code %08x: %08x %08x %08x %08x %08x %08x %08x %08x", lineAddr, memory_readU32(lineAddr + 0), memory_readU32(lineAddr + 4), memory_readU32(lineAddr + 8), memory_readU32(lineAddr + 12), memory_readU32(lineAddr + 16), memory_readU32(lineAddr + 20), memory_readU32(lineAddr + 24), memory_readU32(lineAddr + 28));
                CrashLog_WriteLine(dumpLine);
            }
        }

        // guest data the registers point to (skips code and the stack, which are dumped elsewhere)
        std::vector<MPTR> dumpedData;
        for (uint32 r = 0; r < 32; r++)
        {
            const MPTR value = gprs[r];
            if (r == 1 || value < 0x10000000)
                continue;
            const MPTR base = (value & ~0x1Fu) - 0x40;
            if (std::find(dumpedData.begin(), dumpedData.end(), base) != dumpedData.end() || !memory_isAddressRangeAccessible(base, 0x100))
                continue;
            dumpedData.push_back(base);
            CrashLog_WriteLine("");
            sprintf(dumpLine, "Data around r%u 0x%08x:", r, value);
            CrashLog_WriteLine(dumpLine);
            for (uint32 i = 0; i < 0x100 / 32; i++)
            {
                const MPTR lineAddr = base + i * 32;
                sprintf(dumpLine, "data %08x: %08x %08x %08x %08x %08x %08x %08x %08x", lineAddr, memory_readU32(lineAddr + 0), memory_readU32(lineAddr + 4), memory_readU32(lineAddr + 8), memory_readU32(lineAddr + 12), memory_readU32(lineAddr + 16), memory_readU32(lineAddr + 20), memory_readU32(lineAddr + 24), memory_readU32(lineAddr + 28));
                CrashLog_WriteLine(dumpLine);
            }
        }

        GuestWatch::DumpToCrashLog();

        // stack trace
        MPTR currentStackVAddr = hCPU->gpr[1];
        CrashLog_WriteLine("");
        CrashLog_WriteHeader("PPC stack trace");
        DebugLogStackTrace(currentThread, currentStackVAddr);

        // stack dump
        CrashLog_WriteLine("");
        CrashLog_WriteHeader("PPC stack dump");
        for (uint32 i = 0; i < 16; i++)
        {
            MPTR lineAddr = currentStackVAddr + i * 8 * 4;
            if (memory_isAddressRangeAccessible(lineAddr, 8 * 4))
            {
                sprintf(dumpLine, "[0x%08x] %08x %08x %08x %08x - %08x %08x %08x %08x", lineAddr, memory_readU32(lineAddr + 0), memory_readU32(lineAddr + 4), memory_readU32(lineAddr + 8), memory_readU32(lineAddr + 12), memory_readU32(lineAddr + 16), memory_readU32(lineAddr + 20), memory_readU32(lineAddr + 24), memory_readU32(lineAddr + 28));
                CrashLog_WriteLine(dumpLine);
            }
            else
            {
                sprintf(dumpLine, "[0x%08x] ?", lineAddr);
                CrashLog_WriteLine(dumpLine);
            }
        }
    }
    else
    {
        CrashLog_WriteLine("Not active");
    }

    // PPC thread log
    CrashLog_WriteLine("");
    CrashLog_WriteHeader("PPC threads");
    ExceptionHandler_LogThreadList(false);
}

static thread_local char t_threadNote[640];

void ExceptionHandler_SetThreadNote(const char* note)
{
    if (!note)
    {
        t_threadNote[0] = '\0';
        return;
    }
    strncpy(t_threadNote, note, sizeof(t_threadNote) - 1);
    t_threadNote[sizeof(t_threadNote) - 1] = '\0';
}

const char* ExceptionHandler_GetThreadNote()
{
    return t_threadNote;
}

// the emulated threads: state, where each stopped (IP/LR and the OS call), optionally the stack of every waiting thread.
// Also used without a crash, for games that freeze (perf log tunable hang_dump_s)
void ExceptionHandler_LogThreadList(bool withStacks)
{
    char dumpLine[1024];
    if (activeThreadCount == 0)
    {
        CrashLog_WriteLine("None active");
    }
    for (sint32 i = 0; i < activeThreadCount; i++)
    {
        MPTR threadItrMPTR = activeThread[i];
        OSThread_t* threadItrBE = (OSThread_t*)memory_getPointerFromVirtualOffset(threadItrMPTR);

        // get thread state
        OSThread_t::THREAD_STATE threadState = threadItrBE->state;
        const char* threadStateStr = "UNDEFINED";
        if (threadItrBE->suspendCounter != 0)
            threadStateStr = "SUSPENDED";
        else if (threadState == OSThread_t::THREAD_STATE::STATE_NONE)
            threadStateStr = "NONE";
        else if (threadState == OSThread_t::THREAD_STATE::STATE_READY)
            threadStateStr = "READY";
        else if (threadState == OSThread_t::THREAD_STATE::STATE_RUNNING)
            threadStateStr = "RUNNING";
        else if (threadState == OSThread_t::THREAD_STATE::STATE_WAITING)
            threadStateStr = "WAITING";
        else if (threadState == OSThread_t::THREAD_STATE::STATE_MORIBUND)
            threadStateStr = "MORIBUND";
        // generate log line
        uint8 affinity = threadItrBE->attr;
        sint32 effectivePriority = threadItrBE->effectivePriority;
        const char* threadName = "NULL";
        if (!threadItrBE->threadName.IsNull())
            threadName = threadItrBE->threadName.GetPtr();
        sprintf(dumpLine, "%08x Ent %08x IP %08x LR %08x %-9s Aff %d%d%d Pri %2d Name %s", threadItrMPTR, threadItrBE->entrypoint.GetMPTR(), threadItrBE->context.srr0, _swapEndianU32(threadItrBE->context.lr), threadStateStr, (affinity >> 0) & 1, (affinity >> 1) & 1, (affinity >> 2) & 1, effectivePriority, threadName);
        // name the function the thread stopped in (usually an OS call such as a wait or a yield). In the HLE trampoline area below
        // 0x01000000 only an exact match is meaningful, the closest symbol there belongs to an unrelated stub
        const MPTR ip = threadItrBE->context.srr0;
        if (RPLStoredSymbol* sym = rplSymbolStorage_getByClosestAddress(ip); sym && (ip < 0x01000000 ? ip == sym->address : ip - sym->address < 0x1000))
        {
            size_t len = strlen(dumpLine);
            snprintf(dumpLine + len, sizeof(dumpLine) - len, " in %s.%s+0x%x", (const char*)sym->libName, (const char*)sym->symbolName, ip - sym->address);
        }
        // the caller of that OS call
        const MPTR lr = _swapEndianU32(threadItrBE->context.lr);
        if (RPLStoredSymbol* sym = rplSymbolStorage_getByClosestAddress(lr); sym && lr >= 0x01000000 && lr - sym->address < 0x4000)
        {
            size_t len = strlen(dumpLine);
            snprintf(dumpLine + len, sizeof(dumpLine) - len, " from %s+0x%x", (const char*)sym->symbolName, lr - sym->address);
        }
        // waits: the object passed to the OS call (r3) and its first words, e.g. an OSEvent's value shows a lost wakeup
        if (threadState == OSThread_t::THREAD_STATE::STATE_WAITING && withStacks)
        {
            const uint32 r3 = _swapEndianU32(threadItrBE->context.gpr[3]);
            size_t len = strlen(dumpLine);
            len += snprintf(dumpLine + len, sizeof(dumpLine) - len, " r3 %08x r4 %08x", r3, _swapEndianU32(threadItrBE->context.gpr[4]));
            if (r3 >= 0x01000000 && r3 < 0xFFFFFF00 && memory_isAddressRangeAccessible(r3, 32))
            {
                const uint32be* w = (const uint32be*)memory_getPointerFromVirtualOffset(r3);
                snprintf(dumpLine + len, sizeof(dumpLine) - len, " [%08x %08x %08x %08x %08x %08x %08x %08x]", (uint32)w[0], (uint32)w[1],
                    (uint32)w[2], (uint32)w[3], (uint32)w[4], (uint32)w[5], (uint32)w[6], (uint32)w[7]);
            }
        }
        // write line to log
        CrashLog_WriteLine(dumpLine);
        // hang dumps: the call chain that led each waiting thread into its wait
        if (withStacks && (threadState == OSThread_t::THREAD_STATE::STATE_WAITING || threadState == OSThread_t::THREAD_STATE::STATE_READY))
            DebugLogStackTrace(threadItrBE, _swapEndianU32(threadItrBE->context.gpr[1]));
    }
}
