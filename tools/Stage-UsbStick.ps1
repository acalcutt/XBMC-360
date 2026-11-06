# Puts the current build of 360 Riffle on the USB stick, where Peer Pressure's installer and System info > Update from
# USB look for it: <drive>\BadUpdatePayload\SoftmodExtras\360riffle. See docs\usb-stick.md.
#
#   powershell -ExecutionPolicy Bypass -File tools\Stage-UsbStick.ps1 -Drive F:
#
# It takes default.xex from xbmc360\Release (build the Release configuration first), the folders it runs with and its
# own skin, and the MilkDrop shaders and presets (data, visualisations), which aren't in this repository, from a stock
# XBMC-360 bundle: -Stock, by default the one on the stick at BadUpdatePayload\SoftmodExtras\xbmc360. Never
# settings.xml, sources.xml or UserData, which hold a console's own setup: installing over 360 Riffle keeps them.

param(
	[Parameter(Mandatory = $true)][string]$Drive,
	[string]$Stock
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Drive)) { throw "There's no drive $Drive. Is the stick plugged in?" }

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$extras = Join-Path $Drive 'BadUpdatePayload\SoftmodExtras'
if (-not $Stock) { $Stock = Join-Path $extras 'xbmc360' }
$out = Join-Path $extras '360riffle'
$xex = Join-Path $repo 'xbmc360\Release\xbmc360.xex'

if (-not (Test-Path $extras)) { throw "No ${extras}: set the stick up for Peer Pressure first (docs\usb-stick.md)" }
if (-not (Test-Path $xex)) { throw "No build at ${xex}: build the Release configuration first" }
foreach ($dir in 'data', 'visualisations')
{
	if (-not (Test-Path (Join-Path $Stock $dir))) { throw "No $dir in $Stock, the stock XBMC-360 bundle (docs\usb-stick.md)" }
}

if (Test-Path $out)
{
	if (-not (Test-Path (Join-Path $out 'default.xex'))) { throw "$out is there and isn't a 360 Riffle bundle" }
	Remove-Item -Recurse -Force $out
}
New-Item -ItemType Directory $out | Out-Null

Copy-Item $xex (Join-Path $out 'default.xex')
foreach ($dir in 'language', 'media', 'scripts', 'system', 'web') { Copy-Item -Recurse (Join-Path $repo $dir) (Join-Path $out $dir) }
New-Item -ItemType Directory (Join-Path $out 'skins') | Out-Null
Copy-Item -Recurse (Join-Path $repo 'skins\360 Riffle') (Join-Path $out 'skins\360 Riffle')
foreach ($dir in 'data', 'visualisations') { Copy-Item -Recurse (Join-Path $Stock $dir) (Join-Path $out $dir) }

Get-Item (Join-Path $out 'default.xex') | Select-Object FullName, Length, LastWriteTime | Format-List | Out-String
