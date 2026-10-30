// tui.cpp — the ncurses interface (ytpl -t).
//
// Screens:
//   1. URL input        (only if no URL was given)
//   2. video / playlist (only for watch?v=X&list=Y links)
//   3. pick videos      (only for playlists)
//   4. download view    (live progress for every video + yt-dlp log)
#include <ncurses.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <future>
#include <memory>

#include "app.hpp"
#include "downloader.hpp"
#include "select.hpp"
#include "util.hpp"

namespace {

// Color pair numbers
enum { C_GREEN = 1, C_YELLOW, C_RED, C_DIM };

// Writes `text` at (y, x), cut or padded to exactly `width` columns.
void put(int y, int x, const std::string& text, int width) {
    if (width <= 0) return;
    mvaddstr(y, x, fit(text, width, true).c_str());
}

// Same as put() but in a color (and optionally bold).
void put_color(int y, int x, const std::string& text, int width, int pair, bool bold = false) {
    attron(COLOR_PAIR(pair));
    if (bold) attron(A_BOLD);
    put(y, x, text, width);
    if (bold) attroff(A_BOLD);
    attroff(COLOR_PAIR(pair));
}

// The highlighted bar at the top of every screen.
void header(const std::string& text) {
    int w = COLS;
    attron(A_REVERSE | A_BOLD);
    put(0, 0, " ytpl  │  " + text, w);
    attroff(A_REVERSE | A_BOLD);
}

// The hint line at the bottom of every screen.
void footer(const std::string& text) {
    put_color(LINES - 1, 0, " " + text, COLS, C_DIM);
}

// Breaks a long line into pieces of at most `width` columns, at spaces when possible.
std::vector<std::string> wrap(const std::string& text, int width) {
    std::vector<std::string> out;
    std::string line, word;
    auto flush_word = [&] {
        if (line.empty()) line = word;
        else if (text_width(line + " " + word) <= width) line += " " + word;
        else { out.push_back(line); line = word; }
        // a single word longer than the screen: chop it
        while (text_width(line) > width) {
            std::string part = fit(line, width - 1);
            part.erase(part.size() - std::string("…").size());  // drop the "…"
            out.push_back(part);
            line.erase(0, part.size());
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') flush_word();
        else word += c;
    }
    flush_word();
    out.push_back(line);
    return out;
}

// Shows a message and waits for a key. Long lines are wrapped.
void message_box(const std::string& title, const std::vector<std::string>& lines) {
    timeout(-1);  // block until a key is pressed
    erase();
    header(title);
    int y = 2;
    for (const std::string& l : lines)
        for (const std::string& piece : wrap(l, COLS - 4)) {
            if (y >= LINES - 2) break;
            put(y++, 2, piece, COLS - 4);
        }
    footer("press any key");
    refresh();
    getch();
}

// A one-line text box. Returns the typed text; `cancelled` is set on Esc.
std::string input_line(int y, const std::string& label, std::string text, bool& cancelled) {
    cancelled = false;
    timeout(-1);
    curs_set(1);
    while (true) {
        int field = COLS - static_cast<int>(label.size()) - 3;
        // show the end of the text if it's longer than the field
        std::string shown = text.size() > static_cast<size_t>(field - 1)
                                ? text.substr(text.size() - (field - 1))
                                : text;
        put(y, 1, label + " " + shown, COLS - 2);
        move(y, 2 + static_cast<int>(label.size()) + static_cast<int>(shown.size()));
        refresh();

        int ch = getch();
        if (ch == '\n' || ch == KEY_ENTER) break;
        if (ch == 27) { cancelled = true; break; }  // Esc
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!text.empty()) text.pop_back();
        } else if (ch == 21) {  // Ctrl+U clears the line
            text.clear();
        } else if (ch >= 32 && ch < 256) {
            text += static_cast<char>(ch);
        }
    }
    curs_set(0);
    return trim(text);
}

// ------------------------------------------------------------ screen 1 ---

std::string url_screen() {
    erase();
    header("YouTube downloader");
    put(2, 2, "Paste a YouTube video or playlist link and press Enter.", COLS - 4);
    put_color(3, 2, "Esc to quit.", COLS - 4, C_DIM);
    bool cancelled = false;
    std::string url = input_line(5, "URL:", "", cancelled);
    return cancelled ? "" : url;
}

