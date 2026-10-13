# Regenerates the three 320x240 dashboard backgrounds and visual previews.
# Text uses single-bit grid fitting so small labels stay crisp on the LCD.
Add-Type -AssemblyName System.Drawing

function C([string]$hex) { [System.Drawing.ColorTranslator]::FromHtml($hex) }

function RoundRect($g, $brush, $pen, [float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $d = $r * 2
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    if ($null -ne $brush) { $g.FillPath($brush, $path) }
    if ($null -ne $pen) { $g.DrawPath($pen, $path) }
    $path.Dispose()
}

function Label($g, [string]$text, [float]$x, [float]$y, [float]$size, [string]$color, [bool]$bold = $true) {
    $style = if ($bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }
    $font = [System.Drawing.Font]::new('Arial', $size, $style, [System.Drawing.GraphicsUnit]::Pixel)
    $brush = [System.Drawing.SolidBrush]::new((C $color))
    $g.DrawString($text, $font, $brush, $x, $y, [System.Drawing.StringFormat]::GenericTypographic)
    $brush.Dispose(); $font.Dispose()
}

function CenterLabel($g, [string]$text, [float]$cx, [float]$y, [float]$size, [string]$color, [bool]$bold = $true) {
    $style = if ($bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }
    $font = [System.Drawing.Font]::new('Arial', $size, $style, [System.Drawing.GraphicsUnit]::Pixel)
    $brush = [System.Drawing.SolidBrush]::new((C $color))
    $format = [System.Drawing.StringFormat]::GenericTypographic
    $width = $g.MeasureString($text, $font, 1000, $format).Width
    $g.DrawString($text, $font, $brush, ($cx - $width / 2), $y, $format)
    $brush.Dispose(); $font.Dispose()
}

function BaseCanvas([string]$section) {
    $bitmap = [System.Drawing.Bitmap]::new(320, 240, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
    $graphics.Clear((C '#071725'))
    $header = [System.Drawing.SolidBrush]::new((C '#0d2a42'))
    $graphics.FillRectangle($header, 0, 0, 320, 26)
    $header.Dispose()
    $accent = [System.Drawing.SolidBrush]::new((C '#56d9d7'))
    $graphics.FillRectangle($accent, 0, 25, 320, 2)
    $accent.Dispose()
    Label $graphics 'FERMENTOR KOPI' 8 5 13 '#f3f8fc'
    $sectionWidth = $graphics.MeasureString($section, [System.Drawing.Font]::new('Arial', 9, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)).Width
    Label $graphics $section (310 - $sectionWidth) 8 9 '#56d9d7'
    return @($bitmap, $graphics)
}

function GaugeCard($g, [float]$x, [float]$y, [float]$w, [float]$h, [string]$title, [float]$cx, [float]$cy, [bool]$showGauge) {
    $fill = [System.Drawing.SolidBrush]::new((C '#10283c'))
    $edge = [System.Drawing.Pen]::new((C '#274a61'), 1)
    RoundRect $g $fill $edge $x $y $w $h 4
    $fill.Dispose(); $edge.Dispose()
    CenterLabel $g $title $cx ($y + 5) 9 '#afc0cf'
    if ($showGauge) {
        $track = [System.Drawing.Pen]::new((C '#31566d'), 3)
        $g.DrawArc($track, ($cx - 23), ($cy - 23), 46, 46, 135, 270)
        $track.Dispose()
        $tick = [System.Drawing.Pen]::new((C '#6f91a7'), 1)
        foreach ($angle in @(135,180,225,270,315,360,405)) {
            $rad = $angle * [Math]::PI / 180
            $x1 = $cx + [Math]::Cos($rad) * 19
            $y1 = $cy + [Math]::Sin($rad) * 19
            $x2 = $cx + [Math]::Cos($rad) * 23
            $y2 = $cy + [Math]::Sin($rad) * 23
            $g.DrawLine($tick, [float]$x1, [float]$y1, [float]$x2, [float]$y2)
        }
        $tick.Dispose()
    }
}

function GaugePointer($g, [float]$cx, [float]$cy, [float]$angle, [string]$color = '#56d9d7') {
    $rad = ($angle - 90) * [Math]::PI / 180
    $px = $cx + [Math]::Cos($rad) * 18
    $py = $cy + [Math]::Sin($rad) * 18
    $pen = [System.Drawing.Pen]::new((C $color), 2)
    $g.DrawLine($pen, $cx, $cy, [float]$px, [float]$py)
    $pen.Dispose()
    $hub = [System.Drawing.SolidBrush]::new((C $color))
    $g.FillEllipse($hub, ($cx - 2), ($cy - 2), 4, 4)
    $hub.Dispose()
}

function Nav($g, [int]$active, [bool]$preview) {
    $panel = [System.Drawing.SolidBrush]::new((C '#0b2133'))
    $g.FillRectangle($panel, 0, 209, 320, 31)
    $panel.Dispose()
    if (-not $preview) { return }
    $names = @('PROCESS','ENVIRONMENT','CONTROL')
    $xs = @(6,111,216)
    for ($i=0; $i -lt 3; $i++) {
        $fillColor = if ($i -eq $active) { '#56d9d7' } else { '#18364c' }
        $textColor = if ($i -eq $active) { '#071b2c' } else { '#afc0cf' }
        $fill = [System.Drawing.SolidBrush]::new((C $fillColor))
        $edge = [System.Drawing.Pen]::new((C '#31566d'), 1)
        RoundRect $g $fill $edge $xs[$i] 212 98 25 3
        CenterLabel $g $names[$i] ($xs[$i] + 49) 220 9 $textColor
        $fill.Dispose(); $edge.Dispose()
    }
}

function SavePage($bitmap, $graphics, [string]$name) {
    $graphics.Dispose()
    $bitmap.Save((Join-Path $PSScriptRoot $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

function ProcessPage([bool]$preview) {
    $canvas = BaseCanvas 'PROCESS'
    $bmp = $canvas[0]; $g = $canvas[1]
    $titles = @('PT100','OXYGEN','pH','SCD41 CO2')
    $values = @('25.42 C','20.90 %','6.85','612 ppm')
    $angles = @(294,91,357,258)
    for ($i=0; $i -lt 4; $i++) {
        $col = $i % 2; $row = [Math]::Floor($i / 2)
        $x = 6 + 157 * $col; $y = 31 + 88 * $row
        $cx = $x + 75; $cy = $y + 39
        GaugeCard $g $x $y 151 84 $titles[$i] $cx $cy $preview
        if ($preview) {
            GaugePointer $g $cx $cy $angles[$i]
            CenterLabel $g $values[$i] $cx ($y + 66) 11 '#f3f8fc'
        }
    }
    Nav $g 0 $preview
    $name = if ($preview) { 'page0_process_preview.png' } else { 'page0_process_background.png' }
    SavePage $bmp $g $name
}

function EnvironmentPage([bool]$preview) {
    $canvas = BaseCanvas 'ENVIRONMENT'
    $bmp = $canvas[0]; $g = $canvas[1]
    $titles = @('SCD41 TEMP','SCD41 RH','SHT30 TEMP','SHT30 RH')
    $values = @('25.31 C','60.52 %','24.98 C','68.12 %')
    $angles = @(339,28,337,49)
    for ($i=0; $i -lt 4; $i++) {
        $col = $i % 2; $row = [Math]::Floor($i / 2)
        $x = 6 + 157 * $col; $y = 31 + 88 * $row
        $cx = $x + 75; $cy = $y + 39
        GaugeCard $g $x $y 151 84 $titles[$i] $cx $cy $preview
        if ($preview) {
            GaugePointer $g $cx $cy $angles[$i] '#56d9d7'
            CenterLabel $g $values[$i] $cx ($y + 66) 11 '#f3f8fc'
        }
    }
    Nav $g 1 $preview
    if ($preview) { Label $g 'CONTROL' 243 220 9 '#afc0cf' }
    $name = if ($preview) { 'page1_environment_preview.png' } else { 'page1_environment_background.png' }
    SavePage $bmp $g $name
}

function ControlPage([bool]$preview) {
    $canvas = BaseCanvas 'MOTOR CONTROL'
    $bmp = $canvas[0]; $g = $canvas[1]
    $fill = [System.Drawing.SolidBrush]::new((C '#10283c'))
    $edge = [System.Drawing.Pen]::new((C '#274a61'), 1)
    RoundRect $g $fill $edge 6 32 308 43 4
    Label $g 'MOTOR STATE' 14 39 9 '#afc0cf'
    Label $g 'PWM' 205 39 9 '#afc0cf'
    $divider = [System.Drawing.Pen]::new((C '#31566d'), 1)
    $g.DrawLine($divider, 190, 38, 190, 69)
    $divider.Dispose()
    Label $g 'DIRECTION' 10 82 9 '#afc0cf'
    RoundRect $g $fill $edge 6 143 308 60 4
    Label $g 'SPEED' 14 150 9 '#afc0cf'
    if ($preview) {
        Label $g 'OFF' 14 53 14 '#8ea3b6'
        Label $g '64/255 (25%)' 205 53 12 '#f3f8fc'
        $buttonColors = @('#4dd6b8','#589acc','#e96b73')
        $pressed = @('#36ad92','#397bae','#b84d58')
        $buttonText = @('ON FWD','ON REV','OFF / STOP')
        for ($i=0; $i -lt 3; $i++) {
            $x = 10 + 102 * $i
            $brush = [System.Drawing.SolidBrush]::new((C $buttonColors[$i]))
            RoundRect $g $brush $null $x 96 96 39 4
            CenterLabel $g $buttonText[$i] ($x + 48) 110 12 '#071b2c'
            $brush.Dispose()
        }
        $track = [System.Drawing.SolidBrush]::new((C '#315067'))
        RoundRect $g $track $null 16 174 288 23 3
        $line = [System.Drawing.SolidBrush]::new((C '#9bb3c1'))
        $g.FillRectangle($line, 23, 184, 274, 3)
        $cyan = [System.Drawing.SolidBrush]::new((C '#56d9d7'))
        $g.FillRectangle($cyan, 23, 184, 68, 3)
        RoundRect $g $cyan $null 85 173 13 25 3
        $track.Dispose(); $line.Dispose(); $cyan.Dispose()
    }
    $fill.Dispose(); $edge.Dispose()
    Nav $g 2 $preview
    $name = if ($preview) { 'page2_control_preview.png' } else { 'page2_control_background.png' }
    SavePage $bmp $g $name
}

ProcessPage $false
ProcessPage $true
EnvironmentPage $false
EnvironmentPage $true
ControlPage $false
ControlPage $true

# Keep the original filenames as crisp page-0 aliases for convenient previewing.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'page0_process_background.png') -Destination (Join-Path $PSScriptRoot 'background_320x240.png') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'page0_process_preview.png') -Destination (Join-Path $PSScriptRoot 'preview_320x240.png') -Force
