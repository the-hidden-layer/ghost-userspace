#include <stdio.h>
#include <string>

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


int unoptimizableFunc(int numIters) {
  volatile int result = 0; // Using volatile to prevent optimization

  for (int i = 0; i < numIters; i+=17) {
      result += i * (i + std::rand() % 1000); // Performing some computation
  }

  return result;
}

void TaskDistribution(
  int task_count
  ) {
  std::vector<std::unique_ptr<ghost::GhostThread>> threads;
  threads.reserve(task_count);

  const int smallTaskNumIters = 4400000; // ~ 5 ms

  std::vector<int> taskResults(task_count);

  for (int i=0; i<task_count; i++) {
    int curId = i;
    threads.emplace_back(new ghost::GhostThread(
            ghost::GhostThread::KernelScheduler::kGhost,
            [&,curId]() {
                int result = unoptimizableFunc(smallTaskNumIters);
                taskResults[curId] = result;
            }
        ));
}

  for (auto& t : threads) t->Join();
}

}  // namespace
}  // namespace ghost

int main() {
  // {
  //   printf("HeavyFirst\n");
  //   ghost::ScopedTime time;
  //   ghost::TaskDistribution(10, 1000, 1000, 10);
  // }
  // {
  //   printf("Uniform\n");
  //   ghost::ScopedTime time;
  //   ghost::TaskDistribution(1000, 10, 1000, 10);
  // }
  // printf("N\n");
  ghost::TaskDistribution(1000);
  // printf("TotalServiceTime: %0.2f ms\n", totalServiceTime);
}
