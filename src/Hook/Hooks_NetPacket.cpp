#include "Hooks_NetPacket.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <future>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/SteamTypes.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/ManifestClient.h"
#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Metadata/PicsTokenInjector.h"
#include "Utils/Metadata/ProtoFields.h"
#include "Utils/Metadata/StatsClient.h"

#include "Hook/HookMacros.h"
#include "Hook/Hooks_Misc.h"

namespace {

constexpr int kPacketPoolSize = 32;
constexpr uint32_t kMaxBodySize = 65536;
constexpr uint32_t kMaxHdrSize = 1024;
constexpr uint32_t kMaxPacketSize = 8 + kMaxHdrSize + kMaxBodySize;

// How long RecvPkt may block waiting for the upstream request-code lookup.
// Steam re-issues GetManifestRequestCode roughly every 45s when it fails, and
// successful lookups land in ManifestClient's persistent cache, so a short
// bound avoids stalling the CM receive path while guaranteeing convergence on
// the next attempt.
constexpr auto kManifestRecvWait = std::chrono::milliseconds(2500);

struct CNetPacket {
    void* m_pVTable;
    uint8_t* m_pubData;
    uint32_t m_cubData;
};

#pragma pack(push, 1)
struct MsgHdr {
    uint32_t eMsg;
    uint32_t headerLength;
};
#pragma pack(pop)

uint8_t g_RecvPacketPool[kPacketPoolSize][kMaxPacketSize];
int g_RecvPacketPoolIdx = 0;
std::mutex g_PoolMutex;

uint8_t* AcquirePacketSlot(size_t needed) {
    if (needed > kMaxPacketSize)
        return nullptr;
    std::lock_guard<std::mutex> lock(g_PoolMutex);
    uint8_t* buf = g_RecvPacketPool[g_RecvPacketPoolIdx];
    g_RecvPacketPoolIdx = (g_RecvPacketPoolIdx + 1) % kPacketPoolSize;
    return buf;
}

std::unordered_map<uint64_t, std::shared_future<uint64_t>> g_ManifestFutures;
std::unordered_map<uint64_t, uint64_t> g_ManifestInstantCodes;
std::mutex g_ManifestMutex;

// jobid_source -> appid for in-flight Player.GetUserStats requests
std::unordered_map<uint64_t, AppId_t> g_StatsJobToAppId;
std::mutex g_StatsJobMutex;

void RememberStatsJob(uint64_t jobId, AppId_t appId) {
    std::lock_guard<std::mutex> lock(g_StatsJobMutex);
    if (g_StatsJobToAppId.size() > 512) {
        g_StatsJobToAppId.clear();
    }
    g_StatsJobToAppId[jobId] = appId;
}

AppId_t TakeStatsJob(uint64_t jobId) {
    std::lock_guard<std::mutex> lock(g_StatsJobMutex);
    auto it = g_StatsJobToAppId.find(jobId);
    if (it == g_StatsJobToAppId.end())
        return 0;
    AppId_t appId = it->second;
    g_StatsJobToAppId.erase(it);
    return appId;
}

std::deque<std::vector<uint8_t>> g_LegacyKeyQueue;
std::mutex g_LegacyKeyMutex;

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

