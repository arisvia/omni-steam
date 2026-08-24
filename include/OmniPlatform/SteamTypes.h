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

// OnlineFix Spacewar P2P AppID
inline constexpr AppId_t kOnlineFixAppId = 480;

// Default synthesized license count for unowned standalone apps & DLCs
// Steam Protocol Network eMsg Constants
inline constexpr uint32_t kMsgHdrProtoFlag = 0x80000000;
inline constexpr uint32_t k_EMsgServiceMethodCallFromClient = 151;
inline constexpr uint32_t k_EMsgServiceMethodResponse = 147;
inline constexpr int32_t k_EResultOK = 1;
inline constexpr uint32_t k_EMsgClientGetLegacyGameKey = 730;
inline constexpr uint32_t k_EMsgClientGetLegacyGameKeyResponse = 785;
inline constexpr uint32_t k_EMsgClientGamesPlayed = 742;
inline constexpr uint32_t k_EMsgClientGamesPlayedWithDataBlob = 5410;
inline constexpr uint32_t k_EMsgClientRequestEncryptedAppTicketResponse = 5527;
inline constexpr uint32_t k_EMsgClientPICSProductInfoRequest = 8903;

// Synthetic purchase timestamp injected into AppOverview so unlocked titles
// appear instantly and persist in the library UI.
inline constexpr uint32_t kSteamSyntheticPurchasedTime = 1600000000;

// Protobuf field tags for CMsgProtoBufHeader / service responses
inline constexpr uint8_t kProtoTagJobIdTarget64 = 0x59;       // field 11, wire type 1
inline constexpr uint8_t kProtoTagEresultVarint = 0x68;       // field 13, wire type 0
inline constexpr uint8_t kProtoTagManifestRequestCode = 0x08; // field 1, wire type 0

// Steam Client Game Identifier Structure
struct CGameID {
    uint64_t m_ulGameID;
    AppId_t AppID(bool bAccountIDOnly = false) const {
        (void)bAccountIDOnly;
        return static_cast<AppId_t>(m_ulGameID & 0xFFFFFF);
    }
    void SetAppID(AppId_t appId) { m_ulGameID = (m_ulGameID & ~0xFFFFFFull) | (appId & 0xFFFFFFull); }
};

#pragma pack(push, 1)
struct ExtendedMsgHdr {
    uint32_t eMsg;
    uint8_t cubHeader;
    uint8_t protoVersion;
    uint32_t sourceJobID;
    uint32_t targetJobID;
    uint8_t canary;
    uint64_t steamID;
    int32_t clientSessionID;
};

struct MsgClientGetLegacyGameKey {
    uint32_t m_unAppId;
};

struct MsgClientGetLegacyGameKeyResponse {
    uint32_t m_unAppId;
    int32_t m_eResult;
    uint32_t m_cchKey;
};
#pragma pack(pop)
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

enum class EPackageStatus : uint32_t { Available = 0, Preorder = 1, Unavailable = 2, Invalid = 3 };
// ==============================================================================
// Steam Client Verified Structure Offsets (Windows x64 / Linux / macOS vs 32-bit)
// ==============================================================================
namespace SteamOffsets {

namespace CUser64 {
inline constexpr size_t kAccountId = 0x1E4;
} // namespace CUser64

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
    uint32_t TimeStamp;              // 0x18
    uint32_t TimeExpire;             // 0x1C
    uint32_t Unknown20;              // 0x20
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
    PackageId_t PackageId;            // 0x00
    int32_t ChangeNumber;             // 0x04
    uint64_t PICS_token;              // 0x08
    uint32_t BillingType;             // 0x10
    uint32_t LicenseType;             // 0x14
    EPackageStatus Status;            // 0x18
    uint8_t SHA_1_Hash[20];           // 0x1C
    void* pPackageInfoNodeBegin;      // 0x30
    void* pExtendNodeBegin;           // 0x38
    CUtlVector<AppId_t> AppIdVec;     // 0x40
    CUtlVector<DepotId_t> DepotIdVec; // 0x58
};
