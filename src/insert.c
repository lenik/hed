/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "insert.h"

#include "config.h"
#include "digit_opt.h"
#include "edit_file.h"
#include "lines.h"

#include <bas/locale/i18n.h>
#include <bas/log/deflog.h>
#include <bas/proc/env.h>

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

define_logger();

enum { OPT_VERSION = 256 };

enum src_mode {
    SRC_EXTENDED = 0, /* -e: @file or string */
    SRC_STRINGS,      /* -l */
    SRC_FILENAMES,    /* -f */
};

enum pos_mode {
    POS_UNSET = 0,
    POS_PREPEND,
    POS_APPEND, /* append_back: 0 = end (>>); N>=1 = before Nth line from end */
    POS_BEFORE_LINE,
};

void usage(FILE *out) {
    fputs(_("Usage: insert [OPTION]... FILE [SRC]...\n"
            "Insert SRC content into FILE at a line position.\n"
            "With -r, remove lines at the insert point (useful for delete/update).\n"),
          out);
    fputs("\n", out);
    fputs(_("Source modes:\n"), out);
    fputs("  -l, --strings      ", out);
    fputs(_("treat each SRC as a string literal\n"), out);
    fputs("  -f, --filenames    ", out);
    fputs(_("treat each SRC as a filename\n"), out);
    fputs("  -e, --extended     ", out);
    fputs(_("@FILE or string (default)\n"), out);
    fputs("\n", out);
    fputs(_("Position:\n"), out);
    fputs("  -n NUM             ", out);
    fputs(_("insert before 1-based line NUM (pads empty lines if needed)\n"), out);
    fputs("  -0 .. -9           ", out);
    fputs(_("digit cluster: -1 prepend, -0 prepend+blank, -N before line N\n"), out);
    fputs("  -a, --append       ", out);
    fputs(_("append; -a0 like >> (end), -a1 insert before last line\n"), out);
    fputs("  -p, --prepend      ", out);
    fputs(_("prepend to the start\n"), out);
    fputs("  -r, --remove NUM   ", out);
    fputs(_("remove NUM lines at the insert point before inserting\n"), out);
    fputs("  -b, --blank NUM    ", out);
    fputs(_("trailing blank lines after each SRC (default: 1 for strings, 0 for files)\n"), out);
    fputs("  -m, --missing TEXT ", out);
    fputs(_("use TEXT when a source file is missing\n"), out);
    fputs("\n", out);
    fputs("  -v, --verbose      ", out);
    fputs(_("repeat for more verbose loggings\n"), out);
    fputs("  -q, --quiet        ", out);
    fputs(_("show less logging messages\n"), out);
    fputs("  -h, --help         ", out);
    fputs(_("display this help and exit\n"), out);
    fputs("      --version      ", out);
    fputs(_("output version information and exit\n"), out);
    fputs("\n", out);
    fprintf(out, _("Report bugs to: <%s>\n"), PROJECT_EMAIL);
}

/* Resolve SRC; sets *as_file. Returns 0 or -1. */
static int load_src(const char *prog, enum src_mode mode, const char *src, const char *missing,
                    char **out, size_t *out_len, int *as_file) {
    const char *path = NULL;

    *as_file = 0;
    switch (mode) {
    case SRC_STRINGS:
        break;
    case SRC_FILENAMES:
        *as_file = 1;
        path = src;
        break;
    case SRC_EXTENDED:
        if (src[0] == '@') {
            *as_file = 1;
            path = src + 1;
        }
        break;
    }

    if (!*as_file) {
        *out_len = strlen(src);
        *out = malloc(*out_len + 1);
        if (!*out) {
            return -1;
        }
        memcpy(*out, src, *out_len + 1);
        return 0;
    }

    {
        mode_t modebits;
        char *data = read_file_bytes(path, out_len, &modebits);
        (void)modebits;
        if (!data) {
            if (missing) {
                *out_len = strlen(missing);
                *out = malloc(*out_len + 1);
                if (!*out) {
                    return -1;
                }
                memcpy(*out, missing, *out_len + 1);
                *as_file = 0;
                return 0;
            }
            fprintf(stderr, "%s: ", prog);
            perror(path);
            return -1;
        }
        *out = data;
        return 0;
    }
}

