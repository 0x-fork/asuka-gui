// checksum.h: the app's C, which its extern declarations call.
#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

uint32_t checksum_crc32(const uint8_t *data, size_t len);
int64_t checksum_seed(void);
int32_t checksum_lines(const char *text, void (*each)(void *ctx, int64_t index, uint32_t crc), void *ctx);
void checksum_later(const char *text, void (*done)(void *ctx, uint32_t crc), void *ctx);
void checksum_wait(void);

#endif