        if (wireType == 0) {
            uint64_t val = ReadVarint(ptr, end);
            if (fieldNumber == 10)
                jobid_source = val;
            else if (fieldNumber == 11)
                jobid_target = val;
        } else if (wireType == 1) {
            if (end - ptr < 8)
                break;
            if (fieldNumber == 10) {
                std::memcpy(&jobid_source, ptr, 8);
            } else if (fieldNumber == 11) {
                std::memcpy(&jobid_target, ptr, 8);
            }
            ptr += 8;
        } else if (wireType == 2) {
            uint64_t len = ReadVarint(ptr, end);
            if (len > static_cast<uint64_t>(end - ptr))
                break;
            if (fieldNumber == 12) {
                target_job_name.assign(reinterpret_cast<const char*>(ptr), static_cast<size_t>(len));
            }
            ptr += static_cast<size_t>(len);
        } else if (wireType == 5) {
            if (end - ptr < 4)
                break;
            ptr += 4;
        } else {
            break;
        }
    }
    return true;
}

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

        if (wireType == 0) {
            uint64_t val = ReadVarint(ptr, end);
            if (fieldNumber == 1)
                appId = static_cast<uint32_t>(val);
            else if (fieldNumber == 2)
                depotId = static_cast<uint32_t>(val);
            else if (fieldNumber == 3)
                manifestId = val;
        } else if (wireType == 1) {
            if (end - ptr < 8)
                break;
            ptr += 8;
        } else if (wireType == 2) {
            uint64_t len = ReadVarint(ptr, end);
            if (len > static_cast<uint64_t>(end - ptr))
                break;
            ptr += static_cast<size_t>(len);
        } else if (wireType == 5) {
            if (end - ptr < 4)
                break;
            ptr += 4;
        } else {
            break;
        }
    }
    return depotId != 0 && manifestId != 0;
}

// Assembles a replacement frame into the packet pool. eMsg carries the
// proto flag already; header bytes are preserved verbatim.
uint8_t* BuildPooledFrame(uint32_t eMsgWithFlag, const uint8_t* pHdr, uint32_t cbHdr,
                          const std::vector<uint8_t>& newBody, uint32_t* outSize) {
    size_t totalSize = sizeof(MsgHdr) + cbHdr + newBody.size();
    uint8_t* poolBuf = AcquirePacketSlot(totalSize);
    if (!poolBuf)
        return nullptr;

    auto* outHdr = reinterpret_cast<MsgHdr*>(poolBuf);
    outHdr->eMsg = eMsgWithFlag;
    outHdr->headerLength = cbHdr;
    if (cbHdr)
        std::memcpy(poolBuf + sizeof(MsgHdr), pHdr, cbHdr);
    std::memcpy(poolBuf + sizeof(MsgHdr) + cbHdr, newBody.data(), newBody.size());
    *outSize = static_cast<uint32_t>(totalSize);
    return poolBuf;
}

bool TryInjectRequestCode(uint64_t code, const uint8_t* pHdr, uint32_t cbHdr, const uint8_t* pBody, uint32_t cbBody,
                          CNetPacket* pPacket) {
    // Append-only protobuf override: scalar fields parsed later win, so
    // appending eresult / manifest_request_code preserves every original
    // header and body field untouched.
    std::vector<uint8_t> newHdr(pHdr, pHdr + cbHdr);
    newHdr.push_back(kProtoTagEresultVarint);
    WriteVarint(newHdr, static_cast<uint64_t>(k_EResultOK));

    std::vector<uint8_t> newBody(pBody, pBody + cbBody);
    newBody.push_back(kProtoTagManifestRequestCode);
    WriteVarint(newBody, code);

    uint32_t newSize = 0;
    uint8_t* poolBuf = BuildPooledFrame(k_EMsgServiceMethodResponse | kMsgHdrProtoFlag, newHdr.data(),
                                        static_cast<uint32_t>(newHdr.size()), newBody, &newSize);
    if (!poolBuf)
        return false;

    pPacket->m_pubData = poolBuf;
    pPacket->m_cubData = newSize;
    return true;
}

// ---- Achievement/stats spoofing (donor SteamID) --------------------------
// Field numbers per SteamKit protos:
//   CPlayer_GetUserStats_Request  : steamid=1(varint) appid=2 sha_schema=3(bytes)
//   CPlayer_GetUserStats_Response : stats=4(repeated msg)
//   CMsgClientGetUserStats        : game_id=1(fixed64) schema_local_version=3 steam_id_for_user=4(fixed64)
//   CMsgClientGetUserStatsResponse: game_id=1(fixed64) eresult=2 crc_stats=3 stats=5 achievement_blocks=6

