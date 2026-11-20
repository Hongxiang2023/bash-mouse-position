# Source from an interactive Bash shell. Requires xterm-style SGR mouse reports.
[[ $- == *i* ]] || return 0
if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 4) )); then
    printf '%s\n' 'bash-mouse-position requires Bash 4.4 or newer.' >&2
    return 1
fi
[[ -t 0 && -t 2 && ${TERM:-dumb} != dumb ]] || return 0

if ! type -t _bmp_register >/dev/null; then
    _bmp_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || return 1
    enable -f "$_bmp_dir/bash-mouse-position.so" _bmp_register || {
        printf '%s\n' 'Build bash-mouse-position first with sh build.sh.' >&2
        return 1
    }
    unset _bmp_dir
    _bmp_register || return 1
fi

_bmp_mouse_on=$'\\[\e[?1006h\e[?1000h\\]'
_bmp_mouse_off=$'\e[?1000l\e[?1006l'

bash_mouse_position_on() {
    local keymap
    for keymap in emacs-standard vi-insert vi-command; do
        bind -m "$keymap" '"\e[<": bash-mouse-position-click' || return 1
    done
    [[ $PS1 == "$_bmp_mouse_on"* ]] || PS1=$_bmp_mouse_on$PS1
    [[ ${PS0-} == "$_bmp_mouse_off"* ]] || PS0=$_bmp_mouse_off${PS0-}
}

bash_mouse_position_off() {
    local keymap
    PS1=${PS1#"$_bmp_mouse_on"}
    PS0=${PS0#"$_bmp_mouse_off"}
    printf '%s' "$_bmp_mouse_off" >/dev/tty
    for keymap in emacs-standard vi-insert vi-command; do
        bind -m "$keymap" -r '\e[<'
    done
}

bash_mouse_position_on
