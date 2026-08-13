/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Git-style wildmatch used by .gitignore patterns.
 */

#ifndef IGNORELIST_WILDMATCH_H
#define IGNORELIST_WILDMATCH_H

enum {
    WM_PATHNAME = 1, /* * and ? do not match '/' */
    WM_PERIOD = 2    /* leading '.' must be matched explicitly (unused) */
};

/* Returns 1 on match, 0 otherwise. */
int ignorelist_wildmatch(const char *pattern, const char *text, int flags);

#endif /* IGNORELIST_WILDMATCH_H */
