#include "OmniPlatform/OmniPlatform.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace fs = std::filesystem;

namespace OmniPlatform {

std::string CredentialStore::GetStoragePath() {
    const char* home = std::getenv("HOME");
    std::string base = home ? std::string(home) + "/Library/Application Support/OmniSteam/Credentials" : "/tmp/omnisteam/credentials";
    fs::create_directories(base);
    return base;
}

bool CredentialStore::WriteTicket(uint32_t appId, const std::string& ticketName, const std::string& hexValue) {
    try {
        std::string dir = GetStoragePath() + "/" + std::to_string(appId);
        fs::create_directories(dir);
        std::ofstream out(dir + "/" + ticketName + ".hex", std::ios::trunc);
        if (!out) return false;
        out << hexValue;
        return true;
    } catch (...) {
        return false;
    }
}

std::string CredentialStore::ReadTicket(uint32_t appId, const std::string& ticketName) {
    try {
        std::ifstream in(GetStoragePath() + "/" + std::to_string(appId) + "/" + ticketName + ".hex");
        if (!in) return "";
        std::string hex;
        std::getline(in, hex);
        return hex;
    } catch (...) {
        return "";
    }
}

} // namespace OmniPlatform
