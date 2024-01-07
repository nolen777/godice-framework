#pragma once

#include <functional>
#include <mutex>
#include <queue>
#include <thread>

using WorkItem = std::function<void()>;

class WorkQueue
{
private:
    const std::string name_;
    void runner();
    std::thread runner_thread_;

    std::queue<WorkItem> work_queue_;
    std::mutex mutex_;
    std::condition_variable condition_;

    bool keep_running_ = true;
    
public:
    explicit WorkQueue(const std::string& nm) : name_(nm), runner_thread_(&WorkQueue::runner, this) {}
    ~WorkQueue();

    void enqueue(const WorkItem& item);

    void stop();

    [[nodiscard]] auto name() const -> std::string { return name_; }
};
