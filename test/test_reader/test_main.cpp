
#include <unity.h>
#include <stdint.h>
#include <Reader.h>

SecPlus2Reader r;

// print_packet stub
void print_packet(uint8_t* pkt) {}

void setUp(void) {
}

void tearDown(void) {
}

void test_reader_result(void) {
    uint8_t test_data[SECPLUS2_CODE_LEN] = {
        0x55, 0x01, 0x00,
        0x99, 0x02, 0x11, 0x40, 0x8E, 0x8D, 0x48, 0x0C, 0x65, 0x29, 0x85, 0xC7, 0x7D, 0xC0, 0xCA, 0x2B };

    bool res = false;
    uint8_t i = 0;
    while (i < SECPLUS2_CODE_LEN) {
        res = r.push_byte(test_data[i]);
        i += 1;
        if (res) {
            break;
        }
    }

    TEST_ASSERT_EQUAL(SECPLUS2_CODE_LEN, i);
    TEST_ASSERT_EQUAL_MEMORY(test_data, r.fetch_buf(), SECPLUS2_CODE_LEN);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_reader_result);
    UNITY_END();

    return 0;
}
