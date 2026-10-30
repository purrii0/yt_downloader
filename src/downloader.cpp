// downloader.cpp — see downloader.hpp.
#include "downloader.hpp"

#include <signal.h>

#include <algorithm>
#include <chrono>

Downloader::Downloader(const Options& opt, const std::string& folder,
                       std::vector<VideoEntry> videos, bool is_playlist,
                       std::vector<int> todo)
    : opt_(opt), folder_(folder), videos_(std::move(videos)), is_playlist_(is_playlist),
      todo_(std::move(todo)) {
    if (todo_.empty())
        for (size_t i = 0; i < videos_.size(); ++i) todo_.push_back(static_cast<int>(i));
    // never start more workers than there are videos to do
    jobs_ = std::max(1, std::min(opt.jobs, static_cast<int>(todo_.size())));
}

Downloader::~Downloader() {
    stop();
    for (std::thread& t : threads_)
        if (t.joinable()) t.join();
}

void Downloader::start() {
    workers_left_ = jobs_;
    for (int i = 0; i < jobs_; ++i) threads_.emplace_back(&Downloader::worker, this);
}

void Downloader::push(Update u) {
    {
        std::lock_guard<std::mutex> g(lock_);
        queue_.push_back(std::move(u));
    }
    wake_.notify_one();
}

int Downloader::run_once(int pos, std::string& last_error) {
    Process proc;
    {
        std::lock_guard<std::mutex> g(lock_);
        if (stopping_) return -1;

        // start yt-dlp while holding the lock, so stop() can't miss it
        if (!proc.start(build_video_args(opt_, folder_, videos_[pos], is_playlist_))) {
            last_error = "ERROR: could not start yt-dlp";
            queue_.push_back({pos, last_error});
            return 127;
        }
        running_.push_back(proc.pid());
    }

    // pass every output line to the main thread, remember the last error
    std::string line;
    while (proc.read_line(line)) {
        if (line.rfind("ERROR:", 0) == 0) last_error = line;
        push({pos, line});
    }

    // forget the pid *before* collecting the exit code, so stop()
    // never signals a process that's already gone
    {
        std::lock_guard<std::mutex> g(lock_);
        running_.erase(std::remove(running_.begin(), running_.end(), proc.pid()), running_.end());
    }
    return proc.wait();
}

void Downloader::worker() {
    while (true) {
        // 1. take the next video (or quit if none are left / we're stopping)
        int pos;
        {
            std::lock_guard<std::mutex> g(lock_);
            if (stopping_ || next_ >= todo_.size()) break;
            pos = todo_[next_++];
        }

        // 2. try it, and if it fails try again (up to opt.retries more times)
        int code = 0;
        for (int attempt = 0; ; ++attempt) {
            std::string last_error;
            code = run_once(pos, last_error);
            if (code == 0) break;                         // success
            if (attempt >= opt_.retries) break;           // out of retries
            if (is_permanent_error(last_error)) break;    // retrying can't help
            if (is_bot_check(last_error)) break;          // needs a login, not another try

            // wait a bit longer each time (3s, 6s, 9s ...), unless stop() wakes us
            int delay = 3 * (attempt + 1);
            Update u{pos, last_error};
            u.retry_attempt = attempt + 1;
            u.retry_delay = delay;
            push(u);
            std::unique_lock<std::mutex> g(lock_);
            if (stop_cv_.wait_for(g, std::chrono::seconds(delay), [this] { return stopping_; }))
                break;                                    // stopped while waiting
        }

        // 3. the final result for this video
        Update done;
        done.pos = pos;
        done.exited = true;
        done.exit_code = code;
        push(done);
    }

    {
        std::lock_guard<std::mutex> g(lock_);
        --workers_left_;
    }
    wake_.notify_one();
}

bool Downloader::poll(std::vector<Update>& out, int wait_ms) {
    std::unique_lock<std::mutex> g(lock_);
    if (queue_.empty() && workers_left_ > 0 && wait_ms > 0)
        wake_.wait_for(g, std::chrono::milliseconds(wait_ms),
                       [this] { return !queue_.empty() || workers_left_ == 0; });
    while (!queue_.empty()) {
        out.push_back(std::move(queue_.front()));
        queue_.pop_front();
    }
    return workers_left_ > 0 || !out.empty();
}

void Downloader::stop() {
    std::lock_guard<std::mutex> g(lock_);
    stopping_ = true;
    for (pid_t pid : running_) kill(pid, SIGTERM);
    stop_cv_.notify_all();  // wake workers sleeping between retries
}
