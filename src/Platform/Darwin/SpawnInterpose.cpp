// macOS spawn interposition: re-inject the core into Steam's child processes.
//
// Steam's macOS launch chain (measured on real Apple Silicon CI runners):
//   Steam.app/steam_osx -> exec Steam.AppBundle/.../steam_osx (same pid,
//   DYLD_INSERT_LIBRARIES survives) -> spawn FINAL host (new pid, sanitized
//   environment, DYLD dropped). Replacing the host binary with a wrapper is
//   undone by Steam's package integrity repair. Instead we interpose the
//   spawn/exec APIs inside the intermediate process (where the dylib IS
//   loaded) and re-add our absolute dylib path to every child's environment.
//   The host guard in main.cpp keeps non-Steam children inert.
//
// Interposers must not depend on spdlog/config state: they can run before
// InitializeOmniSteam finishes.

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <spawn.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using PosixSpawnFn = int (*)(pid_t*, const char*, const posix_spawn_file_actions_t*, const posix_spawnattr_t*,
                             char* const[], char* const[]);
using ExecveFn = int (*)(const char*, char* const[], char* const[]);

constexpr const char* kInsertVar = "DYLD_INSERT_LIBRARIES=";
constexpr size_t kInsertVarLen = 22; // strlen(kInsertVar)

std::string OurDylibPath() {
    static std::string path = []() {
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(&OurDylibPath), &info) != 0 && info.dli_fname != nullptr) {
            return std::string(info.dli_fname);
        }
        return std::string();
    }();
    return path;
}

struct InjectedEnv {
    std::vector<char*> env; // borrowed entries + one owned + nullptr
    char* owned = nullptr;
};

InjectedEnv BuildInjectedEnv(char* const envp[], const std::string& dylib) {
    InjectedEnv out;
    std::string existing;
    for (char* const* e = envp; *e != nullptr; ++e) {
        if (std::strncmp(*e, kInsertVar, kInsertVarLen) == 0) {
            existing = *e + kInsertVarLen;
        } else {
            out.env.push_back(*e);
        }
    }
    std::string value = dylib;
    if (!existing.empty() && existing.find(dylib) == std::string::npos) {
        value += ":";
        value += existing;
    }
    out.owned = strdup((std::string(kInsertVar) + value).c_str());
    out.env.push_back(out.owned);
    out.env.push_back(nullptr);
    return out;
}

int OmniPosixSpawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* fileActions,
                   const posix_spawnattr_t* attrp, char* const argv[], char* const envp[]) {
    auto real = reinterpret_cast<PosixSpawnFn>(dlsym(RTLD_NEXT, "posix_spawn"));
    if (real == nullptr) {
        return ENOSYS;
    }
    const std::string dylib = OurDylibPath();
    if (dylib.empty() || envp == nullptr) {
        // NULL envp inherits the parent's environ, which already carries the
        // insert variable in injected processes.
        return real(pid, path, fileActions, attrp, argv, envp);
    }
    InjectedEnv env = BuildInjectedEnv(envp, dylib);
    int rc = real(pid, path, fileActions, attrp, argv, env.env.data());
    std::free(env.owned);
    return rc;
}

int OmniPosixSpawnP(pid_t* pid, const char* file, const posix_spawn_file_actions_t* fileActions,
                    const posix_spawnattr_t* attrp, char* const argv[], char* const envp[]) {
    auto real = reinterpret_cast<PosixSpawnFn>(dlsym(RTLD_NEXT, "posix_spawnp"));
    if (real == nullptr) {
        return ENOSYS;
    }
    const std::string dylib = OurDylibPath();
    if (dylib.empty() || envp == nullptr) {
        return real(pid, file, fileActions, attrp, argv, envp);
    }
    InjectedEnv env = BuildInjectedEnv(envp, dylib);
    int rc = real(pid, file, fileActions, attrp, argv, env.env.data());
    std::free(env.owned);
    return rc;
}

int OmniExecve(const char* path, char* const argv[], char* const envp[]) {
    auto real = reinterpret_cast<ExecveFn>(dlsym(RTLD_NEXT, "execve"));
    if (real == nullptr) {
        errno = ENOSYS;
        return -1;
    }
    const std::string dylib = OurDylibPath();
    if (dylib.empty() || envp == nullptr) {
        return real(path, argv, envp);
    }
    InjectedEnv env = BuildInjectedEnv(envp, dylib);
    int rc = real(path, argv, env.env.data());
    std::free(env.owned); // only reached when exec failed
    return rc;
}

} // namespace

#define OMNI_DYLD_INTERPOSE(replacement, replacee)                                                                     \
    __attribute__((used)) static struct {                                                                              \
        const void* replacement;                                                                                       \
        const void* replacee;                                                                                          \
    } _interpose_##replacee __attribute__((section("__DATA,__interpose"))) = {                                         \
        (const void*)(unsigned long)&replacement, (const void*)(unsigned long)&replacee};

OMNI_DYLD_INTERPOSE(OmniPosixSpawn, posix_spawn)
OMNI_DYLD_INTERPOSE(OmniPosixSpawnP, posix_spawnp)
OMNI_DYLD_INTERPOSE(OmniExecve, execve)