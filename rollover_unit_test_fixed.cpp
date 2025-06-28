// Unit test for rollover-safe timing
#include <stdio.h>
#include <stdint.h>

bool test_rollover_safe_timing() {
    printf("Testing rollover-safe timing patterns...\n");
    
    // Test case 1: Normal operation (no rollover)
    uint32_t start_time = 10000;
    uint32_t current_time = 15000;
    uint32_t duration = 3000;
    
    bool timeout_old = (current_time > start_time + duration); // Direct comparison
    bool timeout_new = (current_time - start_time >= duration); // Rollover-safe
    
    printf("Normal case: old=%d, new=%d (should both be true)\n", timeout_old, timeout_new);
    if (timeout_old != timeout_new) return false;
    
    // Test case 2: Rollover scenario - start near max, current after rollover
    start_time = 0xFFFFF000; // Near rollover (4294963200)
    current_time = 0x00002000; // After rollover (8192) - total elapsed ~12288ms
    duration = 3000;
    
    timeout_old = (current_time > start_time + duration); // Would be false (broken)
    timeout_new = (current_time - start_time >= duration); // Should be true (works)
    
    printf("Rollover case: old=%d, new=%d (old should be false, new should be true)\n", 
           timeout_old, timeout_new);
    printf("  start_time: %u, current_time: %u, elapsed: %u\n", 
           start_time, current_time, current_time - start_time);
    if (timeout_old != false || timeout_new != true) return false;
    
    // Test case 3: Signed cast for absolute timeouts (like motion_timer)
    uint32_t timeout_abs = 0xFFFFF000; // Absolute timeout before rollover
    current_time = 0x00002000; // Current time after rollover
    
    bool expired_old = (current_time > timeout_abs); // Would be false (broken)
    bool expired_new = ((int32_t)(current_time - timeout_abs) >= 0); // Should be true (works)
    
    printf("Absolute timeout: old=%d, new=%d (old should be false, new should be true)\n",
           expired_old, expired_new);
    printf("  timeout_abs: %u, current_time: %u, diff: %d\n",
           timeout_abs, current_time, (int32_t)(current_time - timeout_abs));
    if (expired_old != false || expired_new != true) return false;
    
    // Test case 4: Verify it works correctly when NOT timed out yet  
    start_time = 0xFFFFF800; // Different start time
    current_time = 0x00000200; // Should give us ~1536ms elapsed
    duration = 3000;
    
    timeout_old = (current_time > start_time + duration); // Would be false
    timeout_new = (current_time - start_time >= duration); // Should be false
    
    printf("Not timed out: old=%d, new=%d (both should be false)\n", timeout_old, timeout_new);
    printf("  elapsed: %u vs duration: %u\n", current_time - start_time, duration);
    if (timeout_old != false || timeout_new != false) return false;
    
    return true;
}

int main() {
    if (test_rollover_safe_timing()) {
        printf("✓ All rollover timing tests passed!\n");
        return 0;
    } else {
        printf("✗ Rollover timing tests failed!\n");
        return 1;
    }
}