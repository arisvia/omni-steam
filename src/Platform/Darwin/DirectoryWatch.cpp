#include <atomic>
#include <string>
#include <sys/event.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

namespace {
std::atomic<bool> g_watching{false};
std::thread g_watchThread;
} // namespace

bool DirectoryWatch::StartWatch(const std::vector<std::string>& directories, Callback onChange) {
    g_watching.store(true);
    // macOS kqueue or FSEvents watcher
    return true;
}

void DirectoryWatch::StopWatch() {
    g_watching.store(false);
    if (g_watchThread.joinable())
        g_watchThread.join();
}

} // namespace OmniPlatform
