# ytpl

[![Written by Claude Code](https://img.shields.io/badge/Written%20by-Claude%20Code-D97757?logo=claude&logoColor=white)](https://claude.com/product/claude-code)

Download YouTube videos and playlists at the **highest resolution and bitrate** available, using `yt-dlp`.
ytpl has a plain command-line mode and a terminal UI (TUI, started with `-t`).

## Contents

- [Build](#build)
- [Usage](#usage)
- [Choosing videos from a playlist](#choosing-videos-from-a-playlist)
- [Where files go](#where-files-go)
- [Retries](#retries)
- [Quality](#quality)
- ["Sign in to confirm you're not a bot"](#sign-in-to-confirm-youre-not-a-bot)
- [Config file](#config-file)
- [Speed](#speed)
- [Code tour](#code-tour)

## Build

With Make:

```
make            # builds ./ytpl
make test       # runs the tests for the selection parser
```

Or with CMake:

```
cmake -B build && cmake --build build
```

Optional: `cp ytpl ~/.local/bin/` to run it from anywhere.

## Usage

```
./ytpl <url>                       # CLI: detects whether it's a video or a playlist
./ytpl -t                          # TUI: paste the URL inside
./ytpl -t <url>                    # TUI with the URL already filled in
./ytpl -s "^5" <playlist-url>      # first 5 videos, no questions asked
./ytpl -j 6 <playlist-url>         # 6 videos at the same time (default 4)
./ytpl -n <url>                    # dry run: list videos and show the yt-dlp command
./ytpl -h                          # all options
```

## Choosing videos from a playlist

For a playlist, ytpl prints a numbered list and asks which videos you want:

| You type    | You get                       |
|-------------|-------------------------------|
| `all` / Enter | every video                 |
| `1,3,5`     | videos 1, 3 and 5             |
| `^5`        | videos 1 to 5                 |
| `5*`        | video 5 to the end            |
| `2-4`       | videos 2 to 4                 |
| `1,3,^2,7*` | any mix of the above          |

The TUI uses the same syntax: press `/` on the selection screen.
You can also move with `↑`/`↓`, tick a video with `space`, and toggle all with `a`.

## Where files go

- **Single video:** saved in `~/videos/`, for example `~/videos/Some_Video_Title.mkv`.
- **Playlist:** saved in `~/videos/<playlist name>/`, for example `~/videos/My_Playlist/001-First_Video.mkv`, `002-…`.
- Spaces in filenames become `_`. The title stored inside the file keeps its normal spaces.
- `-o DIR` changes the base folder.
- Running the same playlist again skips videos you already have. They are tracked in `My_Playlist/.archive.txt`; delete that file to download them again.

## Retries

**Automatic:** a failed video is retried up to **3** times, waiting 3s, then 6s, then 9s between attempts.
Use `-r N` to change the count, or `-r 0` to turn retries off.
Errors that retrying can't fix (private, removed or region-locked videos) are not retried.

**Manual:** if videos still fail, then at the end of the round:

- the CLI asks `Retry N failed video(s)? [Y/n]`
- the TUI shows `r retry N failed`

Only the failed videos run again. Videos you stopped part-way, or that never got a turn, are included too, and they resume where they left off.

## Quality

ytpl passes yt-dlp these options:

```
-f "bv*+ba/b" -S "res,fps,hdr:12,vcodec,br,asr"
```

In plain words: take the best video stream and the best audio stream, preferring higher resolution first, then higher fps, then HDR, then a better codec, then **higher bitrate**.
The two streams are merged into `.mkv` without re-encoding, so nothing is lost.

- `-m 1080` caps the resolution (change the number as needed).
- `-a` downloads audio only.

## "Sign in to confirm you're not a bot"

YouTube sometimes blocks downloads that aren't signed in, especially when several run at once.
ytpl can read your YouTube login from your browser. This is read-only, and nothing leaves your machine except requests to YouTube.

Use `-c BROWSER` for one run, or set a default in the config file:

```
# ~/.config/ytpl/config
cookies = chromium
```

`--no-cookies` skips the login for one run.
Bot-check failures are not retried automatically, because only a login fixes them. ytpl prints a hint telling you what to do.

## Config file

`~/.config/ytpl/config` holds your defaults as `key = value` lines:

| Key          | Same as flag |
|--------------|--------------|
| `cookies`    | `-c`         |
| `jobs`       | `-j`         |
| `retries`    | `-r`         |
| `output`     | `-o`         |
| `max_height` | `-m`         |

Command-line flags always win over the file.

## Speed

Videos download **in parallel**. ytpl starts one yt-dlp per video, with up to `-j N` running at once (default 4). Each one also fetches up to 4 pieces of its own video at a time (`--concurrent-fragments 4`).

YouTube limits the speed of each connection, so running several at once helps a lot. Going above about 6 can get you temporarily slowed down (HTTP 429). If that happens, lower `-j`.

`--aria2` makes each video use many connections through aria2c. It's optional (see `to_install.md`), and the live progress bars don't move while it runs.

## Code tour

Everything lives in `src/`:

| File                    | What it does                                                                     |
|-------------------------|----------------------------------------------------------------------------------|
| `main.cpp`              | reads the flags, then starts the CLI or the TUI                                  |
| `options.hpp`           | the settings struct                                                              |
| `ytdlp.cpp`             | runs yt-dlp (fork/exec + pipe), probes the URL, builds the command, parses output |
| `select.cpp`            | the `^5`, `5*`, `1,3,5` parser                                                   |
| `downloader.cpp`        | the worker pool: N threads, each running one yt-dlp per video                    |
| `tracker.cpp`           | keeps the status of each video (queued / downloading / done …)                   |
| `cli.cpp`               | plain terminal mode with the progress bar                                        |
| `tui.cpp`               | ncurses screens                                                                  |
| `app.cpp`, `util.cpp`   | small shared helpers (folder name, progress bar, text width)                     |

### How progress works

ytpl asks yt-dlp to print special marker lines using `--print` and `--progress-template`:

- `@@S` for start
- `@@P` for progress
- `@@D` for done

Worker threads only read output and pass `{video, line}` to the main thread. The main thread runs `parse_line()` and `Tracker`, and does all the drawing.
