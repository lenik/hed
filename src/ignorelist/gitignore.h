/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef IGNORELIST_GITIGNORE_H
#define IGNORELIST_GITIGNORE_H

typedef struct gitignore gitignore_t;

gitignore_t *gitignore_open(const char *root);
void gitignore_free(gitignore_t *gi);

/* path_rel: path relative to worktree root, no leading slash.
 * is_dir: non-zero if path names a directory. */
int gitignore_match(gitignore_t *gi, const char *path_rel, int is_dir);

#endif /* IGNORELIST_GITIGNORE_H */
