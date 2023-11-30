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

void func(int time1, int time2) {
    auto t1 = ghost::GhostThread(ghost::GhostThread::KernelScheduler::kGhost, [&time1] {
    auto timeNow = absl::GetCurrentTimeNanos();
    int i=0;
    while (absl::GetCurrentTimeNanos() - timeNow <= (1e9 * time1)) {
        i=(i+1)%INT_MAX;
    }
    });

    absl::SleepFor(absl::Seconds(1));
    
    auto t2 = ghost::GhostThread(ghost::GhostThread::KernelScheduler::kGhost, [&time2] {
    auto timeNow = absl::GetCurrentTimeNanos();
    int i=0;
    while (absl::GetCurrentTimeNanos() - timeNow <= (1e9 * time2)) {
        i=(i+1)%INT_MAX;
    }
    });

    t1.Join();
    t2.Join();
    std::cout << "Func Done!\n";
}

int main() {

  printf("PreemptionDetector\n");
  ghost::ScopedTime time;
  

  func (10, 2);
  func (10, 2);

  std::cout<<"Done!"<<std::endl;
  return 0;
}
