# Puts the current build of 360 Riffle on the USB stick, where Peer Pressure's installer and System info > Update from
# USB look for it: <drive>\BadUpdatePayload\SoftmodExtras\360riffle. See docs\usb-stick.md.
#
#   powershell -ExecutionPolicy Bypass -File tools\Stage-UsbStick.ps1 -Drive F:
#
# New-Bundle.ps1 makes the bundle there (build the Release configuration first), with the MilkDrop shaders and presets
# (data, visualisations) from a stock XBMC-360 bundle: -Stock, by default the one on the stick at
# BadUpdatePayload\SoftmodExtras\xbmc360. Never settings.xml, sources.xml or UserData, which hold a console's own setup:
# installing over 360 Riffle keeps them.

param(
	[Parameter(Mandatory = $true)][string]$Drive,
	[string]$Stock
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Drive)) { throw "There's no drive $Drive. Is the stick plugged in?" }

$extras = Join-Path $Drive 'BadUpdatePayload\SoftmodExtras'
if (-not (Test-Path $extras)) { throw "No ${extras}: set the stick up for Peer Pressure first (docs\usb-stick.md)" }
if (-not $Stock) { $Stock = Join-Path $extras 'xbmc360' }

& (Join-Path $PSScriptRoot 'New-Bundle.ps1') -Path (Join-Path $extras '360riffle') -Stock $Stock