// ------------------------------------------------------------ screen 2 ---

// Returns 1 = playlist, 0 = video only, -1 = quit.
int video_or_playlist_screen() {
    int choice = 0;
    timeout(-1);
    while (true) {
        erase();
        header("Video or playlist?");
        put(2, 2, "This link points to a video inside a playlist. What should I download?", COLS - 4);
        const char* labels[2] = {"  Just this video  ", "  The whole playlist  "};
        for (int i = 0; i < 2; ++i) {
            if (i == choice) attron(A_REVERSE);
            put(4 + i, 4, labels[i], 24);
            if (i == choice) attroff(A_REVERSE);
        }
        footer("↑/↓ choose   Enter confirm   v video   p playlist   q quit");
        refresh();

        int ch = getch();
        if (ch == KEY_UP || ch == 'k' || ch == KEY_DOWN || ch == 'j') choice = 1 - choice;
        else if (ch == 'v') return 0;
        else if (ch == 'p') return 1;
        else if (ch == '\n' || ch == KEY_ENTER) return choice;
        else if (ch == 'q' || ch == 27) return -1;
    }
}

// --------------------------------------------------------- probe spinner ---

// Runs probe() in the background and shows a spinner meanwhile.
ProbeResult probe_with_spinner(const Options& opt, bool yes_playlist) {
    auto job = std::async(std::launch::async, [&] { return probe(opt, yes_playlist); });
    const char* frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    int frame = 0;
    while (job.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
        erase();
        header("Looking up link");
        put(2, 2, std::string(frames[frame++ % 10]) + " Asking YouTube what's at:", COLS - 4);
        put_color(3, 4, opt.url, COLS - 6, C_DIM);
        refresh();
    }
    return job.get();
}

// ------------------------------------------------------------ screen 3 ---

// Lets the user tick videos. Returns false if they quit.
bool select_screen(const Options& opt, const ProbeResult& info, std::vector<int>& selected) {
    int total = static_cast<int>(info.entries.size());
    std::vector<bool> checked(total, true);  // everything ticked to start with
    int cursor = 0, top = 0;
    std::string note;  // error / info line

    // -s on the command line pre-ticks the boxes
    if (!opt.select.empty()) {
        std::vector<int> pre;
        if (parse_selection(opt.select, total, pre, note)) {
            checked.assign(total, false);
            for (int n : pre) checked[n - 1] = true;
        } else {
            note = "bad -s value: " + note;
        }
    }

    timeout(-1);
    while (true) {
        int list_h = LINES - 5;  // rows for the list
        if (list_h < 1) list_h = 1;
        if (cursor < top) top = cursor;
        if (cursor >= top + list_h) top = cursor - list_h + 1;

        int count = 0;
        for (bool c : checked) count += c;

        erase();
        header(info.playlist_title + "  (" + std::to_string(total) + " videos)");
        put_color(1, 1, "space tick   a all/none   / type a selection (all  1,3,5  ^5  5*  2-4)",
                  COLS - 2, C_DIM);

        int num_w = static_cast<int>(std::to_string(total).size());
        for (int row = 0; row < list_h && top + row < total; ++row) {
            int i = top + row;
            std::string num = std::to_string(info.entries[i].index);
            num.insert(0, num_w - num.size(), ' ');
            std::string line = std::string(checked[i] ? " [x] " : " [ ] ") + num + "  " +
                               info.entries[i].title;
            if (i == cursor) attron(A_REVERSE);
            else if (checked[i]) attron(COLOR_PAIR(C_GREEN));
            put(2 + row, 0, line, COLS);
            if (i == cursor) attroff(A_REVERSE);
            else if (checked[i]) attroff(COLOR_PAIR(C_GREEN));
        }

        std::string status = std::to_string(count) + " of " + std::to_string(total) + " selected";
        if (!note.empty()) put_color(LINES - 2, 1, note, COLS - 2, C_RED);
        else put_color(LINES - 2, 1, status, COLS - 2, C_YELLOW, true);
        footer("↑/↓ move   Enter download   q quit");
        refresh();

        int ch = getch();
        note.clear();
        switch (ch) {
            case KEY_UP: case 'k': if (cursor > 0) --cursor; break;
            case KEY_DOWN: case 'j': if (cursor < total - 1) ++cursor; break;
            case KEY_PPAGE: cursor = std::max(0, cursor - list_h); break;
            case KEY_NPAGE: cursor = std::min(total - 1, cursor + list_h); break;
            case KEY_HOME: case 'g': cursor = 0; break;
            case KEY_END: case 'G': cursor = total - 1; break;
            case ' ':
                checked[cursor] = !checked[cursor];
                if (cursor < total - 1) ++cursor;
                break;
            case 'a': {
                bool all_on = count == total;
                checked.assign(total, !all_on);
                break;
            }
            case '/': {
                bool cancelled = false;
                std::string expr = input_line(LINES - 2, "Select:", "", cancelled);
                if (cancelled) break;
                std::vector<int> picked;
                std::string err;
                if (parse_selection(expr, total, picked, err)) {
                    checked.assign(total, false);
                    for (int n : picked) checked[n - 1] = true;
                } else {
                    note = "✘ " + err;
                }
                break;
            }
            case '\n': case KEY_ENTER:
                if (count == 0) { note = "nothing selected"; break; }
                selected.clear();
                for (int i = 0; i < total; ++i)
                    if (checked[i]) selected.push_back(info.entries[i].index);
                return true;
            case 'q': case 27:
                return false;
            default:
                break;
        }
    }
}

