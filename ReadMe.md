# XBMC-360

This is a port of XBMC4XBOX to XBox 360 using the Microsoft XBox 360 SDK (not using Linux and Kodi).

This is a work in progress, still many features to bring over. Though getting more usable.

### This fork: MediaRiffle
This fork adds a dashboard look and a video source for MediaRiffle, a self-hosted media server:

- **MediaRiffle skin** (`skins/MediaRiffle`): a home screen like MediaRiffle's web app (top bar with tabs, cards), on Confluence Lite. Choose it in Settings > Appearance > Skin.
- **MediaRiffle in Videos** (`mediariffle://`): Continue watching and the server's show and movie libraries. The server transcodes what plays to H.264 at 720p and streams it as HLS; where each video stopped is saved back to the server.
- **HLS playback**: `.m3u8` streams over HTTP play (the bundled FFmpeg has no HLS demuxer of its own).

To link the console, set the server in Settings > Network > MediaRiffle (on the local network plain `http://host:port` works; from outside, HTTPS), select Account, and enter the code it shows at the server's `/link` in a browser where you're signed in.

To install a build, copy `xbmc360\Release\xbmc360.xex` to the console as `default.xex`, with the `language`, `media`, `skins`, `system` and `web` folders beside it (not `settings.xml`, which holds the console's own settings).

### Current Status:
- GUILib - Mostly ported now, still have some areas to finish
- VideoPlayer/DVDPlayer
- PAPlayer - Works well for flac and MP3 (more codecs coming)
- Python 3 - Working well, but missing some of the modules (coming soon).

Usable but many features still need reimplementation or improvements.

### Compiled build (last updated 26-10-26):
https://drive.google.com/file/d/1H1zvXRBK-mJDk_cjROJDCq2tB8bKgZxK/view

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
