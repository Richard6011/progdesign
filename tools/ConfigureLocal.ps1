param(
    [string]$MinGWRoot = '',
    [string]$QtRoot = '',
    [string]$CMakeRoot = '',
    [string]$NinjaRoot = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$localRoot = Join-Path $projectRoot '.local'
New-Item -ItemType Directory -Path $localRoot -Force | Out-Null

function Find-Root($Explicit, $Candidates, $Executable) {
    foreach ($candidate in @($Explicit) + $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate $Executable))) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Cannot find $Executable. Pass its installation root to ConfigureLocal.ps1."
}

function Add-ToolLink($Name, $Root) {
    $link = Join-Path $localRoot $Name
    if (Test-Path -LiteralPath $link) {
        $item = Get-Item -LiteralPath $link
        if ($item.LinkType -ne 'Junction' -or $item.Target -ne $Root) {
            throw "$link already exists with another target; inspect it before reconfiguring."
        }
    } else {
        New-Item -ItemType Junction -Path $link -Target $Root | Out-Null
    }
    Write-Host "$Name -> $Root"
}

$mingw = Find-Root $MinGWRoot @('D:/Qt/Tools/mingw1310_64','C:/Qt/Tools/mingw1310_64') 'bin/gcc.exe'
$qt = Find-Root $QtRoot @('D:/Qt/6.8.0/mingw_64','C:/Qt/6.8.0/mingw_64') 'bin/Qt6Widgets.dll'
$cmake = Find-Root $CMakeRoot @('D:/DevTools/cmake-3.31.10-windows-x86_64','C:/Program Files/CMake') 'bin/cmake.exe'
$ninja = Find-Root $NinjaRoot @('D:/Qt/Tools/Ninja','C:/Qt/Tools/Ninja') 'ninja.exe'
if ((& (Join-Path $mingw 'bin/gcc.exe') -dumpfullversion) -ne '13.1.0') {
    throw 'This project uses Qt MinGW GCC 13.1.0 x64 (MSVCRT).'
}
Add-ToolLink 'mingw' $mingw
Add-ToolLink 'qt' $qt
Add-ToolLink 'cmake' $cmake
Add-ToolLink 'ninja' $ninja