bool TryPatchGetUserStatsRequest(const uint8_t* pBody, uint32_t cbBody, std::vector<uint8_t>& out, AppId_t* outAppId) {
    auto appIdValue = ProtoFields::GetVarintField(pBody, cbBody, 2);
    if (!appIdValue || *appIdValue == 0)
        return false;
    AppId_t appId = static_cast<AppId_t>(*appIdValue);
    if (!LuaConfig::HasDepot(appId))
        return false;
    if (ProtoFields::HasField(pBody, cbBody, 3))
        return false; // sha_schema present: a real schema sync must not be spoofed

    uint64_t donor = 0;
    if (!StatsClient::GetDonorSteamId(appId, &donor))
        return false;

    out.assign(pBody, pBody + cbBody);
    ProtoFields::AppendVarintField(out, 1, donor);
    *outAppId = appId;
    return true;
}

bool TryPatchGetUserStatsLegacyRequest(const uint8_t* pBody, uint32_t cbBody, std::vector<uint8_t>& out,
                                       AppId_t* outAppId) {
    uint32_t wire = 0;
    auto gameId = ProtoFields::GetScalarField(pBody, cbBody, 1, &wire);
    if (!gameId || wire != ProtoFields::WireFixed64)
        return false;
    AppId_t appId = static_cast<AppId_t>(*gameId & 0xFFFFFFFFull);
    if (!LuaConfig::HasDepot(appId))
        return false;

    uint64_t donor = 0;
    if (!StatsClient::GetDonorSteamId(appId, &donor))
        return false;

    out.assign(pBody, pBody + cbBody);
    ProtoFields::AppendVarintField(out, 3, static_cast<uint64_t>(-1)); // schema_local_version = -1
    ProtoFields::AppendFixed64Field(out, 4, donor);
    *outAppId = appId;
    return true;
}

void HandleGetUserStatsResponse(uint64_t jobId, const uint8_t* pHdr, uint32_t cbHdr, const uint8_t* pBody,
                                uint32_t cbBody, CNetPacket* pPacket) {
    AppId_t appId = TakeStatsJob(jobId);
    if (appId == 0 || !LuaConfig::HasDepot(appId))
        return;

    auto pruned = ProtoFields::WithoutFields(pBody, cbBody, {4});
    if (!pruned)
        return;
    // NOTE: CPlayer_GetUserStats_Response carries no eresult field - the
    // result lives in the CM proto header, patched below. Field 2 here is
    // crc_stats and must be left untouched.

    std::vector<uint8_t> newHdr(pHdr, pHdr + cbHdr);
    newHdr.push_back(kProtoTagEresultVarint);
    WriteVarint(newHdr, static_cast<uint64_t>(k_EResultOK));

    uint32_t newSize = 0;
    uint8_t* poolBuf = BuildPooledFrame(k_EMsgServiceMethodResponse | kMsgHdrProtoFlag, newHdr.data(),
                                        static_cast<uint32_t>(newHdr.size()), *pruned, &newSize);
    if (poolBuf) {
        pPacket->m_pubData = poolBuf;
        pPacket->m_cubData = newSize;
        spdlog::info("Hooks_NetPacket: Spoofed GetUserStats response for AppID {} (server stats stripped)", appId);
    }
}

void HandleGetUserStatsLegacyResponse(const uint8_t* pHdr, uint32_t cbHdr, const uint8_t* pBody, uint32_t cbBody,
                                      CNetPacket* pPacket) {
    uint32_t wire = 0;
    auto gameId = ProtoFields::GetScalarField(pBody, cbBody, 1, &wire);
    if (!gameId || wire != ProtoFields::WireFixed64)
        return;
    AppId_t appId = static_cast<AppId_t>(*gameId & 0xFFFFFFFFull);
    if (!LuaConfig::HasDepot(appId))
        return;

    auto eresult = ProtoFields::GetVarintField(pBody, cbBody, 2);
    if (eresult && *eresult == static_cast<uint64_t>(k_EResultOK))
        return;

    auto pruned = ProtoFields::WithoutFields(pBody, cbBody, {5, 6});
    if (!pruned)
        return;
    ProtoFields::AppendVarintField(*pruned, 2, static_cast<uint64_t>(k_EResultOK));
    ProtoFields::AppendVarintField(*pruned, 3, 0);

    uint32_t newSize = 0;
    uint8_t* poolBuf =
        BuildPooledFrame(k_EMsgClientGetUserStatsResponse | kMsgHdrProtoFlag, pHdr, cbHdr, *pruned, &newSize);
    if (poolBuf) {
        pPacket->m_pubData = poolBuf;
        pPacket->m_cubData = newSize;
        spdlog::info("Hooks_NetPacket: Spoofed GetUserStats legacy response for AppID {}", appId);
    }
}

