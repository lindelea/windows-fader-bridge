param([ValidateSet('Release', 'Debug')][string]$Configuration = 'Release', [switch]$SkipTests, [switch]$AudioIntegration)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
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
