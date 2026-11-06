# Draws the 360 Riffle skin's own pictures in place of Confluence Lite's (whose Textures.xpr the skin keeps, and which
# wins over a loose file of the same name, so these have names of their own) into skins\360 Riffle\media:
#
#   mr-logo.png      the name, as the splash has it, where Confluence's logo was (Settings, the side blade)
#   mr-icon.png      a small 360 mark, where XBMC's logo was beside a window's title
#   DefaultGame.png  a game's picture when it has none, in place of Confluence Lite's mouse
#
#   powershell -ExecutionPolicy Bypass -File tools\360RiffleSkin\make-icons.ps1

Add-Type -AssemblyName System.Drawing

$root = Join-Path $PSScriptRoot '..\..'
$media = Join-Path $root 'skins\360 Riffle\media'
$fonts = Join-Path $root 'skins\360 Riffle\fonts'

# MediaRiffle's dark theme (src/MediaRiffle.Server/wwwroot/css/site.css), as in colors\defaults.xml
$accent  = [System.Drawing.ColorTranslator]::FromHtml('#4b9ef5')
$surface = [System.Drawing.ColorTranslator]::FromHtml('#1c1c21')
$border  = [System.Drawing.ColorTranslator]::FromHtml('#33333a')
$text    = [System.Drawing.ColorTranslator]::FromHtml('#f2f2f5')

$collection = New-Object System.Drawing.Text.PrivateFontCollection
$collection.AddFontFile((Join-Path $fonts 'Roboto-Bold.ttf'))
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
function TextPath([string]$s, [float]$size)
{
	$p = New-Object System.Drawing.Drawing2D.GraphicsPath
	$p.AddString($s, $roboto, [int][System.Drawing.FontStyle]::Bold, $size, (New-Object System.Drawing.PointF(0, 0)), [System.Drawing.StringFormat]::GenericTypographic)
	return $p
}

function MovePath($path, [float]$dx, [float]$dy)
{
	$m = New-Object System.Drawing.Drawing2D.Matrix
	$m.Translate($dx, $dy)
	$path.Transform($m)
}

function NewCanvas([int]$w, [int]$h)
{
	$bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
	$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
	$g.Clear([System.Drawing.Color]::Transparent)
	return @($bmp, $g)
}

function Save($canvas, [string]$name)
{
	$canvas[1].Dispose()
	$canvas[0].Save((Join-Path $media $name), [System.Drawing.Imaging.ImageFormat]::Png)
	$canvas[0].Dispose()
}

# The name: 360 in the accent, Riffle in the text colour, on one baseline, in the middle of 440 x 160 (the logo's place
# in the skin is 220 x 80, kept in proportion)
$c = NewCanvas 440 160
$size = 96
$number = TextPath '360' $size
$word = TextPath 'Riffle' $size
$nb = $number.GetBounds()
$wb = $word.GetBounds()
$gap = $size * 0.24
$nameW = $nb.Width + $gap + $wb.Width
$top = [Math]::Min($nb.Top, $wb.Top)
$bottom = [Math]::Max($nb.Bottom, $wb.Bottom)
$x = (440 - $nameW) / 2
$y = (160 - ($bottom - $top)) / 2 - $top
MovePath $number ($x - $nb.Left) $y
MovePath $word ($x + $nb.Width + $gap - $wb.Left) $y
$c[1].FillPath((New-Object System.Drawing.SolidBrush($accent)), $number)
$c[1].FillPath((New-Object System.Drawing.SolidBrush($text)), $word)
Save $c 'mr-logo.png'

# The mark: 360 in white on a rounded square of the accent
$c = NewCanvas 128 128
$c[1].FillPath((New-Object System.Drawing.SolidBrush($accent)), (RoundedRect 4 4 120 120 28))
$mark = TextPath '360' 54
$mb = $mark.GetBounds()
MovePath $mark ((128 - $mb.Width) / 2 - $mb.Left) ((128 - $mb.Height) / 2 - $mb.Top)
$c[1].FillPath((New-Object System.Drawing.SolidBrush($text)), $mark)
Save $c 'mr-icon.png'

# A game: a controller in the accent on a card, its pad and buttons cut out in the card's colour
$c = NewCanvas 256 256
$g = $c[1]
$g.FillPath((New-Object System.Drawing.SolidBrush($surface)), (RoundedRect 8 8 240 240 40))
$g.DrawPath((New-Object System.Drawing.Pen($border, 4)), (RoundedRect 10 10 236 236 38))

$body = New-Object System.Drawing.Drawing2D.GraphicsPath
$body.AddPath((RoundedRect 40 82 176 76 38), $false)
$body.AddEllipse(36, 104, 70, 92)
$body.AddEllipse(150, 104, 70, 92)
$body.FillMode = [System.Drawing.Drawing2D.FillMode]::Winding
$g.FillPath((New-Object System.Drawing.SolidBrush($accent)), $body)

$cut = New-Object System.Drawing.SolidBrush($surface)
$g.FillPath($cut, (RoundedRect 62 112 40 12 4))
$g.FillPath($cut, (RoundedRect 76 98 12 40 4))
foreach ($b in @(@(172, 100), @(172, 128), @(158, 114), @(186, 114)))
{
	$g.FillEllipse($cut, $b[0] - 8, $b[1] - 8 + 4, 16, 16)
}
Save $c 'DefaultGame.png'

Get-ChildItem $media -Filter *.png | Where-Object { $_.Name -in 'mr-logo.png', 'mr-icon.png', 'DefaultGame.png' } | Select-Object Name, Length
