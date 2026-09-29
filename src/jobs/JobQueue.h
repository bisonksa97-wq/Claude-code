#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/CancelToken.h"
#include "core/Result.h"

namespace up {

enum class JobState { Queued, Running, Succeeded, Failed, Cancelled };

const char* toString(JobState state);

struct JobInfo {
    uint64_t id = 0;
    std::string name;
    int priority = 0;
    JobState state = JobState::Queued;
    std::optional<Error> error;
};

// Background job runner with priorities and cooperative cancellation.
// Higher priority runs first; equal priorities run in submission order.
// Completion listeners are called on the worker thread that ran the job
// (or on the cancelling thread for jobs cancelled before they started).
class JobQueue {
public:
    using Fn = std::function<Status(const CancelToken&)>;
    using Listener = std::function<void(const JobInfo&)>;

    explicit JobQueue(int workers = 2);
    ~JobQueue();  // cancels outstanding work and joins the workers

    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    uint64_t submit(std::string name, int priority, Fn fn, Listener onDone = {});
    // Queued jobs are removed immediately; running jobs are asked to stop.
    bool cancel(uint64_t id);
    void cancelAll();
    // Blocks until nothing is queued or running.
    void waitIdle();

    std::optional<JobInfo> info(uint64_t id) const;
    std::size_t queued() const;
    std::size_t running() const;

private:
    struct Job {
        JobInfo info;
        uint64_t sequence = 0;
        Fn fn;
        Listener onDone;
        std::shared_ptr<CancelToken> token = std::make_shared<CancelToken>();
    };

    void workerLoop();
    void finish(Job& job, JobState state, std::optional<Error> error);

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::vector<std::unique_ptr<Job>> queue_;
    std::map<uint64_t, std::shared_ptr<CancelToken>> runningTokens_;
    std::size_t finishing_ = 0;  // jobs out of the queue whose listener has not returned yet
    std::map<uint64_t, JobInfo> history_;
    std::deque<uint64_t> historyOrder_;
    uint64_t nextId_ = 1;
    uint64_t nextSequence_ = 0;
    bool stopping_ = false;
    std::vector<std::thread> workers_;

    static constexpr std::size_t kHistoryLimit = 512;
};

}  // namespace up
