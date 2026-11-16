[CmdletBinding()]
param(
    [ValidateSet('Open', 'Build', 'Test', 'Run', 'Package')]
    [string]$Action = 'Build',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'

$scriptRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $MyInvocation.MyCommand.Path))
$workspaceFromScript = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '..'))
$junctionPath = 'D:\Qt\Tools\note-collision-source'
$cmakePath = 'C:\Users\Administrator\AppData\Local\Programs\CMake\cmake-3.31.10-windows-x86_64\bin\cmake.exe'
$ctestPath = 'C:\Users\Administrator\AppData\Local\Programs\CMake\cmake-3.31.10-windows-x86_64\bin\ctest.exe'
$qtBin = 'D:\Qt\6.8.3\mingw_64\bin'
$mingwBin = 'D:\Qt\Tools\mingw1310_64\bin'
$ninjaBin = 'D:\Qt\Tools\Ninja'
$windeployqtPath = Join-Path $qtBin 'windeployqt.exe'

function Get-PhysicalDirectory([string]$Path, [int]$Depth = 0) {
    if ($Depth -gt 16) {
        throw "目录联接层数过多：$Path"
    }
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "目录不存在：$Path"
    }
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetPathRoot($fullPath)
    $current = $root
    $parts = $fullPath.Substring($root.Length).Split([char[]]@('\', '/'), [System.StringSplitOptions]::RemoveEmptyEntries)
    foreach ($part in $parts) {
        $current = Join-Path $current $part
        $item = Get-Item -LiteralPath $current -Force
        $target = Get-LinkTarget $item
        if ($null -ne $target) {
            $current = Get-PhysicalDirectory $target ($Depth + 1)
        }
    }
    return [System.IO.Path]::GetFullPath($current).TrimEnd('\')
}

function Get-LinkTarget([System.IO.DirectoryInfo]$Item) {
    if ([string]::IsNullOrWhiteSpace($Item.LinkType) -or $null -eq $Item.Target) {
        return $null
    }
    $target = @($Item.Target)[0]
    if ([System.IO.Path]::IsPathRooted($target)) {
        return $target
    }
    return Join-Path $Item.Parent.FullName $target
}

function Resolve-ProjectPaths {
    $physicalWorkspace = Get-PhysicalDirectory $workspaceFromScript

    if (Test-Path -LiteralPath $junctionPath) {
        $junctionItem = Get-Item -LiteralPath $junctionPath -Force
        if ($junctionItem.LinkType -notin @('Junction', 'SymbolicLink')) {
            throw "项目联接目录不是 Junction/SymbolicLink：$junctionPath"
        }
        $resolvedJunctionTarget = Get-PhysicalDirectory $junctionPath
        if (-not [string]::Equals($resolvedJunctionTarget, $physicalWorkspace, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "项目联接目录目标不匹配；拒绝覆盖其他目录。期望：$physicalWorkspace；实际：$resolvedJunctionTarget"
        }
    }

    return [pscustomobject]@{
        PhysicalWorkspace = $physicalWorkspace
        Source = $workspaceFromScript
    }
}

function Assert-Tool([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "工具不存在：$Path"
    }
}

function Invoke-Native([string]$FilePath, [string[]]$Arguments) {
    Write-Host "> $FilePath $($Arguments -join ' ')"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "命令失败（退出码 $LASTEXITCODE）：$FilePath"
    }
}

function Get-Preset {
    return ('qt-' + $Configuration.ToLowerInvariant())
}

function Set-DevelopmentPath {
    $env:PATH = "$mingwBin;$qtBin;$ninjaBin;$(Split-Path -Parent $cmakePath);$env:PATH"
}

function Invoke-ConfigureAndBuild([string]$Preset) {
    Invoke-Native $cmakePath @('--fresh', '--preset', $Preset)
    Invoke-Native $cmakePath @('--build', '--preset', $Preset, '--parallel', '6')
}

function Get-BinaryPath([string]$Preset) {
    $binary = Join-Path $workspace.Source "build\$Preset\NoteCollision.exe"
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
        throw "项目可执行文件不存在：$binary"
    }
    return $binary
}

$workspace = Resolve-ProjectPaths
$preset = Get-Preset
Assert-Tool $cmakePath
Assert-Tool $ctestPath
Assert-Tool (Join-Path $mingwBin 'g++.exe')
Assert-Tool (Join-Path $ninjaBin 'ninja.exe')
$oldLocation = Get-Location
$developmentOriginalPath = $env:PATH
try {
    Set-DevelopmentPath
    Set-Location -LiteralPath $workspace.Source

    switch ($Action) {
        'Open' {
            $code = Get-Command code -ErrorAction Stop
            & $code.Source '--profile' '程序设计' $workspace.Source
            if ($LASTEXITCODE -ne 0) {
                throw "VS Code 打开失败（退出码 $LASTEXITCODE）"
            }
        }
        'Build' {
            Invoke-ConfigureAndBuild $preset
        }
        'Test' {
            Invoke-ConfigureAndBuild $preset
            $testDirectory = Join-Path $workspace.Source "build\$preset"
            Invoke-Native $ctestPath @('--test-dir', $testDirectory, '--output-on-failure', '-C', $Configuration)
        }
        'Run' {
            Invoke-ConfigureAndBuild $preset
            $binary = Get-BinaryPath $preset
            # 子进程继承这里的专用 PATH；脚本结束后恢复调用方 PATH。
            $process = Start-Process -FilePath $binary -WorkingDirectory $workspace.Source `
                -WindowStyle Hidden -PassThru
            Write-Host "已启动 NoteCollision，进程号：$($process.Id)"
        }
        'Package' {
            $releasePreset = 'qt-release'
            Invoke-ConfigureAndBuild $releasePreset
            Assert-Tool $windeployqtPath
            $binary = Get-BinaryPath $releasePreset
            $outDirectory = Join-Path $workspace.PhysicalWorkspace 'out'
            $packageDirectory = Join-Path $outDirectory 'NoteCollision'
            $expectedPackageDirectory = Join-Path $workspace.PhysicalWorkspace 'out\NoteCollision'
            if (Test-Path -LiteralPath $outDirectory) {
                $outItem = Get-Item -LiteralPath $outDirectory -Force
                if (-not [string]::IsNullOrWhiteSpace($outItem.LinkType)) {
                    throw "打包父目录不能是联接或符号链接：$outDirectory"
                }
            }
            if (Test-Path -LiteralPath $packageDirectory) {
                $packageItem = Get-Item -LiteralPath $packageDirectory -Force
                $resolvedPackageDirectory = Get-PhysicalDirectory $packageDirectory
                if (-not [string]::IsNullOrWhiteSpace($packageItem.LinkType) -or
                    -not [string]::Equals($resolvedPackageDirectory, $expectedPackageDirectory, [System.StringComparison]::OrdinalIgnoreCase)) {
                    throw "打包清理目标不在预期项目目录内：$packageDirectory"
                }
                if (-not (Test-Path -LiteralPath (Join-Path $packageDirectory '.note-collision-package'))) {
                    throw "打包目录没有本项目标记，拒绝删除可能的用户数据：$packageDirectory"
                }
                Remove-Item -LiteralPath $packageDirectory -Recurse -Force
            }
            New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null
            Set-Content -LiteralPath (Join-Path $packageDirectory '.note-collision-package') `
                -Value 'Generated by tools/Development.ps1' -Encoding utf8
            Copy-Item -LiteralPath $binary -Destination (Join-Path $packageDirectory 'NoteCollision.exe') -Force
            $deployedBinary = Join-Path $packageDirectory 'NoteCollision.exe'
            Invoke-Native $windeployqtPath @('--release', '--compiler-runtime', '--no-translations',
                '--dir', $packageDirectory, $deployedBinary)
            Copy-Item -LiteralPath (Join-Path $workspace.PhysicalWorkspace 'assets') `
                -Destination (Join-Path $packageDirectory 'assets') -Recurse -Force
            Write-Host "可携带目录已生成：$packageDirectory"
        }
    }
} finally {
    $env:PATH = $developmentOriginalPath
    Set-Location -LiteralPath $oldLocation
}
