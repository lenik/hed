#define _POSIX_C_SOURCE 200809L

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "digit_opt.h"
#include "edit_file.h"
#include "lines.h"

START_TEST(test_digit_accumulate) {
    digit_opt_t d;
    digit_opt_init(&d);

    digit_opt_digit(&d, '2');
    ck_assert_int_eq(d.num, 2);
    digit_opt_digit(&d, '0');
    ck_assert_int_eq(d.num, 20);

    digit_opt_note(&d, 'a');
    digit_opt_digit(&d, '1');
    ck_assert_int_eq(d.num, 1);

    digit_opt_note(&d, 'p');
    digit_opt_digit(&d, '0');
    digit_opt_digit(&d, '7');
    ck_assert_int_eq(d.num, 7);
}
END_TEST

START_TEST(test_line_byte_offset) {
    const char *t = "a\nbb\nccc\n";
    ck_assert_uint_eq(line_byte_offset(t, strlen(t), 1, '\n'), 0);
    ck_assert_uint_eq(line_byte_offset(t, strlen(t), 2, '\n'), 2);
    ck_assert_uint_eq(line_byte_offset(t, strlen(t), 3, '\n'), 5);
    ck_assert_uint_eq(line_byte_offset(t, strlen(t), 99, '\n'), strlen(t));
    ck_assert_uint_eq(count_lines(t, strlen(t), '\n'), 3);
}
END_TEST

START_TEST(test_text_prefix_len) {
    const char *t = "ab\x01""cd";
    ck_assert_uint_eq(text_prefix_len(t, 5), 2);
    ck_assert_uint_eq(text_prefix_len("ok\n", 3), 3);
}
END_TEST

START_TEST(test_replace_file_unchanged) {
    char path[] = "/tmp/hed-edit-XXXXXX";
    int fd = mkstemp(path);
    const char *text = "hello\n";
    mode_t mode = 0644;
    char *old;
    size_t len;
    int rc;

    ck_assert_int_ge(fd, 0);
    ck_assert_int_eq((int)write(fd, text, strlen(text)), (int)strlen(text));
    close(fd);

    old = read_file_bytes(path, &len, &mode);
    ck_assert_ptr_nonnull(old);
    ck_assert_uint_eq(len, strlen(text));

    rc = replace_file_if_changed(path, old, len, text, strlen(text), mode);
    ck_assert_int_eq(rc, 0);

    {
        char *neu = NULL;
        size_t nlen = 0, ncap = 0;
        ck_assert_int_eq(buf_append(&neu, &nlen, &ncap, "x", 1), 0);
        ck_assert_int_eq(buf_append(&neu, &nlen, &ncap, text, strlen(text)), 0);
        rc = replace_file_if_changed(path, old, len, neu, nlen, mode);
        ck_assert_int_eq(rc, 1);
        free(neu);
    }

    free(old);
    unlink(path);
}
END_TEST

static Suite *suite(void) {
    Suite *s = suite_create("insert_unit");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_digit_accumulate);
    tcase_add_test(tc, test_line_byte_offset);
    tcase_add_test(tc, test_text_prefix_len);
    tcase_add_test(tc, test_replace_file_unchanged);
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
