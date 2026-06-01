#include "emu_util.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

bool emu_checked_add_u64(uint64_t left, uint64_t right, uint64_t *out) {
    if (right > UINT64_MAX - left) {
        return false;
    }
    *out = left + right;
    return true;
}

bool emu_checked_add_i64(uint64_t base, int64_t offset, uint64_t *out) {
    if (offset < 0) {
        uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1ull;
        if (magnitude > base) {
            return false;
        }
        *out = base - magnitude;
        return true;
    }

    return emu_checked_add_u64(base, (uint64_t)offset, out);
}

bool emu_parse_u64_strict(const char *text, uint64_t *out) {
    if (text == NULL || text[0] == '\0' || text[0] == '-' || text[0] == '+') {
        return false;
    }
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}
