# Goh4kMod update check: after a game update, does the mod still work? About 7 minutes, hands off the mouse.
# Run with the game closed:  powershell -ExecutionPolicy Bypass -File src\scripts\update-check.ps1
# Needs the 3840x2160 primary monitor with the game set to Fullscreen 3840x2160 (the click positions are the 4K
# ones from NOTES.md) and a dev build (build.cmd).
#  1. Backs up the game's profiles folder, deploys the dev build (deploy.cmd) and starts the game through Steam.
#  2. Drives it: main menu, the Exit prompt (every frame for 1.2 s), Options, a new "Absolute zero" mission
#     (first gameplay frames, a capture and a trace), the in-mission menu, back out, quit.
#  3. Compares each snapshot with captures\update-check\ref (taken with a verified build) and reads the dev log.
#  4. Puts back what was installed before (nothing: uninstall.cmd; the newest release: deploy.cmd -Release; this
#     dev build: as is) and the profiles, also when a step fails, and prints PASS/FAIL.
# -MakeReference stores this run's snapshots as the references (after checking them by eye).
# A FAIL is a prompt to look, not proof of a bug: an update can change the menus themselves.
param([switch]$MakeReference)
. "$PSScriptRoot\gamefolder-common.ps1"  # $Root, $GameBin, Assert-GameNotRunning, Read-Deployed, Get-Sha256
$Drive = "$Root\build\drive\drive.exe"
$Img = "$Root\build\drive\imgtool.exe"
$Exe = Join-Path $GameBin 'call_to_arms.exe'
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$Out = "$Root\captures\update-check\$Stamp"
$Ref = "$Root\captures\update-check\ref"
$results = New-Object 'Collections.Generic.List[object]'
$refNames = New-Object 'Collections.Generic.List[string]'  # the snapshots compared with a reference

function Result([string]$check, [bool]$ok, [string]$detail) {
    $r = if ($ok) { 'PASS' } else { 'FAIL' }
    $results.Add([pscustomobject]@{ Check = $check; Result = $r; Detail = $detail })
    Write-Host ("{0,-5} {1}: {2}" -f $r, $check, $detail)
}
function Strip([string]$line) { $line -replace '^\[[^\]]*\] ', '' }  # drops the log's [time thread] prefix
function D([string[]]$cmds) { & $Drive @cmds | Out-Null }
function Snap([string]$name) { & $Drive snap "$Out\$name.png" 4 | Out-Null }
function Psnr([string]$a, [string]$b) {
    $line = & $Img diff $a $b 2>&1 | Select-Object -Last 1
    if ("$line" -match 'PSNR ([0-9.]+|inf)') { if ($Matches[1] -eq 'inf') { return 99.0 } return [double]$Matches[1] }
    return 0.0
}
function CompareRef([string]$name, [double]$min) {
    $refNames.Add($name)
    $refFile = "$Ref\$name.png"
    if (-not (Test-Path $refFile)) { Write-Host "      ($($name): no reference yet)"; return }
    $p = Psnr "$Out\$name.png" $refFile
    Result "$name looks like the reference" ($p -ge $min) ("PSNR {0:n1} dB (min {1})" -f $p, $min)
}

Assert-GameNotRunning
if (-not (Test-Path $Drive) -or -not (Test-Path "$Root\build\d3d11.dll")) { throw 'Build first (build.cmd).' }
New-Item -ItemType Directory -Force $Out | Out-Null

# What is installed now, to put back afterwards: nothing, the newest release package, this dev build, or other.
$deployed = Read-Deployed | Where-Object { $_.Name -eq 'd3d11.dll' } | Select-Object -First 1
$pkg = Get-ChildItem "$Root\build\release" -Directory -Filter 'Goh4kMod-*' -ErrorAction SilentlyContinue |
    Sort-Object Name | Select-Object -Last 1
$before = if (-not (Test-Ours (Join-Path $GameBin 'd3d11.dll'))) { 'none' }
          elseif ($deployed -and $pkg -and (Test-Path "$($pkg.FullName)\d3d11.dll") -and
                  $deployed.Hash -eq (Get-Sha256 "$($pkg.FullName)\d3d11.dll")) { 'release' }
          elseif ($deployed -and $deployed.Hash -eq (Get-Sha256 "$Root\build\d3d11.dll")) { 'dev' }
          else { 'other' }
Write-Host "installed before: $before"

# The whole profiles folder (the mission autosaves into the active profile).
$profiles = "$env:USERPROFILE\Documents\My Games\gates of hell\profiles"
if (-not (Test-Path $profiles)) { throw "No game profiles folder at $profiles" }
$backup = "$Root\scratch\settings-backup\update-check-$Stamp"
Copy-Item -LiteralPath $profiles -Destination $backup -Recurse
Write-Host "profiles backed up to $backup"

