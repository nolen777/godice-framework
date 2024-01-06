#include "WorkQueue.h"

void WorkQueue::Runner()
{
    while (1)
    {
        workQueueSemaphore_.acquire();
        WorkItem work;
        {
            std::scoped_lock lk(workQueueMutex_);
            work = workQueue_.front();
            workQueue_.pop();
        }
        work();
    }
}

void WorkQueue::Enqueue(const WorkItem& item)
{
    {
        std::scoped_lock lk(workQueueMutex_);
        workQueue_.push(item);
    }
    workQueueSemaphore_.release();
}