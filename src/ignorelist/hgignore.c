/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mercurial .hgignore: syntax: glob | regexp, comments, negation (!).
 */

#define _POSIX_C_SOURCE 200809L

#define PCRE2_CODE_UNIT_WIDTH 8

#include "hgignore.h"
#include "wildmatch.h"

#include <pcre2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HG_GLOB = 0, HG_REGEXP = 1 };

enum { HP_NEGATE = 1 };

typedef struct hg_pattern {
    unsigned flags;
    int syntax; /* HG_GLOB or HG_REGEXP at time of parse */
    char *pat;
    pcre2_code *re; /* for regexp */
} hg_pattern_t;

struct hgignore {
    char *root;
    hg_pattern_t *pats;
    size_t n;
    size_t cap;
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

static int push_pat(hgignore_t *hg, int syntax, unsigned flags, const char *pat) {
    hg_pattern_t *np;
    if (hg->n >= hg->cap) {
        size_t ncap = hg->cap ? hg->cap * 2 : 8;
        np = realloc(hg->pats, ncap * sizeof(*np));
        if (!np) {
            return -1;
        }
        hg->pats = np;
        hg->cap = ncap;
    }
    memset(&hg->pats[hg->n], 0, sizeof(hg->pats[hg->n]));
    hg->pats[hg->n].flags = flags;
    hg->pats[hg->n].syntax = syntax;
    hg->pats[hg->n].pat = xstrdup(pat);
    if (!hg->pats[hg->n].pat) {
        return -1;
    }
    if (syntax == HG_REGEXP) {
        int err = 0;
        PCRE2_SIZE erroff = 0;
        /* Mercurial matches against the full path from repo root */
        hg->pats[hg->n].re =
            pcre2_compile((PCRE2_SPTR)pat, PCRE2_ZERO_TERMINATED, PCRE2_UTF, &err, &erroff, NULL);
        if (!hg->pats[hg->n].re) {
            /* skip invalid regexp */
            free(hg->pats[hg->n].pat);
            return 0;
        }
    }
    hg->n++;
    return 0;
}

static int load_hgignore(hgignore_t *hg, const char *path) {
    FILE *f;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int syntax = HG_REGEXP; /* Mercurial default */

    f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    while ((n = getline(&line, &cap, f)) != -1) {
        char *p = line;
        unsigned flags = 0;
        if (n > 0 && line[n - 1] == '\n') {
            line[n - 1] = '\0';
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0' || *p == '#') {
            continue;
        }
        if (strncmp(p, "syntax:", 7) == 0) {
            p += 7;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (strcmp(p, "glob") == 0) {
                syntax = HG_GLOB;
            } else if (strcmp(p, "regexp") == 0 || strcmp(p, "re") == 0) {
                syntax = HG_REGEXP;
            }
            continue;
        }
        if (*p == '!') {
            flags |= HP_NEGATE;
            p++;
        }
        if (*p == '\0') {
            continue;
        }
        if (push_pat(hg, syntax, flags, p) != 0) {
            free(line);
            fclose(f);
            return -1;
        }
    }
    free(line);
    fclose(f);
    return 0;
}

hgignore_t *hgignore_open(const char *root) {
    hgignore_t *hg;
    char path[4096];

    hg = calloc(1, sizeof(*hg));
    if (!hg) {
        return NULL;
    }
    hg->root = xstrdup(root);
    if (!hg->root) {
        free(hg);
        return NULL;
    }
    {
        size_t n = strlen(hg->root);
        while (n > 1 && hg->root[n - 1] == '/') {
            hg->root[--n] = '\0';
        }
    }
    snprintf(path, sizeof path, "%s/.hgignore", hg->root);
    if (load_hgignore(hg, path) != 0) {
        hgignore_free(hg);
        return NULL;
    }
    return hg;
}

void hgignore_free(hgignore_t *hg) {
    size_t i;
    if (!hg) {
        return;
    }
    for (i = 0; i < hg->n; i++) {
        free(hg->pats[i].pat);
        if (hg->pats[i].re) {
            pcre2_code_free(hg->pats[i].re);
        }
    }
    free(hg->pats);
    free(hg->root);
    free(hg);
}

int hgignore_match(hgignore_t *hg, const char *path_rel, int is_dir) {
    int ignored = 0;
    size_t i;
    pcre2_match_data *md = NULL;

    (void)is_dir;
    if (!hg || !path_rel) {
        return 0;
    }
    if (strcmp(path_rel, ".hg") == 0 || strstr(path_rel, "/.hg/") != NULL ||
        ends_with(path_rel, "/.hg")) {
        return 1;
    }

    for (i = 0; i < hg->n; i++) {
        int hit = 0;
        hg_pattern_t *hp = &hg->pats[i];
        if (hp->syntax == HG_GLOB) {
            /* Mercurial glob: match against full path; * does not cross '/'
             * but unanchored-style: also try basename-like via wildmatch on
             * full path. Hg globs are relative to repo root. */
            if (ignorelist_wildmatch(hp->pat, path_rel, WM_PATHNAME)) {
                hit = 1;
            } else {
                /* also allow matching a trailing segment like *.o */
                const char *slash = path_rel;
                while ((slash = strchr(slash, '/')) != NULL) {
                    slash++;
                    if (ignorelist_wildmatch(hp->pat, slash, WM_PATHNAME)) {
                        hit = 1;
                        break;
                    }
                }
            }
        } else if (hp->re) {
            if (!md) {
                md = pcre2_match_data_create_from_pattern(hp->re, NULL);
                if (!md) {
                    continue;
                }
            }
            if (pcre2_match(hp->re, (PCRE2_SPTR)path_rel, PCRE2_ZERO_TERMINATED, 0, 0, md, NULL) >=
                0) {
                hit = 1;
            }
        }
        if (hit) {
            ignored = !(hp->flags & HP_NEGATE);
        }
    }
    if (md) {
        pcre2_match_data_free(md);
    }
    return ignored;
}
