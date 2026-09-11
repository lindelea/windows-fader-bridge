param(
    [string]$InnoSetup = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyDir = Join-Path $projectRoot 'artifacts\installer-dependencies'
$redist = Join-Path $dependencyDir 'VC_redist.x64.exe'
$chineseMessages = Join-Path $dependencyDir 'ChineseSimplified.isl'
$installerDir = Join-Path $projectRoot 'artifacts\installers'

if (-not (Test-Path -LiteralPath $InnoSetup)) {
    throw "Inno Setup 6 was not found at $InnoSetup"
}

New-Item -ItemType Directory -Force -Path $dependencyDir, $installerDir | Out-Null

if (-not (Test-Path -LiteralPath $redist)) {
    Invoke-WebRequest -Uri 'https://aka.ms/vc14/vc_redist.x64.exe' -OutFile $redist
}

if (-not (Test-Path -LiteralPath $chineseMessages)) {
    Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/kira-96/Inno-Setup-Chinese-Simplified-Translation/1ff90acc4ed4aee82b1cda43253243deee3daed4/ChineseSimplified.isl' -OutFile $chineseMessages
}
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $chineseMessages).Hash -ne
    'BF0751FA176569C6FAA2F6E17ED2734617BEF325D5CC06EAE030FDD0258EE778') {
    throw 'The pinned Simplified Chinese Inno Setup translation did not match its expected hash.'
}

$signature = Get-AuthenticodeSignature -LiteralPath $redist
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft') {
    throw 'The Microsoft Visual C++ Redistributable signature is not valid.'
}

$packages = @(
    'windows-fader-bridge-eucon.iss',
    'windows-fader-bridge-mackie.iss',
    'uad-console-bridge-eucon.iss'
)

foreach ($package in $packages) {
    & $InnoSetup (Join-Path $projectRoot "packaging\$package")
    if ($LASTEXITCODE -ne 0) {
        throw "Installer build failed: $package"
    }
}

Get-ChildItem -LiteralPath $installerDir -Filter '*.exe' |
    Sort-Object Name |
    Select-Object Name, Length, @{Name='SHA256'; Expression={(Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash}}
