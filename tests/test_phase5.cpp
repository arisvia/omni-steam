#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

void TestPackagingDefinitions() {
    // Verify essential release patterns and scripts exist
    assert(fs::exists("patterns/linux_x64.toml") || fs::exists("../patterns/linux_x64.toml"));
    assert(fs::exists("patterns/windows_x64.toml") || fs::exists("../patterns/windows_x64.toml"));
    assert(fs::exists("scripts/install-steamos.sh") || fs::exists("../scripts/install-steamos.sh"));
    std::cout << "[PASS] TestPackagingDefinitions\n";
}

void TestDeckyPluginSchema() {
    std::string pluginPath = fs::exists("plugins/decky-omnisteam/plugin.json") ? "plugins/decky-omnisteam/plugin.json" : "../plugins/decky-omnisteam/plugin.json";
    std::ifstream in(pluginPath);
    assert(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    assert(content.find("\"name\": \"OmniSteam\"") != std::string::npos);
    assert(content.find("\"api_version\": 2") != std::string::npos);
    std::cout << "[PASS] TestDeckyPluginSchema\n";
}

int main() {
    std::cout << "Running OmniSteam Phase 5 Integration & Packaging Tests...\n";
    TestPackagingDefinitions();
    TestDeckyPluginSchema();
    std::cout << "All Phase 5 Tests Passed!\n";
    return 0;
}
