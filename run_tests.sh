#!/bin/bash

# Comprehensive test runner for homekit-ratgdo
# Usage: ./run_tests.sh [test_type]
# test_type: all, unit, integration, web, performance, hardware

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test results tracking
TESTS_PASSED=0
TESTS_FAILED=0
TOTAL_TESTS=0

# Function to print colored output
print_status() {
    local color=$1
    local message=$2
    echo -e "${color}${message}${NC}"
}

# Function to run a test and track results
run_test() {
    local test_name=$1
    local test_command=$2
    
    print_status $BLUE "Running ${test_name}..."
    
    if eval "$test_command"; then
        print_status $GREEN "✓ ${test_name} PASSED"
        ((TESTS_PASSED++))
    else
        print_status $RED "✗ ${test_name} FAILED"
        ((TESTS_FAILED++))
    fi
    ((TOTAL_TESTS++))
    echo
}

# Function to check if PlatformIO is installed
check_platformio() {
    if ! command -v pio &> /dev/null; then
        print_status $RED "PlatformIO not found. Please install it first:"
        echo "pip install platformio"
        exit 1
    fi
}

# Function to check if Python is available
check_python() {
    if ! command -v python3 &> /dev/null; then
        print_status $RED "Python 3 not found. Please install Python 3."
        exit 1
    fi
}

# Function to run unit tests
run_unit_tests() {
    print_status $YELLOW "=== Running Unit Tests ==="
    
    # Only run tests that exist
    if [ -f "test/test_core/test_main.cpp" ]; then
        run_test "Core functionality tests" "pio test -e native --filter test_core"
    else
        print_status $YELLOW "Core tests not found, skipping..."
    fi
    
    if [ -f "test/test_packet/test_main.cpp" ]; then
        run_test "Packet parsing tests" "pio test -e native --filter test_packet"
    else
        print_status $YELLOW "Packet tests not found, skipping..."
    fi
    
    if [ -f "test/test_reader/test_main.cpp" ]; then
        run_test "Reader functionality tests" "pio test -e native --filter test_reader"
    else
        print_status $YELLOW "Reader tests not found, skipping..."
    fi
    
    # Run secplus library tests if they exist and library is built
    if [ -f "lib/secplus/test_secplus.py" ] && [ -f "lib/secplus/libsecplus.dylib" ]; then
        run_test "Secplus library tests" "cd lib/secplus && python3 test_secplus.py"
    else
        print_status $YELLOW "Secplus library tests not found or library not built, skipping..."
    fi
}

# Function to run integration tests
run_integration_tests() {
    print_status $YELLOW "=== Running Integration Tests ==="
    
    # Run HomeKit integration tests if they exist
    if [ -f "test/test_integration/test_main.cpp" ]; then
        run_test "HomeKit integration tests" "pio test -e test_integration --filter test_integration"
    else
        print_status $YELLOW "Integration tests not found, skipping..."
    fi
}

# Function to run web tests
run_web_tests() {
    print_status $YELLOW "=== Running Web Interface Tests ==="
    
    # Run web API tests
    if [ -f "test/web/test_web_api.py" ]; then
        run_test "Web API tests" "(cd test/web && python3 test_web_api.py)"
    else
        print_status $YELLOW "Web tests not found, skipping..."
    fi
}

