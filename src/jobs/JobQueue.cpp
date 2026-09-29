#include "jobs/JobQueue.h"

#include <algorithm>

#include "core/Log.h"

namespace up {

const char* toString(JobState state) {
    switch (state) {
        case JobState::Queued: return "queued";
        case JobState::Running: return "running";
        case JobState::Succeeded: return "succeeded";
        case JobState::Failed: return "failed";
        case JobState::Cancelled: return "cancelled";
    }
    return "?";
}

JobQueue::JobQueue(int workers) {
    for (int i = 0; i < std::max(1, workers); ++i) workers_.emplace_back(&JobQueue::workerLoop, this);
}

JobQueue::~JobQueue() {
    cancelAll();
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (auto& t : workers_) t.join();
}

uint64_t JobQueue::submit(std::string name, int priority, Fn fn, Listener onDone) {
    auto job = std::make_unique<Job>();
    std::lock_guard lock(mutex_);
    job->info.id = nextId_++;
    job->info.name = std::move(name);
    job->info.priority = priority;
    job->sequence = nextSequence_++;
    job->fn = std::move(fn);
    job->onDone = std::move(onDone);
    const uint64_t id = job->info.id;
    history_[id] = job->info;
    historyOrder_.push_back(id);
    while (historyOrder_.size() > kHistoryLimit) {
        history_.erase(historyOrder_.front());
        historyOrder_.pop_front();
    }
    queue_.push_back(std::move(job));
    wake_.notify_one();
    return id;
}

bool JobQueue::cancel(uint64_t id) {
    std::unique_ptr<Job> removed;
    {
        std::lock_guard lock(mutex_);
        if (auto it = runningTokens_.find(id); it != runningTokens_.end()) {
            it->second->cancel();
            return true;
        }
        auto it = std::find_if(queue_.begin(), queue_.end(), [&](const auto& j) { return j->info.id == id; });
        if (it == queue_.end()) return false;
        removed = std::move(*it);
        queue_.erase(it);
        ++finishing_;
    }
    finish(*removed, JobState::Cancelled, std::nullopt);
    return true;
}

void JobQueue::cancelAll() {
    std::vector<std::unique_ptr<Job>> removed;
    {
        std::lock_guard lock(mutex_);
        removed.swap(queue_);
        finishing_ += removed.size();
        for (auto& [id, token] : runningTokens_) token->cancel();
    }
    for (auto& job : removed) finish(*job, JobState::Cancelled, std::nullopt);
}

void JobQueue::waitIdle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return queue_.empty() && finishing_ == 0; });
}

std::optional<JobInfo> JobQueue::info(uint64_t id) const {
    std::lock_guard lock(mutex_);
    auto it = history_.find(id);
    if (it == history_.end()) return std::nullopt;
    return it->second;
}

std::size_t JobQueue::queued() const {
    std::lock_guard lock(mutex_);
    return queue_.size();
}

std::size_t JobQueue::running() const {
    std::lock_guard lock(mutex_);
    return runningTokens_.size();
}

void JobQueue::finish(Job& job, JobState state, std::optional<Error> error) {
    job.info.state = state;
    job.info.error = std::move(error);
    {
        std::lock_guard lock(mutex_);
        if (auto it = history_.find(job.info.id); it != history_.end()) it->second = job.info;
    }
    if (job.onDone) job.onDone(job.info);
    std::lock_guard lock(mutex_);
    --finishing_;
    if (queue_.empty() && finishing_ == 0) idle_.notify_all();
}

void JobQueue::workerLoop() {
    while (true) {
        std::unique_ptr<Job> job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            auto best = std::min_element(queue_.begin(), queue_.end(), [](const auto& a, const auto& b) {
                if (a->info.priority != b->info.priority) return a->info.priority > b->info.priority;
                return a->sequence < b->sequence;
            });
            job = std::move(*best);
            queue_.erase(best);
            ++finishing_;
            job->info.state = JobState::Running;
            history_[job->info.id] = job->info;
            runningTokens_[job->info.id] = job->token;
        }
        Status result = Status::success();
        if (!job->token->cancelled()) result = job->fn(*job->token);
        {
            std::lock_guard lock(mutex_);
            runningTokens_.erase(job->info.id);
        }
        if (job->token->cancelled()) {
            finish(*job, JobState::Cancelled, std::nullopt);
        } else if (!result.ok()) {
            UP_LOG_WARN("jobs", "Job '" << job->info.name << "' failed: " << result.error().message);
            finish(*job, JobState::Failed, result.error());
        } else {
            finish(*job, JobState::Succeeded, std::nullopt);
        }
    }
}

}  // namespace up
