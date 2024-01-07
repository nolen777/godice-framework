#include "WorkQueue.h"

void WorkQueue::runner()
{
    while (keep_running_)
    {
        bool did_work = false;

        do
        {
            WorkItem work_item = nullptr;
            {
                std::unique_lock lk(mutex_);
                if (!keep_running_) return;
                if (work_queue_.empty())
                {
                    condition_.wait(lk);
                }
                else
                {
                    work_item = work_queue_.front();
                    work_queue_.pop();
                }
            }

            if (work_item != nullptr)
            {
                work_item();
                did_work = true;
            }
        } while (did_work && keep_running_);
    }
}

void WorkQueue::enqueue(const WorkItem& item)
{
    std::unique_lock lk(mutex_);
    work_queue_.push(item);
    condition_.notify_one();
}

void WorkQueue::stop()
{
    std::unique_lock lk(mutex_);
    std::queue<WorkItem>().swap(work_queue_);
    keep_running_ = false;
    condition_.notify_one();
}

WorkQueue::~WorkQueue()
{
    stop();
    runner_thread_.join();
}
