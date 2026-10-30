// tracker.hpp — remembers the state of every video while yt-dlp runs.
// Both the CLI and the TUI feed yt-dlp's output lines into a Tracker and
// then just draw what's inside it.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "ytdlp.hpp"

enum class Status { Queued, Downloading, Merging, Retrying, Done, Skipped, Failed };

const char* status_name(Status s);

struct Item {
    int index = 0;            // playlist number
    std::string title;
    Status status = Status::Queued;
    double percent = 0;       // of the current stream
    int stream = 0;           // 1 = video stream, 2 = audio stream ...
    std::string speed, eta;
    std::string file;         // final file path when done
    std::string error;
    int attempt = 0;          // how many automatic retries so far
    int retry_delay = 0;      // seconds we wait before the next try
};

class Tracker {
public:
    explicit Tracker(std::vector<Item> items) : items_(std::move(items)) {}

    // Feeds one parsed yt-dlp line that came from the yt-dlp handling item `pos`.
    // Returns true if the item changed.
    bool apply(int pos, const Event& ev);

    // The yt-dlp for item `pos` exited with `exit_code`. If it never said
    // "done" / "skipped" / "error", decide from the exit code.
    void finish_item(int pos, int exit_code);

    // Called at the very end: anything still "in progress" failed.
    void finish();

    // Positions worth another try: failed ones, plus ones never started
    // (e.g. still queued when you pressed stop).
    std::vector<int> retryable() const;

    // An attempt failed and the worker will try again after `delay` seconds.
    void note_retry(int pos, int attempt, int delay, const std::string& error);

    // Puts an item back to "queued" with a clean slate before a retry.
    void reset_item(int pos);

    const std::vector<Item>& items() const { return items_; }
    std::vector<int> active() const;  // positions downloading / merging / retrying now
    int count(Status s) const;
    int finished() const;  // done + skipped + failed

private:
    std::vector<Item> items_;
};
