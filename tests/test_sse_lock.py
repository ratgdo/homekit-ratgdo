#!/usr/bin/env python3
"""Compile the real SSE subscription code from src/web.cpp against a fake mutex."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, declaration):
    start = source.index(declaration)
    depth = 0
    for i in range(source.index("{", start), len(source)):
        depth += (source[i] == "{") - (source[i] == "}")
        if depth == 0:
            return source[start:i + 1]
    raise AssertionError("Unclosed function " + declaration)


HARNESS = r"""
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *SemaphoreHandle_t;
constexpr BaseType_t pdTRUE = 1, pdFALSE = 0;
constexpr TickType_t portMAX_DELAY = 0xffffffff;
// FreeRTOS recursive mutex held by this task (depth) or by another task.
static int depth, jsonHeld;
static bool otherTask;
static TickType_t lastWait;
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t wait) {
    lastWait = wait;
    if (otherTask) { assert(wait == 0 && "would wait on another task"); return pdFALSE; }
    ++depth;
    return pdTRUE;
}
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t) { assert(depth > 0); --depth; return pdTRUE; }
#define TAKE_MUTEX() (++jsonHeld)
#define GIVE_MUTEX() (--jsonHeld)
#define YIELD() ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define PSTR(s) (s)
#define INADDR_NONE 0u
#define CLIENT_WRITE_TIMEOUT 500
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
constexpr char response400missing[] = "400";
constexpr char type_txt[] = "text/plain";
enum BroadcastType : uint8_t { RATGDO_STATUS = 1, LOG_MESSAGE = 2 };
struct IPAddress {
    uint32_t v;
    IPAddress(uint32_t a = 0) : v(a) {}
    bool operator==(const IPAddress &o) const { return v == o.v; }
    explicit operator bool() const { return v; }
    std::string toString() const { return "ip"; }
};
struct String : std::string {
    using std::string::string;
    bool isEmpty() const { return empty(); }
};
static unsigned stops, headers, notFound, events;
struct WiFiClient {
    int id = 0;
    bool live = false;
    bool connected() { return live; }
    void stop() { live = false; ++stops; }
    void setNoDelay(bool) {}
    void setTimeout(int) {}
    IPAddress remoteIP() { return {}; }
};
struct Ticker {
    std::function<void()> fn;
    void attach_ms(uint32_t, std::function<void()> f) { fn = f; }
    void detach() { fn = nullptr; }
};
struct Server {
    String a0;
    WiFiClient c;
    int args() { return 1; }
    String arg(int) { return a0; }
    String uri() { return "/rest/events/0"; }
    WiFiClient client() { return c; }
    void setContentLength(size_t) {}
    void sendContent_P(const char *) { ++headers; }
    void send_P(int, const char *, const char *) {}
} server;
void handle_notfound() { ++notFound; }
static bool web_setup_done = true;
static uint32_t subscriptionCount;
// STRUCT
// LOCK
static bool sendSSEEvent(WiFiClient &, const char *, const char *, bool = false) { assert(depth > 0); ++events; return true; }
static SSESubscription *beat;
void SSEheartbeat(SSESubscription *s) { assert(jsonHeld == 1 && depth > 0); beat = s; }
// FUNCTIONS
int main() {
    // The Ticker only flags the heartbeat; the main loop sends it under JSON, then SSE.
    SSESubscription &s = subscription[2];
    s.clientIP = 7; s.clientUUID = "abc"; s.heartbeatInterval = 1; subscriptionCount = 1;
    server.a0 = "abc"; server.c.live = true; server.c.id = 1;
    SSEHandler(2);
    assert(s.SSEconnected && s.client.id == 1 && headers == 1 && depth == 0);
    s.heartbeatTimer.fn();
    assert(s.heartbeatDue && !beat);
    sendSSEHeartbeats();
    assert(beat == &s && !s.heartbeatDue && depth == 0 && jsonHeld == 0);
    beat = nullptr; sendSSEHeartbeats(); assert(!beat);

    // Status waits for the lock. A log line only tries, and is skipped while
    // another task holds it.
    s.logViewer = true;
    SSEBroadcastState("{}", RATGDO_STATUS); assert(events == 1 && lastWait == portMAX_DELAY);
    SSEBroadcastState("line", LOG_MESSAGE); assert(events == 2 && lastWait == 0);
    otherTask = true;
    SSEBroadcastState("line", LOG_MESSAGE); assert(events == 2);
    otherTask = false;
    assert(depth == 0);

    // A stream request for another id, or an empty one, never takes a slot's client.
    server.a0 = "other"; server.c.id = 9;
    SSEHandler(2); assert(notFound == 1 && s.client.id == 1);
    server.a0 = "";
    SSEHandler(5); assert(notFound == 2 && !subscription[5].SSEconnected && subscription[5].client.id == 0);

    // Removing a subscription twice only counts it once.
    subscriptionCount = 2;
    removeSSEsubscription(&s); removeSSEsubscription(&s);
    assert(subscriptionCount == 1 && !s.SSEconnected && depth == 0);
}
"""


class SSELockTest(unittest.TestCase):
    def test_actual_subscription_locking(self):
        compiler = shutil.which("c++")
        if not compiler:
            self.skipTest("C++ compiler unavailable")
        source = (ROOT / "src/web.cpp").read_text()
        struct = source[source.index("#define SSE_MAX_CHANNELS"):
                        source.index("// During firmware update note which subscribed client")]
        lock = source[source.index("// Serializes the SSE subscriptions"):
                      source.index("// mDNS update management")]
        functions = "\n".join(function(source, d) for d in (
            "void removeSSEsubscription(SSESubscription *s)\n{",
            "static void sendSSEHeartbeats()\n{",
            "void SSEHandler(uint32_t channel)\n{",
            "void SSEBroadcastState(const char *data, BroadcastType type)\n{"))
        harness = (HARNESS.replace("// STRUCT", struct).replace("// LOCK", lock)
                   .replace("// FUNCTIONS", functions))
        with tempfile.TemporaryDirectory(prefix="ratgdo-sse-lock-") as directory:
            cpp = Path(directory) / "sse_lock.cpp"
            binary = Path(directory) / "sse_lock"
            cpp.write_text(harness)
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", str(cpp), "-o", str(binary)],
                           check=True, timeout=60)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
