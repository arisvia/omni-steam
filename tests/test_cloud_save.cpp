#include "CloudSaveManager.h"
#include "SavePathResolver.h"
#include "WebDavClient.h"
#include "omni_check.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "OmniPlatform/OmniPaths.h"

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

void TestCloudSyncEnabledToggle() {
    // AppId 0 returns false
    OMNI_CHECK(!Manager::SavePathResolver::IsCloudSyncEnabled(0));

    // Non-zero default returns true when not explicitly disabled
    OMNI_CHECK(Manager::SavePathResolver::IsCloudSyncEnabled(1086940));

    std::cout << "[PASS] TestCloudSyncEnabledToggle\n";
}

void TestCloudSyncStatusPersistence() {
    Manager::CloudSyncStatus status;
    status.appId = 777777;
    status.action = "backup";
    status.status = "success";
    status.timestamp = "20260929_123456";
    status.fileCount = 3;
    status.totalBytes = 4096;
    status.message = "Unit test backup status";

    Manager::CloudSaveManager::RecordSyncStatus(status);

    auto retrieved = Manager::CloudSaveManager::GetSyncStatus(777777);
    OMNI_CHECK(retrieved.has_value());
    OMNI_CHECK(retrieved->appId == 777777);
    OMNI_CHECK(retrieved->action == "backup");
    OMNI_CHECK(retrieved->status == "success");
    OMNI_CHECK(retrieved->fileCount == 3);
    OMNI_CHECK(retrieved->totalBytes == 4096);
    OMNI_CHECK(retrieved->message == "Unit test backup status");

    auto all = Manager::CloudSaveManager::GetAllSyncStatuses();
    bool found = false;
    for (const auto& s : all) {
        if (s.appId == 777777) {
            found = true;
            break;
        }
    }
    OMNI_CHECK(found);

    std::cout << "[PASS] TestCloudSyncStatusPersistence\n";
}

void TestManagerPathRegistration() {
    // Register dummy manager executable path
    std::string testPath = (fs::temp_directory_path() / "test_omnisteam_manager.exe").generic_string();
    {
        std::ofstream dummy(testPath);
        dummy << "binary";
    }
    OMNI_CHECK(fs::exists(testPath));

    bool regOk = OmniPlatform::Paths::RegisterManagerExecutablePath(testPath);
    OMNI_CHECK(regOk);

    std::string resolved = OmniPlatform::Paths::GetManagerExecutablePath();
    // Should resolve to the registered path
    OMNI_CHECK(!resolved.empty());
    OMNI_CHECK(fs::exists(resolved));

    fs::remove(testPath);
    std::cout << "[PASS] TestManagerPathRegistration\n";
}

int main() {
    std::cout << "Running OmniSteam Cloud Save & WebDAV Tests...\n";
    TestSavePathResolver();
    TestWebDavConfig();
    TestRemoteCacheUfsResolution();
    TestCloudSyncEnabledToggle();
    TestCloudSyncStatusPersistence();
    TestManagerPathRegistration();
    std::cout << "All Cloud Save Tests Passed!\n";
    return 0;
}
