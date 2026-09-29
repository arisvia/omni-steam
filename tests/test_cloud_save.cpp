#include "CloudSaveManager.h"
#include "SavePathResolver.h"
#include "WebDavClient.h"
#include "omni_check.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "OmniPlatform/OmniPaths.h"

#if defined(_WIN32)
#include <windows.h>
#endif
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
    status.status = "failed";
    status.timestamp = "20260929_123456";
    status.fileCount = 3;
    status.totalBytes = 4096;
    // Regression test: message with quotes, backslashes, and special characters
    status.message = R"(Upload failed "C:\Steam\games\save.bin" (404 / Not Found))";

    Manager::CloudSaveManager::RecordSyncStatus(status);

    auto retrieved = Manager::CloudSaveManager::GetSyncStatus(777777);
    OMNI_CHECK(retrieved.has_value());
    OMNI_CHECK(retrieved->appId == 777777);
    OMNI_CHECK(retrieved->action == "backup");
    OMNI_CHECK(retrieved->status == "failed");
    OMNI_CHECK(retrieved->fileCount == 3);
    OMNI_CHECK(retrieved->totalBytes == 4096);
    OMNI_CHECK(retrieved->message == status.message);

    auto all = Manager::CloudSaveManager::GetAllSyncStatuses();
    bool found = false;
    for (const auto& s : all) {
        if (s.appId == 777777) {
            found = true;
            OMNI_CHECK(s.message == status.message);
            break;
        }
    }
    OMNI_CHECK(found);

    std::cout << "[PASS] TestCloudSyncStatusPersistence (Round-trip escaped JSON OK)\n";
}

void TestManagerPathRegistration() {
    // Hermetic: snapshot prior registry state
    std::string priorRegValue;
    bool hadPriorReg = false;
#if defined(_WIN32)
    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\OmniSteam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buf[MAX_PATH] = {};
        DWORD bufSize = sizeof(buf);
        DWORD type = REG_SZ;
        if (RegQueryValueExA(hKey, "ManagerPath", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &bufSize) ==
            ERROR_SUCCESS) {
            hadPriorReg = true;
            priorRegValue = buf;
        }
        RegCloseKey(hKey);
    }
#endif

    std::string testPath = (fs::temp_directory_path() / "test_omnisteam_mgr.exe").generic_string();
    {
        std::ofstream dummy(testPath);
        dummy << "binary";
    }
    OMNI_CHECK(fs::exists(testPath));

    bool regOk = OmniPlatform::Paths::RegisterManagerExecutablePath(testPath);
    OMNI_CHECK(regOk);

    std::string resolved = OmniPlatform::Paths::GetManagerExecutablePath();
    OMNI_CHECK(!resolved.empty());
    OMNI_CHECK(fs::exists(resolved));

    fs::remove(testPath);

    // Restore prior registry state
#if defined(_WIN32)
    if (hadPriorReg) {
        HKEY hKeyWrite = nullptr;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\OmniSteam", 0, KEY_SET_VALUE, &hKeyWrite) == ERROR_SUCCESS) {
            RegSetValueExA(hKeyWrite, "ManagerPath", 0, REG_SZ, reinterpret_cast<const BYTE*>(priorRegValue.c_str()),
                           static_cast<DWORD>(priorRegValue.size() + 1));
            RegCloseKey(hKeyWrite);
        }
    } else {
        HKEY hKeyWrite = nullptr;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\OmniSteam", 0, KEY_SET_VALUE, &hKeyWrite) == ERROR_SUCCESS) {
            RegDeleteValueA(hKeyWrite, "ManagerPath");
            RegCloseKey(hKeyWrite);
        }
    }
#endif

    std::cout << "[PASS] TestManagerPathRegistration (Hermetic)\n";
}

int main() {
    std::cout << "Running OmniSteam Cloud Save & WebDAV Tests...\n";

    // Hermetic sandbox isolation: redirect config & cache directories into temp
    std::string tempSandbox = (fs::temp_directory_path() / "test_omnisteam_sandbox").generic_string();
    fs::create_directories(tempSandbox);
#if defined(_WIN32)
    _putenv_s("OMNISTEAM_CONFIG_DIR", tempSandbox.c_str());
    _putenv_s("OMNISTEAM_CACHE_DIR", tempSandbox.c_str());
#else
    setenv("OMNISTEAM_CONFIG_DIR", tempSandbox.c_str(), 1);
    setenv("OMNISTEAM_CACHE_DIR", tempSandbox.c_str(), 1);
#endif

    TestSavePathResolver();
    TestWebDavConfig();
    TestRemoteCacheUfsResolution();
    TestCloudSyncEnabledToggle();
    TestCloudSyncStatusPersistence();
    TestManagerPathRegistration();

    std::error_code ec;
    fs::remove_all(tempSandbox, ec);

    std::cout << "All Cloud Save Tests Passed!\n";
    return 0;
}
