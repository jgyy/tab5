// test/test_smoke/test_smoke.cpp
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_smoke() {
    TEST_ASSERT_EQUAL(2, 1 + 1);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_smoke);
    return UNITY_END();
}