// ------------------------------------------------------------ screen 4 ---

// One row in the download list, e.g. " ✔ 003  Title ....   done"
void draw_item(int y, const Options& opt, const Item& it) {
    bool active = it.status == Status::Downloading || it.status == Status::Merging ||
                  it.status == Status::Retrying;
    const char* icon = " ";
    int pair = 0;
    std::string right;
    switch (it.status) {
        case Status::Queued:      icon = "·"; pair = C_DIM; right = "queued"; break;
        case Status::Downloading: {
            icon = "↓"; pair = C_YELLOW;
            char pct[16];
            std::snprintf(pct, sizeof(pct), "%5.1f%%", it.percent);
            right = stream_label(opt, it) + " " + make_bar(it.percent, 16) + " " + pct + " " +
                    fit(it.speed, 10, true) + " " + it.eta;
            break;
        }
        case Status::Merging: icon = "⚙"; pair = C_YELLOW; right = "merging video + audio…"; break;
        case Status::Retrying:
            icon = "↻"; pair = C_YELLOW;
            right = "retry " + std::to_string(it.attempt) + "/" + std::to_string(opt.retries) +
                    " in " + std::to_string(it.retry_delay) + "s";
            break;
        case Status::Done:    icon = "✔"; pair = C_GREEN;  right = "done"; break;
        case Status::Skipped: icon = "↷"; pair = C_DIM;    right = "already downloaded"; break;
        case Status::Failed:  icon = "✘"; pair = C_RED;    right = "failed"; break;
    }

    int right_w = 50;
    int title_w = COLS - right_w - 9;
    if (title_w < 10) { title_w = COLS - 9; right_w = 0; }  // tiny terminal: hide the right side

    char num[8];
    std::snprintf(num, sizeof(num), "%03d", it.index);
    if (active) attron(A_BOLD);
    put_color(y, 1, icon, 2, pair, true);
    put(y, 3, num, 4);
    put(y, 8, it.title, title_w);
    if (right_w > 0) put_color(y, COLS - right_w, right, right_w - 1, pair);
    if (active) attroff(A_BOLD);
}

