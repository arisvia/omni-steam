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

enum class EPackageStatus : uint32_t { Available = 0, Preorder = 1, Unavailable = 2 };
// ==============================================================================
// Steam Client Ownership & License Structures
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
