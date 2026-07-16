# bash completion for repl(1)
_repl() {
    local cur prev
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD - 1]}"
    case "$prev" in
    -e | --regexp | -f | --file | -d | --directories | -D | --devices | --max-depth | --exclude | --exclude-from | --exclude-dir | --include | --binary-files)
        return 0
        ;;
    esac
    if [[ $cur == -* ]]; then
        COMPREPLY=($(compgen -W '-E --extended-regexp -F --fixed-strings -G --basic-regexp -P --perl-regexp -e --regexp -f --file -i --ignore-case --no-ignore-case -v --invert-match -w --word-regexp -x --line-regexp -r --recursive -R --dereference-recursive --max-depth --exclude --exclude-from --exclude-dir --include -d --directories -D --devices --binary-files -I -U --binary -z --null-data -q --quiet --silent -c --context -u --unified -s --summary -h --help --version' -- "$cur"))
        return 0
    fi
    COMPREPLY=($(compgen -f -- "$cur"))
}
complete -F _repl repl
