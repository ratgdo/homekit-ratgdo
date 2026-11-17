#!/usr/bin/env python3
"""Compile the real sendSSEEvent() from src/web.cpp for ESP32 and ESP8266."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <sys/socket.h>
static unsigned allocations;
void *operator new(size_t size) { ++allocations; if (void *p = malloc(size)) return p; throw std::bad_alloc(); }
void operator delete(void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
#define ESP_LOGW(...) ((void)0)
#define PSTR(s) (s)
#define snprintf_P snprintf
static char out[20000];
static size_t outLength;
static unsigned sends, stops, flushes, printfs;
static long result = -2; // -2: accept everything
static int failErrno = EWOULDBLOCK;
static size_t accept(size_t length) { return result == -2 ? length : (size_t)result; }
static void capture(const void *data, size_t length) { memcpy(out + outLength, data, length); outLength += length; }
struct WiFiClient {
    int fd() { return 3; }
    void stop() { ++stops; }
    void flush() { ++flushes; }
    size_t write(const char *data, size_t length) { ++sends; capture(data, length); return accept(length); }
    size_t printf(const char *format, ...) {
        ++printfs; ++sends;
        va_list args; va_start(args, format);
        size_t length = vsnprintf(out + outLength, sizeof(out) - outLength, format, args);
        va_end(args);
        outLength += length;
        return accept(length);
    }
};
[[maybe_unused]] static ssize_t fakeSendmsg(int, const struct msghdr *message, int flags) {
    assert(flags == MSG_DONTWAIT);
    ++sends;
    size_t length = 0;
    for (size_t i = 0; i < (size_t)message->msg_iovlen; ++i) {
        capture(message->msg_iov[i].iov_base, message->msg_iov[i].iov_len);
        length += message->msg_iov[i].iov_len;
    }
    if (result == -1) errno = failErrno;
    return result == -2 ? (ssize_t)length : result;
}
#define sendmsg fakeSendmsg
static const char *TAG = "test";
[[maybe_unused]] static char writeBuffer[512];
// PRODUCTION
static void reset(long r = -2) { outLength = sends = stops = flushes = printfs = 0; result = r; }
int main() {
    (void)TAG;
    WiFiClient client;
    const unsigned before = allocations;
    static char payload[8193];
    for (size_t length : {0, 2, 600, 8192}) {
        memset(payload, 'x', length); payload[length] = 0;
        reset();
        assert(sendSSEEvent(client, "message", payload));
        assert(sends == 1 && stops == 0);
        assert(outLength == 23 + length);
        assert(!memcmp(out, "event: message\ndata: ", 21));
        assert(!memcmp(out + 21, payload, length) && !memcmp(out + 21 + length, "\n\n", 2));
#ifdef ESP8266
        // Unchanged ESP8266 framing: writeBuffer, or printf once it won't fit.
        assert(flushes == 1 && printfs == (length + 23 >= sizeof(writeBuffer)));
#else
        assert(flushes == 0 && printfs == 0);
#endif
    }
    // Anything short of the whole event stops the client, short or long.
    memset(payload, 'x', 600); payload[600] = 0;
    for (const char *data : {"{}", (const char *)payload}) {
        for (long r : {-1L, 0L, 1L, 22L}) {
            reset(r);
            assert(!sendSSEEvent(client, "logger", data));
            assert(sends == 1 && stops == 1);
        }
    }
#ifdef ESP32
    // With dropIfFull, a full send buffer (nothing written) only drops the
    // event. A partial write or any other error still stops the client.
    reset(-1);
    assert(!sendSSEEvent(client, "logger", "{}", true) && stops == 0);
    for (long r : {-1L, 22L}) {
        reset(r); failErrno = r == -1 ? ECONNRESET : EWOULDBLOCK;
        assert(!sendSSEEvent(client, "logger", "{}", true) && stops == 1);
    }
    failErrno = EWOULDBLOCK;
#else
    reset(0);
    assert(!sendSSEEvent(client, "logger", "{}", true) && stops == 1);
#endif
    assert(allocations == before);
}
"""


class SSETransportTest(unittest.TestCase):
    def test_actual_send_sse_event(self):
        compiler = shutil.which("c++")
        if not compiler:
            self.skipTest("C++ compiler unavailable")
        source = (ROOT / "src/web.cpp").read_text()
        start = source.index("// Send one SSE event.")
        production = source[start:source.index("// Helper functions for connection throttling", start)]
        with tempfile.TemporaryDirectory(prefix="ratgdo-sse-test-") as directory:
            cpp = Path(directory) / "sse.cpp"
            cpp.write_text(HARNESS.replace("// PRODUCTION", production))
            for target in ("ESP32", "ESP8266"):
                with self.subTest(target=target):
                    binary = Path(directory) / target
                    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                    "-fsanitize=address,undefined", "-D" + target,
                                    str(cpp), "-o", str(binary)], check=True, timeout=60)
                    subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