static void apply_position_num(enum pos_mode *mode, long num, long *line_no, long *append_back,
                               long *blank_after) {
    switch (*mode) {
    case POS_APPEND:
        /* -a0 append at end; -aN (N>=1) insert before Nth line from end */
        *append_back = num;
        break;
    case POS_BEFORE_LINE:
        *line_no = num;
        break;
    case POS_PREPEND:
        *blank_after = num;
        break;
    case POS_UNSET:
    default:
        if (num == 0) {
            *mode = POS_PREPEND;
            *blank_after = 1;
        } else if (num == 1) {
            *mode = POS_PREPEND;
            *blank_after = 0;
        } else {
            *mode = POS_BEFORE_LINE;
            *line_no = num;
        }
        break;
    }
}

/*
 * Plan insertion for -n / -NUM.
 * When allow_pad is 0 and line_no is past EOF, do not pad; insert_at = end.
 */
static void plan_before_line(const char *data, size_t len, long line_no, int allow_pad,
                             size_t *insert_at, long *pad_after, int *need_nl, long *lead_blanks) {
    size_t nlines = count_lines(data, len, '\n');
    long need_before;

    *pad_after = 0;
    *need_nl = 0;
    *lead_blanks = 0;

    if (line_no < 1) {
        *insert_at = 0;
        if (allow_pad) {
            *lead_blanks = 1 - line_no;
        }
        return;
    }

    need_before = line_no - 1;
    if (need_before <= (long)nlines) {
        *insert_at = line_byte_offset(data, len, line_no, '\n');
        return;
    }

    *insert_at = len;
    if (!allow_pad) {
        return;
    }
    if (len > 0 && data[len - 1] != '\n') {
        *need_nl = 1;
    }
    *pad_after = need_before - (long)nlines;
}

/* Byte offset just past `count` lines starting at `start` (start of a line). */
static size_t skip_lines(const char *data, size_t len, size_t start, long count) {
    size_t i = start;
    long n = 0;

    if (count <= 0) {
        return start;
    }
    while (i < len && n < count) {
        while (i < len && data[i] != '\n') {
            i++;
        }
        if (i < len && data[i] == '\n') {
            i++;
        }
        n++;
    }
    return i;
}

