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
- 

CODING
1. eval RR
2. preemption api
3. change cpu data structure to maintain controle module instead of run queue
4. look into locks for scheduling data structures
5. inject control module into ghost apis

BENCHMARKS

*/

#include "dynamic_scheduler.h"

#include <memory>

namespace ghost {

class DynamicSchedPolicy {
public:
  virtual int64_t evaluatePolicy(const std::vector<DynamicTask*>& sampledTasks) = 0;
  virtual void addTask(DynamicTask* task) = 0;
  virtual void endTask(DynamicTask* task) = 0;
  virtual void preemptTask(DynamicTask* task) = 0;
  virtual DynamicTask* pickNextTask() = 0;
  virtual void blockTask(DynamicTask* task) = 0;
  virtual void loadTasks(const std::vector<DynamicTask*>& tasks) = 0;
  virtual std::vector<DynamicTask*> offloadTasks() = 0;
};

/**
 * TODO:
 * Maintain the list of tasks (the rq)
 * Implement the apis to preempt, add, block, sched tasks (including manipulating the rq) 
*/
class FifoSchedPolicy : public DynamicSchedPolicy {
private:
    std::deque<DynamicTask*> rq;

public:
  int64_t evaluatePolicy(const std::vector<DynamicTask*>& sampledTasks) {
    sort(sampledTasks.begin(), sampledTasks.end(), 
    [](const DynamicTask* t1, const DynamicTask* t2) {
      return t1->creation_time < t2->creation_time;
    });
    int64_t totalServiceTime = 0;
    int64_t taskPossibleStartTime = 0;
    for(const auto& task: sampledTasks) {
      taskPossibleStartTime = std::max(taskPossibleStartTime, task->creation_time);
      totalServiceTime += task->total_runtime + taskPossibleStartTime - task->creation_time;
    }
    return totalServiceTime;
  }

  // add task to back of queue
  void addTask(DynamicTask* task) {
    rq.emplace_back(task);
  }
  
  // TODO: Do Nothing? Cuz task isn't in the queue
  void endTask(DynamicTask* task) {
  }

  // Do nothing since task isn't removed
  void preemptTask(DynamicTask* task) {
    rq.emplace_front(task);
  }
  
  // select front of queue
  DynamicTask* pickNextTask() { 
    if (rq.empty()) return nullptr;
    auto frontTask = rq.front();
    rq.pop_front();
    return frontTask;
  }

  // move task to back
  void blockTask(DynamicTask* task) {
    rq.emplace_back(task);
  }

  void loadTasks(const std::vector<DynamicTask*>& tasks) {
    sort(tasks.begin(), tasks.end(), 
    [](const DynamicTask* t1, const DynamicTask* t2) {
      return t1->creation_time < t2->creation_time;
    });
    for (auto t: tasks) {
      rq.emplace_back(t);
    }
  }

  std::vector<DynamicTask*> offloadTasks() {
    std::vector<DynamicTask*> tasks;
    while (!rq.empty()) {
      tasks.emplace_back(rq.front());
      rq.pop_front();
    }
    return tasks;
  }

  int64_t getPreemptionTime() {
    return -1;
  }
};

/**
 * TODO:
 * Maintain the list of tasks (the rq)
 * Implement the apis to preempt, add, block, sched tasks (including manipulating the rq) 
*/
class RoundRobinSchedPolicy : public DynamicSchedPolicy {
private:
  std::deque<DynamicTask*> rq;

public:
  // TODO: add the right logic related to service time calculation
  int64_t evaluatePolicy(const std::vector<DynamicTask*>& sampledTasks) {
    int64_t eval = 0;
    sort(sampledTasks.begin(), sampledTasks.end(), 
    [](const DynamicTask* t1, const DynamicTask* t2) {
      return t1->creation_time < t2->creation_time;
    });

    // not actual code

    int64_t work_left_for_first = 0;
    // first task finishes before second task arrives

    auto cur_time = sampledTasks[0]->creation_time;
    
    int left = 0, right = 0;

    std::vector<int64_t> workRemaining(sampledTasks.size(), 0);
    workRemaining[0] = sampledTasks[0]->total_runtime;
    while (left < sampledTasks.size()) {
      // Check if left most task is done and if so, move left pointer
      while (left < sampledTasks.size() && workRemaining[left] <= 0) {
        eval += cur_time - sampledTasks[left]->creation_time;
        ++left;
      }

      // Move right pointer if cur time is >= sampledTasks[right]->creationTime
      if (right < sampledTasks.size()-1 && cur_time >= sampledTasks[right+1]->creation_time) {
        ++right;
      }
    }

    if (sampledTasks[0]->creation_time + sampledTasks[0]->total_runtime <= sampledTasks[1]->creation_time) {
      eval += sampledTasks[0]->total_runtime;
    } else {
      work_left_for_first = sampledTasks[0]->creation_time + sampledTasks[0]->total_runtime - sampledTasks[1]->creation_time;
      eval += (sampledTasks[1]->creation_time - sampledTasks[0]->creation_time);
    }


    return eval;
  }
  
