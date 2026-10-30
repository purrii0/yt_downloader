// cli.cpp — the plain terminal mode.
//
// Flow: check URL -> (ask video or playlist?) -> list videos -> ask which ones
//       -> run several yt-dlp at once -> show live progress bars -> print a summary.
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>

#include "app.hpp"
#include "downloader.hpp"
#include "select.hpp"
#include "util.hpp"

namespace {

volatile sig_atomic_t g_interrupted = 0;
void on_sigint(int) { g_interrupted = 1; }

// Reads one line from the keyboard. Returns false on Ctrl+D.
bool ask(const std::string& question, std::string& answer) {
    std::cout << question << std::flush;
    if (!std::getline(std::cin, answer)) return false;
    answer = trim(answer);
    return true;
}

// watch?v=X&list=Y -> does the user want the one video or the whole list?
bool ask_yes_playlist(const Options& opt) {
    if (opt.playlist_choice == PlaylistChoice::VideoOnly) return false;
    if (opt.playlist_choice == PlaylistChoice::Playlist) return true;
    if (!url_is_ambiguous(opt.url)) return true;

    while (true) {
        std::string a;
        if (!ask("This video is part of a playlist. Download [v]ideo only or [p]laylist? ", a))
            return false;
        if (a == "v" || a == "V") return false;
        if (a == "p" || a == "P") return true;
    }
}

// Shows the numbered list and asks which videos to download.
bool choose_videos(const Options& opt, const ProbeResult& info, std::vector<int>& selected) {
    int total = static_cast<int>(info.entries.size());
    int width = terminal_width();
    int num_width = static_cast<int>(std::to_string(total).size());

    std::cout << "\nPlaylist: " << info.playlist_title << "  (" << total << " videos)\n\n";
    for (const VideoEntry& e : info.entries) {
        std::string num = std::to_string(e.index);
        num.insert(0, num_width - num.size(), ' ');
        std::cout << "  " << num << ". " << fit(e.title, width - num_width - 5) << "\n";
    }
    std::cout << "\n";

    // Selection given on the command line with -s: no question asked.
    if (!opt.select.empty()) {
        std::string err;
        if (!parse_selection(opt.select, total, selected, err)) {
            std::cerr << "Bad --select value: " << err << "\n";
            return false;
        }
        return true;
    }

    while (true) {
        std::string answer, err;
        if (!ask("Which videos? (all, 1,3,5, ^5, 5*, 2-4) [all]: ", answer)) return false;
        if (parse_selection(answer, total, selected, err)) return true;
        std::cout << "  ✘ " << err << " — try again.\n";
    }
}

// One live line for a video that's downloading, e.g.
// "↓ 002 Title…   video ████████░░  78.0%  6.1MiB/s   ETA 00:04"
std::string live_line(const Options& opt, const Item& it, int width) {
    char num[16];
    std::snprintf(num, sizeof(num), "%03d ", it.index);

    std::string icon, right;
    if (it.status == Status::Retrying) {
        icon = "↻ ";
        std::string why = it.error;
        if (why.rfind("ERROR: ", 0) == 0) why.erase(0, 7);
        right = "  retry " + std::to_string(it.attempt) + "/" + std::to_string(opt.retries) +
                " in " + std::to_string(it.retry_delay) + "s  (" + why + ")";
    } else if (it.status == Status::Merging) {
        icon = "⚙ ";
        right = "  merging video + audio…";
    } else {
        icon = "↓ ";
        char pct[16];
        std::snprintf(pct, sizeof(pct), "%5.1f%%", it.percent);
        right = "  " + stream_label(opt, it) + " " + make_bar(it.percent, 16) + " " + pct +
                "  " + fit(it.speed, 11, true) + " ETA " + it.eta;
    }
    right = fit(right, 56, true);  // fixed width, so the titles line up

    int title_cols = width - text_width(icon) - text_width(num) - text_width(right);
    if (title_cols < 6) title_cols = 6;
    return fit(icon + num + fit(it.title, title_cols, true) + right, width);
}

// The block of live lines at the bottom: one per running video + overall.
// It is erased and drawn again on every refresh.
class LiveBlock {
public:
    // Moves the cursor back up over the old block and clears it.
    void erase() {
        if (lines_ > 0) std::cout << "\033[" << lines_ << "A\r\033[J";
        lines_ = 0;
    }

