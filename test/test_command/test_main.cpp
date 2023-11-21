
#include <unity.h>
#include <stdint.h>
#include <Command.h>

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

void test_command(void) {
    SecPlus2Command cmd = SecPlus2Command::Sync;
    uint64_t id = 0;
    uint32_t rolling = 0;
    uint8_t count = 0;
    cmd.prepare(id, &rolling, [&](uint8_t pkt[SECPLUS2_CODE_LEN]) {
            TEST_ASSERT_EQUAL(count, rolling);
            TEST_ASSERT_EQUAL(pkt[0], 0x55);
            count += 1;
    });
}

void test_door_noincr(void) {
    SecPlus2Command cmd = SecPlus2Command::Door;
    uint64_t id = 0;
    uint32_t rolling = 0;
    cmd.prepare(id, &rolling, [&](uint8_t pkt[SECPLUS2_CODE_LEN]) {
            TEST_ASSERT_EQUAL(pkt[0], 0x55);
    });
    TEST_ASSERT_EQUAL(1, rolling);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_command);
    RUN_TEST(test_door_noincr);
    UNITY_END();

    return 0;
}
