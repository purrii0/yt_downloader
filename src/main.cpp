// main.cpp — reads the command line flags and starts the CLI or the TUI.
#include <clocale>
#include <cstdlib>
#include <iostream>
#include <string>

#include "app.hpp"
#include "config.hpp"
#include "options.hpp"

namespace {

void print_help() {
    std::cout <<
R"(ytpl — download YouTube videos and playlists in the best quality (uses yt-dlp)

Usage:
  ytpl [options] <url>        plain terminal mode
  ytpl -t [options] [url]     interactive TUI

Options:
  -t, --tui              start the TUI (the URL can be typed inside it)
  -o, --output DIR       base folder (default: ~/videos).
                         Videos go in DIR/, playlists in DIR/Playlist_Name/
  -s, --select EXPR      which playlist videos to get, without asking:
                           all     every video
                           1,3,5   videos 1, 3 and 5
                           ^5      videos 1 to 5
                           5*      video 5 to the end
                           2-4     videos 2 to 4   (mix them: "1,3,^2,7*")
      --video            for watch?v=..&list=.. links: just the video
      --playlist         for watch?v=..&list=.. links: the whole playlist
  -j, --jobs N           how many videos download at the same time (default 4, max 10)
  -r, --retries N        retry a failed video automatically N times (default 3, 0 = off)
      --aria2            use aria2c for many connections per video (needs aria2,
                         see to_install.md; live progress bars won't move)
  -a, --audio            audio only (best audio, original codec)
  -m, --max-height N     limit resolution, e.g. 1080 (default: no limit)
  -c, --cookies BROWSER  use your browser's YouTube login (chromium, chrome,
                         brave, firefox, ...). Fixes "confirm you're not a bot"
                         and private / age-restricted videos
      --no-cookies       don't use the browser login this time
  -q, --quiet            hide the progress bar
  -n, --dry-run          show the videos and the yt-dlp command, download nothing
  -h, --help             show this help

Files are named without spaces, e.g.  My_Playlist/003-Some_Video_Title.mkv
Running the same playlist again skips videos you already have.

Saved defaults (cookies, jobs, retries, output, max_height) go in
~/.config/ytpl/config as "key = value" lines, e.g.  cookies = chromium
)";
}

// For flags like "-o DIR": takes the next argument or exits with an error.
std::string need_value(int& i, int argc, char** argv) {
    if (i + 1 >= argc) {
        std::cerr << "Option " << argv[i] << " needs a value (see ytpl -h)\n";
        std::exit(2);
    }
    return argv[++i];
}

}  // namespace

int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");  // so UTF-8 titles are measured and drawn correctly

    // 1. saved defaults from ~/.config/ytpl/config, 2. command line flags on top
    Options opt;
    std::string warnings;
    load_config(opt, warnings);
    if (!warnings.empty()) std::cerr << warnings;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];

        if (a == "-h" || a == "--help")            { print_help(); return 0; }
        else if (a == "-t" || a == "--tui")        opt.tui = true;
        else if (a == "-a" || a == "--audio")      opt.audio_only = true;
        else if (a == "-q" || a == "--quiet")      opt.quiet = true;
        else if (a == "-n" || a == "--dry-run")    opt.dry_run = true;
        else if (a == "--no-cookies")              opt.cookies_browser.clear();
        else if (a == "--aria2")                   opt.aria2 = true;
        else if (a == "--video")                   opt.playlist_choice = PlaylistChoice::VideoOnly;
        else if (a == "--playlist")                opt.playlist_choice = PlaylistChoice::Playlist;
        else if (a == "-o" || a == "--output")     opt.outdir = need_value(i, argc, argv);
        else if (a == "-s" || a == "--select")     opt.select = need_value(i, argc, argv);
        else if (a == "-c" || a == "--cookies")    opt.cookies_browser = need_value(i, argc, argv);
        else if (a == "-j" || a == "--jobs") {
            opt.jobs = std::atoi(need_value(i, argc, argv).c_str());
            if (opt.jobs < 1 || opt.jobs > 10) { std::cerr << "--jobs wants a number from 1 to 10\n"; return 2; }
            if (opt.jobs > 6)
                std::cerr << "Note: more than 6 at once can make YouTube slow you down (HTTP 429).\n";
        }
        else if (a == "-r" || a == "--retries") {
            std::string v = need_value(i, argc, argv);
            opt.retries = std::atoi(v.c_str());
            if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos || opt.retries > 20) {
                std::cerr << "--retries wants a number from 0 to 20\n";
                return 2;
            }
        }
        else if (a == "-m" || a == "--max-height") {
            opt.max_height = std::atoi(need_value(i, argc, argv).c_str());
            if (opt.max_height <= 0) { std::cerr << "--max-height wants a number like 1080\n"; return 2; }
        }
        else if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << "Unknown option " << a << " (see ytpl -h)\n";
            return 2;
        }
        else if (opt.url.empty())                  opt.url = a;
        else { std::cerr << "Only one URL at a time, please.\n"; return 2; }
    }

    return opt.tui ? run_tui(opt) : run_cli(opt);
}
