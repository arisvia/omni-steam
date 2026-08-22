#pragma once
#include <cstdint>

// ==============================================================================
// Steam Base Types & Typedefs
// ==============================================================================
using AppId_t = uint32_t;
using DepotId_t = uint32_t;
using PackageId_t = uint32_t;
using ManifestId_t = uint64_t;
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

// Steam Client Internal Callback IDs
inline constexpr int32_t k_iCallback_LicensesUpdated = 125;
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
// Steam Client Ownership & License Structures (Legacy References)
// ==============================================================================
struct AppOwnership {
    uint32_t PackageId;
    EAppReleaseState ReleaseState;
    uint32_t ExistInPackageNums;
    bool bOwnsLicense;
    bool bFreeLicense;
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