HOOK_FUNC(BBuildAndAsyncSendFrame, bool, void* pObject, int eWebSocketOpCode, uint8_t* pubData, uint32_t cubData) {
    // 1. Intercept non-proto Legacy CD-Key Request (eMsg 730)
    if (pubData && cubData >= sizeof(ExtendedMsgHdr) + sizeof(MsgClientGetLegacyGameKey)) {
        const auto* reqHdr = reinterpret_cast<const ExtendedMsgHdr*>(pubData);
        if (!(reqHdr->eMsg & kMsgHdrProtoFlag) && reqHdr->eMsg == k_EMsgClientGetLegacyGameKey) {
            const auto* reqBody = reinterpret_cast<const MsgClientGetLegacyGameKey*>(pubData + sizeof(ExtendedMsgHdr));
            AppId_t appId = reqBody->m_unAppId;

            if (LuaConfig::HasApp(appId) || LuaConfig::HasDepot(appId)) {
                std::string syntheticKey = "OMNI-STEAM-FREE-PLAY-KEY";
                uint32_t cchKey = static_cast<uint32_t>(syntheticKey.size() + 1);
                uint32_t totalSize = sizeof(ExtendedMsgHdr) + sizeof(MsgClientGetLegacyGameKeyResponse) + cchKey;

                if (totalSize <= kMaxPacketSize) {
                    std::vector<uint8_t> respPkt(totalSize);
                    auto* respHdr = reinterpret_cast<ExtendedMsgHdr*>(respPkt.data());
                    *respHdr = *reqHdr;
                    respHdr->eMsg = k_EMsgClientGetLegacyGameKeyResponse;
                    respHdr->targetJobID = reqHdr->sourceJobID;
                    respHdr->sourceJobID = 0;

                    auto* respBody =
                        reinterpret_cast<MsgClientGetLegacyGameKeyResponse*>(respPkt.data() + sizeof(ExtendedMsgHdr));
                    respBody->m_unAppId = appId;
                    respBody->m_eResult = k_EResultOK;
                    respBody->m_cchKey = cchKey;
                    std::memcpy(respPkt.data() + sizeof(ExtendedMsgHdr) + sizeof(MsgClientGetLegacyGameKeyResponse),
                                syntheticKey.c_str(), cchKey);

                    {
                        std::lock_guard<std::mutex> lock(g_LegacyKeyMutex);
                        g_LegacyKeyQueue.push_back(std::move(respPkt));
                    }
                    spdlog::info("Hooks_NetPacket: Intercepted LegacyKey request for AppID {}, synthesized CD-Key "
                                 "response (suppressing real send)",
                                 appId);
                    return true;
                }
            }
        }
    }

    // 2. Intercept Protobuf Service Method calls (eMsg 151)
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

                    uint64_t cached = ManifestClient::GetCachedRequestCode(manifestId);
                    if (cached != 0) {
                        std::lock_guard<std::mutex> lock(g_ManifestMutex);
                        g_ManifestInstantCodes[jobid_source] = cached;
                    } else if (!ManifestClient::IsNegativeCached(manifestId)) {
                        auto task = std::async(std::launch::async, [manifestId]() -> uint64_t {
                            uint64_t code = 0;
                            ManifestClient::FetchManifestRequestCode(manifestId, &code);
                            return code;
                        });
                        std::lock_guard<std::mutex> lock(g_ManifestMutex);
                        g_ManifestFutures[jobid_source] = task.share();
                    }
                }
            } else if (target_job_name == "Player.GetUserStats#1") {
                std::vector<uint8_t> patched;
                AppId_t appId = 0;
                if (TryPatchGetUserStatsRequest(pBody, cbBody, patched, &appId)) {
                    RememberStatsJob(jobid_source, appId);
                    uint32_t newSize = 0;
                    if (uint8_t* poolBuf = BuildPooledFrame(eMsg | kMsgHdrProtoFlag, pHdr, cbHdr, patched, &newSize)) {
                        spdlog::info("Hooks_NetPacket: Spoofed GetUserStats request for AppID {} (donor SteamID)",
                                     appId);
                        return oBBuildAndAsyncSendFrame
                                   ? oBBuildAndAsyncSendFrame(pObject, eWebSocketOpCode, poolBuf, newSize)
                                   : false;
                    }
                }
            }
        } else if (eMsg == k_EMsgClientGetUserStats && cbBody > 0 && cubData <= kMaxPacketSize) {
            std::vector<uint8_t> patched;
            AppId_t appId = 0;
            if (TryPatchGetUserStatsLegacyRequest(pBody, cbBody, patched, &appId)) {
                uint32_t newSize = 0;
                if (uint8_t* poolBuf = BuildPooledFrame(eMsg | kMsgHdrProtoFlag, pHdr, cbHdr, patched, &newSize)) {
                    spdlog::info("Hooks_NetPacket: Patched CMsgClientGetUserStats for AppID {} "
                                 "(schema_local_version=-1, donor SteamID)",
                                 appId);
                    return oBBuildAndAsyncSendFrame
                               ? oBBuildAndAsyncSendFrame(pObject, eWebSocketOpCode, poolBuf, newSize)
                               : false;
                }
            }
        } else if (eMsg == k_EMsgClientPICSProductInfoRequest && cbBody > 0 && cubData <= kMaxPacketSize) {
            // Inject configured access tokens into PICS product info requests so
            // addtoken-protected depots resolve without "token required" failures.
            std::vector<uint8_t> patchedBody;
            if (PicsTokenInjector::PatchProductInfoRequest(pBody, cbBody, patchedBody)) {
                size_t totalSize = sizeof(MsgHdr) + cbHdr + patchedBody.size();
                if (uint8_t* poolBuf = AcquirePacketSlot(totalSize)) {
                    auto* outHdr = reinterpret_cast<MsgHdr*>(poolBuf);
                    outHdr->eMsg = eMsg | kMsgHdrProtoFlag;
                    outHdr->headerLength = cbHdr;
                    std::memcpy(poolBuf + sizeof(MsgHdr), pHdr, cbHdr);
                    std::memcpy(poolBuf + sizeof(MsgHdr) + cbHdr, patchedBody.data(), patchedBody.size());

                    spdlog::info("Hooks_NetPacket: Injected access tokens into PICS request ({} -> {} bytes)", cbBody,
                                 patchedBody.size());
                    return oBBuildAndAsyncSendFrame ? oBBuildAndAsyncSendFrame(pObject, eWebSocketOpCode, poolBuf,
                                                                               static_cast<uint32_t>(totalSize))
                                                    : false;
                }
            }
        }
    }
    return oBBuildAndAsyncSendFrame ? oBBuildAndAsyncSendFrame(pObject, eWebSocketOpCode, pubData, cubData) : false;
}

