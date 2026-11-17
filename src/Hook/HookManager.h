#pragma once

#include <string>
#include <utility>
#include <vector>

namespace HookManager {
void InstallHooks();
void UninstallHooks();

// Records whether a named hook was actually attached (used by the Manager
// status endpoint so the UI never shows a fake green state).
void SetHookActive(const char* name, bool active);
bool IsHookActive(const char* name);

// Snapshot of every hook ever attempted (name, attached), sorted by name for
// deterministic serialization into core_status.json.
std::vector<std::pair<std::string, bool>> GetHookStates();
} // namespace HookManager
