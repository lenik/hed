/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "repl.h"

#include "config.h"
#include "edit_file.h"
#include "lines.h"
#include "match.h"
#include "walk.h"

#include <bas/locale/i18n.h>
#include <bas/log/deflog.h>
#include <bas/proc/env.h>

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

define_logger();

enum {
    OPT_VERSION = 256,
    OPT_MAX_DEPTH,
    OPT_EXCLUDE,
    OPT_EXCLUDE_FROM,
    OPT_EXCLUDE_DIR,
    OPT_INCLUDE,
    OPT_BINARY_FILES,
    OPT_NO_IGNORE_CASE,
    OPT_REGEXP,
    OPT_COLOR,
    OPT_PALETTE,
    OPT_RANGE,
};

enum binary_mode {
    BIN_BINARY = 0, /* stop before first non-printable */
    BIN_WITHOUT_MATCH,
    BIN_TEXT,
};

enum color_when {
    COLOR_NEVER = 0,
    COLOR_ALWAYS,
    COLOR_AUTO,
};

/* SGR substrings, GNU diff --palette compatible (rs/hd/ad/de/ln). */
typedef struct palette {
    char rs[64];
    char hd[64];
    char ad[64];
    char de[64];
    char ln[64];
} palette_t;

typedef struct repl_stats {
    size_t files_seen;
    size_t files_changed;
    size_t files_skipped_binary;
    size_t replacements;
} repl_stats_t;

typedef struct repl_ctx {
    const char *prog;
    match_engine_t *eng;
    const char *replacement;
    enum binary_mode binary;
    int force_binary; /* -U */
    int quiet;
    int summary;
    int dryrun;       /* -n: report only, do not rewrite files */
    int context_diff; /* -c */
    int unified_diff; /* -u */
    enum color_when color_when;
    int use_color; /* resolved for this run */
    palette_t palette;
    walk_opts_t walk;
    repl_stats_t stats;
    int had_error;
} repl_ctx_t;