void draw_download_screen(const Options& opt, const std::string& title, const std::string& folder,
                          const Tracker& t, const std::deque<std::string>& log,
                          int jobs, const std::string& footer_text) {
    erase();
    header(title + "  →  " + folder + "/");

    // overall progress
    int total = static_cast<int>(t.items().size());
    int fin = t.finished();
    double overall = 0;
    for (const Item& it : t.items()) {
        if (it.status == Status::Done || it.status == Status::Skipped || it.status == Status::Failed)
            overall += 100;
        else if (it.status == Status::Merging) overall += 99;
        else if (it.status == Status::Downloading)
            overall += opt.audio_only ? it.percent
                                      : (it.stream >= 2 ? 50 + it.percent / 2 : it.percent / 2);
    }
    overall /= (total > 0 ? total : 1);
    std::vector<int> active = t.active();

    int bar_w = COLS - 50;
    if (bar_w < 10) bar_w = 10;
    put(2, 1, "Overall", 8);
    put_color(2, 9, make_bar(overall, bar_w), bar_w, C_GREEN);
    char counts[128];
    std::snprintf(counts, sizeof(counts), " %d/%d   ✔ %d  ↷ %d  ✘ %d   %zu/%d running", fin, total,
                  t.count(Status::Done), t.count(Status::Skipped), t.count(Status::Failed),
                  active.size(), jobs);
    put(2, 10 + bar_w, counts, COLS - 11 - bar_w);

    // list of videos, scrolled so the first running one is visible
    const int log_h = 6;
    int list_top = 4;
    int list_h = LINES - list_top - log_h - 2;
    if (list_h < 1) list_h = 1;
    int first = 0;
    int focus = active.empty() ? t.finished() : active.front();
    if (focus >= list_h / 3) first = focus - list_h / 3;
    if (first > total - list_h) first = std::max(0, total - list_h);
    for (int row = 0; row < list_h && first + row < total; ++row)
        draw_item(list_top + row, opt, t.items()[first + row]);

    // yt-dlp log at the bottom
    int log_y = LINES - log_h - 1;
    attron(COLOR_PAIR(C_DIM));
    mvhline(log_y - 1, 0, ACS_HLINE, COLS);
    attroff(COLOR_PAIR(C_DIM));
    int start = static_cast<int>(log.size()) > log_h ? static_cast<int>(log.size()) - log_h : 0;
    for (int i = start, y = log_y; i < static_cast<int>(log.size()); ++i, ++y) {
        const std::string& l = log[i];
        int pair = l.find("ERROR") != std::string::npos ? C_RED
                 : (l.find("WARNING") != std::string::npos ? C_YELLOW : C_DIM);
        put_color(y, 1, l, COLS - 2, pair);
    }

    footer(footer_text);
    refresh();
}

