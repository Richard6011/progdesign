param([ValidateSet('Build', 'Test')][string]$Action = 'Build')
$ErrorActionPreference = 'Stop'

# 自动寻找本机 Visual Studio，不沿用原仓库中另一台电脑的绝对路径。
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudioRoot = $null
if (Test-Path -LiteralPath $vswherePath) {
    $visualStudioRoot = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $visualStudioRoot) {
    $visualStudioRoot = 'D:\C\Visual Studio\Community'
}
$toolRoot = Join-Path $visualStudioRoot 'VC\Tools\MSVC'
$toolVersion = Get-ChildItem -LiteralPath $toolRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
if (-not $toolVersion) {
    throw '没有找到 Visual Studio C 编译器，请安装 C++ 桌面开发工具，其中包含 C 编译器。'
}
$compilerDirectory = Join-Path $toolVersion.FullName 'bin\Hostx64\x64'
$compilerPath = Join-Path $compilerDirectory 'cl.exe'

# Windows SDK 提供 windows.h、系统图形和音频链接库。
$sdkRoot = 'D:\Windows Kits\10'
if (-not (Test-Path -LiteralPath $sdkRoot)) {
    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
}
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdkVersion) {
    throw '没有找到 Windows SDK。'
}
$sdkInclude = $sdkVersion.FullName
$sdkLibraries = Join-Path $sdkRoot ('Lib\' + $sdkVersion.Name)

$compilerArguments = @(
    '/nologo', '/TC', '/std:c17', '/utf-8', '/W4', '/WX', '/O2', '/MT',
    '/DUNICODE', '/D_UNICODE', '/D_CRT_SECURE_NO_WARNINGS', '/D_WIN32_WINNT=0x0A00',
    ('/I' + (Join-Path $toolVersion.FullName 'include')),
    ('/I' + (Join-Path $sdkInclude 'ucrt')),
    ('/I' + (Join-Path $sdkInclude 'um')),
    ('/I' + (Join-Path $sdkInclude 'shared')),
    '/I.', '/Fobuild\'
)
$linkArguments = @(
    '/link',
    ('/LIBPATH:' + (Join-Path $toolVersion.FullName 'lib\x64')),
    ('/LIBPATH:' + (Join-Path $sdkLibraries 'ucrt\x64')),
    ('/LIBPATH:' + (Join-Path $sdkLibraries 'um\x64')),
    'kernel32.lib', 'user32.lib', 'gdi32.lib', 'winmm.lib', 'bcrypt.lib'
)
$coreSources = @('engine.c', 'assets.c', 'crypto.c', 'storage.c', 'audio.c')
$previousPath = $env:PATH
$env:PATH = $compilerDirectory + ';' + $env:PATH
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    New-Item -ItemType Directory -Path 'build' -Force | Out-Null
    & $compilerPath @compilerArguments @coreSources 'main.c' 'ui.c' '/FeNoteCollisionV2.exe' @linkArguments '/SUBSYSTEM:WINDOWS'
    if ($LASTEXITCODE -ne 0) { throw '图形程序编译失败。' }
    if ($Action -eq 'Test') {
        & $compilerPath @compilerArguments @coreSources 'tests\test_basic.c' '/Febuild\BasicTests.exe' @linkArguments '/SUBSYSTEM:CONSOLE'
        if ($LASTEXITCODE -ne 0) { throw '核心测试编译失败。' }
        & '.\build\BasicTests.exe'
        if ($LASTEXITCODE -ne 0) { throw '核心测试失败。' }
    }
    Write-Host '构建完成：双击 NoteCollisionV2.exe 即可打开登录界面。'
}
finally {
    Pop-Location
    $env:PATH = $previousPath
}
