#pragma once

#include <string>
#include <vector>

namespace ConfigFileWatcher {

void Start(const std::string& luaDir);
void Stop();

} // namespace ConfigFileWatcher
