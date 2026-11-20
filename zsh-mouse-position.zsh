# Source from interactive zsh. ZLE handles input and cursor updates.
[[ -n ${ZSH_VERSION-} && -o interactive ]] || return 0
autoload -Uz add-zle-hook-widget
typeset -gi _zmp_drag_width=0 _zmp_anchor=0 _zmp_anchor_row=0 _zmp_anchor_col=0
typeset -g _zmp_drag_buffer=''

# Clear both modes so re-sourcing over an older version leaves one active mode.
_zmp_reset_mouse=$'\e[?1000l\e[?1002l\e[?1006l'

_zmp_start() {
    _zmp_drag_width=0
    printf '%s\e[?1006h\e[?%sh' "$_zmp_reset_mouse" "$_zmp_tracking_mode" >/dev/tty
}

_zmp_finish() {
    _zmp_drag_width=0
    printf '%s' "$_zmp_reset_mouse" >/dev/tty
}

_zmp_click() {
    emulate -L zsh
    local LC_ALL=C byte report='' reply='' queued='' MATCH
    local -a match
    local -i row col target
    local button event
    # Move on button-down. A missing or intercepted release can still leave a valid click.
    local mouse_pattern='^(0|32);([1-9][0-9]{0,4});([1-9][0-9]{0,4})([mM])$'
    local cursor_pattern=$'^\e\\[([1-9][0-9]{0,4});([1-9][0-9]{0,4})R$'

    while (( ${#report} < 17 )); do
        read -rk 1 -t 0.1 byte || return 0
        report+=$byte
        [[ $byte == [mM] ]] && break
        [[ $byte == [0-9\;] ]] || return 0
    done
    [[ $report =~ $mouse_pattern && -n $BUFFER && $BUFFER != *[^\ -~]* ]] || return 0
    button=$match[1] col=$match[2] row=$match[3] event=$match[4]
    if [[ $event == m ]]; then
        _zmp_drag_width=0
        return 0
    fi
    (( col <= COLUMNS && row <= LINES )) || return 0

    if [[ $button == 32 ]]; then
        [[ $_zmp_drag_width == $COLUMNS && $_zmp_drag_buffer == "$BUFFER" ]] || return 0
        target=$(( _zmp_anchor + (row - _zmp_anchor_row) * COLUMNS + col - _zmp_anchor_col ))
        (( target >= 0 && target <= ${#BUFFER} )) || return 0
        MARK=$_zmp_anchor
        CURSOR=$target
        REGION_ACTIVE=$(( CURSOR != MARK ))
        return 0
    fi

    _zmp_drag_width=0
    REGION_ACTIVE=0
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
                _zmp_anchor=$target _zmp_anchor_row=$row _zmp_anchor_col=$col
                _zmp_drag_width=$COLUMNS _zmp_drag_buffer=$BUFFER
            fi
            [[ -z $queued ]] || zle -U "$queued"
            return 0
        fi
    done
    [[ -z $queued$reply ]] || zle -U "$queued$reply"
    return 0
}

_zmp_delete_selection() {
    if (( REGION_ACTIVE && MARK != CURSOR )); then
        zle .kill-region
        REGION_ACTIVE=0
    fi
    _zmp_drag_width=0
}

_zmp_clear_command() {
    MARK=0
    CURSOR=${#BUFFER}
    zle .kill-region
    REGION_ACTIVE=0
    _zmp_drag_width=0
}

zsh_mouse_position_on() {
    local keymap
    case ${1:-drag} in
        drag) typeset -g _zmp_tracking_mode=1002 ;;
        click) typeset -g _zmp_tracking_mode=1000 ;;
        *) print -u2 'Usage: zsh_mouse_position_on [drag|click]'; return 1 ;;
    esac
    zle -N _zmp_click
    zle -N _zmp_delete_selection
    zle -N _zmp_clear_command
    for keymap in emacs viins vicmd; do
        bindkey -M "$keymap" $'\e[<' _zmp_click || return 1
        bindkey -M "$keymap" '^X^D' _zmp_delete_selection
        bindkey -M "$keymap" '^X^L' _zmp_clear_command
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
        bindkey -M "$keymap" -r '^X^D'
        bindkey -M "$keymap" -r '^X^L'
    done
}

zsh_mouse_position_on
