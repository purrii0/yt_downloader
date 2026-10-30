// tracker.cpp — see tracker.hpp.
#include "tracker.hpp"

const char* status_name(Status s) {
    switch (s) {
        case Status::Queued:      return "queued";
        case Status::Downloading: return "downloading";
        case Status::Merging:     return "merging";
        case Status::Retrying:    return "retrying";
        case Status::Done:        return "done";
        case Status::Skipped:     return "skipped";
        case Status::Failed:      return "failed";
    }
    return "?";
}

bool Tracker::apply(int pos, const Event& ev) {
    if (pos < 0 || pos >= static_cast<int>(items_.size())) return false;
    Item& it = items_[pos];

    switch (ev.type) {
        case EventType::Start:
            it.status = Status::Downloading;
            break;
        case EventType::Destination:  // a new stream starts (video first, then audio)
            it.stream += 1;
            it.percent = 0;
            it.status = Status::Downloading;
            break;
        case EventType::Progress:
            if (it.stream == 0) it.stream = 1;
            it.status = Status::Downloading;
            it.percent = ev.percent;
            it.speed = ev.speed;
            it.eta = ev.eta;
            break;
        case EventType::Merging:
            it.status = Status::Merging;
            it.percent = 100;
            break;
        case EventType::Done:
            it.status = Status::Done;
            it.percent = 100;
            it.file = ev.text;
            break;
        case EventType::Skipped:
            if (it.status != Status::Done) it.status = Status::Skipped;
            break;
        case EventType::Error:
            it.status = Status::Failed;
            it.error = ev.text;
            break;
        default:
            return false;
    }
    return true;
}

std::vector<int> Tracker::active() const {
    std::vector<int> out;
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i].status == Status::Downloading || items_[i].status == Status::Merging ||
            items_[i].status == Status::Retrying)
            out.push_back(static_cast<int>(i));
    return out;
}

void Tracker::note_retry(int pos, int attempt, int delay, const std::string& error) {
    if (pos < 0 || pos >= static_cast<int>(items_.size())) return;
    reset_item(pos);
    Item& it = items_[pos];
    it.status = Status::Retrying;
    it.attempt = attempt;
    it.retry_delay = delay;
    it.error = error;
}

void Tracker::finish_item(int pos, int exit_code) {
    if (pos < 0 || pos >= static_cast<int>(items_.size())) return;
    Item& it = items_[pos];
    if (it.status == Status::Done || it.status == Status::Skipped || it.status == Status::Failed)
        return;  // already decided
    if (exit_code == 0) {
        it.status = Status::Done;
        it.percent = 100;
    } else {
        it.status = Status::Failed;
        if (it.error.empty())
            it.error = exit_code < 0 ? "stopped" : "yt-dlp exited with code " + std::to_string(exit_code);
    }
}

void Tracker::finish() {
    for (Item& it : items_)
        if (it.status == Status::Downloading || it.status == Status::Merging ||
            it.status == Status::Retrying) {
            it.status = Status::Failed;
            if (it.error.empty()) it.error = "stopped before finishing";
        }
}

std::vector<int> Tracker::retryable() const {
    std::vector<int> out;
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i].status == Status::Failed || items_[i].status == Status::Queued)
            out.push_back(static_cast<int>(i));
    return out;
}

void Tracker::reset_item(int pos) {
    if (pos < 0 || pos >= static_cast<int>(items_.size())) return;
    Item fresh;
    fresh.index = items_[pos].index;
    fresh.title = items_[pos].title;
    fresh.attempt = items_[pos].attempt;
    items_[pos] = fresh;
}

int Tracker::count(Status s) const {
    int n = 0;
    for (const Item& it : items_)
        if (it.status == s) ++n;
    return n;
}

int Tracker::finished() const {
    return count(Status::Done) + count(Status::Skipped) + count(Status::Failed);
}
