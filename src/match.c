/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L
#define PCRE2_CODE_UNIT_WIDTH 8

#include "match.h"

#include "edit_file.h"

#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

#include <pcre2.h>

struct pattern_slot {
    match_type_t type;
    char *fixed; /* for FIXED: pattern text (possibly lowercased copy for -i) */
    size_t fixed_len;
    regex_t preg;
    int preg_ok;
    pcre2_code *re;
};

struct match_engine {
    match_opts_t opts;
    struct pattern_slot *slots;
    size_t n;
};

static int is_word_char(unsigned char c) {
    return isalnum(c) || c == '_';
}

static int at_word_start(const char *data, size_t len, size_t off) {
    (void)len;
    if (off == 0) {
        return 1;
    }
    return !is_word_char((unsigned char)data[off - 1]);
}

static int at_word_end(const char *data, size_t len, size_t off) {
    if (off >= len) {
        return 1;
    }
    return !is_word_char((unsigned char)data[off]);
}

static char *str_tolower_dup(const char *s, size_t n) {
    char *d = malloc(n + 1);
    size_t i;

    if (!d) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        d[i] = (char)tolower((unsigned char)s[i]);
    }
    d[n] = '\0';
    return d;
}

static int fixed_find(const struct pattern_slot *slot, const match_opts_t *opts, const char *data,
                      size_t len, size_t start, size_t *m_off, size_t *m_len) {
    size_t i;

    if (slot->fixed_len == 0) {
        /* empty pattern matches at start (like grep) — treat as match of length 0 at start */
        if (start <= len) {
            *m_off = start;
            *m_len = 0;
            return 1;
        }
        return 0;
    }

    if (opts->ignore_case) {
        char *hay = str_tolower_dup(data + start, len - start);
        const char *p;

        if (!hay) {
            return 0;
        }
        p = strstr(hay, slot->fixed);
        if (!p) {
            free(hay);
            return 0;
        }
        *m_off = start + (size_t)(p - hay);
        *m_len = slot->fixed_len;
        free(hay);
    } else {
        const char *p = data + start;
        const char *end = data + len;
        const char *found = NULL;

        while (p + slot->fixed_len <= end) {
            if (memcmp(p, slot->fixed, slot->fixed_len) == 0) {
                found = p;
                break;
            }
            p++;
        }
        if (!found) {
            return 0;
        }
        *m_off = (size_t)(found - data);
        *m_len = slot->fixed_len;
    }

    if (opts->line_regexp) {
        size_t line_start = *m_off;
        size_t line_end = *m_off + *m_len;

        while (line_start > 0 && data[line_start - 1] != opts->line_sep) {
            line_start--;
        }
        while (line_end < len && data[line_end] != opts->line_sep) {
            line_end++;
        }
        if (line_start != *m_off || line_end != *m_off + *m_len) {
            /* retry after this match */
            size_t next = *m_off + (*m_len ? *m_len : 1);
            if (next > len) {
                return 0;
            }
            return fixed_find(slot, opts, data, len, next, m_off, m_len);
        }
    }

    if (opts->word_regexp) {
        if (!at_word_start(data, len, *m_off) || !at_word_end(data, len, *m_off + *m_len)) {
            size_t next = *m_off + (*m_len ? *m_len : 1);
            if (next > len) {
                return 0;
            }
            return fixed_find(slot, opts, data, len, next, m_off, m_len);
        }
    }
    return 1;
}

