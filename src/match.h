#ifndef MATCH_H
#define MATCH_H

#include <stddef.h>

typedef enum {
    MATCH_FIXED = 0, /* -F default */
    MATCH_BASIC,     /* -G BRE */
    MATCH_EXTENDED,  /* -E ERE */
    MATCH_PERL,      /* -P PCRE2 */
} match_type_t;

typedef struct match_opts {
    match_type_t type;
    int ignore_case;   /* -i */
    int word_regexp;   /* -w */
    int line_regexp;   /* -x */
    int invert_match;  /* -v: replace non-matching lines entirely */
    char line_sep;     /* '\n' or '\0' for -z */
    int line_mode;     /* -l: expand match to entire line */
    int replace_mode;  /* 0=global (default), 1=first only (-1) */
    int range_n;       /* -n N: start from N-th occurrence (1-based) */
    int range_m;       /* -n N..M: end at M-th occurrence (-1 for inf) */
} match_opts_t;

typedef struct match_engine match_engine_t;

/* patterns: array of pattern strings; n_patterns >= 1 for normal use.
 * With zero patterns, nothing matches (grep empty -f behavior).
 */
match_engine_t *match_engine_create(const char **patterns, size_t n_patterns,
                                    const match_opts_t *opts, char **err);
void match_engine_free(match_engine_t *eng);

/*
 * Replace in data according to engine + replacement.
 * On success returns malloc'd result; sets *out_len and *n_repl (number of replacements).
 * On error returns NULL.
 *
 * Without invert: each match of any pattern is replaced by `replacement`.
 * With invert: each line that does not match is replaced by `replacement` (+ line_sep if
 * the original line had a separator, except possibly the last line).
 */
char *match_replace(const match_engine_t *eng, const char *data, size_t len,
                    const char *replacement, size_t *out_len, size_t *n_repl);

/* Return 1 if any pattern matches the whole line (for invert / filters). */
int match_line_matches(const match_engine_t *eng, const char *line, size_t len);

#endif /* MATCH_H */