int download_screen(const Options& opt, const ProbeResult& info, const std::string& folder,
                    const std::vector<VideoEntry>& videos) {
    Tracker tracker(make_items(videos));
    std::deque<std::string> log;

    // Worker threads run the yt-dlp programs and only read their output.
    // All drawing happens here on the main thread (ncurses isn't thread safe).
    // unique_ptr so a retry can replace it with a fresh Downloader
    auto downloader = std::make_unique<Downloader>(opt, folder, videos, info.is_playlist);
    downloader->start();

    std::string title = info.is_playlist ? info.playlist_title : info.entries[0].title;
    bool running = true, asking_quit = false, stopping = false;
    timeout(100);  // getch() waits at most 100 ms, so the screen keeps updating
    std::vector<Update> updates;

    while (true) {
        // 1. take what the workers produced
        if (running) {
            updates.clear();
            running = downloader->poll(updates, 0);
            for (const Update& u : updates) {
                char tag[16];
                std::snprintf(tag, sizeof(tag), "[%03d] ", tracker.items()[u.pos].index);
                if (u.retry_attempt > 0) {
                    tracker.note_retry(u.pos, u.retry_attempt, u.retry_delay, u.line);
                    log.push_back(tag + std::string("retry ") + std::to_string(u.retry_attempt) + "/" +
                                  std::to_string(opt.retries) + " in " +
                                  std::to_string(u.retry_delay) + "s: " + u.line);
                    continue;
                }
                if (u.exited) {
                    tracker.finish_item(u.pos, u.exit_code);
                    continue;
                }
                Event ev = parse_line(u.line);
                tracker.apply(u.pos, ev);
                // keep interesting lines for the log (skip the noisy progress markers)
                if (ev.type != EventType::Progress && ev.type != EventType::Start &&
                    !ev.text.empty()) {
                    log.push_back(tag + (ev.type == EventType::Done ? "saved " + ev.text : ev.text));
                    if (log.size() > 200) log.pop_front();
                }
            }
            if (!running) {
                tracker.finish();
                asking_quit = false;
                std::string hint = bot_check_hint(opt, tracker);
                if (!hint.empty()) log.push_back("💡 " + hint);
            }
        }

        // 2. draw
        std::string foot;
        if (asking_quit) foot = "Stop downloading? Unfinished files resume next time.  y = stop   n = keep going";
        else if (stopping && running) foot = "Stopping…";
        else if (running) foot = "q stop & quit";
        else {
            size_t again = tracker.retryable().size();
            foot = "Finished: " + std::to_string(tracker.count(Status::Done)) + " downloaded, " +
                   std::to_string(tracker.count(Status::Skipped)) + " skipped, " +
                   std::to_string(tracker.count(Status::Failed)) + " failed.   " +
                   (again > 0 ? "r retry " + std::to_string(again) + " failed   " : "") + "q quit";
        }
        draw_download_screen(opt, title, folder, tracker, log, downloader->jobs(), foot);

        // 3. keys
        int ch = getch();
        if (!running) {
            if (ch == 'q' || ch == 27 || ch == '\n') break;
            std::vector<int> again = tracker.retryable();
            if ((ch == 'r' || ch == 'R') && !again.empty()) {
                // start a new round with just the failed / unfinished videos
                for (int pos : again) tracker.reset_item(pos);
                log.push_back("retrying " + std::to_string(again.size()) + " video(s)…");
                downloader = std::make_unique<Downloader>(opt, folder, videos, info.is_playlist, again);
                downloader->start();
                running = true;
                stopping = false;
            }
            continue;
        }
        if (asking_quit) {
            if (ch == 'y' || ch == 'Y') { downloader->stop(); stopping = true; asking_quit = false; }
            else if (ch == 'n' || ch == 'N' || ch == 27) asking_quit = false;
        } else if ((ch == 'q' || ch == 27) && !stopping) {
            asking_quit = true;
        }
    }

    bool all_good = tracker.retryable().empty();
    return all_good ? 0 : 1;
}

// Makes sure endwin() runs however we leave run_tui().
struct CursesSession {
    CursesSession() {
        set_escdelay(25);  // Esc reacts quickly
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(0);
        if (has_colors()) {
            start_color();
            use_default_colors();
            init_pair(C_GREEN, COLOR_GREEN, -1);
            init_pair(C_YELLOW, COLOR_YELLOW, -1);
            init_pair(C_RED, COLOR_RED, -1);
            init_pair(C_DIM, COLOR_BLUE, -1);
        }
    }
    ~CursesSession() { endwin(); }
};

}  // namespace

int run_tui(Options opt) {
    CursesSession session;

    // 1. URL
    if (opt.url.empty()) opt.url = url_screen();
    if (opt.url.empty()) return 0;

    // 2. video or playlist?
    bool yes_playlist = true;
    if (opt.playlist_choice == PlaylistChoice::VideoOnly) yes_playlist = false;
    else if (opt.playlist_choice == PlaylistChoice::Ask && url_is_ambiguous(opt.url)) {
        int c = video_or_playlist_screen();
        if (c < 0) return 0;
        yes_playlist = (c == 1);
    }

    // 3. what's there?
    ProbeResult info = probe_with_spinner(opt, yes_playlist);
    if (!info.ok) {
        message_box("Error", {"Could not read that link:", "", info.error});
        return 1;
    }

    // 4. pick videos
    std::vector<int> selected = {1};
    if (info.is_playlist && !select_screen(opt, info, selected)) return 0;

    // 5. download
    std::string folder = download_folder(opt, info);
    std::vector<VideoEntry> videos = pick_entries(info, selected);
    if (opt.dry_run) {
        int jobs = std::max(1, std::min(opt.jobs, static_cast<int>(videos.size())));
        message_box("Dry run", {"Would save " + std::to_string(videos.size()) + " video(s) to " +
                                    folder + "/, " + std::to_string(jobs) + " at a time.", "",
                                "Each video gets its own yt-dlp, e.g. the first one:",
                                shell_quote_command(build_video_args(opt, folder, videos[0],
                                                                     info.is_playlist))});
        return 0;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    return download_screen(opt, info, folder, videos);
}
