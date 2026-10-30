// ytdlp.cpp — see ytdlp.hpp for the overview.
#include "ytdlp.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "util.hpp"

// =============================================================== Process ===

bool Process::start(const std::vector<std::string>& args) {
    // execvp wants a NULL-terminated array of char*. Build it *before* fork():
    // with several threads running, the child must not allocate memory.
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    // O_CLOEXEC: other yt-dlp programs started in parallel must not inherit
    // this pipe, or we'd never see it close.
    int pipe_fds[2];
    if (pipe2(pipe_fds, O_CLOEXEC) != 0) return false;

    pid_ = fork();
    if (pid_ < 0) {  // fork failed
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    }

    if (pid_ == 0) {
        // ---- child process: becomes yt-dlp ----
        // stdin  <- /dev/null (yt-dlp must never wait for keyboard input)
        // stdout -> pipe, stderr -> same pipe
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, STDIN_FILENO);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);

        execvp(argv[0], argv.data());

        // only reached if yt-dlp could not be started
        const char msg[] = "ERROR: could not run yt-dlp (is it installed and in PATH?)\n";
        ssize_t ignored = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
        (void)ignored;
        _exit(127);
    }

    // ---- parent process: keeps the reading end ----
    close(pipe_fds[1]);
    fd_ = pipe_fds[0];
    return true;
}

bool Process::read_line(std::string& line) {
    while (true) {
        // A full line already in the buffer? Hand it out.
        size_t nl = buffer_.find_first_of("\r\n");
        if (nl != std::string::npos) {
            line = buffer_.substr(0, nl);
            buffer_.erase(0, nl + 1);
            return true;
        }
        if (fd_ < 0) break;

        // Otherwise read more bytes from the pipe.
        char chunk[4096];
        ssize_t n = read(fd_, chunk, sizeof(chunk));
        if (n < 0 && errno == EINTR) continue;  // interrupted by a signal, retry
        if (n <= 0) {                           // program closed its output
            close(fd_);
            fd_ = -1;
            break;
        }
        buffer_.append(chunk, n);
    }
    // Last line without a trailing newline.
    if (!buffer_.empty()) {
        line = buffer_;
        buffer_.clear();
        return true;
    }
    return false;
}

int Process::wait() {
    if (pid_ <= 0) return -1;
    int status = 0;
    while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {}
    pid_ = -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;  // killed by a signal
}

void Process::stop() {
    if (pid_ > 0) kill(pid_, SIGTERM);
}

Process::~Process() {
    if (fd_ >= 0) close(fd_);
    if (pid_ > 0) {
        kill(pid_, SIGTERM);
        waitpid(pid_, nullptr, 0);
    }
}

// ================================================================= probe ===

namespace {

// Splits "a\tb\tc" on tabs, at most `max_parts` pieces (the last one keeps the rest).
std::vector<std::string> split_tabs(const std::string& line, size_t max_parts) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (parts.size() + 1 < max_parts) {
        size_t tab = line.find('\t', start);
        if (tab == std::string::npos) break;
        parts.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    parts.push_back(line.substr(start));
    return parts;
}

// Same as split_tabs but for '|' (used by our @@ markers).
std::vector<std::string> split_bars(const std::string& line, size_t max_parts) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (parts.size() + 1 < max_parts) {
        size_t bar = line.find('|', start);
        if (bar == std::string::npos) break;
        parts.push_back(line.substr(start, bar - start));
        start = bar + 1;
    }
    parts.push_back(line.substr(start));
    return parts;
}

// "12" -> 12, "NA" or junk -> -1
int to_int(const std::string& s) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return -1;
    return static_cast<int>(v);
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

// Options that every yt-dlp call gets (login cookies, no colors).
void add_common_args(const Options& opt, std::vector<std::string>& args) {
    args.push_back("--color");
    args.push_back("never");  // no escape codes in the text we parse
    if (!opt.cookies_browser.empty()) {
        args.push_back("--cookies-from-browser");
        args.push_back(opt.cookies_browser);
    }
}

}  // namespace

bool url_is_ambiguous(const std::string& url) {
    bool has_list = url.find("list=") != std::string::npos;
    bool has_video = url.find("v=") != std::string::npos ||
                     url.find("youtu.be/") != std::string::npos ||
                     url.find("/shorts/") != std::string::npos;
    // "playlist?list=..." is clearly a playlist, nothing to ask.
    bool is_playlist_page = url.find("/playlist?") != std::string::npos;
    return has_list && has_video && !is_playlist_page;
}

