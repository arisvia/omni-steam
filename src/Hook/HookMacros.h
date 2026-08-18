#pragma once
#include "OmniPlatform/OmniPlatform.h"
#include <cstdint>

#define HOOK_FUNC(name, retType, ...) \
    typedef retType (*name##_t)(__VA_ARGS__); \
    static name##_t o##name = nullptr; \
    static retType h##name(__VA_ARGS__)

#define HOOK_BEGIN() \
    OmniPlatform::Detour::BeginTransaction()

#define INSTALL_HOOK_C(name) \
    OmniPlatform::Detour::Attach(reinterpret_cast<void**>(&o##name), reinterpret_cast<void*>(h##name))

#define UNINSTALL_HOOK_C(name) \
    OmniPlatform::Detour::Detach(reinterpret_cast<void**>(&o##name), reinterpret_cast<void*>(h##name))

#define HOOK_END() \
    OmniPlatform::Detour::CommitTransaction()

#define UNHOOK_BEGIN() \
    OmniPlatform::Detour::BeginTransaction()

#define UNHOOK_END() \
    OmniPlatform::Detour::CommitTransaction()
