# 360 Riffle

A media dashboard for the Xbox 360: videos, music and pictures from the console's drives, USB, network shares and
media servers, in a modern interface. It's a fork of [XBMC-360](https://github.com/brentdc-nz/XBMC-360), the port of
XBMC4Xbox to the Xbox 360 with the Microsoft Xbox 360 SDK, and keeps its code and names inside (`xbmc360\`, the
classes) so its changes can still be merged in.

What it adds:

- **The 360 Riffle skin** (`skins/360 Riffle`): a home screen like MediaRiffle's web app (top bar with tabs, cards), on
  Confluence Lite.
- **MediaRiffle servers as sources** (`mediariffle://host:port/`, or `mediariffles://` over HTTPS): Continue watching,
  categories, your lists and the show and movie libraries, with Play all and Shuffle. The server transcodes what plays
  to H.264 at 720p and streams it as HLS; where each video stopped is saved back to the server.
- **HLS playback**: `.m3u8` streams over HTTP play (the bundled FFmpeg has no HLS demuxer of its own).
- **Shuffle** for any folder in Videos, the console's own keyboard for typing, controller shortcuts on the on-screen
  keyboard, and filled-in System info pages.

To add a MediaRiffle server, in Videos choose Add source > Browse > Add network location, pick **MediaRiffle server**
(or **MediaRiffle server (HTTPS)** from outside the local network), and give its address and port; the home screen's
MediaRiffle tile asks for one too when there's none. The first time the source is opened, the console shows a code to
enter at the server's `/link` in a browser where you're signed in. To sign out, open the source's context menu.

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
