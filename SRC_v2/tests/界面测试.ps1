$ErrorActionPreference = 'Stop'
$projectFolder = Split-Path -Parent $PSScriptRoot
$applicationPath = Join-Path $projectFolder 'NoteCollisionV2.exe'
$imageFolder = Join-Path $projectFolder 'build\ui_checks'
$dataFolder = Join-Path $projectFolder 'ui_test_data'
New-Item -ItemType Directory -Path $imageFolder,$dataFolder -Force | Out-Null

# 所有操作只针对本脚本启动的第二版窗口，测试数据与正式 data 目录分离。
Set-Content -LiteralPath (Join-Path $dataFolder 'settings.txt') -Value 'NOTE_COLLISION_V2_SETTINGS 1 0 0' -Encoding ascii
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class BasicUiTest {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr window, string text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int capacity);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr SendText(IntPtr window, uint message, IntPtr wParam, string text);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr ReadText(IntPtr window, uint message, IntPtr wParam, StringBuilder text);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindow callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumWindow callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder name, int capacity);
    public delegate bool EnumWindow(IntPtr window, IntPtr data);
    public struct Rect { public int Left; public int Top; public int Right; public int Bottom; }
    public static string Text(IntPtr window) { var text=new StringBuilder(4000); GetWindowText(window,text,text.Capacity); return text.ToString(); }
    public static string ControlText(IntPtr window) { var text=new StringBuilder(4000); ReadText(window,0x000D,(IntPtr)text.Capacity,text); return text.ToString(); }
    public static IntPtr Dialog(uint process) {
        IntPtr result=IntPtr.Zero;
        EnumWindows((window,data) => {
            uint pid; GetWindowThreadProcessId(window,out pid);
            var name=new StringBuilder(100); GetClassName(window,name,100);
            if(pid==process && name.ToString()=="#32770" && IsWindowVisible(window)) { result=window; return false; }
            return true;
        },IntPtr.Zero);
        return result;
    }
    public static string ChildText(IntPtr window) {
        var text=new StringBuilder();
        EnumChildWindows(window,(child,data)=>{text.AppendLine(Text(child));return true;},IntPtr.Zero);
        return text.ToString();
    }
}
'@

