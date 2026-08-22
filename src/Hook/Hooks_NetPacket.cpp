#include "Hooks_NetPacket.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/ManifestClient.h"
#include "Utils/Metadata/PatternLoader.h"

#include "Hook/HookMacros.h"

namespace {

constexpr uint32_t kMsgHdrProtoFlag = 0x80000000;
constexpr uint32_t k_EMsgServiceMethodCallFromClient = 151;
constexpr uint32_t k_EMsgServiceMethodResponse = 147;
constexpr int32_t k_EResultOK = 1;

constexpr uint32_t kMaxBodySize = 65536;
constexpr uint32_t kMaxHdrSize = 1024;
constexpr uint32_t kMaxPacketSize = 8 + kMaxHdrSize + kMaxBodySize;
constexpr int kPacketPoolSize = 8;

#pragma pack(push, 1)
struct MsgHdr {
    uint32_t eMsg;
    uint32_t headerLength;
};

struct CNetPacket {
    uint32_t m_hConnection;
    uint8_t* m_pubData;
    uint32_t m_cubData;
};
#pragma pack(pop)

uint8_t g_RecvPacketPool[kPacketPoolSize][kMaxPacketSize];
int g_RecvPacketPoolIdx = 0;

std::unordered_map<uint64_t, std::shared_future<uint64_t>> g_ManifestFutures;
std::mutex g_ManifestFuturesMutex;

// Lightweight Protobuf varint helper
inline uint64_t ReadVarint(const uint8_t*& ptr, const uint8_t* end) {
    uint64_t val = 0;
    int shift = 0;
    while (ptr < end && shift < 64) {
        uint8_t b = *ptr++;
        val |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0)
            break;
        shift += 7;
    }
    return val;
}

inline void WriteVarint(std::vector<uint8_t>& buf, uint64_t val) {
    while (val >= 0x80) {
        buf.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
        val >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(val & 0x7F));
}

// Check and unpack packet
inline bool UnpackRaw(const uint8_t* data, uint32_t size, uint32_t& eMsg, const uint8_t*& pHdr, uint32_t& cbHdr,
                      const uint8_t*& pBody, uint32_t& cbBody) {
    if (!data || size < sizeof(MsgHdr))
        return false;
    const auto* hdr = reinterpret_cast<const MsgHdr*>(data);
    if (!(hdr->eMsg & kMsgHdrProtoFlag))
        return false;

    eMsg = hdr->eMsg & ~kMsgHdrProtoFlag;
    cbHdr = hdr->headerLength;
    uint32_t off = sizeof(MsgHdr) + cbHdr;
    if (off > size)
        return false;

    pHdr = data + sizeof(MsgHdr);
    pBody = data + off;
    cbBody = size - off;
    return true;
}

// Parse jobid_source and target_job_name from CMsgProtoBufHeader
bool ParseProtoHeader(const uint8_t* pHdr, uint32_t cbHdr, uint64_t& jobid_source, uint64_t& jobid_target,
                      std::string& target_job_name) {
    const uint8_t* ptr = pHdr;
    const uint8_t* end = pHdr + cbHdr;
    jobid_source = 0;
    jobid_target = 0;
    target_job_name.clear();

    while (ptr < end) {
        uint64_t tag = ReadVarint(ptr, end);
        uint32_t fieldNumber = static_cast<uint32_t>(tag >> 3);
        uint32_t wireType = static_cast<uint32_t>(tag & 7);

        if (wireType == 0) { // Varint
            ReadVarint(ptr, end);
        } else if (wireType == 1) { // 64-bit fixed
            if (ptr + 8 > end)
                break;
            if (fieldNumber == 10) { // jobid_source
                std::memcpy(&jobid_source, ptr, 8);
            } else if (fieldNumber == 11) { // jobid_target
                std::memcpy(&jobid_target, ptr, 8);
            }
            ptr += 8;
        } else if (wireType == 2) { // Length-delimited
            uint64_t len = ReadVarint(ptr, end);
            if (ptr + len > end)
                break;
            if (fieldNumber == 12) { // target_job_name
                target_job_name.assign(reinterpret_cast<const char*>(ptr), len);
            }
            ptr += len;
        } else if (wireType == 5) { // 32-bit fixed
            if (ptr + 4 > end)
                break;
            ptr += 4;
        } else {
            break;
        }
    }
    return true;
}

