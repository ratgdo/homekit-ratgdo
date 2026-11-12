"""Run the production heap check from ratgdo.cpp against fake heap readings."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <cassert>
#include <cstdint>
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define MIN_FREE_HEAP (1024 * 4)
[[maybe_unused]] static const char *TAG = "test";
uint32_t free_heap = (1024 * 1024), min_heap = (1024 * 1024), max_alloc_heap = 0;
unsigned restarts = 0;
void sync_and_restart() { ++restarts; }
#ifdef ESP8266
uint32_t freeNow = 0;
struct { uint32_t getFreeHeap() { return freeNow; } } ESP;
#else
constexpr uint32_t MALLOC_CAP_INTERNAL = 1 << 11, MALLOC_CAP_8BIT = 1 << 2;
uint32_t freeNow = 0, lowest = 0, largest = 0;
static void caps(uint32_t c) { assert(c == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)); }
uint32_t heap_caps_get_free_size(uint32_t c) { caps(c); return freeNow; }
uint32_t heap_caps_get_minimum_free_size(uint32_t c) { caps(c); return lowest; }
uint32_t heap_caps_get_largest_free_block(uint32_t c) { caps(c); return largest; }
#endif
void check() {
CHECK_BODY
}
int main() {
#ifdef ESP8266
    freeNow = 50000; check(); assert(free_heap == 50000 && min_heap == 50000);
    freeNow = 60000; check(); assert(min_heap == 50000);
    freeNow = 4000; check(); assert(min_heap == 4000 && restarts == 1);
#else
    // minHeap is the SDK's exact low-water mark, including dips between checks.
    freeNow = 80000; lowest = 18000; largest = 50000; check();
    assert(free_heap == 80000 && min_heap == 18000 && max_alloc_heap == 50000);
    // A past dip below the reboot threshold does not reboot a recovered heap.
    lowest = 3000; check(); assert(min_heap == 3000 && restarts == 0);
    freeNow = 4000; check(); assert(restarts == 1);
#endif
}
'''


class HeapCheckTest(unittest.TestCase):
    def test_heap_check(self):
        source = (ROOT / "src/ratgdo.cpp").read_text()
        start = source.index("        last_heap_check = current_millis;\n")
        end = source.index("#ifdef MMU_IRAM_HEAP", start)
        body = source[start + len("        last_heap_check = current_millis;\n"):end]
        with tempfile.TemporaryDirectory(prefix="ratgdo-heap-") as directory:
            cpp = Path(directory) / "heap.cpp"
            cpp.write_text(HARNESS.replace("CHECK_BODY", body))
            for target in ("ESP32", "ESP8266"):
                with self.subTest(target=target):
                    binary = Path(directory) / target
                    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                                    "-Werror", "-fsanitize=address,undefined", "-D" + target,
                                    str(cpp), "-o", str(binary)], check=True)
                    subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
