param([ValidateSet('Release', 'Debug')][string]$Configuration = 'Release', [switch]$SkipTests, [switch]$AudioIntegration)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
# The shipped preset and its editable command table must never drift apart.
$presetRows = @(Import-Csv -LiteralPath (Join-Path $projectRoot 'docs\mackie-touchscreen-example.csv'))
$presetHeader = Get-Content -LiteralPath (Join-Path $projectRoot 'src\FaderBridge.MackieHost\Windows80Preset.h') -Raw
$presetCommands = @([regex]::Matches($presetHeader, 'L"([A-Za-z0-9]+)"') | ForEach-Object { $_.Groups[1].Value })
if ($presetRows.Count -ne 80 -or $presetCommands.Count -ne 80) { throw 'Windows 80 preset count mismatch.' }
for ($i = 0; $i -lt 80; ++$i) {
    if ([int]$presetRows[$i].MidiChannel -ne 16 -or [int]$presetRows[$i].Note -ne $i -or $presetRows[$i].CommandId -cne $presetCommands[$i]) {
        throw "Windows 80 preset/table mismatch at Note $i."
    }
}
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 C++ Build Tools and Windows SDK.' }
$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'The Visual Studio v143 C++ toolchain was not found.' }
$msbuild = Join-Path $visualStudio 'MSBuild\Current\Bin\MSBuild.exe'
$solutionDir = "/p:SolutionDir=$($projectRoot.Replace('\', '/'))/"
$project = Join-Path $projectRoot 'src\FaderBridge.MackieHost\FaderBridge.MackieHost.vcxproj'
& $msbuild $project /m /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 $solutionDir /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Mackie host build failed ($LASTEXITCODE)." }
if (-not $SkipTests) {
    $tests = Join-Path $projectRoot 'tests\FaderBridge.Mackie.Tests\FaderBridge.Mackie.Tests.vcxproj'
    & $msbuild $tests /m /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 $solutionDir /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Mackie test build failed ($LASTEXITCODE)." }
    & (Join-Path $projectRoot "artifacts\mackie-tests\$Configuration\MackieTests.exe")
    if ($LASTEXITCODE -ne 0) { throw "Mackie protocol tests failed ($LASTEXITCODE)." }
    & (Join-Path $projectRoot 'tests\FaderBridge.Mackie.Tests\PresetGeneratorTests.ps1')
}
Write-Host "Built: $projectRoot\artifacts\mackie\$Configuration\WindowsFaderBridge.Mackie.exe"
if ($AudioIntegration) {
    # Opt-in: creates and controls ONLY its own silent, nonpersistent audio session.
    $audioTest = Join-Path $projectRoot 'tests\FaderBridge.Mackie.Tests\AudioIntegrationTests.vcxproj'
    & $msbuild $audioTest /m /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 $solutionDir /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Audio integration test build failed ($LASTEXITCODE)." }
    & (Join-Path $projectRoot "artifacts\mackie-audio-tests\$Configuration\MackieAudioTests.exe")
    if ($LASTEXITCODE -ne 0) { throw "Audio integration tests failed ($LASTEXITCODE)." }
}