ProbeResult probe(const Options& opt, bool yes_playlist) {
    // --flat-playlist : only read the list, don't open every video page (fast)
    // --print         : one tab separated line per video, nothing is downloaded
    std::vector<std::string> args = {
        "yt-dlp",
        "--flat-playlist",
        "--ignore-errors",
        "--no-warnings",
        yes_playlist ? "--yes-playlist" : "--no-playlist",
        "--print", "%(playlist_index)s\t%(id)s\t%(url)s\t%(playlist_title)s\t%(title)s",
    };
    add_common_args(opt, args);
    args.push_back("--");  // everything after this is a URL, never an option
    args.push_back(opt.url);

    ProbeResult result;
    Process proc;
    if (!proc.start(args)) {
        result.error = "could not start yt-dlp";
        return result;
    }

    std::string line, last_error;
    bool any_with_index = false;
    while (proc.read_line(line)) {
        if (starts_with(line, "ERROR:")) { last_error = line; continue; }
        std::vector<std::string> parts = split_tabs(line, 5);
        if (parts.size() != 5) continue;  // not one of our lines

        VideoEntry e;
        e.index = to_int(parts[0]);
        e.id = parts[1];
        e.url = parts[2];
        e.title = parts[4];
        if (e.url == "NA" || e.url.empty())  // build the link ourselves if yt-dlp didn't give one
            e.url = "https://www.youtube.com/watch?v=" + e.id;
        if (e.index > 0) any_with_index = true;
        if (result.playlist_title.empty() && parts[3] != "NA") result.playlist_title = parts[3];
        result.entries.push_back(e);
    }
    int code = proc.wait();

    if (result.entries.empty()) {
        result.error = last_error.empty()
                           ? "yt-dlp found nothing at that URL (exit code " + std::to_string(code) + ")"
                           : last_error;
        return result;
    }

    // A single video has no playlist_index ("NA"). A playlist numbers every entry.
    result.is_playlist = any_with_index;
    if (!result.is_playlist) {
        result.entries.resize(1);
        result.entries[0].index = 1;
        result.entries[0].url = opt.url;  // the link the user gave us
    } else {
        // fill any missing numbers just in case
        for (size_t i = 0; i < result.entries.size(); ++i)
            if (result.entries[i].index <= 0) result.entries[i].index = static_cast<int>(i) + 1;
        if (result.playlist_title.empty()) result.playlist_title = "playlist";
    }
    result.ok = true;
    return result;
}

// ============================================================== download ===

std::vector<std::string> build_video_args(const Options& opt, const std::string& folder,
                                          const VideoEntry& video, bool is_playlist) {
    std::vector<std::string> a = {"yt-dlp"};

    // --- quality: best video + best audio, sorted by resolution, then bitrate ---
    // "lang" goes first so the original audio track wins over YouTube's AI-dubbed ones.
    std::string res = opt.max_height > 0 ? "res:" + std::to_string(opt.max_height) : "res";
    if (opt.audio_only) {
        a.insert(a.end(), {"-f", "ba/b",
                           "-x", "--audio-format", "best"});  // keep original codec, no re-encode
        a.insert(a.end(), {"-S", "lang,abr,asr"});
    } else {
        a.insert(a.end(), {"-f", "bv*+ba/b",
                           "-S", "lang," + res + ",fps,hdr:12,vcodec,br,asr",
                           "--merge-output-format", "mkv"});
    }

    // --- speed: download several pieces of this one video at the same time ---
    a.insert(a.end(), {"--concurrent-fragments", "4"});
    if (opt.aria2) {
        // aria2c opens many connections per file (faster), but yt-dlp then
        // can't report live progress, so the bar jumps from 0 to 100.
        a.insert(a.end(), {"--downloader", "aria2c",
                           "--downloader-args", "aria2c:-x8 -s8 -k1M"});
    }

    // --- file names without spaces: copy title into "fname", swap spaces for '_' ---
    // (the title stored *inside* the file keeps its normal spaces)
    a.insert(a.end(), {"--parse-metadata", "title:%(fname)s",
                       "--replace-in-metadata", "fname", "\\s+", "_"});

    a.insert(a.end(), {"-P", folder});
    if (is_playlist) {
        // We download each video by its own link, so yt-dlp doesn't know the
        // playlist number anymore: we write it into the name ourselves (003-...).
        char number[16];
        std::snprintf(number, sizeof(number), "%03d", video.index);
        a.insert(a.end(), {"-o", std::string(number) + "-%(fname)s.%(ext)s",
                           "--download-archive", folder + "/.archive.txt"});
    } else {
        a.insert(a.end(), {"-o", "%(fname)s.%(ext)s"});
    }
    a.push_back("--no-playlist");  // always exactly one video per yt-dlp

    // --- extras ---
    a.insert(a.end(), {"--embed-metadata", "--embed-chapters", "--embed-thumbnail",
                       "--no-overwrites", "--newline"});

    // --- machine readable markers that parse_line() understands ---
    // --no-quiet / --no-simulate: --print would otherwise silence & skip the download
    a.insert(a.end(), {
        "--no-quiet", "--no-simulate",
        "--progress-template",
        "download:@@P|%(info.playlist_index)s|%(progress._percent)s|"
        "%(progress._speed_str)s|%(progress._eta_str)s",
        "--print", "before_dl:@@S|%(playlist_index)s|%(title)s",
        "--print", "after_move:@@D|%(playlist_index)s|%(filepath)s",
    });

    add_common_args(opt, a);
    a.push_back("--");
    a.push_back(video.url);
    return a;
}