// Parse GetManifestRequestCode request
bool ParseManifestRequest(const uint8_t* pBody, uint32_t cbBody, uint32_t& appId, uint32_t& depotId,
                          uint64_t& manifestId) {
    const uint8_t* ptr = pBody;
    const uint8_t* end = pBody + cbBody;
    appId = 0;
    depotId = 0;
    manifestId = 0;

    while (ptr < end) {
        uint64_t tag = ReadVarint(ptr, end);
        uint32_t fieldNumber = static_cast<uint32_t>(tag >> 3);
        uint32_t wireType = static_cast<uint32_t>(tag & 7);

        if (wireType == 0) { // Varint
            uint64_t val = ReadVarint(ptr, end);
            if (fieldNumber == 1)
                appId = static_cast<uint32_t>(val);
            else if (fieldNumber == 2)
                depotId = static_cast<uint32_t>(val);
            else if (fieldNumber == 3)
                manifestId = val;
        } else if (wireType == 1) {
            ptr += 8;
        } else if (wireType == 2) {
            uint64_t len = ReadVarint(ptr, end);
            ptr += len;
        } else if (wireType == 5) {
            ptr += 4;
        } else {
            break;
        }
    }
    return depotId != 0 && manifestId != 0;
}

HOOK_FUNC(BBuildAndAsyncSendFrame, bool, void* pObject, int eWebSocketOpCode, uint8_t* pubData, uint32_t cubData) {
    uint32_t eMsg = 0, cbHdr = 0, cbBody = 0;
    const uint8_t *pHdr = nullptr, *pBody = nullptr;

    if (UnpackRaw(pubData, cubData, eMsg, pHdr, cbHdr, pBody, cbBody)) {
        if (eMsg == k_EMsgServiceMethodCallFromClient) {
            uint64_t jobid_source = 0, jobid_target = 0;
            std::string target_job_name;
            ParseProtoHeader(pHdr, cbHdr, jobid_source, jobid_target, target_job_name);

            if (target_job_name == "ContentServerDirectory.GetManifestRequestCode#1") {
                uint32_t appId = 0, depotId = 0;
                uint64_t manifestId = 0;
                if (ParseManifestRequest(pBody, cbBody, appId, depotId, manifestId)) {
                    spdlog::info("Hooks_NetPacket: Intercepted GetManifestRequestCode request (Depot: {}, GID: {}, "
                                 "JobId: {})",
                                 depotId, manifestId, jobid_source);

                    auto task = std::async(std::launch::async, [manifestId]() -> uint64_t {
                        uint64_t code = 0;
                        ManifestClient::FetchManifestRequestCode(manifestId, &code);
                        return code;
                    });

                    std::lock_guard<std::mutex> lock(g_ManifestFuturesMutex);
                    g_ManifestFutures[jobid_source] = task.share();
                }
            }
        }
    }
    return oBBuildAndAsyncSendFrame ? oBBuildAndAsyncSendFrame(pObject, eWebSocketOpCode, pubData, cubData) : false;
}

