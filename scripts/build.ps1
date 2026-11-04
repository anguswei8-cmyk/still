param(
    [string]$QtRoot = '',
    [switch]$Test,
    [switch]$Deploy
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$localQt = Join-Path $projectRoot '.tools\Qt\6.8.3\mingw_64'
if (-not $QtRoot -and (Test-Path -LiteralPath $localQt)) { $QtRoot = $localQt }
if (-not $QtRoot) { throw 'Specify -QtRoot with a Qt >= 6.8 kit including Widgets, Qt Quick and Quick Controls, e.g. C:\Qt\6.8.3\mingw_64' }
$QtRoot = [IO.Path]::GetFullPath($QtRoot)
$localCompiler = Join-Path $projectRoot '.tools\Qt\Tools\mingw1310_64\bin'
$localPython = Join-Path $projectRoot '.tools\python'
$localCmake = Join-Path $localPython 'cmake\data\bin\cmake.exe'
$cmakeCommand = if (Test-Path -LiteralPath $localCmake) { $localCmake } else { 'cmake' }
$env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
$configureArguments = @('-S', $projectRoot, '-B', (Join-Path $projectRoot 'build'), '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$QtRoot")
if (Test-Path -LiteralPath $localCompiler) {
    $env:PATH = $localCompiler + ';' + $env:PATH
    $env:PYTHONPATH = $localPython
    $ninjaPath = Join-Path $localPython 'bin\ninja.exe'
    $configureArguments += @('-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninjaPath", "-DCMAKE_CXX_COMPILER=$(Join-Path $localCompiler 'g++.exe')")
}
& $cmakeCommand @configureArguments
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $cmakeCommand --build (Join-Path $projectRoot 'build') --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if ($Test) {
    $ctestCommand = Join-Path (Split-Path -Parent $cmakeCommand) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $ctestCommand)) { $ctestCommand = 'ctest' }
    & $ctestCommand --test-dir (Join-Path $projectRoot 'build') --build-config Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
}
if ($Deploy) {
    & $cmakeCommand --install (Join-Path $projectRoot 'build') --config Release --prefix (Join-Path $projectRoot 'dist\Still')
    if ($LASTEXITCODE -ne 0) { throw 'Deployment failed' }
}
