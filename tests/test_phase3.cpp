#include "ScriptManager.h"
#include <cassert>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

void TestScriptGeneration() {
    Manager::UnlockGameSpec spec;
    spec.appId = 1361510;
    spec.gameName = "Cyberpunk 2077";
    spec.dlcAppIds = { 2138330, 2564880 };
    spec.accessToken = "123456789";

    std::string lua = Manager::ScriptManager::GenerateLuaScript(spec);
    assert(lua.find("addappid(1361510)") != std::string::npos);
    assert(lua.find("addtoken(1361510, \"123456789\")") != std::string::npos);
    assert(lua.find("addappid(2138330)") != std::string::npos);
    assert(lua.find("addappid(2564880)") != std::string::npos);
    std::cout << "[PASS] TestScriptGeneration\n";
}

void TestScriptLifecycle() {
    std::string tempDir = (fs::temp_directory_path() / "test_omnisteam_lua").string();
    fs::create_directories(tempDir);
    spec.appId = 9999;
    spec.gameName = "TestGame";

    bool saved = Manager::ScriptManager::SaveGameUnlock(spec, tempDir);
    assert(saved);

    auto list = Manager::ScriptManager::ListScripts(tempDir);
    assert(!list.empty());
    assert(list[0].primaryAppId == 9999);
    assert(list[0].enabled);

    // Test toggle
    bool toggled = Manager::ScriptManager::ToggleScript(list[0].fullPath, false);
    assert(toggled);

    auto listAfterToggle = Manager::ScriptManager::ListScripts(tempDir);
    assert(!listAfterToggle.empty());
    assert(!listAfterToggle[0].enabled);

    // Clean up
    fs::remove_all(tempDir);
    std::cout << "[PASS] TestScriptLifecycle\n";
}

int main() {
    std::cout << "Running OmniSteam Phase 3 Manager Tests...\n";
    TestScriptGeneration();
    TestScriptLifecycle();
    std::cout << "All Phase 3 Tests Passed!\n";
    return 0;
}
