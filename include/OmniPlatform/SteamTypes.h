#pragma once
#include <cstdint>

// ==============================================================================
// Steam Base Types & Typedefs
// ==============================================================================
using AppId_t = uint32_t;
using DepotId_t = uint32_t;
using PackageId_t = uint32_t;
using ManifestId_t = uint64_t;
using AccountID_t = uint32_t;
using HSteamPipe = int32_t;
using HSteamUser = int32_t;
// Universal Base Package (Package 0) and Steam client internal access token
inline constexpr PackageId_t kSteamDefaultBasePackageId = 0;
inline constexpr uint64_t kSteamDefaultBasePackageAccessToken = 10660652434190618804ull;

// Default synthesized license count for unowned standalone apps & DLCs
inline constexpr uint32_t kSteamDefaultInjectedPackageCount = 1;
// Steam AppState Manifest Magic Flags
inline constexpr uint32_t kSteamAppStateReadyToInstall =
    1026; // k_EAppStateUpdateRequired (2) | k_EAppStateUpdateOptional (1024)

// OmniSteam Binary DLC Cache Header
inline constexpr uint32_t kSteamDlcCacheMagic = 0x4F4D4443; // 'OMDC' (OmniSteam DLC Cache)
inline constexpr uint32_t kSteamDlcCacheVersion = 1;

// Steam Client Internal Callback IDs
inline constexpr int32_t k_iCallback_LicensesUpdated = 125;
// ==============================================================================
// Competitive Anti-Cheat (VAC / EAC / BattlEye / ACE / Ricochet) Protected AppIDs
// ==============================================================================
namespace AntiCheat {
inline constexpr uint32_t kCounterStrike2 = 730;
inline constexpr uint32_t kCounterStrikeSource = 240;
inline constexpr uint32_t kDota2 = 570;
inline constexpr uint32_t kTeamFortress2 = 440;
inline constexpr uint32_t kApexLegends = 1172470;
inline constexpr uint32_t kPUBG = 578080;
inline constexpr uint32_t kRust = 252490;
inline constexpr uint32_t kRainbowSixSiege = 359550;
inline constexpr uint32_t kDeadByDaylight = 381210;
inline constexpr uint32_t kWarframe = 230410;
inline constexpr uint32_t kTheFinals = 2073850;
inline constexpr uint32_t kCallOfDutyHQ = 1938090;
inline constexpr uint32_t kCallOfDutyWarzone = 1962663;
inline constexpr uint32_t kNarakaBladepoint = 1203220;
inline constexpr uint32_t kDestiny2 = 1085660;
inline constexpr uint32_t kHuntShowdown = 594650;
inline constexpr uint32_t kEldenRing = 1245620;
inline constexpr uint32_t kArmoredCoreVI = 1888160;
inline constexpr uint32_t kHelldivers2 = 553850;
inline constexpr uint32_t kPalworld = 1623730;
inline constexpr uint32_t kDayZ = 221100;
inline constexpr uint32_t kSquad = 393380;
inline constexpr uint32_t kWarThunder = 236390;
inline constexpr uint32_t kUnturned = 304930;
inline constexpr uint32_t kPayday3 = 1272080;
inline constexpr uint32_t kFC24 = 2195250;
inline constexpr uint32_t kFC25 = 2669320;
inline constexpr uint32_t kSmite = 386360;
inline constexpr uint32_t kBrawlhalla = 291550;
inline constexpr uint32_t kLeft4Dead2 = 550;
} // namespace AntiCheat

// ==============================================================================
// Valve Tier1 Containers (CUtlMemory / CUtlVector)
// ==============================================================================
template <typename T> struct CUtlMemory {
    T* m_pMemory;
    int m_nAllocationCount;
    int m_nGrowSize;
};

template <typename T> struct CUtlVector {
    CUtlMemory<T> m_Memory;
    int m_Size;
    T* m_pElements;
};

// ==============================================================================
// Steam Client Internal Enums
// ==============================================================================
enum EConfigStore : uint32_t {
    k_EConfigStoreInvalid = 0,
    k_EConfigStoreSystem = 1,
    k_EConfigStoreUserRoaming = 2,
    k_EConfigStoreUserLocal = 3
};

