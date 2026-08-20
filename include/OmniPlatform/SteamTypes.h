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

enum class EAppReleaseState : uint32_t { Unknown = 0, Unavailable = 1, PreloadOnly = 2, Released = 3 };

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
    uint32_t PackageFlags;
    uint32_t AccountId;
    CUtlVector<AppId_t> AppIdVec;
    CUtlVector<DepotId_t> DepotIdVec;
};
