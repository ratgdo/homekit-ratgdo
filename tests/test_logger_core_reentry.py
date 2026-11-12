#!/usr/bin/env python3
"""Compile production logger and hook functions against bounded host tests."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
using std::min;
static std::recursive_mutex storage;
static std::recursive_mutex *logMutex = &storage;
constexpr int pdTRUE=1, portMAX_DELAY=-1, LWIP_CORE_LOCK_QUERY_HOLDER=0;
static thread_local bool coreHeld=false;
static bool sys_thread_tcpip(int) { return coreHeld; }
static int xSemaphoreTakeRecursive(std::recursive_mutex *m,int wait) {
    if (!wait) return m->try_lock() ? pdTRUE : 0;
    m->lock(); return pdTRUE;
}
static void xSemaphoreGiveRecursive(std::recursive_mutex *m) { m->unlock(); }
#ifdef ESP8266
// As in log.cpp: ESP8266 is single threaded, so the logger has no mutex.
#define TAKE_MUTEX()
#define GIVE_MUTEX()
#else
#define TAKE_MUTEX() xSemaphoreTakeRecursive(logMutex,portMAX_DELAY)
#define GIVE_MUTEX() xSemaphoreGiveRecursive(logMutex)
#endif
static unsigned serialWrites, sseWrites, syslogWrites, romWrites;
static int esp_rom_printf(const char *,...) { return ++romWrites; }
static int esp_rom_vprintf(const char *,va_list) { return ++romWrites; }
#define SERIAL_PRINT(x) (++serialWrites)
constexpr size_t LINE_BUFFER_SIZE=256;
static char lineBuffer[LINE_BUFFER_SIZE];
struct Ring { size_t head=0; bool wrapped=false; char buffer[4096]={}; };
static Ring ring;
static Ring *msgBuffer=&ring;
static uint64_t logAppendBytes=0;
static const char *toHHMMSSmmm(uint32_t) { return "00:00:00.001"; }
constexpr int LOG_MESSAGE=1;
static std::mutex coreMutex;
static bool recurse=false;
struct LOG { void logToBuffer(const char *,va_list); };
static LOG logger;
static LOG *ratgdoLogger=&logger;
static bool suppressSerialLog=false;
struct MockSerial {
    bool ready=false;
    unsigned begins=0, checks=0;
    explicit operator bool() { ++checks; return ready; }
    void begin(unsigned) { ++begins; ready=true; }
    void print(const char *) { ++serialWrites; }
};
static MockSerial Serial;
// The Gelidus integration initializes its console through this wrapper.
struct MockConsoleState {
    void ensureStarted(MockSerial &port) { if (!port) port.begin(115200); }
    bool isReady() const { return true; }
};
static MockConsoleState serialConsole;
void esp_log_hook(const char *,va_list);
static void emitHook(const char *fmt,...) {
    va_list args; va_start(args,fmt); esp_log_hook(fmt,args); va_end(args);
}
static void emit(const char *fmt,...) {
    va_list args; va_start(args,fmt); logger.logToBuffer(fmt,args); va_end(args);
}
static void SSEBroadcastState(const char *,int) {
    ++sseWrites;
    std::lock_guard<std::mutex> core(coreMutex);
    if (recurse) { recurse=false; emit("prefix () nested\n"); }
}
static void logToSyslog(char *) { ++syslogWrites; }
"""
CASES = r"""
int main() {
    emit("prefix () ordinary\n");
    assert(sseWrites==1 && syslogWrites==1 && serialWrites==1 && ring.head>0);
#ifndef ESP8266
    const unsigned oldSSE=sseWrites, oldSyslog=syslogWrites, oldSerial=serialWrites;
    const size_t oldHead=ring.head;
    {
        std::lock_guard<std::mutex> core(coreMutex);
        coreHeld=true;
        emit("prefix () core-held\n");
        coreHeld=false;
    }
    assert(sseWrites==oldSSE && syslogWrites==oldSyslog);
    assert(serialWrites==oldSerial && romWrites==1 && ring.head>oldHead);
    // A logger already owns the logger lock and waits for core. The task
    // holding core must drop the contended message instead of waiting back.
    std::atomic<bool> acquired{false};
    std::thread worker;
    const unsigned serialBeforeContention=serialWrites;
    const size_t headBeforeContention=ring.head;
    {
        std::lock_guard<std::mutex> core(coreMutex);
        worker=std::thread([&] {
            storage.lock(); acquired=true;
            std::lock_guard<std::mutex> blocked(coreMutex);
            storage.unlock();
        });
        while (!acquired.load()) std::this_thread::yield();
        coreHeld=true;
        emit("prefix () contended\n");
        coreHeld=false;
    }
    worker.join();
    assert(serialWrites==serialBeforeContention && romWrites==1 && ring.head==headBeforeContention);
    assert(sseWrites==oldSSE && syslogWrites==oldSyslog);
#endif
    const unsigned beforeSSE=sseWrites, beforeSyslog=syslogWrites;
    recurse=true; emit("prefix () recurse\n");
    assert(sseWrites==beforeSSE+1 && syslogWrites==beforeSyslog+1);

    // Ordinary hook startup and initialized logger delivery remain intact.
    Serial.ready=false;
    unsigned begins=Serial.begins, sends=sseWrites, syslogs=syslogWrites;
    emitHook("prefix () hook startup\n");
    assert(Serial.begins==begins+1 && Serial.ready);
    assert(sseWrites==sends+1 && syslogWrites==syslogs+1);
#ifndef ESP8266
    // Core-held hooks must not inspect/start the UART or enter serial/network
    // sinks. They still reach the console through ROM printf, including the
    // fallback before the logger exists.
    Serial.ready=false;
    begins=Serial.begins;
    const unsigned checks=Serial.checks, writes=serialWrites;
    sends=sseWrites; syslogs=syslogWrites;
    size_t head=ring.head;
    const unsigned rom=romWrites;
    coreHeld=true;
    emitHook("prefix () core hook\n");
    assert(ring.head>head && romWrites==rom+1);
    head=ring.head;
    ratgdoLogger=nullptr;
    emitHook("prefix () early core hook\n");
    coreHeld=false;
    assert(ring.head==head && romWrites==rom+2);
    assert(Serial.begins==begins && Serial.checks==checks && serialWrites==writes);
    assert(sseWrites==sends && syslogWrites==syslogs);
#else
    // ESP8266 still initializes and writes the console normally.
    coreHeld=true;
    Serial.ready=false;
    begins=Serial.begins;
    sends=sseWrites;
    emitHook("prefix () ESP8266 hook\n");
    assert(Serial.begins==begins+1 && sseWrites==sends+1);
    ratgdoLogger=nullptr;
    coreHeld=false;
#endif
    // Non-core early fallback is preserved on both platforms.
    Serial.ready=false;
    begins=Serial.begins;
    const unsigned fallbackWrites=serialWrites;
    sends=sseWrites; syslogs=syslogWrites;
    emitHook("prefix () early normal hook\n");
    assert(Serial.begins==begins+1 && serialWrites>fallbackWrites);
    assert(sseWrites==sends && syslogWrites==syslogs);
    ratgdoLogger=&logger;
}
"""


class LoggerCoreReentryTest(unittest.TestCase):
    def test_actual_logger_function(self):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        if not compiler:
            self.skipTest("C++ compiler unavailable")
        source = (ROOT / "src/log.cpp").read_text()
        start = source.index("void LOG::logToBuffer(")
        body = source[start:source.index("\nvoid LOG::clearCrashLog()", start)]
        hook_start = source.index("void esp_log_hook(")
        hook = source[hook_start:source.index("\n#ifdef ESP8266", hook_start)]
        with tempfile.TemporaryDirectory(prefix="ratgdo-logger-test-") as directory:
            cpp = Path(directory) / "logger.cpp"
            cpp.write_text(HARNESS + body + hook + CASES)
            for target in ("ESP32", "ESP8266"):
                with self.subTest(target=target):
                    binary = Path(directory) / target
                    subprocess.run([compiler, "-std=c++17", "-pthread", "-g",
                                    "-fsanitize=address,undefined", "-D" + target,
                                    str(cpp), "-o", str(binary)], check=True, timeout=30)
                    subprocess.run([str(binary)], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
