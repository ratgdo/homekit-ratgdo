#ifndef _UTILITIES_H
#define _UTILITIES_H
#include <stdint.h>

void sync_and_restart();
uint32_t read_int_from_file(const char *filename, uint32_t defaultValue = 0);
void write_int_to_file(const char *filename, uint32_t *value);

char *read_string_from_file(const char *filename, const char *defaultValue, char *buffer, int bufsize);
void write_string_to_file(const char *filename, const char *value);

#endif