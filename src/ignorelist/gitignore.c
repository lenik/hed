/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Full .gitignore semantics: nested files, negation, doublestar, info/exclude,
 * and core.excludesFile / XDG fallback.
 */

#define _POSIX_C_SOURCE 200809L

#include "gitignore.h"
#include "wildmatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum {
    GP_NEGATE = 1,
    GP_DIRONLY = 2,
    GP_ANCHORED = 4 /* pattern contains a non-trailing slash => rooted */
};

typedef struct git_pattern {
    char *pat;
    unsigned flags;
} git_pattern_t;

typedef struct git_file {
    char *base; /* directory of this file relative to root; "" for root/global */
    git_pattern_t *pats;
    size_t n;
    size_t cap;
} git_file_t;

struct gitignore {
    char *root; /* absolute, no trailing slash (except "/") */
    git_file_t *files;
    size_t n_files;
    size_t cap_files;
    char **loaded; /* relative dirs already loaded for .gitignore */
    size_t n_loaded;
    size_t cap_loaded;
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

static void trim_trailing_spaces(char *s, size_t *len) {
    while (*len > 0) {
        char c = s[*len - 1];
        if (c != ' ' && c != '\t') {
            break;
        }
        /* escaped trailing space? count backslashes */
        {
            size_t bs = 0;
            size_t i = *len - 1;
            while (i > 0 && s[i - 1] == '\\') {
                bs++;
                i--;
            }
            if (bs % 2 == 1) {
                break; /* odd backslashes => space is escaped */
            }
        }
        (*len)--;
        s[*len] = '\0';
    }
}

static int file_push_pattern(git_file_t *gf, const char *line) {
    git_pattern_t *np;
    char *buf;
    size_t len;
    unsigned flags = 0;

    if (!line) {
        return 0;
    }
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0' || *line == '#') {
        return 0;
    }

    buf = xstrdup(line);
    if (!buf) {
        return -1;
    }
    len = strlen(buf);
    trim_trailing_spaces(buf, &len);

    if (buf[0] == '!') {
        flags |= GP_NEGATE;
        memmove(buf, buf + 1, len);
        len--;
    } else if (buf[0] == '\\' && (buf[1] == '!' || buf[1] == '#')) {
        memmove(buf, buf + 1, len);
        len--;
    }
    if (len == 0) {
        free(buf);
        return 0;
    }
    if (buf[len - 1] == '/') {
        flags |= GP_DIRONLY;
        buf[--len] = '\0';
    }
    if (len == 0) {
        free(buf);
        return 0;
    }
    if (buf[0] == '/') {
        flags |= GP_ANCHORED;
        memmove(buf, buf + 1, len);
        len--;
    } else if (strchr(buf, '/') != NULL) {
        flags |= GP_ANCHORED;
    }
    if (len == 0) {
        free(buf);
        return 0;
    }

    if (gf->n >= gf->cap) {
        size_t ncap = gf->cap ? gf->cap * 2 : 8;
        np = realloc(gf->pats, ncap * sizeof(*np));
        if (!np) {
            free(buf);
            return -1;
        }
        gf->pats = np;
        gf->cap = ncap;
    }
    gf->pats[gf->n].pat = buf;
    gf->pats[gf->n].flags = flags;
    gf->n++;
    return 0;
}

static int load_patterns_from_file(git_file_t *gf, const char *path) {
    FILE *f;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;

    f = fopen(path, "r");
    if (!f) {
        return 0; /* missing is ok */
    }
    while ((n = getline(&line, &cap, f)) != -1) {
        if (n > 0 && line[n - 1] == '\n') {
            line[n - 1] = '\0';
        }
        if (file_push_pattern(gf, line) != 0) {
            free(line);
            fclose(f);
            return -1;
        }
    }
    free(line);
    fclose(f);
    return 0;
}

static git_file_t *add_file(gitignore_t *gi, const char *base) {
    git_file_t *gf;
    if (gi->n_files >= gi->cap_files) {
        size_t ncap = gi->cap_files ? gi->cap_files * 2 : 4;
        git_file_t *nf = realloc(gi->files, ncap * sizeof(*nf));
        if (!nf) {
            return NULL;
        }
        gi->files = nf;
        gi->cap_files = ncap;
    }
    gf = &gi->files[gi->n_files++];
    memset(gf, 0, sizeof(*gf));
    gf->base = xstrdup(base ? base : "");
    if (!gf->base) {
        gi->n_files--;
        return NULL;
    }
    return gf;
}

