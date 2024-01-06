#pragma once

#include <functional>
#include <mutex>
#include <queue>
#include <semaphore>
#include <thread>

using WorkItem = std::function<void(void)>;

class WorkQueue
{
private:
    void Runner();
    std::thread runnerThread_;

    std::queue<WorkItem> workQueue_;
    std::mutex mutex_;
    std::condition_variable condition_;
    
public:
    WorkQueue() : runnerThread_(&WorkQueue::Runner, this) {}

    void Enqueue(const WorkItem& item);
};
