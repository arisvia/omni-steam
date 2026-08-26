#include "omni_check.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
bool FindFile(const std::string& relativePath) {
    std::vector<std::string> prefixes = {"", "../", "../../", "../../../"};
    for (const auto& p : prefixes) {
        if (fs::exists(p + relativePath))
            return true;
    }
    return false;
}

std::string ResolvePath(const std::string& relativePath) {
    std::vector<std::string> prefixes = {"", "../", "../../", "../../../"};
    for (const auto& p : prefixes) {
        std::string full = p + relativePath;
        if (fs::exists(full))
            return full;
    }
    return "";
}
} // namespace

void TestPackagingDefinitions() {
    OMNI_CHECK(FindFile("scripts/omnisteam.sh"));
    OMNI_CHECK(FindFile("scripts/install-steamos.sh"));
    std::cout << "[PASS] TestPackagingDefinitions\n";
}

void TestDeckyPluginSchema() {
    std::string pluginPath = ResolvePath("plugins/decky-omnisteam/plugin.json");
    OMNI_CHECK(!pluginPath.empty());
    std::ifstream in(pluginPath);
    OMNI_CHECK(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    OMNI_CHECK(content.find("\"name\": \"OmniSteam\"") != std::string::npos);
    OMNI_CHECK(content.find("\"api_version\": 2") != std::string::npos);
    std::cout << "[PASS] TestDeckyPluginSchema\n";
}

int main() {
    std::cout << "Running OmniSteam Packaging & Decky Integration Tests...\n";
    TestPackagingDefinitions();
    TestDeckyPluginSchema();
    std::cout << "All Packaging & Integration Tests Passed!\n";
    return 0;
}
