param(
    [string]$SDL3SourceDir = '',
    [string]$SDL3PackageDir = '',
    [string]$BuildDir = "$PSScriptRoot/build",
    [string]$Configuration = 'Release',
    [switch]$RenderChecks
)
$ErrorActionPreference = 'Stop'
$configure = @('-S', $PSScriptRoot, '-B', $BuildDir, '-A', 'x64', '-DBUILD_TESTING=ON')
if ($SDL3SourceDir) { $configure += "-DSDL3_SOURCE_DIR=$SDL3SourceDir" }
if ($SDL3PackageDir) { $configure += "-DSDL3_DIR=$SDL3PackageDir" }
if ($RenderChecks) { $configure += '-DGRPP_RUN_RENDER_TESTS=ON' }
& cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
& cmake --build $BuildDir --config $Configuration --target check --parallel 4 -- /nr:false
if ($LASTEXITCODE -ne 0) { throw 'Build or checks failed' }
Write-Output "Built and checked: $BuildDir/$Configuration/grpp_enhanced.exe"
