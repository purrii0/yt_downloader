// select.cpp — parser for the video selection syntax (see select.hpp).
#include "select.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace {

// Reads a positive whole number. Returns false if `s` is not one.
bool to_number(const std::string& s, int& value) {
    if (s.empty() || s.size() > 9) return false;  // 9 digits is plenty, avoids overflow
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    value = std::stoi(s);
    return true;
}

// Splits "1, 3 ^5" into {"1", "3", "^5"} — commas and spaces both separate.
std::vector<std::string> split_tokens(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : text) {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) tokens.push_back(current);
            current.clear();
        } else {
            current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

}  // namespace

bool parse_selection(const std::string& text, int total,
                     std::vector<int>& out, std::string& error) {
    std::vector<std::string> tokens = split_tokens(text);
    std::set<int> chosen;  // a set keeps numbers sorted and drops duplicates

    // Nothing typed, or "all" anywhere -> take everything.
    bool want_all = tokens.empty() ||
                    std::find(tokens.begin(), tokens.end(), "all") != tokens.end();
    if (want_all) {
        out.clear();
        for (int i = 1; i <= total; ++i) out.push_back(i);
        return true;
    }

    for (const std::string& tok : tokens) {
        int from = 0, to = 0;

        if (tok[0] == '^') {                                  // ^5  -> 1..5
            if (!to_number(tok.substr(1), to)) { error = "can't read \"" + tok + "\""; return false; }
            from = 1;
        } else if (tok.back() == '*') {                       // 5*  -> 5..end
            if (!to_number(tok.substr(0, tok.size() - 1), from)) { error = "can't read \"" + tok + "\""; return false; }
            to = total;
        } else if (tok.find('-') != std::string::npos) {      // 2-4 -> 2..4
            size_t dash = tok.find('-');
            if (!to_number(tok.substr(0, dash), from) || !to_number(tok.substr(dash + 1), to)) {
                error = "can't read \"" + tok + "\"";
                return false;
            }
        } else {                                              // 7   -> 7
            if (!to_number(tok, from)) { error = "can't read \"" + tok + "\""; return false; }
            to = from;
        }

        // Check the numbers actually exist in the playlist.
        if (from < 1 || from > total || to < 1 || to > total) {
            error = "\"" + tok + "\" is out of range 1-" + std::to_string(total);
            return false;
        }
        if (from > to) {
            error = "\"" + tok + "\" goes backwards";
            return false;
        }
        for (int i = from; i <= to; ++i) chosen.insert(i);
    }

    out.assign(chosen.begin(), chosen.end());
    return true;
}

std::string join_indices(const std::vector<int>& indices) {
    std::string result;
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) result += ',';
        result += std::to_string(indices[i]);
    }
    return result;
}
