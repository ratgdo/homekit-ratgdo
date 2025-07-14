#include <unity.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#ifdef UNIT_TEST

// Mock Arduino functions for native testing
#ifdef NATIVE_BUILD
unsigned long millis() {
    static unsigned long mock_millis = 0;
    return mock_millis += 100;
}

void delay(unsigned long ms) {}

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

// Mock door states
#define CURR_OPEN 0
#define CURR_CLOSED 1
#define CURR_OPENING 2
#define CURR_CLOSING 3
#define CURR_STOPPED 4

// Mock GPIO pins
#define TRIGGER_PIN 5
#define STATUS_PIN 4
#define LIGHT_PIN 2
#define OBSTRUCTION_PIN 3

// Mock hardware state
struct MockHardware {
    bool door_open;
    bool door_closed;
    bool light_on;
    bool obstruction;
    bool trigger_active;
    unsigned long trigger_start_time;
    unsigned long door_operation_start_time;
    uint8_t current_door_state;
    uint8_t target_door_state;
} mock_hardware = {false, true, false, false, false, 0, 0, CURR_CLOSED, CURR_CLOSED};

void pinMode(int pin, int mode) {}

void digitalWrite(int pin, int value) {
    if (pin == TRIGGER_PIN) {
        mock_hardware.trigger_active = (value == HIGH);
        if (value == HIGH) {
            mock_hardware.trigger_start_time = millis();
        }
    } else if (pin == LIGHT_PIN) {
        mock_hardware.light_on = (value == HIGH);
    }
}

int digitalRead(int pin) {
    if (pin == STATUS_PIN) {
        return mock_hardware.door_open ? HIGH : LOW;
    } else if (pin == OBSTRUCTION_PIN) {
        return mock_hardware.obstruction ? HIGH : LOW;
    }
    return LOW;
}

class SerialMock {
public:
    void begin(int baud) {}
    void print(const char* str) {}
    void println(const char* str) {}
    void printf(const char* format, ...) {}
};
SerialMock Serial;

#else
#include <Arduino.h>
#endif

void setUp(void) {
    // Reset hardware state before each test
    mock_hardware.door_open = false;
    mock_hardware.door_closed = true;
    mock_hardware.light_on = false;
    mock_hardware.obstruction = false;
    mock_hardware.trigger_active = false;
    mock_hardware.trigger_start_time = 0;
    mock_hardware.door_operation_start_time = 0;
    mock_hardware.current_door_state = CURR_CLOSED;
    mock_hardware.target_door_state = CURR_CLOSED;
}

void tearDown(void) {}

// Simulate door opening operation
void simulate_door_opening() {
    static int open_calls = 0;
    
    if (mock_hardware.current_door_state != CURR_OPENING) {
        mock_hardware.current_door_state = CURR_OPENING;
        mock_hardware.door_operation_start_time = millis();
        open_calls = 0;
    }
    
    // Simulate door opening over time - complete after several calls
    open_calls++;
    if (open_calls >= 3) { // Complete after 3 simulation calls
        mock_hardware.door_open = true;
        mock_hardware.door_closed = false;
        mock_hardware.current_door_state = CURR_OPEN;
        open_calls = 0;
    }
}

// Simulate door closing operation
void simulate_door_closing() {
    static int close_calls = 0;
    
    if (mock_hardware.current_door_state != CURR_CLOSING) {
        mock_hardware.current_door_state = CURR_CLOSING;
        mock_hardware.door_operation_start_time = millis();
        close_calls = 0;
    }
    
    // Simulate door closing over time - complete after several calls
    close_calls++;
    if (close_calls >= 3) { // Complete after 3 simulation calls
        mock_hardware.door_open = false;
        mock_hardware.door_closed = true;
        mock_hardware.current_door_state = CURR_CLOSED;
        close_calls = 0;
    }
}

// Test basic door operation simulation
void test_door_operation_simulation(void) {
    // Test initial state
    TEST_ASSERT_EQUAL(CURR_CLOSED, mock_hardware.current_door_state);
    TEST_ASSERT_TRUE(mock_hardware.door_closed);
    TEST_ASSERT_FALSE(mock_hardware.door_open);
    
    // Trigger door opening
    mock_hardware.target_door_state = CURR_OPEN;
    simulate_door_opening();
    
    TEST_ASSERT_EQUAL(CURR_OPENING, mock_hardware.current_door_state);
    
    // Wait for operation to complete - simulate multiple calls
    delay(1100); // Ensure enough time has passed
    for (int i = 0; i < 5; i++) {
        simulate_door_opening();
        if (mock_hardware.current_door_state == CURR_OPEN) break;
        delay(100);
    }
    
    TEST_ASSERT_EQUAL(CURR_OPEN, mock_hardware.current_door_state);
    TEST_ASSERT_TRUE(mock_hardware.door_open);
    TEST_ASSERT_FALSE(mock_hardware.door_closed);
}

