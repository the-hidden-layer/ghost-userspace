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

    void unoptimizableFunc() {
        volatile int result = 0; // Using volatile to prevent optimization

        for (int i = 0; i < 2000000; i+=17) {
            result += i * (i + std::rand() % 1000); // Performing some computation
        }

        std::cout << "Result: " << result << std::endl;
    }

    void TaskDistribution(
  int task_count
  ) {

  std::vector<std::unique_ptr<ghost::GhostThread>> threads;
  threads.reserve(task_count);

  for (int i = 0; i < task_count; ++i)
    threads.emplace_back(
      new ghost::GhostThread(ghost::GhostThread::KernelScheduler::kGhost, [] {
        unoptimizableFunc();
      }));

  for (auto& t : threads) t->Join();
}
}
}

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
  {
    printf("Fifo Workload\n");
    ghost::ScopedTime time;
    ghost::TaskDistribution(5000);
  }
}
