# Draws the flat shapes the 360 Riffle skin is built from into skins\360 Riffle\media, as loose PNGs (the skin's
# Textures.xpr is Confluence Lite's; a texture missing from it is loaded from media\ instead).
#
# White shapes are tinted in the skin with <colordiffuse> and the palette in colors\defaults.xml. Buttons can't be
# tinted per state (one diffuse colour covers focused and unfocused), so those come in their colours here.
#
#   powershell -ExecutionPolicy Bypass -File tools\360RiffleSkin\make-textures.ps1

Add-Type -AssemblyName System.Drawing

$media = Join-Path $PSScriptRoot '..\..\skins\360 Riffle\media'

# MediaRiffle's dark theme (src/MediaRiffle.Server/wwwroot/css/site.css)
$accent  = [System.Drawing.ColorTranslator]::FromHtml('#4b9ef5')
$surface = [System.Drawing.ColorTranslator]::FromHtml('#1c1c21')
$border  = [System.Drawing.ColorTranslator]::FromHtml('#33333a')
$white   = [System.Drawing.Color]::White

function RoundedRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r)
{
	$p = New-Object System.Drawing.Drawing2D.GraphicsPath
	if ($r -le 0)
	{
		$p.AddRectangle((New-Object System.Drawing.RectangleF($x, $y, $w, $h)))
		return $p
	}
	$d = $r * 2
	$p.AddArc($x, $y, $d, $d, 180, 90)
	$p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
	$p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
	$p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
	$p.CloseFigure()
	return $p
}

# A size x size PNG with one rounded rectangle: filled, outlined (ring > 0: that wide, inside the edge), or both
function Shape([string]$name, [int]$size, [float]$radius, $fill, $line, [float]$ring)
{
	$bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
	$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
	$g.Clear([System.Drawing.Color]::Transparent)
	if ($fill)
	{
		$brush = New-Object System.Drawing.SolidBrush($fill)
		$g.FillPath($brush, (RoundedRect 0 0 $size $size $radius))
	}
	if ($line)
	{
		$pen = New-Object System.Drawing.Pen($line, $ring)
		$half = $ring / 2
		$g.DrawPath($pen, (RoundedRect $half $half ($size - $ring) ($size - $ring) ($radius - $half)))
	}
	$g.Dispose()
	$bmp.Save((Join-Path $media $name), [System.Drawing.Imaging.ImageFormat]::Png)
	$bmp.Dispose()
}

# Fills and tiles (tinted in the skin): border="16" in the XML keeps the corners round at any size
Shape 'mr-fill.png'          16 0  $white   $null   0
Shape 'mr-round.png'         64 12 $white   $null   0
Shape 'mr-round-outline.png' 64 12 $null    $white  3

# A card (MediaRiffle's .card): the surface, with a border
Shape 'mr-card.png'          64 12 $surface $border 1

# Buttons and tabs: MediaRiffle's 8px corners, accent when focused, a bordered surface when not
Shape 'mr-button-focus.png'  64 8  $accent  $null   0
Shape 'mr-button.png'        64 8  $surface $border 1
