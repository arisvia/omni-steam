#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "OmniPlatform/SteamTypes.h"

#include "Utils/Metadata/ProtoFields.h"

// Local synthesis of GetUserStats responses.
//
// When no donor SteamID is known, asking Valve for the stats of an app the
// account does not own makes the client surface an error. Answering the
// request ourselves keeps the achievement UI quiet instead.
//
// Wire layout: [MsgHdr{uint32 eMsg; uint32 headerLength}][header][body]
//   CMsgClientGetUserStatsResponse (legacy, eMsg 819)
//     int32 eresult = 2; uint32 crc_stats = 3;   (loose, no inner header)
//   CMsgClientServiceMethodResponse (eMsg 147) for Player.GetUserStats#1
//     header: fixed64 jobid_target = 11; string target_job_name = 12;
//             int32 eresult = 13;
namespace StatsResponse {

inline constexpr uint32_t kHdrFieldJobIdTarget = 11;
inline constexpr uint32_t kHdrFieldJobName = 12;
inline constexpr uint32_t kHdrFieldEResult = 13;

inline constexpr uint32_t kLegacyFieldGameId = 1;
inline constexpr uint32_t kLegacyFieldEResult = 2;
inline constexpr uint32_t kLegacyFieldCrcStats = 3;

inline std::vector<uint8_t> AssemblePacket(uint32_t eMsg, const std::vector<uint8_t>& header,
                                           const std::vector<uint8_t>& body) {
    std::vector<uint8_t> pkt(sizeof(MsgHdr) + header.size() + body.size());
    const uint32_t fullMsg = eMsg | kMsgHdrProtoFlag;
    const uint32_t headerLen = static_cast<uint32_t>(header.size());
    std::memcpy(pkt.data(), &fullMsg, sizeof(fullMsg));
    std::memcpy(pkt.data() + sizeof(fullMsg), &headerLen, sizeof(headerLen));
    if (!header.empty())
        std::memcpy(pkt.data() + sizeof(MsgHdr), header.data(), header.size());
    if (!body.empty())
        std::memcpy(pkt.data() + sizeof(MsgHdr) + header.size(), body.data(), body.size());
    return pkt;
}

// Empty k_EResultOK answer to a Player.GetUserStats#1 service call.
inline std::vector<uint8_t> BuildServiceResponse(uint64_t jobId, const std::string& jobName) {
    std::vector<uint8_t> header;
    ProtoFields::AppendFixed64Field(header, kHdrFieldJobIdTarget, jobId);
    ProtoFields::AppendBytesField(header, kHdrFieldJobName, reinterpret_cast<const uint8_t*>(jobName.data()),
                                  jobName.size());
    ProtoFields::AppendVarintField(header, kHdrFieldEResult, static_cast<uint64_t>(k_EResultOK));
    return AssemblePacket(k_EMsgServiceMethodResponse, header, {});
}

// Empty k_EResultOK answer to a legacy CMsgClientGetUserStats (eMsg 818).
// The caller's proto header is preserved verbatim; only the body is rebuilt
// with the request's game_id plus a zero crc_stats.
inline std::vector<uint8_t> BuildLegacyResponse(const uint8_t* pHdr, uint32_t cbHdr, uint64_t gameId) {
    std::vector<uint8_t> header(pHdr, pHdr + cbHdr);
    std::vector<uint8_t> body;
    ProtoFields::AppendFixed64Field(body, kLegacyFieldGameId, gameId);
    ProtoFields::AppendVarintField(body, kLegacyFieldEResult, static_cast<uint64_t>(k_EResultOK));
    ProtoFields::AppendVarintField(body, kLegacyFieldCrcStats, 0);
    return AssemblePacket(k_EMsgClientGetUserStatsResponse, header, body);
}

} // namespace StatsResponse