void usage(FILE *out) {
    fputs(_("Usage: repl [OPTION]... PATTERN REPLACEMENT [FILE]...\n"
            "       repl [OPTION]... -e PATTERN ... REPLACEMENT [FILE]...\n"
            "Batch replace PATTERN with REPLACEMENT in files.\n"
            "A file is rewritten only when its contents would change.\n"
            "With no FILE, read standard input and write to standard output.\n"),
          out);
    fputs("\n", out);
    fputs(_("Pattern selection (default: fixed strings):\n"), out);
    fputs("  -F, --fixed-strings     ", out);
    fputs(_("interpret patterns as fixed strings (default)\n"), out);
    fputs("  -E, --extended-regexp   ", out);
    fputs(_("interpret patterns as POSIX extended regular expressions\n"), out);
    fputs("  -G, --basic-regexp      ", out);
    fputs(_("interpret patterns as POSIX basic regular expressions\n"), out);
    fputs("  -P, --perl-regexp       ", out);
    fputs(_("interpret patterns as Perl-compatible regular expressions (PCRE2)\n"), out);
    fputs("  -e, --regexp=PATTERNS   ", out);
    fputs(_("use PATTERNS; may be repeated; combine with -f\n"), out);
    fputs("  -f, --file=FILE         ", out);
    fputs(_("read patterns from FILE, one per line; - means standard input\n"), out);
    fputs("  -i, --ignore-case       ", out);
    fputs(_("ignore case distinctions in patterns and data\n"), out);
    fputs("      --no-ignore-case    ", out);
    fputs(_("do not ignore case (default); cancels a prior -i\n"), out);
    fputs("  -v, --invert-match      ", out);
    fputs(_("replace each non-matching line entirely with REPLACEMENT\n"), out);
    fputs("  -w, --word-regexp       ", out);
    fputs(_("match only whole words\n"), out);
    fputs("  -x, --line-regexp       ", out);
    fputs(_("match only whole lines\n"), out);
    fputs("  -l, --line              ", out);
    fputs(_("expand match to entire line (default: matched only)\n"), out);
    fputs("  -g, --global            ", out);
    fputs(_("replace all occurrences in a single line (default)\n"), out);
    fputs("  -1, --first             ", out);
    fputs(_("replace first occurrence in a single line\n"), out);
    fputs("      --range=N..M        ", out);
    fputs(_("replace N-th to M-th occurrences per line (1-based, .. = 1..inf)\n"), out);
    fputs("\n", out);
    fputs(_("File selection:\n"), out);
    fputs("  -r, --recursive         ", out);
    fputs(_("recurse directories; follow symlinks only if on the command line\n"), out);
    fputs("  -R, --dereference-recursive\n"
          "                          ",
          out);
    fputs(_("recurse directories, following all symbolic links\n"), out);
    fputs("      --max-depth=NUM     ", out);
    fputs(_("limit directory recursion depth\n"), out);
    fputs("      --exclude=GLOB      ", out);
    fputs(_("skip files whose base name matches GLOB\n"), out);
    fputs("      --exclude-from=FILE ", out);
    fputs(_("read exclude globs from FILE, one per line\n"), out);
    fputs("      --exclude-dir=GLOB  ", out);
    fputs(_("skip directories whose base name matches GLOB\n"), out);
    fputs("      --include=GLOB      ", out);
    fputs(_("only process files whose base name matches GLOB\n"), out);
    fputs("  -d, --directories=ACTION\n"
          "                          ",
          out);
    fputs(_("how to handle directories: repl (default), skip, or recurse\n"), out);
    fputs("  -D, --devices=ACTION    ", out);
    fputs(_("how to handle devices/FIFOs/sockets: repl (default) or skip\n"), out);
    fputs("      --binary-files=TYPE ", out);
    fputs(_("binary (default: stop before non-printable), without-match, or text\n"), out);
    fputs("  -I                      ", out);
    fputs(_("same as --binary-files=without-match\n"), out);
    fputs("  -U, --binary            ", out);
    fputs(_("treat files as binary; disable the binary heuristic\n"), out);
    fputs("  -z, --null-data         ", out);
    fputs(_("treat input as NUL-terminated lines instead of newline-terminated\n"), out);
    fputs("\n", out);
    fputs(_("Reporting:\n"), out);
    fputs("  -n, --dryrun            ", out);
    fputs(_("show what would change; with -u/-c print diffs, but do not update files\n"),
          out);
    fputs("  -q, --quiet, --silent   ", out);
    fputs(_("suppress the default one-line summary\n"), out);
    fputs("  -c, --context           ", out);
    fputs(_("print a context diff (diff -c) for each changed file\n"), out);
    fputs("  -u, --unified           ", out);
    fputs(_("print a unified diff (diff -u) for each changed file\n"), out);
    fputs("  -s, --summary           ", out);
    fputs(_("print a detailed batch summary (files examined/changed, counts)\n"), out);
    fputs("      --color[=WHEN]      ", out);
    fputs(_("color diffs; WHEN is never, always, or auto (default with --color: auto)\n"),
          out);
    fputs("      --palette=PALETTE   ", out);
    fputs(_("colors when --color is active; colon-separated terminfo/SGR list "
            "(rs:hd:ad:de:ln)\n"),
          out);
    fputs("\n", out);
    fputs("  -h, --help              ", out);
    fputs(_("display this help and exit\n"), out);
    fputs("      --version           ", out);
    fputs(_("output version information and exit\n"), out);
    fprintf(out, _("\nReport bugs to: <%s>\n"), PROJECT_EMAIL);
}

static int list_push(char ***list, size_t *n, const char *s) {
    char **nl = realloc(*list, (*n + 1) * sizeof(char *));
    if (!nl) {
        return -1;
    }
    *list = nl;
    (*list)[*n] = strdup(s);
    if (!(*list)[*n]) {
        return -1;
    }
    (*n)++;
    return 0;
}

static void list_free(char **list, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        free(list[i]);
    }
    free(list);
}

