// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#include "utilities.h"
#include "log.h"
#include "LittleFS.h"


void sync_and_restart() {
    LittleFS.end();
    delay(10);
    ESP.restart();
}

uint32_t read_file_from_flash(const char* filename, uint32_t defaultValue) {
    // set to default value
    uint32_t value = defaultValue;
    File file = LittleFS.open(filename, "r");
    if (!file) {
        RINFO("%s doesn't exist. creating...", filename);
        write_file_to_flash(filename, &value);
    }
    else {
        value = file.parseInt();
        file.close();
    }
    return value;
}

void write_file_to_flash(const char *filename, uint32_t* value) {
    File file = LittleFS.open(filename, "w");
    RINFO("writing %02X to file %s", *value, filename);
    file.print(*value);
    file.close();
}