static int already_loaded(gitignore_t *gi, const char *rel_dir) {
    size_t i;
    for (i = 0; i < gi->n_loaded; i++) {
        if (strcmp(gi->loaded[i], rel_dir) == 0) {
            return 1;
        }
    }
    return 0;
}

static int mark_loaded(gitignore_t *gi, const char *rel_dir) {
    char **nl;
    if (gi->n_loaded >= gi->cap_loaded) {
        size_t ncap = gi->cap_loaded ? gi->cap_loaded * 2 : 8;
        nl = realloc(gi->loaded, ncap * sizeof(char *));
        if (!nl) {
            return -1;
        }
        gi->loaded = nl;
        gi->cap_loaded = ncap;
    }
    gi->loaded[gi->n_loaded] = xstrdup(rel_dir);
    if (!gi->loaded[gi->n_loaded]) {
        return -1;
    }
    gi->n_loaded++;
    return 0;
}

static int ensure_dir_gitignore(gitignore_t *gi, const char *rel_dir) {
    char path[4096];
    git_file_t *gf;
    int n;

    if (already_loaded(gi, rel_dir)) {
        return 0;
    }
    if (mark_loaded(gi, rel_dir) != 0) {
        return -1;
    }

    if (rel_dir[0] == '\0') {
        n = snprintf(path, sizeof path, "%s/.gitignore", gi->root);
    } else {
        n = snprintf(path, sizeof path, "%s/%s/.gitignore", gi->root, rel_dir);
    }
    if (n < 0 || (size_t)n >= sizeof path) {
        return -1;
    }

    {
        struct stat st;
        if (stat(path, &st) != 0) {
            return 0;
        }
    }

    gf = add_file(gi, rel_dir);
    if (!gf) {
        return -1;
    }
    return load_patterns_from_file(gf, path);
}

static int match_one_pattern(const git_pattern_t *gp, const char *base, const char *path_rel,
                             int is_dir) {
    const char *rel; /* path relative to the .gitignore directory */
    char *tmp = NULL;
    int matched = 0;
    size_t blen = strlen(base);

    if (gp->flags & GP_DIRONLY) {
        if (!is_dir) {
            return 0;
        }
    }

    if (blen == 0) {
        rel = path_rel;
    } else {
        if (strncmp(path_rel, base, blen) != 0) {
            return 0;
        }
        if (path_rel[blen] == '\0') {
            rel = "";
        } else if (path_rel[blen] == '/') {
            rel = path_rel + blen + 1;
        } else {
            return 0;
        }
    }

    if (gp->flags & GP_ANCHORED) {
        matched = ignorelist_wildmatch(gp->pat, rel, WM_PATHNAME);
        /* also: pattern "foo/bar" should match path "foo/bar" and, for dironly
         * descendants, git treats a match on a directory as ignoring contents.
         * Matching the exact relative path is enough here; parent-dir checks
         * happen in gitignore_match. */
    } else {
        /* unanchored: match basename anywhere under this base */
        const char *slash;
        /* Match against full rel and against each suffix after '/' */
        if (ignorelist_wildmatch(gp->pat, rel, WM_PATHNAME)) {
            matched = 1;
        } else {
            slash = rel;
            while ((slash = strchr(slash, '/')) != NULL) {
                slash++;
                if (ignorelist_wildmatch(gp->pat, slash, WM_PATHNAME)) {
                    matched = 1;
                    break;
                }
            }
        }
        (void)tmp;
    }

    return matched;
}

static int last_match_ignored(gitignore_t *gi, const char *path_rel, int is_dir) {
    int ignored = 0;
    int any = 0;
    size_t i, j;

    for (i = 0; i < gi->n_files; i++) {
        git_file_t *gf = &gi->files[i];
        for (j = 0; j < gf->n; j++) {
            if (match_one_pattern(&gf->pats[j], gf->base, path_rel, is_dir)) {
                any = 1;
                ignored = !(gf->pats[j].flags & GP_NEGATE);
            }
        }
    }
    (void)any;
    return ignored;
}

