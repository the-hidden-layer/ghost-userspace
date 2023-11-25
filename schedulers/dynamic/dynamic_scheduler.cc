#include "schedulers/dynamic/dynamic_scheduler.h"

#include <memory>

namespace ghost {

std::ostream& operator<<(std::ostream& os, const DynamicTaskState& state) {
  switch (state) {
    case DynamicTaskState::kBlocked:
      return os << "kBlocked";
    case DynamicTaskState::kRunnable:
      return os << "kRunnable";
    case DynamicTaskState::kQueued:
      return os << "kQueued";
    case DynamicTaskState::kOnCpu:
      return os << "kOnCpu";
  }
}

} //  namespace ghost