void HandleManifestResponse(uint64_t jobid_target, CNetPacket* pPacket, const uint8_t* pHdr, uint32_t cbHdr,
                            const uint8_t* pBody, uint32_t cbBody) {
    uint64_t instantCode = 0;
    std::shared_future<uint64_t> future;
    bool hasFuture = false;
    {
        std::lock_guard<std::mutex> lock(g_ManifestMutex);
        auto itInstant = g_ManifestInstantCodes.find(jobid_target);
        if (itInstant != g_ManifestInstantCodes.end()) {
            instantCode = itInstant->second;
            g_ManifestInstantCodes.erase(itInstant);
        }
        if (instantCode == 0) {
            auto it = g_ManifestFutures.find(jobid_target);
            if (it != g_ManifestFutures.end()) {
                future = it->second;
                g_ManifestFutures.erase(it);
                hasFuture = true;
            }
        }
    }

    uint64_t code = instantCode;
    if (code == 0 && hasFuture) {
        if (future.wait_for(kManifestRecvWait) == std::future_status::ready) {
            code = future.get();
        } else {
            spdlog::info("Hooks_NetPacket: Request-code lookup still pending for JobId {}; passing original response, "
                         "retry will use cache",
                         jobid_target);
            return;
        }
    }

    if (code == 0)
        return;

    if (TryInjectRequestCode(code, pHdr, cbHdr, pBody, cbBody, pPacket)) {
        spdlog::info("Hooks_NetPacket: Injecting manifest_request_code {} into response (JobId: {})", code,
                     jobid_target);
    }
}

