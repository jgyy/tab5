#include "save.h"
#include <cstring>

SaveData defaultSaveData() {
    return SaveData{};
}

SaveData toSaveData(const GameState& state, int64_t epochSeconds, uint8_t brightness,
                     uint8_t volume) {
    SaveData d;
    d.qi = state.qi;
    for (int i = 0; i < NUM_GENERATORS; ++i) {
        d.generatorCounts[i] = static_cast<uint32_t>(state.generatorCounts[i]);
    }
    d.realmIndex = static_cast<uint8_t>(state.realmIndex);
    d.lastSaveEpochSeconds = epochSeconds;
    d.brightness = brightness;
    d.volume = volume;
    return d;
}

GameState toGameState(const SaveData& data) {
    GameState s;
    s.qi = data.qi;
    for (int i = 0; i < NUM_GENERATORS; ++i) {
        s.generatorCounts[i] = static_cast<int>(data.generatorCounts[i]);
    }
    s.realmIndex = data.realmIndex;
    return s;
}

namespace {
uint32_t fnv1aChecksum(const uint8_t* data, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

// Byte-for-byte the original (pre-brightness/volume) SaveData layout, kept only so
// deserializeSave() can still read a save written before schema v2 existed and migrate it
// forward instead of failing validation and silently resetting all progress (qi, generators,
// realm) back to a fresh game. Never write this format - only ever read it, once, for
// migration.
struct SaveDataV1 {
    uint32_t magic = SAVE_MAGIC;
    uint16_t version = 1;
    double qi = 0.0;
    uint32_t generatorCounts[NUM_GENERATORS] = {1, 0, 0, 0, 0, 0};
    uint8_t realmIndex = 0;
    int64_t lastSaveEpochSeconds = 0;
};
constexpr size_t SAVE_V1_BUFFER_SIZE = sizeof(SaveDataV1) + sizeof(uint32_t);
}

size_t serializeSave(const SaveData& data, uint8_t* outBuffer, size_t bufferLen) {
    if (bufferLen < SAVE_BUFFER_SIZE) return 0;
    std::memcpy(outBuffer, &data, sizeof(SaveData));
    uint32_t checksum = fnv1aChecksum(outBuffer, sizeof(SaveData));
    std::memcpy(outBuffer + sizeof(SaveData), &checksum, sizeof(uint32_t));
    return SAVE_BUFFER_SIZE;
}

bool deserializeSave(const uint8_t* buffer, size_t bufferLen, SaveData& outData) {
    if (bufferLen >= SAVE_BUFFER_SIZE) {
        SaveData candidate;
        std::memcpy(&candidate, buffer, sizeof(SaveData));

        uint32_t storedChecksum;
        std::memcpy(&storedChecksum, buffer + sizeof(SaveData), sizeof(uint32_t));

        if (fnv1aChecksum(buffer, sizeof(SaveData)) == storedChecksum &&
            candidate.magic == SAVE_MAGIC && candidate.version == SAVE_VERSION) {
            // Defensive: a checksum-valid but out-of-range realmIndex (corrupt-but-
            // consistent data, or a future format mistake) would otherwise later index
            // REALM_NAMES[]/REALM_QI_THRESHOLD[] out of bounds. Clamp rather than reject
            // the whole save — mirrors the precedent set by growForRealm()'s clamping in
            // mesh.cpp.
            if (candidate.realmIndex >= NUM_REALMS) candidate.realmIndex = NUM_REALMS - 1;
            outData = candidate;
            return true;
        }
    }

    // Fall back to the pre-v2 (no brightness/volume) layout: a save written before this
    // schema change would otherwise fail every check above and silently reset all progress
    // (qi, generators, realm) back to a fresh game on next boot - migrate it forward instead.
    if (bufferLen >= SAVE_V1_BUFFER_SIZE) {
        SaveDataV1 legacy;
        std::memcpy(&legacy, buffer, sizeof(SaveDataV1));

        uint32_t storedChecksum;
        std::memcpy(&storedChecksum, buffer + sizeof(SaveDataV1), sizeof(uint32_t));

        if (fnv1aChecksum(buffer, sizeof(SaveDataV1)) == storedChecksum &&
            legacy.magic == SAVE_MAGIC && legacy.version == 1) {
            SaveData migrated; // brightness/volume take SaveData's fresh-game defaults
            migrated.qi = legacy.qi;
            for (int i = 0; i < NUM_GENERATORS; ++i) {
                migrated.generatorCounts[i] = legacy.generatorCounts[i];
            }
            migrated.realmIndex = legacy.realmIndex;
            migrated.lastSaveEpochSeconds = legacy.lastSaveEpochSeconds;
            if (migrated.realmIndex >= NUM_REALMS) migrated.realmIndex = NUM_REALMS - 1;
            outData = migrated;
            return true;
        }
    }

    return false;
}
