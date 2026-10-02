#!/usr/bin/env bash
# Goh4kMod guard (PreToolUse). A deterministic backstop under auto mode:
#   deny: system-wide installs, registry writes, persistent environment/PATH changes
#   ask:  anything that writes into the game install (including deploy/uninstall), and publishing
# Plain pattern matching on the tool call: a speed bump, not a sandbox.
in=$(cat)
tool=$(printf '%s' "$in" | grep -o '"tool_name"[[:space:]]*:[[:space:]]*"[^"]*"' | head -n 1 | sed 's/.*"\([^"]*\)"$/\1/')

decide() { # $1 = deny|ask, $2 = reason shown to Claude and to you
  printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"%s","permissionDecisionReason":"%s"}}\n' "$1" "$2"
  exit 0
}
has() { printf '%s' "$in" | grep -qiE "$1"; }
B='(^|[^[:alnum:]_-])'   # word start
E='([^[:alnum:]_-]|$)'   # word end

case "$tool" in
  Bash|PowerShell)
    if has "${B}(winget|choco|scoop|msiexec|setx)${E}|${B}reg(\\.exe)?[[:space:]]+(add|delete|import|load)${E}|(New|Set|Remove)-ItemProperty|SetEnvironmentVariable|Set-ExecutionPolicy|Add-MpPreference|npm[[:space:]]+(i|install)[[:space:]]+(-g|--global)|uv[[:space:]]+(tool|python)[[:space:]]+install"; then
      decide deny "Goh4kMod guard: no system-wide installs, registry or environment changes. Use a portable copy under tools/, or ask Ethan."
    fi
    if has "${B}pip3?[[:space:]]+install" && ! has "uv[[:space:]]+pip[[:space:]]+install"; then
      decide deny "Goh4kMod guard: no global pip installs."
    fi
    if has "${B}(deploy|uninstall)\\.(cmd|bat|ps1|sh)${E}"; then
      decide ask "Goh4kMod guard: this changes the game folder. Check that LOG.md will record it."
    fi
    if has 'Gates of Hell' && has "${B}(cp|mv|rm|del|erase|copy|move|xcopy|robocopy|ren|rename|mklink|tee|touch|Copy-Item|Move-Item|Remove-Item|New-Item|Rename-Item|Set-Content|Add-Content|Out-File)${E}"; then
      decide ask "Goh4kMod guard: this writes into the game install. Game-folder changes should go through deploy.cmd / uninstall.cmd."
    fi
    if has "${B}git([[:space:]].*)?[[:space:]]push${E}|${B}gh[[:space:]]+pr[[:space:]]+create"; then
      decide ask "Goh4kMod guard: publishing step, needs Ethan's OK."
    fi
    ;;
  Edit|Write|MultiEdit|NotebookEdit)
    if printf '%s' "$in" | grep -oE '"(file_path|notebook_path)"[[:space:]]*:[[:space:]]*"[^"]*"' | grep -qi 'Gates of Hell'; then
      decide ask "Goh4kMod guard: this edits a file inside the game install."
    fi
    ;;
esac
exit 0
