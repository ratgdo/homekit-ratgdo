#ifndef _UTILITIES_H
#define _UTILITIES_H
#include <stdint.h>

void sync_and_restart();
uint32_t read_file_from_flash(const char* filename, uint32_t defaultValue = 0);
void write_file_to_flash(const char *filename, uint32_t* value);

#endif