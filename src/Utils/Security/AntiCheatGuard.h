#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Security {

class AntiCheatGuard {
public:
    static void Initialize();
    static bool IsProtectedApp(uint32_t appId);
    static void AddProtectedApp(uint32_t appId);
    static void RemoveProtectedApp(uint32_t appId);
    static size_t Count();
};

} // namespace Security
