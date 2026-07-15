#pragma once

#include <cstddef>
#include <functional>
#include <vector>

// The per-frame task queue — the native equivalent of the DS task system
// (FrameStep_UpdateTaskQueue @0x020115b8). Tasks are lightweight background
// jobs updated once per frame before the scene; a task that reports itself
// finished is dropped.
namespace khdays::game {

// A task's per-frame step. Return true to keep running, false when finished.
using TaskFn = std::function<bool()>;

class TaskQueue final {
public:
    // Enqueue a task. Tasks added during update() first run on the next frame.
    void add(TaskFn task);

    // Run every task once and remove the finished ones.
    void update();

    std::size_t size() const { return tasks_.size(); }
    bool empty() const { return tasks_.empty(); }

private:
    std::vector<TaskFn> tasks_;
    std::vector<TaskFn> incoming_;
    bool updating_ = false;
};

}  // namespace khdays::game