enum class EAppReleaseState : uint32_t {
    Unknown = 0,
    Unavailable = 1,
    Prerelease = 2,
    PreloadOnly = 3,
    Released = 4,
    Disabled = 5
};
enum class EPackageStatus : uint32_t { Invalid = 0, Unknown = 1, Preorder = 2, Available = 3 };

// ==============================================================================
// Steam Client Verified Structure Offsets (Windows x64 / Linux / macOS vs 32-bit)
// ==============================================================================
namespace SteamOffsets {

namespace Ownership64 {
inline constexpr size_t kPackageId = 0x00;
inline constexpr size_t kExistInPackageNums = 0x14;
inline constexpr size_t kReleaseState = 0x1C;
inline constexpr size_t kExistInPackageNumsFallback = 0x20;
inline constexpr size_t kOwnsLicense = 0x28;
inline constexpr size_t kIsSubscribed = 0x30;
inline constexpr size_t kActiveFlag1 = 0x32;
inline constexpr size_t kActiveFlag2 = 0x33;
inline constexpr size_t kActiveFlag3 = 0x34;
} // namespace Ownership64

namespace Ownership32 {
inline constexpr size_t kPackageId = 0x00;
inline constexpr size_t kReleaseState = 0x04;
inline constexpr size_t kExistInPackageNums = 0x08;
inline constexpr size_t kOwnsLicense = 0x0C;
inline constexpr size_t kFreeLicense = 0x0D;
inline constexpr size_t kIsSubscribed = 0x10;
} // namespace Ownership32

namespace PackageInfo64 {
inline constexpr size_t kPackageId = 0x00;
inline constexpr size_t kStatus = 0x18;
inline constexpr size_t kAppIdVecElements = 0x40;
inline constexpr size_t kAppIdVecSize = 0x50;
inline constexpr size_t kDepotIdVecElements = 0x60;
inline constexpr size_t kDepotIdVecSize = 0x70;
} // namespace PackageInfo64

namespace PackageInfo32 {
inline constexpr size_t kPackageId = 0x00;
inline constexpr size_t kStatus = 0x0C;
inline constexpr size_t kAppIdVecSize = 0x1C;
inline constexpr size_t kAppIdVecElements = 0x20;
inline constexpr size_t kDepotIdVecElements = 0x28;
inline constexpr size_t kDepotIdVecSize = 0x30;
} // namespace PackageInfo32

} // namespace SteamOffsets

// ==============================================================================
// Steam Client Ownership & License Structures
// ==============================================================================
struct AppOwnership {
    PackageId_t PackageId;           // 0x00
    EAppReleaseState ReleaseState;   // 0x04
    AccountID_t SteamId32;           // 0x08
    AppId_t MasterSubscriptionAppID; // 0x0C
    uint32_t TrialSeconds;           // 0x10
    uint32_t ExistInPackageNums;     // 0x14
    char PurchaseCountryCode[4];     // 0x18
    uint32_t TimeStamp;              // 0x1C
    uint32_t TimeExpire;             // 0x20
    bool bOwnsLicense;               // 0x24
    bool bLicenseExpired;            // 0x25
    bool bIsPermanent;               // 0x26
    bool bLowViolence;               // 0x27
    bool bFreeLicense;               // 0x28
    bool bRegionRestricted;          // 0x29
    bool bFromFreeWeekend;           // 0x2A
    bool bLicenseLocked;             // 0x2B
    bool bLicensePending;            // 0x2C
    bool bRetailLicense;             // 0x2D
    bool bAutoGrant;                 // 0x2E
    bool bLicensePermanent;          // 0x2F
    bool bGuestPass;                 // 0x30
    bool bBorrowed;                  // 0x31
    bool bAnySiteLicense;            // 0x32
    bool bAllSiteLicenses;           // 0x33
    bool bAllActivationRequired;     // 0x34
    bool bFamilyShared;              // 0x35
};

struct PackageInfo {
    uint32_t PackageId;
    EPackageStatus Status;
    uint32_t BillingType;
    uint32_t LicenseFlags;
    CUtlVector<AppId_t> AppIdVec;
    CUtlVector<DepotId_t> DepotIdVec;
    uint32_t PackageFlags;
    uint32_t AccountId;
};
