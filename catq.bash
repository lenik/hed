# bash completion for catq(1)
_catq() {
    local cur prev
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD - 1]}"
    if [[ $cur == -* ]]; then
        COMPREPLY=($(compgen -W '-A --show-all -b --number-nonblank -e -E --show-ends -n --number -s --squeeze-blank -t -T --show-tabs -u -v --show-nonprinting --help --version' -- "$cur"))
        return 0
    fi
    COMPREPLY=($(compgen -f -- "$cur"))
}
complete -F _catq catq
