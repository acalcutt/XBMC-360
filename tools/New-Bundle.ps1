# Makes the bundle a console runs 360 Riffle from, as the softmod's installer and System info > Update from USB take
# it. Stage-UsbStick.ps1 puts one on a USB stick; CI zips one for each build and release (.gitlab-ci.yml).
#
#   powershell -ExecutionPolicy Bypass -File tools\New-Bundle.ps1 -Path C:\Temp\360riffle -Zip C:\Temp\360Riffle.zip
#
# It takes default.xex from xbmc360\Release (build the Release configuration first), the folders it runs with and its
# own skin, and the MilkDrop shaders and presets (data, visualisations), which aren't in this repository, from a stock
# XBMC-360 bundle (-Stock). Without one the bundle still updates an installed 360 Riffle, which keeps its own, but a new
# install has no visualisations. Never settings.xml, sources.xml or UserData, which hold a console's own setup.
#
# -Zip zips it too, as a 360riffle folder: unzipped at the top of a USB drive, Update from USB finds it there.

param(
	[Parameter(Mandatory = $true)][string]$Path,
	[string]$Stock,
	[string]$Zip
)

$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$xex = Join-Path $repo 'xbmc360\Release\xbmc360.xex'
if (-not (Test-Path $xex)) { throw "No build at ${xex}: build the Release configuration first" }

if ($Stock)
{
	foreach ($dir in 'data', 'visualisations')
	{
		if (-not (Test-Path (Join-Path $Stock $dir))) { throw "No $dir in $Stock, the stock XBMC-360 bundle (docs\usb-stick.md)" }
	}
}
else
{
	Write-Warning 'No stock XBMC-360 bundle (-Stock): the bundle has no MilkDrop data or visualisations, so it only updates an installed 360 Riffle'
}

if (Test-Path $Path)
{
	if (-not (Test-Path (Join-Path $Path 'default.xex'))) { throw "$Path is there and isn't a 360 Riffle bundle" }
	Remove-Item -Recurse -Force $Path
}
New-Item -ItemType Directory $Path | Out-Null

Copy-Item $xex (Join-Path $Path 'default.xex')
foreach ($dir in 'language', 'media', 'scripts', 'system', 'web') { Copy-Item -Recurse (Join-Path $repo $dir) (Join-Path $Path $dir) }
New-Item -ItemType Directory (Join-Path $Path 'skins') | Out-Null
Copy-Item -Recurse (Join-Path $repo 'skins\360 Riffle') (Join-Path $Path 'skins\360 Riffle')
if ($Stock)
{
	foreach ($dir in 'data', 'visualisations') { Copy-Item -Recurse (Join-Path $Stock $dir) (Join-Path $Path $dir) }
}

Get-Item (Join-Path $Path 'default.xex') | Select-Object FullName, Length, LastWriteTime | Format-List | Out-String

if ($Zip)
{
	# Entry by entry, so that they're 360riffle/<path> with forward slashes whatever the folder's called
	Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
	$zipFile = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Zip)
	New-Item -ItemType Directory -Force (Split-Path $zipFile) | Out-Null
	if (Test-Path $zipFile) { Remove-Item $zipFile }

	$root = (Resolve-Path $Path).Path.TrimEnd('\')
	$archive = [IO.Compression.ZipFile]::Open($zipFile, [IO.Compression.ZipArchiveMode]::Create)
	try
	{
		foreach ($file in Get-ChildItem $root -Recurse -File)
		{
			$entry = '360riffle/' + $file.FullName.Substring($root.Length + 1).Replace('\', '/')
			[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
		}
	}
	finally
	{
		$archive.Dispose()
	}

	Get-Item $zipFile | Select-Object FullName, Length | Format-List | Out-String
}
