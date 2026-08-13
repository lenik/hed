/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef IGNORELIST_HGIGNORE_H
#define IGNORELIST_HGIGNORE_H

typedef struct hgignore hgignore_t;

hgignore_t *hgignore_open(const char *root);
void hgignore_free(hgignore_t *hg);

int hgignore_match(hgignore_t *hg, const char *path_rel, int is_dir);

#endif /* IGNORELIST_HGIGNORE_H */
