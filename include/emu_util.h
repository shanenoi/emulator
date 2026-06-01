#ifndef EMU_UTIL_H
#define EMU_UTIL_H

#include <stdbool.h>
#include <stdint.h>

bool emu_checked_add_u64(uint64_t left, uint64_t right, uint64_t *out);
bool emu_checked_add_i64(uint64_t base, int64_t offset, uint64_t *out);
bool emu_parse_u64_strict(const char *text, uint64_t *out);

#endif