std::string shell_quote_command(const std::vector<std::string>& args) {
    std::string out;
    for (const std::string& arg : args) {
        if (!out.empty()) out += ' ';
        bool plain = !arg.empty() &&
                     arg.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                           "0123456789-_./:=,") == std::string::npos;
        if (plain) { out += arg; continue; }
        out += '\'';
        for (char c : arg) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += '\'';
    }
    return out;
}

// ================================================================ events ===

Event parse_line(const std::string& raw) {
    Event ev;
    std::string line = trim(raw);
    ev.text = line;

    if (starts_with(line, "@@P|")) {  // @@P|index|percent|speed|eta
        std::vector<std::string> p = split_bars(line, 5);
        if (p.size() == 5) {
            ev.type = EventType::Progress;
            ev.number = to_int(p[1]);
            ev.percent = std::atof(p[2].c_str());  // "NA" -> 0
            ev.speed = trim(p[3]);
            ev.eta = trim(p[4]);
            if (ev.speed.find("Unknown") != std::string::npos) ev.speed = "--";
            if (ev.eta.find("Unknown") != std::string::npos) ev.eta = "--:--";
        }
        return ev;
    }
    if (starts_with(line, "@@S|")) {  // @@S|index|title
        std::vector<std::string> p = split_bars(line, 3);
        ev.type = EventType::Start;
        if (p.size() == 3) { ev.number = to_int(p[1]); ev.text = p[2]; }
        return ev;
    }
    if (starts_with(line, "@@D|")) {  // @@D|index|filepath
        std::vector<std::string> p = split_bars(line, 3);
        ev.type = EventType::Done;
        if (p.size() == 3) { ev.number = to_int(p[1]); ev.text = p[2]; }
        return ev;
    }
    if (starts_with(line, "[download] Downloading item ")) {  // "... item 3 of 5"
        ev.type = EventType::ItemNumber;
        ev.number = to_int(line.substr(std::strlen("[download] Downloading item ")));
        return ev;
    }
    if (starts_with(line, "[download] Destination:")) {
        ev.type = EventType::Destination;
        return ev;
    }
    if (starts_with(line, "[Merger]")) {
        ev.type = EventType::Merging;
        return ev;
    }
    if (line.find("has already been recorded in the archive") != std::string::npos ||
        line.find("has already been downloaded") != std::string::npos) {
        ev.type = EventType::Skipped;
        return ev;
    }
    if (starts_with(line, "ERROR:")) {
        ev.type = EventType::Error;
        return ev;
    }
    if (starts_with(line, "WARNING:")) {
        ev.type = EventType::Warning;
        return ev;
    }
    return ev;  // Other
}

bool is_permanent_error(const std::string& error_line) {
    static const char* const permanent[] = {
        "Video unavailable", "Private video", "has been removed", "members-only",
        "Sign in to confirm your age", "not available in your country", "copyright",
        "could not start yt-dlp",
    };
    for (const char* text : permanent)
        if (error_line.find(text) != std::string::npos) return true;
    return false;
}

bool is_bot_check(const std::string& error_line) {
    return error_line.find("not a bot") != std::string::npos;
}
