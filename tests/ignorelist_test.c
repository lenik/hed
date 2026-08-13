#define _POSIX_C_SOURCE 200809L

#include "ignorelist/gitignore.h"
#include "ignorelist/hgignore.h"
#include "ignorelist/ignorelist.h"
#include "ignorelist/svnignore.h"
#include "ignorelist/wildmatch.h"

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    ck_assert_ptr_nonnull(f);
    fputs(content, f);
    fclose(f);
}

static void rm_rf(const char *dir) {
    char cmd[512];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    system(cmd);
}

START_TEST(test_wildmatch_basic) {
    ck_assert_int_eq(ignorelist_wildmatch("*.o", "foo.o", WM_PATHNAME), 1);
    ck_assert_int_eq(ignorelist_wildmatch("*.o", "foo.c", WM_PATHNAME), 0);
    ck_assert_int_eq(ignorelist_wildmatch("*.o", "dir/foo.o", WM_PATHNAME), 0);
    ck_assert_int_eq(ignorelist_wildmatch("foo", "foo", WM_PATHNAME), 1);
    ck_assert_int_eq(ignorelist_wildmatch("foo?", "food", WM_PATHNAME), 1);
    ck_assert_int_eq(ignorelist_wildmatch("foo[ab]", "foob", WM_PATHNAME), 1);
    ck_assert_int_eq(ignorelist_wildmatch("foo[!z]", "fooy", WM_PATHNAME), 1);
    ck_assert_int_eq(ignorelist_wildmatch("foo[!y]", "fooy", WM_PATHNAME), 0);
    ck_assert_int_eq(ignorelist_wildmatch("a\\*b", "a*b", WM_PATHNAME), 1);
    ck_assert_int_eq(ignorelist_wildmatch("a\\*b", "axb", WM_PATHNAME), 0);
}
END_TEST

START_TEST(test_wildmatch_doublestar) {
    ck_assert_int_eq(ignorelist_wildmatch("**/bar", "bar", WM_PATHNAME), 1);
    ck_assert_int_eq(ignorelist_wildmatch("**/bar", "a/bar", WM_PATHNAME), 1);
    ck_assert_int_eq(ignorelist_wildmatch("**/bar", "a/b/bar", WM_PATHNAME), 1);
    ck_assert_int_eq(ignorelist_wildmatch("a/**/c", "a/c", WM_PATHNAME), 1);
    ck_assert_int_eq(ignorelist_wildmatch("a/**/c", "a/b/c", WM_PATHNAME), 1);
    ck_assert_int_eq(ignorelist_wildmatch("a/**", "a/b/c", WM_PATHNAME), 1);
    ck_assert_int_eq(ignorelist_wildmatch("**/x/**", "x/y", WM_PATHNAME), 1);
}
END_TEST

START_TEST(test_gitignore_nested_and_negate) {
    char dir[] = "/tmp/hed-ign-XXXXXX";
    char path[512];
    gitignore_t *gi;

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/.git", dir);
    ck_assert_int_eq(mkdir(path, 0755), 0);

    snprintf(path, sizeof path, "%s/.gitignore", dir);
    write_file(path, "# comment\n"
                     "*.log\n"
                     "build/\n"
                     "!keep.log\n"
                     "/rooted.txt\n"
                     "dir/nested.txt\n"
                     "\n");

    snprintf(path, sizeof path, "%s/sub", dir);
    ck_assert_int_eq(mkdir(path, 0755), 0);
    snprintf(path, sizeof path, "%s/sub/.gitignore", dir);
    write_file(path, "*.tmp\n!important.tmp\n");

    /* assorted files under the fake tree */
    snprintf(path, sizeof path, "%s/foo.log", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/keep.log", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/rooted.txt", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/elsewhere", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/elsewhere/rooted.txt", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/dir", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/dir/nested.txt", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/build", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/build/x.c", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/sub/a.tmp", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/sub/important.tmp", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/sub/a.c", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/ok.c", dir);
    write_file(path, "x\n");

    gi = gitignore_open(dir);
    ck_assert_ptr_nonnull(gi);

    ck_assert_int_eq(gitignore_match(gi, "foo.log", 0), 1);
    ck_assert_int_eq(gitignore_match(gi, "keep.log", 0), 0);
    ck_assert_int_eq(gitignore_match(gi, "build", 1), 1);
    ck_assert_int_eq(gitignore_match(gi, "build/x.c", 0), 1);
    ck_assert_int_eq(gitignore_match(gi, "sub/a.tmp", 0), 1);
    ck_assert_int_eq(gitignore_match(gi, "sub/important.tmp", 0), 0);
    ck_assert_int_eq(gitignore_match(gi, "sub/a.c", 0), 0);
    ck_assert_int_eq(gitignore_match(gi, "ok.c", 0), 0);
    ck_assert_int_eq(gitignore_match(gi, "rooted.txt", 0), 1);
    ck_assert_int_eq(gitignore_match(gi, "elsewhere/rooted.txt", 0), 0);
    ck_assert_int_eq(gitignore_match(gi, "dir/nested.txt", 0), 1);
    ck_assert_int_eq(gitignore_match(gi, ".git", 1), 1);

    gitignore_free(gi);
    rm_rf(dir);
}
END_TEST

