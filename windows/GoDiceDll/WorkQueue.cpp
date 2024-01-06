#include "WorkQueue.h"

void WorkQueue::Runner()
{
    while (1)
    {
        bool didWork = false;

        do
        {
            WorkItem workItem = nullptr;
            {
                std::unique_lock lk(mutex_);
                if (workQueue_.empty())
                {
                    condition_.wait(lk);
                }
                else
                {
                    workItem = workQueue_.front();
                    workQueue_.pop();
                }
            }

            if (workItem)
            {
                workItem();
                didWork = true;
            }
        } while (didWork);
    }
}

void WorkQueue::Enqueue(const WorkItem& item)
{
    std::unique_lock lk(mutex_);
    workQueue_.push(item);
    condition_.notify_one();
}