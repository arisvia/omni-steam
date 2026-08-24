#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <dirent.h>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

namespace {
bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}
} // namespace

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

std::vector<uint32_t> Process::FindProcessIdsByName(const std::string& processName) {
    std::vector<uint32_t> result;
    if (processName.empty())
        return result;

    DIR* dir = opendir("/proc");
    if (!dir)
        return result;

    while (dirent* entry = readdir(dir)) {
        const char* name = entry->d_name;
        bool numeric = *name != '\0';
        for (const char* p = name; numeric && *p; ++p) {
            if (*p < '0' || *p > '9')
                numeric = false;
        }
        if (!numeric || entry->d_type != DT_DIR)
            continue;

        uint32_t pid = static_cast<uint32_t>(std::atoi(name));
        if (pid == 0)
            continue;
        if (EqualsIgnoreCase(GetProcessName(pid), processName)) {
            result.push_back(pid);
        }
    }
    closedir(dir);
    return result;
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
