#include "OmniPlatform/OmniPlatform.h"
#include <unistd.h>
#include <climits>
#include <thread>
#include <chrono>

namespace OmniPlatform {

uint32_t Process::GetCurrentProcessId() {
    return static_cast<uint32_t>(getpid());
}

std::string Process::GetProcessName(uint32_t pid) {
    return "steam";
}

std::string Process::GetExecutablePath() {
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
