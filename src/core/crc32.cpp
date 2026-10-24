#include "crc32.h"

#include <array>
#include <cstring>

namespace hx {
namespace {
struct Tables {
    uint32_t t[8][256];
    Tables() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[0][i] = c;
        }
        for (uint32_t i = 0; i < 256; ++i)
            for (int s = 1; s < 8; ++s) t[s][i] = (t[s - 1][i] >> 8) ^ t[0][t[s - 1][i] & 0xFF];
    }
};
const Tables& tables() {
    static const Tables tb;
    return tb;
}
}  // namespace

uint32_t crc32_update(uint32_t crc, const void* data, size_t len) {
    const auto& T = tables().t;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    crc = ~crc;
    while (len >= 8) {
        uint32_t a, b;
        std::memcpy(&a, p, 4);
        std::memcpy(&b, p + 4, 4);  // little-endian host assumed (x64)
        a ^= crc;
        crc = T[7][a & 0xFF] ^ T[6][(a >> 8) & 0xFF] ^ T[5][(a >> 16) & 0xFF] ^ T[4][a >> 24] ^
              T[3][b & 0xFF] ^ T[2][(b >> 8) & 0xFF] ^ T[1][(b >> 16) & 0xFF] ^ T[0][b >> 24];
        p += 8;
        len -= 8;
    }
    while (len--) crc = (crc >> 8) ^ T[0][(crc ^ *p++) & 0xFF];
    return ~crc;
}
}  // namespace hx