static int regex_find(const struct pattern_slot *slot, const match_opts_t *opts, const char *data,
                      size_t len, size_t start, size_t *m_off, size_t *m_len) {
    regmatch_t m;
    int flags = 0;
    const char *search = data + start;
    size_t search_len = len - start;
    char *tmp = NULL;
    int rc;

    if (opts->line_regexp) {
        /* Match against each line separately — handled by caller for -x via whole line */
    }

    /* regexec needs NUL-terminated; copy remaining if needed */
    if (memchr(search, '\0', search_len)) {
        /* embedded NUL: only search up to first NUL for POSIX regex */
        search_len = (size_t)(memchr(search, '\0', search_len) - (const void *)search);
    }
    tmp = malloc(search_len + 1);
    if (!tmp) {
        return 0;
    }
    memcpy(tmp, search, search_len);
    tmp[search_len] = '\0';

    rc = regexec(&slot->preg, tmp, 1, &m, flags);
    free(tmp);
    if (rc != 0) {
        return 0;
    }
    *m_off = start + (size_t)m.rm_so;
    *m_len = (size_t)(m.rm_eo - m.rm_so);

    if (opts->word_regexp) {
        if (!at_word_start(data, len, *m_off) || !at_word_end(data, len, *m_off + *m_len)) {
            size_t next = *m_off + (*m_len ? *m_len : 1);
            if (next > len) {
                return 0;
            }
            return regex_find(slot, opts, data, len, next, m_off, m_len);
        }
    }
    if (opts->line_regexp) {
        size_t line_start = *m_off;
        size_t line_end = *m_off + *m_len;

        while (line_start > 0 && data[line_start - 1] != opts->line_sep) {
            line_start--;
        }
        while (line_end < len && data[line_end] != opts->line_sep) {
            line_end++;
        }
        if (line_start != *m_off || line_end != *m_off + *m_len) {
            size_t next = *m_off + (*m_len ? *m_len : 1);
            if (next > len) {
                return 0;
            }
            return regex_find(slot, opts, data, len, next, m_off, m_len);
        }
    }
    (void)opts;
    return 1;
}

static int pcre_find(const struct pattern_slot *slot, const match_opts_t *opts, const char *data,
                     size_t len, size_t start, size_t *m_off, size_t *m_len) {
    pcre2_match_data *md;
    int rc;
    PCRE2_SIZE *ovector;
    uint32_t options = 0;

    md = pcre2_match_data_create_from_pattern(slot->re, NULL);
    if (!md) {
        return 0;
    }
    rc = pcre2_match(slot->re, (PCRE2_SPTR)data, (PCRE2_SIZE)len, (PCRE2_SIZE)start, options, md,
                     NULL);
    if (rc < 0) {
        pcre2_match_data_free(md);
        return 0;
    }
    ovector = pcre2_get_ovector_pointer(md);
    *m_off = (size_t)ovector[0];
    *m_len = (size_t)(ovector[1] - ovector[0]);
    pcre2_match_data_free(md);

    if (opts->word_regexp) {
        if (!at_word_start(data, len, *m_off) || !at_word_end(data, len, *m_off + *m_len)) {
            size_t next = *m_off + (*m_len ? *m_len : 1);
            if (next > len) {
                return 0;
            }
            return pcre_find(slot, opts, data, len, next, m_off, m_len);
        }
    }
    if (opts->line_regexp) {
        size_t line_start = *m_off;
        size_t line_end = *m_off + *m_len;

        while (line_start > 0 && data[line_start - 1] != opts->line_sep) {
            line_start--;
        }
        while (line_end < len && data[line_end] != opts->line_sep) {
            line_end++;
        }
        if (line_start != *m_off || line_end != *m_off + *m_len) {
            size_t next = *m_off + (*m_len ? *m_len : 1);
            if (next > len) {
                return 0;
            }
            return pcre_find(slot, opts, data, len, next, m_off, m_len);
        }
    }
    return 1;
}

static int slot_find(const struct pattern_slot *slot, const match_opts_t *opts, const char *data,
                     size_t len, size_t start, size_t *m_off, size_t *m_len) {
    switch (slot->type) {
    case MATCH_FIXED:
        return fixed_find(slot, opts, data, len, start, m_off, m_len);
    case MATCH_BASIC:
    case MATCH_EXTENDED:
        return regex_find(slot, opts, data, len, start, m_off, m_len);
    case MATCH_PERL:
        return pcre_find(slot, opts, data, len, start, m_off, m_len);
    }
    return 0;
}

static int find_any(const match_engine_t *eng, const char *data, size_t len, size_t start,
                    size_t *m_off, size_t *m_len) {
    size_t best_off = (size_t)-1;
    size_t best_len = 0;
    size_t i;
    int found = 0;

    for (i = 0; i < eng->n; i++) {
        size_t off, mlen;
        if (slot_find(&eng->slots[i], &eng->opts, data, len, start, &off, &mlen)) {
            if (!found || off < best_off || (off == best_off && mlen > best_len)) {
                best_off = off;
                best_len = mlen;
                found = 1;
            }
        }
    }
    if (!found) {
        return 0;
    }
    *m_off = best_off;
    *m_len = best_len;
    return 1;
}