HOOK_FUNC(RecvPkt, void*, void* pThis, CNetPacket* pPacket) {
    // 1. Drain synthesized Legacy CD-Key responses first
    if (pPacket) {
        std::vector<uint8_t> respPkt;
        {
            std::lock_guard<std::mutex> lock(g_LegacyKeyMutex);
            if (!g_LegacyKeyQueue.empty()) {
                respPkt = std::move(g_LegacyKeyQueue.front());
                g_LegacyKeyQueue.pop_front();
            }
        }
        if (!respPkt.empty()) {
            if (uint8_t* poolBuf = AcquirePacketSlot(respPkt.size())) {
                std::memcpy(poolBuf, respPkt.data(), respPkt.size());
                pPacket->m_pubData = poolBuf;
                pPacket->m_cubData = static_cast<uint32_t>(respPkt.size());
                spdlog::debug("Hooks_NetPacket: Delivered synthesized LegacyKey response ({} bytes)", respPkt.size());
                return pPacket;
            }
            {
                std::lock_guard<std::mutex> lock(g_LegacyKeyMutex);
                g_LegacyKeyQueue.push_front(std::move(respPkt));
            }
        }
    }

    // 2. Intercept native received packets
    if (pPacket && pPacket->m_pubData && pPacket->m_cubData >= sizeof(MsgHdr)) {
        uint32_t eMsg = 0, cbHdr = 0, cbBody = 0;
        const uint8_t *pHdr = nullptr, *pBody = nullptr;

        if (UnpackRaw(pPacket->m_pubData, pPacket->m_cubData, eMsg, pHdr, cbHdr, pBody, cbBody)) {
            if (eMsg == k_EMsgServiceMethodResponse) {
                uint64_t jobid_source = 0, jobid_target = 0;
                std::string target_job_name;
                ParseProtoHeader(pHdr, cbHdr, jobid_source, jobid_target, target_job_name);

                if (target_job_name == "Player.GetUserStats#1") {
                    HandleGetUserStatsResponse(jobid_target, pHdr, cbHdr, pBody, cbBody, pPacket);
                } else {
                    HandleManifestResponse(jobid_target, pPacket, pHdr, cbHdr, pBody, cbBody);
                }
            } else if (eMsg == k_EMsgClientGetUserStatsResponse) {
                HandleGetUserStatsLegacyResponse(pHdr, cbHdr, pBody, cbBody, pPacket);
            }
        }
    }
    return oRecvPkt ? oRecvPkt(pThis, pPacket) : nullptr;
}

} // namespace

namespace Hooks_NetPacket {

void Install() {
    uintptr_t fnSend = PatternLoader::GetFunctionAddress("BBuildAndAsyncSendFrame");
    if (fnSend) {
        ATTACH_HOOK(fnSend, BBuildAndAsyncSendFrame);
        spdlog::info("Hooks_NetPacket: Successfully installed BBuildAndAsyncSendFrame hook at {:p}",
                     reinterpret_cast<void*>(fnSend));
    } else {
        spdlog::warn("Hooks_NetPacket: BBuildAndAsyncSendFrame signature not resolved");
    }

    uintptr_t fnRecv = PatternLoader::GetFunctionAddress("RecvPkt");
    if (fnRecv) {
        ATTACH_HOOK(fnRecv, RecvPkt);
        spdlog::info("Hooks_NetPacket: Successfully installed RecvPkt hook at {:p}", reinterpret_cast<void*>(fnRecv));
    } else {
        spdlog::warn("Hooks_NetPacket: RecvPkt signature not resolved");
    }
}

void Uninstall() {}

} // namespace Hooks_NetPacket
