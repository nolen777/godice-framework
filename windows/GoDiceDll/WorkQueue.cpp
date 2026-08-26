#include "WorkQueue.h"

#include <utility>

#include <winrt/base.h>

void WorkQueue::runner()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    while (true)
    {
        WorkItem work_item;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this]
            {
                return !keep_running_ || !work_queue_.empty();
            });

            if (!keep_running_ && work_queue_.empty()) return;

            work_item = std::move(work_queue_.front());
            work_queue_.pop();
        }

        try
        {
            work_item();
        }
        catch (...)
        {
            // An operation must not terminate the queue that owns Bluetooth state.
        }
    }
}

bool WorkQueue::enqueue(WorkItem item)
{
    std::unique_lock lock(mutex_);
    if (!keep_running_) return false;

    work_queue_.push(std::move(item));
    condition_.notify_one();
    return true;
}

void WorkQueue::stop()
{
    std::unique_lock lock(mutex_);
    if (!keep_running_) return;

    keep_running_ = false;
    condition_.notify_one();
}

WorkQueue::~WorkQueue()
{
    stop();
    runner_thread_.join();
}
