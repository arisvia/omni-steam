#include "ConfigFileWatcher.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Utils/Config/LuaConfig.h"

#include "Hook/Hooks_Package.h"

namespace fs = std::filesystem;

namespace ConfigFileWatcher {

namespace {

std::atomic<bool> g_running{false};
std::thread g_watcherThread;
std::string g_watchedDir;

struct FileSnapshot {
    std::unordered_map<std::string, fs::file_time_type> files;
};

FileSnapshot TakeSnapshot(const std::string& dirPath) {
    FileSnapshot snap;
    if (!fs::exists(dirPath)) {
        return snap;
    }
    try {
        for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                snap.files[entry.path().string()] = entry.last_write_time();
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("ConfigFileWatcher: Snapshot error: {}", e.what());
    }
    return snap;
}

bool HasChanges(const FileSnapshot& prev, const FileSnapshot& curr) {
    if (prev.files.size() != curr.files.size()) {
        return true;
    }
    for (const auto& [path, time] : curr.files) {
        auto it = prev.files.find(path);
        if (it == prev.files.end() || it->second != time) {
            return true;
        }
    }
    return false;
}

void WatcherLoop() {
    spdlog::info("ConfigFileWatcher: Started watching directory '{}'", g_watchedDir);
    FileSnapshot lastSnapshot = TakeSnapshot(g_watchedDir);

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if (!g_running)
            break;

        FileSnapshot currentSnapshot = TakeSnapshot(g_watchedDir);
        if (HasChanges(lastSnapshot, currentSnapshot)) {
            spdlog::info("ConfigFileWatcher: Detected file changes in '{}', reloading Lua configuration...",
                         g_watchedDir);
            LuaConfig::ReloadDirectories(g_watchedDir);
            Hooks_Package::NotifyLicenseChanged();
            lastSnapshot = currentSnapshot;
        }
    }
    spdlog::info("ConfigFileWatcher: Watcher thread stopped");
}

} // namespace

void Start(const std::string& luaDir) {
    if (g_running) {
        return;
    }
    g_watchedDir = luaDir;
    g_running = true;
    g_watcherThread = std::thread(WatcherLoop);
}

void Stop() {
    if (!g_running) {
        return;
    }
    g_running = false;
    if (g_watcherThread.joinable()) {
        g_watcherThread.join();
    }
}

} // namespace ConfigFileWatcher
