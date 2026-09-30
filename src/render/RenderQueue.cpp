#include "render/RenderQueue.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "core/Log.h"

namespace up::render {
namespace {

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%H:%M:%S");
    return out.str();
}

}  // namespace

const char* RenderQueue::toString(State state) {
    switch (state) {
        case State::Queued: return "Queued";
        case State::Running: return "Rendering";
        case State::Done: return "Done";
        case State::Failed: return "Failed";
        case State::Cancelled: return "Cancelled";
    }
    return "";
}

RenderQueue::~RenderQueue() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        if (runningJob_) runningJob_->cancel();
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void RenderQueue::note(Entry& entry, const std::string& line) {
    entry.info.log.push_back(timestamp() + "  " + line);
    entry.info.message = line;
}

void RenderQueue::notify() {
    Listener listener;
    {
        std::lock_guard lock(mutex_);
        listener = listener_;
    }
    if (listener) listener();
}

int RenderQueue::add(Project snapshot, std::string timelineId, ExportOptions options, std::string name) {
    int id = 0;
    {
        std::lock_guard lock(mutex_);
        auto entry = std::make_unique<Entry>();
        id = entry->info.id = nextId_++;
        entry->info.name = name.empty() ? options.output.filename().string() : std::move(name);
        entry->info.output = options.output;
        entry->info.presetName = options.preset ? options.preset->name : "H.264 (default)";
        entry->project = std::move(snapshot);
        entry->timelineId = std::move(timelineId);
        entry->options = std::move(options);
        note(*entry, "Queued: " + entry->info.presetName + " -> " + entry->info.output.string());
        entries_.push_back(std::move(entry));
        if (!worker_.joinable()) worker_ = std::thread([this] { workerLoop(); });
    }
    wake_.notify_all();
    notify();
    return id;
}

bool RenderQueue::cancel(int id) {
    bool changed = false;
    {
        std::lock_guard lock(mutex_);
        for (auto& e : entries_) {
            if (e->info.id != id) continue;
            if (e->info.state == State::Queued) {
                e->info.state = State::Cancelled;
                note(*e, "Cancelled before it started");
                changed = true;
            } else if (e.get() == running_ && runningJob_) {
                runningJob_->cancel();
                note(*e, "Cancelling...");
                changed = true;
            }
        }
    }
    if (changed) {
        idle_.notify_all();
        notify();
    }
    return changed;
}

bool RenderQueue::remove(int id) {
    bool removed = false;
    {
        std::lock_guard lock(mutex_);
        const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto& e) { return e->info.id == id; });
        if (it != entries_.end() && it->get() != running_) {
            entries_.erase(it);
            removed = true;
        }
    }
    if (removed) {
        idle_.notify_all();
        notify();
    }
    return removed;
}

void RenderQueue::clearFinished() {
    {
        std::lock_guard lock(mutex_);
        std::erase_if(entries_, [&](const auto& e) {
            return e.get() != running_ && (e->info.state == State::Done || e->info.state == State::Failed ||
                                           e->info.state == State::Cancelled);
        });
    }
    notify();
}

std::vector<RenderQueue::Job> RenderQueue::jobs() const {
    std::lock_guard lock(mutex_);
    std::vector<Job> out;
    for (const auto& e : entries_) out.push_back(e->info);
    return out;
}

std::optional<RenderQueue::Job> RenderQueue::job(int id) const {
    std::lock_guard lock(mutex_);
    for (const auto& e : entries_)
        if (e->info.id == id) return e->info;
    return std::nullopt;
}

bool RenderQueue::busy() const {
    std::lock_guard lock(mutex_);
    return running_ || std::any_of(entries_.begin(), entries_.end(), [](const auto& e) { return e->info.state == State::Queued; });
}

void RenderQueue::setListener(Listener listener) {
    std::lock_guard lock(mutex_);
    listener_ = std::move(listener);
}

bool RenderQueue::waitIdle(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return idle_.wait_for(lock, timeout, [&] {
        return !running_ && std::none_of(entries_.begin(), entries_.end(), [](const auto& e) { return e->info.state == State::Queued; });
    });
}

void RenderQueue::workerLoop() {
    while (true) {
        Entry* entry = nullptr;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] {
                return stopping_ || std::any_of(entries_.begin(), entries_.end(), [](const auto& e) { return e->info.state == State::Queued; });
            });
            if (stopping_) return;
            for (auto& e : entries_) {
                if (e->info.state == State::Queued) {
                    entry = e.get();
                    break;
                }
            }
            entry->info.state = State::Running;
            note(*entry, "Rendering");
            running_ = entry;
        }
        notify();

        // The job works on the entry's own snapshot, outside the lock.
        ExportJob job(entry->project, entry->timelineId, entry->options);
        {
            std::lock_guard lock(mutex_);
            runningJob_ = &job;
            if (stopping_) job.cancel();
        }
        const auto started = std::chrono::steady_clock::now();
        FrameIndex lastReported = -1;
        const Status result = job.run([&](const ExportProgress& p) {
            {
                std::lock_guard lock(mutex_);
                entry->info.framesDone = p.framesDone;
                entry->info.framesTotal = p.framesTotal;
            }
            // Throttle notifications to about every 1 % (and the last frame).
            const FrameIndex step = std::max<FrameIndex>(1, p.framesTotal / 100);
            if (p.framesDone == p.framesTotal || p.framesDone - lastReported >= step) {
                lastReported = p.framesDone;
                notify();
            }
        });
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        {
            std::lock_guard lock(mutex_);
            runningJob_ = nullptr;
            running_ = nullptr;
            entry->info.seconds = seconds;
            if (result.ok()) {
                entry->info.state = State::Done;
                std::ostringstream line;
                line << "Done: " << entry->info.framesTotal << " frames in " << std::fixed << std::setprecision(1) << seconds << " s";
                note(*entry, line.str());
            } else if (result.error().code == ErrorCode::Cancelled) {
                entry->info.state = State::Cancelled;
                note(*entry, "Cancelled; no file was left behind");
            } else {
                entry->info.state = State::Failed;
                note(*entry, "Failed: " + result.error().message);
                if (!result.error().suggestion.empty()) entry->info.log.push_back(timestamp() + "  " + result.error().suggestion);
                if (!result.error().details.empty()) entry->info.log.push_back(timestamp() + "  " + result.error().details);
                UP_LOG_WARN(log::sub::Render, "Render queue job '" << entry->info.name << "' failed: " << result.error().toString());
            }
        }
        idle_.notify_all();
        notify();
    }
}

}  // namespace up::render
