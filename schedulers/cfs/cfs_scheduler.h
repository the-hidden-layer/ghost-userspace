// Copyright 2022 Google LLC
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef GHOST_SCHEDULERS_CFS_CFS_SCHEDULER_H_
#define GHOST_SCHEDULERS_CFS_CFS_SCHEDULER_H_

#include <climits>
#include <cstdint>
#include <iostream>
#include <memory>
#include <ostream>
#include <set>

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "lib/agent.h"
#include "lib/base.h"
#include "lib/scheduler.h"

static const absl::Time start = absl::Now();

namespace ghost {

// We could use just "enum class", but embedding an enum (which is implicitly
// convertable to an int) makes a lot of the debugging code simpler. We could do
// some hackery like static_cast<typename
// std::underlying_type<CfsTaskState>::type>(s) to go to an int, but even so,
// enum classes can't have member functions, which makes it very easy to embed
// debugging asserts.
class CfsTaskState {
 public:
  enum class State : uint32_t {
    kBlocked = 0,  // Task cannot run
    kRunnable,     // Task can run
    kRunning,      // Task is running (up to preemption by the agent itself)
    kDone,         // Task is dead or departed
    kNumStates,
  };

  enum class OnRq : uint32_t {
    kQueued = 0,  // Task is not migrating and can be migrated.
    kMigrating,   // Task is currently migrating and on a migration
                  // queue.
    kNumStates,
  };

  // Make sure state transition asserts work. i.e., Transition is represented
  // as uint64_t bit mask.
  static_assert(static_cast<uint32_t>(State::kNumStates) <=
                sizeof(uint64_t) * CHAR_BIT);
  static_assert(static_cast<uint32_t>(OnRq::kNumStates) <=
                sizeof(uint64_t) * CHAR_BIT);

  explicit CfsTaskState(State state)
      : CfsTaskState(state, OnRq::kMigrating, "") {}
  explicit CfsTaskState(State state, absl::string_view task_name)
      : CfsTaskState(state, OnRq::kMigrating, task_name) {}
  explicit CfsTaskState(State state, OnRq on_rq, absl::string_view task_name)
      : state_(state), on_rq_(on_rq), task_name_(task_name) {}

  // Make sure no one accidentally does something that'll mess up tracking like
  // task.task_state = CfsTaskState(CfsTaskState::kBlocked)
  CfsTaskState(const CfsTaskState&) = delete;
  CfsTaskState& operator=(const CfsTaskState&) = delete;

  // Convenience functions for accessing running state.
  inline bool IsBlocked() const { return state_ == State::kBlocked; }
  inline bool IsRunning() const { return state_ == State::kRunning; }
  inline bool IsRunnable() const { return state_ == State::kRunnable; }
  inline bool IsDone() const { return state_ == State::kDone; }

  // Convenience functions for accessing on_rq state.
  inline bool OnRqQueued() const { return on_rq_ == OnRq::kQueued; }
  inline bool OnRqMigrating() const { return on_rq_ == OnRq::kMigrating; }

  // Accessors.
  State GetState() const { return state_; }
  OnRq GetOnRq() const { return on_rq_; }

  // Only sets the run state without manipulating the migration state. For
  // simplicity, we let a task to make transition either between migration
  // states or run states but not both at the same time.
  void SetState(State state) {
#ifndef NDEBUG
    state_trace_.Insert({.state = state, .on_rq = on_rq_});
    AssertValidTransition(state);
#endif
    state_ = state;
  }

  // Only sets the migration state without manipulating the run state. For
  // simplicity, we let a task to make transition either between migration
  // states or run states but not both at the same time.
  void SetOnRq(OnRq on_rq) {
#ifndef NDEBUG
    state_trace_.Insert({.state = state_, .on_rq = on_rq});
    AssertValidTransition(on_rq);
#endif
    on_rq_ = on_rq;
  }

 private:
  struct FullState {
    State state;
    OnRq on_rq;
  };

  // Minimalistic implementation of circular buffer for the state trace.
  class StateTrace {
   public:
    static constexpr size_t kMaxSize = 20;

    void Insert(const FullState& state) {
      CHECK_LE(size_, array_.size());
      if (size_ == array_.size()) {
        array_[oldest_] = state;
        oldest_ = (oldest_ + 1) % array_.size();
      } else {
        // oldest_ is always zero in this case.
        array_[size_++] = state;
      }
    }