int match_line_matches(const match_engine_t *eng, const char *line, size_t len) {
    size_t off, mlen;

    if (!find_any(eng, line, len, 0, &off, &mlen)) {
        return 0;
    }
    if (eng->opts.line_regexp) {
        return off == 0 && mlen == len;
    }
    return 1;
}

static int line_matches_any(const match_engine_t *eng, const char *line, size_t len) {
    return match_line_matches(eng, line, len);
}

match_engine_t *match_engine_create(const char **patterns, size_t n_patterns,
                                    const match_opts_t *opts, char **err) {
    match_engine_t *eng;
    size_t i;

    if (err) {
        *err = NULL;
    }
    eng = calloc(1, sizeof(*eng));
    if (!eng) {
        return NULL;
    }
    eng->opts = *opts;
    eng->n = n_patterns;
    if (n_patterns == 0) {
        return eng;
    }
    eng->slots = calloc(n_patterns, sizeof(*eng->slots));
    if (!eng->slots) {
        free(eng);
        return NULL;
    }

    for (i = 0; i < n_patterns; i++) {
        struct pattern_slot *s = &eng->slots[i];
        const char *pat = patterns[i] ? patterns[i] : "";

        s->type = opts->type;
        if (opts->type == MATCH_FIXED) {
            s->fixed_len = strlen(pat);
            if (opts->ignore_case) {
                s->fixed = str_tolower_dup(pat, s->fixed_len);
            } else {
                s->fixed = strdup(pat);
            }
            if (!s->fixed) {
                match_engine_free(eng);
                return NULL;
            }
        } else if (opts->type == MATCH_PERL) {
            int errorcode;
            PCRE2_SIZE erroffset;
            uint32_t options = PCRE2_UTF;
            char *wrap = NULL;
            const char *use_pat = pat;

            if (opts->ignore_case) {
                options |= PCRE2_CASELESS;
            }
            if (opts->line_regexp) {
                size_t plen = strlen(pat);
                wrap = malloc(plen + 3);
                if (!wrap) {
                    match_engine_free(eng);
                    return NULL;
                }
                wrap[0] = '^';
                memcpy(wrap + 1, pat, plen);
                wrap[plen + 1] = '$';
                wrap[plen + 2] = '\0';
                use_pat = wrap;
            }
            s->re = pcre2_compile((PCRE2_SPTR)use_pat, PCRE2_ZERO_TERMINATED, options, &errorcode,
                                  &erroffset, NULL);
            free(wrap);
            if (!s->re) {
                PCRE2_UCHAR buffer[256];
                pcre2_get_error_message(errorcode, buffer, sizeof buffer);
                if (err) {
                    *err = strdup((char *)buffer);
                }
                match_engine_free(eng);
                return NULL;
            }
        } else {
            int cflags = 0;
            char *wrap = NULL;
            const char *use_pat = pat;
            int rc;

            if (opts->type == MATCH_EXTENDED) {
                cflags |= REG_EXTENDED;
            }
            if (opts->ignore_case) {
                cflags |= REG_ICASE;
            }
            if (opts->line_regexp) {
                size_t plen = strlen(pat);
                wrap = malloc(plen + 3);
                if (!wrap) {
                    match_engine_free(eng);
                    return NULL;
                }
                wrap[0] = '^';
                memcpy(wrap + 1, pat, plen);
                wrap[plen + 1] = '$';
                wrap[plen + 2] = '\0';
                use_pat = wrap;
            }
            rc = regcomp(&s->preg, use_pat, cflags);
            free(wrap);
            if (rc != 0) {
                char buf[256];
                regerror(rc, &s->preg, buf, sizeof buf);
                if (err) {
                    *err = strdup(buf);
                }
                match_engine_free(eng);
                return NULL;
            }
            s->preg_ok = 1;
        }
    }
    return eng;
}

void match_engine_free(match_engine_t *eng) {
    size_t i;

    if (!eng) {
        return;
    }
    for (i = 0; i < eng->n; i++) {
        struct pattern_slot *s = &eng->slots[i];
        free(s->fixed);
        if (s->preg_ok) {
            regfree(&s->preg);
        }
        if (s->re) {
            pcre2_code_free(s->re);
        }
    }
    free(eng->slots);
    free(eng);
}

