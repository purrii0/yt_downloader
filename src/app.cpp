// app.cpp — helpers shared by cli.cpp and tui.cpp (see app.hpp).
#include "app.hpp"

#include "util.hpp"

std::string download_folder(const Options& opt, const ProbeResult& info) {
    std::string base = expand_home(opt.outdir.empty() ? "~/videos" : opt.outdir);
    while (base.size() > 1 && base.back() == '/') base.pop_back();
    if (!info.is_playlist) return base;
    return base + "/" + clean_name(info.playlist_title);
}

std::vector<VideoEntry> pick_entries(const ProbeResult& info, const std::vector<int>& selected) {
    std::vector<VideoEntry> out;
    for (int number : selected)
        for (const VideoEntry& e : info.entries)
            if (e.index == number) { out.push_back(e); break; }
    return out;
}

std::vector<Item> make_items(const std::vector<VideoEntry>& videos) {
    std::vector<Item> items;
    for (const VideoEntry& e : videos) {
        Item it;
        it.index = e.index;
        it.title = e.title;
        items.push_back(it);
    }
    return items;
}

std::string make_bar(double percent, int width) {
    if (width <= 0) return "";
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int filled = static_cast<int>(percent / 100.0 * width + 0.5);
    std::string bar;
    for (int i = 0; i < width; ++i) bar += (i < filled) ? "█" : "░";
    return bar;
}

std::string stream_label(const Options& opt, const Item& it) {
    if (opt.audio_only) return "audio";
    if (it.stream >= 2) return "audio";  // bv*+ba downloads video first, audio second
    return "video";
}

std::string bot_check_hint(const Options& opt, const Tracker& t) {
    bool hit = false;
    for (const Item& it : t.items())
        if (it.status == Status::Failed && is_bot_check(it.error)) hit = true;
    if (!hit) return "";

    if (opt.cookies_browser.empty())
        return "YouTube wants you signed in (\"confirm you're not a bot\"). Use your browser's "
               "login: add -c chromium (or put  cookies = chromium  in ~/.config/ytpl/config).";
    return "YouTube still asks for the bot check even with your " + opt.cookies_browser +
           " login. Make sure you're signed in to YouTube in " + opt.cookies_browser +
           ", and try fewer at once with -j 2.";
}
