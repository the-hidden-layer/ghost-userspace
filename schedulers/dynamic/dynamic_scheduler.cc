/**
TODO: 

WRITING
- abstract
- intro
- background
- related material
- design
- future work (things we wanted to do for efficiency / cleaner code)
  - strategy pattern
  - adding more policies
  - optimizing load/offload API
  - experimenting with different hyperparams for different algorithms (RR)

Important NOTES
1. Now that CPU tick has been turned on, both the cpu tick and schedule apis are called every tic

CODING
1. eval RR - Done
2. For fifo and round robin don’t remove current task from run queue until the task end function is called - Done
3. preemption api - Done
4. change cpu data structure to maintain controle module instead of run queue - Done
5. look into locks for scheduling data structures
6. inject control module into ghost apis

BENCHMARKS

*/

#include "dynamic_scheduler.h"

#include <memory>
#include <queue>

namespace ghost {

class FifoSchedPolicy : public DynamicSchedPolicy {
private:
  std::deque<DynamicTask*> rq;
  mutable absl::Mutex mu_;

public:
  int64_t evaluatePolicy(const std::vector<SampledTaskDetails*>& sampledTasks) {
    // std::cout<<"Evaluating FIFO"<<std::endl;
    int64_t totalServiceTime = 0;

    int64_t curTaskStartTime = 0;
    int64_t earliestStartTime = 0;
    for(const auto& task: sampledTasks) {
      curTaskStartTime = std::max(earliestStartTime, task->creation_time);
      totalServiceTime += 
      /* waiting in queue time after creation */ (curTaskStartTime - task->creation_time) +
      /* on cpu time*/ task->total_runtime;

      earliestStartTime = curTaskStartTime + task->total_runtime;
    }
    return totalServiceTime;
  }

  // add task to back of queue
  void addTask(DynamicTask* task) {
    absl::MutexLock lock(&mu_);
    task->run_state = DynamicTaskState::kQueued;
    if (task->prio_boost) rq.push_front(task);
    else rq.push_back(task);
  }
  
  // Do Nothing? Cuz task isn't in the queue
  void endTask(DynamicTask* task) {
    absl::MutexLock lock(&mu_);
    size_t size = rq.size();
    if (size > 0) {
      // Check if 'task' is at the back of the runqueue (common case).
      size_t pos = size - 1;
      if (rq[pos] == task) {
        rq.erase(rq.cbegin() + pos);
        task->run_state = DynamicTaskState::kRunnable;
        return;
      }
      // Now search for 'task' from the beginning of the runqueue.
      for (pos = 0; pos < size - 1; pos++) {
        if (rq[pos] == task) {
          rq.erase(rq.cbegin() + pos);
          task->run_state =  DynamicTaskState::kRunnable;
          return;
        }
      }
    }
  }

  // Do nothing since task isn't removed
  void preemptTask(DynamicTask* task) {
  }

  size_t rq_size() {
    absl::MutexLock lock(&mu_);
    return rq.size();
  }

  bool isEmpty() { return rq_size() == 0; }
  
  // select front of queue
  DynamicTask* pickNextTask() {
    absl::MutexLock lock(&mu_);
    if (rq.empty()) return nullptr;

    DynamicTask* curTask = rq.front();
    curTask->run_state = DynamicTaskState::kRunnable;
    
    rq.pop_front();
    return curTask;
  }

  // move task to back
  void blockTask(DynamicTask* task) {
    // absl::MutexLock lock(&mu_);
    // curTask = nullptr;
    // rq.push_back(task);
  }

  void loadTasks(std::vector<DynamicTask*> tasks) {
    absl::MutexLock lock(&mu_);
    sort(tasks.begin(), tasks.end(), 
    [](const DynamicTask* t1, const DynamicTask* t2) {
      return t1->creation_time < t2->creation_time;
    });
    for (auto t: tasks) {
      rq.push_back(t);
    }
  }

  std::vector<DynamicTask*> offloadTasks() {
    absl::MutexLock lock(&mu_);
    std::vector<DynamicTask*> tasks;
    while (!rq.empty()) {
      tasks.push_back(rq.front());
      rq.pop_front();
    }
    return tasks;
  }

