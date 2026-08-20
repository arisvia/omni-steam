#include <atomic>
#include <map>
#include <poll.h>
#include <string>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

namespace {
std::atomic<bool> g_watching{false};
std::thread g_watchThread;
int g_inotifyFd = -1;
} // namespace

bool DirectoryWatch::StartWatch(const std::vector<std::string>& directories, Callback onChange) {
    if (g_watching.load())
        return true;
    g_inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (g_inotifyFd < 0)
        return false;

    std::map<int, std::string> watchDescriptors;
    for (const auto& dir : directories) {
        int wd = inotify_add_watch(g_inotifyFd, dir.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE | IN_CLOSE_WRITE);
        if (wd >= 0)
            watchDescriptors[wd] = dir;
    }

    if (watchDescriptors.empty()) {
        close(g_inotifyFd);
        g_inotifyFd = -1;
        return false;
    }

    g_watching.store(true);
    g_watchThread = std::thread([onChange, watchDescriptors]() {
        char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
        struct pollfd pfd = {g_inotifyFd, POLLIN, 0};

        while (g_watching.load()) {
            int pollRet = poll(&pfd, 1, 500);
            if (pollRet > 0 && (pfd.revents & POLLIN)) {
                ssize_t len = read(g_inotifyFd, buffer, sizeof(buffer));
                if (len < 0)
                    continue;

                const struct inotify_event* event;
                for (char* ptr = buffer; ptr < buffer + len; ptr += sizeof(struct inotify_event) + event->len) {
                    event = reinterpret_cast<const struct inotify_event*>(ptr);
                    if (event->len > 0) {
                        auto it = watchDescriptors.find(event->wd);
                        if (it != watchDescriptors.end()) {
                            std::string fullPath = it->second + "/" + std::string(event->name);
                            bool isDir = (event->mask & IN_ISDIR) != 0;
                            onChange(fullPath, isDir);
                        }
                    }
                }
            }
        }
    });

    return true;
}

void DirectoryWatch::StopWatch() {
    if (!g_watching.load())
        return;
    g_watching.store(false);
    if (g_watchThread.joinable())
        g_watchThread.join();
    if (g_inotifyFd >= 0) {
        close(g_inotifyFd);
        g_inotifyFd = -1;
    }
}

} // namespace OmniPlatform
