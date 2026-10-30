// util.cpp — see util.hpp.
#include "util.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <cctype>
#include <climits>
#include <cstdlib>
#include <cwchar>

std::string clean_name(const std::string& name) {
    std::string out;
    bool last_was_underscore = false;
    for (unsigned char c : name) {
        if (std::isspace(c) || c == '/' || c == '\\') {
            // collapse any run of spaces into one '_'
            if (!last_was_underscore && !out.empty()) out += '_';
            last_was_underscore = true;
            continue;
        }
        // characters that Windows/Linux file managers dislike
        if (c < 32 || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            continue;
        out += static_cast<char>(c);
        last_was_underscore = false;
    }
    while (!out.empty() && (out.back() == '_' || out.back() == '.')) out.pop_back();
    if (out.empty()) out = "playlist";
    return out;
}

namespace {

// Copies characters from `text` until `limit` columns are used.
// Returns the copied part and stores the columns it takes in `used`.
std::string take_columns(const std::string& text, int limit, int& used) {
    std::string out;
    used = 0;
    std::mbstate_t state{};
    const char* p = text.c_str();
    const char* end = p + text.size();

    while (p < end) {
        wchar_t wc;
        size_t len = std::mbrtowc(&wc, p, end - p, &state);
        if (len == 0 || len == static_cast<size_t>(-1) || len == static_cast<size_t>(-2)) {
            // broken UTF-8 byte: skip it
            state = std::mbstate_t{};
            ++p;
            continue;
        }
        int w = wcwidth(wc);
        if (w < 0) w = 1;              // unprintable -> count as 1 column
        if (used + w > limit) break;   // doesn't fit anymore
        out.append(p, len);
        used += w;
        p += len;
    }
    return out;
}

}  // namespace

std::string fit(const std::string& text, int width, bool pad) {
    if (width <= 0) return "";

    int used = 0;
    std::string out = take_columns(text, INT_MAX, used);  // measure the full text
    if (used > width) {
        out = take_columns(text, width - 1, used);        // leave room for "…"
        out += "…";
        used += 1;
    }
    if (pad)
        while (used < width) { out += ' '; ++used; }
    return out;
}

int text_width(const std::string& text) {
    int used = 0;
    take_columns(text, INT_MAX, used);
    return used;
}

int terminal_width() {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    return 80;
}

std::string expand_home(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    if (path.size() > 1 && path[1] != '/') return path;  // "~bob/..." isn't supported
    const char* home = std::getenv("HOME");
    if (!home || !*home) return path;
    return std::string(home) + path.substr(1);
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