  int64_t getPreemptionTime() {
    return -1;
  }
};

class RoundRobinSchedPolicy : public DynamicSchedPolicy {
private:
  std::deque<DynamicTask*> prio_rq;
  std::deque<DynamicTask*> rq;
  mutable absl::Mutex mu_;

public:
  int64_t evaluatePolicy(const std::vector<SampledTaskDetails*>& sampledTasks) {
    //  std::cout<<"Evaluating RoundRobin"<<std::endl;
    std::vector<int64_t> remainingTime(sampledTasks.size());
    int64_t currentTime = sampledTasks[0]->creation_time;
    // std::cout << "Start time: " << currentTime << std::endl;
    int lastUnqueuedTaskIdx = 1;
    int64_t totalServiceTime = 0;

    for (int i = 0; i < sampledTasks.size(); ++i) {
      remainingTime[i] = (sampledTasks[i]->total_runtime);
    }

    // std::cout << "Total Run Time Left: " << std::endl;
    // for(auto remaining: remainingTime) {
    //   std::cout << remaining << std::endl;
    // }

    std::queue<int> readyQueue;
    readyQueue.push(0);

    int numFinishedTasks = 0;
    while (numFinishedTasks < sampledTasks.size()) {
      // std::cout << "Beginning Main RR Eval loop: "<<std::endl;
      // std::cout << "RQ:" <<std::endl;
      // auto q = readyQueue;
      // while (!q.empty()) {
      //   std::cout << q.front() << " ";
      //   q.pop();
      // }
      if (readyQueue.empty()) {
        readyQueue.push(numFinishedTasks);
        lastUnqueuedTaskIdx=numFinishedTasks+1;
        currentTime = sampledTasks[numFinishedTasks]->creation_time;
      }
      int curTaskIdx = readyQueue.front(); readyQueue.pop(); // Remove task from run queue
      // std::cout << "Main RR Eval loop: " << curTaskIdx << " " << numFinishedTasks <<" " << sampledTasks.size() << std::endl;

      int timeSlice = std::min(this->getPreemptionTime(), remainingTime[curTaskIdx]);
      remainingTime[curTaskIdx] -= timeSlice;
      currentTime += timeSlice;

      while (lastUnqueuedTaskIdx < sampledTasks.size()) {
        if (sampledTasks[lastUnqueuedTaskIdx]->creation_time <= currentTime) {
          readyQueue.push(lastUnqueuedTaskIdx);
        }
        lastUnqueuedTaskIdx+=1;
      }

      if (remainingTime[curTaskIdx] == 0) {
        totalServiceTime += currentTime - sampledTasks[curTaskIdx]->creation_time;
        // std::cout<<"Task finished in RR simulation:"<<std::endl;
        // std::cout<<curTaskIdx<<" "<<sampledTasks[curTaskIdx]->creation_time<<" "<<currentTime<<" "<<currentTime - sampledTasks[curTaskIdx]->creation_time<<" "<<totalServiceTime<<std::endl;
        numFinishedTasks+=1;
      } else {
        readyQueue.push(curTaskIdx); // Put task at end of run queue
      }
    }
    return totalServiceTime;
  }
  
  // add to back of queue
  void addTask(DynamicTask* task) {
    absl::MutexLock lock(&mu_);
    task->run_state = DynamicTaskState::kQueued;
    if (task->prio_boost) rq.push_back(task);
    else rq.push_back(task);
  }
  
  void endTask(DynamicTask* task) {
    if (task->prio_boost) {
      absl::MutexLock lock(&mu_);
      size_t size = rq.size();
      if (size > 0) {
      // Check if 'task' is at the back of the runqueue (common case).
        size_t pos = size - 1;
        if (rq[pos] == task) {
          rq.erase(rq.cbegin() + pos);
          task->run_state = DynamicTaskState::kRunnable;
          return;
        }
        // Now search for 'task' from the beginning of the runqueue.
        for (pos = 0; pos < size - 1; pos++) {
          if (rq[pos] == task) {
            rq.erase(rq.cbegin() + pos);
            task->run_state =  DynamicTaskState::kRunnable;
            return;
          }
        }
      }
    } else {
      absl::MutexLock lock(&mu_);
      size_t size = rq.size();
      if (size > 0) {
      // Check if 'task' is at the back of the runqueue (common case).
        size_t pos = size - 1;
        if (rq[pos] == task) {
          rq.erase(rq.cbegin() + pos);
          task->run_state = DynamicTaskState::kRunnable;
          return;
        }
        // Now search for 'task' from the beginning of the runqueue.
        for (pos = 0; pos < size - 1; pos++) {
          if (rq[pos] == task) {
            rq.erase(rq.cbegin() + pos);
            task->run_state =  DynamicTaskState::kRunnable;
            return;
          }
        }
      }
    }
  }
  
