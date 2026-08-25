#pragma once

namespace HookManager {
void InstallHooks();
void UninstallHooks();

// Records whether a named hook was actually attached (used by the Manager
// status endpoint so the UI never shows a fake green state).
void SetHookActive(const char* name, bool active);
bool IsHookActive(const char* name);
} // namespace HookManager
