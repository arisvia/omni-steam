#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace {
    bool FindFile(const std::string& relativePath) {
        std::vector<std::string> prefixes = { "", "../", "../../", "../../../" };
        for (const auto& p : prefixes) {
            if (fs::exists(p + relativePath)) return true;
        }
        return false;
    }

    std::string ResolvePath(const std::string& relativePath) {
        std::vector<std::string> prefixes = { "", "../", "../../", "../../../" };
        for (const auto& p : prefixes) {
            std::string full = p + relativePath;
            if (fs::exists(full)) return full;
        }
        return "";
    }
}

void TestPackagingDefinitions() {
    assert(FindFile("patterns/linux_x64.toml"));
    assert(FindFile("patterns/windows_x64.toml"));
    assert(FindFile("scripts/install-steamos.sh"));
    std::cout << "[PASS] TestPackagingDefinitions\n";
}

void TestDeckyPluginSchema() {
    std::string pluginPath = ResolvePath("plugins/decky-omnisteam/plugin.json");
    assert(!pluginPath.empty());
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
