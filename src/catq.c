/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "config.h"

#include <bas/locale/i18n.h>
#include <bas/log/deflog.h>
#include <bas/proc/env.h>

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

define_logger();

static int show_all = 0;
static int number_nonblank = 0;
static int show_ends = 0;
static int number = 0;
static int squeeze_blank = 0;
static int show_tabs = 0;
static int show_nonprinting = 0;

static void print_line(const char *line, size_t len, int line_num) {
    if (number && !number_nonblank) {
        printf("%6d\t", line_num);
    } else if (number_nonblank && len > 0) {
        printf("%6d\t", line_num);
    }

    for (size_t i = 0; i < len; i++) {
        unsigned char c = line[i];
        if (show_nonprinting) {
            if (c == '\t' && show_tabs) {
                printf("^I");
            } else if (c == '\n' && show_ends) {
                printf("$\n");
            } else if (c < 32 || c == 127) {
                printf("^%c", c == 127 ? '?' : c + 64);
            } else if (c >= 128 && c < 160) {
                printf("M-^%c", c - 128 + 64);
            } else {
                putchar(c);
            }
        } else {
            if (show_tabs && c == '\t') {
                printf("^I");
            } else if (show_ends && c == '\n') {
                printf("$\n");
            } else {
                putchar(c);
            }
        }
    }
}

static int cat_file(const char *filename, int *line_num, int *blank_count) {
    FILE *f;
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    int is_stdin = (strcmp(filename, "-") == 0);

    if (is_stdin) {
        f = stdin;
    } else {
        f = fopen(filename, "r");
        if (!f) {
            /* Silently ignore missing files - this is the key difference from cat */
            return 0;
        }
    }

    while ((len = getline(&line, &cap, f)) != -1) {
        int is_blank = (len == 1 || (len == 1 && line[0] == '\n'));

        if (squeeze_blank && is_blank) {
            (*blank_count)++;
            if (*blank_count > 1) {
                continue;
            }
        } else {
            *blank_count = 0;
        }

        if (!is_blank || (is_blank && !number_nonblank)) {
            (*line_num)++;
        }

        print_line(line, len, *line_num);
    }

    free(line);
    if (!is_stdin) {
        fclose(f);
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *exe = self_exe();
    init_i18n(LOCALEDIR);

    int line_num = 0;
    int blank_count = 0;

    static const struct option long_opts[] = {
        {"show-all", no_argument, NULL, 'A'},
        {"number-nonblank", no_argument, NULL, 'b'},
        {"show-ends", no_argument, NULL, 'E'},
        {"number", no_argument, NULL, 'n'},
        {"squeeze-blank", no_argument, NULL, 's'},
        {"show-tabs", no_argument, NULL, 'T'},
        {"show-nonprinting", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0},
    };

    for (;;) {
        int c = getopt_long(argc, argv, "AbEnstvhu", long_opts, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'A':
            show_all = 1;
            show_nonprinting = 1;
            show_ends = 1;
            show_tabs = 1;
            break;
        case 'b':
            number_nonblank = 1;
            number = 1;
            break;
        case 'E':
            show_ends = 1;
            break;
        case 'n':
            number = 1;
            break;
        case 's':
            squeeze_blank = 1;
            break;
        case 't':
            show_tabs = 1;
            show_nonprinting = 1;
            break;
        case 'T':
            show_tabs = 1;
            break;
        case 'v':
            show_nonprinting = 1;
            break;
        case 'u':
            /* ignored for compatibility */
            break;
        case 'h':
            printf(_("Usage: %s [OPTION]... [FILE]...\n"), exe);
            fputs(_("Concatenate FILE(s) to standard output.\n"), stdout);
            fputs(_("\nWith no FILE, or when FILE is -, read standard input.\n"), stdout);
            fputs(_("\n  -A, --show-all           equivalent to -vET\n"), stdout);
            fputs(_("  -b, --number-nonblank    number nonempty output lines\n"), stdout);
            fputs(_("  -e                       equivalent to -vE\n"), stdout);
            fputs(_("  -E, --show-ends          display $ at end of each line\n"), stdout);
            fputs(_("  -n, --number             number all output lines\n"), stdout);
            fputs(_("  -s, --squeeze-blank      suppress repeated empty output lines\n"), stdout);
            fputs(_("  -t                       equivalent to -vT\n"), stdout);
            fputs(_("  -T, --show-tabs          display TAB characters as ^I\n"), stdout);
            fputs(_("  -u                       (ignored)\n"), stdout);
            fputs(_("  -v, --show-nonprinting   use ^ and M- notation\n"), stdout);
            fputs(_("      --help               display this help and exit\n"), stdout);
            fputs(_("      --version            output version information and exit\n"), stdout);
            fputs(_("\nLike cat(1), but silently ignores missing files without error.\n"), stdout);
            printf(_("\nReport bugs to: <%s>\n"), PROJECT_EMAIL);
            return 0;
        case 'V':
            printf("catq %s\n", PROJECT_VERSION);
            printf(_("Copyright (C) %d %s\n"), PROJECT_YEAR, PROJECT_AUTHOR);
            fputs(_("License AGPL-3.0-or-later: <https://www.gnu.org/licenses/agpl-3.0.html>\n"),
                  stdout);
            return 0;
        default:
            fprintf(stderr, _("Try '%s --help' for more information.\n"), exe);
            return 1;
        }
    }

    argc -= optind;
    argv += optind;

    if (argc == 0) {
        cat_file("-", &line_num, &blank_count);
    } else {
        for (int i = 0; i < argc; i++) {
            cat_file(argv[i], &line_num, &blank_count);
        }
    }

    return 0;
}
