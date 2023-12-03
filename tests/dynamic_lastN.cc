#include <stdio.h>

#include <atomic>
#include <memory>
#include <vector>

#include "lib/base.h"
#include "lib/ghost.h"

// A series of simple tests for ghOSt schedulers.

namespace ghost {
namespace {

struct ScopedTime {
  ScopedTime() { start = absl::Now(); }
  ~ScopedTime() {
    printf(" took %0.2f ms\n", absl::ToDoubleMilliseconds(absl::Now() - start));
  }
  absl::Time start;
};

void TaskDistribution(
  int task1_count,
  int task1_duration /* milliseconds */
  ) {

  std::vector<std::unique_ptr<ghost::GhostThread>> threads;
  threads.reserve(task1_count);

  for (int i = 0; i < task1_count; ++i)
    threads.emplace_back(
      new ghost::GhostThread(ghost::GhostThread::KernelScheduler::kGhost, [&task1_duration] {
        int i=0;
        auto timeNow = absl::GetCurrentTimeNanos();
        while (absl::GetCurrentTimeNanos() - timeNow <= (1e6 * task1_duration)) i=(i+1)%INT_MAX;
      }));

  for (auto& t : threads) t->Join();
}

}  // namespace
}  // namespace ghost

int main() {
  {
    printf("lastN\n");
    ghost::ScopedTime time;
    ghost::TaskDistribution(400, 10);
  }
}
