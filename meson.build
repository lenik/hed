project(
    'hed',
    'c',
    'cpp',
    version: run_command(
        'sh',
        '-c', '''
        v=$(git describe --tags --always --dirty 2>/dev/null || true)
        v=${v#v}
        if [ -n "$v" ]; then
            if [ "${v%.*}" = "$v" ]; then
                v="0.0.0-$v"
            fi
            printf "%s" "$v"
        else
            echo "0.0.0"
        fi
        ''',
        check: false,
    ).stdout().strip(),
    license: 'AGPL-3.0-or-later',
    default_options: [
        'c_std=c99',
        'warning_level=2',
    ],
)

project_author = 'Lenik'
project_email = 'hed@bodz.net'
project_year = 2026

fs = import('fs')
c = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

prefix = get_option('prefix')
bindir = prefix / get_option('bindir')
datadir = prefix / get_option('datadir')
localedir = prefix / get_option('localedir')
mandir = prefix / get_option('mandir')
pkgdatadir = datadir / meson.project_name()
pkgdocdir = datadir / 'doc' / meson.project_name()

config_h = configuration_data()
config_h.set_quoted('PROJECT_VERSION', meson.project_version())
config_h.set_quoted('PROJECT_AUTHOR', project_author)
config_h.set_quoted('PROJECT_EMAIL', project_email)
config_h.set('PROJECT_YEAR', project_year)

config_h.set_quoted('PREFIX', prefix)
config_h.set_quoted('DATADIR', datadir)
config_h.set_quoted('MANDIR', mandir)
config_h.set_quoted('PKGDATADIR', pkgdatadir)
config_h.set_quoted('LOCALEDIR', localedir)

configure_file(
    output: 'config.h',
    configuration: config_h,
)

bas_c_dep = dependency('bas-c-static', required: true)
pcre2_dep = dependency('libpcre2-8', required: true)

nowarn = cpp.get_supported_arguments(['-w', '/w'])
bas_args = ['-DLOGGER_NAME=hed', '-DASSETS_NAME=hed']

c_pch = 'src/c_pch.h'
c_args = [
    nowarn,
    bas_args,
    '-DTEXT_DOMAIN=hed',
    '-D_POSIX_C_SOURCE=200809L',
    '-D_DEFAULT_SOURCE',
]

src_inc = include_directories('src')

deps = [
    bas_c_dep,
    pcre2_dep,
]

subdir('po')

# Common sources compiled into each standalone program (no shared library).
common_edit = [
    'src/edit_file.c',
    'src/lines.c',
]

insert_sources = [
    'src/insert.c',
    'src/digit_opt.c',
] + common_edit

repl_sources = [
    'src/repl.c',
    'src/match.c',
    'src/walk.c',
] + common_edit

catq_sources = [
    'src/catq.c',
]

insert_exe = executable(
    'insert',
    insert_sources,
    c_args: c_args,
    c_pch: c_pch,
    include_directories: src_inc,
    dependencies: deps,
    install: true,
    install_dir: bindir,
)

repl_exe = executable(
    'repl',
    repl_sources,
    c_args: c_args,
    c_pch: c_pch,
    include_directories: src_inc,
    dependencies: deps,
    install: true,
    install_dir: bindir,
)

catq_exe = executable(
    'catq',
    catq_sources,
    c_args: c_args,
    c_pch: c_pch,
    include_directories: src_inc,
    dependencies: [bas_c_dep],
    install: true,
    install_dir: bindir,
)

bash_files = [
    'insert.bash',
    'repl.bash',
    'catq.bash',
]

foreach file : bash_files
    name = fs.stem(file)
    install_data(
        file,
        install_dir: datadir / 'bash-completion' / 'completions',
        rename: name,
    )
endforeach

install_data(
    [
        'LICENSE',
        'README.md',
        'README-zh.md',
    ],
    install_dir: pkgdocdir,
)

config_man = configuration_data()
config_man.set('PROJECT_VERSION', meson.project_version())
config_man.set('PROJECT_AUTHOR', project_author)
config_man.set('PROJECT_EMAIL', project_email)
config_man.set('PROJECT_YEAR', project_year)

