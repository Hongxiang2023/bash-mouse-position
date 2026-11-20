# Source from interactive zsh. ZLE handles input and cursor updates.
[[ -n ${ZSH_VERSION-} && -o interactive ]] || return 0
autoload -Uz add-zle-hook-widget

_zmp_start() {
    printf '\e[?1006h\e[?1000h' >/dev/tty
}

_zmp_finish() {
    printf '\e[?1000l\e[?1006l' >/dev/tty
}

_zmp_click() {
    emulate -L zsh
    local LC_ALL=C byte report='' reply='' queued='' MATCH
    local -a match
    local -i row col target
    # Move on button-down. A missing or intercepted release can still leave a valid click.
    local mouse_pattern='^0;([1-9][0-9]{0,4});([1-9][0-9]{0,4})M$'
    local cursor_pattern=$'^\e\\[([1-9][0-9]{0,4});([1-9][0-9]{0,4})R$'

    while (( ${#report} < 17 )); do
        read -rk 1 -t 0.1 byte || return 0
        report+=$byte
        [[ $byte == [mM] ]] && break
        [[ $byte == [0-9\;] ]] || return 0
    done
    [[ $report =~ $mouse_pattern && -n $BUFFER && $BUFFER != *[^\ -~]* ]] || return 0
    col=$match[1] row=$match[2]
    (( col <= COLUMNS && row <= LINES )) || return 0

    printf '\e[6n' >/dev/tty
    # Keep keystrokes received before the cursor position reply.
    while (( ${#queued} + ${#reply} < 64 )); do
        read -rk 1 -t 0.5 byte || break
        if [[ $byte == $'\e' ]]; then
            queued+=$reply
            reply=''
        fi
        reply+=$byte
        if [[ $reply =~ $cursor_pattern ]]; then
            target=$(( CURSOR + (row - match[1]) * COLUMNS + col - match[2] ))
            if (( match[1] <= LINES && match[2] <= COLUMNS &&
                  target >= 0 && target <= ${#BUFFER} )); then
                CURSOR=$target
            fi
            [[ -z $queued ]] || zle -U "$queued"
            return 0
        fi
    done
    [[ -z $queued$reply ]] || zle -U "$queued$reply"
    return 0
}

zsh_mouse_position_on() {
    local keymap
    zle -N _zmp_click
    for keymap in emacs viins vicmd; do
        bindkey -M "$keymap" $'\e[<' _zmp_click || return 1
    done
    add-zle-hook-widget line-init _zmp_start
    add-zle-hook-widget line-finish _zmp_finish
}

zsh_mouse_position_off() {
    local keymap
    add-zle-hook-widget -d line-init _zmp_start
    add-zle-hook-widget -d line-finish _zmp_finish
    _zmp_finish
    for keymap in emacs viins vicmd; do
        bindkey -M "$keymap" -r $'\e[<'
    done
}

zsh_mouse_position_on
