#define _POSIX_C_SOURCE 200809L

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *insert_bin(void) {
    const char *p = getenv("HED_INSERT");
    ck_assert_ptr_nonnull(p);
    return p;
}

static char *read_all(const char *path) {
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    ck_assert_ptr_nonnull(f);
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    buf = malloc((size_t)n + 1);
    ck_assert_ptr_nonnull(buf);
    ck_assert_int_eq((int)fread(buf, 1, (size_t)n, f), (int)n);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void rm_rf(const char *dir) {
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    system(cmd);
}

static int run_insert(char *const argv[]) {
    pid_t pid = fork();
    int status;

    ck_assert_int_ge(pid, 0);
    if (pid == 0) {
        execv(argv[0], argv);
        _exit(127);
    }
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

START_TEST(test_cli_prepend) {
    char dir[] = "/tmp/hed-ins-XXXXXX";
    char path[128];
    char *out;
    char *argv[6];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("a\nb\n", f);
        fclose(f);
    }

    argv[0] = (char *)insert_bin();
    argv[1] = "-1";
    argv[2] = path;
    argv[3] = "HEAD";
    argv[4] = NULL;
    ck_assert_int_eq(run_insert(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "HEAD\na\nb\n");
    free(out);
    rm_rf(dir);
}
END_TEST

START_TEST(test_cli_before_line) {
    char dir[] = "/tmp/hed-ins-XXXXXX";
    char path[128];
    char *out;
    char *argv[6];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("1\n2\n3\n4\n", f);
        fclose(f);
    }

    argv[0] = (char *)insert_bin();
    argv[1] = "-3";
    argv[2] = path;
    argv[3] = "X";
    argv[4] = NULL;
    ck_assert_int_eq(run_insert(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "1\n2\nX\n3\n4\n");
    free(out);
    rm_rf(dir);
}
END_TEST

START_TEST(test_cli_pad_past_eof) {
    char dir[] = "/tmp/hed-ins-XXXXXX";
    char path[128];
    char *out;
    char *argv[6];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("a\nb\n", f);
        fclose(f);
    }

    argv[0] = (char *)insert_bin();
    argv[1] = "-5";
    argv[2] = path;
    argv[3] = "X";
    argv[4] = NULL;
    ck_assert_int_eq(run_insert(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "a\nb\n\n\nX\n");
    free(out);
    rm_rf(dir);
}
END_TEST

START_TEST(test_cli_append_before_last) {
    char dir[] = "/tmp/hed-ins-XXXXXX";
    char path[128];
    char *out;
    char *argv[6];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("a\nb\nfoot\n", f);
        fclose(f);
    }

    argv[0] = (char *)insert_bin();
    argv[1] = "-a1";
    argv[2] = path;
    argv[3] = "NEW";
    argv[4] = NULL;
    ck_assert_int_eq(run_insert(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "a\nb\nNEW\nfoot\n");
    free(out);
    rm_rf(dir);
}
END_TEST

START_TEST(test_cli_blank_file_default_zero) {
    char dir[] = "/tmp/hed-ins-XXXXXX";
    char path[128];
    char src[128];
    char atsrc[130];
    char *out;
    char *argv[6];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    snprintf(src, sizeof src, "%s/s.txt", dir);
    snprintf(atsrc, sizeof atsrc, "@%s", src);
    {
        FILE *f = fopen(path, "w");
        fputs("base\n", f);
        fclose(f);
        f = fopen(src, "w");
        fputs("SNIP", f);
        fclose(f);
    }

    argv[0] = (char *)insert_bin();
    argv[1] = "-p";
    argv[2] = path;
    argv[3] = atsrc;
    argv[4] = NULL;
    ck_assert_int_eq(run_insert(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "SNIPbase\n");
    free(out);
    rm_rf(dir);
}
END_TEST

START_TEST(test_cli_blank_override) {
    char dir[] = "/tmp/hed-ins-XXXXXX";
    char path[128];
    char src[128];
    char atsrc[130];
    char *out;
    char *argv[9];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    snprintf(src, sizeof src, "%s/s.txt", dir);
    snprintf(atsrc, sizeof atsrc, "@%s", src);
    {
        FILE *f = fopen(path, "w");
        fputs("Z\n", f);
        fclose(f);
        f = fopen(src, "w");
        fputs("F", f);
        fclose(f);
    }

    argv[0] = (char *)insert_bin();
    argv[1] = "-p";
    argv[2] = "-b";
    argv[3] = "2";
    argv[4] = path;
    argv[5] = "S";
    argv[6] = atsrc;
    argv[7] = NULL;
    ck_assert_int_eq(run_insert(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "S\n\nF\n\nZ\n");
    free(out);
    rm_rf(dir);
}
END_TEST

START_TEST(test_cli_zero_prepend_blank) {
    char dir[] = "/tmp/hed-ins-XXXXXX";
    char path[128];
    char *out;
    char *argv[6];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("a\n", f);
        fclose(f);
    }

    argv[0] = (char *)insert_bin();
    argv[1] = "-0";
    argv[2] = path;
    argv[3] = "H";
    argv[4] = NULL;
    ck_assert_int_eq(run_insert(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "H\n\na\n");
    free(out);
    rm_rf(dir);
}
END_TEST

START_TEST(test_cli_remove_delete_lines) {
    char dir[] = "/tmp/hed-ins-XXXXXX";
    char path[128];
    char *out;
    char *argv[6];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("1\n2\n3\n4\n5\n", f);
        fclose(f);
    }

    argv[0] = (char *)insert_bin();
    argv[1] = "-3";
    argv[2] = "-r";
    argv[3] = "2";
    argv[4] = path;
    argv[5] = NULL;
    ck_assert_int_eq(run_insert(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "1\n2\n5\n");
    free(out);
    rm_rf(dir);
}
END_TEST

START_TEST(test_cli_update_shebang) {
    char dir[] = "/tmp/hed-ins-XXXXXX";
    char path[128];
    char *out;
    char *argv[7];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.sh", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("#!/bin/sh\necho hi\n", f);
        fclose(f);
    }

    argv[0] = (char *)insert_bin();
    argv[1] = "-p";
    argv[2] = "-r";
    argv[3] = "1";
    argv[4] = path;
    argv[5] = "#!/bin/whale";
    argv[6] = NULL;
    ck_assert_int_eq(run_insert(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "#!/bin/whale\necho hi\n");
    free(out);
    rm_rf(dir);
}
END_TEST

START_TEST(test_cli_update_footer) {
    char dir[] = "/tmp/hed-ins-XXXXXX";
    char path[128];
    char *out;
    char *argv[7];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("body\noldfoot\n", f);
        fclose(f);
    }

    argv[0] = (char *)insert_bin();
    argv[1] = "-a1";
    argv[2] = "-r";
    argv[3] = "1";
    argv[4] = path;
    argv[5] = "newfoot";
    argv[6] = NULL;
    ck_assert_int_eq(run_insert(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "body\nnewfoot\n");
    free(out);
    rm_rf(dir);
}
END_TEST

START_TEST(test_cli_remove_outofrange_no_pad) {
    char dir[] = "/tmp/hed-ins-XXXXXX";
    char path[128];
    char *out;
    char *argv[6];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("a\nb\n", f);
        fclose(f);
    }

    argv[0] = (char *)insert_bin();
    argv[1] = "-20";
    argv[2] = "-r";
    argv[3] = "1";
    argv[4] = path;
    argv[5] = NULL;
    ck_assert_int_eq(run_insert(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "a\nb\n");
    free(out);
    rm_rf(dir);
}
END_TEST

START_TEST(test_cli_zero_remove_keep_from_line) {
    char dir[] = "/tmp/hed-ins-XXXXXX";
    char path[128];
    char *out;
    char *argv[5];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/foo", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("a\nb\nc\nd\ne\n", f);
        fclose(f);
    }

    argv[0] = (char *)insert_bin();
    argv[1] = "-0";
    argv[2] = "-r3";
    argv[3] = path;
    argv[4] = NULL;
    ck_assert_int_eq(run_insert(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "c\nd\ne\n");
    free(out);
    rm_rf(dir);
}
END_TEST

static Suite *suite(void) {
    Suite *s = suite_create("insert_cli");
    TCase *tc = tcase_create("cli");

    tcase_add_test(tc, test_cli_prepend);
    tcase_add_test(tc, test_cli_before_line);
    tcase_add_test(tc, test_cli_pad_past_eof);
    tcase_add_test(tc, test_cli_append_before_last);
    tcase_add_test(tc, test_cli_blank_file_default_zero);
    tcase_add_test(tc, test_cli_blank_override);
    tcase_add_test(tc, test_cli_zero_prepend_blank);
    tcase_add_test(tc, test_cli_remove_delete_lines);
    tcase_add_test(tc, test_cli_update_shebang);
    tcase_add_test(tc, test_cli_update_footer);
    tcase_add_test(tc, test_cli_remove_outofrange_no_pad);
    tcase_add_test(tc, test_cli_zero_remove_keep_from_line);
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
