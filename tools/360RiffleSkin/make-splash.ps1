# Draws the splash shown while 360 Riffle starts (media\splash.png): its name as the Home screen's top bar has it, in
# Roboto Bold with 360 in MediaRiffle's accent, on a card of MediaRiffle's dark theme. The splash is drawn over black,
# kept in proportion, in the middle half of the screen.
#
#   powershell -ExecutionPolicy Bypass -File tools\360RiffleSkin\make-splash.ps1 [-Preview preview.png]
#
# -Preview also saves it over black, as the console shows it.

param([string]$Preview)

Add-Type -AssemblyName System.Drawing

$root = Join-Path $PSScriptRoot '..\..'
$out = Join-Path $root 'media\splash.png'
$fonts = Join-Path $root 'skins\360 Riffle\fonts'

# MediaRiffle's dark theme (src/MediaRiffle.Server/wwwroot/css/site.css), as in colors\defaults.xml
$accent  = [System.Drawing.ColorTranslator]::FromHtml('#4b9ef5')
$surface = [System.Drawing.ColorTranslator]::FromHtml('#1c1c21')
$border  = [System.Drawing.ColorTranslator]::FromHtml('#33333a')
$text    = [System.Drawing.ColorTranslator]::FromHtml('#f2f2f5')
$muted   = [System.Drawing.ColorTranslator]::FromHtml('#a1a1aa')

$collection = New-Object System.Drawing.Text.PrivateFontCollection
$collection.AddFontFile((Join-Path $fonts 'Roboto-Bold.ttf'))
$collection.AddFontFile((Join-Path $fonts 'Roboto-Regular.ttf'))
$roboto = $collection.Families | Where-Object { $_.Name -eq 'Roboto' } | Select-Object -First 1
if (-not $roboto) { throw 'Roboto not found in the skin fonts' }

function RoundedRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r)
{
	$p = New-Object System.Drawing.Drawing2D.GraphicsPath
	$d = $r * 2
	$p.AddArc($x, $y, $d, $d, 180, 90)
	$p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
	$p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
	$p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
	$p.CloseFigure()
	return $p
}

# Text as a shape, so its edges are smoothed against a transparent background
function TextPath([string]$s, [System.Drawing.FontStyle]$style, [float]$size)
{
	$p = New-Object System.Drawing.Drawing2D.GraphicsPath
	$p.AddString($s, $roboto, [int]$style, $size, (New-Object System.Drawing.PointF(0, 0)), [System.Drawing.StringFormat]::GenericTypographic)
	return $p
}

function MovePath($path, [float]$dx, [float]$dy)
{
	$m = New-Object System.Drawing.Drawing2D.Matrix
	$m.Translate($dx, $dy)
	$path.Transform($m)
}

# 16:9 like the screen, so it fills the middle half of it
$width = 1280
$height = 720
$bmp = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$g.Clear([System.Drawing.Color]::Transparent)

# The card: MediaRiffle's .card, its surface with a border
$cardW = 1120
$cardH = 520
$cardX = ($width - $cardW) / 2
$cardY = ($height - $cardH) / 2
$g.FillPath((New-Object System.Drawing.SolidBrush($surface)), (RoundedRect $cardX $cardY $cardW $cardH 40))
$g.DrawPath((New-Object System.Drawing.Pen($border, 4)), (RoundedRect ($cardX + 2) ($cardY + 2) ($cardW - 4) ($cardH - 4) 38))

# The name: 360 in the accent, Riffle in the text colour, a space apart, on one baseline
$size = 190
$number = TextPath '360' ([System.Drawing.FontStyle]::Bold) $size
$word = TextPath 'Riffle' ([System.Drawing.FontStyle]::Bold) $size
$nb = $number.GetBounds()
$wb = $word.GetBounds()
$gap = $size * 0.24
$nameW = $nb.Width + $gap + $wb.Width
$top = [Math]::Min($nb.Top, $wb.Top)
$bottom = [Math]::Max($nb.Bottom, $wb.Bottom)

# Under it, what it is, spaced out in small capitals
$tagSize = 44
$tag = TextPath 'M E D I A   D A S H B O A R D' ([System.Drawing.FontStyle]::Regular) $tagSize
$tb = $tag.GetBounds()

$lineGap = 56
$blockH = ($bottom - $top) + $lineGap + $tb.Height
$nameX = ($width - $nameW) / 2
$nameY = ($height - $blockH) / 2 - $top

MovePath $number ($nameX - $nb.Left) $nameY
MovePath $word ($nameX + $nb.Width + $gap - $wb.Left) $nameY
MovePath $tag (($width - $tb.Width) / 2 - $tb.Left) ($nameY + $bottom + $lineGap - $tb.Top)

$g.FillPath((New-Object System.Drawing.SolidBrush($accent)), $number)
$g.FillPath((New-Object System.Drawing.SolidBrush($text)), $word)
$g.FillPath((New-Object System.Drawing.SolidBrush($muted)), $tag)
$g.Dispose()

$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)

if ($Preview)
{
	$view = New-Object System.Drawing.Bitmap($width, $height)
	$v = [System.Drawing.Graphics]::FromImage($view)
	$v.Clear([System.Drawing.Color]::Black)
	$v.DrawImage($bmp, 0, 0, $width, $height)
	$v.Dispose()
	$view.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png)
	$view.Dispose()
}

$bmp.Dispose()
Get-Item $out | Select-Object Name, Length
