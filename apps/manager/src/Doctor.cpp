#include "Doctor.h"

#include "ConfigManager.h"
#include "CoreInstaller.h"
#include "DepotKeyStore.h"
#include "ScriptManager.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "OmniPlatform/OmniBuildInfo.h"
#include "OmniPlatform/OmniPlatform.h"

namespace fs = std::filesystem;

namespace Manager {

namespace {
const char* GetLevelIcon(DiagnosticLevel level) {
    switch (level) {
        case DiagnosticLevel::Pass:
            return "[PASS]";
        case DiagnosticLevel::Warning:
            return "[WARN]";
        case DiagnosticLevel::Error:
            return "[FAIL]";
        case DiagnosticLevel::Info:
            return "[INFO]";
    }
    return "[INFO]";
}
} // namespace

DoctorReport Doctor::RunDiagnostics() {
    DoctorReport report;

    auto addItem = [&](const std::string& category, const std::string& name, DiagnosticLevel level,
                       const std::string& msg, const std::string& rec = "") {
        report.items.push_back({category, name, level, msg, rec});
        if (level == DiagnosticLevel::Pass)
            report.passCount++;
        else if (level == DiagnosticLevel::Warning)
            report.warningCount++;
        else if (level == DiagnosticLevel::Error) {
            report.errorCount++;
            report.overallHealthy = false;
        }
    };

    // 1. Steam Installation & Path
    std::string steamPath = OmniPlatform::Paths::GetSteamInstallPath();
    if (steamPath.empty() || !fs::exists(steamPath)) {
        addItem("Steam Environment", "Steam Installation Directory", DiagnosticLevel::Error,
                "Steam installation path could not be detected from system.",
                "Please make sure Steam is installed properly or specify it via config.");
    } else {
        addItem("Steam Environment", "Steam Installation Directory", DiagnosticLevel::Pass,
                "Found Steam installation at: " + steamPath);
    }

    // 2. Core Hook & DLL Status
    auto coreStatus = CoreInstaller::GetStatus();
    if (!coreStatus.installed) {
        addItem("OmniSteam Core", "Core Binary Files", DiagnosticLevel::Warning,
                "Core hook is not installed in Steam directory.",
                "Run 'omnisteam install-core' or install from Manager WebUI.");
    } else {
        addItem("OmniSteam Core", "Core Binary Files", DiagnosticLevel::Pass,
                "Core installed properly (Version " + coreStatus.installedVersion + ")");
    }

    // 3. Steam Process & Live Hook Attachment
    if (coreStatus.active) {
        addItem("OmniSteam Core", "Live Steam Process Attachment", DiagnosticLevel::Pass,
                "Steam running (PID: " + std::to_string(coreStatus.livePid) + ") with Hook ACTIVE");
    } else {
        addItem("OmniSteam Core", "Live Steam Process Attachment", DiagnosticLevel::Info,
                "Steam process is currently offline or hook is not attached.");
    }

    // 4. DepotKey Database
    size_t keyCount = DepotKeyStore::Count();
    if (keyCount == 0) {
        addItem("Database", "DepotKey Database", DiagnosticLevel::Warning, "DepotKey database has 0 keys cached.",
                "Depot keys will be downloaded asynchronously from CDN.");
    } else {
        addItem("Database", "DepotKey Database", DiagnosticLevel::Pass,
                std::to_string(keyCount) + " depot keys loaded and indexed in memory.");
    }

    // 5. Config / Lua Directory & Permissions
    std::string luaDir = ScriptManager::GetDefaultLuaDirectory();
    if (luaDir.empty()) {
        addItem("Configuration", "Lua Script Directory", DiagnosticLevel::Error,
                "Cannot resolve candidate Lua script directory.", "Verify Steam path permissions.");
    } else {
        std::error_code ec;
        fs::create_directories(luaDir, ec);
        if (ec) {
            addItem("Configuration", "Lua Script Directory", DiagnosticLevel::Error,
                    "Failed to create or access Lua directory: " + luaDir + " (" + ec.message() + ")",
                    "Check folder write permissions.");
        } else {
            // Test write permission
            std::string testFile = (fs::path(luaDir) / ".omnisteam_rw_test").string();
            std::ofstream out(testFile);
            if (out) {
                out << "test";
                out.close();
                fs::remove(testFile, ec);
                auto scripts = ScriptManager::ListScripts();
                addItem("Configuration", "Lua Script Directory", DiagnosticLevel::Pass,
                        "Directory accessible (" + std::to_string(scripts.size()) + " script(s) loaded at " + luaDir +
                            ")");
            } else {
                addItem("Configuration", "Lua Script Directory", DiagnosticLevel::Error,
                        "Directory is not writable: " + luaDir,
                        "Run as Administrator or grant write access to Steam directory.");
            }
        }
    }

    // 6. Cache Directory & Zero-Scan .ptch Cache
    std::string cacheDir = OmniPlatform::Paths::GetCacheDirectory();
    if (!cacheDir.empty() && fs::exists(cacheDir)) {
        addItem("Cache", "OmniPlatform Cache Directory", DiagnosticLevel::Pass,
                "Cache directory verified at: " + cacheDir);
    } else {
        addItem("Cache", "OmniPlatform Cache Directory", DiagnosticLevel::Info,
                "Cache directory will be initialized on first core execution.");
    }

    // 7. Cloud Save Configuration
    auto config = ConfigManager::ReadConfig();
    if (config.cloudEnabled) {
        if (config.webdavServerUrl.empty() || config.webdavUsername.empty()) {
            addItem("Cloud Save", "WebDAV Configuration", DiagnosticLevel::Warning,
                    "Cloud save is enabled but WebDAV Server URL or Username is incomplete.",
                    "Configure WebDAV settings via WebUI or CLI.");
        } else {
            addItem("Cloud Save", "WebDAV Configuration", DiagnosticLevel::Pass,
                    "WebDAV Cloud Save configured (" + config.webdavServerUrl + ")");
        }
    } else {
        addItem("Cloud Save", "WebDAV Configuration", DiagnosticLevel::Info,
                "WebDAV Cloud Save is currently disabled.");
    }

    return report;
}

void Doctor::PrintReport(const DoctorReport& report) {
    std::cout << "\n======================================================\n";
    std::cout << " OmniSteam Doctor Diagnostic Report (v" << OMNISTEAM_VERSION << ")\n";
    std::cout << "======================================================\n";

    std::string lastCat = "";
    for (const auto& item : report.items) {
        if (item.category != lastCat) {
            std::cout << "\n[" << item.category << "]\n";
            lastCat = item.category;
        }

        std::cout << "  " << GetLevelIcon(item.level) << " " << item.name << "\n";
        std::cout << "         -> " << item.message << "\n";
        if (!item.recommendation.empty()) {
            std::cout << "         [Recommendation] " << item.recommendation << "\n";
        }
    }

    std::cout << "\n------------------------------------------------------\n";
    std::cout << " Summary: " << report.passCount << " Passed, " << report.warningCount << " Warnings, "
              << report.errorCount << " Errors.\n";
    if (report.overallHealthy) {
        std::cout << " Status:  HEALTHY - System is fully ready for operation.\n";
    } else {
        std::cout << " Status:  ATTENTION REQUIRED - Follow recommendations above.\n";
    }
    std::cout << "======================================================\n\n";
}

} // namespace Manager
