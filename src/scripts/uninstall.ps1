# Removes Goh4kMod files from the game's binaries\x64, restores any stock file deploy backed up, then compares
# the folder with the Phase 1 baseline (scratch\baseline-binaries-x64.txt) and records the result in LOG.md.
# Exit code 0 = folder matches the baseline (files the game manages itself may differ). Run through uninstall.cmd.
# -TestDir points it at a fake game folder for the self-test.
param([string]$TestDir)
. "$PSScriptRoot\gamefolder-common.ps1"

Assert-GameNotRunning
$records = @(Read-Deployed)
$names = @($records | ForEach-Object { $_.Name })
# Also catch any of our DLLs that aren't in the list (e.g. deployed.txt was lost).
$names += @(Get-ChildItem -LiteralPath $GameBin -File | Where-Object { $_.VersionInfo.ProductName -eq $Product } |
    ForEach-Object { $_.Name })
$names = @($names | Sort-Object -Unique)
$logLines = @()
$problems = 0

foreach ($name in $names) {
    $dst = Join-Path $GameBin $name
    $rec = $records | Where-Object { $_.Name -eq $name } | Select-Object -First 1
    $hasBackup = $rec -and $rec.Backup -and (Test-Path -LiteralPath $rec.Backup)
    if ((Test-Path -LiteralPath $dst) -and $hasBackup -and (Get-Sha256 $dst) -eq (Get-Sha256 $rec.Backup)) {
        Write-Host "stock $name already in place (e.g. restored by Steam)"
        continue
    }
    if (Test-Path -LiteralPath $dst) {
        if ((Test-Ours $dst) -or ($rec -and (Get-Sha256 $dst) -eq $rec.Hash)) {
            Remove-Item -LiteralPath $dst -Force
            $logLines += "- $(Get-Stamp) uninstall: removed $name"
            Write-Host "removed $name"
        } else {
            $logLines += "- $(Get-Stamp) uninstall: $name is not ours, left in place"
            Write-Host "WARNING: $name is not a $Product file; left in place" -ForegroundColor Yellow
            $problems++
            continue
        }
    }
    if ($hasBackup) {
        Copy-Item -LiteralPath $rec.Backup $dst -Force
        $logLines += "- $(Get-Stamp) uninstall: restored stock $name from $($rec.Backup.Substring($Root.Length + 1))"
        Write-Host "restored stock $name"
    }
}

# Compare with the baseline.
$base = @{}
foreach ($line in Get-Content $Baseline) {
    if ($line -match '^([0-9A-F]{64})\s+(\d+)\s+(.+)$') { $base[$Matches[3]] = $Matches[1] }
}
$now = @{}
foreach ($f in Get-ChildItem -LiteralPath $GameBin -Recurse -File) {
    $now[$f.FullName.Substring($GameBin.Length + 1)] = Get-Sha256 $f.FullName
}
$diffs = @()
foreach ($k in $base.Keys) {
    if (-not $now.ContainsKey($k)) { $diffs += "missing: $k" }
    elseif ($now[$k] -ne $base[$k]) { $diffs += "changed: $k" }
}
foreach ($k in $now.Keys) { if (-not $base.ContainsKey($k)) { $diffs += "extra: $k" } }
$real = @($diffs | Where-Object { $d = $_; -not ($GameManaged | Where-Object { $d -like "*: $_" }) })
$benign = @($diffs | Where-Object { $real -notcontains $_ })

if ($real.Count -eq 0) {
    $msg = "folder matches the baseline ($($base.Count) files)"
    if ($benign.Count) { $msg += "; changed by the game itself: $(($benign | ForEach-Object { $_ -replace '^\w+: ', '' }) -join ', ')" }
    Write-Host "OK: $msg" -ForegroundColor Green
} else {
    $msg = "folder differs from the baseline: $($real -join '; ')"
    Write-Host "CHECK: $msg" -ForegroundColor Yellow
    $problems++
}
$logLines += "- $(Get-Stamp) uninstall check: $msg"

if (Test-Path $DeployedList) {
    Move-Item -LiteralPath $DeployedList "$Root\scratch\deployed-$(Get-Date -Format 'yyyyMMdd-HHmmss').txt"
}
Add-GameFolderLog $logLines
exit ([int]($problems -gt 0))
