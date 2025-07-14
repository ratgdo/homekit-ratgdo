#pragma once

// Mock LittleFS.h for native testing
#include <stdio.h>
#include <stdint.h>

class File {
public:
    File() : fp(nullptr) {}
    File(FILE* f) : fp(f) {}
    ~File() { if (fp) fclose(fp); }
    
    operator bool() const { return fp != nullptr; }
    size_t write(const uint8_t* data, size_t len) { 
        return fp ? fwrite(data, 1, len, fp) : 0; 
    }
    size_t read(uint8_t* data, size_t len) { 
        return fp ? fread(data, 1, len, fp) : 0; 
    }
    void close() { if (fp) { fclose(fp); fp = nullptr; } }
    
private:
    FILE* fp;
};

class LittleFSClass {
public:
    bool begin() { return true; }
    File open(const char* path, const char* mode = "r") {
        return File(fopen(path, mode));
    }
    bool exists(const char* path) { 
        FILE* f = fopen(path, "r");
        if (f) { fclose(f); return true; }
        return false;
    }
};

extern LittleFSClass LittleFS;