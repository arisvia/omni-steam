#pragma once
#include <string>

namespace Manager {

class ApiRouter {
public:
    static std::string HandleRequest(const std::string& request);
};

} // namespace Manager
