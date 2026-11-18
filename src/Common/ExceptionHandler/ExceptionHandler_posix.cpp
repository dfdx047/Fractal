#include <signal.h>
#include <sys/uio.h>
#include <unistd.h>
#include <execinfo.h>
#include <string.h>
#include <string>
#include "config/CemuConfig.h"
#include "util/helpers/StringHelpers.h"
#include "ExceptionHandler.h"
#include "Cafe/HW/Espresso/Debugger/GuestWatch.h"

#include "Cafe/HW/Espresso/Debugger/GDBStub.h"
#include "Cafe/HW/Espresso/Debugger/GDBBreakpoints.h"


#include <boost/stacktrace.hpp>
#include <dlfcn.h>

// the handlers that were installed before ours (Android's debuggerd), so a crash still produces a tombstone
static struct sigaction s_previousActions[NSIG];


// handle signals that would dump core, print stacktrace and then dump depending on config
void handlerDumpingSignal(int sig, siginfo_t *info, void *context)
{
	// GuestWatch watch_jit: stores to its read-only pages are logged and performed there
	if (sig == SIGSEGV && GuestWatch::HandleFault(info->si_addr, context))
		return;

    if(!CrashLog_Create())
        return; // give up if crashlog was already created

    char* sigName = strsignal(sig);
	if (sigName)
	{
		printf("%s!\n", sigName);
	}
	else
	{
		// should never be the case
		printf("Unknown core dumping signal!\n");
	}
    CrashLog_WriteLine(fmt::format("Error: signal {}:", sig));
    {
        // the address of the faulting instruction, as library + offset when it's inside a library
        uint64 pc = ((ucontext_t*)context)->uc_mcontext.pc;
        Dl_info dlInfo{};
        std::string location = "outside of any library (recompiled code?)";
        if (dladdr((void*)pc, &dlInfo) && dlInfo.dli_fname)
            location = fmt::format("{} + 0x{:x}", dlInfo.dli_fname, pc - (uint64)dlInfo.dli_fbase);
        CrashLog_WriteLine(fmt::format("PC 0x{:016x} ({}), fault address 0x{:016x}, code {}", pc, location, (uint64)info->si_addr, info->si_code));
        if (const char* note = ExceptionHandler_GetThreadNote(); note && note[0])
            CrashLog_WriteLine(fmt::format("Crashed while: {}", note));
        // host registers, and for recompiled code the instructions around the PC (they are not in any file)
        const auto& mc = ((ucontext_t*)context)->uc_mcontext;
        for (int i = 0; i < 31; i += 4)
        {
            std::string line;
            for (int r = i; r < i + 4 && r < 31; r++)
                line += fmt::format("x{:<2}={:016x} ", r, (uint64)mc.regs[r]);
            CrashLog_WriteLine(line);
        }
        CrashLog_WriteLine(fmt::format("sp ={:016x}", (uint64)mc.sp));
        // the caller: with a call through a null function pointer (PC 0) it is the only hint where the crash came from
        {
            uint64 lr = (uint64)mc.regs[30];
            Dl_info lrInfo{};
            if (dladdr((void*)lr, &lrInfo) && lrInfo.dli_fname)
                CrashLog_WriteLine(fmt::format("LR 0x{:016x} ({} + 0x{:x}{}{})", lr, lrInfo.dli_fname, lr - (uint64)lrInfo.dli_fbase,
                    lrInfo.dli_sname ? ", " : "", lrInfo.dli_sname ? lrInfo.dli_sname : ""));
            else
                CrashLog_WriteLine(fmt::format("LR 0x{:016x} (outside of any library)", lr));
        }
        // only when the code around the PC can be read, a bad PC would fault again here and lose the backtrace
        uint32 code[96];
        iovec local{code, sizeof(code)}, remote{(void*)(pc - 64 * 4), sizeof(code)};
        if (!dlInfo.dli_fname && (pc & 3) == 0 && pc >= 0x10000 && process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)sizeof(code))
        {
            for (int row = 0; row < 96; row += 8)
            {
                std::string line = fmt::format("host {:016x}:", (uint64)(pc - 64 * 4 + row * 4));
                for (int k = 0; k < 8; k++)
                    line += fmt::format(" {:08x}", code[row + k]);
                CrashLog_WriteLine(line);
            }
        }
    }
    CrashLog_WriteLine(to_string(boost::stacktrace::stacktrace()));

    std::cerr << fmt::format("\nStacktrace and additional info written to:") << std::endl;
    std::cerr << cemuLog_GetLogFilePath().generic_string() << std::endl;

    // log.txt can't be opened by the user on Android, so the game and PPC info goes to logcat too
    ExceptionHandler_LogGeneralInfo();
    // the log is written by a background thread, let it finish before the process ends
    cemuLog_waitForFlush(std::chrono::milliseconds(500));
    // hand the signal to Android's handler, which writes a tombstone with the native backtrace
    sigaction(sig, &s_previousActions[sig], nullptr);
    if (info->si_code <= 0)
        raise(sig); // sent by abort() or kill, it has to be sent again
    // a fault happens again when the faulting instruction runs again, this time handled by Android
    return;

    CrashLog_SetOutputChannels(false, true);
    ExceptionHandler_LogGeneralInfo();
    CrashLog_SetOutputChannels(true, true);

	if (GetConfig().crash_dump == CrashDump::Enabled)
	{
		// reset signal handler to default and re-raise signal to dump core
		signal(sig, SIG_DFL);
		raise(sig);
		return;
	}
	// exit process ignoring all issues
	_Exit(1);
}

void handler_SIGINT(int sig)
{
	/*
	 * Received when pressing CTRL + C in a console
	 * Ideally should be exiting cleanly after saving settings but currently
	 * there's no clean exit pathway (at least on linux) and exiting the app
	 * by any mean ends up with a SIGABRT from the standard library destroying
	 * threads.
	 */
	_Exit(0);
}

void ExceptionHandler_Init()
{
	struct sigaction action;
	action.sa_flags = 0;
	sigfillset(&action.sa_mask); // don't allow signals to be interrupted

	action.sa_handler = handler_SIGINT;
	sigaction(SIGINT, &action, nullptr);
	sigaction(SIGTERM, &action, nullptr);

    action.sa_flags = SA_SIGINFO;
    action.sa_handler = nullptr;
	action.sa_sigaction = handlerDumpingSignal;
	for (int sig : {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGIOT, SIGQUIT, SIGSEGV, SIGSYS, SIGTRAP})
	{
		sigaction(sig, &action, &s_previousActions[sig]);
	}
}
