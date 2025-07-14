# homekit-ratgdo Testing Infrastructure

This directory contains comprehensive testing infrastructure for the homekit-ratgdo project.

## Test Structure

```
test/
├── test_core/              # Core functionality unit tests
├── test_integration/       # Integration tests (HomeKit, WiFi, etc.)
├── test_packet/           # Security+ packet parsing tests
├── test_reader/           # SecPlus2Reader functionality tests
├── test_performance/      # Memory and performance regression tests
├── test_hardware/         # Hardware simulation tests
├── web/                   # Web interface tests
└── README.md             # This file
```

## Test Categories

### 1. Unit Tests (`test_core/`)
- **Purpose**: Test individual functions and modules in isolation
- **Coverage**: 
  - Rollover-safe timing patterns
  - Door state validation
  - Memory buffer management
  - Protocol command validation
  - Configuration bounds checking
- **Framework**: Unity
- **Run with**: `pio test -e native --filter test_core`

### 2. Integration Tests (`test_integration/`)
- **Purpose**: Test interaction between components
- **Coverage**:
  - HomeKit characteristic mapping
  - Service discovery
  - Error handling
  - State transitions
- **Framework**: Unity
- **Run with**: `pio test -e native --filter test_integration`

### 3. Packet Tests (`test_packet/`)
- **Purpose**: Test Security+ protocol implementation
- **Coverage**:
  - Packet encoding/decoding
  - Wireline protocol handling
  - Command validation
- **Framework**: Unity
- **Run with**: `pio test -e native --filter test_packet`

### 4. Performance Tests (`test_performance/`)
- **Purpose**: Prevent memory and performance regressions
- **Coverage**:
  - Heap memory usage
  - IRAM constraints
  - Stack usage patterns
  - Memory leak detection
- **Framework**: Unity + custom monitoring
- **Run with**: `pio test -e native --filter test_performance`

### 5. Hardware Simulation Tests (`test_hardware/`)
- **Purpose**: Test door operation logic without real hardware
- **Coverage**:
  - Door opening/closing sequences
  - Obstruction detection
  - Emergency stop
  - Light and lock control
  - Sensor debouncing
- **Framework**: Unity + simulation layer
- **Run with**: `pio test -e native --filter test_hardware`

### 6. Web Interface Tests (`web/`)
- **Purpose**: Test web API and interface functionality
- **Coverage**:
  - REST API endpoints
  - JSON response validation
  - Security headers
  - Error handling
- **Framework**: Python unittest
- **Run with**: `python3 test/web/test_web_api.py`

## Running Tests

### Quick Start
```bash
# Run all tests
./run_tests.sh

# Run specific test category
./run_tests.sh unit
./run_tests.sh integration
./run_tests.sh performance
./run_tests.sh web
```

### Individual Test Commands
```bash
# Native unit tests
pio test -e native

# Specific test filter
pio test -e native --filter test_core

# Build and memory check
pio run -e esp8266 -t size

# Web tests
cd test/web && python3 test_web_api.py

# Static analysis (requires cppcheck)
cppcheck --enable=all src/ lib/ratgdo/
```

## Continuous Integration

The project includes GitHub Actions workflows for automated testing:

### Test Workflow (`.github/workflows/test.yml`)
- **Triggers**: Push to main/develop, pull requests
- **Jobs**:
  - Build test (multiple environments)
  - Unit tests (native platform)
  - Static analysis (cppcheck)
  - Memory usage validation

### Memory Regression Protection
- Automatically fails if RAM usage > 85%
- Automatically fails if Flash usage > 95%
- Tracks memory usage trends

## Test Configuration

### PlatformIO Test Environment
```ini
[env:native]
platform = native
framework = 
build_flags = 
    -std=c++11
    -D UNIT_TEST
    -D NATIVE_BUILD
test_framework = unity
lib_deps = Unity
```

### Mock Hardware Layer
Tests use a comprehensive hardware simulation layer that:
- Simulates ESP8266 memory functions
- Mocks Arduino framework functions
- Provides realistic timing and sensor behavior
- Enables testing without physical hardware

## Adding New Tests

### 1. Unit Tests
Create new test files in appropriate subdirectories:
```cpp
#include <unity.h>

void test_new_functionality(void) {
    // Test implementation
    TEST_ASSERT_EQUAL(expected, actual);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_new_functionality);
    UNITY_END();
    return 0;
}
```

### 2. Integration Tests
Follow the same pattern but focus on component interactions:
```cpp
void test_component_integration(void) {
    // Setup components
    // Trigger interaction
    // Verify results
}
```

### 3. Performance Tests
Add memory or timing validations:
```cpp
void test_memory_usage(void) {
    uint32_t before = ESP_getFreeHeap();
    // Perform operation
    uint32_t after = ESP_getFreeHeap();
    
    int memory_used = before - after;
    TEST_ASSERT_LESS_THAN(MAX_ACCEPTABLE, memory_used);
}
```

## Test Requirements

### Hardware Requirements
- **None** - All tests run on native platform with hardware simulation

### Software Requirements
- PlatformIO Core
- Python 3.x
- Unity testing framework (automatically installed)
- cppcheck (optional, for static analysis)

### ESP8266 Constraints Tested
- **RAM**: 80KB total, tests ensure < 85% usage
- **Flash**: 4MB total, tests ensure < 95% usage  
- **IRAM**: 32KB total, critical for interrupt handlers
- **Stack**: Limited depth, especially in ISRs

## Coverage Goals

### Current Coverage Areas
✅ Protocol handling (Security+)  
✅ Memory management  
✅ Timing and rollover safety  
✅ HomeKit integration  
✅ Web interface validation  
✅ Hardware simulation  
✅ Error handling  

### Future Coverage Areas
- [ ] Network connectivity edge cases
- [ ] Firmware update validation
- [ ] Long-term reliability testing
- [ ] Power management scenarios
- [ ] Multi-client HomeKit scenarios

## Debugging Tests

### Common Issues
1. **Memory tests failing**: Check for memory leaks or increased usage
2. **Timing tests failing**: Verify rollover-safe patterns
3. **Hardware tests failing**: Check simulation state management
4. **Web tests failing**: Verify mock responses match actual API

### Debug Commands
```bash
# Verbose test output
pio test -e native -v

# Run single test with debug
pio test -e native --filter test_core -v

# Memory analysis
pio run -e esp8266 -t size --verbose
```

## Best Practices

### Writing Tests
1. **Isolation**: Each test should be independent
2. **Deterministic**: Tests should produce consistent results
3. **Fast**: Unit tests should complete quickly
4. **Meaningful**: Test names should describe what they verify
5. **Comprehensive**: Cover both success and failure paths

### Test Maintenance
1. Update tests when changing functionality
2. Add regression tests for bug fixes
3. Review test coverage regularly
4. Keep mocks updated with real implementations
5. Document complex test scenarios

## Troubleshooting

### PlatformIO Issues
```bash
# Clean and rebuild
pio run -t clean
pio test -e native

# Update platform
pio platform update

# Reinstall dependencies
pio lib install
```

### Python Test Issues
```bash
# Install dependencies
pip3 install requests

# Run with verbose output
python3 -m unittest test.web.test_web_api -v
```

This testing infrastructure provides comprehensive coverage for the homekit-ratgdo project while maintaining the ability to run without physical hardware.