  // add to back of queue
  void addTask(DynamicTask* task) {
    rq.emplace_back(task);
  }
  
  void endTask(DynamicTask* task) {};
  
  void preemptTask(DynamicTask* task) {
    rq.emplace_back(task);
  }
  
  // Will be invoked when RR quantum is exhausted
  DynamicTask* pickNextTask() {
    if (rq.empty()) return nullptr;
    auto frontTask = rq.front();
    rq.pop_front();
    return frontTask;
  }
  
  void blockTask(DynamicTask* task) {
    rq.emplace_back(task);
  };
  
  void loadTasks(const std::vector<DynamicTask*>& tasks) {};
  
  std::vector<DynamicTask*> offloadTasks() {
    std::vector<DynamicTask*> tasks;
    while (!rq.empty()) {
      tasks.emplace_back(rq.front());
      rq.pop_front();
    }
    return tasks;
  };

  int64_t getPreemptionTime() {
    return 5e6; // 5 million nanoseconds (5 miliseconds)
  }
};

/**
 * TODO:
 * Compose the current Sched policy - Done (maintaining idx)
 * Maintain the list of supported sched policies - Done
 * Maintain a list of sampledtasks for that cpu (since this class will be composed by the cpu) - Done
 * Expose apis to hide the sched policy apis - Done
 * Expose a swapsched api, that will call each of the supported policies's evaluate apis on the sampled tasks - Done
 * and then swap policies if another policy is better - Done
*/
class DynamicSchedControlModule {
  public:

  std::vector<DynamicTask*> sampledTasks;
  int curPolicyIdx = 0;
  std::vector<DynamicSchedPolicy*> supportedPolicies = std::vector<DynamicSchedPolicy*>{
    new FifoSchedPolicy(),
    new RoundRobinSchedPolicy()
  };

  void addTask(DynamicTask* task) {
    this->supportedPolicies[curPolicyIdx]->addTask(task);
  }

  void endTask(DynamicTask* task) {
    this->sampledTasks.push_back(task);
    this->supportedPolicies[curPolicyIdx]->endTask(task);
  }

  void preemptTask(DynamicTask* task) {
    this->supportedPolicies[curPolicyIdx]->preemptTask(task);
  }

  DynamicTask* pickNextTask() { 
    return this->supportedPolicies[curPolicyIdx]->pickNextTask();
  }

  void blockTask(DynamicTask* task) {
    this->supportedPolicies[curPolicyIdx]->preemptTask(task);
  }

  void swapScheduler() {
    // TODO: evaluate all supported policies against sampled tasks
    int bestPolicyIdx = 0;
    int64_t bestPolicyEvaluation = this->supportedPolicies[0]->evaluatePolicy(sampledTasks);

    for(int policyIdx=1; policyIdx < this->supportedPolicies.size(); policyIdx++) {
      int64_t curPolicyEvaluation = this->supportedPolicies[policyIdx]->evaluatePolicy(sampledTasks);

      if (curPolicyEvaluation < bestPolicyEvaluation) {
        bestPolicyIdx = policyIdx;
        bestPolicyEvaluation = curPolicyEvaluation;
      }
    }

    if (this->curPolicyIdx == bestPolicyIdx) return; // No use in swapping schedulers

    // TODO: Load current tasks into new current policy
    auto tasks = this->supportedPolicies[curPolicyIdx]->offloadTasks();
    this->supportedPolicies[bestPolicyIdx]->loadTasks(tasks);

    // TODO: Update the current policy
    this->curPolicyIdx = bestPolicyIdx;
  }
};

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
      cs->run_queue.Empty()) {
    return;
  }

  const DynamicTask* current = cs->current;
  const DynamicRq* rq = &cs->run_queue;
  absl::FPrintF(stderr, "SchedState[%d]: %s rq_l=%lu\n", cpu.id(),
                current ? current->gtid.describe() : "none", rq->Size());
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
  cs->run_queue.Enqueue(task);

  // Get the agent's attention so it notices the new task.
  enclave()->GetAgent(cpu)->Ping();
}

