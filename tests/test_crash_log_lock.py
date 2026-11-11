#!/usr/bin/env python3
"""Test crash-log locking using the actual production function bodies."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get("CRASH_LOG_TEST_SOURCE", ROOT / "src/log.cpp"))


class CrashLogLockTest(unittest.TestCase):
    def test_crash_output_does_not_hold_logger_lock(self):
        source = SOURCE.read_text()
        start = source.index("void LOG::clearCrashLog()")
        end = source.index("#ifdef ESP8266\nvoid LOG::printCrashLog", start)
        clear = source[start:end]
        start = source.index("void LOG::printCrashLog(Print &outputDev)", end)
        start = source.index("void LOG::printCrashLog(Print &outputDev)", start + 1)
        end = source.index("void LOG::saveMessageLog()", start)
        crash = source[start:end]
        harness = HARNESS.replace("/* ACTUAL_SOURCE */", clear + crash)
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / "crash-log.cpp"
            executable = Path(tmp) / "crash-log"
            cpp.write_text(harness)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-g", "-O1", "-pthread",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            str(cpp), "-o", str(executable)], check=True, timeout=60)
            subprocess.run([str(executable)], check=True, timeout=30)


HARNESS = r'''
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>
#include <ctime>

using _millis_t = int64_t;
std::recursive_mutex logMutex;
thread_local int held=0;
#define TAKE_MUTEX() do { logMutex.lock(); ++held; } while (0)
#define GIVE_MUTEX() do { --held; logMutex.unlock(); } while (0)
struct Buffer { uint32_t wrapped=0, head=0; char buffer[32]{}; } rtcCrashLog;
int32_t crashCount=0; time_t crashTime=0; _millis_t crashUpTime=0;
char reasonString[64]="reason", crashVersion[16]="1.0";
struct esp_core_dump_summary_t { char exc_task[32]; unsigned long exc_pc; struct { int depth; unsigned long bt[12]; } exc_bt_info; };
const int ESP_OK=0;
int check_result=1, summary_result=1, erase_count=0, summary_count=0;
bool fail_alloc=false;
std::mutex wait_lock; std::condition_variable cv; bool block_output=false, started=false, release_output=false;
void assertUnlocked(){ assert(held==0); }
void maybeBlock(){ assertUnlocked(); std::unique_lock<std::mutex> l(wait_lock); if(block_output && !started) { started=true; cv.notify_all(); cv.wait(l,[]{return release_output;}); } }
int esp_core_dump_image_check(){assertUnlocked();return check_result;}
int esp_core_dump_image_erase(){assertUnlocked();++erase_count;return ESP_OK;}
int esp_core_dump_get_summary(esp_core_dump_summary_t *p){assertUnlocked();++summary_count; strcpy(p->exc_task,"main");p->exc_pc=123;p->exc_bt_info.depth=1;p->exc_bt_info.bt[0]=456;return summary_result;}
const char *timeString(time_t){return "time";}
const char *toHHMMSSmmm(_millis_t){return "uptime";}
struct Print {
 std::string out;
 void printf(const char *fmt,...){maybeBlock(); char buf[512]; va_list ap; va_start(ap,fmt); int n=vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap); assert(n>=0 && n<(int)sizeof buf); out.append(buf,n);}
 void print(const char *s){maybeBlock();out+=s;}
 void write(const char *s,size_t n){maybeBlock();out.append(s,n);}
 void flush(){assertUnlocked();}
};
class LOG { public: void clearCrashLog(); void printCrashLog(Print &); };
void *test_malloc(size_t n){assertUnlocked();return fail_alloc?nullptr:std::malloc(n);}
#define malloc test_malloc
/* ACTUAL_SOURCE */
#undef malloc
int main(){
 LOG log; Print out;
 crashCount=1; crashTime=10000; crashUpTime=1000; strcpy(rtcCrashLog.buffer,"ring");
 check_result=ESP_OK; summary_result=ESP_OK; log.printCrashLog(out);
 assert(out.out.find("Crash reason: reason")!=std::string::npos && out.out.find("ring")!=std::string::npos && out.out.find("homekit-ratgdo32-v1.0.elf")!=std::string::npos);
 out.out.clear(); crashCount=0; log.printCrashLog(out);
 assert(out.out.find("No saved message log available")!=std::string::npos && out.out.find("-e firmware.elf")!=std::string::npos);
 out.out.clear(); check_result=1; log.printCrashLog(out); assert(out.out.find("No core dump")!=std::string::npos);
 out.out.clear(); check_result=ESP_OK; summary_result=1; log.printCrashLog(out); assert(out.out.empty());
 summary_result=ESP_OK; fail_alloc=true; summary_count=0; log.printCrashLog(out); assert(summary_count==0 && out.out.empty()); fail_alloc=false;
 crashCount=7; log.clearCrashLog(); assert(crashCount==0 && erase_count==1);
 crashCount=1; check_result=ESP_OK; block_output=true; started=false; release_output=false;
 Print blocked; std::thread t([&]{ log.printCrashLog(blocked); });
 { std::unique_lock<std::mutex> l(wait_lock); cv.wait(l,[]{return started;}); }
 { std::lock_guard<std::recursive_mutex> l(logMutex); crashCount=0; }
 { std::lock_guard<std::mutex> l(wait_lock); release_output=true; } cv.notify_all(); t.join();
 assert(blocked.out.find("Crash reason")!=std::string::npos && blocked.out.find("homekit-ratgdo32")!=std::string::npos);
}
'''


if __name__ == "__main__":
    unittest.main()
