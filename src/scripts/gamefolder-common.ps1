# Shared by deploy.ps1 and uninstall.ps1 (dot-sourced). These two scripts, started through deploy.cmd and
# uninstall.cmd, are the only things that write into the game folder.
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
if ($TestDir) {
    # Self-test against a fake game folder (src\scripts\test-gamefolder.ps1); never touches the real one.
    $GameBin = "$TestDir\binaries\x64"
    $DeployedList = "$TestDir\deployed.txt"
    $Baseline = "$TestDir\baseline.txt"
    $BackupDir = "$TestDir\stock-backup"
    $RecordDir = $TestDir
    $LogFile = "$TestDir\LOG.md"
} else {
    $GameBin = (Get-Content "$Root\.claude\game-bin.txt" -Raw).Trim() -replace '/', '\'
    $DeployedList = "$Root\scratch\deployed.txt"
    $Baseline = "$Root\scratch\baseline-binaries-x64.txt"
    $BackupDir = "$Root\scratch\stock-backup"
    $RecordDir = "$Root\scratch\deploy-history"  # uninstall keeps old deployed.txt files here as deployed-<time>.txt
    $LogFile = "$Root\LOG.md"
}
$GameRoot = Split-Path (Split-Path $GameBin)
$Product = 'Goh4kMod'
# Files the game rewrites by itself; a baseline mismatch on these is reported but not a failure.
$GameManaged = @('announcements.set')
# Non-DLL files that belong to the mod by name: the release build's optional settings file, its log, the
# uninstaller releases up to 2.1 shipped, and its diagnostics folder (only created when Diagnostics=1).
$ModFileNames = @('goh4k.ini', 'goh4k.log', 'Uninstall Goh4kMod.bat')
$ModDirNames = @('goh4k-diagnostics')

function Assert-GameNotRunning {
    $running = @(Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($GameRoot, [StringComparison]::OrdinalIgnoreCase) })
    if ($running.Count) {
        throw "The game is running ($(($running | ForEach-Object { "$($_.Name) pid $($_.Id)" }) -join ', ')). Quit it first."
    }
}

function Get-Sha256([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }

function Test-Ours([string]$path) {
    (Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path).VersionInfo.ProductName -eq $Product)
}

# deployed.txt: "<sha256>|<file name>|<backup path or empty>" per line
function Read-Deployed {
    if (-not (Test-Path $DeployedList)) { return @() }
    @(Get-Content $DeployedList | Where-Object { $_ -match '\|' } | ForEach-Object {
        $p = $_ -split '\|'
        [pscustomobject]@{ Hash = $p[0]; Name = $p[1]; Backup = $p[2] }
    })
}

function Write-Deployed([object[]]$records) {
    if ($null -eq $records -or $records.Length -eq 0) { Remove-Item -LiteralPath $DeployedList -ErrorAction SilentlyContinue; return }
    $records | ForEach-Object { "$($_.Hash)|$($_.Name)|$($_.Backup)" } | Set-Content -Encoding ascii $DeployedList
}

function Get-Stamp { Get-Date -Format 'yyyy-MM-dd HH:mm' }

# Appends lines under "## Game-folder changes" in LOG.md, dropping the "(none yet ...)" placeholder.
function Add-GameFolderLog([string[]]$lines) {
    $text = [IO.File]::ReadAllText($LogFile)
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $all = [Collections.Generic.List[string]]($text -split '\r?\n')
    $start = $all.IndexOf('## Game-folder changes')
    if ($start -lt 0) {
        $all.Add(''); $all.Add('## Game-folder changes'); $start = $all.Count - 1
    }
    $end = $start + 1
    while ($end -lt $all.Count -and -not $all[$end].StartsWith('## ')) { $end++ }
    for ($i = $end - 1; $i -gt $start; $i--) { if ($all[$i] -match '^\(none yet') { $all.RemoveAt($i); $end-- } }
    $insert = $end
    while ($insert - 1 -gt $start -and $all[$insert - 1].Trim() -eq '') { $insert-- }
    $all.InsertRange($insert, [string[]]$lines)
    [IO.File]::WriteAllText($LogFile, ($all -join $nl), (New-Object Text.UTF8Encoding($false)))
}