  void preemptTask(DynamicTask* task) {
    absl::MutexLock lock(&mu_);
    task->run_state = DynamicTaskState::kQueued;
    if (task->prio_boost) rq.push_back(task);
    else rq.push_back(task);
  }

  size_t rq_size() {
    absl::MutexLock lock(&mu_);
    return prio_rq.size() + rq.size();
  }

  bool isEmpty() { return rq_size() == 0; }
  
  // Will be invoked when RR quantum is exhausted
  DynamicTask* pickNextTask() {
    absl::MutexLock lock(&mu_);
    if (!prio_rq.empty()) {
      DynamicTask* curTask = prio_rq.front();
      curTask->run_state = DynamicTaskState::kRunnable;
      prio_rq.pop_front();
      return curTask;
    }

    if (rq.empty()) return nullptr;

    DynamicTask* curTask = rq.front();
    curTask->run_state = DynamicTaskState::kRunnable;
    rq.pop_front();
    return curTask;
  }
  
  void blockTask(DynamicTask* task) {
  };
  
  void loadTasks(std::vector<DynamicTask*> tasks) {
    absl::MutexLock lock(&mu_);
    sort(tasks.begin(), tasks.end(), 
    [](const DynamicTask* t1, const DynamicTask* t2) {
      return t1->creation_time < t2->creation_time;
    });
    for (auto t: tasks) {
      rq.push_back(t);
    }
  };
  
  std::vector<DynamicTask*> offloadTasks() {
    absl::MutexLock lock(&mu_);
    std::vector<DynamicTask*> tasks;
    while (!rq.empty()) {
      tasks.push_back(rq.front());
      rq.pop_front();
    }
    return tasks;
  };

