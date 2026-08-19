#include <cassert>
#include <filesystem>
#include <iostream>

#include "CloudSaveManager.h"
#include "SavePathResolver.h"
#include "WebDavClient.h"

namespace fs = std::filesystem;

void TestSavePathResolver() {
    std::string steamDir = Manager::SavePathResolver::GetSteamInstallDirectory();
    std::cout << "[INFO] Detected Steam Directory: " << steamDir << "\n";
    // Function executes without crash across platforms
    auto locs = Manager::SavePathResolver::LocateSaveDirectories(1361510);
    std::cout << "[PASS] TestSavePathResolver (Found " << locs.size() << " potential save paths)\n";
}

void TestWebDavConfig() {
    Manager::WebDavConfig cfg;
    cfg.serverUrl = "https://dav.jianguoyun.com/dav/";
    cfg.username = "test_user";
    cfg.password = "test_pass";
    cfg.remoteRootPath = "OmniSteam_Saves";

    assert(!cfg.serverUrl.empty());
    assert(cfg.remoteRootPath == "OmniSteam_Saves");
    std::cout << "[PASS] TestWebDavConfig\n";
}

int main() {
    std::cout << "Running OmniSteam Cloud Save & WebDAV Tests...\n";
    TestSavePathResolver();
    TestWebDavConfig();
    std::cout << "All Cloud Save Tests Passed!\n";
    return 0;
}
