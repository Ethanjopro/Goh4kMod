# Self-test for deploy.ps1 / uninstall.ps1 against a fake game folder in scratch\fakegame (the real game
# folder is never touched). Covers: new file, redeploy, game-managed file changed, stock file backup/restore.
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$T = "$Root\scratch\fakegame"
$bin = "$T\binaries\x64"
$fails = 0
function Check($cond, $what) { if ($cond) { "ok    $what" } else { "FAIL  $what"; $script:fails++ } }
function Run($script) {
    $ErrorActionPreference = 'Continue'
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\$script" -TestDir $T 2>&1
    [pscustomobject]@{ Code = $LASTEXITCODE; Out = ($out | Out-String) }
}
function New-Baseline {
    $lines = @('# fake baseline')
    foreach ($f in Get-ChildItem -LiteralPath $bin -Recurse -File) {
        $lines += '{0}  {1,12}  {2}' -f (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash, $f.Length, $f.FullName.Substring($bin.Length + 1)
    }
    $lines | Set-Content -Encoding utf8 "$T\baseline.txt"
}

Remove-Item -Recurse -Force $T -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$bin\mss" | Out-Null
'exe' | Set-Content "$bin\call_to_arms.exe"
'news v1' | Set-Content "$bin\announcements.set"
'mss' | Set-Content "$bin\mss\x.flt"
"# log`n`n## Game-folder changes`n(none yet; deploy.cmd / uninstall.cmd append here)`n`n## Cleanup`n" | Set-Content -NoNewline "$T\LOG.md"
New-Baseline

# A: no stock d3d11.dll
$r = Run 'deploy.ps1'
Check ($r.Code -eq 0 -and (Test-Path "$bin\d3d11.dll")) 'deploy copies d3d11.dll'
Check ((Get-Content "$T\deployed.txt") -match '\|d3d11.dll\|$') 'deployed.txt records it without a backup'
$r = Run 'deploy.ps1'
Check ($r.Out -match 'replaced our previous build') 'redeploy recognises our own build'
'news v2' | Set-Content "$bin\announcements.set"
$r = Run 'uninstall.ps1'
Check ($r.Code -eq 0 -and -not (Test-Path "$bin\d3d11.dll")) 'uninstall removes it, exit 0'
Check ($r.Out -match 'changed by the game itself: announcements.set') 'game-managed change reported as benign'
'extra' | Set-Content "$bin\stray.txt"
$r = Run 'uninstall.ps1'
Check ($r.Code -eq 1 -and $r.Out -match 'extra: stray.txt') 'unexpected extra file fails the check'
Remove-Item "$bin\stray.txt"

# B: a stock d3d11.dll exists
'stock d3d11' | Set-Content "$bin\d3d11.dll"
New-Baseline
$stockHash = (Get-FileHash "$bin\d3d11.dll" -Algorithm SHA256).Hash
$r = Run 'deploy.ps1'
Check ($r.Code -eq 0 -and (Test-Path "$T\stock-backup\d3d11.dll")) 'stock file backed up before replacing'
$r = Run 'deploy.ps1'
Check ((Get-Content "$T\deployed.txt") -match 'stock-backup\\d3d11.dll$') 'redeploy keeps the original backup'
$r = Run 'uninstall.ps1'
Check ($r.Code -eq 0 -and (Get-FileHash "$bin\d3d11.dll" -Algorithm SHA256).Hash -eq $stockHash) 'uninstall restores the stock file, exit 0'

# C: release-style files (settings file, runtime log and diagnostics folder) are removed by uninstall
New-Baseline
$r = Run 'deploy.ps1'
'Scale=2' | Set-Content "$bin\goh4k.ini"
'log' | Set-Content "$bin\goh4k.log"
New-Item -ItemType Directory -Force "$bin\goh4k-diagnostics" | Out-Null
'x' | Set-Content "$bin\goh4k-diagnostics\status.txt"
$r = Run 'uninstall.ps1'
Check ($r.Code -eq 0 -and -not (Test-Path "$bin\goh4k.ini") -and -not (Test-Path "$bin\goh4k.log") -and
    -not (Test-Path "$bin\goh4k-diagnostics")) 'uninstall removes goh4k.ini, goh4k.log and goh4k-diagnostics, exit 0'

$log = Get-Content "$T\LOG.md" -Raw
Check ($log -notmatch '\(none yet' -and ([regex]::Matches($log, '- \d{4}-\d\d-\d\d')).Count -ge 8) 'LOG.md gets dated lines under Game-folder changes'
Check ($log.IndexOf('uninstall check') -lt $log.IndexOf('## Cleanup')) 'lines stay inside the section'

Remove-Item -Recurse -Force $T
if ($fails) { "FAILED: $fails"; exit 1 } else { 'PASSED'; exit 0 }