static void palette_set_defaults(palette_t *p) {
    /* Like GNU diff; hd uses reverse so the filename banner has a background. */
    snprintf(p->rs, sizeof p->rs, "%s", "0");
    snprintf(p->hd, sizeof p->hd, "%s", "1;7");
    snprintf(p->ad, sizeof p->ad, "%s", "32");
    snprintf(p->de, sizeof p->de, "%s", "31");
    snprintf(p->ln, sizeof p->ln, "%s", "36");
}

static int palette_parse(palette_t *p, const char *spec) {
    char *copy;
    char *tok;
    char *save = NULL;

    if (!spec) {
        return -1;
    }
    copy = strdup(spec);
    if (!copy) {
        return -1;
    }
    for (tok = strtok_r(copy, ":", &save); tok; tok = strtok_r(NULL, ":", &save)) {
        char *eq = strchr(tok, '=');
        char *key;
        char *val;
        size_t klen;

        if (!eq || eq == tok) {
            free(copy);
            return -1;
        }
        *eq = '\0';
        key = tok;
        val = eq + 1;
        klen = strlen(key);
        if (klen == 2 && key[0] == 'r' && key[1] == 's') {
            snprintf(p->rs, sizeof p->rs, "%s", val);
        } else if (klen == 2 && key[0] == 'h' && key[1] == 'd') {
            snprintf(p->hd, sizeof p->hd, "%s", val);
        } else if (klen == 2 && key[0] == 'a' && key[1] == 'd') {
            snprintf(p->ad, sizeof p->ad, "%s", val);
        } else if (klen == 2 && key[0] == 'd' && key[1] == 'e') {
            snprintf(p->de, sizeof p->de, "%s", val);
        } else if (klen == 2 && key[0] == 'l' && key[1] == 'n') {
            snprintf(p->ln, sizeof p->ln, "%s", val);
        } else {
            free(copy);
            return -1;
        }
    }
    free(copy);
    return 0;
}

static void sgr_begin(FILE *out, int use_color, const char *sgr) {
    if (use_color && sgr && sgr[0]) {
        fprintf(out, "\033[%sm", sgr);
    }
}

static void sgr_end(FILE *out, int use_color, const char *rs) {
    if (use_color) {
        fprintf(out, "\033[%sm", (rs && rs[0]) ? rs : "0");
    }
}

static int is_diff_file_header(const char *line) {
    /* GNU diff file headers: "--- path\ttime" / "+++ path\ttime" / "*** path\ttime" */
    if ((strncmp(line, "--- ", 4) == 0 || strncmp(line, "+++ ", 4) == 0 ||
         strncmp(line, "*** ", 4) == 0) &&
        strchr(line, '\t') != NULL) {
        return 1;
    }
    return 0;
}

static const char *line_sgr(const palette_t *p, const char *line) {
    if (line[0] == '@' || strcmp(line, "***************") == 0 ||
        (strncmp(line, "*** ", 4) == 0 && strchr(line, '\t') == NULL) ||
        (strncmp(line, "--- ", 4) == 0 && strchr(line, '\t') == NULL) ||
        (strncmp(line, "+++ ", 4) == 0 && strchr(line, '\t') == NULL)) {
        return p->ln;
    }
    if (line[0] == '+' && strncmp(line, "+++", 3) != 0) {
        return p->ad;
    }
    if (line[0] == '-' && strncmp(line, "---", 3) != 0) {
        return p->de;
    }
    if (line[0] == '!') {
        return p->de;
    }
    return NULL;
}

