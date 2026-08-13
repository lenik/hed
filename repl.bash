# bash completion for repl(1)
_repl() {
    local cur prev
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD - 1]}"
    case "$prev" in
    -e | --regexp | -f | --file | -d | --directories | -D | --devices | --max-depth | --exclude | --exclude-from | --exclude-dir | --include | --binary-files | --palette | --range | --restore | --patch | -X | --ignores)
        return 0
        ;;
    --color)
        COMPREPLY=($(compgen -W 'never always auto' -- "$cur"))
        return 0
        ;;
    esac
    if [[ $cur == -* ]]; then
        COMPREPLY=($(compgen -W '-E --extended-regexp -F --fixed-strings -G --basic-regexp -P --perl-regexp -e --regexp -f --file -i --ignore-case --no-ignore-case -v --invert-match -w --word-regexp -x --line-regexp -l --line -g --global -1 --first --range -a --all -X --ignores -r --recursive -R --dereference-recursive -p --patch --restore --max-depth --exclude --exclude-from --exclude-dir --include -d --directories -D --devices --binary-files -I -U --binary -z --null-data -n --dryrun --dry-run -q --quiet --silent -c --context -u --unified -s --summary --color --palette -h --help --version' -- "$cur"))
        return 0
    fi
    COMPREPLY=($(compgen -f -- "$cur"))
}
complete -F _repl repl
