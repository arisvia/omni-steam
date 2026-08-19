#pragma once
#include <string>

namespace OmniEndpoints {

// ==============================================================================
// 1. GitHub 官方远程回退仓库端点 (用于签名更新、DepotKey 索引在线拉取)
// ==============================================================================
namespace GitHub {
inline constexpr const char* kRepoOwner = "arisvia";
inline constexpr const char* kRepoName = "omni-steam";
inline constexpr const char* kBranch = "main";

// GitHub Raw 基础镜像路径 (可切换为加速镜像如 raw.fastgit.org / cdn.jsdelivr.net)
inline constexpr const char* kRawBaseUrl = "https://raw.githubusercontent.com/arisvia/omni-steam/main";

// 远程动态特征签名库
inline constexpr const char* kPatternsWindowsX64 =
    "https://raw.githubusercontent.com/arisvia/omni-steam/main/patterns/windows_x64.toml";
inline constexpr const char* kPatternsLinuxX64 =
    "https://raw.githubusercontent.com/arisvia/omni-steam/main/patterns/linux_x64.toml";
inline constexpr const char* kPatternsMacosX64 =
    "https://raw.githubusercontent.com/arisvia/omni-steam/main/patterns/macos_x64.toml";

// 远程全局 Depot 解密 Key 二进制数据库
inline constexpr const char* kDepotKeysBin =
    "https://raw.githubusercontent.com/arisvia/omni-steam/main/depotkeys.bin";
} // namespace GitHub

// ==============================================================================
// 2. Manifest 上游授权请求码服务商列表
// ==============================================================================
namespace Manifest {
inline constexpr const char* kDefaultUpstream = "opensteamtool";
inline constexpr const char* kOpenSteamToolUrl = "https://manifest.opensteamtool.com";
inline constexpr const char* kSteamRunUrl = "https://api.steamrun.net";
inline constexpr const char* kWuDrmUrl = "https://manifest.wudrm.com";
} // namespace Manifest

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
inline constexpr const char* kAppDetailsApi = "https://store.steampowered.com/api/appdetails";
inline constexpr const char* kImageCdnBase = "https://cdn.cloudflare.steamstatic.com/steam/apps/";
} // namespace Steam

} // namespace OmniEndpoints
