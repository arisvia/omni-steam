#include "ApiRouter.h"

#include "CloudSaveManager.h"
#include "ConfigManager.h"
#include "CoreInstaller.h"
#include "DenuvoImporter.h"
#include "DepotKeyStore.h"
#include "Doctor.h"
#include "ScriptManager.h"
#include "StaticAssets.h"
#include "SteamApi.h"
#include "WebDavClient.h"

#include <cstdint>
#include <iomanip>
#include <regex>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>

#include "OmniPlatform/OmniPlatform.h"

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>

#include <tlhelp32.h>
#endif

namespace Manager {

namespace {

bool CheckSteamProcess(uint32_t* outPid = nullptr) {
#if defined(OMNI_PLATFORM_WINDOWS)
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(hSnap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "steam.exe") == 0) {
                if (outPid)
                    *outPid = pe.th32ProcessID;
                CloseHandle(hSnap);
                return true;
            }
        } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return false;
#else
    return false;
#endif
}

std::string MakeHttpResponse(int statusCode, const std::string& contentType, const std::string& body) {
    std::ostringstream oss;
    std::string statusText =
        (statusCode == 200) ? "200 OK" : (statusCode == 404 ? "404 Not Found" : "500 Internal Server Error");
    oss << "HTTP/1.1 " << statusText << "\r\n"
        << "Content-Type: " << contentType << "; charset=utf-8\r\n"
        << "Content-Length: " << body.length() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return oss.str();
}

} // namespace

