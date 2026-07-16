#define _POSIX_C_SOURCE 200809L

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "match.h"

START_TEST(test_repl_fixed) {
    match_opts_t opts;
    match_engine_t *eng;
    const char *pats[] = {"foo"};
    char *out;
    size_t out_len = 0, n = 0;

    memset(&opts, 0, sizeof opts);
    opts.type = MATCH_FIXED;
    opts.line_sep = '\n';
    eng = match_engine_create(pats, 1, &opts, NULL);
    ck_assert_ptr_nonnull(eng);

    out = match_replace(eng, "a foo b foo", 11, "X", &out_len, &n);
    ck_assert_ptr_nonnull(out);
    ck_assert_str_eq(out, "a X b X");
    ck_assert_uint_eq(n, 2);
    free(out);
    match_engine_free(eng);
}
END_TEST

START_TEST(test_repl_word_regexp) {
    match_opts_t opts;
    match_engine_t *eng;
    const char *pats[] = {"cat"};
    char *out;
    size_t out_len = 0, n = 0;

    memset(&opts, 0, sizeof opts);
    opts.type = MATCH_FIXED;
    opts.word_regexp = 1;
    opts.line_sep = '\n';
    eng = match_engine_create(pats, 1, &opts, NULL);
    ck_assert_ptr_nonnull(eng);
    out = match_replace(eng, "cat category cat", 16, "DOG", &out_len, &n);
    ck_assert_ptr_nonnull(out);
    ck_assert_str_eq(out, "DOG category DOG");
    ck_assert_uint_eq(n, 2);
    free(out);
    match_engine_free(eng);
}
END_TEST

START_TEST(test_repl_invert_line) {
    match_opts_t opts;
    match_engine_t *eng;
    const char *pats[] = {"keep"};
    char *out;
    size_t out_len = 0, n = 0;

    memset(&opts, 0, sizeof opts);
    opts.type = MATCH_FIXED;
    opts.invert_match = 1;
    opts.line_sep = '\n';
    eng = match_engine_create(pats, 1, &opts, NULL);
    ck_assert_ptr_nonnull(eng);
    out = match_replace(eng, "keep\ndrop\nkeep\n", 15, "X", &out_len, &n);
    ck_assert_ptr_nonnull(out);
    ck_assert_str_eq(out, "keep\nX\nkeep\n");
    ck_assert_uint_eq(n, 1);
    free(out);
    match_engine_free(eng);
}
END_TEST

START_TEST(test_repl_ere) {
    match_opts_t opts;
    match_engine_t *eng;
    const char *pats[] = {"[0-9]+"};
    char *out;
    size_t out_len = 0, n = 0;
    char *err = NULL;

    memset(&opts, 0, sizeof opts);
    opts.type = MATCH_EXTENDED;
    opts.line_sep = '\n';
    eng = match_engine_create(pats, 1, &opts, &err);
    ck_assert_ptr_nonnull(eng);
    out = match_replace(eng, "a12b34c", 7, "#", &out_len, &n);
    ck_assert_ptr_nonnull(out);
    ck_assert_str_eq(out, "a#b#c");
    free(out);
    match_engine_free(eng);
}
END_TEST

START_TEST(test_repl_ignore_case) {
    match_opts_t opts;
    match_engine_t *eng;
    const char *pats[] = {"Ab"};
    char *out;
    size_t out_len = 0, n = 0;

    memset(&opts, 0, sizeof opts);
    opts.type = MATCH_FIXED;
    opts.ignore_case = 1;
    opts.line_sep = '\n';
    eng = match_engine_create(pats, 1, &opts, NULL);
    ck_assert_ptr_nonnull(eng);
    out = match_replace(eng, "xABy", 4, "Z", &out_len, &n);
    ck_assert_ptr_nonnull(out);
    ck_assert_str_eq(out, "xZy");
    free(out);
    match_engine_free(eng);
}
END_TEST

START_TEST(test_repl_line_regexp) {
    match_opts_t opts;
    match_engine_t *eng;
    const char *pats[] = {"ab"};
    char *out;
    size_t out_len = 0, n = 0;

    memset(&opts, 0, sizeof opts);
    opts.type = MATCH_FIXED;
    opts.line_regexp = 1;
    opts.line_sep = '\n';
    eng = match_engine_create(pats, 1, &opts, NULL);
    ck_assert_ptr_nonnull(eng);
    out = match_replace(eng, "ab\nxab\nab\n", 10, "Z", &out_len, &n);
    ck_assert_ptr_nonnull(out);
    ck_assert_str_eq(out, "Z\nxab\nZ\n");
    ck_assert_uint_eq(n, 2);
    free(out);
    match_engine_free(eng);
}
END_TEST

START_TEST(test_repl_pcre) {
    match_opts_t opts;
    match_engine_t *eng;
    const char *pats[] = {"\\d+"};
    char *out;
    size_t out_len = 0, n = 0;
    char *err = NULL;

    memset(&opts, 0, sizeof opts);
    opts.type = MATCH_PERL;
    opts.line_sep = '\n';
    eng = match_engine_create(pats, 1, &opts, &err);
    ck_assert_msg(eng != NULL, "pcre compile: %s", err ? err : "?");
    out = match_replace(eng, "a12b", 4, "#", &out_len, &n);
    ck_assert_ptr_nonnull(out);
    ck_assert_str_eq(out, "a#b");
    free(out);
    match_engine_free(eng);
}
END_TEST

static Suite *suite(void) {
    Suite *s = suite_create("repl_unit");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_repl_fixed);
    tcase_add_test(tc, test_repl_word_regexp);
    tcase_add_test(tc, test_repl_invert_line);
    tcase_add_test(tc, test_repl_ere);
    tcase_add_test(tc, test_repl_ignore_case);
    tcase_add_test(tc, test_repl_line_regexp);
    tcase_add_test(tc, test_repl_pcre);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    SRunner *sr = srunner_create(suite());
    int failed;

    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
