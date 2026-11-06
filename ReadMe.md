# 360 Riffle

A media dashboard for the Xbox 360: videos, music and pictures from the console's drives, USB, network shares and
media servers, in a modern interface. It's a fork of [XBMC-360](https://github.com/brentdc-nz/XBMC-360), the port of
XBMC4Xbox to the Xbox 360 with the Microsoft Xbox 360 SDK, and keeps its code and names inside (`xbmc360\`, the
classes) so its changes can still be merged in.

What it adds:

- **The 360 Riffle skin** (`skins/360 Riffle`): a home screen like MediaRiffle's web app (top bar with tabs, cards),
  on Confluence Lite. Videos, Games, Music and Pictures show your sources as tiles; with a disc in, a Disc tab plays
  it, copies its files to a folder, saves an ISO image of it or ejects it.
- **Settings > Sources**: every source in one place, added by saying where it is (a MediaRiffle or Jellyfin server, a
  Windows share, a folder on the console) and then only what that needs. A share or folder can allow saving, for disc
  copies and diagnostics.
- **MediaRiffle servers as sources** (`mediariffle://host:port/`, or `mediariffles://` over HTTPS): Continue watching,
  categories, your lists and the show and movie libraries, with Play all and Shuffle, and Hide (with a Hidden folder to
  show things again). The server transcodes what plays to H.264 at 720p and streams it as HLS; where each video stopped
  is saved back to the server.
- **Jellyfin servers as sources** (`jellyfin://host:port/`, or `jellyfins://`; Emby's API too): Continue watching, Next
  up and the video libraries, streamed the same way.
- **Network shares** that can be written to, and signed in to with a domain account (`DOMAIN\name` or
  `name@domain`), with file sizes and dates in their listings.
- **Games for other consoles** in Games, next to Xbox 360 games: NES, SNES, Genesis, Master System, Game Gear, Sega CD,
  32X, Game Boy (Color, Advance), N64, PlayStation, PC Engine, Neo Geo Pocket, WonderSwan, Lynx and Virtual Boy. They
  play in [Salvia](https://github.com/danibiker/Salvia)'s emulators, an XEX for each, which 360 Riffle starts with the
  game. A game on a share is copied to the console first, as the emulators read the console's drives only. The system
  comes from the extension, or the folder's name for ones that many systems use (a `.zip` in `Genesis`).
- **HLS playback**: `.m3u8` streams over HTTP play (the bundled FFmpeg has no HLS demuxer of its own).
- **Show all videos** in a folder and the folders below it as one list, **Shuffle** for any folder in Videos, the
  console's own keyboard for typing, controller shortcuts on the on-screen keyboard, filled-in System info pages, and
  **Save diagnostics to USB** there (system details and the logs, never passwords or tokens).

To add a MediaRiffle or Jellyfin server, choose Settings > Sources > Add a source, pick the kind of server and give
its address (as `https://…` or with port 443 for HTTPS). The console then links to it straight away: MediaRiffle shows
a code to enter at the server's `/link` in a browser where you're signed in, and Jellyfin a Quick Connect code to enter
in your Jellyfin profile (or asks for a name and password where Quick Connect is off). Sources can be changed, removed
or signed out of on the same page.

To play games for other consoles, unzip Salvia-360 from [Salvia's releases](https://github.com/danibiker/Salvia/releases)
to a folder at the top of the hard drive or a USB drive, or one folder down (`Hdd1:\Salvia`, `Usb0:\Emulators\Salvia`),
with the BIOS files its readme lists in its `system` folder (PlayStation games need `SCPH1001.BIN`). With Peer
Pressure, a SoftmodExtras entry of its own (Category `Apps`, Source `Salvia`, Destination `Hdd1:\PeerPressure\Salvia`)
installs it to the hard drive, apart from 360 Riffle, so updating 360 Riffle leaves Salvia's settings alone. Then add
the folder with your games as a Games source. Leaving Salvia goes to the console's default dashboard, so set 360 Riffle as
that (`DashXex` in SoftmodExtras.ini with Peer Pressure) to come back to it. DS, 3DS, PSP, PS2, GameCube, Wii,
Dreamcast and Saturn games are listed too, but no emulator for them runs on the 360 yet.

To install a build, copy `xbmc360\Release\xbmc360.xex` to the console as `default.xex` (in
`Hdd1:\PeerPressure\360riffle` with the Peer Pressure softmod), with the `language`, `media`, `scripts`, `system` and
`web` folders and `skins\360 Riffle` beside it. Leave out `settings.xml`, which holds the console's own settings.

## From XBMC-360

This is a port of XBMC4XBOX to XBox 360 using the Microsoft XBox 360 SDK (not using Linux and Kodi).

### Current Status:
- GUILib - Mostly ported now, still have some areas to finish
- VideoPlayer/DVDPlayer
- PAPlayer - Works well for flac and MP3 (more codecs coming)
- Python 3 - Working well, but missing some of the modules (coming soon).

Usable but many features still need reimplementation or improvements.

### Libraries
- FFmpeg (DVDPlayer)
- libdvdnav
- libdvdread
- LibSMB2
- FreeType
- MilkDrop2 (Music Visualizations)
- libFlac (PAPlayer)
- libMad (PAPlayer)
- libCurl 
- libUPnP - DLNA (Neptune & Platinum)
- sqlite3
- libiconv (Charset Converter)
- libjpeg
- libid3tag
- libmicrohttpd
- libPython

### Services
- FTP Server
- NTP Client
- UPnP

### Tools
- XBMC360Tex (XPR skin textures bundle tool)

### Notes:
Special thanks to XBMC4XBOX and Kodi.

## Licence

360 Riffle is free software under the GNU General Public License, version 2 or (at your option) any later version,
like the XBMC and XBMC4Xbox code it comes from: see [COPYING](COPYING). The libraries in `libraries\` keep their own
licences, each with its licence file there. Building needs the Microsoft Xbox 360 SDK, which isn't part of this
repository.
