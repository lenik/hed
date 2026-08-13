/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef IGNORELIST_SVNIGNORE_H
#define IGNORELIST_SVNIGNORE_H

typedef struct svnignore svnignore_t;

svnignore_t *svnignore_open(const char *root);
void svnignore_free(svnignore_t *si);

int svnignore_match(svnignore_t *si, const char *path_rel, int is_dir);

#endif /* IGNORELIST_SVNIGNORE_H */
