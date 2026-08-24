#pragma once
#include <string>

namespace OmniEndpoints {

// ==============================================================================
// 1. GitHub 官方远程回退仓库端点 (用于 DepotKey 索引在线拉取)
// ==============================================================================
namespace GitHub {
inline constexpr const char* kRepoOwner = "arisvia";
inline constexpr const char* kRepoName = "omni-steam";
inline constexpr const char* kBranch = "main";

// GitHub Raw 基础镜像路径 (可切换为加速镜像如 raw.fastgit.org / cdn.jsdelivr.net)
inline constexpr const char* kRawBaseUrl = "https://raw.githubusercontent.com/arisvia/omni-steam/main";
inline constexpr const char* kApiReleasesLatest = "https://api.github.com/repos/arisvia/omni-steam/releases/latest";
inline constexpr const char* kReleasesLatestDownload =
    "https://github.com/arisvia/omni-steam/releases/latest/download/";
inline constexpr const char* kReleasesDownloadBase = "https://github.com/arisvia/omni-steam/releases/download/";
inline constexpr const char* kJsDelivrMainBase = "https://cdn.jsdelivr.net/gh/arisvia/omni-steam@main/";
inline constexpr const char* kJsDelivrNightlyBase = "https://cdn.jsdelivr.net/gh/arisvia/omni-steam@nightly/";

// 远程全局 Depot 解密 Key 二进制数据库与加速 CDN 镜像
inline constexpr const char* kDepotKeysBin = "https://raw.githubusercontent.com/arisvia/omni-steam/main/depotkeys.bin";
inline constexpr const char* kDepotKeysJsDelivr = "https://cdn.jsdelivr.net/gh/arisvia/omni-steam@main/depotkeys.bin";
inline constexpr const char* kDepotKeysFastly = "https://fastly.jsdelivr.net/gh/arisvia/omni-steam@main/depotkeys.bin";
inline constexpr const char* kDepotKeysGcore = "https://gcore.jsdelivr.net/gh/arisvia/omni-steam@main/depotkeys.bin";
} // namespace GitHub

// ==============================================================================
// 2. Manifest 上游授权请求码服务商列表
// ==============================================================================
namespace Manifest {
inline constexpr const char* kDefaultUpstream = "opensteamtool";
// Request-code endpoints: append "/<manifest_gid>" to the base URL.
inline constexpr const char* kOpenSteamToolUrl = "https://manifest.opensteamtool.com";
inline constexpr const char* kSteamRunUrl = "https://manifest.steam.run/api/manifest";
inline constexpr const char* kWuDrmUrl = "http://gmrc.wudrm.com/manifest";
// Full manifest download endpoint: append "/<depot_id>/<manifest_gid>".
inline constexpr const char* kManifestDownloadBase = "https://manifest.opensteamtool.com";
} // namespace Manifest

// ==============================================================================
// 2b. 远程签名数据库 (PatternLoader 运行时拉取, depotkeys.bin 同款分发模型)
//     路径格式: <base>/<platform-dir>/<sha256>.toml
// ==============================================================================
namespace SignatureDb {
inline constexpr const char* kJsDelivrBase = "https://cdn.jsdelivr.net/gh/arisvia/omni-steam@main/signatures";
inline constexpr const char* kRawBase = "https://raw.githubusercontent.com/arisvia/omni-steam/main/signatures";
} // namespace SignatureDb

// ==============================================================================
// 3. 成就与统计数据同步服务
// ==============================================================================
namespace Stats {
inline constexpr const char* kStatsBaseUrl = "https://stats.opensteamtool.com";
}

// ==============================================================================
// 4. Steam 官方公开 WebAPI 与 CDN 服务
// ==============================================================================
namespace Steam {
inline constexpr const char* kStoreSearchApi = "https://store.steampowered.com/api/storesearch/";
inline constexpr const char* kStoreSuggestApi = "https://store.steampowered.com/search/suggest";
inline constexpr const char* kAppDetailsApi = "https://store.steampowered.com/api/appdetails";
inline constexpr const char* kImageCdnBase = "https://cdn.cloudflare.steamstatic.com/steam/apps/";
inline constexpr const char* kAkamaiImageCdnBase =
    "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/";
inline constexpr const char* kSteamDbAppBase = "https://steamdb.info/app/";
inline constexpr const char* kSteamStoreAppBase = "https://store.steampowered.com/app/";
} // namespace Steam

// ==============================================================================
// 5. 全局统一网络通信请求头与客户端标识 (HTTP User-Agent)
// ==============================================================================
namespace Http {
inline constexpr const char* kUserAgent = "OpenSteamTool/1.0";
inline constexpr const wchar_t* kUserAgentW = L"OpenSteamTool/1.0";
} // namespace Http

// ==============================================================================
// 6. 云存档 WebDAV 默认配置
// ==============================================================================
namespace CloudSave {
inline constexpr const char* kDefaultRemoteRoot = "OmniSteam_Saves";
} // namespace CloudSave
} // namespace OmniEndpoints
