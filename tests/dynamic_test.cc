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

}  // namespace
}  // namespace ghost

void TaskDistribution(
  int task1_count,
  int task1_duration,
  int task2_count,
  int task2_duration,
  ) {

  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(task1_count + task2_count);

  for (int i = 0; i < task1_count; ++i)
    threads.emplace_back(
      new ghost::GhostThread(ghost::GhostThread::KernelScheduler::kGhost, [&task1_duration] {
        auto i = 0, timeNow = absl::GetCurrentTimeNanos();
        while (absl::GetCurrentTimeNanos() - timeNow <= (1e9 * task1_duration)) i=(i+1)%INT_MAX;
      }));

  for (int i = 0; i < task2_count; ++i)
    threads.emplace_back(
      new ghost::GhostThread(ghost::GhostThread::KernelScheduler::kGhost, [&task2_duration] {
        auto i = 0, timeNow = absl::GetCurrentTimeNanos();
        while (absl::GetCurrentTimeNanos() - timeNow <= (1e9 * task2_duration)) i=(i+1)%INT_MAX;
      }));

  for (auto& t : threads) t->Join();
}

int main() {
  {
    printf("HeavyFirst\n");
    ghost::ScopedTime time;
    ghost::TaskDistribution(1, 10, 1, 2);
  }
  {
    printf("Uniform\n");
    ghost::ScopedTime time;
    ghost::TaskDistribution(1, 5, 1, 5);
  }
  {
    printf("LightFirst\n");
    ghost::ScopedTime time;
    ghost::TaskDistribution(1, 2, 1, 10);
  }
}
