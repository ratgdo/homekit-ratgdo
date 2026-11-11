#!/usr/bin/env python3
"""Exercise actual log-ring appends and chunked output with ASan and UBSan."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get("LOG_SNAPSHOT_TEST_SOURCE", ROOT / "src/log.cpp"))


class LogSnapshotTest(unittest.TestCase):
    def test_chunked_output_preserves_logging_and_detects_overwrites(self):
        source = SOURCE.read_text()
        start = source.index("void LOG::printMessageLog(Print &outputDev, bool slow)")
        end = source.index("/****************************************************************************\n * Syslog", start)
        output = source[start:end]
        start = source.index("    size_t len = strlen(lineBuffer);")
        end = source.index("    static bool inFn = false;", start)
        append = source[start:end]
        counter_match = re.search(r"static\s+uint64_t\s+logAppendBytes\s*=\s*0\s*;", source)
        self.assertIsNotNone(counter_match, "the append cursor must use the production 64-bit counter")
        counter = counter_match.group()
        header = (ROOT / "lib/ratgdo/log.h").read_text()
        structure = re.search(r"typedef struct logBuffer\s*\{.*?\} logBuffer;", header, re.S).group()
        harness=r'''
        #include <algorithm>
        #include <cassert>
        #include <cstdint>
        #include <cstring>
        #include <ctime>
        #include <functional>
        #include <string>
        #include <mutex>
        #include <thread>
        #include <condition_variable>
        #include <iostream>
        using std::min;
        #define LOG_BUFFER_SIZE (1024*16)
        ''' + structure + r'''
        thread_local unsigned held=0;
        ''' + counter + r'''
        void *checkedCopy(void*d,const void*s,size_t n){assert(held>0&&n<=256);return std::memcpy(d,s,n);}
        #define memcpy checkedCopy
        #define TAKE_MUTEX() do{logMutex.lock();++held;}while(0)
        #define GIVE_MUTEX() do{assert(held);--held;logMutex.unlock();}while(0)
        #define YIELD() do{assert(!held);std::this_thread::yield();}while(0)
        #define PSTR(s) s
        #define AUTO_VERSION "test"
        #define TCP_SND_BUF 5744
        using _millis_t=uint64_t;
        uint64_t _millis(){return 123;}
        const char *toHHMMSSmmm(uint64_t){return "test";}
        const char *timeString(time_t){return "test";}
        bool clockSet=false;
        uint64_t lastRebootAt=0;
        unsigned free_heap=0,min_heap=0;
        int doorControlType=0;
        const int DOOR_CONTROL_SEC_PLUS_V2=2;
        const char*gdoFirmwareVersion="test";
        class Print {
        public:
         std::string data;
         unsigned writes=0,flushes=0;
         size_t shortLimit=SIZE_MAX;
         std::function<void()> onWrite,onFlush;
         size_t write(const uint8_t*p,size_t n){assert(!held);++writes;if(onWrite)onWrite();n=std::min(n,shortLimit);data.append(reinterpret_cast<const char*>(p),n);return n;}
         size_t write(const char*p,size_t n){return write(reinterpret_cast<const uint8_t*>(p),n);}
         void flush(){assert(!held);++flushes;if(onFlush)onFlush();}
         void print(const char*s){assert(!held);data+=s;}
         void println(const char*){assert(!held);}
         template<typename...T>void printf_P(const char*,T...){assert(!held);}
        };
        class LOG {
        public:
         logBuffer storage{},*msgBuffer=&storage;
         std::recursive_mutex logMutex;
         void printMessageLog(Print&,bool);
         void append(const std::string&line){assert(line.size()<256);const char*lineBuffer=line.c_str();TAKE_MUTEX();
        ''' + append + r'''
         GIVE_MUTEX();}
         void reset(uint64_t base=0){std::memset(&storage,0,sizeof(storage));logAppendBytes=base;}
         void appendAll(const std::string&s){for(size_t at=0;at<s.size();at+=255)append(s.substr(at,255));}
        };
        ''' + output + r'''
        std::string pattern(size_t n){std::string s;for(size_t i=0;i<n;++i)s+=char('a'+i%26);return s;}
        const std::string notice="\nLog truncated: newer messages overwrote unread log data.\n";
        #ifdef ESP8266
        int main(){
         LOG log;const size_t capacity=sizeof(log.storage.buffer);
         for(size_t n:{size_t(0),size_t(1),size_t(255),size_t(256),capacity-1,capacity,capacity+1,capacity*2+17}){
          log.reset();log.appendAll(pattern(n));Print p;log.printMessageLog(p,true);
          std::string expected;
          if(log.storage.wrapped){size_t start=((log.storage.head+4)&~size_t(3))%capacity;expected.append(log.storage.buffer+start,capacity-start);}
          expected+=std::string(log.storage.buffer);
          assert(p.data==expected);
         }
         std::cout<<"actual-source ESP8266 IRAM output branch: all cases passed\\n";
        }
        #else
        int main(){
         LOG log;const size_t capacity=sizeof(log.storage.buffer);
         for(size_t n:{size_t(0),size_t(1),size_t(255),size_t(256),capacity-1,capacity,capacity+1,capacity*2+17}){
          log.reset();std::string input=pattern(n);log.appendAll(input);Print p;log.printMessageLog(p,true);
          std::string expected=input.substr(n>capacity-1?n-(capacity-1):0);
          assert(p.data==expected);assert(!held);
         }
         log.reset(1ULL<<40);auto original=pattern(8000);log.appendAll(original);assert(logAppendBytes==(1ULL<<40)+original.size());Print high;log.printMessageLog(high,false);assert(high.data==original);
         log.reset();log.appendAll(original);Print append;
         append.onWrite=[&](){log.append("new output log\n");};log.printMessageLog(append,true);assert(append.data==original);
         log.reset();log.appendAll(original);Print header;
         header.onFlush=[&](){if(header.flushes==1)log.append("header log\n");};log.printMessageLog(header,true);assert(header.data==original);
         log.reset();log.appendAll(original);Print lapped;
         lapped.onWrite=[&](){if(lapped.writes==1)log.appendAll(pattern(capacity));};log.printMessageLog(lapped,true);
         assert(lapped.data==original.substr(0,256)+notice);assert(lapped.writes==1);
         // Even a full ring has a useful stable prefix when header output logs.
         log.reset();auto full=pattern(capacity-1);log.appendAll(full);Print exact;
         exact.onFlush=[&](){if(exact.flushes==1)log.append("x");};log.printMessageLog(exact,true);assert(exact.data==full);
         log.reset();log.appendAll(full);Print headerLapped;
         headerLapped.onFlush=[&](){if(headerLapped.flushes==1)log.appendAll(pattern(capacity));};log.printMessageLog(headerLapped,true);
         assert(headerLapped.data==full.substr(0,256)+notice);
         for(size_t limit:{size_t(0),size_t(253)}){
          log.reset();log.appendAll(original);Print shortWrite;shortWrite.shortLimit=limit;log.printMessageLog(shortWrite,true);
          assert(shortWrite.writes==1&&shortWrite.data==original.substr(0,limit)&&shortWrite.flushes==1);
         }
         log.reset();log.appendAll(original);Print reader1,reader2;
         std::mutex sync;std::condition_variable cv;unsigned ready=0;bool appended=false;
         auto waitForWriter=[&](){std::unique_lock<std::mutex> lock(sync);++ready;cv.notify_all();cv.wait(lock,[&](){return appended;});};
         reader1.onWrite=[&](){if(reader1.writes==1)waitForWriter();};reader2.onWrite=[&](){if(reader2.writes==1)waitForWriter();};
         std::thread writer([&](){std::unique_lock<std::mutex> lock(sync);cv.wait(lock,[&](){return ready==2;});lock.unlock();log.append("concurrent tail\n");lock.lock();appended=true;lock.unlock();cv.notify_all();});
         std::thread first([&](){log.printMessageLog(reader1,true);});std::thread second([&](){log.printMessageLog(reader2,false);});
         first.join();second.join();writer.join();assert(reader1.data==original&&reader2.data==original);
         log.reset();log.msgBuffer=nullptr;Print missing;log.printMessageLog(missing,true);assert(missing.data.empty());
         std::cout<<"actual-source log chunks: all cases passed\n";
        }
        #endif
        '''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / "log-chunks.cpp"
            cpp.write_text(harness)
            for platform in ("esp32", "esp8266"):
                with self.subTest(platform=platform):
                    executable = Path(tmp) / ("log-chunks-" + platform)
                    command = [os.environ.get("CXX", "c++"), "-std=c++17", "-g", "-O1", "-pthread",
                               "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                               "-fno-omit-frame-pointer"]
                    if platform == "esp8266":
                        command.append("-DESP8266")
                    subprocess.run(command + [str(cpp), "-o", str(executable)], check=True, timeout=60)
                    subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
