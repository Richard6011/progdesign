[CmdletBinding(SupportsShouldProcess = $true)]
param()

$ErrorActionPreference = 'Stop'

# 请先关闭 VS Code、终端、Codex/ChatGPT 及其他正在此目录运行的程序，再另开 PowerShell 执行。
# 本脚本只把指定目录联接替换为同名真实目录；不会递归删除任何项目内容。
$desktop = [Environment]::GetFolderPath('Desktop')
$oldRoot = [System.IO.Path]::GetFullPath((Join-Path $desktop '程序设计')).TrimEnd('\')
$newRoot = [System.IO.Path]::GetFullPath((Join-Path $desktop 'progdesign')).TrimEnd('\')
$developmentJunction = 'D:\Qt\Tools\note-collision-source'
$repairDevelopmentJunction = $false
$developmentItem = Get-Item -LiteralPath $developmentJunction -Force -ErrorAction SilentlyContinue
if ($null -ne $developmentItem -and $developmentItem.LinkType -eq 'Junction') {
    $developmentTarget = @($developmentItem.Target)[0]
    if (-not [System.IO.Path]::IsPathRooted($developmentTarget)) {
        $developmentTarget = Join-Path $developmentItem.Parent.FullName $developmentTarget
    }
    $developmentTarget = [System.IO.Path]::GetFullPath($developmentTarget).TrimEnd('\')
    $repairDevelopmentJunction = [string]::Equals($developmentTarget,
        (Join-Path $oldRoot 'code'), [System.StringComparison]::OrdinalIgnoreCase)
}

if (-not (Test-Path -LiteralPath $newRoot -PathType Container)) {
    throw "预期的 progdesign 目录联接不存在：$newRoot"
}
$aliasItem = Get-Item -LiteralPath $newRoot -Force
if ($aliasItem.LinkType -ne 'Junction') {
    if (-not (Test-Path -LiteralPath $oldRoot) -and
        (Test-Path -LiteralPath (Join-Path $newRoot 'code\CMakeLists.txt'))) {
        Write-Host 'progdesign 已经是真实项目目录，无需再次重命名。'
        return
    }
    throw "progdesign 必须是指向原项目的 Junction；当前不是，拒绝修改：$newRoot"
}
if (-not (Test-Path -LiteralPath $oldRoot -PathType Container)) {
    throw "原项目目录不存在：$oldRoot"
}
$oldItem = Get-Item -LiteralPath $oldRoot -Force
if (-not [string]::IsNullOrWhiteSpace($oldItem.LinkType)) {
    throw "原项目目录本身不能是联接或符号链接：$oldRoot"
}
$aliasTarget = @($aliasItem.Target)[0]
if (-not [System.IO.Path]::IsPathRooted($aliasTarget)) {
    $aliasTarget = Join-Path $aliasItem.Parent.FullName $aliasTarget
}
$aliasTarget = [System.IO.Path]::GetFullPath($aliasTarget).TrimEnd('\')
if (-not [string]::Equals($aliasTarget, $oldRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "progdesign 联接目标不匹配，拒绝修改。实际目标：$aliasTarget"
}
if (-not (Test-Path -LiteralPath (Join-Path $oldRoot 'code\CMakeLists.txt'))) {
    throw '原目录中没有预期的 code\CMakeLists.txt，拒绝重命名。'
}

if (-not $PSCmdlet.ShouldProcess($oldRoot, "仅移除目录联接 $newRoot，然后将原目录重命名为 progdesign")) {
    return
}

# 先离开旧目录，避免当前 PowerShell 的工作目录阻碍重命名。
Set-Location -LiteralPath $desktop
$aliasRemoved = $false
try {
    # false 明确禁止递归：这里只移除经过验证的 Junction，保留其目标内全部文件。
    [System.IO.Directory]::Delete($newRoot, $false)
    $aliasRemoved = $true
    Rename-Item -LiteralPath $oldRoot -NewName 'progdesign' -ErrorAction Stop
    if ($repairDevelopmentJunction) {
        # 只修复上面已确认指向本项目旧 code 的专用联接；不改其他目录。
        [System.IO.Directory]::Delete($developmentJunction, $false)
        New-Item -ItemType Junction -Path $developmentJunction -Target (Join-Path $newRoot 'code') -ErrorAction Stop | Out-Null
    }
    Write-Host "物理目录已重命名：$newRoot"
} catch {
    $renameError = $_
    if ($repairDevelopmentJunction -and (Test-Path -LiteralPath $newRoot -PathType Container) -and
        -not (Test-Path -LiteralPath $developmentJunction)) {
        try {
            New-Item -ItemType Junction -Path $developmentJunction -Target (Join-Path $newRoot 'code') -ErrorAction Stop | Out-Null
            Write-Warning '重命名已完成但专用联接修复遇到错误，已将其指向新的 progdesign\code。'
        } catch {
            Write-Warning "专用联接恢复失败：$($_.Exception.Message)"
        }
    }
    if ($aliasRemoved -and (Test-Path -LiteralPath $oldRoot -PathType Container) -and
        -not (Test-Path -LiteralPath $newRoot)) {
        try {
            New-Item -ItemType Junction -Path $newRoot -Target $oldRoot -ErrorAction Stop | Out-Null
            Write-Warning '重命名失败，已恢复原 progdesign 目录联接。请关闭仍占用目录的程序后重试。'
        } catch {
            Write-Warning "重命名和联接恢复均失败；原项目仍位于 $oldRoot。恢复错误：$($_.Exception.Message)"
        }
    }
    throw $renameError
}
