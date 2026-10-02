# Copies the Goh4kMod proxy into the game's binaries\x64. A stock file with the same name is backed up to
# scratch\stock-backup first. Records hashes in scratch\deployed.txt and the change in LOG.md.
# Run through deploy.cmd. -Release deploys the packaged release build (build\release\Goh4kMod-*\: both DLLs
# plus goh4k.ini) exactly as a user would install it. -TestDir points it at a fake game folder for the self-test.
param([string]$TestDir, [switch]$Release)
. "$PSScriptRoot\gamefolder-common.ps1"

Assert-GameNotRunning
if (-not (Test-Path "$GameBin\call_to_arms.exe")) { throw "call_to_arms.exe not found in $GameBin" }
if (-not (Test-Path $Baseline)) { throw "Baseline $Baseline is missing; take it before the first deploy." }

if ($Release) {
    $pkg = Get-ChildItem "$Root\build\release" -Directory -Filter 'Goh4kMod-*' | Sort-Object Name | Select-Object -Last 1
    if (-not $pkg) { throw "No packaged release in build\release; run build.cmd release first." }
    $files = @('d3d11.dll', 'dxgi.dll', 'goh4k.ini') | ForEach-Object { @{ Src = "$($pkg.FullName)\$_"; Name = $_ } }
} else {
    $files = @(@{ Src = "$Root\build\d3d11.dll"; Name = 'd3d11.dll' }, @{ Src = "$Root\build\dxgi.dll"; Name = 'dxgi.dll' })
}
$records = New-Object 'Collections.Generic.List[object]'
foreach ($r in @(Read-Deployed)) { $records.Add($r) }
$logLines = @()

foreach ($f in $files) {
    if (-not (Test-Path $f.Src)) { throw "Missing $($f.Src); run build.cmd first." }
    if ($f.Name -like '*.dll' -and -not (Test-Ours $f.Src)) { throw "$($f.Src) is not a $Product build." }
    $dst = Join-Path $GameBin $f.Name
    $old = $records | Where-Object { $_.Name -eq $f.Name } | Select-Object -First 1
    $backup = if ($old) { $old.Backup } else { '' }
    $note = 'new file'
    if (Test-Path -LiteralPath $dst) {
        if ((Test-Ours $dst) -or $ModFileNames -contains $f.Name) {
            $note = 'replaced our previous build'
        } else {
            New-Item -ItemType Directory -Force $BackupDir | Out-Null
            $backup = Join-Path $BackupDir $f.Name
            if ((Test-Path $backup) -and (Get-Sha256 $backup) -ne (Get-Sha256 $dst)) {
                $backup = "$backup.$(Get-Date -Format 'yyyyMMdd-HHmmss')"
            }
            Copy-Item -LiteralPath $dst $backup -Force
            $note = "replaced a stock file, backed up to $($backup.Substring($Root.Length + 1))"
        }
    }
    Copy-Item -LiteralPath $f.Src $dst -Force
    $hash = Get-Sha256 $dst
    if ($hash -ne (Get-Sha256 $f.Src)) { throw "Copy of $($f.Name) doesn't match the build." }
    if ($old) { [void]$records.Remove($old) }
    $records.Add([pscustomobject]@{ Hash = $hash; Name = $f.Name; Backup = $backup })
    $logLines += "- $(Get-Stamp) deploy: $($f.Name) sha256 $($hash.Substring(0, 12)) ($note)"
    Write-Host "deployed $($f.Name) -> $dst ($note)"
}

Write-Deployed $records.ToArray()
Add-GameFolderLog $logLines
Write-Host "Recorded in LOG.md and scratch\deployed.txt."
Write-Host "REMINDER: run uninstall.cmd before playing multiplayer." -ForegroundColor Yellow