std::string ApiRouter::HandleRequest(const std::string& request) {
    // 1. Static HTML Root
    if (request.rfind("GET / ", 0) == 0 || request.rfind("GET /index.html", 0) == 0) {
        return MakeHttpResponse(200, "text/html", StaticAssets::GetIndexHtml());
    }

    // 2. /api/status - Live System & Hook States
    if (request.rfind("GET /api/status", 0) == 0) {
        uint32_t pid = 0;
        bool isRunning = CheckSteamProcess(&pid);
        auto scripts = ScriptManager::ListScripts();
        auto coreStatus = CoreInstaller::GetStatus();
        if (!isRunning) {
            coreStatus.active = false;
            coreStatus.checkAppOwnershipHook = false;
            coreStatus.configStoreHook = false;
            coreStatus.ipcHook = false;
        }

        std::ostringstream json;
        json << "{"
             << "\"steamRunning\":" << (isRunning ? "true" : "false") << ","
             << "\"steamPid\":" << pid << ","
             << "\"depotKeysCount\":" << DepotKeyStore::Count() << ","
             << "\"installedScriptsCount\":" << scripts.size() << ","
             << "\"defaultLuaDir\":\"" << OmniPlatform::Encoding::EscapeJson(ScriptManager::GetDefaultLuaDirectory())
             << "\","
             << "\"core\":{"
             << "\"installed\":" << (coreStatus.installed ? "true" : "false") << ","
             << "\"active\":" << (coreStatus.active ? "true" : "false") << ","
             << "\"installedVersion\":\"" << OmniPlatform::Encoding::EscapeJson(coreStatus.installedVersion) << "\","
             << "\"targetModule\":\"" << OmniPlatform::Encoding::EscapeJson(coreStatus.targetModule) << "\","
             << "\"checkAppOwnershipHook\":" << (coreStatus.checkAppOwnershipHook ? "true" : "false") << ","
             << "\"configStoreHook\":" << (coreStatus.configStoreHook ? "true" : "false") << ","
             << "\"ipcHook\":" << (coreStatus.ipcHook ? "true" : "false") << "}"
             << "}";
        return MakeHttpResponse(200, "application/json", json.str());
    }

    // 3. /api/appdetails - App & DLC Details
    if (request.rfind("GET /api/appdetails?", 0) == 0) {
        size_t idPos = request.find("appId=");
        uint32_t appId = 0;
        if (idPos != std::string::npos) {
            size_t endPos = request.find_first_of(" &\r\n", idPos);
            std::string idStr = request.substr(idPos + 6, endPos - (idPos + 6));
            try {
                appId = static_cast<uint32_t>(std::stoul(idStr));
            } catch (...) {
            }
        }

        auto details = SteamApi::GetAppDetails(appId);
        std::ostringstream json;
        json << "{"
             << "\"appId\":" << appId << ","
             << "\"name\":\"" << OmniPlatform::Encoding::EscapeJson(details.name) << "\","
             << "\"type\":\"" << OmniPlatform::Encoding::EscapeJson(details.type) << "\","
             << "\"headerImage\":\"" << OmniPlatform::Encoding::EscapeJson(details.headerImage) << "\","
             << "\"description\":\"" << OmniPlatform::Encoding::EscapeJson(details.description) << "\","
             << "\"dlcList\":[";
        for (size_t i = 0; i < details.dlcList.size(); ++i) {
            json << "{\"dlcId\":" << details.dlcList[i].dlcId << ",\"name\":\""
                 << OmniPlatform::Encoding::EscapeJson(details.dlcList[i].name) << "\"}";
            if (i + 1 < details.dlcList.size())
                json << ",";
        }
        json << "]}";
        return MakeHttpResponse(200, "application/json", json.str());
    }

    // 4. /api/core/install & /api/core/uninstall
    if (request.rfind("POST /api/core/install", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        std::string channel = "release";
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            if (body.find("\"nightly\"") != std::string::npos) {
                channel = "nightly";
            }
        }
        auto res = CoreInstaller::InstallCore(channel);
        std::ostringstream json;
        json << "{\"success\":" << (res.success ? "true" : "false") << ",\"message\":\""
             << OmniPlatform::Encoding::EscapeJson(res.message) << "\"}";
        return MakeHttpResponse(200, "application/json", json.str());
    }

    if (request.rfind("POST /api/core/uninstall", 0) == 0) {
        bool ok = CoreInstaller::UninstallCore();
        return MakeHttpResponse(200, "application/json", ok ? "{\"success\":true}" : "{\"success\":false}");
    }

    // 5. /api/denuvo/upload - Denuvo Tickets
    if (request.rfind("POST /api/denuvo/upload", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        std::string payload = (bodyPos != std::string::npos) ? request.substr(bodyPos + 4) : "";

        std::string filename = "";
        size_t fnPos = request.find("X-Filename:");
        if (fnPos != std::string::npos) {
            size_t fnEnd = request.find("\r\n", fnPos);
            std::string enc = request.substr(fnPos + 11, fnEnd - (fnPos + 11));
            while (!enc.empty() && enc.front() == ' ')
                enc.erase(0, 1);
            filename = OmniPlatform::Encoding::UrlDecode(enc);
        }

        auto res = DenuvoImporter::ImportFromPayload(payload, filename);
        std::ostringstream json;
        json << "{"
             << "\"success\":" << (res.success ? "true" : "false") << ","
             << "\"appId\":" << res.appId << ","
             << "\"gameName\":\"" << OmniPlatform::Encoding::EscapeJson(res.gameName) << "\","
             << "\"dlcCount\":" << res.dlcCount << ","
             << "\"resolvedDepotKeysCount\":" << res.resolvedDepotKeysCount << ","
             << "\"missingDepots\":[";
        for (size_t i = 0; i < res.missingDepots.size(); ++i) {
            json << res.missingDepots[i];
            if (i + 1 < res.missingDepots.size())
                json << ",";
        }
        json << "],\"message\":\"" << OmniPlatform::Encoding::EscapeJson(res.message) << "\"}";
        return MakeHttpResponse(200, "application/json", json.str());
    }

    // 6. /api/search - Store Search
    if (request.rfind("GET /api/search?", 0) == 0) {
        size_t qPos = request.find("q=");
        std::string q = "";
        if (qPos != std::string::npos) {
            size_t endPos = request.find_first_of(" &\r\n", qPos);
            q = request.substr(qPos + 2, endPos - (qPos + 2));
        }

        std::string cc = "US";
        size_t ccPos = request.find("cc=");
        if (ccPos != std::string::npos) {
            size_t endPos = request.find_first_of(" &\r\n", ccPos);
            std::string rawCc = request.substr(ccPos + 3, endPos - (ccPos + 3));
            if (!rawCc.empty()) {
                cc = rawCc;
            }
        }

        std::string decodedQ = OmniPlatform::Encoding::UrlDecode(q);
        auto results = SteamApi::SearchStore(decodedQ, "schinese", cc);
        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < results.size(); ++i) {
            json << "{\"appId\":" << results[i].appId << ",\"name\":\""
                 << OmniPlatform::Encoding::EscapeJson(results[i].name) << "\",\"tinyImage\":\""
                 << OmniPlatform::Encoding::EscapeJson(results[i].tinyImage) << "\"}";
            if (i + 1 < results.size())
                json << ",";
        }
        json << "]";
        return MakeHttpResponse(200, "application/json", json.str());
    }

    // 7. /api/scripts - Script Listing
    if (request.rfind("GET /api/scripts", 0) == 0) {
        auto scripts = ScriptManager::ListScripts();
        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < scripts.size(); ++i) {
            json << "{\"fileName\":\"" << OmniPlatform::Encoding::EscapeJson(scripts[i].fileName)
                 << "\",\"fullPath\":\"" << OmniPlatform::Encoding::EscapeJson(scripts[i].fullPath) << "\",\"title\":\""
                 << OmniPlatform::Encoding::EscapeJson(scripts[i].title)
                 << "\",\"primaryAppId\":" << scripts[i].primaryAppId
                 << ",\"enabled\":" << (scripts[i].enabled ? "true" : "false")
                 << ",\"fileSize\":" << scripts[i].fileSize << "}";
            if (i + 1 < scripts.size())
                json << ",";
        }
        json << "]";
        return MakeHttpResponse(200, "application/json", json.str());
    }

    // 8. /api/unlock - Unlock Game & Match Keys
    if (request.rfind("POST /api/unlock", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        uint32_t appId = 0;
        size_t dlcCount = 0;
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            std::regex idRegex("\"appId\"\\s*:\\s*(\\d+)");
            std::smatch m;
            if (std::regex_search(body, m, idRegex)) {
                appId = static_cast<uint32_t>(std::stoul(m[1].str()));
                auto details = SteamApi::GetAppDetails(appId);
                UnlockGameSpec spec;
                spec.appId = appId;
                spec.gameName = details.name.empty() ? ("App_" + std::to_string(appId)) : details.name;
                spec.dlcAppIds = details.dlcAppIds;
                dlcCount = spec.dlcAppIds.size();

                auto matchedKeys = DepotKeyStore::FindDepotKeysForApp(appId, details.dlcAppIds);
                for (const auto& [depotId, keyHex] : matchedKeys) {
                    if (depotId == appId) {
                        spec.depotKeyHex = keyHex;
                    }
                    spec.depotKeys[depotId] = keyHex;
                }
                ScriptManager::SaveGameUnlock(spec);
            }
        }
        std::ostringstream json;
        json << "{\"success\":true,\"appId\":" << appId << ",\"dlcCount\":" << dlcCount << "}";
        return MakeHttpResponse(200, "application/json", json.str());
    }

    // 9. /api/toggle & /api/delete
    if (request.rfind("POST /api/toggle", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        bool ok = false;
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            std::regex pathRegex("\"filePath\"\\s*:\\s*\"([^\"]+)\"");
            std::regex enableRegex("\"enable\"\\s*:\\s*(true|false)");
            std::smatch mPath, mEnable;
            if (std::regex_search(body, mPath, pathRegex) && std::regex_search(body, mEnable, enableRegex)) {
                std::string path = mPath[1].str();
                bool enable = (mEnable[1].str() == "true");
                ok = ScriptManager::ToggleScript(path, enable);
            }
        }
        return MakeHttpResponse(200, "application/json", ok ? "{\"success\":true}" : "{\"success\":false}");
    }

    if (request.rfind("POST /api/delete", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        bool ok = false;
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            std::regex pathRegex("\"filePath\"\\s*:\\s*\"([^\"]+)\"");
            std::smatch mPath;
            if (std::regex_search(body, mPath, pathRegex)) {
                std::string path = mPath[1].str();
                ok = ScriptManager::DeleteScript(path);
            }
        }
        return MakeHttpResponse(200, "application/json", ok ? "{\"success\":true}" : "{\"success\":false}");
    }

    // 10. /api/cloud/* - WebDAV Cloud Save Endpoints
    if (request.rfind("GET /api/cloud/config", 0) == 0) {
        auto cfg = ConfigManager::ReadConfig();
        std::ostringstream json;
        json << "{"
             << "\"enabled\":" << (cfg.cloudEnabled ? "true" : "false") << ","
             << "\"serverUrl\":\"" << OmniPlatform::Encoding::EscapeJson(cfg.webdavServerUrl) << "\","
             << "\"username\":\"" << OmniPlatform::Encoding::EscapeJson(cfg.webdavUsername) << "\","
             << "\"password\":\"" << OmniPlatform::Encoding::EscapeJson(cfg.webdavPassword) << "\","
             << "\"remoteRoot\":\"" << OmniPlatform::Encoding::EscapeJson(cfg.webdavRemoteRoot) << "\""
             << "}";
        return MakeHttpResponse(200, "application/json", json.str());
    }

    if (request.rfind("POST /api/cloud/config", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        bool ok = false;
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            auto cfg = ConfigManager::ReadConfig();

            std::regex enabledRegex("\"enabled\"\\s*:\\s*(true|false)");
            std::regex urlRegex("\"serverUrl\"\\s*:\\s*\"([^\"]*)\"");
            std::regex userRegex("\"username\"\\s*:\\s*\"([^\"]*)\"");
            std::regex passRegex("\"password\"\\s*:\\s*\"([^\"]*)\"");
            std::regex rootRegex("\"remoteRoot\"\\s*:\\s*\"([^\"]*)\"");

            std::smatch m;
            if (std::regex_search(body, m, enabledRegex))
                cfg.cloudEnabled = (m[1].str() == "true");
            if (std::regex_search(body, m, urlRegex))
                cfg.webdavServerUrl = m[1].str();
            if (std::regex_search(body, m, userRegex))
                cfg.webdavUsername = m[1].str();
            if (std::regex_search(body, m, passRegex))
                cfg.webdavPassword = m[1].str();
            if (std::regex_search(body, m, rootRegex))
                cfg.webdavRemoteRoot = m[1].str();

            ok = ConfigManager::SaveConfig(cfg);
        }
        return MakeHttpResponse(200, "application/json", ok ? "{\"success\":true}" : "{\"success\":false}");
    }

    if (request.rfind("POST /api/cloud/test", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        WebDavConfig webdav;
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            std::regex urlRegex("\"serverUrl\"\\s*:\\s*\"([^\"]*)\"");
            std::regex userRegex("\"username\"\\s*:\\s*\"([^\"]*)\"");
            std::regex passRegex("\"password\"\\s*:\\s*\"([^\"]*)\"");
            std::regex rootRegex("\"remoteRoot\"\\s*:\\s*\"([^\"]*)\"");
            std::smatch m;
            if (std::regex_search(body, m, urlRegex))
                webdav.serverUrl = m[1].str();
            if (std::regex_search(body, m, userRegex))
                webdav.username = m[1].str();
            if (std::regex_search(body, m, passRegex))
                webdav.password = m[1].str();
            if (std::regex_search(body, m, rootRegex))
                webdav.remoteRootPath = m[1].str();
        }
        if (webdav.serverUrl.empty()) {
            auto cfg = ConfigManager::ReadConfig();
            webdav.serverUrl = cfg.webdavServerUrl;
            webdav.username = cfg.webdavUsername;
            webdav.password = cfg.webdavPassword;
            webdav.remoteRootPath = cfg.webdavRemoteRoot.empty() ? "OmniSteam_Saves" : cfg.webdavRemoteRoot;
        }

        auto testResp = WebDavClient::MkCol(webdav, webdav.remoteRootPath);
        bool success = testResp.isSuccess() || testResp.statusCode == 405;
        std::string errMsg = testResp.error.empty() ? ("HTTP " + std::to_string(testResp.statusCode)) : testResp.error;

        std::ostringstream json;
        json << "{\"success\":" << (success ? "true" : "false") << ",\"statusCode\":" << testResp.statusCode
             << ",\"message\":\"" << (success ? "OK" : OmniPlatform::Encoding::EscapeJson(errMsg)) << "\"}";
        return MakeHttpResponse(200, "application/json", json.str());
    }

    if (request.rfind("POST /api/cloud/backup", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        uint32_t appId = 0;
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            std::regex idRegex("\"appId\"\\s*:\\s*(\\d+)");
            std::smatch m;
            if (std::regex_search(body, m, idRegex)) {
                appId = static_cast<uint32_t>(std::stoul(m[1].str()));
            }
        }
        auto cfg = ConfigManager::ReadConfig();
        WebDavConfig webdav;
        webdav.serverUrl = cfg.webdavServerUrl;
        webdav.username = cfg.webdavUsername;
        webdav.password = cfg.webdavPassword;
        webdav.remoteRootPath = cfg.webdavRemoteRoot.empty() ? "OmniSteam_Saves" : cfg.webdavRemoteRoot;

        bool success = CloudSaveManager::BackupAppSaves(appId, webdav);
        std::ostringstream json;
        json << "{\"success\":" << (success ? "true" : "false") << ",\"message\":\""
             << (success ? "Backup completed" : "No save files found or WebDAV connection failed") << "\"}";
        return MakeHttpResponse(200, "application/json", json.str());
    }

    if (request.rfind("POST /api/cloud/restore", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        uint32_t appId = 0;
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            std::regex idRegex("\"appId\"\\s*:\\s*(\\d+)");
            std::smatch m;
            if (std::regex_search(body, m, idRegex)) {
                appId = static_cast<uint32_t>(std::stoul(m[1].str()));
            }
        }
        auto cfg = ConfigManager::ReadConfig();
        WebDavConfig webdav;
        webdav.serverUrl = cfg.webdavServerUrl;
        webdav.username = cfg.webdavUsername;
        webdav.password = cfg.webdavPassword;
        webdav.remoteRootPath = cfg.webdavRemoteRoot.empty() ? "OmniSteam_Saves" : cfg.webdavRemoteRoot;

        bool success = CloudSaveManager::RestoreAppSaves(appId, webdav);
        std::ostringstream json;
        json << "{\"success\":" << (success ? "true" : "false") << ",\"message\":\""
             << (success ? "Restore completed" : "No remote backups found or WebDAV connection failed") << "\"}";
        return MakeHttpResponse(200, "application/json", json.str());
    }
    // 16. /api/doctor - System Diagnostics
    if (request.rfind("GET /api/doctor", 0) == 0) {
        auto report = Doctor::RunDiagnostics();
        std::ostringstream json;
        json << "{"
             << "\"overallHealthy\":" << (report.overallHealthy ? "true" : "false") << ","
             << "\"passCount\":" << report.passCount << ","
             << "\"warningCount\":" << report.warningCount << ","
             << "\"errorCount\":" << report.errorCount << ","
             << "\"items\":[";
        for (size_t i = 0; i < report.items.size(); ++i) {
            const auto& item = report.items[i];
            std::string lvl = "pass";
            if (item.level == DiagnosticLevel::Warning)
                lvl = "warning";
            else if (item.level == DiagnosticLevel::Error)
                lvl = "error";
            else if (item.level == DiagnosticLevel::Info)
                lvl = "info";

            json << "{"
                 << "\"category\":\"" << OmniPlatform::Encoding::EscapeJson(item.category) << "\","
                 << "\"name\":\"" << OmniPlatform::Encoding::EscapeJson(item.name) << "\","
                 << "\"level\":\"" << lvl << "\","
                 << "\"message\":\"" << OmniPlatform::Encoding::EscapeJson(item.message) << "\","
                 << "\"recommendation\":\"" << OmniPlatform::Encoding::EscapeJson(item.recommendation) << "\""
                 << "}";
            if (i + 1 < report.items.size())
                json << ",";
        }
        json << "]}";
        return MakeHttpResponse(200, "application/json", json.str());
    }

    return MakeHttpResponse(404, "text/plain", "404 Not Found");
}

} // namespace Manager
