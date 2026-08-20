#include "WebServer.h"

#include "ApiRouter.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>

#include "OmniPlatform/OmniPlatform.h"

#if defined(OMNI_PLATFORM_WINDOWS)
#include <winsock2.h>

#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace Manager {

namespace {
std::atomic<bool> g_serverRunning{false};
std::thread g_serverThread;
SOCKET g_listenSocket = INVALID_SOCKET;
} // namespace

bool WebServer::Start(const std::string& host, uint16_t port) {
    if (g_serverRunning.load())
        return true;

#if defined(OMNI_PLATFORM_WINDOWS)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    g_listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listenSocket == INVALID_SOCKET)
        return false;

    int opt = 1;
    setsockopt(g_listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr);

    if (bind(g_listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(g_listenSocket);
        return false;
    }

    if (listen(g_listenSocket, 10) == SOCKET_ERROR) {
        closesocket(g_listenSocket);
        return false;
    }

    g_serverRunning.store(true);
    spdlog::info("WebServer: Listening on http://{}:{}", host, port);

    g_serverThread = std::thread([]() {
        while (g_serverRunning.load()) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(g_listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (clientSocket == INVALID_SOCKET) {
                if (!g_serverRunning.load())
                    break;
                continue;
            }

            char buffer[65536];
            int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string response = ApiRouter::HandleRequest(buffer);
                send(clientSocket, response.c_str(), static_cast<int>(response.length()), 0);
            }
            closesocket(clientSocket);
        }
    });

    return true;
}

void WebServer::Stop() {
    if (!g_serverRunning.load())
        return;

    g_serverRunning.store(false);
    if (g_listenSocket != INVALID_SOCKET) {
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
    }

    if (g_serverThread.joinable()) {
        g_serverThread.join();
    }

#if defined(OMNI_PLATFORM_WINDOWS)
    WSACleanup();
#endif
    spdlog::info("WebServer: Stopped");
}

bool WebServer::IsRunning() {
    return g_serverRunning.load();
}

} // namespace Manager
