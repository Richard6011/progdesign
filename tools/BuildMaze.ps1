param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmake = Join-Path $projectRoot '.local/cmake/bin/cmake.exe'
if (!(Test-Path -LiteralPath $cmake)) { throw 'Run tools/ConfigureLocal.ps1 first.' }
$preset = 'c-' + $Configuration.ToLowerInvariant()

Push-Location $projectRoot
try {
    # 复用 VS Code 已有的 CMake 预设与编译器，只编译独立的 EasyX 目标。
    & $cmake --preset $preset
    if ($LASTEXITCODE) { throw 'Configure failed' }
    & $cmake --build --preset $preset --target EasyXMaze --parallel 6
    if ($LASTEXITCODE) { throw 'Maze build failed' }

    # 系统桌面可能被重定向，使用系统查询结果，不把用户名或桌面路径写死。
    $mazeDesktop = [Environment]::GetFolderPath('Desktop')
    $mazeBuilt = Join-Path $projectRoot "build/$($Configuration.ToLowerInvariant())/examples/easyx-maze/EasyXMaze.exe"
    $mazeDestination = Join-Path $mazeDesktop '迷宫小球.exe'
    Copy-Item -LiteralPath $mazeBuilt -Destination $mazeDestination -Force
    Write-Host "Executable: $mazeDestination"
} finally { Pop-Location }