  int64_t getPreemptionTime() {
    return 20000000; // 20 milliseconds
  }
};

DynamicSchedControlModule :: DynamicSchedControlModule() {
  supportedPolicies = std::vector<DynamicSchedPolicy*>{
    new FifoSchedPolicy(),
    new RoundRobinSchedPolicy()
  };
}

DynamicScheduler::DynamicScheduler(Enclave* enclave, CpuList cpulist,
                             std::shared_ptr<TaskAllocator<DynamicTask>> allocator)
    : BasicDispatchScheduler(enclave, std::move(cpulist),
                             std::move(allocator)) {
  for (const Cpu& cpu : cpus()) {
    // TODO: extend Cpu to get numa node.
    int node = 0;
    CpuState* cs = cpu_state(cpu);
    cs->channel = enclave->MakeChannel(GHOST_MAX_QUEUE_ELEMS, node,
                                       MachineTopology()->ToCpuList({cpu}));
    // This channel pointer is valid for the lifetime of DynamicScheduler
    if (!default_channel_) {
      default_channel_ = cs->channel.get();
    }
  }
}

void DynamicScheduler::DumpAllTasks() {
  fprintf(stderr, "task        state   cpu\n");
  allocator()->ForEachTask([](Gtid gtid, const DynamicTask* task) {
    absl::FPrintF(stderr, "%-12s%-8d%-8d%c%c\n", gtid.describe(),
                  task->run_state, task->cpu, task->preempted ? 'P' : '-',
                  task->prio_boost ? 'B' : '-');
    return true;
  });
}

void DynamicScheduler::DumpState(const Cpu& cpu, int flags) {
  if (flags & Scheduler::kDumpAllTasks) {
    DumpAllTasks();
  }

  CpuState* cs = cpu_state(cpu);
  if (!(flags & Scheduler::kDumpStateEmptyRQ) && !cs->current &&
      cs->dynamicSchedControlModule.empty()) {
    return;
  }

  const DynamicTask* current = cs->current;
  absl::FPrintF(stderr, "SchedState[%d]: %s rq_l=%lu\n", cpu.id(),
                current ? current->gtid.describe() : "none", cs->dynamicSchedControlModule.rq_size());
}

void DynamicScheduler::EnclaveReady() {
  for (const Cpu& cpu : cpus()) {
    CpuState* cs = cpu_state(cpu);
    Agent* agent = enclave()->GetAgent(cpu);

    // AssociateTask may fail if agent barrier is stale.
    while (!cs->channel->AssociateTask(agent->gtid(), agent->barrier(),
                                       /*status=*/nullptr)) {
      CHECK_EQ(errno, ESTALE);
    }
  }

  enclave()->SetDeliverTicks(true);
}

/* void DynamicScheduler::CpuTick(const Message& msg) {
  const ghost_msg_payload_cpu_tick* payload =
      static_cast<const ghost_msg_payload_cpu_tick*>(msg.payload());
  Cpu cpu = topology()->cpu(payload->cpu);
  CpuState* cs = cpu_state(cpu);

  // We do not actually need any logic in CpuTick for preemption. Since
  // CpuTick messages wake up the agent, CfsSchedule will eventually be
  // called, which contains the logic for figuring out if we should run the
  // task that was running before we got preempted the agent or if we should
  // reach into our rb tree.
  CheckPreemptTick(cpu);
}

void DynamicScheduler::CheckPreemptTick(const Cpu& cpu) {
  CpuState* cs = cpu_state(cpu);

  if (cs->current && cs->dynamicSchedControlModule.getPreemptionTime() != -1) {
    // If we were on cpu, check if we have run for longer than
    // Granularity(). If so, force picking another task via setting current
    // to nullptr.
    if (cs->current->status_word.runtime() -
                          cs->current->prev_on_cpu_time > cs->dynamicSchedControlModule.getPreemptionTime()) {
      cs->preempt_curr = true;
    }
  }
} */

// Implicitly thread-safe because it is only called from one agent associated
// with the default queue.
Cpu DynamicScheduler::AssignCpu(DynamicTask* task) {
  static auto begin = cpus().begin();
  static auto end = cpus().end();
  static auto next = end;

  if (next == end) {
    next = begin;
  }
  return next++;
}

void DynamicScheduler::Migrate(DynamicTask* task, Cpu cpu, BarrierToken seqnum) {
  CHECK_EQ(task->run_state, DynamicTaskState::kRunnable);
  CHECK_EQ(task->cpu, -1);

  CpuState* cs = cpu_state(cpu);
  const Channel* channel = cs->channel.get();
  CHECK(channel->AssociateTask(task->gtid, seqnum, /*status=*/nullptr));

  GHOST_DPRINT(3, stderr, "Migrating task %s to cpu %d", task->gtid.describe(),
               cpu.id());
  task->cpu = cpu.id();

  // Make task visible in the new runqueue *after* changing the association
  // (otherwise the task can get oncpu while producing into the old queue).
  cs->dynamicSchedControlModule.addTask(task);

  // Get the agent's attention so it notices the new task.
  enclave()->GetAgent(cpu)->Ping();
}

void DynamicScheduler::TaskNew(DynamicTask* task, const Message& msg) {
  const ghost_msg_payload_task_new* payload =
      static_cast<const ghost_msg_payload_task_new*>(msg.payload());

  task->seqnum = msg.seqnum();
  task->run_state = DynamicTaskState::kBlocked;
  task->creation_time = absl::GetCurrentTimeNanos();

  if (payload->runnable) {
    task->run_state = DynamicTaskState::kRunnable;
    Cpu cpu = AssignCpu(task);
    Migrate(task, cpu, msg.seqnum());
  } else {
    // Wait until task becomes runnable to avoid race between migration
    // and MSG_TASK_WAKEUP showing up on the default channel.
  }
}

void DynamicScheduler::TaskRunnable(DynamicTask* task, const Message& msg) {
  const ghost_msg_payload_task_wakeup* payload =
      static_cast<const ghost_msg_payload_task_wakeup*>(msg.payload());

  CHECK(task->blocked());
  task->run_state = DynamicTaskState::kRunnable;

  // A non-deferrable wakeup gets the same preference as a preempted task.
  // This is because it may be holding locks or resources needed by other
  // tasks to make progress.
  task->prio_boost = !payload->deferrable;

  if (task->cpu < 0) {
    // There cannot be any more messages pending for this task after a
    // MSG_TASK_WAKEUP (until the agent puts it oncpu) so it's safe to
    // migrate.
    Cpu cpu = AssignCpu(task);
    Migrate(task, cpu, msg.seqnum());
  } else {
    CpuState* cs = cpu_state_of(task);
    cs->dynamicSchedControlModule.addTask(task);
  }
}

// TODO: Understand what tasks can be called with this api
void DynamicScheduler::TaskDeparted(DynamicTask* task, const Message& msg) {
  // if (task->gtid.describe() == firstTask)
  //   std::cout<<"TASK DEPART: "<<task->gtid.describe()<<std::endl;
  const ghost_msg_payload_task_departed* payload =
      static_cast<const ghost_msg_payload_task_departed*>(msg.payload());

  if (task->oncpu() || payload->from_switchto) {
    TaskOffCpu(task, /*blocked=*/false, payload->from_switchto);
  } else if (task->queued()) {
    CpuState* cs = cpu_state_of(task);
    auto curTime = absl::GetCurrentTimeNanos();
    if (task->prev_on_cpu_time != -1) task->total_runtime += curTime - task->prev_on_cpu_time;
    task->prev_on_cpu_time = -1;
    cs->dynamicSchedControlModule.endTask(task);
  } else {
    CHECK(task->blocked());
  }

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }

  allocator()->FreeTask(task);
}

// TODO: Understand what tasks can be called with this api
void DynamicScheduler::TaskDead(DynamicTask* task, const Message& msg) {
  CHECK(task->blocked());
  CpuState* cs = cpu_state_of(task);
  auto curTime = absl::GetCurrentTimeNanos();
  if (task->prev_on_cpu_time != -1) task->total_runtime += curTime - task->prev_on_cpu_time;
  task->prev_on_cpu_time = -1;
  cs->dynamicSchedControlModule.endTask(task);
  allocator()->FreeTask(task);
}

void DynamicScheduler::TaskYield(DynamicTask* task, const Message& msg) {
  // if (task->gtid.describe() == firstTask)
  //   std::cout<<"TASK YIELD: "<<task->gtid.describe()<<std::endl;
  const ghost_msg_payload_task_yield* payload =
      static_cast<const ghost_msg_payload_task_yield*>(msg.payload());

  TaskOffCpu(task, /*blocked=*/false, payload->from_switchto);

  CpuState* cs = cpu_state_of(task);
  cs->dynamicSchedControlModule.addTask(task);

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }
}

void DynamicScheduler::TaskBlocked(DynamicTask* task, const Message& msg) {
  // if (task->gtid.describe() == firstTask)
  // std::cout<<"TASK BLOCK: "<<task->gtid.describe()<<std::endl;
  const ghost_msg_payload_task_blocked* payload =
      static_cast<const ghost_msg_payload_task_blocked*>(msg.payload());

  TaskOffCpu(task, /*blocked=*/true, payload->from_switchto);
  

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }
}

void DynamicScheduler::TaskPreempted(DynamicTask* task, const Message& msg) {
  // if (task->gtid.describe() == firstTask)
  // std::cout<<"TASK PREEMPT: "<<task->gtid.describe()<<std::endl;
  const ghost_msg_payload_task_preempt* payload =
      static_cast<const ghost_msg_payload_task_preempt*>(msg.payload());
    
  // CpuState* cs = cpu_state_of(task);

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }
}

void DynamicScheduler::TaskSwitchto(DynamicTask* task, const Message& msg) {
  TaskOffCpu(task, /*blocked=*/true, /*from_switchto=*/false);
}


void DynamicScheduler::TaskOffCpu(DynamicTask* task, bool blocked,
                               bool from_switchto) {
  
  auto curTime = absl::GetCurrentTimeNanos();
  if (task->prev_on_cpu_time != -1)
    task->total_runtime += curTime - task->prev_on_cpu_time;
  CpuState* cs = cpu_state_of(task);

  GHOST_DPRINT(3, stderr, "Task %s OFFCPU %d oncpu time: %lld off cpu time: %lld total run time: %lld", task->gtid.describe(), task->cpu, task->prev_on_cpu_time, curTime, task->total_runtime);

  task->prev_on_cpu_time = -1;


  if (task->oncpu()) {
    CHECK_EQ(cs->current, task);
    cs->current = nullptr;
  } else {
    CHECK(from_switchto);
    CHECK_EQ(task->run_state, DynamicTaskState::kBlocked);
  }

  task->run_state =
      blocked ? DynamicTaskState::kBlocked : DynamicTaskState::kRunnable;
}

