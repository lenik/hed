# bash completion for insert(1)
_insert() {
    local cur prev
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD - 1]}"
    case "$prev" in
    -n | -b | --blank | -m | --missing | -r | --remove)
        return 0
        ;;
    esac
    if [[ $cur == -* ]]; then
        COMPREPLY=($(compgen -W '-l --strings -f --filenames -e --extended -n -a --append -p --prepend -r --remove -b --blank -m --missing -v --verbose -q --quiet -h --help --version' -- "$cur"))
        return 0
    fi
    COMPREPLY=($(compgen -f -- "$cur"))
}
complete -F _insert insert