    void ForEach(absl::AnyInvocable<void(const FullState&)> on_each) const {
      for (size_t i = 0; i < size_; i++) {
        on_each(array_[(oldest_ + i) % array_.size()]);
      }
    }

   private:
    std::array<FullState, kMaxSize> array_;
    size_t oldest_ = 0;
    size_t size_ = 0;
  };

#ifndef NDEBUG
  void AssertValidTransition(State next);
  void AssertValidTransition(OnRq next);
  const std::map<State, uint64_t>& GetStateTransitionMap() {
    static const auto* map =
        new std::map<State, uint64_t>{{State::kBlocked, kToBlocked},
                                      {State::kRunnable, kToRunnable},
                                      {State::kRunning, kToRunning},
                                      {State::kDone, kToDone}};
    return *map;
  }

  const std::map<OnRq, uint64_t>& GetOnRqTransitionMap() {
    static const auto* map =
        new std::map<OnRq, uint64_t>{{OnRq::kQueued, kToQueued},
                                     {OnRq::kMigrating, kToMigrating}};
    return *map;
  }
#endif  // !NDEBUG

  // Tracks the running state of this task.
  State state_;
  // Tracks the run queue state of this task.
  OnRq on_rq_;

  absl::string_view task_name_;

#ifndef NDEBUG
  // TODO: Consider minimizing if(n)def NDEBUG blocks.
  StateTrace state_trace_;

  // State Transition Map. Each kToBlah encodes valid states such that we can
  // transition to blah. To validate that we can go from kFoo to kBar, we check
  // that the correct bit it set. e.g. (kToFoo & (1 << kBar)) == 1 iff kBar ->
  // kFoo is valid.
  constexpr static uint64_t kToBlocked =
      (1 << static_cast<uint32_t>(State::kRunning)) +
      (1 << static_cast<uint32_t>(State::kBlocked));
  constexpr static uint64_t kToRunnable =
      (1 << static_cast<uint32_t>(State::kBlocked)) +
      (1 << static_cast<uint32_t>(State::kRunning));
  constexpr static uint64_t kToRunning =
      (1 << static_cast<uint32_t>(State::kRunnable)) +
      (1 << static_cast<uint32_t>(State::kRunning));
  constexpr static uint64_t kToDone =
      (1 << static_cast<uint32_t>(State::kRunnable)) +
      (1 << static_cast<uint32_t>(State::kBlocked)) +
      (1 << static_cast<uint32_t>(State::kRunning));

  constexpr static uint64_t kToQueued =
      1 << static_cast<uint32_t>(OnRq::kMigrating);
  constexpr static uint64_t kToMigrating =
      1 << static_cast<uint32_t>(OnRq::kQueued);
#endif
};

std::ostream& operator<<(std::ostream& os, CfsTaskState::State state);
std::ostream& operator<<(std::ostream& os, CfsTaskState::OnRq state);
std::ostream& operator<<(std::ostream& os, const CfsTaskState& state);

struct CpuState;

struct CfsTask : public Task<> {
  explicit CfsTask(Gtid d_task_gtid, ghost_sw_info sw_info)
      : Task<>(d_task_gtid, sw_info), vruntime(absl::ZeroDuration()) {}
  ~CfsTask() override {}

  // std::multiset expects one to pass a strict (< not <=) weak ordering
  // function as a template parameter. Technically, this doesn't have to be
  // inside of the struct, but it seems logical to keep this here.
  static bool Less(CfsTask* a, CfsTask* b) {
    if (a->vruntime == b->vruntime) {
      return (uintptr_t)a < (uintptr_t)b;
    }
    return a->vruntime < b->vruntime;
  }

  CfsTaskState task_state =
      CfsTaskState(CfsTaskState::State::kBlocked,
                   CfsTaskState::OnRq::kMigrating,
                   gtid.describe());
  int cpu = -1;

  // Nice value and its corresponding weight/inverse-weight values for this
  // task.
  int nice;
  uint32_t weight;
  uint32_t inverse_weight;

  // CPU affinity of this task.
  CpuList cpu_affinity = MachineTopology()->EmptyCpuList();

