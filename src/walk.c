/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "walk.h"

#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int match_any(const char *name, const char **globs, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (fnmatch(globs[i], name, 0) == 0) {
            return 1;
        }
    }
    return 0;
}

static int basename_of(const char *path, char *buf, size_t buflen) {
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    if (strlen(base) >= buflen) {
        return -1;
    }
    strcpy(buf, base);
    return 0;
}

static int should_skip_file(const char *path, const walk_opts_t *opts) {
    char base[512];

    if (basename_of(path, base, sizeof base) != 0) {
        return 0;
    }
    if (opts->n_exclude && match_any(base, opts->exclude, opts->n_exclude)) {
        return 1;
    }
    if (opts->n_include > 0 && !match_any(base, opts->include, opts->n_include)) {
        return 1;
    }
    return 0;
}

static int walk_dir(const char *path, int depth, int cmdline, const walk_opts_t *opts,
                    walk_file_fn fn, void *userdata);

int walk_path(const char *path, int cmdline, const walk_opts_t *opts, walk_file_fn fn,
              void *userdata) {
    struct stat st;
    int follow = 0;

    if (opts->dereference) {
        follow = 1;
    } else if (cmdline && opts->recursive) {
        /* -r: follow symlinks only if on command line */
        follow = 1;
    }

    if (follow) {
        if (stat(path, &st) != 0) {
            return -1;
        }
    } else {
        if (lstat(path, &st) != 0) {
            return -1;
        }
    }

    if (S_ISDIR(st.st_mode)) {
        char base[512];
        if (basename_of(path, base, sizeof base) == 0 && opts->n_exclude_dir &&
            match_any(base, opts->exclude_dir, opts->n_exclude_dir)) {
            return 0;
        }
        if (opts->dir_action == 1) {
            return 0; /* skip */
        }
        if (opts->dir_action == 2 || opts->recursive || opts->dereference) {
            return walk_dir(path, 0, cmdline, opts, fn, userdata);
        }
        /* treat as ordinary file — unusual; still try callback */
        return fn(path, userdata);
    }

    if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
        if (should_skip_file(path, opts)) {
            return 0;
        }
        return fn(path, userdata);
    }

    /* device / fifo / socket */
    if (opts->device_action == 1) {
        return 0;
    }
    if (should_skip_file(path, opts)) {
        return 0;
    }
    return fn(path, userdata);
}

static int walk_dir(const char *path, int depth, int cmdline, const walk_opts_t *opts,
                    walk_file_fn fn, void *userdata) {
    DIR *d;
    struct dirent *ent;
    int rc = 0;

    (void)cmdline;
    if (opts->max_depth >= 0 && depth > opts->max_depth) {
        return 0;
    }

    d = opendir(path);
    if (!d) {
        return -1;
    }

    while ((ent = readdir(d)) != NULL) {
        char *child;
        size_t len;
        struct stat st;
        int follow = opts->dereference;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (opts->n_exclude_dir && match_any(ent->d_name, opts->exclude_dir, opts->n_exclude_dir)) {
            /* skip this name if it is a directory — check after stat */
        }

        len = strlen(path) + 1 + strlen(ent->d_name) + 1;
        child = malloc(len);
        if (!child) {
            rc = -1;
            break;
        }
        snprintf(child, len, "%s/%s", path, ent->d_name);

        if (follow) {
            if (stat(child, &st) != 0) {
                free(child);
                continue;
            }
        } else {
            if (lstat(child, &st) != 0) {
                free(child);
                continue;
            }
        }

        if (S_ISDIR(st.st_mode)) {
            if (opts->n_exclude_dir &&
                match_any(ent->d_name, opts->exclude_dir, opts->n_exclude_dir)) {
                free(child);
                continue;
            }
            if (opts->max_depth >= 0 && depth + 1 > opts->max_depth) {
                free(child);
                continue;
            }
            rc = walk_dir(child, depth + 1, 0, opts, fn, userdata);
        } else {
            if (!should_skip_file(child, opts)) {
                rc = fn(child, userdata);
            }
        }
        free(child);
        if (rc != 0) {
            break;
        }
    }
    closedir(d);
    return rc;
}

int walk_load_exclude_from(const char *file, char ***list, size_t *n) {
    FILE *f;
    char *line = NULL;
    size_t cap = 0;
    ssize_t nr;

    if (strcmp(file, "-") == 0) {
        f = stdin;
    } else {
        f = fopen(file, "r");
        if (!f) {
            return -1;
        }
    }

    while ((nr = getline(&line, &cap, f)) != -1) {
        char **nl;
        if (nr > 0 && line[nr - 1] == '\n') {
            line[nr - 1] = '\0';
            nr--;
        }
        if (nr == 0) {
            continue;
        }
        nl = realloc(*list, (*n + 1) * sizeof(char *));
        if (!nl) {
            free(line);
            if (f != stdin) {
                fclose(f);
            }
            return -1;
        }
        *list = nl;
        (*list)[*n] = strdup(line);
        if (!(*list)[*n]) {
            free(line);
            if (f != stdin) {
                fclose(f);
            }
            return -1;
        }
        (*n)++;
    }
    free(line);
    if (f != stdin) {
        fclose(f);
    }
    return 0;
}
