/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef IGNORELIST_H
#define IGNORELIST_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VCS_NONE = 0,
    VCS_GIT,
    VCS_HG,
    VCS_SVN
} vcs_type_t;

typedef struct ignorelist ignorelist_t;

/*
 * Discover a VCS working tree by walking up from start_path (file or dir).
 * The first marker found (.git, .hg, or .svn) selects the type.
 * Returns NULL if no VCS root is found or on allocation failure.
 */
ignorelist_t *ignorelist_open(const char *start_path);

void ignorelist_free(ignorelist_t *il);

vcs_type_t ignorelist_vcs(const ignorelist_t *il);

/* Absolute path of the working tree root, or NULL. */
const char *ignorelist_root(const ignorelist_t *il);

/*
 * Returns 1 if path should be ignored, 0 otherwise.
 * path may be absolute or relative; is_dir is non-zero for directories.
 * Always ignores the VCS metadata directory (.git / .hg / .svn).
 */
int ignorelist_match(ignorelist_t *il, const char *path, int is_dir);

/*
 * True if name is a VCS metadata directory that walkers should skip
 * regardless of ignorelist options (.git, .hg, .svn).
 */
int ignorelist_is_vcs_dir(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* IGNORELIST_H */