    void draw(const Options& opt, const Tracker& t, int jobs) {
        int width = terminal_width() - 1;  // never touch the last column (avoids line wraps)
        std::vector<int> active = t.active();
        for (int pos : active) {
            std::cout << live_line(opt, t.items()[pos], width) << "\n";
            ++lines_;
        }

        // overall: finished videos count fully, running ones by their progress
        int total = static_cast<int>(t.items().size());
        double sum = 0;
        for (const Item& it : t.items()) {
            if (it.status == Status::Done || it.status == Status::Skipped || it.status == Status::Failed)
                sum += 100;
            else if (it.status == Status::Merging) sum += 99;
            else if (it.status == Status::Downloading)
                sum += opt.audio_only ? it.percent : (it.stream >= 2 ? 50 + it.percent / 2 : it.percent / 2);
        }
        char info[128];
        std::snprintf(info, sizeof(info), "  %d/%d done   ✔%d ↷%d ✘%d   %zu running (max %d)",
                      t.finished(), total, t.count(Status::Done), t.count(Status::Skipped),
                      t.count(Status::Failed), active.size(), jobs);
        std::cout << fit("Overall " + make_bar(sum / (total ? total : 1), 20) + info, width) << "\n";
        ++lines_;
        std::cout << std::flush;
    }

private:
    int lines_ = 0;  // how many lines the block took last time
};

// Prints a permanent line when a video finished / was skipped / failed.
// `number` is how many videos have been reported so far, including this one.
void announce(const Tracker& t, int pos, int number) {
    const Item& it = t.items()[pos];
    int total = static_cast<int>(t.items().size());
    std::string counter = "  (" + std::to_string(number) + "/" + std::to_string(total) + ")";
    int room = terminal_width() - 4 - static_cast<int>(counter.size());

    switch (it.status) {
        case Status::Done: {
            std::string name = std::filesystem::path(it.file).filename().string();
            std::string tries = it.attempt > 0 ? "  (after " + std::to_string(it.attempt) + " retr" +
                                                     (it.attempt == 1 ? "y)" : "ies)")
                                               : "";
            std::cout << "✔ " << fit((name.empty() ? it.title : name) + tries, room) << counter << "\n";
            break;
        }
        case Status::Skipped:
            std::cout << "↷ " << fit(it.title + " (already downloaded)", room) << counter << "\n";
            break;
        case Status::Failed:
            std::cout << "✘ " << fit(it.title, room) << counter << "\n";
            if (!it.error.empty()) std::cout << "    " << it.error << "\n";
            break;
        default:
            break;
    }
    std::cout << std::flush;
}

// Downloads the videos at positions `todo` (all when empty) with up to
// opt.jobs running at once, and keeps the live display updated.
void download_round(const Options& opt, const std::string& folder,
                    const std::vector<VideoEntry>& videos, bool is_playlist,
                    Tracker& tracker, const std::vector<int>& todo) {
    Downloader downloader(opt, folder, videos, is_playlist, todo);
    downloader.start();

    std::vector<bool> announced(videos.size(), false);
    int reported = tracker.finished();  // videos already reported (from earlier rounds)
    LiveBlock block;
    auto last_draw = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    bool stop_sent = false;
    std::vector<Update> updates;

    while (true) {
        updates.clear();
        bool more = downloader.poll(updates, 100);
        if (g_interrupted && !stop_sent) { downloader.stop(); stop_sent = true; }

        // apply everything that arrived, remember which videos just finished
        std::vector<int> newly_finished;
        for (const Update& u : updates) {
            if (u.retry_attempt > 0) {
                tracker.note_retry(u.pos, u.retry_attempt, u.retry_delay, u.line);
            } else if (u.exited) {
                // only now is the result final (a failure may still be retried before this)
                tracker.finish_item(u.pos, u.exit_code);
                if (!announced[u.pos]) {
                    announced[u.pos] = true;
                    newly_finished.push_back(u.pos);
                }
            } else {
                tracker.apply(u.pos, parse_line(u.line));
            }
        }

        // redraw: at most 10 times a second, or right away when a video finished
        auto now = std::chrono::steady_clock::now();
        bool time_to_draw = now - last_draw >= std::chrono::milliseconds(100);
        if (!newly_finished.empty() || (time_to_draw && !opt.quiet) || !more) {
            block.erase();
            for (int pos : newly_finished) announce(tracker, pos, ++reported);  // permanent lines
            if (!opt.quiet && more) block.draw(opt, tracker, downloader.jobs());
            last_draw = now;
        }
        if (!more) break;
    }
    tracker.finish();
}

void print_summary(const Tracker& t, const std::string& folder) {
    std::cout << "\n"
              << (g_interrupted ? "Interrupted. " : "Finished. ")
              << t.count(Status::Done) << " downloaded, "
              << t.count(Status::Skipped) << " skipped, "
              << t.count(Status::Failed) << " failed"
              << "  →  " << folder << "/\n";
}

}  // namespace

