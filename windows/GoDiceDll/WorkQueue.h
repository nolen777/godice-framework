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
    std::binary_semaphore workQueueSemaphore_;
    std::mutex workQueueMutex_;
    
public:
    WorkQueue() : runnerThread_(&WorkQueue::Runner, this), workQueueSemaphore_(0) {}

    void Enqueue(const WorkItem& item);
};
