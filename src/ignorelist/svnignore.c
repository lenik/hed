/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Subversion ignores: default global-ignores, ~/.subversion/config,
 * per-directory svn:ignore (via `svn propget -R`), and optional .svnignore.
 */

#define _POSIX_C_SOURCE 200809L

#include "svnignore.h"

#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct svn_dir_ign {
    char *dir_rel;
    char **pats;
    size_t n;
    size_t cap;
    int svnignore_loaded; /* .svnignore consulted for this dir */
} svn_dir_ign_t;

struct svnignore {
    char *root;
    char **global_pats;
    size_t n_global;
    size_t cap_global;
    svn_dir_ign_t *dirs;
    size_t n_dirs;
    size_t cap_dirs;
};

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) {
        memcpy(p, s, n);
    }
    return p;
}

static int ends_with(const char *s, const char *suf) {
    size_t ls = strlen(s), lf = strlen(suf);
    return ls >= lf && strcmp(s + ls - lf, suf) == 0;
}

static int push_str(char ***list, size_t *n, size_t *cap, const char *s) {
    char **nl;
    if (*n >= *cap) {
        size_t ncap = *cap ? *cap * 2 : 8;
        nl = realloc(*list, ncap * sizeof(char *));
        if (!nl) {
            return -1;
        }
        *list = nl;
        *cap = ncap;
    }
    (*list)[*n] = xstrdup(s);
    if (!(*list)[*n]) {
        return -1;
    }
    (*n)++;
    return 0;
}

static int add_global(svnignore_t *si, const char *pat) {
    if (!pat || !*pat) {
        return 0;
    }
    return push_str(&si->global_pats, &si->n_global, &si->cap_global, pat);
}

static svn_dir_ign_t *find_or_add_dir(svnignore_t *si, const char *dir_rel) {
    size_t i;
    svn_dir_ign_t *d;
    for (i = 0; i < si->n_dirs; i++) {
        if (strcmp(si->dirs[i].dir_rel, dir_rel) == 0) {
            return &si->dirs[i];
        }
    }
    if (si->n_dirs >= si->cap_dirs) {
        size_t ncap = si->cap_dirs ? si->cap_dirs * 2 : 4;
        svn_dir_ign_t *nd = realloc(si->dirs, ncap * sizeof(*nd));
        if (!nd) {
            return NULL;
        }
        si->dirs = nd;
        si->cap_dirs = ncap;
    }
    d = &si->dirs[si->n_dirs++];
    memset(d, 0, sizeof(*d));
    d->dir_rel = xstrdup(dir_rel);
    if (!d->dir_rel) {
        si->n_dirs--;
        return NULL;
    }
    return d;
}

static int add_dir_pat(svnignore_t *si, const char *dir_rel, const char *pat) {
    svn_dir_ign_t *d = find_or_add_dir(si, dir_rel);
    if (!d) {
        return -1;
    }
    return push_str(&d->pats, &d->n, &d->cap, pat);
}

static void load_default_globals(svnignore_t *si) {
    static const char *defs[] = {"*.o",
                                 "*.lo",
                                 "*.la",
                                 "*.al",
                                 ".libs",
                                 "*.so",
                                 "*.so.[0-9]*",
                                 "*.a",
                                 "*.pyc",
                                 "*.pyo",
                                 "__pycache__",
                                 "*.rej",
                                 "*~",
                                 "#*#",
                                 ".#*",
                                 ".*.swp",
                                 ".DS_Store",
                                 NULL};
    size_t i;
    for (i = 0; defs[i]; i++) {
        add_global(si, defs[i]);
    }
}

static void load_subversion_config(svnignore_t *si) {
    const char *home = getenv("HOME");
    char path[4096];
    FILE *f;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int in_misc = 0;

    if (!home) {
        return;
    }
    snprintf(path, sizeof path, "%s/.subversion/config", home);
    f = fopen(path, "r");
    if (!f) {
        return;
    }
    while ((n = getline(&line, &cap, f)) != -1) {
        char *p = line;
        if (n > 0 && line[n - 1] == '\n') {
            line[n - 1] = '\0';
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\0') {
            continue;
        }
        if (*p == '[') {
            in_misc = (strncmp(p, "[miscellany]", 12) == 0);
            continue;
        }
        if (!in_misc) {
            continue;
        }
        if (strncmp(p, "global-ignores", 14) == 0) {
            p += 14;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p == '=') {
                size_t i;
                p++;
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                for (i = 0; i < si->n_global; i++) {
                    free(si->global_pats[i]);
                }
                free(si->global_pats);
                si->global_pats = NULL;
                si->n_global = 0;
                si->cap_global = 0;
                while (*p) {
                    char tok[256];
                    size_t t = 0;
                    while (*p == ' ' || *p == '\t') {
                        p++;
                    }
                    while (*p && *p != ' ' && *p != '\t' && t + 1 < sizeof tok) {
                        tok[t++] = *p++;
                    }
                    tok[t] = '\0';
                    if (t) {
                        add_global(si, tok);
                    }
                }
            }
        }
    }
    free(line);
    fclose(f);
}

