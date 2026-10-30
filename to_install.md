# What ytpl needs

**Nothing new.** Everything below is already installed on this machine (I checked on 2026-10-06). I didn't install anything.
The list is here in case you set ytpl up on another computer.

| Needed for | Package | Already have | Arch install command |
|---|---|---|---|
| downloading | yt-dlp | ✔ 2026.08.19 | `sudo pacman -S yt-dlp` |
| merging video + audio, thumbnails | ffmpeg | ✔ | `sudo pacman -S ffmpeg` |
| YouTube's JS challenges (yt-dlp uses it automatically) | deno | ✔ | `sudo pacman -S deno` |
| compiling | g++ / make | ✔ g++ 16 | `sudo pacman -S base-devel` |
| compiling with CMake (optional, `make` is enough) | cmake | ✔ | `sudo pacman -S cmake` |
| the TUI | ncurses | ✔ 6.6 | `sudo pacman -S ncurses` |

### Optional

| What for | Package | Have it? | Arch install command |
|---|---|---|---|
| `ytpl --aria2`: many connections per video, can be faster | aria2 | ✘ not installed | `sudo pacman -S aria2` |

| Chromium cookie decryption via GNOME keyring (only if yt-dlp ever says it *can't decrypt* cookies) | python-secretstorage | ✘ not installed (not needed right now: yt-dlp already reads your Chromium cookies) | `sudo pacman -S python-secretstorage` |

Without these, ytpl works fine. It already downloads several videos in parallel (`-j`).

Debian/Ubuntu equivalents:
```
sudo apt install yt-dlp ffmpeg g++ make cmake libncurses-dev
```
(Ubuntu's yt-dlp is often old. YouTube changes things often, so keep yt-dlp up to date: `sudo pacman -Syu yt-dlp`.)
