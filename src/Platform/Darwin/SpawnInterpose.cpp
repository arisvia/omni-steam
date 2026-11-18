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
//   The updater's last hop may go through /bin/sh -> open -> LaunchServices
//   (launchd builds a fresh env); CI covers that with launchctl setenv.
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
int __posix_spawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* file_actions,
                  const posix_spawnattr_t* attrp, char* const argv[], char* const envp[]);
int __execve(const char* path, char* const argv[], char* const envp[]);
}

namespace {

std::string OurDylibPath() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&OurDylibPath), &info) != 0 && info.dli_fname != nullptr) {
        return std::string(info.dli_fname);
    }
    return {};
}

bool IsSteamHostPath(const char* path) {
    if (path == nullptr) {
        return false;
    }
    const char* base = std::strrchr(path, '/');
    base = (base != nullptr) ? base + 1 : path;
    // Only the Steam host itself. Helpers (Steam Helper, CEF) carry
    // library-validation hardening and abort on unsigned inserts.
    return std::strcmp(base, "steam_osx") == 0 || std::strcmp(base, "steam") == 0 || std::strcmp(base, "steam.exe") == 0;
}

// First few argv entries for the breadcrumb: reveals what Steam's launch
// shell actually runs (open/LaunchServices vs direct exec).
std::string ArgsBrief(char* const argv[]) {
    std::string out;
    for (int i = 0; argv != nullptr && argv[i] != nullptr && i < 5; ++i) {
        out += ' ';
        out += argv[i];
        if (out.size() > 160) {
            out += "...";
            break;
        }
    }
    return out;
}

struct InjectedEnv {
    char* owned = nullptr;
    std::vector<char*> env{};
};

// envp == NULL: the child inherits OUR environ, which already carries
// DYLD_INSERT_LIBRARIES - pass through untouched. Otherwise copy the
// caller's environment, replacing any existing insert entry.
InjectedEnv BuildInjectedEnv(char* const envp[], const std::string& dylib) {
    InjectedEnv result;
    if (envp == nullptr) {
        return result;
    }
    static const char kKey[] = "DYLD_INSERT_LIBRARIES=";
    constexpr std::size_t kKeyLen = sizeof(kKey) - 1;
    result.owned = static_cast<char*>(std::malloc(kKeyLen + dylib.size() + 1));
    if (result.owned == nullptr) {
        return result;
    }
    std::memcpy(result.owned, kKey, kKeyLen);
    std::memcpy(result.owned + kKeyLen, dylib.c_str(), dylib.size() + 1);
    for (std::size_t i = 0; envp[i] != nullptr; ++i) {
        if (std::strncmp(envp[i], kKey, kKeyLen) != 0) {
            result.env.push_back(envp[i]);
        }
    }
    result.env.push_back(result.owned);
    result.env.push_back(nullptr);
    return result;
}

int SpawnInjected(pid_t* pid, const char* path, const posix_spawn_file_actions_t* fileActions,
                  const posix_spawnattr_t* attrp, char* const argv[], char* const envp[]) {
    if (!IsSteamHostPath(path)) {
        return __posix_spawn(pid, path, fileActions, attrp, argv, envp);
    }
    const std::string dylib = OurDylibPath();
    if (dylib.empty()) {
        return __posix_spawn(pid, path, fileActions, attrp, argv, envp);
    }
    InjectedEnv env = BuildInjectedEnv(envp, dylib);
    char* const* useEnv = (env.env.empty() && env.owned == nullptr) ? envp : env.env.data();
    int rc = __posix_spawn(pid, path, fileActions, attrp, argv, useEnv);
    std::free(env.owned);
    return rc;
}

int OmniPosixSpawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* fileActions,
                   const posix_spawnattr_t* attrp, char* const argv[], char* const envp[]) {
    std::fprintf(stderr, "[omnisteam] interpose posix_spawn: %s%s envp=%s\n", path != nullptr ? path : "(null)",
                 ArgsBrief(argv).c_str(), envp != nullptr ? "set" : "null");
    return SpawnInjected(pid, path, fileActions, attrp, argv, envp);
}

int OmniPosixSpawnP(pid_t* pid, const char* file, const posix_spawn_file_actions_t* fileActions,
                    const posix_spawnattr_t* attrp, char* const argv[], char* const envp[]) {
    std::fprintf(stderr, "[omnisteam] interpose posix_spawnp: %s%s envp=%s\n", file, ArgsBrief(argv).c_str(),
                 envp != nullptr ? "set" : "null");
    if (file == nullptr) {
        errno = EINVAL;
        return EINVAL;
    }
    if (std::strchr(file, '/') != nullptr) {
        return SpawnInjected(pid, file, fileActions, attrp, argv, envp);
    }
    // PATH search, then spawn through the absolute-path interposer above.
    const char* paths = std::getenv("PATH");
    if (paths == nullptr) {
        paths = "/usr/bin:/bin:/usr/sbin:/sbin";
    }
    std::size_t start = 0;
    while (start <= std::strlen(paths)) {
        std::size_t end = std::strchr(paths + start, ':') != nullptr
                              ? static_cast<std::size_t>(std::strchr(paths + start, ':') - paths)
                              : std::strlen(paths);
        std::string dir(paths + start, end - start);
        if (!dir.empty()) {
            std::string candidate = dir + "/" + file;
            if (access(candidate.c_str(), X_OK) == 0) {
                return SpawnInjected(pid, candidate.c_str(), fileActions, attrp, argv, envp);
            }
        }
        if (end == std::strlen(paths)) {
            break;
        }
        start = end + 1;
    }
    errno = ENOENT;
    return ENOENT;
}

int OmniExecve(const char* path, char* const argv[], char* const envp[]) {
    std::fprintf(stderr, "[omnisteam] interpose execve: %s%s envp=%s\n", path != nullptr ? path : "(null)",
                 ArgsBrief(argv).c_str(), envp != nullptr ? "set" : "null");
    if (!IsSteamHostPath(path)) {
        return __execve(path, argv, envp);
    }
    const std::string dylib = OurDylibPath();
    if (dylib.empty()) {
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