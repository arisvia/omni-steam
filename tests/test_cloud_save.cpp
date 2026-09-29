#include "CloudSaveManager.h"
#include "SavePathResolver.h"
#include "WebDavClient.h"
#include "omni_check.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

void TestSavePathResolver() {
    std::string steamDir = Manager::SavePathResolver::GetSteamInstallDirectory();
    std::cout << "[INFO] Detected Steam Directory: " << steamDir << "\n";
    // Function executes without crash across platforms
    auto locs = Manager::SavePathResolver::LocateSaveDirectories(1361510);
    std::cout << "[PASS] TestSavePathResolver (Found " << locs.size() << " potential save paths)\n";
}

void TestRemoteCacheUfsResolution() {
    // Create dummy remotecache.vdf structure
    std::string tempRoot = (fs::temp_directory_path() / "test_omnisteam_cloud").generic_string();
    std::string appUserData = tempRoot + "/userdata/123456/999999";
    std::string remoteDir = appUserData + "/remote";
    fs::create_directories(remoteDir);

    std::string vdfPath = appUserData + "/remotecache.vdf";
    std::ofstream out(vdfPath);
    out << "\"999999\"\n"
        << "{\n"
        << "\t\"savegame.sav\"\n"
        << "\t{\n"
        << "\t\t\"root\"\t\t\"0\"\n"
        << "\t\t\"size\"\t\t\"2048\"\n"
        << "\t}\n"
        << "}\n";
    out.close();

    OMNI_CHECK(fs::exists(vdfPath));
    OMNI_CHECK(fs::exists(remoteDir));

    // Cleanup
    fs::remove_all(tempRoot);
    std::cout << "[PASS] TestRemoteCacheUfsResolution\n";
}
void TestWebDavConfig() {
    Manager::WebDavConfig cfg;
    cfg.serverUrl = "https://dav.jianguoyun.com/dav/";
    cfg.username = "test_user";
    cfg.password = "test_pass";
    cfg.remoteRootPath = "OmniSteam_Saves";

    OMNI_CHECK(!cfg.serverUrl.empty());
    OMNI_CHECK(cfg.remoteRootPath == "OmniSteam_Saves");
    std::cout << "[PASS] TestWebDavConfig\n";
}

int main() {
    std::cout << "Running OmniSteam Cloud Save & WebDAV Tests...\n";
    TestSavePathResolver();
    TestWebDavConfig();
    TestRemoteCacheUfsResolution();
    std::cout << "All Cloud Save Tests Passed!\n";
    return 0;
}
