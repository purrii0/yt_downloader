// util.hpp — small text helpers used by both the CLI and the TUI.
#pragma once

#include <string>

// "My Cool / Playlist!" -> "My_Cool_Playlist!"
// Spaces become '_', characters that are bad in folder names are dropped.
std::string clean_name(const std::string& name);

// Cuts `text` so it takes at most `width` columns on screen (UTF-8 aware,
// so emoji / Japanese titles don't get chopped in half). Adds "…" if cut.
// The result is padded with spaces to exactly `width` columns when `pad` is true.
std::string fit(const std::string& text, int width, bool pad = false);

// How many columns `text` takes on screen ("█" is 1, "日" is 2).
int text_width(const std::string& text);

// How many columns the terminal has (80 if we can't tell).
int terminal_width();

// "~/videos" -> "/home/you/videos" (other paths are returned unchanged).
std::string expand_home(const std::string& path);

// Removes spaces/newlines from both ends.
std::string trim(const std::string& s);
