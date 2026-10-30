// config.cpp — see config.hpp.
#include "config.hpp"

#include <cstdlib>
#include <fstream>

#include "util.hpp"

namespace {

// "12" -> 12, anything else -> -1
int to_count(const std::string& s) {
    if (s.empty() || s.size() > 4 || s.find_first_not_of("0123456789") != std::string::npos)
        return -1;
    return std::atoi(s.c_str());
}

}  // namespace

std::string config_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base = (xdg && *xdg) ? xdg : expand_home("~/.config");
    return base + "/ytpl/config";
}

bool load_config(Options& opt, std::string& warnings) {
    std::ifstream file(config_path());
    if (!file) return true;  // no config file: just use the built-in defaults

    std::string line;
    int line_no = 0;
    while (std::getline(file, line)) {
        ++line_no;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);  // drop comments
        line = trim(line);
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warnings += config_path() + ":" + std::to_string(line_no) + ": expected key = value\n";
            continue;
        }
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        int n = to_count(value);

        if (key == "cookies")                                  opt.cookies_browser = value;
        else if (key == "output" && !value.empty())            opt.outdir = value;
        else if (key == "jobs" && n >= 1 && n <= 10)           opt.jobs = n;
        else if (key == "retries" && n >= 0 && n <= 20)        opt.retries = n;
        else if (key == "max_height" && n >= 0)                opt.max_height = n;
        else
            warnings += config_path() + ":" + std::to_string(line_no) + ": ignored \"" + line + "\"\n";
    }
    return true;
}
