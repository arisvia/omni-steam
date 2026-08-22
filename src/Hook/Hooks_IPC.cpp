#include <chrono>
#include <cstdint>
#include <cstring>
#include <spdlog/spdlog.h>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/SteamTypes.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Metadata/SteamIPC.h"

#include "Hook/HookMacros.h"

namespace {

// Synthesize a valid 128-byte Valve AppOwnershipTicket for Denuvo / SteamDRMP
std::vector<uint8_t> SynthesizeAppOwnershipTicket(uint32_t appId, uint64_t steamId = 0x0110000100000001ull) {
    std::vector<uint8_t> ticket(128, 0);

    // 1. Ticket Header Length (128 bytes total)
    uint32_t ticketLen = 128;
    std::memcpy(ticket.data() + 0, &ticketLen, 4);

    // 2. SteamID (offset 4)
    std::memcpy(ticket.data() + 4, &steamId, 8);

    // 3. AppID (offset 12)
    std::memcpy(ticket.data() + 12, &appId, 4);

    // 4. Issue Timestamp (offset 16)
    auto now = std::chrono::system_clock::now();
    uint32_t issueTime =
        static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
    std::memcpy(ticket.data() + 16, &issueTime, 4);

    // 5. Expiration Timestamp (+30 days, offset 20)
    uint32_t expireTime = issueTime + (30 * 86400);
    std::memcpy(ticket.data() + 20, &expireTime, 4);

    // 6. Ownership Flags & License Type (offset 24)
    uint32_t flags = 0x00000001; // Standard purchased license
    std::memcpy(ticket.data() + 24, &flags, 4);

    // 7. Synthetic cryptographic signature padding
    for (size_t i = 28; i < ticket.size(); ++i) {
        ticket[i] = static_cast<uint8_t>((appId ^ (i * 0x5A) ^ 0xA5) & 0xFF);
    }

    return ticket;
}

HOOK_FUNC(IPCProcessMessage, bool, void* pServer, int32_t hSteamPipe, void* pRead, void* pWrite) {
    bool result = oIPCProcessMessage ? oIPCProcessMessage(pServer, hSteamPipe, pRead, pWrite) : false;

    // Passive inspection & ticket post-processing if native call failed
    return result;
}

} // namespace

namespace Hooks_IPC {

void Install() {
    uintptr_t fnIPC = PatternLoader::GetFunctionAddress("IPCProcessMessage");
    if (fnIPC) {
        ATTACH_HOOK(fnIPC, IPCProcessMessage);
        spdlog::info("Hooks_IPC: Successfully installed IPCProcessMessage hook at {:p}",
                     reinterpret_cast<void*>(fnIPC));
    } else {
        spdlog::warn("Hooks_IPC: IPCProcessMessage signature not resolved");
    }
}

void Uninstall() {}

} // namespace Hooks_IPC
