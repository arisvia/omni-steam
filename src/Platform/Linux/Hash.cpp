#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

// ponytail: SHA-256 only, implemented in-house (FIPS 180-4). The injected core
// must not carry a libcrypto dependency: the bullseye toolchain links OpenSSL
// 1.1 while the CI host ships 3.x, and an unresolved versioned symbol under
// LD_PRELOAD killed even /usr/bin/env (run 44251540185). Static OpenSSL is not
// the escape hatch either - it drags non-PIC libz.a into the shared object
// (R_X86_64_PC32 against z_errmsg). macOS answers the same problem with
// CommonCrypto; Linux has no system digest, so the 100 lines below are it.
// If more digests/HMAC are ever required, extend this file rather than
// reintroducing a crypto dependency into the hook.
namespace OmniPlatform {
namespace {

constexpr uint32_t kShaK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
    0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
    0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
};

constexpr uint32_t kShaH0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

inline uint32_t Rotr(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
}

struct Sha256Ctx {
    uint32_t h[8];
    uint64_t bits = 0;
    uint8_t block[64] = {0};
    size_t filled = 0;

    Sha256Ctx() {
        for (int i = 0; i < 8; ++i) {
            h[i] = kShaH0[i];
        }
    }

    void Compress(const uint8_t* p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(p[4 * i]) << 24) | (static_cast<uint32_t>(p[4 * i + 1]) << 16) |
                   (static_cast<uint32_t>(p[4 * i + 2]) << 8) | static_cast<uint32_t>(p[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t t1 = hh + S1 + ch + kShaK[i] + w[i];
            const uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    // Feed bytes and count them: the public input path.
    void Update(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        bits += static_cast<uint64_t>(n) * 8;
        Absorb(p, n);
    }

    // Padding must not disturb the message length counter.
    void Absorb(const uint8_t* p, size_t n) {
        while (n > 0) {
            const size_t take = (std::min)(n, sizeof(block) - filled);
            std::memcpy(block + filled, p, take);
            filled += take;
            p += take;
            n -= take;
            if (filled == sizeof(block)) {
                Compress(block);
                filled = 0;
            }
        }
    }

    void Final(uint8_t out[32]) {
        const uint64_t totalBits = bits;
        uint8_t pad[128] = {0x80};
        const size_t padLen = (filled < 56) ? (56 - filled) : (120 - filled);
        for (size_t i = 0; i < 8; ++i) {
            pad[padLen + i] = static_cast<uint8_t>((totalBits >> (56 - 8 * i)) & 0xff);
        }
        Absorb(pad, padLen + 8);
        for (int i = 0; i < 8; ++i) {
            out[4 * i] = static_cast<uint8_t>(h[i] >> 24);
            out[4 * i + 1] = static_cast<uint8_t>(h[i] >> 16);
            out[4 * i + 2] = static_cast<uint8_t>(h[i] >> 8);
            out[4 * i + 3] = static_cast<uint8_t>(h[i]);
        }
    }
};

std::string ToHex(const uint8_t (&hash)[32]) {
    std::ostringstream oss;
    for (uint8_t byte : hash) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

} // namespace

std::string Hash::Sha256(const std::vector<uint8_t>& data) {
    Sha256Ctx ctx;
    if (!data.empty()) {
        ctx.Update(data.data(), data.size());
    }
    uint8_t out[32];
    ctx.Final(out);
    return ToHex(out);
}

std::string Hash::Sha256File(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return "";
    }
    Sha256Ctx ctx;
    char buf[8192];
    while (file) {
        file.read(buf, sizeof(buf));
        const std::streamsize got = file.gcount();
        if (got > 0) {
            ctx.Update(buf, static_cast<size_t>(got));
        }
    }
    uint8_t out[32];
    ctx.Final(out);
    return ToHex(out);
}

} // namespace OmniPlatform
