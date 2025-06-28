#ifndef TEST_MILLIS_H
#define TEST_MILLIS_H

#ifdef TEST_ROLLOVER
// Simulate millis() rollover by starting near the maximum value
// This lets us test rollover behavior in minutes instead of 49+ days

#define ROLLOVER_OFFSET (0xFFFFFFFF - 120000UL)  // Start 2 minutes before rollover

// Override millis() function
inline unsigned long millis() {
    extern unsigned long millis_original();
    return millis_original() + ROLLOVER_OFFSET;
}

// Provide access to original millis() function
inline unsigned long millis_original() {
    return ::millis();
}

// Add debug output to monitor rollover
inline void log_rollover_status() {
    static unsigned long last_log = 0;
    unsigned long now = millis();
    
    if (now - last_log > 10000) { // Log every 10 seconds
        last_log = now;
        Serial.printf("ROLLOVER TEST: millis()=%lu, original=%lu, offset=%lu\n", 
                     now, millis_original(), ROLLOVER_OFFSET);
        
        // Alert when rollover occurs
        if (millis_original() > 120000 && now < millis_original()) {
            Serial.println("*** ROLLOVER OCCURRED! ***");
        }
    }
}

#else
// Normal operation - no changes
inline void log_rollover_status() {
    // No-op in normal builds
}
#endif

#endif // TEST_MILLIS_H