  // Cfs sorts tasks by vruntime, so we need to keep track of how long a task
  // has been running.
  absl::Duration vruntime;

  // runtime_at_first_pick is how much runtime this task had at its initial
  // picking. This timestamp does not change unless we are put back in the
  // runqueue. IOW, if we bounce between oncpu and put_prev_task_elision_,
  // the timestamp is not reset. The timestamp is used to figure out
  // if a task has run for granularity_ yet.
  uint64_t runtime_at_first_pick_ns;
};

std::ostream& operator<<(std::ostream& os, CfsTaskState::State state);
std::ostream& operator<<(std::ostream& os, const CfsTaskState& state);

class CfsRq {
 public:
  explicit CfsRq();
  CfsRq(const CfsRq&) = delete;
  CfsRq& operator=(CfsRq&) = delete;

  // See CfsRq::granularity_ for a description of how these parameters work.
  void SetMinGranularity(absl::Duration t) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void SetLatency(absl::Duration t) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Returns the length of time that the task should run in real time before it
  // is preempted. This value is equivalent to:
  // IF min_granularity * num_tasks > latency THEN min_granularity
  // ELSE latency / num_tasks
  // The purpose of having granularity is so that even if a task has a lot
  // of vruntime to makeup, it doesn't hog all the cputime.
  // TODO: update this when we introduce nice values.
  // NOTE: This needs to be updated everytime we change the number of tasks
  // associated with the runqueue changes. e.g. simply pulling a task out of
  // rq to give it time on the cpu doesn't require a change as we still manage
  // the same number of tasks. But a task blocking, departing, or adding
  // a new task, does require an update.
  absl::Duration MinPreemptionGranularity() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // PickNextTask checks if prev should run again, and if so, returns prev.
  // Otherwise, it picks the task with the smallest vruntime.
  // PickNextTask also is the sync up point for processing state changes to
  // prev. PickNextTask sets the state of its returned task to kOnCpu.
  CfsTask* PickNextTask(CfsTask* prev, TaskAllocator<CfsTask>* allocator,
                        CpuState* cs) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Enqueues a new task or a task that is coming from being blocked.
  void EnqueueTask(CfsTask* task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Enqueue a task that is transitioning from being on the cpu to off the cpu.
  void PutPrevTask(CfsTask* task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Erase 'task' from the runqueue. Task must be on rq.
  void Erase(CfsTask* task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  size_t Size() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return rq_.size(); }

  bool Empty() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return Size() == 0; }

  // Needs to be called everytime we touch the rq or update a current task's
  // vruntime.
  void UpdateMinVruntime(CpuState* cs) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Protects this runqueue and the state of any task assoicated with the rq.
  mutable absl::Mutex mu_;

 private:
  // Inserts a task into the backing runqueue.
  // Preconditons: task->vruntime has been set to a logical value.
  void InsertTaskIntoRq(CfsTask* task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::Duration min_vruntime_ ABSL_GUARDED_BY(mu_);

  // Unlike in-kernel CFS, we want to have this properties per run-queue instead
  // of system wide.
  absl::Duration min_preemption_granularity_ ABSL_GUARDED_BY(mu_);
  absl::Duration latency_ ABSL_GUARDED_BY(mu_);

  // We use a set as the backing data structure as, according to the
  // C++ standard, it is backed by a red-black tree, which is the backing
  // data structure in CFS in the the kernel. While opaque, using an std::
  // container is easiest way to use a red-black tree short of writing or
  // importing our own.
  std::set<CfsTask*, decltype(&CfsTask::Less)> rq_ ABSL_GUARDED_BY(mu_);
};

struct CpuState {
  // current points to the CfsTask that we most recently picked to run on the
  // cpu. Note, we say most recently picked as a txn could fail leaving us with
  // current pointing to a task that is not currently on cpu.
  CfsTask* current = nullptr;
  // pointer to the kernel ipc queue.
  std::unique_ptr<Channel> channel = nullptr;
  // the run queue responsible from scheduling tasks on this cpu.
  CfsRq run_queue;
  // Should we keep running the current task.
  bool preempt_curr = false;
} ABSL_CACHELINE_ALIGNED;

class CfsScheduler : public BasicDispatchScheduler<CfsTask> {
 public:
  static constexpr int kMaxNice = 19;
  static constexpr int kMinNice = -20;

  // Pre-computed weight values for each nice value. The weight values are
  // the same as ones defined in `/kernel/sched/core.c`. The values are
  // scaled with respect to 1024 for nice value 0.
  static constexpr uint32_t kNiceToWeight[40] = {
      88761, 71755, 56483, 46273, 36291,  // -20 .. -16
      29154, 23254, 18705, 14949, 11916,  // -15 .. -11
      9548,  7620,  6100,  4904,  3906,   // -10 .. -6
      3121,  2501,  1991,  1586,  1277,   // -5 .. -1
      1024,  820,   655,   526,   423,    // 0 .. 4
      335,   272,   215,   172,   137,    // 5 .. 9
      110,   87,    70,    56,    45,     // 10 .. 14
      36,    29,    23,    18,    15      // 15 .. 19
  };

  // Pre-computed inverse weight values for each nice value (2^32/weight). The
  // inverse weight values are the same as ones defined in
  // `/kernel/sched/core.c`. These values are to transform division by the
  // weight values into multiplication by the inverse weights, which works
  // better for integers.
  static constexpr uint32_t kNiceToInverseWeight[40] = {
      48388,     59856,     76040,     92818,     118348,    // -20 .. -16
      147320,    184698,    229616,    287308,    360437,    // -15 .. -11
      449829,    563644,    704093,    875809,    1099582,   // -10 .. -6
      1376151,   1717300,   2157191,   2708050,   3363326,   // -5 .. -1
      4194304,   5237765,   6557202,   8165337,   10153587,  // 0 .. 4
      12820798,  15790321,  19976592,  24970740,  31350126,  // 5 .. 9
      39045157,  49367440,  61356676,  76695844,  95443717,  // 10 .. 14
      119304647, 148102320, 186737708, 238609294, 286331153  // 15 .. 19
  };

  explicit CfsScheduler(Enclave* enclave, CpuList cpulist,
                        std::shared_ptr<TaskAllocator<CfsTask>> allocator,
                        absl::Duration min_granularity, absl::Duration latency);
  ~CfsScheduler() final {}

  void Schedule(const Cpu& cpu, const StatusWord& sw);

  void EnclaveReady() final;
  Channel& GetDefaultChannel() final { return *default_channel_; };

  bool Empty(const Cpu& cpu) {
    CpuState* cs = cpu_state(cpu);
    absl::MutexLock l(&cs->run_queue.mu_);
    bool res = cs->run_queue.Empty();
    return res;
  }

  void DumpState(const Cpu& cpu, int flags) final;
  std::atomic<bool> debug_runqueue_ = false;

  int CountAllTasks() {
    int num_tasks = 0;
    allocator()->ForEachTask([&num_tasks](Gtid gtid, const CfsTask* task) {
      ++num_tasks;
      return true;
    });
    return num_tasks;
  }

  static constexpr int kDebugRunqueue = 1;
  static constexpr int kCountAllTasks = 2;

 protected:
  void TaskNew(CfsTask* task, const Message& msg) final;
  void TaskRunnable(CfsTask* task, const Message& msg) final;
  void TaskDeparted(CfsTask* task, const Message& msg) final;
  void TaskDead(CfsTask* task, const Message& msg) final;
  void TaskYield(CfsTask* task, const Message& msg) final;
  void TaskBlocked(CfsTask* task, const Message& msg) final;
  void TaskPreempted(CfsTask* task, const Message& msg) final;
  void TaskSwitchto(CfsTask* task, const Message& msg) final;
  void TaskAffinityChanged(CfsTask* task, const Message& msg) final;
  void TaskPriorityChanged(CfsTask* task, const Message& msg) final;
  void CpuTick(const Message& msg) final;

 private:
  // Empties the channel associated with cpu and dispatches the messages.
  void DrainChannel(const Cpu& cpu);

  // Checks if we should preempt the current task. If so, sets preempt_curr_.
  void CheckPreemptTick(const Cpu& cpu);

  // CfsSchedule looks at the current cpu state and its run_queue, decides what
  // to run next, and then commits a txn. REQUIRES: Called after all messages
  // have been ack'ed otherwise the txn will fail.
  void CfsSchedule(const Cpu& cpu, BarrierToken agent_barrier, bool prio_boost);

  // HandleTaskDone is responsible for remvoing a task from the run queue and
  // freeing it if it is currently !cs->current, otherwise, it defers the
  // freeing to PickNextTask.
  void HandleTaskDone(CfsTask* task, bool from_switchto);

  // Migrate takes task and places it on cpu's run queue.
  void Migrate(CfsTask* task, Cpu cpu, BarrierToken seqnum);
  Cpu SelectTaskRq(CfsTask* task);
  void DumpAllTasks();

  void PingCpu(const Cpu& cpu);

  CpuState* cpu_state(const Cpu& cpu) { return &cpu_states_[cpu.id()]; }

  CpuState* cpu_state_of(const CfsTask* task) {
    CHECK_GE(task->cpu, 0);
    CHECK_LT(task->cpu, MAX_CPUS);
    return &cpu_states_[task->cpu];
  }

  // If called with is_agent_thread = true, then we use the cache'd TLS cpu id
  // as agent threads are local to a single cpu, otherwise, issue a syscall.
  int MyCpu(bool is_agent_thread = true) {
    if (!is_agent_thread) return sched_getcpu();
    // "When thread_local is applied to a variable of block scope the
    // storage-class-specifier static is implied if it does not appear
    // explicitly" - C++ standard.
    // This isn't obvious, so keep the static modifier.
    static thread_local int my_cpu = -1;
    if (my_cpu == -1) {
      my_cpu = sched_getcpu();
    }
    return my_cpu;
  }

  CpuState cpu_states_[MAX_CPUS];
  Channel* default_channel_ = nullptr;

  absl::Duration min_granularity_;
  absl::Duration latency_;

  friend class CfsRq;
};

std::unique_ptr<CfsScheduler> MultiThreadedCfsScheduler(
    Enclave* enclave, CpuList cpulist, absl::Duration min_granularity,
    absl::Duration latency);
class CfsAgent : public LocalAgent {
 public:
  CfsAgent(Enclave* enclave, Cpu cpu, CfsScheduler* scheduler)
      : LocalAgent(enclave, cpu), scheduler_(scheduler) {}

  void AgentThread() override;
  Scheduler* AgentScheduler() const override { return scheduler_; }

 private:
  CfsScheduler* scheduler_;
};

class CfsConfig : public AgentConfig {
 public:
  CfsConfig() {}
  CfsConfig(Topology* topology, CpuList cpulist)
      : AgentConfig(topology, std::move(cpulist)) {
    tick_config_ = CpuTickConfig::kAllTicks;
  }
  CfsConfig(Topology* topology, CpuList cpulist, absl::Duration min_granularity,
            absl::Duration latency)
      : AgentConfig(topology, std::move(cpulist)),
        min_granularity_(min_granularity),
        latency_(latency) {
    tick_config_ = CpuTickConfig::kAllTicks;
  }

  absl::Duration min_granularity_;
  absl::Duration latency_;
};

// TODO: Pull these classes out into different files.
template <class EnclaveType>
class FullCfsAgent : public FullAgent<EnclaveType> {
 public:
  explicit FullCfsAgent(CfsConfig config) : FullAgent<EnclaveType>(config) {
    scheduler_ =
        MultiThreadedCfsScheduler(&this->enclave_, *this->enclave_.cpus(),
                                  config.min_granularity_, config.latency_);
    this->StartAgentTasks();
    this->enclave_.Ready();
  }

  ~FullCfsAgent() override { this->TerminateAgentTasks(); }

  std::unique_ptr<Agent> MakeAgent(const Cpu& cpu) override {
    return std::make_unique<CfsAgent>(&this->enclave_, cpu, scheduler_.get());
  }

  void RpcHandler(int64_t req, const AgentRpcArgs& args,
                  AgentRpcResponse& response) override {
    switch (req) {
      case CfsScheduler::kDebugRunqueue:
        scheduler_->debug_runqueue_ = true;
        response.response_code = 0;
        return;
      case CfsScheduler::kCountAllTasks:
        response.response_code = scheduler_->CountAllTasks();
        return;
      default:
        response.response_code = -1;
        return;
    }
  }

 private:
  std::unique_ptr<CfsScheduler> scheduler_;
};

}  // namespace ghost

#endif  // GHOST_SCHEDULERS_CFS_CFS_SCHEDULER_H_
