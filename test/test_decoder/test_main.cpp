
#include <unity.h>
#include <stdint.h>
#include <Decoder.h>

SecPlus2DoorStatus door_status;

PacketDecoder p;

// handler to store the argument passed to the callback used by p
void cb(SecPlus2DoorStatus s) {
    door_status = s;
}

void setUp(void) {
    door_status = SecPlus2DoorStatus::Unknown;
    p.set_door_status_cb(cb);
}

void tearDown(void) {
    door_status = SecPlus2DoorStatus::Unknown;
}

void test_decoder_opening(void) {
    uint8_t test_data[SECPLUS2_CODE_LEN] = {
        0x55, 0x01, 0x00,
        0x99, 0x02, 0x11, 0x40, 0x8E, 0x8D, 0x48, 0x0C, 0x65, 0x29, 0x85, 0xC7, 0x7D, 0xC0, 0xCA, 0x2B };

    p.handle_code(test_data);

    TEST_ASSERT_EQUAL(
            SecPlus2DoorStatus::Opening,
            door_status);
}


void test_decoder_closing(void) {
    uint8_t test_data[SECPLUS2_CODE_LEN] = {
        0x55, 0x01, 0x00,
        0x4A, 0x2B, 0xB4, 0xFA, 0xE1, 0xA8, 0xDF, 0x75, 0x91, 0x12, 0x78, 0x38, 0x86, 0xAD, 0x64, 0xD5 };

    p.handle_code(test_data);

    TEST_ASSERT_EQUAL(
            SecPlus2DoorStatus::Closing,
            door_status);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_decoder_opening);
    RUN_TEST(test_decoder_closing);
    UNITY_END();

    return 0;
}