static int print_diff(const repl_ctx_t *ctx, const char *path, const char *old, size_t old_len,
                      const char *neu, size_t neu_len, int unified) {
    char t1[] = "/tmp/hed-repl-a-XXXXXX";
    char t2[] = "/tmp/hed-repl-b-XXXXXX";
    int fd1, fd2;
    FILE *f;
    int pipefd[2];
    pid_t pid;
    int status;
    FILE *in;
    char *line = NULL;
    size_t cap = 0;
    ssize_t nr;
    int skipped_headers = 0;

    fd1 = mkstemp(t1);
    fd2 = mkstemp(t2);
    if (fd1 < 0 || fd2 < 0) {
        if (fd1 >= 0) {
            close(fd1);
            unlink(t1);
        }
        if (fd2 >= 0) {
            close(fd2);
            unlink(t2);
        }
        return -1;
    }
    f = fdopen(fd1, "wb");
    if (!f || (old_len && fwrite(old, 1, old_len, f) != old_len) || fclose(f) != 0) {
        close(fd2);
        unlink(t1);
        unlink(t2);
        return -1;
    }
    f = fdopen(fd2, "wb");
    if (!f || (neu_len && fwrite(neu, 1, neu_len, f) != neu_len) || fclose(f) != 0) {
        unlink(t1);
        unlink(t2);
        return -1;
    }

    if (pipe(pipefd) != 0) {
        unlink(t1);
        unlink(t2);
        return -1;
    }

    pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(pipefd[1]);
        if (unified) {
            execlp("diff", "diff", "-u", t1, t2, (char *)NULL);
        } else {
            execlp("diff", "diff", "-c", t1, t2, (char *)NULL);
        }
        _exit(127);
    }
    close(pipefd[1]);
    if (pid < 0) {
        close(pipefd[0]);
        unlink(t1);
        unlink(t2);
        return -1;
    }

    in = fdopen(pipefd[0], "r");
    if (!in) {
        close(pipefd[0]);
        waitpid(pid, &status, 0);
        unlink(t1);
        unlink(t2);
        return -1;
    }

    sgr_begin(stdout, ctx->use_color, ctx->palette.hd);
    printf("::: %s :::", path);
    sgr_end(stdout, ctx->use_color, ctx->palette.rs);
    fputc('\n', stdout);

    while ((nr = getline(&line, &cap, in)) != -1) {
        if (nr > 0 && line[nr - 1] == '\n') {
            line[nr - 1] = '\0';
        }
        if (skipped_headers < 2 && is_diff_file_header(line)) {
            skipped_headers++;
            continue;
        }
        {
            const char *sgr = line_sgr(&ctx->palette, line);
            sgr_begin(stdout, ctx->use_color && sgr, sgr);
            fputs(line, stdout);
            sgr_end(stdout, ctx->use_color && sgr, ctx->palette.rs);
            fputc('\n', stdout);
        }
    }
    free(line);
    fclose(in);
    waitpid(pid, &status, 0);
    unlink(t1);
    unlink(t2);
    return 0;
}

static int process_file(const char *path, void *userdata) {
    repl_ctx_t *ctx = userdata;
    char *old = NULL;
    size_t old_len = 0;
    mode_t mode = 0644;
    char *work;
    size_t work_len;
    char *neu = NULL;
    size_t neu_len = 0;
    size_t n_repl = 0;
    size_t suffix_off = 0;
    char *suffix = NULL;
    size_t suffix_len = 0;
    int rc;

    ctx->stats.files_seen++;

    old = read_file_bytes(path, &old_len, &mode);
    if (!old) {
        fprintf(stderr, "%s: ", ctx->prog);
        perror(path);
        ctx->had_error = 1;
        return 0; /* continue batch */
    }

    work = old;
    work_len = old_len;

    if (!ctx->force_binary && ctx->binary != BIN_TEXT) {
        size_t prefix = text_prefix_len(old, old_len);
        if (prefix < old_len) {
            if (ctx->binary == BIN_WITHOUT_MATCH) {
                ctx->stats.files_skipped_binary++;
                free(old);
                return 0;
            }
            /* BIN_BINARY: only replace in text prefix */
            work_len = prefix;
            suffix_off = prefix;
            suffix = old + prefix;
            suffix_len = old_len - prefix;
        }
    }

    neu = match_replace(ctx->eng, work, work_len, ctx->replacement, &neu_len, &n_repl);
    if (!neu) {
        fprintf(stderr, _("%s: replace failed: %s\n"), ctx->prog, path);
        free(old);
        ctx->had_error = 1;
        return 0;
    }

    if (suffix_len > 0) {
        char *combined = NULL;
        size_t cl = 0, cc = 0;
        if (buf_append(&combined, &cl, &cc, neu, neu_len) != 0 ||
            buf_append(&combined, &cl, &cc, suffix, suffix_len) != 0) {
            free(neu);
            free(old);
            free(combined);
            ctx->had_error = 1;
            return 0;
        }
        free(neu);
        neu = combined;
        neu_len = cl;
    }

    if (n_repl == 0 && neu_len == old_len && memcmp(neu, old, old_len) == 0) {
        free(neu);
        free(old);
        return 0;
    }

    if (ctx->context_diff || ctx->unified_diff) {
        print_diff(ctx, path, old, old_len, neu, neu_len, ctx->unified_diff);
    }

    if (ctx->dryrun) {
        ctx->stats.files_changed++;
        ctx->stats.replacements += n_repl;
        if (!ctx->quiet && !ctx->context_diff && !ctx->unified_diff) {
            printf(_("would change: %s (%zu replacement(s))\n"), path, n_repl);
        } else if (ctx->summary && !ctx->quiet) {
            printf(_("%s: %zu replacement(s)\n"), path, n_repl);
        }
        free(neu);
        free(old);
        return 0;
    }

    rc = replace_file_if_changed(path, old, old_len, neu, neu_len, mode);
    if (rc < 0) {
        fprintf(stderr, "%s: ", ctx->prog);
        perror(path);
        ctx->had_error = 1;
    } else if (rc > 0) {
        ctx->stats.files_changed++;
        ctx->stats.replacements += n_repl;
        if (ctx->summary && !ctx->quiet) {
            printf(_("%s: %zu replacement(s)\n"), path, n_repl);
        }
    }

    free(neu);
    free(old);
    return 0;
}

