# Produces PNG assets matching the editable SVG layout at exactly 320x240.
# System.Drawing is used because the local FFmpeg build has no SVG decoder.
param(
    [ValidateRange(1, 8)] [int] $Scale = 1,
    [switch] $UpscaleOnly
)

Add-Type -AssemblyName System.Drawing

function Color([string]$hex) {
    return [System.Drawing.ColorTranslator]::FromHtml($hex)
}

function Fill-RoundRect($g, $brush, [float]$x, [float]$y, [float]$w, [float]$h, [float]$radius, $pen = $null) {
    $d = 2 * $radius
    $p = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    $g.FillPath($brush, $p)
    if ($null -ne $pen) { $g.DrawPath($pen, $p) }
    $p.Dispose()
}

function Draw-Label($g, [string]$value, [float]$x, [float]$y, [float]$size, [string]$hex, [bool]$bold = $true) {
    $style = if ($bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }
    $font = [System.Drawing.Font]::new('Segoe UI', $size, $style, [System.Drawing.GraphicsUnit]::Pixel)
    $brush = [System.Drawing.SolidBrush]::new((Color $hex))
    $format = [System.Drawing.StringFormat]::GenericTypographic
    $g.DrawString($value, $font, $brush, $x, $y, $format)
    $brush.Dispose()
    $font.Dispose()
}

