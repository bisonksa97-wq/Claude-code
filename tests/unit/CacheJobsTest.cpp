#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

#include "cache/DiskCache.h"
#include "jobs/JobQueue.h"
#include "support/TestSupport.h"

using namespace up;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

TEST(DiskCache, WriteReadRemove) {
    test::TempDir dir;
    DiskCache cache(dir.path());
    EXPECT_FALSE(cache.read("k", ".bin").has_value());
    ASSERT_TRUE(cache.write("k", ".bin", "hello").ok());
    EXPECT_TRUE(cache.contains("k", ".bin"));
    EXPECT_EQ(cache.read("k", ".bin").value(), "hello");
    EXPECT_FALSE(cache.contains("k", ".other"));  // extension is part of the identity
    EXPECT_EQ(cache.entryCount(), 1u);
    cache.remove("k", ".bin");
    EXPECT_FALSE(cache.contains("k", ".bin"));
}

TEST(DiskCache, KeysAreHashedIntoShardedPaths) {
    test::TempDir dir;
    DiskCache cache(dir.path());
    EXPECT_EQ(DiskCache::hashKey("a").size(), 16u);
    EXPECT_NE(DiskCache::hashKey("a"), DiskCache::hashKey("b"));
    EXPECT_EQ(DiskCache::hashKey("same"), DiskCache::hashKey("same"));
    const fs::path p = cache.pathFor("/very/long/path|123|456", ".ppm");
    EXPECT_EQ(p.parent_path().parent_path(), dir.path());
    EXPECT_EQ(p.parent_path().filename().string(), DiskCache::hashKey("/very/long/path|123|456").substr(0, 2));
}

TEST(DiskCache, EvictsLeastRecentlyUsedWhenOverLimit) {
    test::TempDir dir;
    DiskCache cache(dir.path(), 2500);
    const std::string kb(1000, 'x');
    ASSERT_TRUE(cache.write("old", ".b", kb).ok());
    ASSERT_TRUE(cache.write("mid", ".b", kb).ok());
    // Make access times unambiguous regardless of file-system timestamp resolution.
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(cache.pathFor("old", ".b"), now - 10s);
    fs::last_write_time(cache.pathFor("mid", ".b"), now - 5s);
    ASSERT_TRUE(cache.read("old", ".b").has_value());  // touching "old" makes "mid" the LRU entry
    ASSERT_TRUE(cache.write("new", ".b", kb).ok());     // 3000 bytes > 2500: evict one
    EXPECT_TRUE(cache.contains("old", ".b"));
    EXPECT_FALSE(cache.contains("mid", ".b"));
    EXPECT_TRUE(cache.contains("new", ".b"));
    EXPECT_LE(cache.sizeBytes(), 2500u);
}

TEST(DiskCache, ClearOnlyRemovesCacheEntries) {
    test::TempDir dir;
    DiskCache cache(dir.path());
    ASSERT_TRUE(cache.write("a", ".b", "1").ok());
    ASSERT_TRUE(cache.write("b", ".b", "2").ok());
    std::ofstream(dir / "keep-me.txt") << "not ours";
    ASSERT_TRUE(cache.clear().ok());
    EXPECT_EQ(cache.entryCount(), 0u);
    EXPECT_TRUE(fs::exists(dir / "keep-me.txt"));
}

TEST(DiskCache, DefaultDirectoryIsPerUser) {
    EXPECT_FALSE(DiskCache::defaultDirectory().empty());
}

TEST(JobQueue, RunsByPriorityThenSubmissionOrder) {
    JobQueue queue(1);
    std::mutex m;
    std::vector<std::string> order;
    std::atomic<bool> release{false};
    // Block the single worker so the rest queue up.
    queue.submit("gate", 100, [&](const CancelToken&) {
        while (!release) std::this_thread::sleep_for(1ms);
        return Status::success();
    });
    auto record = [&](std::string name) {
        return [&, name](const CancelToken&) {
            std::lock_guard lock(m);
            order.push_back(name);
            return Status::success();
        };
    };
    queue.submit("low1", 1, record("low1"));
    queue.submit("high", 9, record("high"));
    queue.submit("low2", 1, record("low2"));
    queue.submit("mid", 5, record("mid"));
    release = true;
    queue.waitIdle();
    EXPECT_EQ(order, (std::vector<std::string>{"high", "mid", "low1", "low2"}));
}

TEST(JobQueue, ReportsFailureAndSuccess) {
    JobQueue queue(2);
    const auto ok = queue.submit("ok", 0, [](const CancelToken&) { return Status::success(); });
    const auto bad = queue.submit("bad", 0, [](const CancelToken&) {
        return Status(makeError(ErrorCode::DecodeError, "test", "boom"));
    });
    queue.waitIdle();
    EXPECT_EQ(queue.info(ok)->state, JobState::Succeeded);
    EXPECT_EQ(queue.info(bad)->state, JobState::Failed);
    EXPECT_EQ(queue.info(bad)->error->message, "boom");
}

TEST(JobQueue, CancelsQueuedAndRunningJobs) {
    JobQueue queue(1);
    std::atomic<bool> started{false};
    std::atomic<int> listenerCalls{0};
    const auto running = queue.submit(
        "long", 0,
        [&](const CancelToken& token) {
            started = true;
            while (!token.cancelled()) std::this_thread::sleep_for(1ms);
            return Status::success();
        },
        [&](const JobInfo& info) {
            EXPECT_EQ(info.state, JobState::Cancelled);
            ++listenerCalls;
        });
    std::atomic<bool> queuedRan{false};
    const auto queued = queue.submit(
        "queued", 0,
        [&](const CancelToken&) {
            queuedRan = true;
            return Status::success();
        },
        [&](const JobInfo& info) {
            EXPECT_EQ(info.state, JobState::Cancelled);
            ++listenerCalls;
        });
    while (!started) std::this_thread::sleep_for(1ms);
    EXPECT_TRUE(queue.cancel(queued));
    EXPECT_TRUE(queue.cancel(running));
    queue.waitIdle();
    EXPECT_FALSE(queuedRan);
    EXPECT_EQ(queue.info(running)->state, JobState::Cancelled);
    EXPECT_EQ(queue.info(queued)->state, JobState::Cancelled);
    EXPECT_EQ(listenerCalls, 2);
    EXPECT_FALSE(queue.cancel(running));  // already finished
}

TEST(JobQueue, DestructorCancelsOutstandingWork) {
    std::atomic<int> cancelled{0};
    {
        JobQueue queue(1);
        for (int i = 0; i < 5; ++i) {
            queue.submit(
                "job", 0,
                [](const CancelToken& token) {
                    while (!token.cancelled()) std::this_thread::sleep_for(1ms);
                    return Status::success();
                },
                [&](const JobInfo& info) { cancelled += info.state == JobState::Cancelled; });
        }
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_EQ(cancelled, 5);
}

TEST(JobQueue, WaitIdleWaitsForListeners) {
    JobQueue queue(2);
    std::atomic<int> done{0};
    for (int i = 0; i < 20; ++i) {
        queue.submit("n", 0, [](const CancelToken&) { return Status::success(); },
                     [&](const JobInfo&) {
                         std::this_thread::sleep_for(1ms);
                         ++done;
                     });
    }
    queue.waitIdle();
    EXPECT_EQ(done, 20);
}
