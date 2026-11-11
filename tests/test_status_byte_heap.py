"""Check the production heap fields and their cost in the existing JSON buffer."""
from pathlib import Path
import json
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class StatusByteHeapTest(unittest.TestCase):
    def test_caps_fields_and_bounded_serialization(self):
        source = (ROOT / "src/web.cpp").read_text()
        begin = source.index('    JSON_ADD_INT("freeHeap", free_heap);',
                             source.index("void build_status_json("))
        end = source.index('    JSON_ADD_INT("crashCount",', begin)
        fields = source[begin:end]
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define JSON_BUFFER_SIZE capacity
#include "json.h"
constexpr uint32_t MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=4;
unsigned calls=0;
size_t heap_caps_get_free_size(uint32_t caps) {
    assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)); ++calls; return UINT32_MAX;
}
size_t heap_caps_get_minimum_free_size(uint32_t caps) {
    assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)); ++calls; return UINT32_MAX-1;
}
size_t heap_caps_get_largest_free_block(uint32_t caps) {
    assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)); ++calls; return UINT32_MAX-2;
}
int main(int argc,char **argv) {
    assert(argc==3);
    const size_t capacity=std::strtoul(argv[1],nullptr,10);
    const unsigned existingFields=std::strtoul(argv[2],nullptr,10);
    assert(capacity>=8 && capacity<=3072);
    unsigned char guarded[3072+16]; std::memset(guarded,0xa5,sizeof(guarded));
    char *json=reinterpret_cast<char *>(guarded+8);
    JSON_START(json);
    // Representative status data: many short fields, not one oversized string.
    for (unsigned i=0;i<existingFields;++i) {
        char key[24]; std::snprintf(key,sizeof(key),"existing%03u",i);
        JSON_ADD_STR(key,"xxxxxxxxxxxxxxxxxxxx");
    }
    uint32_t free_heap=123456,min_heap=111111;
''' + fields + r'''
    JSON_ADD_BOOL("endOfStatus",true);
    JSON_END();
    assert(calls==3);
    assert(std::strlen(json)<capacity);
    for (unsigned i=0;i<8;++i) assert(guarded[i]==0xa5);
    for (size_t i=capacity+8;i<sizeof(guarded);++i) assert(guarded[i]==0xa5);
    std::puts(json);
}
'''
        with tempfile.TemporaryDirectory(prefix="ratgdo-byte-heap-") as directory:
            cpp = Path(directory) / "heap.cpp"
            binary = Path(directory) / "heap"
            cpp.write_text(harness)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17",
                            "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-I", str(ROOT / "src"),
                            str(cpp), "-o", str(binary)], check=True)
            # Existing status captures are about 2.5 KiB. Sixty-four comparable
            # short fields plus the existing heap fields exercise that range.
            result = subprocess.check_output([str(binary), "3072", "64"], text=True)
            value = json.loads(result)
            self.assertEqual(value["freeHeap"], 123456)
            self.assertEqual(value["minHeap"], 111111)
            self.assertEqual(value["freeInternal8Bit"], 2**32-1)
            self.assertEqual(value["minInternal8Bit"], 2**32-2)
            self.assertEqual(value["largestInternal8Bit"], 2**32-3)
            self.assertTrue(value["endOfStatus"])
            self.assertEqual(len(value), 70)
            self.assertLess(len(result), 2800)
            # Clipped responses must still close the object and stay in-bounds.
            for capacity in (8, 16, 32, 64, 128, 256, 257, 512, 3071, 3072):
                with self.subTest(capacity=capacity):
                    clipped = subprocess.check_output(
                        [str(binary), str(capacity), "100"], text=True)
                    json.loads(clipped)
                    self.assertLess(len(clipped.rstrip("\n")), capacity)


if __name__ == "__main__":
    unittest.main()
