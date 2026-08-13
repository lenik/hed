#ifndef WALK_H
#define WALK_H

#include "ignorelist/ignorelist.h"

#include <stddef.h>
#include <stdio.h>

typedef struct walk_opts {
    int recursive;          /* -r */
    int dereference;        /* -R: follow symlinks always */
    int max_depth;          /* -1 = unlimited */
    int dir_action;         /* 0=repl, 1=skip, 2=recurse */
    int device_action;      /* 0=repl, 1=skip */
    const char **exclude;   /* globs */
    size_t n_exclude;
    const char **exclude_dir;
    size_t n_exclude_dir;
    const char **include; /* if n_include>0, must match one */
    size_t n_include;
    ignorelist_t *ignorelist; /* NULL = do not apply VCS ignore lists */
} walk_opts_t;

typedef int (*walk_file_fn)(const char *path, void *userdata);

/*
 * Visit path. If directory and recurse, walk children.
 * cmdline=1 means path was given on the command line.
 * With -r (recursive, not -R): follow a cmdline symlink once; do not follow
 * symlinks discovered while walking.
 * Returns 0, or first non-zero from callback.
 */
int walk_path(const char *path, int cmdline, const walk_opts_t *opts, walk_file_fn fn,
              void *userdata);

/*
 * Recursively list paths under dir that are ignored by opts->ignorelist.
 * Prints one path per line to out. Returns 0 on success.
 * Requires opts->ignorelist != NULL.
 */
int walk_list_ignored(const char *dir, const walk_opts_t *opts, FILE *out);

/* Load exclude patterns from file (one per line). Appends to *list / *n. */
int walk_load_exclude_from(const char *file, char ***list, size_t *n);

#endif /* WALK_H */
