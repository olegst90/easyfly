#pragma once
#include <stddef.h>
#include <stdint.h>

int write_ppm(const uint8_t *buffer, int width, int height, const char *path);
void DumpHex(const void* data, size_t size);
