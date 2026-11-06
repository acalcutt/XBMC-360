# The USB stick

360 Riffle goes onto a console softmodded with [Peer Pressure](https://github.com/grimdoomer/Xbox360PeerPressure)
(Grimdoomer's softmod) from a USB stick. The stick starts Peer Pressure's installer, and carries what that installer
can put on the console: 360 Riffle, Salvia's emulators and other homebrew. 360 Riffle also installs updates from it
(System info > Update from USB) and saves diagnostics to it.

Peer Pressure stays on the console once installed, so the stick is only needed to install or update things.

## What's on it

| Path | What it is | Where it comes from |
|---|---|---|
| `Content\` | The Rock Band Blitz demo, whose title screen starts the exploit, and the save that does it | The Rock Band Blitz demo package, and Peer Pressure's `SoftmodInstaller.zip` |
| `BadUpdatePayload\` | Peer Pressure's installer (`default.xex`) and the exploit's stages | `SoftmodInstaller.zip` |
| `BadUpdatePayload\SoftmodExtras\` | The extras the installer can install, listed in `SoftmodExtras.ini` | Set up below |
| `BadUpdatePayload\SystemBackups\` | This console's backup: CPU key, NAND image and hard drive security sectors | Made by the installer |
| `BadUpdatePayload\Log.txt` | The installer's log | Made by the installer |
| `$SystemUpdate\` | The 2.0.17559 system update (optional) | Microsoft's 17559 update |
| `license.txt` | Peer Pressure's licence | `SoftmodInstaller.zip` |
| `360Riffle-diagnostics\` | Diagnostics saved from System info | 360 Riffle |

**Keep `SystemBackups` safe, and to yourself.** It's this console's way back if the softmod ever has to come off, and
its CPU key and NAND image belong to this console alone. `Log.txt` lists the CPU's fuses, so don't post that either.
A new stick doesn't need them: the installer makes its own.

## Setting one up from scratch

### 1. The stick

FAT32, 16 GB or more. FAT32 takes files of up to 4 GB, which everything here fits.

### 2. Peer Pressure

Follow Peer Pressure's [installation guide](https://github.com/grimdoomer/Xbox360PeerPressure/wiki/Installation),
which comes down to this:

- Copy the Rock Band Blitz demo package's `Content` folder to the top of the stick.
- Unzip `SoftmodInstaller.zip` from [Peer Pressure's releases](https://github.com/grimdoomer/Xbox360PeerPressure/releases)
  (v1.0.477 here) and copy its `BadUpdatePayload` and `Content` folders to the top of the stick, merging them.
- If you like, add the 17559 system update's `$SystemUpdate` folder too: it reinstalls the system's extended packages
  after formatting the hard drive.

On a console that hasn't had the softmod, run the installer once to make the system backup and do its Full Install,
as the guide says. It also says which consoles it works on.

### 3. 360 Riffle

360 Riffle goes in `BadUpdatePayload\SoftmodExtras\360riffle`. Build the Release configuration (`Build.bat Release`,
or `MSBuild.exe xbmc360.sln /p:Configuration=Release "/p:Platform=Xbox 360"`), and with the stick plugged in as F:

```powershell
powershell -ExecutionPolicy Bypass -File tools\Stage-UsbStick.ps1 -Drive F:
```

That replaces what's in `360riffle` with the build's `default.xex`, the `language`, `media`, `scripts`, `system` and
`web` folders, and `skins\360 Riffle`. It never copies `settings.xml`, `sources.xml` or `UserData`, which hold a
console's own setup, so installing over 360 Riffle keeps them.

Two folders it needs aren't in this repository: `data` and `visualisations`, MilkDrop's shaders and presets. They come
from a stock XBMC-360 bundle, by default the one on the same stick in `SoftmodExtras\xbmc360` (`-Stock` gives another).
XBMC-360 has no downloads on GitHub, so keep a copy of that bundle with your backups.

### 4. Salvia, the emulators

Download `Salvia-360.zip` from [Salvia's releases](https://github.com/danibiker/Salvia/releases) (v1.1.0 here) and
unzip it into `BadUpdatePayload\SoftmodExtras\Salvia`. The zip has no folder of its own: `Nestopia.xex` and the other
cores go straight into `Salvia`.

PlayStation games need the BIOS file `SCPH1001.BIN` in `Salvia\system`. Salvia's readme lists the other BIOS files,
for the systems that need them (Famicom Disk System, Sega CD, PC Engine CD, Lynx; Game Boy Advance's is optional).

### 5. SoftmodExtras.ini

`BadUpdatePayload\SoftmodExtras\SoftmodExtras.ini` lists the extras the installer offers, a section each. Save it as
UTF-8 with a byte order mark and Windows line ends, as it comes. The entries for 360 Riffle and Salvia:

```ini
[360-Riffle]
Category = "Dashboards"
Name = "360 Riffle"
Description = "Installs 360 Riffle (XBMC-360 with MediaRiffle sources, disc copying and its own skin) and sets it as the homebrew dashboard."
Source = "360riffle"
Destination = "Hdd1:\PeerPressure\360riffle"
DashXex = "default.xex"

[Salvia]
Category = "Apps"
Name = "Salvia"
Description = "Installs Salvia, the emulators 360 Riffle plays NES, SNES, Genesis, Game Boy, N64 and PlayStation games in."
Source = "Salvia"
Destination = "Hdd1:\PeerPressure\Salvia"
```

Peer Pressure doesn't document the keys. As this file uses them:

| Key | What it does |
|---|---|
| `Category` | The installer's list it's in: `Dashboards`, `Apps` or `Xell` |
| `Name`, `Description` | What the installer shows for it |
| `Source` | Its folder in `SoftmodExtras` |
| `Destination` | Where it's copied to on the console |
| `DashXex` | Makes it the homebrew dashboard, started from this file in `Destination` |
| `EjectButtonBehavior`, `OtherOSName` | XeLL's entry: power on with the eject button to boot XeLL |

Keep Salvia an entry of its own rather than inside 360 Riffle's folder: installing 360 Riffle again then doesn't copy
Salvia's 150 MB again or reset its settings. 360 Riffle finds it at `Hdd1:\PeerPressure\Salvia` by itself.

The other entries on this stick: `[XBMC-360]` (the stock bundle that the MilkDrop folders come from) and
`[XBMC-360-MediaRiffle]`, both dashboards; `[Aurora]` as an app; and `[Xell]`.

### 6. On the console

Start the Rock Band Blitz demo and press A at its title screen; Peer Pressure's installer comes up. Install **360
Riffle**, which becomes the homebrew dashboard, and **Salvia**.

## Updating 360 Riffle

Build, run `tools\Stage-UsbStick.ps1` again, plug the stick into the console and choose **System info > Update from
USB**. It says when the build on the stick was made, installs it if you say so, and starts 360 Riffle again. The
installer's 360 Riffle entry does the same job, the long way round.