int run_cli(Options opt) {
    if (opt.url.empty()) {
        std::cerr << "No URL given. Usage: ytpl [options] <url>   (ytpl -h for help)\n";
        return 2;
    }

    // ---- 1. figure out what the URL is ----
    bool yes_playlist = ask_yes_playlist(opt);
    std::cout << "Looking up " << opt.url << " …\n";
    ProbeResult info = probe(opt, yes_playlist);
    if (!info.ok) {
        std::cerr << "✘ " << info.error << "\n";
        return 1;
    }

    // ---- 2. pick videos (playlists only) ----
    std::vector<int> selected = {1};
    if (info.is_playlist) {
        if (!choose_videos(opt, info, selected)) return 1;
    } else {
        std::cout << "Video: " << info.entries[0].title << "\n";
    }

    // ---- 3. prepare ----
    std::string folder = download_folder(opt, info);
    std::vector<VideoEntry> videos = pick_entries(info, selected);
    int jobs = std::max(1, std::min(opt.jobs, static_cast<int>(videos.size())));

    std::cout << "Saving " << videos.size() << " video(s) to " << folder << "/  ("
              << jobs << " at a time)\n";
    if (opt.dry_run) {
        std::cout << "\n[dry run] Each video gets its own yt-dlp, e.g. the first one:\n"
                  << shell_quote_command(build_video_args(opt, folder, videos[0], info.is_playlist))
                  << "\n";
        return 0;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    std::cout << "\n";

    // ---- 4. download in parallel, then offer to retry what failed ----
    signal(SIGINT, on_sigint);  // Ctrl+C: the yt-dlp programs get it too; we stop and summarize
    Tracker tracker(make_items(videos));
    std::vector<int> todo;  // empty = everything
    while (true) {
        download_round(opt, folder, videos, info.is_playlist, tracker, todo);
        print_summary(tracker, folder);
        std::string hint = bot_check_hint(opt, tracker);
        if (!hint.empty()) std::cout << "\n💡 " << hint << "\n";

        std::vector<int> again = tracker.retryable();
        if (again.empty() || g_interrupted || !isatty(STDIN_FILENO)) break;
        std::string answer;
        if (!ask("Retry " + std::to_string(again.size()) + " failed video(s)? [Y/n] ", answer)) break;
        if (answer == "n" || answer == "N") break;

        for (int pos : again) tracker.reset_item(pos);
        todo = again;
        std::cout << "\n";
    }

    if (g_interrupted || !tracker.retryable().empty())
        std::cout << "Run the same command again to resume (finished videos are skipped).\n";
    return tracker.retryable().empty() && !g_interrupted ? 0 : 1;
}
