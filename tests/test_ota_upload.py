#!/usr/bin/env python3
"""Compile the real upload callback with fake HTTP/flash I/O and a polling mutex."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, declaration):
    start = source.index(declaration)
    opening = source.index("{", start)
    depth = 0
    for i in range(opening, len(source)):
        depth += (source[i] == "{") - (source[i] == "}")
        if depth == 0:
            return source[start:i + 1]
    raise AssertionError("Unclosed callback")


class OtaUpload(unittest.TestCase):
    def test_esp8266_recovery_delay_wrap(self):
        source = (ROOT / "src/web.cpp").read_text()
        recovery = source[source.index("static bool otaServicesStopped"):
                          source.index("// Common HTTP responses")]
        harness = r'''
#include <cassert>
#include <cstdint>
#define ESP8266 1
#define ESP_LOGW(...) ((void)0)
using _millis_t=uint32_t;
uint32_t now=0;
uint32_t _millis() { return now; }
struct Server { unsigned stops=0; void stop() { ++stops; } } server;
unsigned restarts=0;
void sync_and_restart() { ++restarts; }
''' + recovery + r'''
int main() {
    // The delay spans the 32-bit millis() wrap.
    now=UINT32_MAX-500;
    otaRecoveryPending=true;
    otaRecoveryStart=now; otaRecoveryDelay=OTA_FAILURE_RESTART_MS;
    assert(!recoverStoppedOTAServices());
    now=otaRecoveryStart+OTA_FAILURE_RESTART_MS-1; assert(!recoverStoppedOTAServices());
    now=otaRecoveryStart+OTA_FAILURE_RESTART_MS; assert(recoverStoppedOTAServices());
    assert(restarts==1 && server.stops==1);
    otaRecoveryPending=true; otaRecoveryStart=now; otaRecoveryDelay=OTA_STAGED_RESTART_MS;
    assert(!recoverStoppedOTAServices());
    now=otaRecoveryStart+OTA_STAGED_RESTART_MS; otaUploadActive=true;
    assert(!recoverStoppedOTAServices());
    otaUploadActive=false; assert(recoverStoppedOTAServices());
    assert(restarts==2);
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / "ota-recovery.cpp"
            binary = Path(temporary) / "ota-recovery"
            cpp.write_text(harness)
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-variable", "-fsanitize=address,undefined",
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_actual_callback_pause_cleanup_and_md5_ownership(self):
        source = (ROOT / "src/web.cpp").read_text()
        callback = function(source, "void handle_firmware_upload()\n{")
        recovery = source[source.index("static bool otaServicesStopped"):
                          source.index("// Common HTTP responses")]
        loop_start = source.index("void web_loop()\n{")
        loop_prefix = source[loop_start:source.index("    static char *json = status_json;", loop_start)] + "}\n"
        harness = r'''
#include <cassert>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#define ESP32 1
#define RATGDO_HTTP_LIFECYCLE_EXPERIMENT 1
#define RATGDO32_DISCO 1
#define PSTR(x) x
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define TAKE_MUTEX() ((void)0)
#define GIVE_MUTEX() ((void)0)
#define JSON_START(...) ((void)0)
#define JSON_ADD_INT(...) ((void)0)
#define JSON_END() ((void)0)
#define JSON_REMOVE_NL(...) ((void)0)
#define snprintf_P snprintf
using String=std::string;
using _millis_t=uint64_t;
_millis_t now=0;
_millis_t _millis() { return now; }
bool httpOwnedPending=false;
constexpr int U_FLASH=0;
enum UploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload {
    UploadStatus status=UPLOAD_FILE_START;
    String filename="firmware.bin";
    size_t currentSize=64, totalSize=128;
    uint8_t buf[64]{};
};
struct HttpServer {
    unsigned stops=0;
    void stop() { ++stops; }
    HTTPUpload data;
    std::map<String,String> arguments{{"action","update"},{"size","128"},
                                    {"md5","0123456789abcdef0123456789abcdef"}};
    HTTPUpload &upload() { return data; }
    String arg(const char *key) { return arguments[key]; }
} server;
struct HomeSpan {
    std::shared_mutex mutex;
    std::shared_mutex &getMutex() { return mutex; }
} homeSpan;
bool pollCanRun() {
    return std::async(std::launch::async, [] {
        if (!homeSpan.mutex.try_lock()) return false;
        homeSpan.mutex.unlock(); return true;
    }).get();
}
struct Config { bool password=true; bool getPasswordRequired() { return password; } } config, *userConfig=&config;
bool authenticated=true, _authenticatedUpdate=false;
bool requestAuthenticated() { return authenticated; }
String _updaterError;
size_t firmwareSize=0;
char firmwareMD5[33]{};
bool suspend_service_loop=false;
bool vehicle_setup_done=true, commsRunning=true;
unsigned shutdowns=0;
void shutdown_comms() { ++shutdowns; commsRunning=false; assert(!pollCanRun()); }
unsigned restarts=0;
void sync_and_restart() { assert(pollCanRun()); ++restarts; }
size_t strlcpy(char *to,const char *from,size_t size) {
    size_t n=strlen(from); if (size) { size_t copy=n<size-1?n:size-1; memcpy(to,from,copy);to[copy]=0; }return n;
}
struct Esp {
    uint32_t freeSpace=0x200000;
    uint32_t getFreeSketchSpace() { return freeSpace; }
    uint32_t getFlashChipSpeed() { return 40000000; }
} ESP;
struct SerialPort { template<class T> void print(T) {} } Serial;
bool streamConnected=false, sendOk=true;
unsigned retiredStreams=0;
struct Client { bool connected() { return streamConnected; } };
struct SSESubscription { Client client; bool active=false; };
SSESubscription *firmwareUpdateSub=nullptr;
struct SSELock { explicit operator bool() const { return true; } };
char statusStorage[256]{}, *status_json=statusStorage, writeBuffer[512]{};
void clientWrite(Client,const char *) {}
bool sendSSEEvent(Client,const char *,const char *) { return sendOk; }
void removeSSEsubscription(SSESubscription *s) {
    s->active=false;
    ++retiredStreams;
    SSESubscription *expected=s;
    if (firmwareUpdateSub==expected) firmwareUpdateSub=nullptr;
}
struct FlashUpdate {
    bool beginOk=true, writeOk=true, endOk=true, active=false;
    unsigned begins=0, writes=0, ends=0;
    String checksum;
    bool begin(size_t size,int) {
        ++begins;
        // Oversized images are rejected without stopping the running services.
        const bool ok=beginOk && size<=0x1ff000;
        if (ok) assert(!pollCanRun());
        checksum.clear(); active=ok; return ok;
    }
    size_t write(uint8_t *,size_t size) { ++writes; assert(!pollCanRun()); return writeOk?size:0; }
    bool end(bool=false) { ++ends; if (active) assert(!pollCanRun()); active=false; return endOk; }
    void setMD5(const char *value) { checksum=value; }
} Update;
void _setUpdaterError() { _updaterError="mock flash error"; }
''' + recovery + r'''
bool web_setup_done=true;
bool httpOwnedPoll() { return !httpOwnedPending; }
bool drainRetiredSSE() { return true; }
''' + loop_prefix + callback + r'''
void event(UploadStatus status) { server.data.status=status; handle_firmware_upload(); }
void start(bool verify=false) {
    server.arguments["action"]=verify?"verify":"update";
    event(UPLOAD_FILE_START);
}
int main() {
    // A pause survives returns from the real callback between upload chunks.
    assert(pollCanRun()); start(); assert(!pollCanRun());
    assert(Update.checksum=="0123456789abcdef0123456789abcdef");
    event(UPLOAD_FILE_WRITE); assert(!pollCanRun());
    event(UPLOAD_FILE_END); assert(pollCanRun());
    event(UPLOAD_FILE_END); assert(pollCanRun());
    assert(suspend_service_loop && !vehicle_setup_done && !commsRunning);
    assert(otaRecoveryPending);
    now=OTA_STAGED_RESTART_MS-1; web_loop(); assert(restarts==0);
    now=OTA_STAGED_RESTART_MS; web_loop();
    assert(restarts==1 && server.stops==1 && !otaRecoveryPending);

    // A verify request extends a successful staging deadline. No reboot can
    // happen from an active callback, even when its old deadline has expired.
    start(); event(UPLOAD_FILE_WRITE); event(UPLOAD_FILE_END);
    const auto oldDeadline=otaRecoveryStart+otaRecoveryDelay;
    now=oldDeadline-1; start(true); assert(otaRecoveryPending && otaUploadActive);
    now=oldDeadline+500; assert(!recoverStoppedOTAServices());
    event(UPLOAD_FILE_END); assert(otaRecoveryPending);
    assert(otaRecoveryStart==now && otaRecoveryDelay==OTA_STAGED_RESTART_MS);

    // A disconnected/aborted transfer recovers quickly, only after the core
    // callback is quiescent and HomeSpan's pause has been released.
    start(); event(UPLOAD_FILE_ABORTED);
    assert(otaRecoveryStart==now && otaRecoveryDelay==OTA_FAILURE_RESTART_MS && pollCanRun());
    now+=OTA_FAILURE_RESTART_MS; httpOwnedPending=true;
    assert(!recoverStoppedOTAServices() && restarts==1);
    httpOwnedPending=false; assert(recoverStoppedOTAServices());
    assert(restarts==2);

    // Re-entering START is idempotent; aborted and repeated uploads can recover.
    start(); start(); assert(!pollCanRun());
    event(UPLOAD_FILE_ABORTED); assert(pollCanRun());
    event(UPLOAD_FILE_ABORTED); assert(pollCanRun());
    start(); event(UPLOAD_FILE_WRITE); event(UPLOAD_FILE_END); assert(pollCanRun());

    // Failed flash setup/write/finalization still releases the pause at END.
    Update.beginOk=false; start(); assert(!pollCanRun());
    event(UPLOAD_FILE_END); assert(pollCanRun()); Update.beginOk=true;
    Update.writeOk=false; start(); event(UPLOAD_FILE_WRITE);
    event(UPLOAD_FILE_END); assert(pollCanRun()); Update.writeOk=true;
    Update.endOk=false; start(); event(UPLOAD_FILE_END);
    assert(pollCanRun()); Update.endOk=true;

    // Verify and rejected authentication do not stop HomeSpan or GDO services.
    const unsigned before=shutdowns;
    start(true); assert(pollCanRun());
    event(UPLOAD_FILE_WRITE); event(UPLOAD_FILE_END); assert(pollCanRun());
    authenticated=false; start(); assert(pollCanRun());
    event(UPLOAD_FILE_WRITE); event(UPLOAD_FILE_END); assert(pollCanRun());
    assert(shutdowns==before);
    authenticated=true; start(); event(UPLOAD_FILE_ABORTED); assert(pollCanRun());

    // Rejected authentication cannot cancel an already armed recovery.
    const auto recoveryStart=otaRecoveryStart;
    authenticated=false; start();
    assert(otaRecoveryPending && otaRecoveryStart==recoveryStart);
    authenticated=true;

    // Verify-only and oversize requests that never stopped services must not
    // cause a reboot, even if an older Update error remains set.
    otaServicesStopped=false; otaRecoveryPending=false;
    suspend_service_loop=false; commsRunning=true; vehicle_setup_done=true;
    start(true); event(UPLOAD_FILE_END);
    assert(!otaRecoveryPending && commsRunning && vehicle_setup_done && !suspend_service_loop);
    server.arguments["size"]="99999999";
    start(); event(UPLOAD_FILE_END);
    assert(!otaRecoveryPending && commsRunning && vehicle_setup_done && !suspend_service_loop);

    server.arguments["size"]="128";

    // An upload without md5 keeps the hash set earlier by updateUnderway.
    server.arguments.erase("md5");
    std::strcpy(firmwareMD5,"fedcba9876543210fedcba9876543210");
    start(true); assert(Update.checksum=="fedcba9876543210fedcba9876543210");
    event(UPLOAD_FILE_END);

}
'''
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / "ota-upload.cpp"
            binary = Path(temporary) / "ota-upload"
            cpp.write_text(harness)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                            "-Werror", "-Wno-unused-variable", "-pthread",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
