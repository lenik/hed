/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Minimal git-compatible wildmatch (supports *, ?, [], **, escapes).
 */

#define _POSIX_C_SOURCE 200809L

#include "wildmatch.h"

#include <string.h>

static int match_bracket(const char **ppat, int ch) {
    const char *p = *ppat;
    int invert = 0;
    int matched = 0;

    if (*p == '!' || *p == '^') {
        invert = 1;
        p++;
    }
    if (*p == ']') {
        if (ch == ']') {
            matched = 1;
        }
        p++;
    }
    while (*p && *p != ']') {
        int a = (unsigned char)*p++;
        int b = a;
        if (*p == '-' && p[1] && p[1] != ']') {
            p++;
            b = (unsigned char)*p++;
            if (a > b) {
                int t = a;
                a = b;
                b = t;
            }
            if (ch >= a && ch <= b) {
                matched = 1;
            }
        } else if (ch == a) {
            matched = 1;
        }
    }
    if (*p == ']') {
        p++;
    }
    *ppat = p;
    return invert ? !matched : matched;
}

static int wm_rec(const char *pat, const char *text, int flags);

/* pat is the remainder after a "**" token (optional leading '/'). */
static int wm_starstar(const char *pat, const char *text, int flags) {
    if (*pat == '/') {
        pat++;
    }
    if (*pat == '\0') {
        return 1;
    }
    for (;;) {
        if (wm_rec(pat, text, flags)) {
            return 1;
        }
        if (*text == '\0') {
            return 0;
        }
        while (*text && *text != '/') {
            text++;
        }
        if (*text == '/') {
            text++;
        } else {
            return 0;
        }
    }
}

static int is_doublestar(const char *pat) {
    return pat[0] == '*' && pat[1] == '*' && (pat[2] == '/' || pat[2] == '\0');
}

static int wm_rec(const char *pat, const char *text, int flags) {
    for (;;) {
        char pc = *pat;
        char tc = *text;

        if (pc == '\0') {
            return tc == '\0';
        }

        if (is_doublestar(pat)) {
            return wm_starstar(pat + 2, text, flags);
        }

        if (pc == '*') {
            pat++;
            if (!(flags & WM_PATHNAME)) {
                while (*text) {
                    if (wm_rec(pat, text, flags)) {
                        return 1;
                    }
                    text++;
                }
                return wm_rec(pat, text, flags);
            }
            while (*text && *text != '/') {
                if (wm_rec(pat, text, flags)) {
                    return 1;
                }
                text++;
            }
            return wm_rec(pat, text, flags);
        }

        if (pc == '?') {
            if (tc == '\0') {
                return 0;
            }
            if ((flags & WM_PATHNAME) && tc == '/') {
                return 0;
            }
            pat++;
            text++;
            continue;
        }

        if (pc == '[') {
            if (tc == '\0') {
                return 0;
            }
            if ((flags & WM_PATHNAME) && tc == '/') {
                return 0;
            }
            pat++;
            if (!match_bracket(&pat, (unsigned char)tc)) {
                return 0;
            }
            text++;
            continue;
        }

        if (pc == '\\' && pat[1]) {
            pat++;
            pc = *pat;
        }

        if (tc == '\0' || tc != pc) {
            return 0;
        }
        pat++;
        text++;
    }
}

int ignorelist_wildmatch(const char *pattern, const char *text, int flags) {
    if (!pattern || !text) {
        return 0;
    }
    return wm_rec(pattern, text, flags);
}
