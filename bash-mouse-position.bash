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

# Retire an earlier prompt prefix when this file is sourced again.
PS1=${PS1#"${_bmp_mouse_on-}"}
PS0=${PS0#"${_bmp_mouse_off-}"}
_bmp_mode=${BASH_MOUSE_POSITION_MODE:-drag}
if [[ -n ${STY:-} && -z ${BASH_MOUSE_POSITION_MODE:-} ]]; then
    _bmp_mode=keyboard
fi
case $_bmp_mode in
    click) _bmp_mouse_on=$'\\[\e[?1006h\e[?1000h\\]' ;;
    drag) _bmp_mouse_on=$'\\[\e[?1006h\e[?1002h\\]' ;;
    *) _bmp_mouse_on='' ;;
esac
_bmp_mouse_off=$'\e[?1000l\e[?1002l\e[?1006l'

bash_mouse_position_on() {
    local keymap
    for keymap in emacs-standard vi-insert vi-command; do
        if [[ -n $_bmp_mouse_on ]]; then
            bind -m "$keymap" '"\e[<": bash-mouse-position-click' || return 1
        elif [[ $(bind -m "$keymap" -q bash-mouse-position-click) == *'\e[<'* ]]; then
            bind -m "$keymap" -r '\e[<'
        fi
        bind -m "$keymap" '"\C-x\C-d": kill-region'
        bind -m "$keymap" '"\C-xw": copy-region-as-kill'
        bind -m "$keymap" '"\C-x\C-l": bash-mouse-position-clear'
        bind -m "$keymap" '"\C-xm": set-mark'
        bind -m "$keymap" '"\C-x\C-y": yank'
    done
    if [[ -n $_bmp_mouse_on ]]; then
        [[ $PS1 == "$_bmp_mouse_on"* ]] || PS1=$_bmp_mouse_on$PS1
        [[ ${PS0-} == "$_bmp_mouse_off"* ]] || PS0=$_bmp_mouse_off${PS0-}
    else
        printf '%s' "$_bmp_mouse_off" >/dev/tty
    fi
}

bash_mouse_position_off() {
    local keymap sequence
    PS1=${PS1#"$_bmp_mouse_on"}
    PS0=${PS0#"$_bmp_mouse_off"}
    printf '%s' "$_bmp_mouse_off" >/dev/tty
    for keymap in emacs-standard vi-insert vi-command; do
        for sequence in '\e[<' '\C-x\C-d' '\C-xw' '\C-x\C-l' '\C-xm' '\C-x\C-y'; do
            bind -m "$keymap" -r "$sequence"
        done
    done
}

bash_mouse_position_on