std::string firstTask = "";
void DynamicScheduler::TaskNew(DynamicTask* task, const Message& msg) {
  if (firstTask == "") {
    firstTask = task->gtid.describe();
  }
  if (task->gtid.describe() == firstTask) {
    std::cout<<"TASK NEW: "<<task->gtid.describe()<<std::endl;
  }
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
    cs->run_queue.Enqueue(task);
  }
}

void DynamicScheduler::TaskDeparted(DynamicTask* task, const Message& msg) {
  if (task->gtid.describe() == firstTask)
    std::cout<<"TASK DEPART: "<<task->gtid.describe()<<std::endl;
  const ghost_msg_payload_task_departed* payload =
      static_cast<const ghost_msg_payload_task_departed*>(msg.payload());

  if (task->oncpu() || payload->from_switchto) {
    TaskOffCpu(task, /*blocked=*/false, payload->from_switchto);
  } else if (task->queued()) {
    CpuState* cs = cpu_state_of(task);
    cs->run_queue.Erase(task);
  } else {
    CHECK(task->blocked());
  }

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }

  allocator()->FreeTask(task);
}

void DynamicScheduler::TaskDead(DynamicTask* task, const Message& msg) {
  task->total_time = absl::GetCurrentTimeNanos() - task->creation_time;
  if (task->gtid.describe() == firstTask)
    std::cout<<"TASK DEAD: "<<task->gtid.describe()<< " " << task->creation_time << " " << task->total_runtime << " " << task->total_time << std::endl;
  CHECK(task->blocked());
  allocator()->FreeTask(task);
}

void DynamicScheduler::TaskYield(DynamicTask* task, const Message& msg) {
  if (task->gtid.describe() == firstTask)
    std::cout<<"TASK YIELD: "<<task->gtid.describe()<<std::endl;
  const ghost_msg_payload_task_yield* payload =
      static_cast<const ghost_msg_payload_task_yield*>(msg.payload());

  TaskOffCpu(task, /*blocked=*/false, payload->from_switchto);

  CpuState* cs = cpu_state_of(task);
  cs->run_queue.Enqueue(task); // TODO: Change this to ask the control module to place it in the queue based on scheduling policy

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }
}

void DynamicScheduler::TaskBlocked(DynamicTask* task, const Message& msg) {
  if (task->gtid.describe() == firstTask)
  std::cout<<"TASK BLOCK: "<<task->gtid.describe()<<std::endl;
  const ghost_msg_payload_task_blocked* payload =
      static_cast<const ghost_msg_payload_task_blocked*>(msg.payload());

  TaskOffCpu(task, /*blocked=*/true, payload->from_switchto);

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }
}

void DynamicScheduler::TaskPreempted(DynamicTask* task, const Message& msg) {
  if (task->gtid.describe() == firstTask)
  std::cout<<"TASK PREEMPT: "<<task->gtid.describe()<<std::endl;
  const ghost_msg_payload_task_preempt* payload =
      static_cast<const ghost_msg_payload_task_preempt*>(msg.payload());

  TaskOffCpu(task, /*blocked=*/false, payload->from_switchto);

  task->preempted = true;
  task->prio_boost = true;
  CpuState* cs = cpu_state_of(task);
  cs->run_queue.Enqueue(task); // TODO: Change this to ask the control module to place it in the queue based on scheduling policy

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
  GHOST_DPRINT(3, stderr, "Task %s offcpu %d", task->gtid.describe(),
               task->cpu);
  if (task->gtid.describe() == firstTask) {
    std::cout<<"TASK OFF CPU: "<<task->gtid.describe()<<std::endl;
  }

  
  task->total_runtime += absl::GetCurrentTimeNanos() - task->prev_on_cpu_time;
  CpuState* cs = cpu_state_of(task);

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

  GHOST_DPRINT(3, stderr, "Task %s oncpu %d", task->gtid.describe(), cpu.id());
  if (task->gtid.describe() == firstTask)
  std::cout << "TASK ON CPU: "<< task->gtid.describe() << " " << cpu.id() << std::endl;

  task->run_state = DynamicTaskState::kOnCpu;
  task->cpu = cpu.id();
  task->preempted = false;
  task->prio_boost = false;

  task->prev_on_cpu_time = absl::GetCurrentTimeNanos();
}

