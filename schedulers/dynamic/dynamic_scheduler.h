#ifndef GHOST_SCHEDULERS_DYNAMIC_DYNAMIC_SCHEDULER_H
#define GHOST_SCHEDULERS_DYNAMIC_DYNAMIC_SCHEDULER_H

#include <deque>
#include <memory>

#include "lib/agent.h"
#include "lib/scheduler.h"

namespace ghost {

enum class DynamicScheduler {
  FIFO,
  ROUND_ROBIN,
};

std::ostream& operator<<(std::ostream& os, const DynamicScheduler& state);

struct DynamicTask : public Task<> {
  explicit DynamicTask()
}

}  // namespace ghost

#endif  // GHOST_SCHEDULERS_DYNAMIC_DYNAMIC_SCHEDULER_H
