#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Process {

class ProcessInjector {
public:
    static bool InjectForApp(uint32_t appId, uint32_t processId);
    static bool InjectModule(uint32_t processId, const std::string& modulePath);
};

} // namespace Process