int main(int argc, char **argv) {
    const char *exe = self_exe();
    init_i18n(LOCALEDIR);

    enum src_mode srcmode = SRC_EXTENDED;
    enum pos_mode posmode = POS_UNSET;
    long line_no = 1;
    long append_back = 0; /* for POS_APPEND: 0=end, N=before Nth from end */
    long blank_after = 0; /* after inserted block (prepend -0) */
    long blank_num = 0;
    int blank_set = 0;
    long remove_num = 0;
    const char *missing = NULL;
    digit_opt_t dig;

    digit_opt_init(&dig);

    static const struct option long_opts[] = {
        {"strings", no_argument, NULL, 'l'},
        {"filenames", no_argument, NULL, 'f'},
        {"extended", no_argument, NULL, 'e'},
        {"append", no_argument, NULL, 'a'},
        {"prepend", no_argument, NULL, 'p'},
        {"remove", required_argument, NULL, 'r'},
        {"blank", required_argument, NULL, 'b'},
        {"missing", required_argument, NULL, 'm'},
        {"verbose", no_argument, NULL, 'v'},
        {"quiet", no_argument, NULL, 'q'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, OPT_VERSION},
        {NULL, 0, NULL, 0},
    };

    for (;;) {
        int c = getopt_long(argc, argv, "lfeapr:n:b:m:vqh0123456789", long_opts, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'l':
            srcmode = SRC_STRINGS;
            digit_opt_note(&dig, c);
            break;
        case 'f':
            srcmode = SRC_FILENAMES;
            digit_opt_note(&dig, c);
            break;
        case 'e':
            srcmode = SRC_EXTENDED;
            digit_opt_note(&dig, c);
            break;
        case 'a':
            posmode = POS_APPEND;
            append_back = 0;
            digit_opt_note(&dig, c);
            break;
        case 'p':
            posmode = POS_PREPEND;
            blank_after = 0;
            digit_opt_note(&dig, c);
            break;
        case 'r': {
            char *end = NULL;
            remove_num = strtol(optarg, &end, 10);
            if (!optarg[0] || (end && *end) || remove_num < 0) {
                fprintf(stderr, _("%s: invalid remove count: %s\n"), exe, optarg);
                return 1;
            }
            digit_opt_note(&dig, c);
            break;
        }
        case 'n': {
            char *end = NULL;
            long n = strtol(optarg, &end, 10);
            if (!optarg[0] || (end && *end)) {
                fprintf(stderr, _("%s: invalid line number: %s\n"), exe, optarg);
                return 1;
            }
            posmode = POS_BEFORE_LINE;
            line_no = n;
            digit_opt_note(&dig, c);
            break;
        }
        case 'b': {
            char *end = NULL;
            blank_num = strtol(optarg, &end, 10);
            if (!optarg[0] || (end && *end) || blank_num < 0) {
                fprintf(stderr, _("%s: invalid blank count: %s\n"), exe, optarg);
                return 1;
            }
            blank_set = 1;
            digit_opt_note(&dig, c);
            break;
        }
        case 'm':
            missing = optarg;
            digit_opt_note(&dig, c);
            break;
        case 'v':
            log_more();
            digit_opt_note(&dig, c);
            break;
        case 'q':
            log_less();
            digit_opt_note(&dig, c);
            break;
        case 'h':
            usage(stdout);
            return 0;
        case OPT_VERSION:
            printf("insert %s\n", PROJECT_VERSION);
            printf(_("Copyright (C) %d %s\n"), PROJECT_YEAR, PROJECT_AUTHOR);
            fputs(_("License AGPL-3.0-or-later: <https://www.gnu.org/licenses/agpl-3.0.html>\n"),
                  stdout);
            fputs(_("This is free software: you are free to change and redistribute it.\n"),
                  stdout);
            fputs(_("This project opposes AI exploitation and AI hegemony.\n"), stdout);
            fputs(_("This project rejects mindless MIT-style licensing and politically naive "
                    "BSD-style licensing.\n"),
                  stdout);
            fputs(_("There is NO WARRANTY, to the extent permitted by law.\n"), stdout);
            return 0;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            digit_opt_digit(&dig, c);
            apply_position_num(&posmode, dig.num, &line_no, &append_back, &blank_after);
            break;
        default:
            usage(stderr);
            return 1;
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        fprintf(stderr, _("%s: missing FILE\n"), exe);
        usage(stderr);
        return 1;
    }

    {
        const char *path = argv[0];
        char *payload = NULL;
        size_t payload_len = 0, payload_cap = 0;
        char *old = NULL;
        size_t old_len = 0;
        mode_t mode = 0644;
        char *neu = NULL;
        size_t neu_len = 0, neu_cap = 0;
        size_t insert_at = 0;
        size_t keep_from = 0;
        long lead_blanks = 0;
        long pad_after = 0;
        int need_nl = 0;
        int allow_pad;
        int i;
        int rc;

        if (posmode == POS_UNSET) {
            posmode = POS_APPEND;
            append_back = 0;
        }

        for (i = 1; i < argc; i++) {
            char *piece = NULL;
            size_t piece_len = 0;
            int as_file = 0;
            long nblank;

            if (load_src(exe, srcmode, argv[i], missing, &piece, &piece_len, &as_file) != 0) {
                free(payload);
                return 1;
            }
            if (buf_append(&payload, &payload_len, &payload_cap, piece, piece_len) != 0) {
                free(piece);
                free(payload);
                fprintf(stderr, _("%s: out of memory\n"), exe);
                return 1;
            }
            free(piece);

            if (blank_set) {
                nblank = blank_num;
            } else {
                nblank = as_file ? 0 : 1;
            }
            if (nblank > 0) {
                if (buf_append_ch(&payload, &payload_len, &payload_cap, '\n', (size_t)nblank) !=
                    0) {
                    free(payload);
                    fprintf(stderr, _("%s: out of memory\n"), exe);
                    return 1;
                }
            }
        }

        if (argc == 1) {
            payload = malloc(1);
            if (!payload) {
                return 1;
            }
            payload[0] = '\0';
            payload_len = 0;
        }

        /* No content + -r: do not pad when -NUM is past EOF. */
        allow_pad = !(payload_len == 0 && remove_num > 0);

        old = read_file_bytes(path, &old_len, &mode);
        if (!old) {
            fprintf(stderr, "%s: ", exe);
            perror(path);
            free(payload);
            return 1;
        }

        switch (posmode) {
        case POS_PREPEND:
            insert_at = 0;
            break;
        case POS_APPEND:
            if (append_back <= 0) {
                insert_at = old_len;
            } else {
                size_t nlines = count_lines(old, old_len, '\n');
                long ln = (long)nlines - append_back + 1;
                if (ln < 1) {
                    ln = 1;
                }
                insert_at = line_byte_offset(old, old_len, ln, '\n');
            }
            break;
        case POS_BEFORE_LINE:
        default:
            plan_before_line(old, old_len, line_no, allow_pad, &insert_at, &pad_after, &need_nl,
                             &lead_blanks);
            break;
        }

        keep_from = skip_lines(old, old_len, insert_at, remove_num);

        /*
         * Empty delete at prepend: -rN keeps from 1-based line N onward
         * (e.g. insert -0 -r3 FILE on a/b/c/d/e => c/d/e).
         * Suppress -0 trailing blank when there is no insert payload.
         */
        if (payload_len == 0 && remove_num > 0 && posmode == POS_PREPEND) {
            keep_from = line_byte_offset(old, old_len, remove_num, '\n');
            insert_at = 0;
            blank_after = 0;
            lead_blanks = 0;
            pad_after = 0;
            need_nl = 0;
        }

        /*
         * Build:
         *   lead_blanks*\n + old[0..insert_at) + [NL] + pad_after*\n
         *   + payload + blank_after*\n + old[keep_from..)
         */
        if (lead_blanks > 0) {
            if (buf_append_ch(&neu, &neu_len, &neu_cap, '\n', (size_t)lead_blanks) != 0) {
                goto oom;
            }
        }
        if (buf_append(&neu, &neu_len, &neu_cap, old, insert_at) != 0) {
            goto oom;
        }
        if (need_nl) {
            if (buf_append_ch(&neu, &neu_len, &neu_cap, '\n', 1) != 0) {
                goto oom;
            }
        }
        if (pad_after > 0) {
            if (buf_append_ch(&neu, &neu_len, &neu_cap, '\n', (size_t)pad_after) != 0) {
                goto oom;
            }
        }
        if (buf_append(&neu, &neu_len, &neu_cap, payload, payload_len) != 0) {
            goto oom;
        }
        if (blank_after > 0) {
            if (buf_append_ch(&neu, &neu_len, &neu_cap, '\n', (size_t)blank_after) != 0) {
                goto oom;
            }
        }
        if (buf_append(&neu, &neu_len, &neu_cap, old + keep_from, old_len - keep_from) != 0) {
            goto oom;
        }

        rc = replace_file_if_changed(path, old, old_len, neu, neu_len, mode);
        free(old);
        free(payload);
        free(neu);
        if (rc < 0) {
            fprintf(stderr, "%s: ", exe);
            perror(path);
            return 1;
        }
        loginfo_fmt("%s: %s %s", exe, rc ? "updated" : "unchanged", path);
        return 0;

    oom:
        fprintf(stderr, _("%s: out of memory\n"), exe);
        free(old);
        free(payload);
        free(neu);
        return 1;
    }
}
