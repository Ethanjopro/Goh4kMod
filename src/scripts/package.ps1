# Packages the release build (build.cmd release) into build\release\Goh4kMod-<version>.zip:
#   d3d11.dll, dxgi.dll (release flavour), goh4k.ini, README.txt, THIRD-PARTY-NOTICES.txt
# Refuses to package if a DLL contains this machine's user name or project path (privacy check).
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Version = '1.0'
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
Copy-Item "$Root\src\package\goh4k.ini", "$Root\src\package\README.txt", "$Root\src\package\THIRD-PARTY-NOTICES.txt" $Stage
Remove-Item $Zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$Stage\*" -DestinationPath $Zip
$hashes = Get-ChildItem $Stage | ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash, $_.Name }
$hashes | Set-Content -Encoding ascii "$Out\Goh4kMod-$Version.sha256.txt"
"packaged $Zip ($([math]::Round((Get-Item $Zip).Length / 1KB)) KB):"
Get-ChildItem $Stage | ForEach-Object { '  {0,-26} {1,8:n0} bytes' -f $_.Name, $_.Length }
