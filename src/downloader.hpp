// downloader.hpp — downloads several videos at the same time.
//
// If a video fails, its worker tries it again up to opt.retries times
// (waiting 3s, 6s, 9s ... in between), unless the error is permanent.
//
// It starts `jobs` worker threads. Each worker keeps taking the next video
// from the list and runs one yt-dlp for it, so up to `jobs` yt-dlp
// programs run in parallel. The workers only *read* output; they hand every
// line to the main thread through poll(), which does the parsing and drawing.
#pragma once

#include <sys/types.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "options.hpp"
#include "ytdlp.hpp"

// One message from a worker to the main thread.
struct Update {
    int pos = -1;           // which video (position in the list given to Downloader)
    std::string line;       // one line of yt-dlp output
    bool exited = false;    // true: this video is completely finished (line is empty)
    int exit_code = 0;      // valid when exited == true
    int retry_attempt = 0;  // > 0: this attempt failed, retry number N comes next
    int retry_delay = 0;    // ...after waiting this many seconds (line = the error)
};

class Downloader {
public:
    // `todo`: positions in `videos` to download. Empty = all of them.
    // (A retry passes just the positions that failed.)
    Downloader(const Options& opt, const std::string& folder,
               std::vector<VideoEntry> videos, bool is_playlist,
               std::vector<int> todo = {});
    ~Downloader();  // stops and waits for everything

    void start();

    // Moves new updates into `out`, waiting up to `wait_ms` for something to
    // arrive. Returns false once all videos are finished and nothing is left.
    bool poll(std::vector<Update>& out, int wait_ms);

    // Stops every running yt-dlp and doesn't start new ones.
    void stop();

    int jobs() const { return jobs_; }

private:
    void worker();
    // Runs yt-dlp once for video `pos`. Returns its exit code (-1 = stopped).
    int run_once(int pos, std::string& last_error);
    void push(Update u);

    Options opt_;
    std::string folder_;
    std::vector<VideoEntry> videos_;
    bool is_playlist_;
    int jobs_;

    std::mutex lock_;                 // protects everything below
    std::condition_variable wake_;    // signals "new update in the queue"
    std::condition_variable stop_cv_; // signals "stop() was called"
    std::deque<Update> queue_;
    std::vector<int> todo_;           // positions still to download, in order
    size_t next_ = 0;                 // next entry of todo_ a worker should take
    int workers_left_ = 0;            // workers still running
    bool stopping_ = false;
    std::vector<pid_t> running_;      // pids of the yt-dlp programs running now
    std::vector<std::thread> threads_;
};
