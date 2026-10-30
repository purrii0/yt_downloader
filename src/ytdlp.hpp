// ytdlp.hpp — everything that talks to the yt-dlp program.
//
//  1. Process     : start yt-dlp, read its output line by line, stop it.
//  2. probe()     : ask yt-dlp "is this a video or a playlist, and what's in it?"
//  3. build_download_args() : the full yt-dlp command for the real download.
//  4. parse_line(): turn one line of yt-dlp output into an Event we understand.
#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

#include "options.hpp"

// ---------------------------------------------------------------- process ---

class Process {
public:
    // Starts `args[0]` with the given arguments. stdout and stderr are merged
    // into one pipe that we read from. No shell is used, so a URL with
    // weird characters can never run commands.
    bool start(const std::vector<std::string>& args);

    // Reads the next line (without '\n'). Returns false when the program
    // closed its output (it finished or died).
    bool read_line(std::string& line);

    // Waits for the program to exit and returns its exit code.
    int wait();

    // Asks the program to stop (SIGTERM).
    void stop();

    pid_t pid() const { return pid_; }

    ~Process();

private:
    pid_t pid_ = -1;
    int fd_ = -1;
    std::string buffer_;  // bytes read but not yet returned as a line
};

// ------------------------------------------------------------------ probe ---

struct VideoEntry {
    int index = 0;        // position in the playlist (1, 2, 3 ...)
    std::string id;       // YouTube video id
    std::string url;      // link to just this video
    std::string title;
};

struct ProbeResult {
    bool ok = false;
    bool is_playlist = false;
    std::string playlist_title;
    std::vector<VideoEntry> entries;  // for a single video: exactly one entry
    std::string error;                // filled when ok == false
};

// Lists what is behind `url` without downloading anything.
// `yes_playlist`: true -> treat watch?v=X&list=Y as the playlist,
//                 false -> just the single video.
ProbeResult probe(const Options& opt, bool yes_playlist);

// True for links like watch?v=X&list=Y, which can mean "this video"
// or "the whole playlist", so we have to ask.
bool url_is_ambiguous(const std::string& url);

// --------------------------------------------------------------- download ---

// Builds the yt-dlp command that downloads ONE video. Several of these run
// at the same time (see downloader.hpp).
//   folder      : where files go (~/videos, or ~/videos/Playlist_Name)
//   is_playlist : true -> name it "003-Title.mkv" and use the playlist's archive
std::vector<std::string> build_video_args(const Options& opt, const std::string& folder,
                                          const VideoEntry& video, bool is_playlist);

// Turns {"yt-dlp", "-f", "bv*+ba/b"} into a copy-pasteable shell line.
std::string shell_quote_command(const std::vector<std::string>& args);

// ----------------------------------------------------------------- events ---

enum class EventType {
    ItemNumber,  // "[download] Downloading item 3 of 5"   (number = 3)
    Start,       // our @@S marker: a video is about to download
    Destination, // a new stream (video or audio part) starts
    Progress,    // our @@P marker: percent / speed / eta
    Merging,     // ffmpeg is joining video + audio
    Done,        // our @@D marker: file finished and moved into place
    Skipped,     // already in the archive / already on disk
    Error,       // "ERROR: ..."
    Warning,     // "WARNING: ..."
    Other,       // anything else
};

struct Event {
    EventType type = EventType::Other;
    int number = -1;        // playlist index (or item number for ItemNumber); -1 = unknown
    double percent = 0;
    std::string speed;
    std::string eta;
    std::string text;       // title / file path / error message / raw line
};

Event parse_line(const std::string& line);

// True for errors that will never go away by trying again
// (private / removed / region-locked videos ...).
bool is_permanent_error(const std::string& error_line);

// True for YouTube's "Sign in to confirm you're not a bot" block. Retrying
// doesn't help; using your browser login (-c BROWSER) does.
bool is_bot_check(const std::string& error_line);
