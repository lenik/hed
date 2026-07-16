#define _POSIX_C_SOURCE 200809L

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *repl_bin(void) {
    const char *p = getenv("HED_REPL");
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

static int run_repl(char *const argv[]) {
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

START_TEST(test_cli_fixed_replace) {
    char dir[] = "/tmp/hed-repl-XXXXXX";
    char path[128];
    char *out;
    char *argv[7];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("foo bar foo\n", f);
        fclose(f);
    }

    argv[0] = (char *)repl_bin();
    argv[1] = "-q";
    argv[2] = "foo";
    argv[3] = "X";
    argv[4] = path;
    argv[5] = NULL;
    ck_assert_int_eq(run_repl(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "X bar X\n");
    free(out);

    snprintf(path, sizeof path, "rm -rf %s", dir);
    system(path);
}
END_TEST

START_TEST(test_cli_skip_unchanged) {
    char dir[] = "/tmp/hed-repl-XXXXXX";
    char path[128];
    struct stat st1, st2;
    char *argv[7];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("no match here\n", f);
        fclose(f);
    }

    ck_assert_int_eq(stat(path, &st1), 0);
    sleep(1);

    argv[0] = (char *)repl_bin();
    argv[1] = "-q";
    argv[2] = "zzz";
    argv[3] = "YYY";
    argv[4] = path;
    argv[5] = NULL;
    ck_assert_int_eq(run_repl(argv), 0);
    ck_assert_int_eq(stat(path, &st2), 0);
    ck_assert_int_eq(st1.st_mtime, st2.st_mtime);
    ck_assert_int_eq(st1.st_ino, st2.st_ino);

    snprintf(path, sizeof path, "rm -rf %s", dir);
    system(path);
}
END_TEST

START_TEST(test_cli_ignore_case) {
    char dir[] = "/tmp/hed-repl-XXXXXX";
    char path[128];
    char *out;
    char *argv[8];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("AbC\n", f);
        fclose(f);
    }

    argv[0] = (char *)repl_bin();
    argv[1] = "-q";
    argv[2] = "-i";
    argv[3] = "abc";
    argv[4] = "Z";
    argv[5] = path;
    argv[6] = NULL;
    ck_assert_int_eq(run_repl(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "Z\n");
    free(out);

    snprintf(path, sizeof path, "rm -rf %s", dir);
    system(path);
}
END_TEST

START_TEST(test_cli_recursive) {
    char dir[] = "/tmp/hed-repl-XXXXXX";
    char path[128];
    char sub[128];
    char *out;
    char *argv[8];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(sub, sizeof sub, "%s/sub", dir);
    ck_assert_int_eq(mkdir(sub, 0755), 0);
    snprintf(path, sizeof path, "%s/sub/f.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("old\n", f);
        fclose(f);
    }

    argv[0] = (char *)repl_bin();
    argv[1] = "-q";
    argv[2] = "-r";
    argv[3] = "old";
    argv[4] = "new";
    argv[5] = dir;
    argv[6] = NULL;
    ck_assert_int_eq(run_repl(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "new\n");
    free(out);

    snprintf(path, sizeof path, "rm -rf %s", dir);
    system(path);
}
END_TEST

START_TEST(test_cli_ere) {
    char dir[] = "/tmp/hed-repl-XXXXXX";
    char path[128];
    char *out;
    char *argv[8];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("a12b34\n", f);
        fclose(f);
    }

    argv[0] = (char *)repl_bin();
    argv[1] = "-q";
    argv[2] = "-E";
    argv[3] = "[0-9]+";
    argv[4] = "#";
    argv[5] = path;
    argv[6] = NULL;
    ck_assert_int_eq(run_repl(argv), 0);

    out = read_all(path);
    ck_assert_str_eq(out, "a#b#\n");
    free(out);

    snprintf(path, sizeof path, "rm -rf %s", dir);
    system(path);
}
END_TEST

static Suite *suite(void) {
    Suite *s = suite_create("repl_cli");
    TCase *tc = tcase_create("cli");

    tcase_set_timeout(tc, 30);
    tcase_add_test(tc, test_cli_fixed_replace);
    tcase_add_test(tc, test_cli_skip_unchanged);
    tcase_add_test(tc, test_cli_ignore_case);
    tcase_add_test(tc, test_cli_recursive);
    tcase_add_test(tc, test_cli_ere);
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
