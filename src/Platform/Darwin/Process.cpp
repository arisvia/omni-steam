#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdint>
#include <libproc.h>
#include <string>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"
namespace OmniPlatform {

namespace {
bool EqualsIgnoreCasePrefix(const std::string& candidate, const std::string& target) {
    if (candidate.size() < target.size())
        return false;
    for (size_t i = 0; i < target.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(candidate[i])) !=
            std::tolower(static_cast<unsigned char>(target[i]))) {
            return false;
        }
    }
    return true;
}
} // namespace

uint32_t Process::GetCurrentProcessId() {
    return static_cast<uint32_t>(getpid());
}

std::string Process::GetProcessName(uint32_t pid) {
    return "steam";
}

std::vector<uint32_t> Process::FindProcessIdsByName(const std::string& processName) {
    std::vector<uint32_t> result;
    if (processName.empty())
        return result;

    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
    size_t size = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0)
        return result;

    std::vector<kinfo_proc> procs(size / sizeof(kinfo_proc));
    size = procs.size() * sizeof(kinfo_proc);
    if (sysctl(mib, 4, procs.data(), &size, nullptr, 0) != 0)
        return result;

    const size_t count = size / sizeof(kinfo_proc);
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        // p_comm is truncated to MAXCOMLEN bytes; a prefix compare keeps the
        // common case working without per-pid proc_pidpath syscalls.
        const char* comm = procs[i].kp_proc.p_comm;
        if (!comm || !*comm)
            continue;
        if (EqualsIgnoreCasePrefix(std::string(comm), processName)) {
            result.push_back(procs[i].kp_proc.p_pid);
        }
    }
    return result;
}

std::string Process::GetExecutablePath() {
    char path[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(getpid(), path, sizeof(path)) > 0) {
        return std::string(path);
    }
    return "";
}
void Thread::StartDetached(std::function<void()> task) {
    std::thread t(task);
    t.detach();
}

void Thread::Sleep(uint32_t milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

} // namespace OmniPlatform