static int ensure_path_gitignores(gitignore_t *gi, const char *path_rel) {
    char buf[4096];
    char *slash;
    size_t n;

    if (ensure_dir_gitignore(gi, "") != 0) {
        return -1;
    }
    n = strlen(path_rel);
    if (n >= sizeof buf) {
        return -1;
    }
    memcpy(buf, path_rel, n + 1);
    slash = buf;
    while ((slash = strchr(slash, '/')) != NULL) {
        *slash = '\0';
        if (ensure_dir_gitignore(gi, buf) != 0) {
            return -1;
        }
        *slash = '/';
        slash++;
    }
    return 0;
}

static char *read_git_config_excludesfile(const char *git_dir) {
    char path[4096];
    FILE *f;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    char *result = NULL;

    snprintf(path, sizeof path, "%s/config", git_dir);
    f = fopen(path, "r");
    if (!f) {
        return NULL;
    }
    while ((n = getline(&line, &cap, f)) != -1) {
        char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (strncmp(p, "excludesfile", 12) == 0) {
            p += 12;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p == '=') {
                p++;
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                if (n > 0 && line[n - 1] == '\n') {
                    line[n - 1] = '\0';
                }
                {
                    size_t L = strlen(p);
                    while (L > 0 && (p[L - 1] == ' ' || p[L - 1] == '\t' || p[L - 1] == '\r')) {
                        p[--L] = '\0';
                    }
                }
                if (*p == '~' && p[1] == '/') {
                    const char *home = getenv("HOME");
                    if (home) {
                        size_t need = strlen(home) + strlen(p + 1) + 1;
                        result = malloc(need);
                        if (result) {
                            snprintf(result, need, "%s%s", home, p + 1);
                        }
                    }
                } else if (*p) {
                    result = xstrdup(p);
                }
                break;
            }
        }
    }
    free(line);
    fclose(f);
    return result;
}

static char *find_global_excludes(void) {
    const char *env = getenv("GIT_CONFIG_GLOBAL");
    const char *xdg;
    char path[4096];
    struct stat st;
    char *from_config = NULL;

    /* Try reading ~/.gitconfig for core.excludesFile — light parse */
    {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof path, "%s/.gitconfig", home);
            /* reuse config reader by faking git_dir parent — open directly */
            {
                FILE *f = fopen(path, "r");
                char *line = NULL;
                size_t cap = 0;
                ssize_t n;
                int in_core = 0;
                if (f) {
                    while ((n = getline(&line, &cap, f)) != -1) {
                        char *p = line;
                        while (*p == ' ' || *p == '\t') {
                            p++;
                        }
                        if (*p == '[') {
                            in_core = (strncmp(p, "[core]", 6) == 0);
                            continue;
                        }
                        if (!in_core) {
                            continue;
                        }
                        if (strncmp(p, "excludesfile", 12) == 0) {
                            p += 12;
                            while (*p == ' ' || *p == '\t') {
                                p++;
                            }
                            if (*p == '=') {
                                p++;
                                while (*p == ' ' || *p == '\t') {
                                    p++;
                                }
                                if (n > 0 && line[n - 1] == '\n') {
                                    line[n - 1] = '\0';
                                }
                                {
                                    size_t L = strlen(p);
                                    while (L > 0 &&
                                           (p[L - 1] == ' ' || p[L - 1] == '\t' || p[L - 1] == '\r')) {
                                        p[--L] = '\0';
                                    }
                                }
                                if (*p == '~' && p[1] == '/') {
                                    size_t need = strlen(home) + strlen(p + 1) + 1;
                                    from_config = malloc(need);
                                    if (from_config) {
                                        snprintf(from_config, need, "%s%s", home, p + 1);
                                    }
                                } else if (*p) {
                                    from_config = xstrdup(p);
                                }
                                break;
                            }
                        }
                    }
                    free(line);
                    fclose(f);
                }
            }
        }
    }
    if (from_config) {
        return from_config;
    }
    (void)env;

    xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        snprintf(path, sizeof path, "%s/git/ignore", xdg);
        if (stat(path, &st) == 0) {
            return xstrdup(path);
        }
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof path, "%s/.config/git/ignore", home);
            if (stat(path, &st) == 0) {
                return xstrdup(path);
            }
        }
    }
    return NULL;
}