static void ensure_svnignore_file(svnignore_t *si, const char *dir_rel) {
    svn_dir_ign_t *d;
    char path[4096];
    FILE *f;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;

    d = find_or_add_dir(si, dir_rel);
    if (!d || d->svnignore_loaded) {
        return;
    }
    d->svnignore_loaded = 1;

    if (dir_rel[0] == '\0') {
        snprintf(path, sizeof path, "%s/.svnignore", si->root);
    } else {
        snprintf(path, sizeof path, "%s/%s/.svnignore", si->root, dir_rel);
    }
    f = fopen(path, "r");
    if (!f) {
        return;
    }
    while ((n = getline(&line, &cap, f)) != -1) {
        char *p = line;
        if (n > 0 && line[n - 1] == '\n') {
            line[n - 1] = '\0';
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0' || *p == '#') {
            continue;
        }
        push_str(&d->pats, &d->n, &d->cap, p);
    }
    free(line);
    fclose(f);
}

static void load_svn_propget(svnignore_t *si) {
    char cmd[4096];
    FILE *fp;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    char cur_rel[4096];
    int have = 0;
    size_t rootlen;

    snprintf(cmd, sizeof cmd, "svn propget -R svn:ignore -- \"%s\" 2>/dev/null", si->root);
    fp = popen(cmd, "r");
    if (!fp) {
        return;
    }
    cur_rel[0] = '\0';
    rootlen = strlen(si->root);
    while ((n = getline(&line, &cap, fp)) != -1) {
        char *p = line;
        char *dash;
        if (n > 0 && line[n - 1] == '\n') {
            line[n - 1] = '\0';
        }
        if (line[0] == '\0') {
            continue;
        }
        dash = strstr(p, " - ");
        if (dash && dash > p) {
            char pathbuf[4096];
            size_t plen = (size_t)(dash - p);
            int looks_like_path = 0;

            if (plen >= sizeof pathbuf) {
                continue;
            }
            memcpy(pathbuf, p, plen);
            pathbuf[plen] = '\0';
            while (plen > 0 && (pathbuf[plen - 1] == ' ' || pathbuf[plen - 1] == '\t')) {
                pathbuf[--plen] = '\0';
            }
            if (pathbuf[0] == '/' || strcmp(pathbuf, ".") == 0 ||
                strncmp(pathbuf, si->root, rootlen) == 0) {
                looks_like_path = 1;
            }
            if (looks_like_path) {
                if (pathbuf[0] == '/' && strncmp(pathbuf, si->root, rootlen) == 0) {
                    const char *rest = pathbuf + rootlen;
                    if (*rest == '/') {
                        rest++;
                    }
                    snprintf(cur_rel, sizeof cur_rel, "%s", rest);
                } else if (strcmp(pathbuf, ".") == 0) {
                    cur_rel[0] = '\0';
                } else {
                    snprintf(cur_rel, sizeof cur_rel, "%s", pathbuf);
                }
                have = 1;
                continue;
            }
        }
        if (have) {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p) {
                add_dir_pat(si, cur_rel, p);
            }
        }
    }
    free(line);
    pclose(fp);
}

svnignore_t *svnignore_open(const char *root) {
    svnignore_t *si = calloc(1, sizeof(*si));
    if (!si) {
        return NULL;
    }
    si->root = xstrdup(root);
    if (!si->root) {
        free(si);
        return NULL;
    }
    {
        size_t n = strlen(si->root);
        while (n > 1 && si->root[n - 1] == '/') {
            si->root[--n] = '\0';
        }
    }
    load_default_globals(si);
    load_subversion_config(si);
    load_svn_propget(si);
    return si;
}

void svnignore_free(svnignore_t *si) {
    size_t i, j;
    if (!si) {
        return;
    }
    for (i = 0; i < si->n_global; i++) {
        free(si->global_pats[i]);
    }
    free(si->global_pats);
    for (i = 0; i < si->n_dirs; i++) {
        for (j = 0; j < si->dirs[i].n; j++) {
            free(si->dirs[i].pats[j]);
        }
        free(si->dirs[i].pats);
        free(si->dirs[i].dir_rel);
    }
    free(si->dirs);
    free(si->root);
    free(si);
}

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int match_basenames(char **pats, size_t n, const char *base) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (fnmatch(pats[i], base, 0) == 0) {
            return 1;
        }
    }
    return 0;
}

int svnignore_match(svnignore_t *si, const char *path_rel, int is_dir) {
    const char *base;
    char dir[4096];
    const char *slash;
    size_t i;

    (void)is_dir;
    if (!si || !path_rel) {
        return 0;
    }
    if (strcmp(path_rel, ".svn") == 0 || strstr(path_rel, "/.svn/") != NULL ||
        ends_with(path_rel, "/.svn")) {
        return 1;
    }

    base = basename_of(path_rel);
    if (match_basenames(si->global_pats, si->n_global, base)) {
        return 1;
    }

    slash = strrchr(path_rel, '/');
    if (slash) {
        size_t dlen = (size_t)(slash - path_rel);
        if (dlen >= sizeof dir) {
            return 0;
        }
        memcpy(dir, path_rel, dlen);
        dir[dlen] = '\0';
    } else {
        dir[0] = '\0';
    }

    ensure_svnignore_file(si, dir);

    for (i = 0; i < si->n_dirs; i++) {
        if (strcmp(si->dirs[i].dir_rel, dir) == 0) {
            if (match_basenames(si->dirs[i].pats, si->dirs[i].n, base)) {
                return 1;
            }
        }
    }
    return 0;
}
