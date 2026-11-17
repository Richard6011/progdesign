param(
    [ValidateSet('Build','Test','Run','Package')][string]$Action = 'Build',
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [switch]$TemporaryData
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$localRoot = Join-Path $projectRoot '.local'
$cmake = Join-Path $localRoot 'cmake/bin/cmake.exe'
$ctest = Join-Path $localRoot 'cmake/bin/ctest.exe'
if (!(Test-Path -LiteralPath $cmake)) { throw 'Run tools/ConfigureLocal.ps1 first.' }
$preset = 'c-' + $Configuration.ToLowerInvariant()
# 所有入口使用同一组 .local 工具，避免系统 PATH 中的旧 CMake。
$env:PATH = "$localRoot/qt/bin;$localRoot/mingw/bin;$localRoot/ninja;$localRoot/cmake/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$localRoot/qt/plugins"
Push-Location $projectRoot
try {
    & $cmake --preset $preset
    if ($LASTEXITCODE) { throw 'Configure failed' }
    & $cmake --build --preset $preset --parallel 6
    if ($LASTEXITCODE) { throw 'Build failed' }
    if ($Action -eq 'Test') {
        & $ctest --preset $preset
        if ($LASTEXITCODE) { throw 'Tests failed' }
    }
    $buildRoot = Join-Path $projectRoot ('build/' + $Configuration.ToLowerInvariant())
    if ($Action -eq 'Run') {
        $arguments = @{}
        if ($TemporaryData) {
            # 复用已有 --data-dir，不更改 APPDATA，也不搬动真实账号文件。
            $arguments.ArgumentList = @('--data-dir', ('"' + (Join-Path $localRoot 'run-data') + '"'))
        }
        $process = Start-Process -FilePath (Join-Path $buildRoot 'NoteCollision.exe') -WorkingDirectory $buildRoot -PassThru @arguments
        Write-Host "Started NoteCollision (PID $($process.Id))"
    }
    if ($Action -eq 'Package') {
        $packageRoot = Join-Path $projectRoot 'out/NoteCollision-Qt'
        & $cmake --install $buildRoot --prefix $packageRoot
        if ($LASTEXITCODE) { throw 'Install failed' }
        # Qt 使用动态库，由官方部署工具收集平台插件和 MinGW 运行库。
        $deploy = Join-Path $localRoot 'qt/bin/windeployqt.exe'
        & $deploy ('--' + $Configuration.ToLowerInvariant()) --no-translations --no-opengl-sw (Join-Path $packageRoot 'NoteCollision.exe')
        if ($LASTEXITCODE) { throw 'Qt deployment failed' }
        $licenses = Join-Path $localRoot 'qt/LICENSES'
        if (Test-Path -LiteralPath $licenses) { Copy-Item -LiteralPath $licenses -Destination (Join-Path $packageRoot 'licenses/Qt') -Recurse -Force }
        Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath (Join-Path $projectRoot 'out/NoteCollision-Qt-Windows-x64.zip') -Force
        Write-Host 'Package: out/NoteCollision-Qt-Windows-x64.zip'
    }
} finally { Pop-Location }
