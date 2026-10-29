#!/usr/bin/env python3
"""Exercise the actual logger append block with AddressSanitizer and UBSan."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get("LOG_RING_TEST_SOURCE", ROOT / "src/log.cpp"))


class LogRingBoundaryTest(unittest.TestCase):
    def test_append_boundaries_and_retained_contents(self):
        source = SOURCE.read_text()
        start = source.index("    size_t len = strlen(lineBuffer);")
        end_marker = "msgBuffer->buffer[msgBuffer->head] = 0;"
        end = source.index(end_marker, start) + len(end_marker)
        append = source[start:end]
        header = (ROOT / "lib/ratgdo/log.h").read_text()
        struct = re.search(r"typedef struct logBuffer\s*\{.*?\} logBuffer;", header, re.S).group()
        harness = r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#define LOG_BUFFER_SIZE (1024 * 16)
using std::min;
""" + struct + r"""
void appendLine(logBuffer *msgBuffer, const char *lineBuffer) {
""" + append + r"""
}
std::string contents(const logBuffer *ring) {
    assert(ring->head < sizeof(ring->buffer));
    assert(ring->buffer[ring->head] == 0);
    assert(memchr(ring->buffer, 0, sizeof(ring->buffer)) != nullptr);
    std::string out;
    if (ring->wrapped) {
        size_t start = ring->head + 1;
        out.append(ring->buffer + start, sizeof(ring->buffer) - start);
    }
    out.append(ring->buffer, ring->head);
    return out;
}
int main() {
    auto *ring = static_cast<logBuffer *>(malloc(sizeof(logBuffer)));
    assert(ring);
    const size_t capacity = sizeof(ring->buffer);
    // Every possible logger line length ending exactly at the allocation boundary.
    for (size_t len = 1; len < 256; ++len) {
        memset(ring, 0, sizeof(*ring));
        ring->head = capacity - len;
        memset(ring->buffer, 'x', ring->head);
        std::string line(len, 'y');
        std::string expected(ring->head, 'x');
        expected += line;
        expected.erase(0, expected.size() - (capacity - 1));
        appendLine(ring, line.c_str());
        assert(ring->head == 0 && ring->wrapped == 1);
        assert(contents(ring) == expected);
    }
    // Empty lines and repeated ordinary/cross-boundary appends retain the last N-1 bytes.
    memset(ring, 0, sizeof(*ring));
    std::string expected;
    for (size_t i = 0; i < 2000; ++i) {
        size_t len = (i * 73) % 256;
        std::string line(len, static_cast<char>('a' + i % 26));
        expected += line;
        if (expected.size() > capacity - 1)
            expected.erase(0, expected.size() - (capacity - 1));
        appendLine(ring, line.c_str());
        assert(contents(ring) == expected);
    }
    free(ring);
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / "log-ring.cpp"
            executable = Path(tmp) / "log-ring"
            cpp.write_text(harness)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-g", "-O1",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            str(cpp), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