$script:checkCount = 0
function Assert-Ui([bool]$Condition, [string]$Description) {
    $script:checkCount++
    if (-not $Condition) { throw "UI 检查失败：$Description" }
}
function Wait-Title([string]$Text) {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ([BasicUiTest]::Text($script:mainWindow).Contains($Text)) { return }
        Start-Sleep -Milliseconds 80
    }
    throw "没有进入预期页面：$Text；当前标题：$([BasicUiTest]::Text($script:mainWindow))"
}
function Click-Ui([int]$Id) {
    Assert-Ui ([BasicUiTest]::GetDlgItem($script:mainWindow,$Id) -ne [IntPtr]::Zero) "按钮 $Id 存在"
    [void][BasicUiTest]::PostMessage($script:mainWindow,0x0111,[IntPtr]$Id,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
}
function Set-UiText([int]$Id,[string]$Text) {
    $child = [BasicUiTest]::GetDlgItem($script:mainWindow,$Id)
    Assert-Ui ($child -ne [IntPtr]::Zero) "输入框 $Id 存在"
    Assert-Ui ([BasicUiTest]::SendText($child,0x000C,[IntPtr]::Zero,$Text) -ne [IntPtr]::Zero) "输入框 $Id 可填写"
    if ($Id -ne 101 -and $Id -ne 102) {
        Assert-Ui ([BasicUiTest]::ControlText($child) -eq $Text) "输入框 $Id 实际保存了输入内容"
    }
}
function Close-TestDialog([string]$Expected) {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    $dialog = [IntPtr]::Zero
    while ([DateTime]::UtcNow -lt $deadline -and $dialog -eq [IntPtr]::Zero) {
        $dialog = [BasicUiTest]::Dialog([uint32]$script:applicationProcess.Id)
        Start-Sleep -Milliseconds 60
    }
    Assert-Ui ($dialog -ne [IntPtr]::Zero) '预期的图形提示框出现'
    $actual = [BasicUiTest]::ChildText($dialog)
    Assert-Ui ($actual.Contains($Expected)) "提示包含：$Expected；实际提示：$actual"
    Assert-Ui ([BasicUiTest]::PostMessage($dialog,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)) '可以关闭本次测试提示框'
    $closeDeadline = [DateTime]::UtcNow.AddSeconds(2)
    while ([BasicUiTest]::IsWindow($dialog) -and [DateTime]::UtcNow -lt $closeDeadline) { Start-Sleep -Milliseconds 50 }
    Assert-Ui (-not [BasicUiTest]::IsWindow($dialog)) '提示框已经完全关闭'
}
function Save-Window([string]$Name) {
    $rectangle = New-Object BasicUiTest+Rect
    Assert-Ui ([BasicUiTest]::GetWindowRect($script:mainWindow,[ref]$rectangle)) '可以读取测试窗口尺寸'
    $bitmap = [System.Drawing.Bitmap]::new($rectangle.Right-$rectangle.Left,$rectangle.Bottom-$rectangle.Top)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try { Assert-Ui ([BasicUiTest]::PrintWindow($script:mainWindow,$dc,2)) '可以捕获测试窗口' }
    finally { $graphics.ReleaseHdc($dc) }
    $bitmap.Save((Join-Path $imageFolder ($Name+'.png')),[System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
}

$script:applicationProcess = Start-Process -FilePath $applicationPath -ArgumentList '--test-mode' -WorkingDirectory $projectFolder -WindowStyle Normal -PassThru
try {
    $script:applicationProcess.WaitForInputIdle(5000) | Out-Null
    $script:applicationProcess.Refresh()
    $script:mainWindow = $script:applicationProcess.MainWindowHandle
    Assert-Ui ($script:mainWindow -ne [IntPtr]::Zero) '图形主窗口实际创建'
    Wait-Title '注册登录'
    Save-Window '01_login'
    $username = 'ui_' + $script:applicationProcess.Id
    Click-Ui 104
    Set-UiText 100 $username
    Set-UiText 101 'test_pass_123'
    Set-UiText 102 'test_pass_123'
    Save-Window '02_register'
    Click-Ui 103
    Close-TestDialog '注册成功'
    Assert-Ui ([BasicUiTest]::GetDlgItem($script:mainWindow,102) -eq [IntPtr]::Zero) '注册成功后返回登录模式'
    Set-UiText 101 'wrong_password'
    Click-Ui 103
    Close-TestDialog '账号或密码错误'
    Wait-Title '注册登录'
    Set-UiText 101 'test_pass_123'
    Click-Ui 103
    Wait-Title '主菜单'
    Save-Window '03_home'
    Write-Host 'UI: 注册、错误密码拒绝、正确登录通过。'

    Click-Ui 107
    Wait-Title '设置'
    Set-UiText 120 '0'
    Set-UiText 121 '25'
    Save-Window '04_settings'
    Click-Ui 122
    Wait-Title '主菜单'
    Assert-Ui ((Get-Content -LiteralPath (Join-Path $dataFolder 'settings.txt') -Raw).Trim().EndsWith('0 25')) '设置通过实际界面保存成功'
    Click-Ui 108
    Wait-Title '操作说明'
    Save-Window '05_help'
    Click-Ui 110
    Wait-Title '主菜单'
    Click-Ui 106
    Wait-Title '排行榜'
    Save-Window '06_ranking'
    Click-Ui 110
    Wait-Title '主菜单'
    Click-Ui 105
    Wait-Title '选择歌曲'
    Save-Window '07_songs'
    Click-Ui 111
    Wait-Title '选择难度'
    Save-Window '08_difficulties'
    Click-Ui 113
    Wait-Title '正在游戏'
    Save-Window '09_countdown'
    Start-Sleep -Milliseconds 4500
    $unexpectedDialog = [BasicUiTest]::Dialog([uint32]$script:applicationProcess.Id)
    $unexpectedText = if ($unexpectedDialog -ne [IntPtr]::Zero) { [BasicUiTest]::ChildText($unexpectedDialog) } else { '' }
    Assert-Ui ($unexpectedDialog -eq [IntPtr]::Zero) "游戏实际起播且无音频错误框：$unexpectedText"
    Save-Window '10_playing'
    Click-Ui 116
    Save-Window '11_paused'
    [void][BasicUiTest]::PostMessage($script:mainWindow,0x0100,[IntPtr]13,[IntPtr]::Zero)
    [void][BasicUiTest]::PostMessage($script:mainWindow,0x0101,[IntPtr]13,[IntPtr]::Zero)
    Write-Host 'UI: 设置、排行榜、逐页选择、倒计时和暂停继续通过，等待自然结算。'
    $deadline = [DateTime]::UtcNow.AddSeconds(50)
    while ([DateTime]::UtcNow -lt $deadline -and -not [BasicUiTest]::Text($script:mainWindow).Contains('本局结果')) {
        Start-Sleep -Milliseconds 250
        if ([BasicUiTest]::Dialog([uint32]$script:applicationProcess.Id) -ne [IntPtr]::Zero) { throw '演奏中出现了意外提示框。' }
    }
    Wait-Title '本局结果'
    Save-Window '12_result'
    $rows = @(Get-Content -LiteralPath (Join-Path $dataFolder 'scores.txt') -Encoding utf8 | Where-Object { $_.StartsWith($username + "`t") })
    Assert-Ui ($rows.Count -eq 1) '自然结束保存一条账号最佳成绩'
    $fields = $rows[0].Split("`t")
    Assert-Ui ($fields.Count -eq 10 -and $fields[3] -eq '43' -and $fields[7] -eq '43') '无按键的完整演奏结算为四十三个漏击'
    Click-Ui 106
    Wait-Title '排行榜'
    Save-Window '13_saved_ranking'
    Click-Ui 110
    Wait-Title '主菜单'
    Click-Ui 109
    Wait-Title '注册登录'
    Assert-Ui ([BasicUiTest]::ControlText([BasicUiTest]::GetDlgItem($script:mainWindow,100)) -eq '') '退出登录后清除上一账号输入'
    Write-Host "UI PASS: $script:checkCount checks."
    [ordered]@{passed=$true;checks=$script:checkCount;screenshots=$imageFolder;testData=$dataFolder;scope='实际窗口注册登录、错误密码、设置、选曲、暂停继续、自然结算及账号退出'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $imageFolder 'result.json') -Encoding utf8
}
finally {
    if (-not $script:applicationProcess.HasExited) {
        [void][BasicUiTest]::PostMessage($script:mainWindow,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)
        if (-not $script:applicationProcess.WaitForExit(5000)) { $script:applicationProcess.Kill() }
    }
    $script:applicationProcess.Dispose()
}
