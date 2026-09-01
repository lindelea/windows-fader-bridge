param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$CoreOnly,
    [switch]$TransportTests,
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$NativeOutputName = 'apollo-eucon',
    [string]$AvidEuconSdkDir = 'C:\Program Files\Avid\EUCON SDK'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools and Windows SDK.' }
$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'The v143 C++ toolchain was not found.' }
$msbuild = Join-Path $visualStudio 'MSBuild\Current\Bin\MSBuild.exe'
$solutionDir = "/p:SolutionDir=$($projectRoot.Replace('\', '/'))/"
& $msbuild (Join-Path $projectRoot 'tests\ApolloBridge.Tests\ApolloBridge.Tests.vcxproj') /m /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 $solutionDir /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Apollo core test build failed.' }
& (Join-Path $projectRoot "artifacts\apollo-tests\$Configuration\ApolloTests.exe")
if ($LASTEXITCODE -ne 0) { throw 'Apollo core tests failed.' }
if ($TransportTests) {
    & $msbuild (Join-Path $projectRoot 'tests\ApolloBridge.Tests\ApolloBridge.TransportTests.vcxproj') /m /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 $solutionDir /v:minimal
    if ($LASTEXITCODE -ne 0) { throw 'Apollo transport test build failed.' }
    & (Join-Path $projectRoot "artifacts\apollo-tests\$Configuration\ApolloTransportTests.exe")
    if ($LASTEXITCODE -ne 0) { throw 'Apollo transport tests failed.' }
}
if (-not $CoreOnly) {
    if (-not (Test-Path -LiteralPath (Join-Path $AvidEuconSdkDir 'include\EuConManager.h'))) { throw 'Obtain and install the Avid EUCON SDK separately. The core tests do not need it.' }
    $nativeOutput = Join-Path $projectRoot "artifacts\$NativeOutputName\$Configuration\"
    $nativeIntermediate = Join-Path $projectRoot "obj\$NativeOutputName\$Configuration\"
    & $msbuild (Join-Path $projectRoot 'src\ApolloBridge.EuconHost\ApolloBridge.EuconHost.vcxproj') /m /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 $solutionDir "/p:AvidEuconSdkDir=$AvidEuconSdkDir" "/p:OutDir=$nativeOutput" "/p:IntDir=$nativeIntermediate" /v:minimal
    if ($LASTEXITCODE -ne 0) { throw 'Apollo EUCON host build failed.' }
    & (Join-Path $nativeOutput 'ApolloBridge.Eucon.exe') --desktop-self-test | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Desktop settings tests failed.' }
    Write-Host "Built: ${nativeOutput}ApolloBridge.Eucon.exe"
}