static char *resolve_git_dir(const char *root) {
    char path[4096];
    struct stat st;
    snprintf(path, sizeof path, "%s/.git", root);
    if (lstat(path, &st) != 0) {
        return NULL;
    }
    if (S_ISDIR(st.st_mode)) {
        return xstrdup(path);
    }
    if (S_ISREG(st.st_mode)) {
        /* gitfile: gitdir: <path> */
        FILE *f = fopen(path, "r");
        char *line = NULL;
        size_t cap = 0;
        ssize_t n;
        char *out = NULL;
        if (!f) {
            return NULL;
        }
        n = getline(&line, &cap, f);
        fclose(f);
        if (n > 0) {
            if (line[n - 1] == '\n') {
                line[n - 1] = '\0';
            }
            if (strncmp(line, "gitdir: ", 8) == 0) {
                const char *gd = line + 8;
                if (gd[0] == '/') {
                    out = xstrdup(gd);
                } else {
                    size_t need = strlen(root) + 1 + strlen(gd) + 1;
                    out = malloc(need);
                    if (out) {
                        snprintf(out, need, "%s/%s", root, gd);
                    }
                }
            }
        }
        free(line);
        return out;
    }
    return NULL;
}

gitignore_t *gitignore_open(const char *root) {
    gitignore_t *gi;
    char *git_dir;
    git_file_t *gf;
    char *global;

    gi = calloc(1, sizeof(*gi));
    if (!gi) {
        return NULL;
    }
    gi->root = xstrdup(root);
    if (!gi->root) {
        free(gi);
        return NULL;
    }
    /* strip trailing slashes */
    {
        size_t n = strlen(gi->root);
        while (n > 1 && gi->root[n - 1] == '/') {
            gi->root[--n] = '\0';
        }
    }

    /* Lowest precedence: global excludes */
    global = find_global_excludes();
    if (global) {
        gf = add_file(gi, "");
        if (gf) {
            load_patterns_from_file(gf, global);
        }
        free(global);
    }

    /* Then $GIT_DIR/info/exclude */
    git_dir = resolve_git_dir(gi->root);
    if (git_dir) {
        char excl[4096];
        char *cfg;

        snprintf(excl, sizeof excl, "%s/info/exclude", git_dir);
        gf = add_file(gi, "");
        if (gf) {
            load_patterns_from_file(gf, excl);
        }

        cfg = read_git_config_excludesfile(git_dir);
        if (cfg) {
            gf = add_file(gi, "");
            if (gf) {
                load_patterns_from_file(gf, cfg);
            }
            free(cfg);
        }
        free(git_dir);
    }

    /* Root .gitignore loaded lazily on first match, but also mark ready */
    (void)ends_with;
    return gi;
}

void gitignore_free(gitignore_t *gi) {
    size_t i, j;
    if (!gi) {
        return;
    }
    for (i = 0; i < gi->n_files; i++) {
        for (j = 0; j < gi->files[i].n; j++) {
            free(gi->files[i].pats[j].pat);
        }
        free(gi->files[i].pats);
        free(gi->files[i].base);
    }
    free(gi->files);
    for (i = 0; i < gi->n_loaded; i++) {
        free(gi->loaded[i]);
    }
    free(gi->loaded);
    free(gi->root);
    free(gi);
}

int gitignore_match(gitignore_t *gi, const char *path_rel, int is_dir) {
    char buf[4096];
    char *slash;
    size_t n;

    if (!gi || !path_rel) {
        return 0;
    }

    /* Always ignore .git at root or anywhere as a component named .git */
    if (strcmp(path_rel, ".git") == 0 || strstr(path_rel, "/.git/") != NULL ||
        ends_with(path_rel, "/.git")) {
        return 1;
    }

    if (ensure_path_gitignores(gi, path_rel) != 0) {
        return 0;
    }

    /* If any parent directory is ignored, the path is ignored.
     * Git does not re-include files under ignored directories. */
    n = strlen(path_rel);
    if (n >= sizeof buf) {
        return 0;
    }
    memcpy(buf, path_rel, n + 1);
    slash = buf;
    while ((slash = strchr(slash, '/')) != NULL) {
        *slash = '\0';
        if (buf[0] != '\0' && last_match_ignored(gi, buf, 1)) {
            return 1;
        }
        *slash = '/';
        slash++;
    }

    return last_match_ignored(gi, path_rel, is_dir);
}
