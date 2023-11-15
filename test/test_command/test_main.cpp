
#include <unity.h>
#include <stdint.h>
#include <Update.h>

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

void test_command_decode(void) {
    TEST_ASSERT_EQUAL(SecPlus2Update::from_byte(0x81), SecPlus2Update::StatusMsg);
}

void test_command_decode_unknown(void) {
    TEST_ASSERT_EQUAL(SecPlus2Update::from_byte(0x82), SecPlus2Update::Unknown);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_command_decode);
    RUN_TEST(test_command_decode_unknown);
    UNITY_END();

    return 0;
}
