/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "edit_file.h"

#include <bas/io/file.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char *read_file_bytes(const char *path, size_t *lenp, mode_t *modep) {
    struct stat st;
    char *data;
    size_t len;

    if (stat(path, &st) != 0) {
        return NULL;
    }
    if (modep) {
        *modep = st.st_mode & 07777;
    }

    data = load_file(path, &len, 0, 1);
    if (!data) {
        /* Distinguish empty file from error: load_file may return NULL for empty. */
        FILE *f = fopen(path, "rb");
        if (!f) {
            return NULL;
        }
        fclose(f);
        data = malloc(1);
        if (!data) {
            return NULL;
        }
        data[0] = '\0';
        len = 0;
    }
    if (lenp) {
        *lenp = len;
    }
    return data;
}

int buf_append(char **buf, size_t *len, size_t *cap, const void *src, size_t n) {
    if (n == 0) {
        return 0;
    }
    if (*len + n + 1 > *cap) {
        size_t ncap = *cap ? *cap : 64;
        char *nb;

        while (ncap < *len + n + 1) {
            ncap *= 2;
        }
        nb = realloc(*buf, ncap);
        if (!nb) {
            return -1;
        }
        *buf = nb;
        *cap = ncap;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

int buf_append_ch(char **buf, size_t *len, size_t *cap, char ch, size_t n) {
    size_t i;

    for (i = 0; i < n; i++) {
        if (buf_append(buf, len, cap, &ch, 1) != 0) {
            return -1;
        }
    }
    return 0;
}

int write_file_atomic(const char *path, const void *data, size_t len, mode_t mode) {
    char *tmp = NULL;
    size_t path_len;
    int fd = -1;
    FILE *f = NULL;
    int rc = -1;

    path_len = strlen(path);
    tmp = malloc(path_len + 32);
    if (!tmp) {
        return -1;
    }
    snprintf(tmp, path_len + 32, "%s.tmp.%d", path, (int)getpid());

    f = fopen(tmp, "wb");
    if (!f) {
        goto out;
    }
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        goto out;
    }
    if (fflush(f) != 0) {
        goto out;
    }
    fd = fileno(f);
    if (fd >= 0) {
        if (fchmod(fd, mode) != 0) {
            /* ignore if not supported */
        }
        if (fsync(fd) != 0) {
            /* best-effort */
        }
    }
    if (fclose(f) != 0) {
        f = NULL;
        goto out;
    }
    f = NULL;

    if (rename(tmp, path) != 0) {
        goto out;
    }
    rc = 0;

out:
    if (f) {
        fclose(f);
    }
    if (rc != 0 && tmp) {
        unlink(tmp);
    }
    free(tmp);
    return rc;
}

int replace_file_if_changed(const char *path, const void *old, size_t old_len, const void *neu,
                            size_t neu_len, mode_t mode) {
    if (old_len == neu_len && (old_len == 0 || memcmp(old, neu, old_len) == 0)) {
        return 0;
    }
    if (write_file_atomic(path, neu, neu_len, mode) != 0) {
        return -1;
    }
    return 1;
}
