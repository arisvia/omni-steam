#include "WebServer.h"

#include "ApiRouter.h"

#include <algorithm>
#include <atomic>
#include <cctype>
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
#include <sys/time.h>
#include <unistd.h>
#define closesocket close
#define SD_BOTH SHUT_RDWR
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace Manager {

namespace {

constexpr size_t kMaxRequestSize = 1024 * 1024;
constexpr int kRecvTimeoutSeconds = 15;
constexpr int kMaxConcurrentClients = 16;

std::atomic<bool> g_serverRunning{false};
std::atomic<int> g_activeClients{0};
std::thread g_acceptThread;
SOCKET g_listenSocket = INVALID_SOCKET;

size_t FindHeaderValue(const std::string& headers, const char* name) {
    std::string lowered = headers;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string needle = std::string(name);
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    needle += ':';
    size_t pos = lowered.find(needle);
    return pos == std::string::npos ? std::string::npos : pos + needle.length();
}

bool IsRequestComplete(const std::string& request) {
    size_t headerEnd = request.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return false;

    size_t valuePos = FindHeaderValue(request.substr(0, headerEnd), "Content-Length");
    if (valuePos == std::string::npos)
        return true;

    while (valuePos < headerEnd && (request[valuePos] == ' ' || request[valuePos] == '\t'))
        ++valuePos;
    size_t digits = 0;
    unsigned long long contentLength = 0;
    while (valuePos + digits < headerEnd && request[valuePos + digits] >= '0' && request[valuePos + digits] <= '9') {
        contentLength = contentLength * 10 + static_cast<unsigned long long>(request[valuePos + digits] - '0');
        ++digits;
        if (contentLength > kMaxRequestSize)
            return true;
    }
    size_t bodyStart = headerEnd + 4;
    return request.size() >= bodyStart + contentLength;
}

bool ReadFullRequest(SOCKET clientSocket, std::string& request) {
    char buffer[16384];
    while (request.size() <= kMaxRequestSize) {
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesRead <= 0)
            break;
        request.append(buffer, static_cast<size_t>(bytesRead));
        if (IsRequestComplete(request))
            return true;
    }
    return !request.empty();
}

bool SendAll(SOCKET clientSocket, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int chunk = static_cast<int>((data.size() - sent > 0x7FFFFFFF) ? 0x7FFFFFFF : (data.size() - sent));
        int n = send(clientSocket, data.data() + sent, chunk, 0);
        if (n <= 0)
            return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

void ApplyReceiveTimeout(SOCKET socket) {
#if defined(OMNI_PLATFORM_WINDOWS)
    DWORD timeoutMs = kRecvTimeoutSeconds * 1000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
    timeval tv{};
    tv.tv_sec = kRecvTimeoutSeconds;
    tv.tv_usec = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif
}

void HandleClient(SOCKET clientSocket) {
    ApplyReceiveTimeout(clientSocket);

    std::string request;
    if (ReadFullRequest(clientSocket, request)) {
        std::string response = ApiRouter::HandleRequest(request);
        SendAll(clientSocket, response);
    }
    closesocket(clientSocket);
    --g_activeClients;
}

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
        g_listenSocket = INVALID_SOCKET;
        return false;
    }

    if (listen(g_listenSocket, 16) == SOCKET_ERROR) {
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
        return false;
    }

    g_serverRunning.store(true);
    spdlog::info("WebServer: Listening on http://{}:{}", host, port);

    g_acceptThread = std::thread([]() {
        while (g_serverRunning.load()) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(g_listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (clientSocket == INVALID_SOCKET) {
                if (!g_serverRunning.load())
                    break;
                continue;
            }

            if (g_activeClients.load() >= kMaxConcurrentClients) {
                closesocket(clientSocket);
                continue;
            }

            ++g_activeClients;
            std::thread(HandleClient, clientSocket).detach();
        }
    });

    return true;
}

void WebServer::Stop() {
    if (!g_serverRunning.load())
        return;

    g_serverRunning.store(false);
    if (g_listenSocket != INVALID_SOCKET) {
#if defined(OMNI_PLATFORM_WINDOWS)
        shutdown(g_listenSocket, SD_BOTH);
#endif
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
    }

    if (g_acceptThread.joinable()) {
        g_acceptThread.join();
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
