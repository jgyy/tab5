#pragma once
#include <cstddef>
#include <cstdint>
#include "economy.h"

constexpr uint32_t SAVE_MAGIC = 0x51494732; // 'QIG2'; bump on breaking format changes
constexpr uint16_t SAVE_VERSION = 2; // v2 added brightness/volume; see deserializeSave's v1 migration

// NOTE: this struct contains compiler-inserted padding between fields (e.g. around the
// uint8_t/int64_t tail), and the checksum in serializeSave()/deserializeSave() covers
// that padding too. That's safe only because those functions always round-trip through
// a literal byte buffer — never compare two independently-constructed SaveData instances
// for byte-equality. Don't add such a comparison without accounting for padding bytes.
struct SaveData {
    uint32_t magic = SAVE_MAGIC;
    uint16_t version = SAVE_VERSION;
    double qi = 0.0;
    // A fresh save starts owning 1 unit of generator 0 (Breathing Technique) — see the
    // matching comment on GameState in economy.h; this is what a real fresh device
    // actually boots from (defaultSaveData() -> toGameState()), so it must agree.
    uint32_t generatorCounts[NUM_GENERATORS] = {1, 0, 0, 0, 0, 0};
    uint8_t realmIndex = 0;
    int64_t lastSaveEpochSeconds = 0;
    // Device settings, not game economy - kept out of GameState on purpose (see economy.h).
    // Scale matches M5Unified's setBrightness()/Speaker.setVolume() (both take uint8_t 0-255).
    uint8_t brightness = 200;
    uint8_t volume = 128;
};

constexpr size_t SAVE_BUFFER_SIZE = sizeof(SaveData) + sizeof(uint32_t); // payload + checksum

SaveData defaultSaveData();

// brightness/volume default to fresh-game values for callers (like tests) that don't care
// about device settings; main.cpp always passes the device's actual current values.
SaveData toSaveData(const GameState& state, int64_t epochSeconds, uint8_t brightness = 200,
                     uint8_t volume = 128);
GameState toGameState(const SaveData& data);

// Serializes `data` plus a trailing FNV-1a checksum into `outBuffer` (must be at least
// SAVE_BUFFER_SIZE bytes). Returns bytes written, or 0 if the buffer is too small.
size_t serializeSave(const SaveData& data, uint8_t* outBuffer, size_t bufferLen);

// Validates length, checksum, magic, and version. On success fills `outData` and returns
// true; on any failure leaves `outData` untouched and returns false.
bool deserializeSave(const uint8_t* buffer, size_t bufferLen, SaveData& outData);