int main(int argc, char **argv) {
    const char *exe = self_exe();
    init_i18n(LOCALEDIR);

    match_opts_t mopts;
    memset(&mopts, 0, sizeof mopts);
    mopts.type = MATCH_FIXED;
    mopts.line_sep = '\n';

    char **patterns = NULL;
    size_t n_patterns = 0;
    char **exclude = NULL;
    size_t n_exclude = 0;
    char **exclude_dir = NULL;
    size_t n_exclude_dir = 0;
    char **include = NULL;
    size_t n_include = 0;

    repl_ctx_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.prog = exe;
    ctx.binary = BIN_BINARY;
    ctx.color_when = COLOR_AUTO;
    palette_set_defaults(&ctx.palette);
    ctx.walk.max_depth = -1;
    ctx.walk.dir_action = 0;
    ctx.walk.device_action = 0;

    int pattern_from_opt = 0;

    static const struct option long_opts[] = {
        {"extended-regexp", no_argument, NULL, 'E'},
        {"fixed-strings", no_argument, NULL, 'F'},
        {"basic-regexp", no_argument, NULL, 'G'},
        {"perl-regexp", no_argument, NULL, 'P'},
        {"regexp", required_argument, NULL, 'e'},
        {"file", required_argument, NULL, 'f'},
        {"ignore-case", no_argument, NULL, 'i'},
        {"no-ignore-case", no_argument, NULL, OPT_NO_IGNORE_CASE},
        {"invert-match", no_argument, NULL, 'v'},
        {"word-regexp", no_argument, NULL, 'w'},
        {"line-regexp", no_argument, NULL, 'x'},
        {"line", no_argument, NULL, 'l'},
        {"global", no_argument, NULL, 'g'},
        {"first", no_argument, NULL, '1'},
        {"range", required_argument, NULL, OPT_RANGE},
        {"recursive", no_argument, NULL, 'r'},
        {"dereference-recursive", no_argument, NULL, 'R'},
        {"max-depth", required_argument, NULL, OPT_MAX_DEPTH},
        {"exclude", required_argument, NULL, OPT_EXCLUDE},
        {"exclude-from", required_argument, NULL, OPT_EXCLUDE_FROM},
        {"exclude-dir", required_argument, NULL, OPT_EXCLUDE_DIR},
        {"include", required_argument, NULL, OPT_INCLUDE},
        {"binary-files", required_argument, NULL, OPT_BINARY_FILES},
        {"directories", required_argument, NULL, 'd'},
        {"devices", required_argument, NULL, 'D'},
        {"binary", no_argument, NULL, 'U'},
        {"null-data", no_argument, NULL, 'z'},
        {"dryrun", no_argument, NULL, 'n'},
        {"dry-run", no_argument, NULL, 'n'},
        {"quiet", no_argument, NULL, 'q'},
        {"silent", no_argument, NULL, 'q'},
        {"context", no_argument, NULL, 'c'},
        {"unified", no_argument, NULL, 'u'},
        {"summary", no_argument, NULL, 's'},
        {"color", optional_argument, NULL, OPT_COLOR},
        {"palette", required_argument, NULL, OPT_PALETTE},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, OPT_VERSION},
        {NULL, 0, NULL, 0},
    };

    for (;;) {
        int c = getopt_long(argc, argv, "EFGPe:f:ivwxlg1rRd:D:IUznqcusVh", long_opts, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'E':
            mopts.type = MATCH_EXTENDED;
            break;
        case 'F':
            mopts.type = MATCH_FIXED;
            break;
        case 'G':
            mopts.type = MATCH_BASIC;
            break;
        case 'P':
            mopts.type = MATCH_PERL;
            break;
        case 'e':
            pattern_from_opt = 1;
            if (list_push(&patterns, &n_patterns, optarg) != 0) {
                return 1;
            }
            break;
        case 'f': {
            FILE *f;
            char *line = NULL;
            size_t cap = 0;
            ssize_t nr;
            pattern_from_opt = 1;
            if (strcmp(optarg, "-") == 0) {
                f = stdin;
            } else {
                f = fopen(optarg, "r");
                if (!f) {
                    perror(optarg);
                    return 1;
                }
            }
            while ((nr = getline(&line, &cap, f)) != -1) {
                if (nr > 0 && line[nr - 1] == '\n') {
                    line[nr - 1] = '\0';
                }
                if (list_push(&patterns, &n_patterns, line) != 0) {
                    free(line);
                    if (f != stdin) {
                        fclose(f);
                    }
                    return 1;
                }
            }
            free(line);
            if (f != stdin) {
                fclose(f);
            }
            break;
        }
        case 'i':
            mopts.ignore_case = 1;
            break;
        case OPT_NO_IGNORE_CASE:
            mopts.ignore_case = 0;
            break;
        case 'v':
            mopts.invert_match = 1;
            break;
        case 'w':
            mopts.word_regexp = 1;
            break;
        case 'x':
            mopts.line_regexp = 1;
            break;
        case 'l':
            mopts.line_mode = 1;
            break;
        case 'g':
            mopts.replace_mode = 0; /* global (default) */
            break;
        case '1':
            mopts.replace_mode = 1; /* first only */
            break;
        case OPT_RANGE: {
            char *dot = strchr(optarg, '.');
            if (dot && dot[1] == '.') {
                /* N..M format */
                char *end = NULL;
                mopts.range_n = (int)strtol(optarg, &end, 10);
                if (end != dot) {
                    fprintf(stderr, _("%s: invalid --range format\n"), exe);
                    return 1;
                }
                if (dot[2] != '\0') {
                    mopts.range_m = (int)strtol(dot + 2, &end, 10);
                    if (*end != '\0') {
                        fprintf(stderr, _("%s: invalid --range format\n"), exe);
                        return 1;
                    }
                } else {
                    mopts.range_m = -1; /* infinity */
                }
            } else {
                /* single number N */
                char *end = NULL;
                mopts.range_n = (int)strtol(optarg, &end, 10);
                if (*end != '\0') {
                    fprintf(stderr, _("%s: invalid --range format\n"), exe);
                    return 1;
                }
                mopts.range_m = mopts.range_n; /* single occurrence */
            }
            break;
        }
        case 'r':
            ctx.walk.recursive = 1;
            ctx.walk.dir_action = 2;
            break;
        case 'R':
            ctx.walk.dereference = 1;
            ctx.walk.recursive = 1;
            ctx.walk.dir_action = 2;
            break;
        case OPT_MAX_DEPTH: {
            char *end = NULL;
            ctx.walk.max_depth = (int)strtol(optarg, &end, 10);
            if (!optarg[0] || (end && *end)) {
                fprintf(stderr, _("%s: invalid max-depth\n"), exe);
                return 1;
            }
            break;
        }
        case OPT_EXCLUDE:
            if (list_push(&exclude, &n_exclude, optarg) != 0) {
                return 1;
            }
            break;
        case OPT_EXCLUDE_FROM:
            if (walk_load_exclude_from(optarg, &exclude, &n_exclude) != 0) {
                perror(optarg);
                return 1;
            }
            break;
        case OPT_EXCLUDE_DIR:
            if (list_push(&exclude_dir, &n_exclude_dir, optarg) != 0) {
                return 1;
            }
            break;
        case OPT_INCLUDE:
            if (list_push(&include, &n_include, optarg) != 0) {
                return 1;
            }
            break;
        case OPT_BINARY_FILES:
            if (strcmp(optarg, "binary") == 0) {
                ctx.binary = BIN_BINARY;
            } else if (strcmp(optarg, "without-match") == 0) {
                ctx.binary = BIN_WITHOUT_MATCH;
            } else if (strcmp(optarg, "text") == 0) {
                ctx.binary = BIN_TEXT;
            } else {
                fprintf(stderr, _("%s: invalid --binary-files value\n"), exe);
                return 1;
            }
            break;
        case 'I':
            ctx.binary = BIN_WITHOUT_MATCH;
            break;
        case 'd':
            if (strcmp(optarg, "repl") == 0) {
                ctx.walk.dir_action = 0;
            } else if (strcmp(optarg, "skip") == 0) {
                ctx.walk.dir_action = 1;
            } else if (strcmp(optarg, "recurse") == 0) {
                ctx.walk.dir_action = 2;
                ctx.walk.recursive = 1;
            } else {
                fprintf(stderr, _("%s: invalid --directories action\n"), exe);
                return 1;
            }
            break;
        case 'D':
            if (strcmp(optarg, "repl") == 0) {
                ctx.walk.device_action = 0;
            } else if (strcmp(optarg, "skip") == 0) {
                ctx.walk.device_action = 1;
            } else {
                fprintf(stderr, _("%s: invalid --devices action\n"), exe);
                return 1;
            }
            break;
        case 'U':
            ctx.force_binary = 1;
            break;
        case 'z':
            mopts.line_sep = '\0';
            break;
        case 'n':
            ctx.dryrun = 1;
            break;
        case 'q':
            ctx.quiet = 1;
            break;
        case 'c':
            ctx.context_diff = 1;
            break;
        case 'u':
            ctx.unified_diff = 1;
            break;
        case 's':
            ctx.summary = 1;
            break;
        case OPT_COLOR:
            if (!optarg || strcmp(optarg, "auto") == 0) {
                ctx.color_when = COLOR_AUTO;
            } else if (strcmp(optarg, "always") == 0) {
                ctx.color_when = COLOR_ALWAYS;
            } else if (strcmp(optarg, "never") == 0) {
                ctx.color_when = COLOR_NEVER;
            } else {
                fprintf(stderr, _("%s: invalid --color value (use never, always, or auto)\n"),
                        exe);
                return 1;
            }
            break;
        case OPT_PALETTE:
            if (palette_parse(&ctx.palette, optarg) != 0) {
                fprintf(stderr, _("%s: invalid --palette\n"), exe);
                return 1;
            }
            break;
        case 'h':
            usage(stdout);
            return 0;
        case OPT_VERSION:
            printf("repl %s\n", PROJECT_VERSION);
            printf(_("Copyright (C) %d %s\n"), PROJECT_YEAR, PROJECT_AUTHOR);
            fputs(_("License AGPL-3.0-or-later: <https://www.gnu.org/licenses/agpl-3.0.html>\n"),
                  stdout);
            return 0;
        default:
            usage(stderr);
            return 1;
        }
    }

    argc -= optind;
    argv += optind;

    if (!pattern_from_opt) {
        if (argc < 2) {
            fprintf(stderr, _("%s: missing PATTERN and REPLACEMENT\n"), exe);
            usage(stderr);
            return 1;
        }
        if (list_push(&patterns, &n_patterns, argv[0]) != 0) {
            return 1;
        }
        ctx.replacement = argv[1];
        argv += 2;
        argc -= 2;
    } else {
        if (argc < 1) {
            fprintf(stderr, _("%s: missing REPLACEMENT\n"), exe);
            usage(stderr);
            return 1;
        }
        ctx.replacement = argv[0];
        argv += 1;
        argc -= 1;
    }

    {
        char *err = NULL;
        const char **pp = (const char **)patterns;
        ctx.eng = match_engine_create(pp, n_patterns, &mopts, &err);
        if (!ctx.eng) {
            fprintf(stderr, "%s: %s\n", exe, err ? err : _("pattern error"));
            free(err);
            list_free(patterns, n_patterns);
            return 1;
        }
    }

    ctx.walk.exclude = (const char **)exclude;
    ctx.walk.n_exclude = n_exclude;
    ctx.walk.exclude_dir = (const char **)exclude_dir;
    ctx.walk.n_exclude_dir = n_exclude_dir;
    ctx.walk.include = (const char **)include;
    ctx.walk.n_include = n_include;

    if (ctx.color_when == COLOR_ALWAYS) {
        ctx.use_color = 1;
    } else if (ctx.color_when == COLOR_AUTO) {
        ctx.use_color = isatty(STDOUT_FILENO);
    } else {
        ctx.use_color = 0;
    }

    if (argc == 0 && !ctx.walk.recursive) {
        /* read stdin, write stdout */
        char *data = NULL;
        size_t len = 0, cap = 0;
        char buf[8192];
        size_t n;
        char *neu;
        size_t neu_len = 0, n_repl = 0;

        while ((n = fread(buf, 1, sizeof buf, stdin)) > 0) {
            if (buf_append(&data, &len, &cap, buf, n) != 0) {
                return 1;
            }
        }
        if (!data) {
            data = malloc(1);
            data[0] = '\0';
        }
        neu = match_replace(ctx.eng, data, len, ctx.replacement, &neu_len, &n_repl);
        if (!neu) {
            free(data);
            return 1;
        }
        if (neu_len && fwrite(neu, 1, neu_len, stdout) != neu_len) {
            free(neu);
            free(data);
            return 1;
        }
        free(neu);
        free(data);
        ctx.stats.replacements = n_repl;
    } else {
        char *default_path[] = {"."};
        int i;

        /* -r/-R with no path: walk the current directory */
        if (argc == 0) {
            argv = default_path;
            argc = 1;
        }
        for (i = 0; i < argc; i++) {
            if (walk_path(argv[i], 1, &ctx.walk, process_file, &ctx) < 0) {
                fprintf(stderr, "%s: ", exe);
                perror(argv[i]);
                ctx.had_error = 1;
            }
        }
    }

    if (!ctx.quiet) {
        if (ctx.summary) {
            if (ctx.dryrun) {
                printf(_("files examined:     %zu\n"
                         "files would change: %zu\n"
                         "replacements:       %zu\n"
                         "binary skipped:     %zu\n"),
                       ctx.stats.files_seen, ctx.stats.files_changed, ctx.stats.replacements,
                       ctx.stats.files_skipped_binary);
            } else {
                printf(_("files examined: %zu\n"
                         "files changed:  %zu\n"
                         "replacements:   %zu\n"
                         "binary skipped: %zu\n"),
                       ctx.stats.files_seen, ctx.stats.files_changed, ctx.stats.replacements,
                       ctx.stats.files_skipped_binary);
            }
        } else if (ctx.dryrun) {
            printf(_("%zu file(s) would change, %zu replacement(s)\n"), ctx.stats.files_changed,
                   ctx.stats.replacements);
        } else {
            printf(_("%zu file(s) changed, %zu replacement(s)\n"), ctx.stats.files_changed,
                   ctx.stats.replacements);
        }
    }

    match_engine_free(ctx.eng);
    list_free(patterns, n_patterns);
    list_free(exclude, n_exclude);
    list_free(exclude_dir, n_exclude_dir);
    list_free(include, n_include);
    return ctx.had_error ? 1 : 0;
}