void DynamicScheduler::DynamicSchedule(const Cpu& cpu, BarrierToken agent_barrier,
                                 bool prio_boost) {
  CpuState* cs = cpu_state(cpu);
  DynamicTask* next = nullptr;
  if (!prio_boost) {
    // TODO: Ask control module to pick the task here by applying the scheduling policy
    next = cs->current;
    if (!next) next = cs->run_queue.Dequeue();
  }

  GHOST_DPRINT(3, stderr, "DynamicSchedule %s on %s cpu %d ",
               next ? next->gtid.describe() : "idling",
               prio_boost ? "prio-boosted" : "", cpu.id());

  RunRequest* req = enclave()->GetRunRequest(cpu);
  if (next) {
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
    while (next->status_word.on_cpu()) {
      Pause();
    }

    req->Open({
        .target = next->gtid,
        .target_barrier = next->seqnum,
        .agent_barrier = agent_barrier,
        .commit_flags = COMMIT_AT_TXN_COMMIT,
    });

    if (req->Commit()) {
      // Txn commit succeeded and 'next' is oncpu.
      if (next->gtid.describe() == firstTask)
        std::cout << "DynamicSchedule: "<< next->gtid.describe() << std::endl;
      TaskOnCpu(next, cpu);
    } else {
      GHOST_DPRINT(3, stderr, "DynamicSchedule: commit failed (state=%d)",
                   req->state());

      if (next == cs->current) {
        std::cout<<"ERROR CASE CAME HERE"<<std::endl;
        TaskOffCpu(next, /*blocked=*/false, /*from_switchto=*/false);
      }

      // Txn commit failed so push 'next' to the front of runqueue.
      next->prio_boost = true;
      cs->run_queue.Enqueue(next);
    }
  } else {
    // If LocalYield is due to 'prio_boost' then instruct the kernel to
    // return control back to the agent when CPU is idle.
    int flags = 0;
    if (prio_boost && (cs->current || !cs->run_queue.Empty())) {
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

void DynamicRq::Enqueue(DynamicTask* task) {
  CHECK_GE(task->cpu, 0);
  CHECK_EQ(task->run_state, DynamicTaskState::kRunnable);

  task->run_state = DynamicTaskState::kQueued;

  absl::MutexLock lock(&mu_);
  if (task->prio_boost)
    rq_.push_front(task);
  else
    rq_.push_back(task);
}

DynamicTask* DynamicRq::Dequeue() {
  absl::MutexLock lock(&mu_);
  if (rq_.empty()) return nullptr;

  DynamicTask* task = rq_.front();
  CHECK(task->queued());
  task->run_state = DynamicTaskState::kRunnable;
  rq_.pop_front();
  return task;
}

void DynamicRq::Erase(DynamicTask* task) {
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
}

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

// Requires sampledTasks to be sorted in order of task creation time
// TODO - This is incorrect
int64_t round_robin_total_service_time(const std::vector<DynamicTask*>& sampledTasks) {
  if (sampledTasks.size() == 0) return 0;

  int64_t totalServiceTime = 0;
  int64_t taskPossibleStartTime = 0;
  for(const auto& task: sampledTasks) {
    taskPossibleStartTime = std::max(taskPossibleStartTime, task->creation_time);
    totalServiceTime += task->total_runtime + taskPossibleStartTime - task->creation_time;
  }

  return totalServiceTime;
}



}  //  namespace ghost