void DynamicScheduler::TaskOnCpu(DynamicTask* task, Cpu cpu) {
  CpuState* cs = cpu_state(cpu);
  cs->current = task;

  task->run_state = DynamicTaskState::kOnCpu;
  task->cpu = cpu.id();
  task->preempted = false;
  task->prio_boost = false;

  if (task->prev_on_cpu_time == -1) task->prev_on_cpu_time = absl::GetCurrentTimeNanos();

  GHOST_DPRINT(3, stderr, "Task %s oncpu %d time: %lld", task->gtid.describe(), cpu.id(), task->prev_on_cpu_time);
}

void DynamicScheduler::DynamicSchedule(const Cpu& cpu, BarrierToken agent_barrier,
                                 bool prio_boost) {
  CpuState* cs = cpu_state(cpu);
  DynamicTask* curTask = nullptr;
  DynamicTask* nextTask = nullptr;
  bool shouldPreemptCurTask = false;

  if (!prio_boost) {
    // cs->dynamicSchedControlModule.swapScheduler();
    curTask = cs->current;

    // Check if task should be preempted
/*     std::cout<<absl::GetCurrentTimeNanos()<<std::endl;
 */ 
    auto curTime = absl::GetCurrentTimeNanos();
    if (!curTask) {
      // no current task so find a new task to run
      nextTask = cs->dynamicSchedControlModule.pickNextTask();
    }
    else if (cs->dynamicSchedControlModule.getPreemptionTime() > 0 
    && curTask->prev_on_cpu_time != -1
    && curTime - curTask->prev_on_cpu_time >= cs->dynamicSchedControlModule.getPreemptionTime()) {
      // next->total_runtime += curTime - next->prev_on_cpu_time;
      // next->prev_on_cpu_time = -1;
      // cs->dynamicSchedControlModule.preemptTask(next);
      shouldPreemptCurTask = true;
      nextTask = cs->dynamicSchedControlModule.pickNextTask();
    } else {
      // run current task
      nextTask = curTask;
    }

    if (!nextTask) {
      nextTask = curTask;
      shouldPreemptCurTask = false;
    }
  }

  GHOST_DPRINT(3, stderr, "DynamicSchedule %s on %s cpu %d ",
               nextTask ? nextTask->gtid.describe() : "idling",
               prio_boost ? "prio-boosted" : "", cpu.id());

  RunRequest* req = enclave()->GetRunRequest(cpu);
  if (nextTask) {
    // Wait for 'next' to get offcpu before switching to it. This might seem
    // superfluous because we don't migrate tasks past the initial assignment
    // of the task to a cpu. However a SwitchTo target can migrate and run on
    // another CPU behind the agent's back. This is usually undetectable from
    // the agent's pov since the SwitchTo target is blocked and thus !on_rq.
    //
    // However if 'next' happens to be the last task in a SwitchTo chain then
    // it is possible to process TASK_WAKEUP(next) before it has gotten off
    // the remote cpu. The 'on_cpu()' check below handles this scenario.
    //
    // See go/switchto-ghost for more details.
    while (nextTask->status_word.on_cpu()) {
      Pause();
    }

    req->Open({
        .target = nextTask->gtid,
        .target_barrier = nextTask->seqnum,
        .agent_barrier = agent_barrier,
        .commit_flags = COMMIT_AT_TXN_COMMIT,
    });

    if (req->Commit()) {
      // Txn commit succeeded and 'next' is oncpu.
      // if (next->gtid.describe() == firstTask)
        // std::cout << "DynamicSchedule: "<< next->gtid.describe() << std::endl;
      if (shouldPreemptCurTask) {
        TaskOffCpu(curTask, false, false);
        cs->dynamicSchedControlModule.addTask(curTask);
      }
      TaskOnCpu(nextTask, cpu);
    } else {
      GHOST_DPRINT(3, stderr, "DynamicSchedule: commit failed (state=%d)",
                   req->state());

      // curTask @nullable - Current task on the cpu
      // nextTask - Next task on the cpu
      // shouldPreemptCurTask - current on cpu task needs to be preempted
      // current if-else branch - ghost failed to place current task on the cpu, so we need to add it to the runqueue
      if (!curTask) {
        // there wasn't a task on the cpu already, so just add next task to the back of the runq
        cs->dynamicSchedControlModule.addTask(nextTask);
      } else {
        if (curTask == nextTask) {
          // Task was already on the cpu and it was going to run again, so remove it and let a different task run instead
          TaskOffCpu(curTask, /*blocked=*/false, /*from_switchto=*/false);
          cs->dynamicSchedControlModule.addTask(curTask);
        } else {
          // Different task was on the cpu, so take curTask off the cpu and add it to the queue
          // nextTask never made it to the cpu so it add it to the queue too, since we just removed it from the runq
          cs->dynamicSchedControlModule.addTask(nextTask);
          TaskOffCpu(curTask, /*blocked=*/false, /*from_switchto=*/false);
          cs->dynamicSchedControlModule.addTask(curTask);
        }
      }
    }
  } else {
    // If LocalYield is due to 'prio_boost' then instruct the kernel to
    // return control back to the agent when CPU is idle.
    int flags = 0;
    if (prio_boost && (cs->current || !cs->dynamicSchedControlModule.empty())) {
      flags = RTLA_ON_IDLE;
    }
    req->LocalYield(agent_barrier, flags);
  }
}

