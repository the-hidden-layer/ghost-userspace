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

void PreemptionDetector(int threadID) {
  printf("\nStarting simple worker\n");
  GhostThread t(GhostThread::KernelScheduler::kGhost, [&threadID] {
    for(int i=0; i<6; i++) {
        printf("THREAD %d iter %d \n", threadID, i);
        absl::SleepFor(absl::Seconds(1));
    }
  });
  t.Join();
  // printf("\nFinished simple worker\n");
}

}  // namespace
}  // namespace ghost

int main() {

  printf("PreemptionDetector\n");
  ghost::ScopedTime time;
  
  auto t1 = ghost::GhostThread(ghost::GhostThread::KernelScheduler::kGhost, [] {
    auto timeNow = absl::GetCurrentTimeNanos();
    int i=0;
    while(absl::GetCurrentTimeNanos() - timeNow <= 6e9) {
        i=(i+1)%INT_MAX;
        // printf("THREAD %d iter %d \n", 1, i);
    }
    std::cout<<"Thread 1 "<<i<<std::endl;
  });

  auto t2 = ghost::GhostThread(ghost::GhostThread::KernelScheduler::kGhost, [] {
    auto timeNow = absl::GetCurrentTimeNanos();
    int i=0;
    while (absl::GetCurrentTimeNanos() - timeNow <= 6e9) {
        i=(i+1)%INT_MAX;
    }
    std::cout<<"Thread 2 "<<i<<std::endl;
  });

  t1.Join();
  t2.Join();

  std::cout<<"Done!"<<std::endl;
  return 0;
}
