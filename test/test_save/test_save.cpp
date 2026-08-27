// test/test_save/test_save.cpp
#include <unity.h>
#include "save.h"
#include "economy.h"
#include <cstring>

void setUp(void) {}
void tearDown(void) {}

namespace {
// Deliberately duplicates save.cpp's private fnv1aChecksum (not exported - it's an
// implementation detail) so this test can hand-construct a pre-v2 (no brightness/volume)
// save buffer, matching exactly what a real device would have written before this schema
// change, to verify deserializeSave() migrates it forward instead of resetting progress.
// If save.cpp's checksum algorithm ever changes, this copy must change with it.
uint32_t fnv1aChecksumForTest(const uint8_t* data, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

// Byte-for-byte the pre-v2 SaveData layout (see save.cpp's private SaveDataV1).
struct LegacySaveDataV1 {
    uint32_t magic = SAVE_MAGIC;
    uint16_t version = 1;
    double qi = 0.0;
    uint32_t generatorCounts[NUM_GENERATORS] = {1, 0, 0, 0, 0, 0};
    uint8_t realmIndex = 0;
    int64_t lastSaveEpochSeconds = 0;
};
} // namespace

void test_round_trip_preserves_data() {
    SaveData original;
    original.qi = 12345.678;
    original.generatorCounts[0] = 3;
    original.generatorCounts[5] = 7;
    original.realmIndex = 4;
    original.lastSaveEpochSeconds = 1700000000;

    uint8_t buffer[SAVE_BUFFER_SIZE];
    size_t written = serializeSave(original, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(SAVE_BUFFER_SIZE, written);

    SaveData restored;
    bool ok = deserializeSave(buffer, sizeof(buffer), restored);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(original.qi, restored.qi);
    TEST_ASSERT_EQUAL(3, restored.generatorCounts[0]);
    TEST_ASSERT_EQUAL(7, restored.generatorCounts[5]);
    TEST_ASSERT_EQUAL(4, restored.realmIndex);
    TEST_ASSERT_EQUAL_INT64(1700000000, restored.lastSaveEpochSeconds);
}

void test_corrupted_byte_fails_checksum() {
    SaveData original;
    original.qi = 99.0;
    uint8_t buffer[SAVE_BUFFER_SIZE];
    serializeSave(original, buffer, sizeof(buffer));
    buffer[0] ^= 0xFF;

    SaveData restored;
    TEST_ASSERT_FALSE(deserializeSave(buffer, sizeof(buffer), restored));
}

void test_truncated_buffer_fails() {
    SaveData original;
    uint8_t buffer[SAVE_BUFFER_SIZE];
    serializeSave(original, buffer, sizeof(buffer));

    SaveData restored;
    TEST_ASSERT_FALSE(deserializeSave(buffer, SAVE_BUFFER_SIZE - 1, restored));
}

void test_default_save_data_is_fresh_game() {
    SaveData d = defaultSaveData();
    TEST_ASSERT_EQUAL_DOUBLE(0.0, d.qi);
    TEST_ASSERT_EQUAL(0, d.realmIndex);
}

void test_game_state_round_trip_via_save_data() {
    GameState state;
    state.qi = 500.0;
    state.generatorCounts[2] = 9;
    state.realmIndex = 2;

    SaveData saved = toSaveData(state, 42);
    GameState restored = toGameState(saved);

    TEST_ASSERT_EQUAL_DOUBLE(state.qi, restored.qi);
    TEST_ASSERT_EQUAL(state.generatorCounts[2], restored.generatorCounts[2]);
    TEST_ASSERT_EQUAL(state.realmIndex, restored.realmIndex);
}

// Regression test for the fresh-device dead-game bug: a genuinely clean NVS load
// (defaultSaveData(), never touched) must eventually make real progress once fed
// through the exact same tick/breakthrough/purchase loop main.cpp's loop() runs.
void test_fresh_state_from_default_save_makes_progress_under_automation() {
    GameState s = toGameState(defaultSaveData()); // exactly what a real fresh device starts with
    for (int i = 0; i < 20 * 60; ++i) { // simulate 60 seconds at the real 20Hz economy tick
        tick(s, 0.05);
        if (canBreakthrough(s)) attemptBreakthrough(s);
        for (int g = 0; g < NUM_GENERATORS; ++g) purchaseGenerator(s, g);
    }
    TEST_ASSERT_TRUE(s.qi > 0.0 || s.generatorCounts[0] > 1);
}

// Defensive-clamp regression: a checksum-valid save with an out-of-range realmIndex
// (corrupt-but-internally-consistent data) must be clamped to the last valid realm on
// load, not passed through to later index REALM_NAMES[]/REALM_QI_THRESHOLD[] out of bounds.
void test_deserialize_clamps_out_of_range_realm_index() {
    SaveData original;
    original.qi = 10.0;
    original.realmIndex = static_cast<uint8_t>(NUM_REALMS + 5); // deliberately out of range

    uint8_t buffer[SAVE_BUFFER_SIZE];
    size_t written = serializeSave(original, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(SAVE_BUFFER_SIZE, written);

    SaveData restored;
    bool ok = deserializeSave(buffer, sizeof(buffer), restored);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(NUM_REALMS - 1, restored.realmIndex);
}

void test_round_trip_preserves_brightness_and_volume() {
    SaveData original;
    original.qi = 10.0;
    original.brightness = 77;
    original.volume = 33;

    uint8_t buffer[SAVE_BUFFER_SIZE];
    serializeSave(original, buffer, sizeof(buffer));

    SaveData restored;
    TEST_ASSERT_TRUE(deserializeSave(buffer, sizeof(buffer), restored));
    TEST_ASSERT_EQUAL_UINT8(77, restored.brightness);
    TEST_ASSERT_EQUAL_UINT8(33, restored.volume);
}

void test_to_save_data_carries_brightness_and_volume() {
    GameState state;
    SaveData saved = toSaveData(state, 42, 90, 60);
    TEST_ASSERT_EQUAL_UINT8(90, saved.brightness);
    TEST_ASSERT_EQUAL_UINT8(60, saved.volume);
}

void test_to_save_data_defaults_brightness_and_volume_when_unspecified() {
    GameState state;
    SaveData saved = toSaveData(state, 42); // brightness/volume omitted
    TEST_ASSERT_EQUAL_UINT8(200, saved.brightness);
    TEST_ASSERT_EQUAL_UINT8(128, saved.volume);
}

// Regression coverage for the schema v1 -> v2 migration: a save written before
// brightness/volume existed must load successfully with its qi/generators/realm/epoch
// intact and brightness/volume filled in with fresh-game defaults, not silently reset to
// defaultSaveData() just because it doesn't checksum-validate against the current version.
void test_deserialize_migrates_legacy_v1_save() {
    LegacySaveDataV1 legacy;
    legacy.qi = 555.5;
    legacy.generatorCounts[1] = 4;
    legacy.realmIndex = 3;
    legacy.lastSaveEpochSeconds = 1234567;

    uint8_t buffer[sizeof(LegacySaveDataV1) + sizeof(uint32_t)];
    std::memcpy(buffer, &legacy, sizeof(LegacySaveDataV1));
    uint32_t checksum = fnv1aChecksumForTest(buffer, sizeof(LegacySaveDataV1));
    std::memcpy(buffer + sizeof(LegacySaveDataV1), &checksum, sizeof(uint32_t));

    SaveData migrated;
    bool ok = deserializeSave(buffer, sizeof(buffer), migrated);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(555.5, migrated.qi);
    TEST_ASSERT_EQUAL(4, migrated.generatorCounts[1]);
    TEST_ASSERT_EQUAL(3, migrated.realmIndex);
    TEST_ASSERT_EQUAL_INT64(1234567, migrated.lastSaveEpochSeconds);
    TEST_ASSERT_EQUAL_UINT8(200, migrated.brightness); // fresh-game default, v1 never had this
    TEST_ASSERT_EQUAL_UINT8(128, migrated.volume);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_preserves_data);
    RUN_TEST(test_corrupted_byte_fails_checksum);
    RUN_TEST(test_truncated_buffer_fails);
    RUN_TEST(test_default_save_data_is_fresh_game);
    RUN_TEST(test_game_state_round_trip_via_save_data);
    RUN_TEST(test_fresh_state_from_default_save_makes_progress_under_automation);
    RUN_TEST(test_deserialize_clamps_out_of_range_realm_index);
    RUN_TEST(test_round_trip_preserves_brightness_and_volume);
    RUN_TEST(test_to_save_data_carries_brightness_and_volume);
    RUN_TEST(test_to_save_data_defaults_brightness_and_volume_when_unspecified);
    RUN_TEST(test_deserialize_migrates_legacy_v1_save);
    return UNITY_END();
}
