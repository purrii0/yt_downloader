// app.hpp — the two front ends, plus a few helpers they share.
#pragma once

#include <string>
#include <vector>

#include "options.hpp"
#include "tracker.hpp"
#include "ytdlp.hpp"

// Plain terminal mode (default).
int run_cli(Options opt);

// ncurses mode (-t).
int run_tui(Options opt);

// Where files go: the base folder for a single video, or
// base/Playlist_Name for a playlist.
std::string download_folder(const Options& opt, const ProbeResult& info);

// The chosen videos, in playlist order (e.g. numbers {1,3} -> entries 1 and 3).
std::vector<VideoEntry> pick_entries(const ProbeResult& info, const std::vector<int>& selected);

// One Tracker item per video.
std::vector<Item> make_items(const std::vector<VideoEntry>& videos);

// A text progress bar like "██████░░░░" that is `width` columns wide.
std::string make_bar(double percent, int width);

// If some video failed because of YouTube's bot check, a short "how to fix
// it" message; otherwise an empty string.
std::string bot_check_hint(const Options& opt, const Tracker& t);

// "video" / "audio" label for the stream being downloaded right now.
std::string stream_label(const Options& opt, const Item& it);
