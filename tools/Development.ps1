param(
    [ValidateSet('Build','Run','Package')][string]$Action = 'Build',
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$localRoot = Join-Path $projectRoot '.local'
$cmake = Join-Path $localRoot 'cmake/bin/cmake.exe'
if (!(Test-Path -LiteralPath $cmake)) { throw 'Run tools/ConfigureLocal.ps1 first.' }
$preset = 'c-' + $Configuration.ToLowerInvariant()
$env:PATH = "$localRoot/qt/bin;$localRoot/mingw/bin;$localRoot/ninja;$localRoot/cmake/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$localRoot/qt/plugins"
Push-Location $projectRoot
try {
    & $cmake --preset $preset
    if ($LASTEXITCODE) { throw 'Configure failed' }
    & $cmake --build --preset $preset --parallel 6
    if ($LASTEXITCODE) { throw 'Build failed' }
    $buildRoot = Join-Path $projectRoot ('build/' + $Configuration.ToLowerInvariant())
    if ($Action -eq 'Run') {
        $process = Start-Process -FilePath (Join-Path $buildRoot 'NoteCollision.exe') -WorkingDirectory $buildRoot -PassThru
        Write-Host "Started NoteCollision (PID $($process.Id))"
    }
    if ($Action -eq 'Package') {
        $packageRoot = Join-Path $projectRoot 'out/NoteCollision-Qt'
        & $cmake --install $buildRoot --prefix $packageRoot
        if ($LASTEXITCODE) { throw 'Install failed' }
        $deploy = Join-Path $localRoot 'qt/bin/windeployqt.exe'
        & $deploy ('--' + $Configuration.ToLowerInvariant()) --no-translations --no-opengl-sw (Join-Path $packageRoot 'NoteCollision.exe')
        if ($LASTEXITCODE) { throw 'Qt deployment failed' }
        $licenses = Join-Path $localRoot 'qt/LICENSES'
        if (Test-Path -LiteralPath $licenses) { Copy-Item -LiteralPath $licenses -Destination (Join-Path $packageRoot 'licenses/Qt') -Recurse -Force }
        Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath (Join-Path $projectRoot 'out/NoteCollision-Qt-Windows-x64.zip') -Force
        Write-Host 'Package: out/NoteCollision-Qt-Windows-x64.zip'
    }
} finally { Pop-Location }
