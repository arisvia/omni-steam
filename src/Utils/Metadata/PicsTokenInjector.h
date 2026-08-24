#pragma once
#include <cstdint>
#include <vector>

// Injects configured access tokens into outgoing
// CMsgClientPICSProductInfoRequest (eMsg 8903) bodies.
//
// Protobuf layout (SteamKit):
//   CMsgClientPICSProductInfoRequest { repeated App apps = 1; }
//   App { optional uint32 appid = 1; optional uint64 access_token = 2; }
//
// Patching strategy: scalar fields parsed later win, so an access_token
// varint appended to the end of an App entry overrides any earlier value.
namespace PicsTokenInjector {

// Rewrites every App entry whose appid has a configured non-zero token,
// preserving all other fields byte-for-byte. Returns false when the body
// is malformed or when no entry required modification (out left untouched).
bool PatchProductInfoRequest(const uint8_t* body, uint32_t cbBody, std::vector<uint8_t>& out);

} // namespace PicsTokenInjector
