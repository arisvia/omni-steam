#include <chrono>
#include <climits>
#include <fstream>
#include <thread>
#include <unistd.h>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

uint32_t Process::GetCurrentProcessId() {
    return static_cast<uint32_t>(getpid());
}

std::string Process::GetProcessName(uint32_t pid) {
    std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
    if (comm) {
        std::string name;
        std::getline(comm, name);
        return name;
    }
    return "";
}

std::string Process::GetExecutablePath() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        return std::string(buf);
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
