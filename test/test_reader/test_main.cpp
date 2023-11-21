
#include <unity.h>
#include <stdint.h>
#include <Reader.h>

SecPlus2DoorStatus door_status;

PacketDecoder p;
SecPlus2Reader r;

// print_packet stub
void print_packet(uint8_t* pkt) {}

// handler to store the argument passed to the callback used by p
void cb(SecPlus2DoorStatus s) {
    door_status = s;
}

void setUp(void) {
    door_status = SecPlus2DoorStatus::Unknown;
    p.set_door_status_cb(cb);
    r.set_packet_decoder(&p);
}

void tearDown(void) {
    door_status = SecPlus2DoorStatus::Unknown;
}

void test_reader_opening(void) {
    uint8_t test_data[SECPLUS2_CODE_LEN] = {
        0x55, 0x01, 0x00,
        0x99, 0x02, 0x11, 0x40, 0x8E, 0x8D, 0x48, 0x0C, 0x65, 0x29, 0x85, 0xC7, 0x7D, 0xC0, 0xCA, 0x2B };

    for (uint8_t i = 0; i < SECPLUS2_CODE_LEN; i++) {
        r.push_byte(test_data[i]);
    }

    TEST_ASSERT_EQUAL(
            SecPlus2DoorStatus::Opening,
            door_status);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_reader_opening);
    UNITY_END();

    return 0;
}
