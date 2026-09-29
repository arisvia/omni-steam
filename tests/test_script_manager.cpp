#include "ScriptManager.h"
#include "omni_check.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

void TestScriptGeneration() {
    Manager::UnlockGameSpec spec;
    spec.appId = 1361510;
    spec.gameName = "Cyberpunk 2077";
    spec.dlcAppIds = {2138330, 2564880};
    spec.accessToken = "123456789";

    std::string lua = Manager::ScriptManager::GenerateLuaScript(spec);
    OMNI_CHECK(lua.find("addappid(1361510)") != std::string::npos);
    OMNI_CHECK(lua.find("addtoken(1361510, \"123456789\")") != std::string::npos);
    OMNI_CHECK(lua.find("addappid(2138330)") != std::string::npos);
    OMNI_CHECK(lua.find("addappid(2564880)") != std::string::npos);
    std::cout << "[PASS] TestScriptGeneration\n";
}

void TestScriptLifecycle() {
    std::string tempDir = (fs::temp_directory_path() / "test_omnisteam_lua").string();
    fs::create_directories(tempDir);

    Manager::UnlockGameSpec spec;
    spec.appId = 9999;
    spec.gameName = "TestGame";

    bool saved = Manager::ScriptManager::SaveGameUnlock(spec, tempDir);
    OMNI_CHECK(saved);

    auto list = Manager::ScriptManager::ListScripts(tempDir);
    OMNI_CHECK(!list.empty());
    OMNI_CHECK(list[0].primaryAppId == 9999);
    OMNI_CHECK(list[0].enabled);

    // Test toggle
    bool toggled = Manager::ScriptManager::ToggleScript(list[0].fullPath, false);
    OMNI_CHECK(toggled);

    auto listAfterToggle = Manager::ScriptManager::ListScripts(tempDir);
    OMNI_CHECK(!listAfterToggle.empty());
    OMNI_CHECK(!listAfterToggle[0].enabled);
    // Test DeleteScript
    bool deleted = Manager::ScriptManager::DeleteScript(listAfterToggle[0].fullPath);
    OMNI_CHECK(deleted);
    auto listAfterDelete = Manager::ScriptManager::ListScripts(tempDir);
    OMNI_CHECK(listAfterDelete.empty());

    // Clean up
    fs::remove_all(tempDir);
    std::cout << "[PASS] TestScriptLifecycle\n";
}

void TestScriptDeduplicationWithDepotKeys() {
    Manager::UnlockGameSpec spec;
    spec.appId = 1086940; // Baldur's Gate 3
    spec.gameName = "Baldur's Gate 3";
    spec.depotKeyHex = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
    spec.dlcAppIds = {2138330, 2564880};
    spec.depotKeys[2138330] = "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899";
    spec.appTicketHex = "112233445566";
    spec.eTicketHex = "deadbeefcafebabe";

    std::string lua = Manager::ScriptManager::GenerateLuaScript(spec);

    // 1. Keyed primary app is emitted
    OMNI_CHECK(lua.find("addappid(1086940, 0, \"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\")") !=
               std::string::npos);
    // 2. Bare duplicate must NOT be present
    OMNI_CHECK(lua.find("addappid(1086940)\n") == std::string::npos);

    // 3. Keyed DLC is emitted with key
    OMNI_CHECK(lua.find("addappid(2138330, 0, \"aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899\")") !=
               std::string::npos);
    // 4. Bare DLC without key is emitted normally
    OMNI_CHECK(lua.find("addappid(2564880)") != std::string::npos);

    // 5. AppOwnershipTicket and ETicket are emitted
    OMNI_CHECK(lua.find("setAppTicket(1086940, \"112233445566\")") != std::string::npos);
    OMNI_CHECK(lua.find("setETicket(1086940, \"deadbeefcafebabe\")") != std::string::npos);

    std::cout << "[PASS] TestScriptDeduplicationWithDepotKeys\n";
}

void TestEnsureAppManifestInvalidApp() {
    // AppId 0 must immediately fail
    bool res = Manager::ScriptManager::EnsureAppManifest(0);
    OMNI_CHECK(!res);
    std::cout << "[PASS] TestEnsureAppManifestInvalidApp\n";
}

int main() {
    std::cout << "Running OmniSteam Script Manager Tests...\n";
    TestScriptGeneration();
    TestScriptLifecycle();
    TestScriptDeduplicationWithDepotKeys();
    TestEnsureAppManifestInvalidApp();
    std::cout << "All Script Manager Tests Passed!\n";
    return 0;
}
