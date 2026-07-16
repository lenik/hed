# hed

`hed` (**h**uman **ed**itor) is a small suite of in-place file editors,
as opposed to stream editors like `sed`.

## Tools

### insert

Insert string or file contents into a target file at a line position.

```bash
insert -p notes.txt "Header"
insert -20 notes.txt @snippet.txt
insert -a1 notes.txt -l "footer"
```

- `-l` / `-f` / `-e` — SRC is a string, filename, or `@file`/string (default)
- `-n NUM` or digit clusters `-0`…`-9` — position (`-1` prepend, `-0` prepend+blank, `-20` before line 20; pads empty lines past EOF when needed)
- `-a` / `-p` — append / prepend (`-a0` like `>>`, `-a1` before last line)
- `-r NUM` — remove NUM lines at the insert point (delete/update)
- `-b NUM` — trailing newlines after each SRC (default: 1 for strings, 0 for files; when set, applies to both)
- `-m TEXT` — substitute TEXT for missing source files

### repl

Batch replace a pattern with a replacement across files. Unchanged files are not rewritten.

```bash
repl foo bar file.txt
repl -i -r 'OldName' 'NewName' src/
repl -E '[0-9]+' '#' data.txt
```

Matching flags follow `grep` (`-F`/`-E`/`-G`/`-P`, `-i`, `-w`, `-x`, `-v`, include/exclude, `-r`/`-R`, …).
Reporting: `-q` quiet, `-s` summary, `-c`/`-u` context/unified diff.

## Build

```bash
meson setup /build
ninja -C /build
ninja -C /build test
```

## Install

```bash
ninja -C /build install
# or for local symlink install:
ninja -C /build install-symlinks
```

## Translations

gettext catalogs live in `po/` (domain `hed`). Supported languages are listed in `po/LINGUAS`.

```bash
ninja -C /build hed-pot        # refresh po/hed.pot from sources
ninja -C /build hed-update-po  # merge into po/*.po
ninja -C /build hed-gmo        # compile .mo files
```

Translated man pages live under `man/<lang>/insert.1.in` and `man/<lang>/repl.1.in`, installed to `$mandir/<lang>/man1/`.

## License

AGPL-3.0-or-later. Copyright (C) 2026 Lenik <hed@bodz.net>