function Draw-CenteredLabel($g, [string]$value, [float]$centerX, [float]$y, [float]$size, [string]$hex) {
    $font = [System.Drawing.Font]::new('Segoe UI', $size, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $brush = [System.Drawing.SolidBrush]::new((Color $hex))
    $format = [System.Drawing.StringFormat]::GenericTypographic
    $width = $g.MeasureString($value, $font, 1000, $format).Width
    $g.DrawString($value, $font, $brush, ($centerX - $width / 2), $y, $format)
    $brush.Dispose()
    $font.Dispose()
}

function Render-Dashboard([bool]$preview, [string]$destination, [int]$scale = 1) {
    $bmp = [System.Drawing.Bitmap]::new((320 * $scale), (240 * $scale), [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.ScaleTransform($scale, $scale)

    $screen = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        ([System.Drawing.Rectangle]::new(0, 0, 320, 240)), (Color '#0b2237'), (Color '#071725'),
        [System.Drawing.Drawing2D.LinearGradientMode]::Vertical)
    $g.FillRectangle($screen, 0, 0, 320, 240)
    $screen.Dispose()
    $header = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        ([System.Drawing.Rectangle]::new(0, 0, 320, 27)), (Color '#102f4c'), (Color '#0a2238'),
        [System.Drawing.Drawing2D.LinearGradientMode]::Horizontal)
    $g.FillRectangle($header, 0, 0, 320, 27)
    $header.Dispose()
    $cyan = [System.Drawing.SolidBrush]::new((Color '#56d9d7'))
    $g.FillRectangle($cyan, 0, 26, 320, 2)
    Draw-Label $g 'FERMENTOR KOPI' 9 2 14 '#f3f8fc'
    Draw-Label $g 'PROCESS CONTROL' 234 8 8 '#80aec5'

    $cardBrush = [System.Drawing.SolidBrush]::new((Color '#122b40'))
    $cardPen = [System.Drawing.Pen]::new((Color '#2c5067'), 1)
    $inactive = [System.Drawing.SolidBrush]::new((Color '#627d91'))
    $labels = @(
        @('PT100', 'OXYGEN'),
        @('pH', 'CO2'),
        @('SCD41 T', 'SCD41 RH'),
        @('CHAMBER T', 'CHAMBER RH'),
        @('SHT30 T', 'SHT30 RH')
    )
    for ($row = 0; $row -lt 5; $row++) {
        $y = 34 + 22 * $row
        foreach ($x in @(7, 163)) {
            Fill-RoundRect $g $cardBrush $x $y 150 20 3 $cardPen
            $railBrush = if ($row -eq 4) { $inactive } else { $cyan }
            $g.FillRectangle($railBrush, $x, ($y + 2), 2, 16)
        }
        Draw-Label $g $labels[$row][0] 14 ($y + 5) 8 '#afc0cf'
        Draw-Label $g $labels[$row][1] 170 ($y + 5) 8 '#afc0cf'
    }
    $cardBrush.Dispose()
    $cardPen.Dispose()
    $inactive.Dispose()

    $motorBrush = [System.Drawing.SolidBrush]::new((Color '#0d3545'))
    $motorPen = [System.Drawing.Pen]::new((Color '#2d7780'), 1)
    Fill-RoundRect $g $motorBrush 7 146 306 26 3 $motorPen
    $g.FillRectangle($cyan, 7, 147, 2, 24)
    Draw-Label $g 'MOTOR' 14 153 8 '#afc0cf'
    Draw-Label $g 'PWM' 183 153 8 '#afc0cf'
    $separator = [System.Drawing.Pen]::new((Color '#326072'), 1)
    $g.DrawLine($separator, 175, 151, 175, 166)
    $separator.Dispose()
    $motorBrush.Dispose()
    $motorPen.Dispose()

    $controlBrush = [System.Drawing.SolidBrush]::new((Color '#10283c'))
    $controlPen = [System.Drawing.Pen]::new((Color '#2d5066'), 1)
    Fill-RoundRect $g $controlBrush 7 177 306 61 4 $controlPen
    Draw-Label $g 'SPEED' 13 221 8 '#afc0cf'
    $controlBrush.Dispose()
    $controlPen.Dispose()

    if ($preview) {
        $readingRows = @(
            @('25.42 C', '20.90 %'),
            @('6.85', '612 ppm'),
            @('25.31 C', '60.52 %'),
            @('24.98 C', '68.12 %')
        )
        for ($row = 0; $row -lt 4; $row++) {
            $y = 38 + 22 * $row
            Draw-Label $g $readingRows[$row][0] 79 $y 11 '#f3f8fc'
            Draw-Label $g $readingRows[$row][1] 237 $y 11 '#f3f8fc'
        }
        Draw-Label $g 'DISABLED' 79 126 10 '#8ea3b6'
        Draw-Label $g 'DISABLED' 237 126 10 '#8ea3b6'
        Draw-Label $g 'OFF' 69 154 10 '#8ea3b6'
        Draw-Label $g '64/255 (25%)' 220 154 10 '#f3f8fc'

        $buttonColors = @('#4dd6b8', '#589acc', '#e96b73')
        $buttonLabels = @('ON FWD', 'ON REV', 'OFF / STOP')
        for ($button = 0; $button -lt 3; $button++) {
            $x = 10 + 102 * $button
            $buttonBrush = [System.Drawing.SolidBrush]::new((Color $buttonColors[$button]))
            Fill-RoundRect $g $buttonBrush $x 180 96 30 4
            Draw-CenteredLabel $g $buttonLabels[$button] ($x + 48) 188 12 '#071b2c'
            $buttonBrush.Dispose()
        }
        $trackBrush = [System.Drawing.SolidBrush]::new((Color '#315067'))
        $trackPen = [System.Drawing.Pen]::new((Color '#6f91a7'), 1)
        Fill-RoundRect $g $trackBrush 70 218 240 17 3 $trackPen
        $lineBrush = [System.Drawing.SolidBrush]::new((Color '#9bb3c1'))
        $g.FillRectangle($lineBrush, 74, 225, 225, 3)
        $g.FillRectangle($cyan, 74, 225, 56, 3)
        $cursorPen = [System.Drawing.Pen]::new((Color '#b5f0e9'), 1)
        Fill-RoundRect $g $cyan 125 216 13 21 3 $cursorPen
        $trackBrush.Dispose()
        $trackPen.Dispose()
        $lineBrush.Dispose()
        $cursorPen.Dispose()
    }

    $cyan.Dispose()
    $g.Dispose()
    $bmp.Save($destination, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

if ($UpscaleOnly) {
    $width = 320 * $Scale
    $height = 240 * $Scale
    Render-Dashboard $false (Join-Path $PSScriptRoot "background_${width}x${height}.png") $Scale
} else {
    Render-Dashboard $false (Join-Path $PSScriptRoot 'background_320x240.png')
    Render-Dashboard $true (Join-Path $PSScriptRoot 'preview_320x240.png')
}
