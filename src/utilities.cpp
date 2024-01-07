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

uint32_t read_file_from_flash(const char* filename) {

    File file = LittleFS.open(filename, "r");

    if (!file) {
        RINFO("%s doesn't exist. creating...", filename);

        uint32_t count = 0;
        write_file_to_flash(filename, &count);
        return 0;
    }

    uint32_t counter = file.parseInt();

    file.close();

    return counter;
}

void write_file_to_flash(const char *filename, uint32_t* counter) {
    File file = LittleFS.open(filename, "w");
    RINFO("writing %02X to file %s", *counter, filename);

    file.print(*counter);

    file.close();
}