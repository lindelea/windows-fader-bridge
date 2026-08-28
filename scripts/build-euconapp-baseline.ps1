$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$solution = Join-Path $projectRoot 'src\FaderBridge.EuConApp\EuConApp\platform\win\EuConApp_vc17.sln'
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $solution)) {
    throw 'The workspace EuConApp baseline has not been copied.'
}

$visualStudio = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $visualStudio) {
    throw 'The Visual Studio v143 C++ toolchain was not found.'
}

$msbuild = Join-Path $visualStudio 'MSBuild\Current\Bin\MSBuild.exe'
& $msbuild $solution /m /t:Rebuild '/p:Configuration=Release + DLL crtl' `
    /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) {
    throw "EuConApp baseline build failed with exit code $LASTEXITCODE."
}

$output = Join-Path (Split-Path $solution) 'x64\Release + DLL crtl\EuConApp.exe'
Write-Host "Built: $output"
