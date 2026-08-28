$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$sdkHeader = 'C:\Program Files\Avid\EUCON SDK\include\EuConManager.h'
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $sdkHeader)) {
    throw 'Avid EUCON SDK 2026.4 is not installed.'
}

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio 2022 Build Tools are not installed.'
}

$visualStudio = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $visualStudio) {
    throw 'The Visual Studio v143 C++ toolchain was not found.'
}

$msbuild = Join-Path $visualStudio 'MSBuild\Current\Bin\MSBuild.exe'
$project = Join-Path $projectRoot 'src\FaderBridge.EuconHost\FaderBridge.EuconHost.vcxproj'
$audioHost = Join-Path $projectRoot 'src\FaderBridge.AudioHost\FaderBridge.AudioHost.csproj'
$output = Join-Path $projectRoot 'artifacts\eucon\Release'

& dotnet publish $audioHost --configuration Release --output $output --no-self-contained

if ($LASTEXITCODE -ne 0) {
    throw "Windows audio host publish failed with exit code $LASTEXITCODE."
}

& $msbuild $project /m /t:Build /p:Configuration=Release /p:Platform=x64 `
    "/p:SolutionDir=$projectRoot\" /v:minimal

if ($LASTEXITCODE -ne 0) {
    throw "EUCON host build failed with exit code $LASTEXITCODE."
}

Write-Host "Built: $output\FaderBridge.EuconHost.exe"
