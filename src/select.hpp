// select.hpp — turns text like "1,3,^5,7*" into a list of video numbers.
#pragma once

#include <string>
#include <vector>

// Supported pieces (separated by commas or spaces):
//   all   -> every video          (an empty string means "all" too)
//   7     -> just video 7
//   ^5    -> videos 1 to 5
//   5*    -> video 5 to the last one
//   2-4   -> videos 2 to 4
//
// `total` is how many videos the playlist has.
// On success: fills `out` with sorted, unique numbers and returns true.
// On failure: puts a human readable message in `error` and returns false.
bool parse_selection(const std::string& text, int total,
                     std::vector<int>& out, std::string& error);

// Turns {1,2,3,7} back into "1,2,3,7" (the format yt-dlp's --playlist-items wants).
std::string join_indices(const std::vector<int>& indices);
