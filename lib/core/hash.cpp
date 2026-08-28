#include "hash.h"
#include <cstdint>

float hashUnitFloat(int a, int b) {
    uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0x1000000);
}

float hashRange(int a, int b, float lo, float hi) {
    return lo + hashUnitFloat(a, b) * (hi - lo);
}