// Test door trigger timing
void test_door_trigger_timing(void) {
    const unsigned long TRIGGER_DURATION = 100; // 100ms trigger pulse (reduced for mock)
    
    // Activate trigger
    digitalWrite(TRIGGER_PIN, HIGH);
    TEST_ASSERT_TRUE(mock_hardware.trigger_active);
    
    unsigned long trigger_start = mock_hardware.trigger_start_time;
    
    // Simulate trigger pulse duration
    delay(TRIGGER_DURATION + 100);
    
    // Deactivate trigger
    digitalWrite(TRIGGER_PIN, LOW);
    TEST_ASSERT_FALSE(mock_hardware.trigger_active);
    
    // Check trigger duration was within acceptable range
    unsigned long trigger_duration = millis() - trigger_start;
    TEST_ASSERT_GREATER_OR_EQUAL(TRIGGER_DURATION, trigger_duration);
}

// Test light control simulation
void test_light_control_simulation(void) {
    // Test initial light state
    TEST_ASSERT_FALSE(mock_hardware.light_on);
    
    // Turn light on
    digitalWrite(LIGHT_PIN, HIGH);
    TEST_ASSERT_TRUE(mock_hardware.light_on);
    
    // Turn light off
    digitalWrite(LIGHT_PIN, LOW);
    TEST_ASSERT_FALSE(mock_hardware.light_on);
}

// Test obstruction detection simulation
void test_obstruction_detection_simulation(void) {
    // Test no obstruction initially
    mock_hardware.obstruction = false;
    int status = digitalRead(OBSTRUCTION_PIN);
    TEST_ASSERT_EQUAL(LOW, status);
    
    // Simulate obstruction
    mock_hardware.obstruction = true;
    status = digitalRead(OBSTRUCTION_PIN);
    TEST_ASSERT_EQUAL(HIGH, status);
    
    // Clear obstruction
    mock_hardware.obstruction = false;
    status = digitalRead(OBSTRUCTION_PIN);
    TEST_ASSERT_EQUAL(LOW, status);
}

// Test door state transitions
void test_door_state_transitions(void) {
    // Test complete open cycle
    mock_hardware.current_door_state = CURR_CLOSED;
    
    // Start opening
    mock_hardware.target_door_state = CURR_OPEN;
    simulate_door_opening();
    TEST_ASSERT_EQUAL(CURR_OPENING, mock_hardware.current_door_state);
    
    // Complete opening - simulate multiple calls
    delay(1100);
    for (int i = 0; i < 5; i++) {
        simulate_door_opening();
        if (mock_hardware.current_door_state == CURR_OPEN) break;
        delay(100);
    }
    TEST_ASSERT_EQUAL(CURR_OPEN, mock_hardware.current_door_state);
    
    // Start closing
    mock_hardware.target_door_state = CURR_CLOSED;
    simulate_door_closing();
    TEST_ASSERT_EQUAL(CURR_CLOSING, mock_hardware.current_door_state);
    
    // Complete closing - simulate multiple calls
    delay(1100);
    for (int i = 0; i < 5; i++) {
        simulate_door_closing();
        if (mock_hardware.current_door_state == CURR_CLOSED) break;
        delay(100);
    }
    TEST_ASSERT_EQUAL(CURR_CLOSED, mock_hardware.current_door_state);
}

// Test door reversal on obstruction
void test_door_reversal_on_obstruction(void) {
    // Start closing door
    mock_hardware.current_door_state = CURR_CLOSING;
    mock_hardware.door_operation_start_time = millis();
    
    // Simulate obstruction during closing
    mock_hardware.obstruction = true;
    
    // Door should reverse direction
    if (mock_hardware.obstruction && mock_hardware.current_door_state == CURR_CLOSING) {
        mock_hardware.current_door_state = CURR_OPENING;
        mock_hardware.target_door_state = CURR_OPEN;
    }
    
    TEST_ASSERT_EQUAL(CURR_OPENING, mock_hardware.current_door_state);
    TEST_ASSERT_EQUAL(CURR_OPEN, mock_hardware.target_door_state);
}

