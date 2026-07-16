/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lines.h"

#include <ctype.h>

size_t line_byte_offset(const char *data, size_t len, long lineno, char sep) {
    size_t i;
    long line;

    if (lineno <= 1) {
        return 0;
    }

    line = 1;
    for (i = 0; i < len; i++) {
        if (data[i] == sep) {
            line++;
            if (line == lineno) {
                return i + 1;
            }
        }
    }
    return len;
}

size_t count_lines(const char *data, size_t len, char sep) {
    size_t i;
    size_t n = 0;

    if (len == 0) {
        return 0;
    }
    n = 1;
    for (i = 0; i < len; i++) {
        if (data[i] == sep && i + 1 < len) {
            n++;
        }
    }
    return n;
}

size_t text_prefix_len(const char *data, size_t len) {
    size_t i;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            continue;
        }
        if (c < 32 || c == 127) {
            return i;
        }
    }
    return len;
}