configure_file(
    input: 'insert.1.in',
    output: 'insert.1',
    configuration: config_man,
    install: true,
    install_dir: mandir / 'man1',
)

configure_file(
    input: 'repl.1.in',
    output: 'repl.1',
    configuration: config_man,
    install: true,
    install_dir: mandir / 'man1',
)

configure_file(
    input: 'catq.1.in',
    output: 'catq.1',
    configuration: config_man,
    install: true,
    install_dir: mandir / 'man1',
)

manpage_langs = ['de', 'eo', 'fr', 'it', 'ja', 'ko', 'th', 'vi', 'zh_CN', 'zh_TW']
manpage_names = ['insert', 'repl', 'catq']
foreach lang : manpage_langs
    foreach name : manpage_names
        manin = 'man' / lang / (name + '.1.in')
        if fs.is_file(meson.current_source_dir() / manin)
            man_gen = configure_file(
                input: manin,
                output: name + '.1.' + lang,
                configuration: config_man,
            )
            install_data(
                man_gen,
                install_dir: mandir / lang / 'man1',
                rename: name + '.1',
            )
        endif
    endforeach
endforeach

run_target(
    'look',
    command: [
        'bash',
        '-euc', '''
            tmpdir=$(mktemp -d)
            DESTDIR=$tmpdir meson install -C "@BUILD_ROOT@"
            tree "$tmpdir"
            rm -fr "$tmpdir"
        ''',
    ],
)

run_target(
    'install-symlinks',
    command: [
        'bash',
        '-euc', '''
            prefix="@0@"
            bindir="@1@"
            datadir="@2@"
            localedir="@3@"
            mandir="@4@"

            mkdir -p "$bindir" "$datadir/bash-completion/completions" "$localedir" "$mandir/man1"

            sudo ln -sfn "@BUILD_ROOT@/insert" "$bindir/insert"
            sudo ln -sfn "@BUILD_ROOT@/repl" "$bindir/repl"
            sudo ln -sfn "@BUILD_ROOT@/catq" "$bindir/catq"
            sudo ln -sfn "@BUILD_ROOT@/insert.1" "$mandir/man1/insert.1"
            sudo ln -sfn "@BUILD_ROOT@/repl.1" "$mandir/man1/repl.1"
            sudo ln -sfn "@BUILD_ROOT@/catq.1" "$mandir/man1/catq.1"
            sudo ln -sfn "@SOURCE_ROOT@/insert.bash" "$datadir/bash-completion/completions/insert"
            sudo ln -sfn "@SOURCE_ROOT@/repl.bash" "$datadir/bash-completion/completions/repl"
            sudo ln -sfn "@SOURCE_ROOT@/catq.bash" "$datadir/bash-completion/completions/catq"

            printf "Symlinks installed under %s\n" "$prefix"
        '''.format(
            get_option('prefix'), #
            bindir,
            datadir,
            localedir,
            mandir,
        ),
    ],
)

run_target(
    'uninstall-symlinks',
    command: [
        'bash',
        '-euc', '''
            prefix="@0@"
            bindir="@1@"
            datadir="@2@"
            localedir="@3@"
            mandir="@4@"

            for p in \
                "$bindir/insert" \
                "$bindir/repl" \
                "$bindir/catq" \
                "$mandir/man1/insert.1" \
                "$mandir/man1/repl.1" \
                "$mandir/man1/catq.1" \
                "$datadir/bash-completion/completions/insert" \
                "$datadir/bash-completion/completions/repl" \
                "$datadir/bash-completion/completions/catq"
            do
                if [ -L "$p" ]; then
                    sudo rm -f "$p"
                    printf "Removed %s\n" "$p"
                else
                    printf "Skipped (not a symlink): %s\n" "$p"
                fi
            done
        '''.format(
            get_option('prefix'), #
            bindir,
            datadir,
            localedir,
            mandir,
        ),
    ],
)

subdir('tests')
