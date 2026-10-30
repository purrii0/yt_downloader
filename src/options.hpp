// options.hpp — every setting the user can change from the command line.
#pragma once

#include <string>

// What to do when a URL is a video *inside* a playlist (watch?v=X&list=Y).
enum class PlaylistChoice {
    Ask,        // ask the user (default)
    VideoOnly,  // --video
    Playlist,   // --playlist
};

struct Options {
    std::string url;              // the YouTube link
    std::string outdir = "~/videos";  // base folder (playlists get a subfolder inside)
    std::string select;           // selection like "1,3,^5,7*" (empty = ask)
    std::string cookies_browser;  // e.g. "firefox" -> --cookies-from-browser
    int retries = 3;              // -r : automatic retries per failed video (333 ✨)
    int jobs = 4;                 // -j : how many videos download at the same time
    int max_height = 0;           // 0 = no limit, otherwise e.g. 1080
    bool tui = false;             // -t : start the ncurses interface
    bool audio_only = false;      // -a : only grab the audio
    bool quiet = false;           // -q : hide the progress bar
    bool aria2 = false;           // --aria2 : use aria2c for more connections per video
    bool dry_run = false;         // -n : show what would happen, download nothing
    PlaylistChoice playlist_choice = PlaylistChoice::Ask;
};
