# Changelog

## master
### ✨ Features and improvements
- _...Add new stuff here..._

### 🐞 Bug fixes
- _...Add new stuff here..._

## 0.1.0
The first version of 360 Riffle, from [XBMC-360](https://github.com/brentdc-nz/XBMC-360).

### ✨ Features and improvements
- **The 360 Riffle skin**: a home screen like MediaRiffle's web app, with tabs whose tiles are your sources, and a Disc
  tab while there's a disc in. The splash screen says 360 Riffle.
- **Settings > Sources**: add, change and remove every source in one place, by saying where it is (a MediaRiffle or
  Jellyfin server, a Windows share, a folder on the console). A share or folder can hold more than one kind of media,
  and can allow saving, for disc copies and diagnostics.
- **MediaRiffle servers** as sources, linked with a code: Continue watching, categories, your lists and the libraries,
  Play all and Shuffle, and Hide with a Hidden folder. Videos stream as HLS at 720p and where each one stopped is saved
  to the server.
- **Jellyfin servers** as sources, signed in to with Quick Connect or a password: Continue watching, Next up and the
  video libraries, Play all and Shuffle, with progress sent back to the server.
- **Windows shares** can be written to and signed in to with a domain account, and list files with their sizes and
  dates.
- **Discs**: play the disc, copy its files to a folder, save an ISO image of it, or eject it. An Xbox 360 game disc is
  copied to the hard drive and played from there, as Peer Pressure doesn't start games from the disc, and starts at
  once the next time.
- **Games for other consoles** (NES to PlayStation) play in [Salvia](https://github.com/danibiker/Salvia)'s emulators,
  copied to the console from a share first. Zipped games unzip on the console.
- **Xbox 360 games and homebrew on a share** are copied to the console to play, and kept with their saves. The games
  played longest ago are freed when space runs low, and System info > Games on the console shows each copy's size.
- **On this console** in Games: the games copied to the console, to play without their share or disc.
- **Videos**: Show all videos in a folder and its folders as one list, Shuffle a folder, play HLS (`.m3u8`) streams,
  and skip to the previous or next video with LB and RB.
- **Update from USB** in System info installs a new build without the softmod's installer.
- **Releases** on GitLab, each with a zip to unzip at the top of a USB drive for Update from USB.
- **Save diagnostics to USB** in System info (never passwords or tokens), and filled-in System info pages.
- **Typing** with the console's own keyboard, and controller shortcuts on the on-screen keyboard.

### 🐞 Bug fixes
- Stopping a video with B no longer freezes the console.
- Share passwords and server keys are kept out of the log.
- Every console drive works as a path (`Dvd:`, `Usb1:`, `Flash:`...), not only `C:`, `Hdd1:` and `Usb0:`.
- The screensaver no longer comes on while something is being copied or saved.
- The log is moved to `xbmc.old.log` at each start, so it holds one run.
- Stopping an HLS stream no longer waits on the server.
