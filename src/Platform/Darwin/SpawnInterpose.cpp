// macOS spawn interposition: re-inject the core into Steam's child processes.
//
// Steam's macOS launch chain (measured on real Apple Silicon CI runners):
//   Steam.app/steam_osx -> exec Steam.AppBundle/.../steam_osx (same pid,
//   DYLD_INSERT_LIBRARIES survives) -> spawn FINAL host (new pid, sanitized
//   environment, DYLD dropped). Replacing the host binary with a wrapper is
//   undone by Steam's package integrity repair. Instead we interpose the
//   spawn/exec APIs inside the intermediate process (where the dylib IS
//   loaded) and re-add our absolute dylib path to every Steam-host child's
//   environment. The host guard in main.cpp keeps non-Steam children inert.
//
// Real-function resolution: bind DIRECTLY to the libsystem_kernel syscall
// stubs __posix_spawn / __execve. dlsym is unusable here - RTLD_NEXT cannot
// see libSystem from an inserted dylib, and handle/RTLD_DEFAULT lookups can
// return our own interposer, causing infinite recursion and stack overflow
// (run 44089747947: 58k nested execve wrappers, EXC_BAD_ACCESS in the stack
// guard inside dyld`dlsym <- RealSymbol <- OmniExecve). The __-prefixed
// stubs are not interposed by us and resolve two-level straight into
// libsystem_kernel.
//
// Interposers must not depend on spdlog/config state: they can run before
// InitializeOmniSteam finishes.

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <spawn.h>
#include <string>
#include <unistd.h>
#include <vector>

extern "C" {
// libsystem_kernel syscall stubs (stable exports, bypass our interposers).
int __posix_spawn(pid_t*, const char*, const posix_spawn_file_actions_t*, const posix_spawnattr_t*, char* const[],
                  char* const[]);
int __execve(const char*, char* const[], char* const[]);
}

namespace {

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

// Only re-inject into Steam HOST binaries. Steam's helpers (Steam Helper,
// CEF renderers) carry hardened-runtime / library-validation signatures:
// dyld aborts them when an unsigned dylib is inserted, killing the client.
bool IsSteamHostPath(const char* path) {
    if (path == nullptr) {
        return false;
    }
    const char* base = std::strrchr(path, '/');
    base = (base != nullptr) ? base + 1 : path;
    return std::strcmp(base, "steam_osx") == 0 || std::strcmp(base, "steam") == 0 ||
           std::strcmp(base, "steam.exe") == 0;
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

int SpawnInjected(pid_t* pid, const char* path, const posix_spawn_file_actions_t* fileActions,
                  const posix_spawnattr_t* attrp, char* const argv[], char* const envp[]) {
    const std::string dylib = OurDylibPath();
    if (dylib.empty() || envp == nullptr || !IsSteamHostPath(path)) {
        // NULL envp inherits the parent's environ, which already carries the
        // insert variable in injected processes.
        return __posix_spawn(pid, path, fileActions, attrp, argv, envp);
    }
    InjectedEnv env = BuildInjectedEnv(envp, dylib);
    int rc = __posix_spawn(pid, path, fileActions, attrp, argv, env.env.data());
    std::free(env.owned);
    return rc;
}

int OmniPosixSpawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* fileActions,
                   const posix_spawnattr_t* attrp, char* const argv[], char* const envp[]) {
    std::fprintf(stderr, "[omnisteam] interpose posix_spawn: %s envp=%s\n", path != nullptr ? path : "(null)",
                 envp != nullptr ? "set" : "null");
    return SpawnInjected(pid, path, fileActions, attrp, argv, envp);
}

int OmniPosixSpawnP(pid_t* pid, const char* file, const posix_spawn_file_actions_t* fileActions,
                    const posix_spawnattr_t* attrp, char* const argv[], char* const envp[]) {
    std::fprintf(stderr, "[omnisteam] interpose posix_spawnp: %s envp=%s\n", file != nullptr ? file : "(null)",
                 envp != nullptr ? "set" : "null");
    if (file == nullptr) {
        return EINVAL;
    }
    if (std::strchr(file, '/') != nullptr) {
        return SpawnInjected(pid, file, fileActions, attrp, argv, envp);
    }
    // PATH search, mirroring posix_spawnp semantics.
    const char* pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr) {
        pathEnv = "/usr/bin:/bin";
    }
    std::string paths(pathEnv);
    size_t start = 0;
    while (start <= paths.size()) {
        size_t end = paths.find(':', start);
        if (end == std::string::npos) {
            end = paths.size();
        }
        std::string dir = paths.substr(start, end - start);
        if (!dir.empty()) {
            std::string candidate = dir + "/" + file;
            if (access(candidate.c_str(), X_OK) == 0) {
                return SpawnInjected(pid, candidate.c_str(), fileActions, attrp, argv, envp);
            }
        }
        start = end + 1;
    }
    return ENOENT;
}

int OmniExecve(const char* path, char* const argv[], char* const envp[]) {
    std::fprintf(stderr, "[omnisteam] interpose execve: %s envp=%s\n", path != nullptr ? path : "(null)",
                 envp != nullptr ? "set" : "null");
    const std::string dylib = OurDylibPath();
    if (dylib.empty() || envp == nullptr || !IsSteamHostPath(path)) {
        return __execve(path, argv, envp);
    }
    InjectedEnv env = BuildInjectedEnv(envp, dylib);
    int rc = __execve(path, argv, env.env.data());
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