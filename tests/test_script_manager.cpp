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

    // Clean up
    fs::remove_all(tempDir);
    std::cout << "[PASS] TestScriptLifecycle\n";
}

int main() {
    std::cout << "Running OmniSteam Script Manager Tests...\n";
    TestScriptGeneration();
    TestScriptLifecycle();
    std::cout << "All Script Manager Tests Passed!\n";
    return 0;
}
