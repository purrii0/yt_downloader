// config.hpp — reads your saved defaults from ~/.config/ytpl/config.
//
// The file is plain "key = value" lines; '#' starts a comment. Example:
//
//     cookies = chromium     # use this browser's YouTube login
//     jobs = 4
//     retries = 3
//     output = ~/videos
//
// Command line flags always win over the file.
#pragma once

#include <string>

#include "options.hpp"

// Where the config file lives (~/.config/ytpl/config, honoring $XDG_CONFIG_HOME).
std::string config_path();

// Fills `opt` from the config file. A missing file is fine (returns true).
// Bad lines are reported in `warnings` (one per line) and skipped.
bool load_config(Options& opt, std::string& warnings);
