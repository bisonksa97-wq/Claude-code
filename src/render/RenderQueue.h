#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "render/ExportJob.h"

namespace up::render {

// Background exports, one at a time in the order they were added. Each job renders
// its own project snapshot, so editing continues while the queue runs. Jobs can be
// cancelled while queued or running; a failed job does not stop the ones after it.
class RenderQueue {
public:
    enum class State { Queued, Running, Done, Failed, Cancelled };

    struct Job {
        int id = 0;
        std::string name;
        std::filesystem::path output;
        std::string presetName;
        State state = State::Queued;
        FrameIndex framesDone = 0;
        FrameIndex framesTotal = 0;
        std::string message;           // last status line (the error for failed jobs)
        std::vector<std::string> log;  // timestamped lines
        double seconds = 0.0;          // render time once finished
    };

    // Called on the worker thread after any change (state, progress, log). Keep it cheap;
    // UIs should marshal to their own thread.
    using Listener = std::function<void()>;

    RenderQueue() = default;
    // Cancels the running job, drops queued ones and joins the worker.
    ~RenderQueue();
    RenderQueue(const RenderQueue&) = delete;
    RenderQueue& operator=(const RenderQueue&) = delete;

    int add(Project snapshot, std::string timelineId, ExportOptions options, std::string name = {});
    // Queued jobs are cancelled at once; a running job stops at its next frame.
    bool cancel(int id);
    // Removes a job that is not running.
    bool remove(int id);
    void clearFinished();

    std::vector<Job> jobs() const;
    std::optional<Job> job(int id) const;
    bool busy() const;
    void setListener(Listener listener);
    // Blocks until no job is queued or running (or the timeout passes). Returns true when idle.
    bool waitIdle(std::chrono::milliseconds timeout = std::chrono::hours(24));

    static const char* toString(State state);

private:
    struct Entry {
        Job info;
        Project project;
        std::string timelineId;
        ExportOptions options;
    };

    void workerLoop();
    void note(Entry& entry, const std::string& line);  // requires mutex_
    void notify();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<std::unique_ptr<Entry>> entries_;
    Entry* running_ = nullptr;
    ExportJob* runningJob_ = nullptr;
    Listener listener_;
    std::thread worker_;
    bool stopping_ = false;
    int nextId_ = 1;
};

}  // namespace up::render
