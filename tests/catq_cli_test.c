#define _POSIX_C_SOURCE 200809L

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *catq_bin(void) {
    const char *p = getenv("HED_CATQ");
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

static int run_catq(char *const argv[]) {
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

START_TEST(test_cli_basic_cat) {
    char dir[] = "/tmp/hed-catq-XXXXXX";
    char path[128];
    char *out;
    char *argv[5];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("hello world\n", f);
        fclose(f);
    }

    argv[0] = (char *)catq_bin();
    argv[1] = path;
    argv[2] = NULL;
    ck_assert_int_eq(run_catq(argv), 0);

    out = read_all(path);
    free(out);

    snprintf(path, sizeof path, "rm -rf %s", dir);
    system(path);
}
END_TEST

START_TEST(test_cli_missing_file_silent) {
    char dir[] = "/tmp/hed-catq-XXXXXX";
    char path[128];
    char missing[128];
    char outp[128];
    char *out;
    char *argv[6];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    snprintf(missing, sizeof missing, "%s/missing.txt", dir);
    snprintf(outp, sizeof outp, "%s/out.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("existing content\n", f);
        fclose(f);
    }

    argv[0] = (char *)catq_bin();
    argv[1] = path;
    argv[2] = missing;
    argv[3] = NULL;
    
    pid_t pid = fork();
    ck_assert_int_ge(pid, 0);
    if (pid == 0) {
        freopen(outp, "w", stdout);
        execv(argv[0], argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    ck_assert_int_eq(WEXITSTATUS(status), 0);

    out = read_all(outp);
    ck_assert_str_eq(out, "existing content\n");
    free(out);

    snprintf(path, sizeof path, "rm -rf %s", dir);
    system(path);
}
END_TEST

START_TEST(test_cli_stdin) {
    char dir[] = "/tmp/hed-catq-XXXXXX";
    char outp[128];
    char cmd[128];
    char *out;
    char *argv[5];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(outp, sizeof outp, "%s/out.txt", dir);

    argv[0] = (char *)catq_bin();
    argv[1] = "-";
    argv[2] = NULL;
    
    pid_t pid = fork();
    ck_assert_int_ge(pid, 0);
    if (pid == 0) {
        freopen(outp, "w", stdout);
        FILE *in = fopen("/dev/null", "r");
        dup2(fileno(in), STDIN_FILENO);
        fclose(in);
        execv(argv[0], argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    ck_assert_int_eq(WEXITSTATUS(status), 0);

    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    system(cmd);
}
END_TEST

START_TEST(test_cli_number_lines) {
    char dir[] = "/tmp/hed-catq-XXXXXX";
    char path[128];
    char outp[128];
    char *out;
    char *argv[6];

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/t.txt", dir);
    snprintf(outp, sizeof outp, "%s/out.txt", dir);
    {
        FILE *f = fopen(path, "w");
        fputs("line1\nline2\nline3\n", f);
        fclose(f);
    }

    argv[0] = (char *)catq_bin();
    argv[1] = "-n";
    argv[2] = path;
    argv[3] = NULL;
    
    pid_t pid = fork();
    ck_assert_int_ge(pid, 0);
    if (pid == 0) {
        freopen(outp, "w", stdout);
        execv(argv[0], argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    ck_assert_int_eq(WEXITSTATUS(status), 0);

    out = read_all(outp);
    ck_assert_ptr_nonnull(strstr(out, "     1\tline1"));
    ck_assert_ptr_nonnull(strstr(out, "     2\tline2"));
    ck_assert_ptr_nonnull(strstr(out, "     3\tline3"));
    free(out);

    snprintf(path, sizeof path, "rm -rf %s", dir);
    system(path);
}
END_TEST

static Suite *suite(void) {
    Suite *s = suite_create("catq_cli");
    TCase *tc = tcase_create("cli");

    tcase_set_timeout(tc, 30);
    tcase_add_test(tc, test_cli_basic_cat);
    tcase_add_test(tc, test_cli_missing_file_silent);
    tcase_add_test(tc, test_cli_stdin);
    tcase_add_test(tc, test_cli_number_lines);
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
