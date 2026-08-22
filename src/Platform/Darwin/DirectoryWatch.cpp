#include <atomic>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

namespace {
std::atomic<bool> g_watching{false};
std::thread g_watchThread;
} // namespace

bool DirectoryWatch::StartWatch(const std::vector<std::string>& directories, Callback onChange) {
    if (directories.empty() || !onChange || g_watching.load())
        return false;

    g_watching.store(true);
    g_watchThread = std::thread([directories, onChange]() {
        int kq = kqueue();
        if (kq < 0) {
            g_watching.store(false);
            return;
        }

        std::vector<int> fds;
        std::vector<struct kevent> changeEvents;
        for (const auto& dir : directories) {
            int fd = open(dir.c_str(), O_RDONLY);
            if (fd >= 0) {
                fds.push_back(fd);
                struct kevent ev;
                EV_SET(&ev, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
                       NOTE_WRITE | NOTE_DELETE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_RENAME, 0, nullptr);
                changeEvents.push_back(ev);
            }
        }

        if (fds.empty()) {
            close(kq);
            g_watching.store(false);
            return;
        }

        kevent(kq, changeEvents.data(), static_cast<int>(changeEvents.size()), nullptr, 0, nullptr);

        while (g_watching.load()) {
            struct timespec timeout{1, 0};
            struct kevent eventList[16];
            int nev = kevent(kq, nullptr, 0, eventList, 16, &timeout);
            if (nev > 0) {
                for (int i = 0; i < nev; ++i) {
                    int activeFd = static_cast<int>(eventList[i].ident);
                    for (size_t d = 0; d < fds.size(); ++d) {
                        if (fds[d] == activeFd) {
                            try {
                                for (const auto& entry : std::filesystem::directory_iterator(directories[d])) {
                                    if (entry.is_regular_file()) {
                                        onChange(entry.path().generic_string(), false);
                                    }
                                }
                            } catch (...) {
                            }
                            break;
                        }
                    }
                }
            }
        }

        for (int fd : fds) {
            close(fd);
        }
        close(kq);
    });
    return true;
}

void DirectoryWatch::StopWatch() {
    if (g_watching.load()) {
        g_watching.store(false);
        if (g_watchThread.joinable()) {
            g_watchThread.join();
        }
    }
}

} // namespace OmniPlatform
