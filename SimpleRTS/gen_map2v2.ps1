# SimpleRTS 2v2 地图生成器（输出 maps/map2v2.srmap）
# 4 个基地：1/2 在左侧同盟，3/4 在右侧同盟；中央椭圆湖 + 上下两条进攻走廊。
$ErrorActionPreference = 'Stop'

$W = 320
$H = 240
$C = 10
$PlayerCount = 4

$RSIZE = @{ 0 = 2; 1 = 10; 2 = 15; 3 = 10; 4 = 4 }
$RHP = @{ 0 = 20; 1 = 3000; 2 = 8000; 3 = 3000; 4 = 500 }

$grid = New-Object 'System.Char[][]' $H
$occ = New-Object 'bool[][]' $H
for ($y = 0; $y -lt $H; $y++) {
    $grid[$y] = ('L' * $W).ToCharArray()
    $occ[$y] = New-Object 'bool[]' $W
}
$entities = New-Object 'System.Collections.Generic.List[string]'
$warns = 0

function Can-Place([int]$gx, [int]$gy, [int]$size) {
    if ($gx -lt 0 -or $gy -lt 0 -or ($gx + $size) -gt $W -or ($gy + $size) -gt $H) { return $false }
    for ($yy = $gy; $yy -lt $gy + $size; $yy++) {
        for ($xx = $gx; $xx -lt $gx + $size; $xx++) {
            if ($script:grid[$yy][$xx] -eq 'W') { return $false }
            if ($script:occ[$yy][$xx]) { return $false }
        }
    }
    return $true
}

function Mark-Place([int]$gx, [int]$gy, [int]$size) {
    for ($yy = $gy; $yy -lt $gy + $size; $yy++) {
        for ($xx = $gx; $xx -lt $gx + $size; $xx++) {
            $script:occ[$yy][$xx] = $true
        }
    }
}

function Add-Resource-At([int]$rtype, [int]$gx, [int]$gy) {
    $size = $script:RSIZE[$rtype]
    if (-not (Can-Place $gx $gy $size)) {
        $script:warns++
        Write-Host ("  WARN resource R{0} at ({1},{2}) blocked" -f $rtype, $gx, $gy)
        return
    }
    Mark-Place $gx $gy $size
    $script:entities.Add(("R {0} {1} {2} {3}" -f $rtype, $gx, $gy, $script:RHP[$rtype]))
}

function Add-Resource([int]$rtype, [int]$gx, [int]$gy, [bool]$mx = $true, [bool]$my = $true) {
    $size = $script:RSIZE[$rtype]
    $xs = @($gx)
    $ys = @($gy)
    if ($mx) { $xs += ($W - $size - $gx) }
    if ($my) { $ys += ($H - $size - $gy) }
    $seen = @{}
    foreach ($x in $xs) {
        foreach ($y in $ys) {
            $key = "$x,$y"
            if ($seen.ContainsKey($key)) { continue }
            $seen[$key] = $true
            Add-Resource-At $rtype $x $y
        }
    }
}

function Add-Building([int]$btype, [int]$gx, [int]$gy, [int]$player) {
    $size = 20
    if (-not (Can-Place $gx $gy $size)) {
        $script:warns++
        Write-Host ("  WARN building B{0} at ({1},{2}) blocked" -f $btype, $gx, $gy)
        return
    }
    Mark-Place $gx $gy $size
    $script:entities.Add(("B {0} {1} {2} {3}" -f $btype, $gx, $gy, $player))
}

# ---- 中央椭圆湖 ----
for ($y = 0; $y -lt $H; $y++) {
    for ($x = 0; $x -lt $W; $x++) {
        $dx = ($x - 160.0) / 62.0
        $dy = ($y - 120.0) / 42.0
        if (($dx * $dx + $dy * $dy) -le 1.0) {
            $grid[$y][$x] = 'W'
        }
    }
}

# ---- 四个基地：左半场 1/2，右半场 3/4 ----
Add-Building 0 30 20 1
Add-Building 0 30 200 2
Add-Building 0 270 20 3
Add-Building 0 270 200 4

# ---- 基地周边资源（左上角一套，自动左右+上下镜像到四个基地）----
# 木材
foreach ($pos in @(
    @(76, 6), @(82, 10), @(76, 18), @(84, 24), @(78, 32), @(88, 38),
    @(68, 50), @(75, 57), @(82, 64), @(60, 72), @(66, 78),
    @(58, 90), @(64, 100), @(58, 112), @(66, 118), @(72, 124)
)) {
    Add-Resource 0 $pos[0] $pos[1] $true $true
}
# 浆果
foreach ($pos in @(@(86, 48), @(94, 56))) {
    Add-Resource 4 $pos[0] $pos[1] $true $true
}
# 小金矿
Add-Resource 1 100 5 $true $true
# 石矿
Add-Resource 3 100 42 $true $true
# 基地侧翼大金矿
Add-Resource 2 68 96 $true $true

# ---- 中央争夺区 ----
Add-Resource 2 135 12 $true $true
Add-Resource 1 140 45 $true $true
Add-Resource 3 145 60 $true $true
Add-Resource 4 125 65 $true $true

# ---- 湖岸树林：制造上下两条走廊 ----
foreach ($pos in @(
    @(92, 86), @(92, 100), @(92, 114), @(92, 128), @(92, 142),
    @(104, 74), @(116, 72), @(128, 70), @(140, 69),
    @(164, 70), @(176, 72), @(188, 74), @(200, 76)
)) {
    Add-Resource 0 $pos[0] $pos[1] $true $true
}

# ---- 上下走廊里的掩体树林 ----
foreach ($pos in @(
    @(118, 30), @(124, 36), @(112, 42),
    @(84, 166), @(90, 172), @(78, 178)
)) {
    Add-Resource 0 $pos[0] $pos[1] $true $true
}

# ---- 输出 ----
$sb = New-Object System.Text.StringBuilder
[void]$sb.Append("MYSTRMAP 1`n")
[void]$sb.Append("W $W`n")
[void]$sb.Append("H $H`n")
[void]$sb.Append("C $C`n")
[void]$sb.Append("P $PlayerCount`n")
for ($y = 0; $y -lt $H; $y++) {
    [void]$sb.Append((-join $grid[$y]) + "`n")
}
foreach ($e in $entities) {
    [void]$sb.Append($e + "`n")
}

$out = Join-Path $PSScriptRoot 'maps\map2v2.srmap'
[System.IO.File]::WriteAllText($out, $sb.ToString(), [System.Text.Encoding]::ASCII)
Write-Host ("WROTE {0}: {1} entities, {2} warnings" -f $out, $entities.Count, $warns)
