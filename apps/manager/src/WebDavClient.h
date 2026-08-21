#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "OmniPlatform/OmniEndpoints.h"

namespace Manager {

struct WebDavConfig {
    std::string serverUrl; // e.g. "https://dav.jianguoyun.com/dav/"
    std::string username;
    std::string password;
    std::string remoteRootPath = OmniEndpoints::CloudSave::kDefaultRemoteRoot;
};

struct WebDavResponse {
    int statusCode = 0;
    std::string body;
    std::string error;
    bool isSuccess() const { return statusCode >= 200 && statusCode < 300; }
};

class WebDavClient {
public:
    static WebDavResponse MkCol(const WebDavConfig& config, const std::string& remotePath);
    static WebDavResponse UploadFile(const WebDavConfig& config, const std::string& remotePath,
                                     const std::vector<uint8_t>& data);
    static WebDavResponse DownloadFile(const WebDavConfig& config, const std::string& remotePath);
    static WebDavResponse Delete(const WebDavConfig& config, const std::string& remotePath);
};

} // namespace Manager
