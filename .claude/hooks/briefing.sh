#!/usr/bin/env bash
# Goh4kMod session briefing (SessionStart): a few lines Claude reads at the start of every session.
cd "${CLAUDE_PROJECT_DIR:-.}" 2>/dev/null || exit 0
echo "== Goh4kMod briefing =="
if [ -d .git ]; then
  b=$(git branch --show-current 2>/dev/null)
  n=$(git status --porcelain 2>/dev/null | wc -l | tr -d ' ')
  l=$(git log -1 --format='%h %s (%cr)' 2>/dev/null)
  echo "repo: branch ${b:-?}, ${n:-0} uncommitted, last commit: ${l:-none}"
fi
if [ -f .claude/game-bin.txt ]; then
  g=$(tr -d '\r' < .claude/game-bin.txt)
  command -v cygpath >/dev/null 2>&1 && g=$(cygpath -u "$g")
  if [ -d "$g" ]; then
    x=$(cd "$g" && ls -1 dxgi.dll d3d11.dll d3d9.dll dinput8.dll winmm.dll version.dll dxvk.conf ./*.asi ./*.ini 2>/dev/null | sed 's#^\./##' | tr '\n' ' ')
    echo "proxy/loader files in game binaries/x64: ${x:-none}"
    # Has the game been updated since the last passing update check (src/scripts/update-check.ps1)?
    if [ -f "$g/call_to_arms.exe" ]; then
      now="$(stat -c '%s %Y' "$g/call_to_arms.exe" 2>/dev/null)"
      last="$(tr -d '\r\n' < scratch/update-check-last-pass.txt 2>/dev/null)"
      if [ -z "$last" ]; then echo "update check: no PASS recorded yet"
      elif [ "$now" != "$last" ]; then echo "update check: call_to_arms.exe changed since the last PASS: run src/scripts/update-check.ps1"
      else echo "update check: the game exe is the one that last passed"; fi
    fi
  else
    echo "game path in .claude/game-bin.txt not found: $g"
  fi
fi
[ -f LOG.md ] && { echo "--- LOG.md (last 20 lines)"; tail -n 20 LOG.md; }
exit 0
