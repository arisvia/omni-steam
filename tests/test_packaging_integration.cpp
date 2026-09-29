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

void TestSignatureAssetsIntegrity() {
    OMNI_CHECK(FindFile("signatures/anchor-map-windows.json"));
    OMNI_CHECK(FindFile("signatures/linux-x64/abd32eb3d963afb1.toml"));
    OMNI_CHECK(FindFile("signatures/macos-universal/42776e12adaa3114.toml"));
    std::cout << "[PASS] TestSignatureAssetsIntegrity\n";
}

void TestDwmapiProxyDefinitions() {
    std::string defPath = ResolvePath("src/Platform/Windows/Proxy/dwmapi.def");
    OMNI_CHECK(!defPath.empty());
    std::ifstream in(defPath);
    OMNI_CHECK(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    OMNI_CHECK(content.find("DllCanUnloadNow = DWMAPI.DllCanUnloadNow @111") != std::string::npos);
    OMNI_CHECK(content.find("DwmExtendFrameIntoClientArea = DWMAPI.DwmExtendFrameIntoClientArea @121") !=
               std::string::npos);
    OMNI_CHECK(content.find("DwmFlush = DWMAPI.DwmFlush @122") != std::string::npos);
    std::cout << "[PASS] TestDwmapiProxyDefinitions\n";
}

int main() {
    std::cout << "Running OmniSteam Packaging & Decky Integration Tests...\n";
    TestPackagingDefinitions();
    TestDeckyPluginSchema();
    TestSignatureAssetsIntegrity();
    TestDwmapiProxyDefinitions();
    std::cout << "All Packaging & Integration Tests Passed!\n";
    return 0;
}
