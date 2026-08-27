#pragma once
#include <cstddef>
#include <cstdint>
#include "economy.h"

constexpr uint32_t SAVE_MAGIC = 0x51494732; // 'QIG2'; bump on breaking format changes
constexpr uint16_t SAVE_VERSION = 1;

struct SaveData {
    uint32_t magic = SAVE_MAGIC;
    uint16_t version = SAVE_VERSION;
    double qi = 0.0;
    uint32_t generatorCounts[NUM_GENERATORS] = {};
    uint8_t realmIndex = 0;
    int64_t lastSaveEpochSeconds = 0;
};

constexpr size_t SAVE_BUFFER_SIZE = sizeof(SaveData) + sizeof(uint32_t); // payload + checksum

SaveData defaultSaveData();
SaveData toSaveData(const GameState& state, int64_t epochSeconds);
GameState toGameState(const SaveData& data);

// Serializes `data` plus a trailing FNV-1a checksum into `outBuffer` (must be at least
// SAVE_BUFFER_SIZE bytes). Returns bytes written, or 0 if the buffer is too small.
size_t serializeSave(const SaveData& data, uint8_t* outBuffer, size_t bufferLen);

// Validates length, checksum, magic, and version. On success fills `outData` and returns
// true; on any failure leaves `outData` untouched and returns false.
bool deserializeSave(const uint8_t* buffer, size_t bufferLen, SaveData& outData);
