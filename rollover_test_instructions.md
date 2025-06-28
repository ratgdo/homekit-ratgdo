# Accelerated Rollover Test Instructions

## Setup

1. **Apply the test files:**
   ```bash
   # The test_millis.h file is already created
   # Apply the patch to ratgdo.cpp:
   patch -p1 < apply_rollover_test.patch
   ```

2. **Build with test flags:**
   ```bash
   # Using the test environment
   pio run -e test_rollover
   
   # Or manually add to your build_flags in platformio.ini:
   # -D TEST_ROLLOVER
   ```

3. **Flash and monitor:**
   ```bash
   pio run -e test_rollover -t upload
   pio device monitor -e test_rollover
   ```

## What Happens

- Device starts with millis() = ~4294847295 (2 minutes before rollover)
- After ~2 minutes of runtime, millis() will wrap to 0
- Serial output shows rollover status every 10 seconds
- You'll see "*** ROLLOVER OCCURRED! ***" when it happens

## Test Schedule

**Minutes 0-2: Pre-rollover**
- Test manual recovery (press wall button 5 times quickly)
- Test motion detection if available
- Change WiFi settings to test 30-second timeout
- Verify all features work normally

**Minutes 2-4: Post-rollover**  
- Repeat all the same tests
- Verify timing still works correctly
- Check that no features hang or malfunction

## Expected Serial Output

```
ROLLOVER TEST: millis()=4294847295, original=120000, offset=4294727295
ROLLOVER TEST: millis()=4294857295, original=130000, offset=4294727295
...
ROLLOVER TEST: millis()=4294957295, original=230000, offset=4294727295
*** ROLLOVER OCCURRED! ***
ROLLOVER TEST: millis()=10000, original=240000, offset=4294727295
ROLLOVER TEST: millis()=20000, original=250000, offset=4294727295
```

## Test Cases

### 1. Manual Recovery Test
- **Before rollover**: Press wall button 5 times in 3 seconds → should enter WiFi recovery
- **After rollover**: Same test → should work identically

### 2. WiFi Timeout Test
- **Before rollover**: Change WiFi settings → should timeout in 30 seconds
- **Across rollover**: Change settings 1 minute before rollover → should still timeout correctly

### 3. Motion Timer Test
- **Before rollover**: Trigger motion → should clear after 5 seconds
- **After rollover**: Same test → should work identically

### 4. Status Timeout Test
- **Before rollover**: Reboot device → HomeKit should start after 2 seconds
- **After rollover**: Same test → should work identically

## Success Criteria

- All timeouts work correctly before rollover
- Rollover occurs without crashes or hangs  
- All timeouts work correctly after rollover
- No infinite waits or premature timeouts
- Device functions normally throughout test

## Cleanup

After testing, remove the test code:
```bash
git checkout src/ratgdo.cpp  # Undo the patch
rm src/test_millis.h         # Remove test file
# Remove -D TEST_ROLLOVER from build flags
```