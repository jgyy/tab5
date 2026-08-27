// test/test_save/test_save.cpp
#include <unity.h>
#include "save.h"

void setUp(void) {}
void tearDown(void) {}

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

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_preserves_data);
    RUN_TEST(test_corrupted_byte_fails_checksum);
    RUN_TEST(test_truncated_buffer_fails);
    RUN_TEST(test_default_save_data_is_fresh_game);
    RUN_TEST(test_game_state_round_trip_via_save_data);
    return UNITY_END();
}