void DynamicScheduler::Schedule(const Cpu& cpu, const StatusWord& agent_sw) {
  BarrierToken agent_barrier = agent_sw.barrier();
  CpuState* cs = cpu_state(cpu);

  GHOST_DPRINT(3, stderr, "Schedule: agent_barrier[%d] = %d\n", cpu.id(),
               agent_barrier);

  Message msg;
  while (!(msg = Peek(cs->channel.get())).empty()) {
    DispatchMessage(msg);
    Consume(cs->channel.get(), msg);
  }

  DynamicSchedule(cpu, agent_barrier, agent_sw.boosted_priority());
}

/* void DynamicRq::Enqueue(DynamicTask* task) {
  CHECK_GE(task->cpu, 0);
  CHECK_EQ(task->run_state, DynamicTaskState::kRunnable);

  task->run_state = DynamicTaskState::kQueued;

  absl::MutexLock lock(&mu_);
  if (task->prio_boost)
    rq_.push_front(task);
  else
    rq_.push_back(task);
} */

/* DynamicTask* DynamicRq::Dequeue() {
  absl::MutexLock lock(&mu_);
  if (rq_.empty()) return nullptr;

  DynamicTask* task = rq_.front();
  CHECK(task->queued());
  task->run_state = DynamicTaskState::kRunnable;
  rq_.pop_front();
  return task;
} */

/* void DynamicRq::Erase(DynamicTask* task) {
  CHECK_EQ(task->run_state, DynamicTaskState::kQueued);
  absl::MutexLock lock(&mu_);
  size_t size = rq_.size();
  if (size > 0) {
    // Check if 'task' is at the back of the runqueue (common case).
    size_t pos = size - 1;
    if (rq_[pos] == task) {
      rq_.erase(rq_.cbegin() + pos);
      task->run_state = DynamicTaskState::kRunnable;
      return;
    }

    // Now search for 'task' from the beginning of the runqueue.
    for (pos = 0; pos < size - 1; pos++) {
      if (rq_[pos] == task) {
        rq_.erase(rq_.cbegin() + pos);
        task->run_state =  DynamicTaskState::kRunnable;
        return;
      }
    }
  }
  CHECK(false);
} */

std::unique_ptr<DynamicScheduler> MultiThreadedDynamicScheduler(Enclave* enclave,
                                                          CpuList cpulist) {
  auto allocator = std::make_shared<ThreadSafeMallocTaskAllocator<DynamicTask>>();
  auto scheduler = std::make_unique<DynamicScheduler>(enclave, std::move(cpulist),
                                                   std::move(allocator));
  return scheduler;
}

void DynamicAgent::AgentThread() {
  gtid().assign_name("Agent:" + std::to_string(cpu().id()));
  if (verbose() > 1) {
    printf("Agent tid:=%d\n", gtid().tid());
  }
  SignalReady();
  WaitForEnclaveReady();

  PeriodicEdge debug_out(absl::Seconds(1));

  while (!Finished() || !scheduler_->Empty(cpu())) {
    scheduler_->Schedule(cpu(), status_word());

    if (verbose() && debug_out.Edge()) {
      static const int flags = verbose() > 1 ? Scheduler::kDumpStateEmptyRQ : 0;
      if (scheduler_->debug_runqueue_) {
        scheduler_->debug_runqueue_ = false;
        scheduler_->DumpState(cpu(), Scheduler::kDumpAllTasks);
      } else {
        scheduler_->DumpState(cpu(), flags);
      }
    }
  }
}

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



}  //  namespace ghost

