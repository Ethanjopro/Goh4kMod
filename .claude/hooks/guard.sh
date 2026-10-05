#!/usr/bin/env bash
# Goh4kMod guard (PreToolUse). A deterministic backstop under auto mode:
#   ask:  system-wide installs, registry writes, persistent environment/PATH changes, and publishing.
#         These are the only things Ethan approves; everything else runs without asking.
#   deny: direct writes into the Steam install. deploy.cmd / uninstall.cmd are the way in and out.
# Plain pattern matching on the tool call: a speed bump, not a sandbox.
in=$(cat)
tool=$(printf '%s' "$in" | grep -o '"tool_name"[[:space:]]*:[[:space:]]*"[^"]*"' | head -n 1 | sed 's/.*"\([^"]*\)"$/\1/')
GAME='call to arms - gates of hell'   # the Steam install; the profile (My Games\gates of hell) is not matched

decide() { # $1 = deny|ask, $2 = reason shown to Claude and to you
  printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"%s","permissionDecisionReason":"%s"}}\n' "$1" "$2"
  exit 0
}
B='(^|[^[:alnum:]_-])'   # word start
E='([^[:alnum:]_-]|$)'   # word end

case "$tool" in
  Bash|PowerShell)
    # Match only the command text, not the description.
    cmd=$(printf '%s' "$in" | grep -oE '"command"[[:space:]]*:[[:space:]]*"([^"\\]|\\.)*"' | head -n 1)
    has() { printf '%s' "$cmd" | grep -qiE "$1"; }
    if has "${B}(winget|choco|scoop|msiexec|setx)${E}|${B}reg(\\.exe)?[[:space:]]+(add|delete|import|load)${E}|(New|Set|Remove)-ItemProperty|SetEnvironmentVariable|Set-ExecutionPolicy|Add-MpPreference|npm[[:space:]]+(i|install)[[:space:]]+(-g|--global)|uv[[:space:]]+(tool|python)[[:space:]]+install"; then
      decide ask "Goh4kMod guard: system-wide install, registry or environment change. Needs Ethan's OK."
    fi
    if has "${B}pip3?[[:space:]]+install" && ! has "uv[[:space:]]+pip[[:space:]]+install"; then
      decide ask "Goh4kMod guard: global pip install. Needs Ethan's OK."
    fi
    if has "${B}git([[:space:]].*)?[[:space:]]push${E}|${B}gh[[:space:]]+pr[[:space:]]+create"; then
      decide ask "Goh4kMod guard: publishing step, needs Ethan's OK."
    fi
    W="Goh4kMod guard: this writes into the game install. Use deploy.cmd / uninstall.cmd; if it only reads the install, put the read in its own command."
    # A write verb followed by the install path: the install is the target.
    if has "${B}(rm|del|erase|mv|move|ren|rename|mklink|tee|touch|Move-Item|Remove-Item|New-Item|Rename-Item|Set-Content|Add-Content|Clear-Content|Out-File)${E}.*${GAME}"; then
      decide deny "$W"
    fi
    # A copy goes into the install when no other path follows the install path (the destination comes last).
    if has "${B}(cp|copy|xcopy|robocopy|Copy-Item)${E}.*${GAME}"; then
      lc=${cmd,,}
      rest=${lc##*"$GAME"}
      if ! printf '%s' "$rest" | grep -qE '[a-z]:(\\\\|/)|goh4kmod|[[:space:]"](scratch|captures|build|tools|src)(\\\\|/)'; then
        decide deny "$W"
      fi
    fi
    ;;
  Edit|Write|MultiEdit|NotebookEdit)
    if printf '%s' "$in" | grep -oE '"(file_path|notebook_path)"[[:space:]]*:[[:space:]]*"[^"]*"' | grep -qiF "$GAME"; then
      decide deny "Goh4kMod guard: this edits a file inside the game install. Game-folder changes go through deploy.cmd / uninstall.cmd."
    fi
    ;;
esac
exit 0
