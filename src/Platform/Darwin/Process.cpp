#include <chrono>
#include <climits>
#include <cstdint>
#include <libproc.h>
#include <string>
#include <thread>
#include <unistd.h>

namespace OmniPlatform {

uint32_t Process::GetCurrentProcessId() {
    return static_cast<uint32_t>(getpid());
}

std::string Process::GetProcessName(uint32_t pid) {
    return "steam";
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
