check_dep = dependency('check', required: true)

c_pch = '../' + c_pch
src_inc = include_directories('../src', '..')

common_edit = [
    '../src/edit_file.c',
    '../src/lines.c',
]

# Unit tests compile helper sources directly (no libhed).
insert_unit_sources = [
    'insert_test.c',
    '../src/digit_opt.c',
] + common_edit

repl_unit_sources = [
    'repl_test.c',
    '../src/match.c',
] + common_edit

insert_unit = executable(
    'insert_unit',
    insert_unit_sources,
    c_args: c_args,
    c_pch: c_pch,
    include_directories: src_inc,
    dependencies: [deps, check_dep],
    install: false,
)
test('insert_unit', insert_unit)

repl_unit = executable(
    'repl_unit',
    repl_unit_sources,
    c_args: c_args,
    c_pch: c_pch,
    include_directories: src_inc,
    dependencies: [deps, check_dep],
    install: false,
)
test('repl_unit', repl_unit)

ignorelist_unit_sources = [
    'ignorelist_test.c',
    '../src/ignorelist/ignorelist.c',
    '../src/ignorelist/wildmatch.c',
    '../src/ignorelist/gitignore.c',
    '../src/ignorelist/hgignore.c',
    '../src/ignorelist/svnignore.c',
]

ignorelist_unit = executable(
    'ignorelist_unit',
    ignorelist_unit_sources,
    c_args: c_args,
    c_pch: c_pch,
    include_directories: src_inc,
    dependencies: [deps, check_dep],
    install: false,
)
test('ignorelist_unit', ignorelist_unit)

# CLI integration tests invoke the real binaries.
insert_cli = executable(
    'insert_cli',
    'insert_cli_test.c',
    c_args: c_args,
    c_pch: c_pch,
    include_directories: src_inc,
    dependencies: [deps, check_dep],
    install: false,
)
test(
    'insert_cli',
    insert_cli,
    env: {'HED_INSERT': insert_exe.full_path()},
    depends: [insert_exe],
)

repl_cli = executable(
    'repl_cli',
    'repl_cli_test.c',
    c_args: c_args,
    c_pch: c_pch,
    include_directories: src_inc,
    dependencies: [deps, check_dep],
    install: false,
)
test(
    'repl_cli',
    repl_cli,
    env: {'HED_REPL': repl_exe.full_path()},
    depends: [repl_exe],
)

catq_cli = executable(
    'catq_cli',
    'catq_cli_test.c',
    c_args: c_args,
    c_pch: c_pch,
    include_directories: src_inc,
    dependencies: [bas_c_dep, check_dep],
    install: false,
)
test(
    'catq_cli',
    catq_cli,
    env: {'HED_CATQ': catq_exe.full_path()},
    depends: [catq_exe],
)