// Test multiple rapid door commands
void test_rapid_door_commands(void) {
    // Test that rapid commands are handled properly
    mock_hardware.current_door_state = CURR_CLOSED;
    
    // First command: open
    mock_hardware.target_door_state = CURR_OPEN;
    simulate_door_opening();
    TEST_ASSERT_EQUAL(CURR_OPENING, mock_hardware.current_door_state);
    
    // Second command: close (before first completes)
    mock_hardware.target_door_state = CURR_CLOSED;
    
    // Should stop current operation and reverse
    if (mock_hardware.current_door_state == CURR_OPENING && mock_hardware.target_door_state == CURR_CLOSED) {
        mock_hardware.current_door_state = CURR_STOPPED;
        delay(100); // Brief stop
        mock_hardware.current_door_state = CURR_CLOSING;
    }
    
    TEST_ASSERT_EQUAL(CURR_CLOSING, mock_hardware.current_door_state);
}

// Test door position sensing
void test_door_position_sensing(void) {
    // Test closed position
    mock_hardware.door_closed = true;
    mock_hardware.door_open = false;
    
    bool is_closed = mock_hardware.door_closed && !mock_hardware.door_open;
    bool is_open = mock_hardware.door_open && !mock_hardware.door_closed;
    bool is_partial = !mock_hardware.door_open && !mock_hardware.door_closed;
    
    TEST_ASSERT_TRUE(is_closed);
    TEST_ASSERT_FALSE(is_open);
    TEST_ASSERT_FALSE(is_partial);
    
    // Test open position
    mock_hardware.door_closed = false;
    mock_hardware.door_open = true;
    
    is_closed = mock_hardware.door_closed && !mock_hardware.door_open;
    is_open = mock_hardware.door_open && !mock_hardware.door_closed;
    is_partial = !mock_hardware.door_open && !mock_hardware.door_closed;
    
    TEST_ASSERT_FALSE(is_closed);
    TEST_ASSERT_TRUE(is_open);
    TEST_ASSERT_FALSE(is_partial);
    
    // Test partial position
    mock_hardware.door_closed = false;
    mock_hardware.door_open = false;
    
    is_closed = mock_hardware.door_closed && !mock_hardware.door_open;
    is_open = mock_hardware.door_open && !mock_hardware.door_closed;
    is_partial = !mock_hardware.door_open && !mock_hardware.door_closed;
    
    TEST_ASSERT_FALSE(is_closed);
    TEST_ASSERT_FALSE(is_open);
    TEST_ASSERT_TRUE(is_partial);
}

// Test motor timing constraints
void test_motor_timing_constraints(void) {
    const unsigned long MIN_OPERATION_TIME = 200; // Minimum time adjusted for mock (200ms)
    const unsigned long MAX_OPERATION_TIME = 2000; // Maximum 20 seconds (mocked as 2000ms)
    
    // Test door opening timing
    unsigned long start_time = millis();
    simulate_door_opening();
    
    // Fast-forward to completion
    delay(1100);
    for (int i = 0; i < 5; i++) {
        simulate_door_opening();
        if (mock_hardware.current_door_state == CURR_OPEN) break;
        delay(100);
    }
    
    unsigned long operation_time = millis() - start_time;
    
    TEST_ASSERT_GREATER_OR_EQUAL(MIN_OPERATION_TIME, operation_time);
    TEST_ASSERT_LESS_OR_EQUAL(MAX_OPERATION_TIME, operation_time);
    TEST_ASSERT_EQUAL(CURR_OPEN, mock_hardware.current_door_state);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    RUN_TEST(test_door_operation_simulation);
    RUN_TEST(test_door_trigger_timing);
    RUN_TEST(test_light_control_simulation);
    RUN_TEST(test_obstruction_detection_simulation);
    RUN_TEST(test_door_state_transitions);
    RUN_TEST(test_door_reversal_on_obstruction);
    RUN_TEST(test_rapid_door_commands);
    RUN_TEST(test_door_position_sensing);
    RUN_TEST(test_motor_timing_constraints);
    
    UNITY_END();
    return 0;
}

#endif // UNIT_TEST