static char *replace_invert(const match_engine_t *eng, const char *data, size_t len,
                            const char *replacement, size_t *out_len, size_t *n_repl) {
    char *out = NULL;
    size_t out_l = 0, out_c = 0;
    size_t i = 0;
    size_t n = 0;
    size_t rlen = strlen(replacement);
    char sep = eng->opts.line_sep;

    while (i <= len) {
        size_t line_start = i;
        size_t line_end = i;
        int has_sep = 0;

        while (line_end < len && data[line_end] != sep) {
            line_end++;
        }
        if (line_end < len && data[line_end] == sep) {
            has_sep = 1;
        }

        /* empty file: one empty line consideration */
        if (line_start == len && len > 0) {
            break;
        }

        {
            size_t llen = line_end - line_start;
            int matches = line_matches_any(eng, data + line_start, llen);

            if (!matches) {
                if (buf_append(&out, &out_l, &out_c, replacement, rlen) != 0) {
                    free(out);
                    return NULL;
                }
                n++;
            } else {
                if (buf_append(&out, &out_l, &out_c, data + line_start, llen) != 0) {
                    free(out);
                    return NULL;
                }
            }
            if (has_sep) {
                if (buf_append_ch(&out, &out_l, &out_c, sep, 1) != 0) {
                    free(out);
                    return NULL;
                }
                i = line_end + 1;
            } else {
                break;
            }
        }
        if (line_start == len) {
            break;
        }
    }

    if (n_repl) {
        *n_repl = n;
    }
    if (out_len) {
        *out_len = out_l;
    }
    if (!out) {
        out = malloc(1);
        if (out) {
            out[0] = '\0';
        }
    }
    return out;
}

static char *replace_line_based(const match_engine_t *eng, const char *data, size_t len,
                                const char *replacement, size_t *out_len, size_t *n_repl) {
    char *out = NULL;
    size_t out_l = 0, out_c = 0;
    size_t i = 0;
    size_t total_n = 0;
    size_t rlen = strlen(replacement);
    char sep = eng->opts.line_sep;
    int range_n = eng->opts.range_n > 0 ? eng->opts.range_n : 1;
    int range_m = eng->opts.range_m > 0 ? eng->opts.range_m : -1;

    while (i <= len) {
        size_t line_start = i;
        size_t line_end = i;
        int has_sep = 0;

        while (line_end < len && data[line_end] != sep) {
            line_end++;
        }
        if (line_end < len && data[line_end] == sep) {
            has_sep = 1;
        }

        /* empty file: one empty line consideration */
        if (line_start == len && len > 0) {
            break;
        }

        {
            size_t llen = line_end - line_start;
            char *line_out = NULL;
            size_t line_out_l = 0, line_out_c = 0;
            size_t pos = 0;
            size_t line_occurrence = 0;

            while (pos <= llen) {
                size_t m_off, m_len;
                if (!find_any(eng, data + line_start, llen, pos, &m_off, &m_len)) {
                    if (buf_append(&line_out, &line_out_l, &line_out_c, 
                                  data + line_start + pos, llen - pos) != 0) {
                        free(line_out);
                        free(out);
                        return NULL;
                    }
                    break;
                }

                line_occurrence++;
                int should_replace = 0;
                
                if (eng->opts.replace_mode == 1) {
                    /* -1: first only */
                    should_replace = (line_occurrence == 1);
                } else {
                    /* global (default): check range */
                    should_replace = (line_occurrence >= range_n && 
                                     (range_m < 0 || line_occurrence <= range_m));
                }

                if (should_replace) {
                    if (eng->opts.line_mode) {
                        /* -l: expand to entire line */
                        if (buf_append(&line_out, &line_out_l, &line_out_c, 
                                      replacement, rlen) != 0) {
                            free(line_out);
                            free(out);
                            return NULL;
                        }
                        total_n++;
                        pos = llen; /* skip rest of line */
                        break;
                    } else {
                        if (buf_append(&line_out, &line_out_l, &line_out_c, 
                                      data + line_start + pos, m_off - pos) != 0) {
                            free(line_out);
                            free(out);
                            return NULL;
                        }
                        if (buf_append(&line_out, &line_out_l, &line_out_c, 
                                      replacement, rlen) != 0) {
                            free(line_out);
                            free(out);
                            return NULL;
                        }
                        total_n++;
                    }
                } else {
                    if (buf_append(&line_out, &line_out_l, &line_out_c, 
                                  data + line_start + pos, m_off - pos) != 0) {
                        free(line_out);
                        free(out);
                        return NULL;
                    }
                    if (buf_append(&line_out, &line_out_l, &line_out_c, 
                                  data + line_start + m_off, m_len) != 0) {
                        free(line_out);
                        free(out);
                        return NULL;
                    }
                }

                if (m_len == 0) {
                    if (m_off < llen) {
                        if (buf_append(&line_out, &line_out_l, &line_out_c, 
                                      data + line_start + m_off, 1) != 0) {
                            free(line_out);
                            free(out);
                            return NULL;
                        }
                        pos = m_off + 1;
                    } else {
                        break;
                    }
                } else {
                    pos = m_off + m_len;
                }
                if (pos > llen) {
                    break;
                }
                if (pos == llen) {
                    break;
                }
            }

            if (buf_append(&out, &out_l, &out_c, line_out, line_out_l) != 0) {
                free(line_out);
                free(out);
                return NULL;
            }
            free(line_out);

            if (has_sep) {
                if (buf_append_ch(&out, &out_l, &out_c, sep, 1) != 0) {
                    free(out);
                    return NULL;
                }
                i = line_end + 1;
            } else {
                break;
            }
        }
        if (line_start == len) {
            break;
        }
    }

    if (n_repl) {
        *n_repl = total_n;
    }
    if (out_len) {
        *out_len = out_l;
    }
    if (!out) {
        out = malloc(1);
        if (out) {
            out[0] = '\0';
        }
    }
    return out;
}

