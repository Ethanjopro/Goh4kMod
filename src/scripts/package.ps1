# Packages the release build (build.cmd release) into build\release\Goh4kMod-<version>.zip:
#   d3d11.dll, dxgi.dll (release flavour), README.txt, THIRD-PARTY-NOTICES.txt. No uninstaller script: antivirus
#   tools flag a .bat that deletes DLLs; players delete the two DLLs themselves (README).
# No goh4k.ini: the release defaults are the shipped settings. The DLL still reads one if a user adds it.
# Refuses to package if a DLL contains this machine's user name or project path (privacy check).
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Version = '2.1.4'
$Out = "$Root\build\release"
$Stage = "$Out\Goh4kMod-$Version"
$Zip = "$Out\Goh4kMod-$Version.zip"

foreach ($dll in 'd3d11.dll', 'dxgi.dll') {
    $path = "$Out\$dll"
    if (-not (Test-Path $path)) { throw "$path missing; run build.cmd release" }
    $bytes = [IO.File]::ReadAllBytes($path)
    $ascii = [Text.Encoding]::ASCII.GetString($bytes)
    $utf16 = [Text.Encoding]::Unicode.GetString($bytes)
    foreach ($needle in @($env:USERNAME, $Root)) {
        if ($ascii.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
            $utf16.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "$dll contains '$needle'; not packaging a build that leaks local paths"
        }
    }
    $info = (Get-Item $path).VersionInfo
    if ($info.ProductName -ne 'Goh4kMod') { throw "$dll is not a Goh4kMod build" }
}

Remove-Item -Recurse -Force $Stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Stage | Out-Null
Copy-Item "$Out\d3d11.dll", "$Out\dxgi.dll" $Stage
# Text files go out with CRLF whatever the checkout did: Notepad expects it.
foreach ($name in 'README.txt', 'THIRD-PARTY-NOTICES.txt') {
    $text = [IO.File]::ReadAllText("$Root\src\package\$name") -replace '\r?\n', "`r`n"
    [IO.File]::WriteAllText("$Stage\$name", $text, [Text.Encoding]::ASCII)
}
Remove-Item $Zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$Stage\*" -DestinationPath $Zip
$hashes = Get-ChildItem $Stage | ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash, $_.Name }
$hashes | Set-Content -Encoding ascii "$Out\Goh4kMod-$Version.sha256.txt"
"packaged $Zip ($([math]::Round((Get-Item $Zip).Length / 1KB)) KB):"
Get-ChildItem $Stage | ForEach-Object { '  {0,-26} {1,8:n0} bytes' -f $_.Name, $_.Length }
