#include "khdays/game/task.h"

#include <utility>

namespace khdays::game {

void TaskQueue::add(TaskFn task) {
    if (!task) {
        return;
    }
    // Adding while iterating would invalidate the walk; stage it for next frame.
    (updating_ ? incoming_ : tasks_).push_back(std::move(task));
}

void TaskQueue::update() {
    updating_ = true;
    std::vector<TaskFn> survivors;
    survivors.reserve(tasks_.size());
    for (auto& task : tasks_) {
        if (task()) {
            survivors.push_back(std::move(task));
        }
    }
    updating_ = false;

    tasks_ = std::move(survivors);
    for (auto& task : incoming_) {
        tasks_.push_back(std::move(task));
    }
    incoming_.clear();
}

}  // namespace khdays::game
