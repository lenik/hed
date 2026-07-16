# hed

`hed`（**h**uman **ed**itor，人工编辑器）是一组就地文件编辑工具，相对 `sed` 一类流编辑器。

## 工具

### insert

在目标文件的指定行位置插入字符串或文件内容。

```bash
insert -p notes.txt "Header"
insert -20 notes.txt @snippet.txt
insert -a1 notes.txt -l "footer"
```

### repl

在多个文件中批量替换。内容未变则不写回。

```bash
repl foo bar file.txt
repl -i -r 'OldName' 'NewName' src/
```

## 构建

```bash
meson setup /build
ninja -C /build
ninja -C /build test
```

## 安装

```bash
ninja -C /build install
```

## 翻译

gettext 目录在 `po/`（域名为 `hed`）。语言列表见 `po/LINGUAS`。

```bash
ninja -C /build hed-pot
ninja -C /build hed-update-po
ninja -C /build hed-gmo
```

手册页翻译在 `man/<lang>/insert.1.in` 与 `man/<lang>/repl.1.in`，安装到 `$mandir/<lang>/man1/`。

## 许可证

AGPL-3.0-or-later. Copyright (C) 2026 Lenik <hed@bodz.net>
