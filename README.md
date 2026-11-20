# Mouse positioning for Bash and zsh

Click a character in the command you are editing to move the cursor there. The add-on uses your shell's existing line editor and supports commands that wrap across several terminal rows, including long paths with escaped spaces.

## Try it in zsh on macOS or inside Herdr

```zsh
git clone https://github.com/Hongxiang2023/bash-mouse-position.git &&
cd bash-mouse-position &&
source ./zsh-mouse-position.zsh
```

If you cloned the repository already, run `git pull` in that directory and then source the file. Left-click a character to move the cursor, or drag across text to select it. Press **Ctrl+X, then Ctrl+D** to delete the selection or **Ctrl+X, then Ctrl+L** to clear the whole command. Use `zsh_mouse_position_off` to turn it off. To enable it in future panes, source the script by its absolute path near the end of your `~/.zshrc`.

The zsh version uses ZLE and requires no compiler. In macOS Terminal, enable **View → Allow Mouse Reporting**. Use a plain single click in the focused Herdr pane. If drag tracking interferes with clicks in your terminal, run `zsh_mouse_position_on click` to use click-only tracking. Run `zsh_mouse_position_on drag` to enable drag selection again.

## Try it in Bash

Paste this into the Bash prompt where you want mouse editing, including a Bash pane in Herdr or a Bash session on your HPC:

```bash
git clone https://github.com/Hongxiang2023/bash-mouse-position.git &&
cd bash-mouse-position &&
sh build.sh &&
source ./bash-mouse-position.bash
```

Paste a long command at the prompt, or press Up to recall one. Left-click a character on either row to move the cursor, then type to edit. The cursor moves when you release the button. Run `bash_mouse_position_off` to disable it and `bash_mouse_position_on` to enable it again.

## Use it on an HPC login node or inside Herdr

Run the steps above on the machine running Bash. If the HPC cannot reach GitHub, clone on your local machine first, then copy the source directory over SSH and build it on the HPC:

```bash
scp -r bash-mouse-position your-user@your-hpc:~/
ssh -t your-user@your-hpc
cd ~/bash-mouse-position
sh build.sh
source ./bash-mouse-position.bash
```

Once it works, add `source ~/bash-mouse-position/bash-mouse-position.bash` near the end of the HPC's `~/.bashrc`, after your prompt configuration. For a locally running Bash pane in Herdr, source the local copy instead. A Herdr pane needs to forward SGR mouse reports and answer cursor position queries. The standard action is a plain left-click.

## Bash requirements and shared limits

- Linux or macOS, a C compiler at build time, and interactive Bash 4.4 or later with loadable builtins enabled. The terminal must handle SGR mouse reports and cursor position queries. The build uses the Readline symbols in the running Bash, so separate Bash or Readline development headers are unnecessary.
- Supports printable ASCII commands and ordinary line wrapping. Commands with literal tabs, line breaks, or non-ASCII characters are skipped. Readline's horizontal scroll mode and wrapped buffers that extend beyond the visible screen are outside the supported layout.
- Mouse reporting is enabled while editing and turned off before the command runs. In Bash, use `exit` to leave an enabled shell cleanly. Closing the shell with Ctrl+D can leave reporting on. While editing, the terminal may require a modifier for its own mouse selection. If you use a prompt framework that rebuilds PS1 or PS0, it must preserve the add-on's prompt prefixes.
- A click briefly waits for the terminal's cursor-position reply. On a slow SSH connection, wait for the cursor to move before typing.
- The add-on owns the Readline escape prefix for SGR mouse reports while enabled. If mouse reporting gets stuck, run `printf '\033[?1000l\033[?1006l'` in the affected terminal.

The loadable builtin registers a Readline handler for SGR mouse reports. The handler measures the distance from the displayed cursor to the clicked cell and moves the Readline cursor by that amount.
