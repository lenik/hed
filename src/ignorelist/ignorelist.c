/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "ignorelist.h"

#include "gitignore.h"
#include "hgignore.h"
#include "svnignore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct ignorelist {
    vcs_type_t type;
    char *root;
    gitignore_t *git;
    hgignore_t *hg;
    svnignore_t *svn;
};

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) {
        memcpy(p, s, n);
    }
    return p;
}

int ignorelist_is_vcs_dir(const char *name) {
    return name && (strcmp(name, ".git") == 0 || strcmp(name, ".hg") == 0 ||
                    strcmp(name, ".svn") == 0);
}

/* Resolve start_path to an absolute existing directory (or parent of file). */
static char *resolve_start_dir(const char *start_path) {
    char buf[4096];
    char resolved[4096];
    struct stat st;
    char *slash;

    if (!start_path || !*start_path) {
        start_path = ".";
    }
    if (start_path[0] == '/') {
        snprintf(buf, sizeof buf, "%s", start_path);
    } else {
        char cwd[4096];
        if (!getcwd(cwd, sizeof cwd)) {
            return NULL;
        }
        snprintf(buf, sizeof buf, "%s/%s", cwd, start_path);
    }

    /* If it does not exist yet, still use dirname */
    if (lstat(buf, &st) != 0) {
        slash = strrchr(buf, '/');
        if (slash && slash != buf) {
            *slash = '\0';
        } else if (slash == buf) {
            buf[1] = '\0';
        }
    } else if (!S_ISDIR(st.st_mode)) {
        slash = strrchr(buf, '/');
        if (slash && slash != buf) {
            *slash = '\0';
        } else if (slash == buf) {
            buf[1] = '\0';
        }
    }

    if (!realpath(buf, resolved)) {
        return xstrdup(buf);
    }
    return xstrdup(resolved);
}

static int marker_exists(const char *dir, const char *name) {
    char path[4096];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return lstat(path, &st) == 0;
}

static int find_vcs_root(const char *start_dir, vcs_type_t *out_type, char **out_root) {
    char cur[4096];
    snprintf(cur, sizeof cur, "%s", start_dir);

    for (;;) {
        if (marker_exists(cur, ".git")) {
            *out_type = VCS_GIT;
            *out_root = xstrdup(cur);
            return *out_root ? 0 : -1;
        }
        if (marker_exists(cur, ".hg")) {
            *out_type = VCS_HG;
            *out_root = xstrdup(cur);
            return *out_root ? 0 : -1;
        }
        if (marker_exists(cur, ".svn")) {
            *out_type = VCS_SVN;
            *out_root = xstrdup(cur);
            return *out_root ? 0 : -1;
        }
        {
            char *slash = strrchr(cur, '/');
            if (!slash) {
                break;
            }
            if (slash == cur) {
                /* at filesystem root "/" */
                if (marker_exists("/", ".git")) {
                    *out_type = VCS_GIT;
                    *out_root = xstrdup("/");
                    return *out_root ? 0 : -1;
                }
                if (marker_exists("/", ".hg")) {
                    *out_type = VCS_HG;
                    *out_root = xstrdup("/");
                    return *out_root ? 0 : -1;
                }
                if (marker_exists("/", ".svn")) {
                    *out_type = VCS_SVN;
                    *out_root = xstrdup("/");
                    return *out_root ? 0 : -1;
                }
                break;
            }
            *slash = '\0';
        }
    }
    return -1;
}

ignorelist_t *ignorelist_open(const char *start_path) {
    ignorelist_t *il;
    char *start_dir;
    vcs_type_t type = VCS_NONE;
    char *root = NULL;

    start_dir = resolve_start_dir(start_path);
    if (!start_dir) {
        return NULL;
    }
    if (find_vcs_root(start_dir, &type, &root) != 0) {
        free(start_dir);
        return NULL;
    }
    free(start_dir);

    il = calloc(1, sizeof(*il));
    if (!il) {
        free(root);
        return NULL;
    }
    il->type = type;
    il->root = root;

    switch (type) {
    case VCS_GIT:
        il->git = gitignore_open(root);
        if (!il->git) {
            ignorelist_free(il);
            return NULL;
        }
        break;
    case VCS_HG:
        il->hg = hgignore_open(root);
        if (!il->hg) {
            ignorelist_free(il);
            return NULL;
        }
        break;
    case VCS_SVN:
        il->svn = svnignore_open(root);
        if (!il->svn) {
            ignorelist_free(il);
            return NULL;
        }
        break;
    default:
        ignorelist_free(il);
        return NULL;
    }
    return il;
}

void ignorelist_free(ignorelist_t *il) {
    if (!il) {
        return;
    }
    gitignore_free(il->git);
    hgignore_free(il->hg);
    svnignore_free(il->svn);
    free(il->root);
    free(il);
}

vcs_type_t ignorelist_vcs(const ignorelist_t *il) {
    return il ? il->type : VCS_NONE;
}

const char *ignorelist_root(const ignorelist_t *il) {
    return il ? il->root : NULL;
}

static char *path_relative_to_root(const char *root, const char *path) {
    char abs[4096];
    char resolved[4096];
    size_t rootlen;
    const char *use;

    if (!root || !path) {
        return NULL;
    }
    if (path[0] == '/') {
        snprintf(abs, sizeof abs, "%s", path);
    } else {
        char cwd[4096];
        if (!getcwd(cwd, sizeof cwd)) {
            return NULL;
        }
        snprintf(abs, sizeof abs, "%s/%s", cwd, path);
    }
    if (realpath(abs, resolved)) {
        use = resolved;
    } else {
        use = abs;
    }

    rootlen = strlen(root);
    if (strcmp(root, "/") == 0) {
        if (use[0] == '/') {
            return xstrdup(use + 1);
        }
        return xstrdup(use);
    }
    if (strncmp(use, root, rootlen) != 0) {
        /* outside worktree — not ignored by this list */
        return NULL;
    }
    if (use[rootlen] == '\0') {
        return xstrdup("");
    }
    if (use[rootlen] != '/') {
        return NULL;
    }
    return xstrdup(use + rootlen + 1);
}

int ignorelist_match(ignorelist_t *il, const char *path, int is_dir) {
    char *rel;
    int rc = 0;

    if (!il || !path) {
        return 0;
    }

    /* Fast path: VCS metadata directory name */
    {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (ignorelist_is_vcs_dir(base)) {
            return 1;
        }
    }

    rel = path_relative_to_root(il->root, path);
    if (!rel) {
        return 0;
    }
    if (rel[0] == '\0') {
        free(rel);
        return 0;
    }

    switch (il->type) {
    case VCS_GIT:
        rc = gitignore_match(il->git, rel, is_dir);
        break;
    case VCS_HG:
        rc = hgignore_match(il->hg, rel, is_dir);
        break;
    case VCS_SVN:
        rc = svnignore_match(il->svn, rel, is_dir);
        break;
    default:
        break;
    }
    free(rel);
    return rc;
}