char *match_replace(const match_engine_t *eng, const char *data, size_t len,
                    const char *replacement, size_t *out_len, size_t *n_repl) {
    char *out = NULL;
    size_t out_l = 0, out_c = 0;
    size_t pos = 0;
    size_t n = 0;
    size_t rlen;

    if (!replacement) {
        replacement = "";
    }
    rlen = strlen(replacement);

    if (eng->opts.invert_match) {
        return replace_invert(eng, data, len, replacement, out_len, n_repl);
    }

    /* Use line-based replacement if any of the new options are set */
    if (eng->opts.line_mode || eng->opts.replace_mode == 1 || 
        eng->opts.range_n > 0 || eng->opts.range_m > 0) {
        return replace_line_based(eng, data, len, replacement, out_len, n_repl);
    }

    if (eng->n == 0) {
        /* no patterns => no matches */
        out = malloc(len + 1);
        if (!out) {
            return NULL;
        }
        memcpy(out, data, len);
        out[len] = '\0';
        if (out_len) {
            *out_len = len;
        }
        if (n_repl) {
            *n_repl = 0;
        }
        return out;
    }

    while (pos <= len) {
        size_t m_off, m_len;
        if (!find_any(eng, data, len, pos, &m_off, &m_len)) {
            if (buf_append(&out, &out_l, &out_c, data + pos, len - pos) != 0) {
                free(out);
                return NULL;
            }
            break;
        }
        if (buf_append(&out, &out_l, &out_c, data + pos, m_off - pos) != 0) {
            free(out);
            return NULL;
        }
        if (buf_append(&out, &out_l, &out_c, replacement, rlen) != 0) {
            free(out);
            return NULL;
        }
        n++;
        if (m_len == 0) {
            /* avoid infinite loop on empty match */
            if (m_off < len) {
                if (buf_append(&out, &out_l, &out_c, data + m_off, 1) != 0) {
                    free(out);
                    return NULL;
                }
                pos = m_off + 1;
            } else {
                break;
            }
        } else {
            pos = m_off + m_len;
        }
        if (pos > len) {
            break;
        }
        if (pos == len) {
            /* matched at end */
            break;
        }
    }

    if (n_repl) {
        *n_repl = n;
    }
    if (out_len) {
        *out_len = out_l;
    }
    if (!out) {
        out = malloc(1);
        if (out) {
            out[0] = '\0';
        }
    }
    return out;
}
