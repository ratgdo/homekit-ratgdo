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

uint8_t read_gdo_security_from_flash(const char* filename) {

    // DEFAULT TO THE NEWER +2.0 SECURITY
    uint8_t secType = 2;
    File file = LittleFS.open(filename, "r");

    if (!file) {
        RINFO("%s doesn't exist. creating...", filename);
        write_gdo_security_to_flash(filename, &secType);
    } else {
        secType = file.parseInt();
        file.close();
    }

    return secType;
}

void write_gdo_security_to_flash(const char *filename, uint8_t* secType) {
    File file = LittleFS.open(filename, "w");
    RINFO("writing %02X to file %s", *secType, filename);

    file.print(*secType);

    file.close();
}
