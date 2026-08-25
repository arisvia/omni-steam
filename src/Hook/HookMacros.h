#pragma once
#include "HookManager.h"

#include <cstdint>

#include "OmniPlatform/OmniPlatform.h"

#define RESOLVE_FUNC(name, retType, ...)                                                                               \
    typedef retType (*name##_t)(__VA_ARGS__);                                                                          \
    static name##_t o##name = nullptr

#define HOOK_FUNC(name, retType, ...)                                                                                  \
    typedef retType (*name##_t)(__VA_ARGS__);                                                                          \
    static name##_t o##name = nullptr;                                                                                 \
    static retType h##name(__VA_ARGS__)
#define HOOK_BEGIN() OmniPlatform::Detour::BeginTransaction()

#define ATTACH_HOOK(addr, name)                                                                                        \
    do {                                                                                                               \
        o##name = reinterpret_cast<name##_t>(addr);                                                                    \
        bool attached_ = (addr) != 0 && OmniPlatform::Detour::Attach(reinterpret_cast<void**>(&o##name),               \
                                                                     reinterpret_cast<void*>(h##name));                \
        HookManager::SetHookActive(#name, attached_);                                                                  \
    } while (0)

#define DETACH_HOOK(addr, name)                                                                                        \
    do {                                                                                                               \
        OmniPlatform::Detour::Detach(reinterpret_cast<void**>(&o##name), reinterpret_cast<void*>(h##name));            \
    } while (0)

#define INSTALL_HOOK_C(name)                                                                                           \
    OmniPlatform::Detour::Attach(reinterpret_cast<void**>(&o##name), reinterpret_cast<void*>(h##name))

#define UNINSTALL_HOOK_C(name)                                                                                         \
    OmniPlatform::Detour::Detach(reinterpret_cast<void**>(&o##name), reinterpret_cast<void*>(h##name))

#define HOOK_END() OmniPlatform::Detour::CommitTransaction()

#define UNHOOK_BEGIN() OmniPlatform::Detour::BeginTransaction()

#define UNHOOK_END() OmniPlatform::Detour::CommitTransaction()
