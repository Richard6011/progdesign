param([string]$SourceRoot = '')
$ErrorActionPreference = 'Stop'
$versionRoot = Split-Path -Parent $PSScriptRoot
$parentRoot = Split-Path -Parent $versionRoot
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = Join-Path $parentRoot 'assets\songs'
    if (-not (Test-Path -LiteralPath $SourceRoot)) {
        $SourceRoot = Join-Path $parentRoot 'progdesign\assets\songs'
    }
}
if (-not (Test-Path -LiteralPath $SourceRoot)) {
    throw '未找到原版 JSON 资源，请用 -SourceRoot 指定原仓库的 assets\songs；已有文本资源可以直接运行游戏。'
}
$destination = Join-Path (Split-Path -Parent $PSScriptRoot) 'assets'
$songs = @('twinkle-star', 'neon-pulse')
$difficulties = @('easy', 'normal', 'hard')

# 这个脚本只在重新转换原 JSON 资源时使用，正常运行游戏不需要执行。
New-Item -ItemType Directory -Path $destination -Force | Out-Null
for ($songIndex = 0; $songIndex -lt $songs.Count; $songIndex++) {
    for ($difficultyIndex = 0; $difficultyIndex -lt $difficulties.Count; $difficultyIndex++) {
        $sourcePath = Join-Path $SourceRoot ($songs[$songIndex] + '\' + $difficulties[$difficultyIndex] + '.json')
        $chart = Get-Content -LiteralPath $sourcePath -Raw -Encoding utf8 | ConvertFrom-Json
        if ($chart.notes.Count -lt 1 -or $chart.notes.Count -gt 2000) {
            throw '谱面音符数量超过第二版的固定数组范围。'
        }
        $lines = [System.Collections.Generic.List[string]]::new()
        $lines.Add("$($chart.notes.Count) $($chart.audioDurationMs) $($chart.approachMs)")
        foreach ($note in ($chart.notes | Sort-Object timeMs,lane,id)) {
            $kind = switch ($note.type) {
                'tap' { 0 }
                'hold' { 1 }
                'chain' { 2 }
                default { throw "未知音符类型：$($note.type)" }
            }
            $endTime = $note.timeMs
            if ($kind -eq 1) {
                if ($null -eq $note.endMs) { throw "长按缺少 endMs：$sourcePath" }
                $endTime = $note.endMs
            }
            $chain = if ($null -ne $note.chainId) { $note.chainId } else { -1 }
            $chord = if ($null -ne $note.chordId) { $note.chordId } else { -1 }
            $lines.Add("$($note.id) $kind $($note.lane) $($note.timeMs) $endTime $chain $chord")
        }
        $outputPath = Join-Path $destination "song${songIndex}_${difficultyIndex}.txt"
        Set-Content -LiteralPath $outputPath -Value $lines -Encoding ascii
        Write-Host "Converted: $outputPath"
    }
}