START_TEST(test_gitignore_info_exclude) {
    char dir[] = "/tmp/hed-ign-ex-XXXXXX";
    char path[512];
    gitignore_t *gi;

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/.git", dir);
    ck_assert_int_eq(mkdir(path, 0755), 0);
    snprintf(path, sizeof path, "%s/.git/info", dir);
    ck_assert_int_eq(mkdir(path, 0755), 0);
    snprintf(path, sizeof path, "%s/.git/info/exclude", dir);
    write_file(path, "local-only\n*.bak\n");
    snprintf(path, sizeof path, "%s/.gitignore", dir);
    write_file(path, "*.o\n");

    snprintf(path, sizeof path, "%s/local-only", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/x.bak", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/x.o", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/x.c", dir);
    write_file(path, "x\n");

    gi = gitignore_open(dir);
    ck_assert_ptr_nonnull(gi);
    ck_assert_int_eq(gitignore_match(gi, "local-only", 0), 1);
    ck_assert_int_eq(gitignore_match(gi, "x.bak", 0), 1);
    ck_assert_int_eq(gitignore_match(gi, "x.o", 0), 1);
    ck_assert_int_eq(gitignore_match(gi, "x.c", 0), 0);
    gitignore_free(gi);
    rm_rf(dir);
}
END_TEST

START_TEST(test_ignorelist_detect_git) {
    char dir[] = "/tmp/hed-ign2-XXXXXX";
    char path[512];
    ignorelist_t *il;

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/.git", dir);
    ck_assert_int_eq(mkdir(path, 0755), 0);
    snprintf(path, sizeof path, "%s/.gitignore", dir);
    write_file(path, "secret\n");
    snprintf(path, sizeof path, "%s/secret", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/public", dir);
    write_file(path, "y\n");

    il = ignorelist_open(dir);
    ck_assert_ptr_nonnull(il);
    ck_assert_int_eq(ignorelist_vcs(il), VCS_GIT);
    ck_assert_str_eq(ignorelist_root(il), dir);
    ck_assert_int_eq(ignorelist_match(il, path /* public */, 0), 0);
    snprintf(path, sizeof path, "%s/secret", dir);
    ck_assert_int_eq(ignorelist_match(il, path, 0), 1);
    ck_assert_int_eq(ignorelist_is_vcs_dir(".git"), 1);
    ck_assert_int_eq(ignorelist_is_vcs_dir(".hg"), 1);
    ck_assert_int_eq(ignorelist_is_vcs_dir(".svn"), 1);
    ck_assert_int_eq(ignorelist_is_vcs_dir("src"), 0);

    ignorelist_free(il);
    rm_rf(dir);
}
END_TEST

START_TEST(test_hgignore_glob_and_regexp) {
    char dir[] = "/tmp/hed-hg-XXXXXX";
    char path[512];
    hgignore_t *hg;
    ignorelist_t *il;

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/.hg", dir);
    ck_assert_int_eq(mkdir(path, 0755), 0);
    snprintf(path, sizeof path, "%s/.hgignore", dir);
    write_file(path, "syntax: glob\n"
                     "*.pyc\n"
                     "dist/\n"
                     "!keep.pyc\n"
                     "syntax: regexp\n"
                     "^notes/.*\\.md$\n");

    snprintf(path, sizeof path, "%s/a.pyc", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/keep.pyc", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/dist", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/notes", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/notes/todo.md", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/ok.c", dir);
    write_file(path, "x\n");

    hg = hgignore_open(dir);
    ck_assert_ptr_nonnull(hg);
    ck_assert_int_eq(hgignore_match(hg, "a.pyc", 0), 1);
    ck_assert_int_eq(hgignore_match(hg, "keep.pyc", 0), 0);
    ck_assert_int_eq(hgignore_match(hg, "notes/todo.md", 0), 1);
    ck_assert_int_eq(hgignore_match(hg, "ok.c", 0), 0);
    ck_assert_int_eq(hgignore_match(hg, ".hg", 1), 1);
    hgignore_free(hg);

    il = ignorelist_open(dir);
    ck_assert_ptr_nonnull(il);
    ck_assert_int_eq(ignorelist_vcs(il), VCS_HG);
    snprintf(path, sizeof path, "%s/a.pyc", dir);
    ck_assert_int_eq(ignorelist_match(il, path, 0), 1);
    ignorelist_free(il);
    rm_rf(dir);
}
END_TEST

START_TEST(test_svnignore_files) {
    char dir[] = "/tmp/hed-svn-XXXXXX";
    char path[512];
    svnignore_t *si;
    ignorelist_t *il;

    ck_assert_ptr_nonnull(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/.svn", dir);
    ck_assert_int_eq(mkdir(path, 0755), 0);

    /* default global ignores include *.o */
    snprintf(path, sizeof path, "%s/foo.o", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/foo.c", dir);
    write_file(path, "x\n");

    snprintf(path, sizeof path, "%s/.svnignore", dir);
    write_file(path, "# local\nsecret.txt\n");
    snprintf(path, sizeof path, "%s/secret.txt", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/visible.txt", dir);
    write_file(path, "x\n");

    snprintf(path, sizeof path, "%s/sub", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/sub/.svnignore", dir);
    write_file(path, "local.dat\n");
    snprintf(path, sizeof path, "%s/sub/local.dat", dir);
    write_file(path, "x\n");
    snprintf(path, sizeof path, "%s/sub/ok.dat", dir);
    write_file(path, "x\n");

    si = svnignore_open(dir);
    ck_assert_ptr_nonnull(si);
    ck_assert_int_eq(svnignore_match(si, "foo.o", 0), 1);
    ck_assert_int_eq(svnignore_match(si, "foo.c", 0), 0);
    ck_assert_int_eq(svnignore_match(si, "secret.txt", 0), 1);
    ck_assert_int_eq(svnignore_match(si, "visible.txt", 0), 0);
    ck_assert_int_eq(svnignore_match(si, "sub/local.dat", 0), 1);
    ck_assert_int_eq(svnignore_match(si, "sub/ok.dat", 0), 0);
    ck_assert_int_eq(svnignore_match(si, ".svn", 1), 1);
    svnignore_free(si);

    il = ignorelist_open(dir);
    ck_assert_ptr_nonnull(il);
    ck_assert_int_eq(ignorelist_vcs(il), VCS_SVN);
    ignorelist_free(il);
    rm_rf(dir);
}
END_TEST

START_TEST(test_ignorelist_none) {
    char dir[] = "/tmp/hed-none-XXXXXX";
    ignorelist_t *il;

    ck_assert_ptr_nonnull(mkdtemp(dir));
    il = ignorelist_open(dir);
    ck_assert_ptr_null(il);
    ignorelist_free(NULL);
    ck_assert_int_eq(ignorelist_vcs(NULL), VCS_NONE);
    ck_assert_ptr_null(ignorelist_root(NULL));
    rm_rf(dir);
}
END_TEST

static Suite *suite(void) {
    Suite *s = suite_create("ignorelist");
    TCase *tc = tcase_create("core");
    tcase_set_timeout(tc, 30);
    tcase_add_test(tc, test_wildmatch_basic);
    tcase_add_test(tc, test_wildmatch_doublestar);
    tcase_add_test(tc, test_gitignore_nested_and_negate);
    tcase_add_test(tc, test_gitignore_info_exclude);
    tcase_add_test(tc, test_ignorelist_detect_git);
    tcase_add_test(tc, test_hgignore_glob_and_regexp);
    tcase_add_test(tc, test_svnignore_files);
    tcase_add_test(tc, test_ignorelist_none);
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