# Function to run performance tests
run_performance_tests() {
    print_status $YELLOW "=== Running Performance Tests ==="
    
    # Run memory tests if they exist
    if [ -f "test/test_performance/test_main.cpp" ]; then
        run_test "Memory regression tests" "pio test -e test_performance --filter test_performance"
    else
        print_status $YELLOW "Performance tests not found, skipping..."
    fi
    
    # Build and check memory usage
    run_test "Build size check" "pio run -e esp8266"
    
    # Check for memory usage regression
    print_status $BLUE "Checking memory usage..."
    if pio run -e esp8266 2>&1 | grep -q "RAM:"; then
        RAM_USAGE=$(pio run -e esp8266 2>&1 | grep "RAM:" | awk '{print $4}' | tr -d '%' | cut -d'.' -f1 || echo "0")
        FLASH_USAGE=$(pio run -e esp8266 2>&1 | grep "Flash:" | awk '{print $4}' | tr -d '%' | cut -d'.' -f1 || echo "0")
        
        print_status $BLUE "Memory usage: RAM ${RAM_USAGE}%, Flash ${FLASH_USAGE}%"
        
        if [ "${RAM_USAGE}" -gt 85 ]; then
            print_status $RED "⚠️  RAM usage too high: ${RAM_USAGE}%"
            ((TESTS_FAILED++))
        else
            print_status $GREEN "✓ RAM usage acceptable: ${RAM_USAGE}%"
            ((TESTS_PASSED++))
        fi
        
        if [ "${FLASH_USAGE}" -gt 95 ]; then
            print_status $RED "⚠️  Flash usage too high: ${FLASH_USAGE}%"
            ((TESTS_FAILED++))
        else
            print_status $GREEN "✓ Flash usage acceptable: ${FLASH_USAGE}%"
            ((TESTS_PASSED++))
        fi
        
        ((TOTAL_TESTS+=2))
    fi
}

# Function to run hardware simulation tests
run_hardware_tests() {
    print_status $YELLOW "=== Running Hardware Simulation Tests ==="
    
    # Run hardware simulation tests if they exist
    if [ -f "test/test_hardware/test_main.cpp" ]; then
        run_test "Door simulation tests" "pio test -e test_hardware --filter test_hardware"
    else
        print_status $YELLOW "Hardware simulation tests not found, skipping..."
    fi
}

# Function to run static analysis
run_static_analysis() {
    print_status $YELLOW "=== Running Static Analysis ==="
    
    # Check if cppcheck is available
    if command -v cppcheck &> /dev/null; then
        run_test "Static analysis (cppcheck)" "cppcheck --enable=all --std=c++11 --platform=unix32 --suppress=missingIncludeSystem --suppress=unusedFunction --quiet src/ lib/ratgdo/"
    else
        print_status $YELLOW "cppcheck not found, skipping static analysis"
    fi
}

# Function to run compilation tests
run_compile_tests() {
    print_status $YELLOW "=== Running Compilation Tests ==="
    
    # Test compilation for different environments
    run_test "ESP8266 compilation" "pio run -e esp8266"
    run_test "Test rollover compilation" "pio run -e test_rollover"
}

# Function to print test summary
print_summary() {
    echo
    print_status $BLUE "=== Test Summary ==="
    echo "Total tests run: $TOTAL_TESTS"
    print_status $GREEN "Passed: $TESTS_PASSED"
    print_status $RED "Failed: $TESTS_FAILED"
    
    if [ $TESTS_FAILED -eq 0 ]; then
        print_status $GREEN "🎉 All tests passed!"
        exit 0
    else
        print_status $RED "❌ Some tests failed!"
        exit 1
    fi
}

# Main function
main() {
    local test_type=${1:-all}
    
    print_status $BLUE "homekit-ratgdo Test Runner"
    print_status $BLUE "=========================="
    
    # Check prerequisites
    check_platformio
    check_python
    
    case $test_type in
        "unit")
            run_unit_tests
            ;;
        "integration")
            run_integration_tests
            ;;
        "web")
            run_web_tests
            ;;
        "performance")
            run_performance_tests
            ;;
        "hardware")
            run_hardware_tests
            ;;
        "static")
            run_static_analysis
            ;;
        "compile")
            run_compile_tests
            ;;
        "all")
            run_compile_tests
            run_unit_tests
            run_integration_tests
            run_web_tests
            run_performance_tests
            run_hardware_tests
            run_static_analysis
            ;;
        *)
            print_status $RED "Unknown test type: $test_type"
            echo "Usage: $0 [all|unit|integration|web|performance|hardware|static|compile]"
            exit 1
            ;;
    esac
    
    print_summary
}

# Make sure script is executable and run main function
if [ "${BASH_SOURCE[0]}" == "${0}" ]; then
    main "$@"
fi