HOOK_FUNC(RecvPkt, void*, void* pThis, CNetPacket* pPacket) {
    if (pPacket && pPacket->m_pubData && pPacket->m_cubData >= sizeof(MsgHdr)) {
        uint32_t eMsg = 0, cbHdr = 0, cbBody = 0;
        const uint8_t *pHdr = nullptr, *pBody = nullptr;

        if (UnpackRaw(pPacket->m_pubData, pPacket->m_cubData, eMsg, pHdr, cbHdr, pBody, cbBody)) {
            if (eMsg == k_EMsgServiceMethodResponse) {
                uint64_t jobid_source = 0, jobid_target = 0;
                std::string target_job_name;
                ParseProtoHeader(pHdr, cbHdr, jobid_source, jobid_target, target_job_name);

                std::shared_future<uint64_t> future;
                bool hasFuture = false;
                {
                    std::lock_guard<std::mutex> lock(g_ManifestFuturesMutex);
                    auto it = g_ManifestFutures.find(jobid_target);
                    if (it != g_ManifestFutures.end()) {
                        future = it->second;
                        g_ManifestFutures.erase(it);
                        hasFuture = true;
                    }
                }

                if (hasFuture) {
                    auto status = future.wait_for(std::chrono::seconds(8));
                    uint64_t code = (status == std::future_status::ready) ? future.get() : 0;

                    if (code != 0) {
                        spdlog::info("Hooks_NetPacket: Injecting manifest_request_code {} into response (JobId: {})",
                                     code, jobid_target);

                        // 1. Build modified Header (set eresult = 1 / k_EResultOK)
                        std::vector<uint8_t> newHdr;
                        // tag for eresult: (13 << 3) | 0 = 0x68
                        // tag for jobid_target: (11 << 3) | 1 = 0x59
                        newHdr.push_back(0x59);
                        newHdr.resize(newHdr.size() + 8);
                        std::memcpy(&newHdr[newHdr.size() - 8], &jobid_target, 8);
                        newHdr.push_back(0x68);
                        WriteVarint(newHdr, static_cast<uint64_t>(k_EResultOK));

                        // 2. Build modified Body (tag for manifest_request_code: (1 << 3) | 0 = 0x08)
                        std::vector<uint8_t> newBody;
                        newBody.push_back(0x08);
                        WriteVarint(newBody, code);

                        // 3. Assemble complete packet into pool
                        uint32_t totalSize = sizeof(MsgHdr) + static_cast<uint32_t>(newHdr.size() + newBody.size());
                        if (totalSize <= kMaxPacketSize) {
                            uint8_t* poolBuf = g_RecvPacketPool[g_RecvPacketPoolIdx];
                            g_RecvPacketPoolIdx = (g_RecvPacketPoolIdx + 1) % kPacketPoolSize;

                            auto* outHdr = reinterpret_cast<MsgHdr*>(poolBuf);
                            outHdr->eMsg = k_EMsgServiceMethodResponse | kMsgHdrProtoFlag;
                            outHdr->headerLength = static_cast<uint32_t>(newHdr.size());

                            std::memcpy(poolBuf + sizeof(MsgHdr), newHdr.data(), newHdr.size());
                            std::memcpy(poolBuf + sizeof(MsgHdr) + newHdr.size(), newBody.data(), newBody.size());

                            pPacket->m_pubData = poolBuf;
                            pPacket->m_cubData = totalSize;
                        }
                    }
                }
            }
        }
    }
    return oRecvPkt ? oRecvPkt(pThis, pPacket) : nullptr;
}

} // namespace

namespace Hooks_NetPacket {

void Install() {
    uintptr_t fnSend = PatternLoader::GetFunctionAddress("BBuildAndAsyncSendFrame");
    if (fnSend != 0) {
        ATTACH_HOOK(fnSend, BBuildAndAsyncSendFrame);
        spdlog::info("Hooks_NetPacket: Successfully installed BBuildAndAsyncSendFrame hook at {:p}",
                     reinterpret_cast<void*>(fnSend));
    } else {
        spdlog::warn("Hooks_NetPacket: BBuildAndAsyncSendFrame signature not resolved");
    }

    uintptr_t fnRecv = PatternLoader::GetFunctionAddress("RecvPkt");
    if (fnRecv != 0) {
        ATTACH_HOOK(fnRecv, RecvPkt);
        spdlog::info("Hooks_NetPacket: Successfully installed RecvPkt hook at {:p}", reinterpret_cast<void*>(fnRecv));
    } else {
        spdlog::warn("Hooks_NetPacket: RecvPkt signature not resolved");
    }
}

void Uninstall() {}

} // namespace Hooks_NetPacket