$logsBefore = @(Get-ChildItem "$Root\scratch\logs\*.log" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
$game = $null
try {
    & "$Root\deploy.cmd" | Select-Object -Last 1 | Write-Host
    if ($LASTEXITCODE) { throw 'deploy.cmd failed.' }
    Start-Process 'steam://rungameid/400750'
    for ($i = 0; $i -lt 90 -and -not $game; $i++) {
        Start-Sleep 1
        $game = Get-Process call_to_arms -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if (-not $game) { throw 'The game did not start.' }
    # The main menu is up once the status file shows this process presenting for a while.
    for ($i = 0; $i -lt 120; $i++) {
        Start-Sleep 1
        $st = Get-Content "$Root\captures\status.txt" -ErrorAction SilentlyContinue
        $frames = $st | Where-Object { $_ -like 'frames=*' } | Select-Object -First 1
        if (($st -contains "pid=$($game.Id)") -and $frames -and [int]($frames -replace 'frames=', '') -gt 3000) { break }
    }
    Start-Sleep 8
    Write-Host "game pid $($game.Id)"

    D focus, $game.Id, wait, 500
    Snap 'menu'; CompareRef 'menu' 28
    # The Exit prompt, every frame: no frame may differ much from the settled prompt (a popup drawn small in the
    # top-left corner, or a magnified backdrop, would).
    D click, 2876, 1304, burst, "$Out\burst-exit", 1200, 8
    Snap 'exit-prompt'; CompareRef 'exit-prompt' 28
    $frames = @(Get-ChildItem "$Out\burst-exit\*.png" | Sort-Object Name)
    if ($frames.Count -gt 1) {
        $last = $frames[-1].FullName
        $odd = @($frames[0..($frames.Count - 2)] | Where-Object { (Psnr $_.FullName $last) -lt 26 }).Count
        Result 'Exit prompt frame by frame' ($odd -eq 0) "$odd of $($frames.Count) frames differ from the settled prompt"
    }
    D click, 2152, 1192, wait, 1500
    D click, 2556, 1304, wait, 1500; Snap 'options'; CompareRef 'options' 28
    D key, esc, wait, 1500
    # A new "Absolute zero" mission (Easy). The campaign's intro videos play when it is new (the profile is put
    # back afterwards, so every run). Only inputs that are harmless once the mission runs: Space (skips or
    # continues; at most a pause toggle in the mission) and CONTINUE's corner (ends the opening cinematic;
    # empty terrain in the mission). Esc or a click in the middle would open the game menu and hit RESTART.
    D click, 1280, 1304, wait, 1500, click, 1072, 388, wait, 1500, click, 1072, 580, wait, 2000, click, 2548, 896, wait, 2000
    D click, 1072, 388, wait, 2000, click, 1068, 344, wait, 3000
    for ($i = 0; $i -lt 14; $i++) { D key, space, wait, 8000, click, 3680, 76, wait, 2000 }
    Snap 'mission'
    Set-Content -Encoding ascii "$Root\captures\request.txt" "capture update-check`r`ntrace update-check"
    Start-Sleep 5
    D key, esc, wait, 1500; Snap 'mission-menu'; CompareRef 'mission-menu' 26
    D click, 1916, 1316, wait, 1500, click, 2056, 1194, wait, 15000; Snap 'back-in-menus'
    D click, 2876, 48, wait, 1500, click, 2056, 1194
    for ($i = 0; $i -lt 60 -and (Get-Process -Id $game.Id -ErrorAction SilentlyContinue); $i++) { Start-Sleep 1 }
} finally {
    # Whatever happened above: put back what was installed before, and the profiles.
    if ($game -and (Get-Process -Id $game.Id -ErrorAction SilentlyContinue)) {
        Write-Host "the game is still running (pid $($game.Id)): ending it"
        Stop-Process -Id $game.Id -Force
        Start-Sleep 3
    }
    $putBack = switch ($before) {
        'none' { & "$Root\uninstall.cmd" /nopause | Select-Object -Last 1 | Write-Host; 'nothing installed (uninstall.cmd)' }
        'release' { & "$Root\deploy.cmd" -Release | Select-Object -Last 1 | Write-Host; "the release ($($pkg.Name))" }
        'dev' { 'this dev build (left installed)' }
        default { $null }
    }
    $putBackOk = $putBack -and -not $LASTEXITCODE
    & robocopy $backup $profiles /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
}
Result 'installation put back' $putBackOk $(if ($putBack) { $putBack } else {
    'an older Goh4kMod build was installed; this dev build is now: run deploy.cmd -Release or uninstall.cmd' })

# The dev log of this run.
$log = Get-ChildItem "$Root\scratch\logs\*.log" | Where-Object { $logsBefore -notcontains $_.FullName } |
    Where-Object { (Select-String -LiteralPath $_.FullName -Pattern 'call_to_arms\.exe' -Quiet) } |
    Sort-Object LastWriteTime | Select-Object -Last 1
if ($log) {
    $text = Get-Content -LiteralPath $log.FullName
    $build = $text | Where-Object { $_ -match 'game: built' } | Select-Object -First 1
    Result 'game build' ("$build" -match 'the build this mod version was verified with') (Strip $build)
    $hooks = "$($text | Where-Object { $_ -like '*input hooks: * user32*' } | Select-Object -First 1)"
    $allHooks = ($hooks -match 'input hooks: (\d+) of (\d+)') -and $Matches[1] -eq $Matches[2]
    Result 'input hooks' $allHooks (Strip $hooks)
    Result 'health line' ([bool]($text -match 'health: .*k 2/1')) 'swapchain wrapped, game window followed, k 2/1 at 4K'
    Result 'UI shader recognised' ([bool]($text -match "recognised as the UI's")) 'rule 1 drew label surfaces'
    Result 'rule 1 and 2 applied' ([bool]($text -match 'ui-label-viewport') -and [bool]($text -match 'ui-screen-viewport')) 'notes ui-label-viewport, ui-screen-viewport'
    Result 'popup stand-in' ([bool]($text -match 'popup stand-in \d+x\d+ fmt')) 'rule 3 found the Exit prompt backdrop'
    $dis = @($text | Where-Object { $_ -match 'DISAGREES' }).Count
    Result 'rule 3 self-check' ($dis -le 2) "$dis disagreements (one or two per display change is known)"
    $warn = @($text | Where-Object { $_ -match 'WARNING' })
    Result 'no warnings' ($warn.Count -eq 0) $(if ($warn.Count) { ($warn | Select-Object -First 2) -join ' / ' } else { 'none' })
    $session = $text | Where-Object { $_ -match 'session:' } | Select-Object -Last 1
    Result 'session summary' ([bool]$session) (Strip $session)
    $text | Where-Object { $_ -match 'patched over' } | ForEach-Object { Write-Host "      info: $(Strip $_)" }
} else {
    Result 'dev log' $false 'no log from this run in scratch\logs'
}

function TreeHashes([string]$dir) {  # "relative path hash" per file
    @(Get-ChildItem -LiteralPath $dir -Recurse -File | ForEach-Object { "$($_.FullName.Substring($dir.Length)) $(Get-Sha256 $_.FullName)" })
}
$diff = Compare-Object (TreeHashes $backup) (TreeHashes $profiles)
Result 'profiles restored' (-not $diff) $(if ($diff) { 'differ from the backup' } else { "byte-identical to $backup" })

if ($MakeReference) {
    New-Item -ItemType Directory -Force $Ref | Out-Null
    foreach ($n in $refNames) { Copy-Item "$Out\$n.png" "$Ref\$n.png" -Force }
    Write-Host "references stored in $Ref"
}
$failed = @($results | Where-Object { $_.Result -eq 'FAIL' }).Count
$results | Format-Table -AutoSize | Out-String -Width 200 | Set-Content -Encoding ascii "$Out\report.txt"
$exeInfo = Get-Item $Exe
$summary = "{0} {1}: {2} checks, {3} failed; call_to_arms.exe {4} bytes, {5:yyyy-MM-dd HH:mm}; snapshots {6}" -f `
    (Get-Date -Format 'yyyy-MM-dd HH:mm'), $(if ($failed) { 'FAIL' } else { 'PASS' }), $results.Count, $failed,
    $exeInfo.Length, $exeInfo.LastWriteTime, $Out
Add-Content -Encoding ascii "$Root\scratch\update-check-history.txt" $summary
if (-not $failed) {  # read by .claude\hooks\briefing.sh: the exe's size and modification time (Unix seconds)
    $mtime = ([DateTimeOffset]$exeInfo.LastWriteTimeUtc).ToUnixTimeSeconds()
    Set-Content -Encoding ascii "$Root\scratch\update-check-last-pass.txt" "$($exeInfo.Length) $mtime"
}
Write-Host $summary
Write-Host 'Look at mission.png and back-in-menus.png too: the mission path has no reference check.'
exit $failed
