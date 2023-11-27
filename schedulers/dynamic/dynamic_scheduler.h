#ifndef GHOST_SCHEDULERS_DYNAMIC_DYNAMIC_SCHEDULER_H
#define GHOST_SCHEDULERS_DYNAMIC_DYNAMIC_SCHEDULER_H

#include <deque>
#include <memory>

#include "lib/agent.h"
#include "lib/scheduler.h"

namespace ghost {

enum class DynamicTaskState {
  kBlocked,   // not on runqueue.
  kRunnable,  // transitory state:
              // 1. kBlocked->kRunnable->kQueued
              // 2. kQueued->kRunnable->kOnCpu
  kQueued,    // on runqueue.
  kOnCpu,     // running on cpu.
};

// For CHECK and friends.
std::ostream& operator<<(std::ostream& os, const DynamicTaskState& state);

struct DynamicTask : public Task<> {
  explicit DynamicTask(Gtid dynamic_task_gtid, ghost_sw_info sw_info)
      : Task<>(dynamic_task_gtid, sw_info) {}
  ~DynamicTask() override {}

  inline bool blocked() const { return run_state == DynamicTaskState::kBlocked; }
  inline bool queued() const { return run_state == DynamicTaskState::kQueued; }
  inline bool oncpu() const { return run_state == DynamicTaskState::kOnCpu; }

  // N.B. _runnable() is a transitory state typically used during runqueue
  // manipulation. It is not expected to be used from task msg callbacks.
  //
  // If you are reading this then you probably want to take a closer look
  // at queued() instead.
  inline bool _runnable() const {
    return run_state == DynamicTaskState::kRunnable;
  }

  DynamicTaskState run_state = DynamicTaskState::kBlocked;
  int cpu = -1;

  // Whether the last execution was preempted or not.
  bool preempted = false;

  // A task's priority is boosted on a kernel preemption or a !deferrable
  // wakeup - basically when it may be holding locks or other resources
  // that prevent other tasks from making progress.
  bool prio_boost = false;

  int64_t creation_time = 0;
  int64_t prev_on_cpu_time = 0;
  int64_t total_runtime = 0;
  int64_t total_time = 0;
};

class DynamicRq {
 public:
  DynamicRq() = default;
  DynamicRq(const DynamicRq&) = delete;
  DynamicRq& operator=(DynamicRq&) = delete;

  DynamicTask* Dequeue();
  void Enqueue(DynamicTask* task);

  // Erase 'task' from the runqueue.
  //
  // Caller must ensure that 'task' is on the runqueue in the first place
  // (e.g. via task->queued()).
  void Erase(DynamicTask* task);

  size_t Size() const {
    absl::MutexLock lock(&mu_);
    return rq_.size();
  }

  bool Empty() const { return Size() == 0; }

 private:
  mutable absl::Mutex mu_;
  std::deque<DynamicTask*> rq_ ABSL_GUARDED_BY(mu_);
};

class DynamicScheduler : public BasicDispatchScheduler<DynamicTask> {
 public:
  explicit DynamicScheduler(Enclave* enclave, CpuList cpulist,
                         std::shared_ptr<TaskAllocator<DynamicTask>> allocator);
  ~DynamicScheduler() final {}

  void Schedule(const Cpu& cpu, const StatusWord& sw);

  void EnclaveReady() final;
  Channel& GetDefaultChannel() final { return *default_channel_; };

  bool Empty(const Cpu& cpu) {
    CpuState* cs = cpu_state(cpu);
    return cs->run_queue.Empty();
  }

  void DumpState(const Cpu& cpu, int flags) final;
  std::atomic<bool> debug_runqueue_ = false;

  int CountAllTasks() {
    int num_tasks = 0;
    allocator()->ForEachTask([&num_tasks](Gtid gtid, const DynamicTask* task) {
      ++num_tasks;
      return true;
    });
    return num_tasks;
  }

  static constexpr int kDebugRunqueue = 1;
  static constexpr int kCountAllTasks = 2;

 protected:
  void TaskNew(DynamicTask* task, const Message& msg) final;
  void TaskRunnable(DynamicTask* task, const Message& msg) final;
  void TaskDeparted(DynamicTask* task, const Message& msg) final;
  void TaskDead(DynamicTask* task, const Message& msg) final;
  void TaskYield(DynamicTask* task, const Message& msg) final;
  void TaskBlocked(DynamicTask* task, const Message& msg) final;
  void TaskPreempted(DynamicTask* task, const Message& msg) final;
  void TaskSwitchto(DynamicTask* task, const Message& msg) final;

 private:
  void DynamicSchedule(const Cpu& cpu, BarrierToken agent_barrier,
                    bool prio_boosted);
  void TaskOffCpu(DynamicTask* task, bool blocked, bool from_switchto);
  void TaskOnCpu(DynamicTask* task, Cpu cpu);
  void Migrate(DynamicTask* task, Cpu cpu, BarrierToken seqnum);
  Cpu AssignCpu(DynamicTask* task);
  void DumpAllTasks();

  struct CpuState {
    DynamicTask* current = nullptr;
    std::unique_ptr<Channel> channel = nullptr;
    DynamicRq run_queue;
  } ABSL_CACHELINE_ALIGNED;

  inline CpuState* cpu_state(const Cpu& cpu) { return &cpu_states_[cpu.id()]; }

  inline CpuState* cpu_state_of(const DynamicTask* task) {
    CHECK_GE(task->cpu, 0);
    CHECK_LT(task->cpu, MAX_CPUS);
    return &cpu_states_[task->cpu];
  }

  CpuState cpu_states_[MAX_CPUS];
  Channel* default_channel_ = nullptr;
};

std::unique_ptr<DynamicScheduler> MultiThreadedDynamicScheduler(Enclave* enclave,
                                                          CpuList cpulist);
class DynamicAgent : public LocalAgent {
 public:
  DynamicAgent(Enclave* enclave, Cpu cpu, DynamicScheduler* scheduler)
      : LocalAgent(enclave, cpu), scheduler_(scheduler) {}

  void AgentThread() override;
  Scheduler* AgentScheduler() const override { return scheduler_; }

 private:
  DynamicScheduler* scheduler_;
};

template <class EnclaveType>
class FullDynamicAgent : public FullAgent<EnclaveType> {
 public:
  explicit FullDynamicAgent(AgentConfig config) : FullAgent<EnclaveType>(config) {
    scheduler_ =
        MultiThreadedDynamicScheduler(&this->enclave_, *this->enclave_.cpus());
    this->StartAgentTasks();
    this->enclave_.Ready();
  }

  ~FullDynamicAgent() override {
    this->TerminateAgentTasks();
  }

  std::unique_ptr<Agent> MakeAgent(const Cpu& cpu) override {
    return std::make_unique<DynamicAgent>(&this->enclave_, cpu, scheduler_.get());
  }

  void RpcHandler(int64_t req, const AgentRpcArgs& args,
                  AgentRpcResponse& response) override {
    switch (req) {
      case DynamicScheduler::kDebugRunqueue:
        scheduler_->debug_runqueue_ = true;
        response.response_code = 0;
        return;
      case DynamicScheduler::kCountAllTasks:
        response.response_code = scheduler_->CountAllTasks();
        return;
      default:
        response.response_code = -1;
        return;
    }
  }

 private:
  std::unique_ptr<DynamicScheduler> scheduler_;
};

}  // namespace ghost

#endif  // GHOST_SCHEDULERS_DYNAMIC_DYNAMIC_SCHEDULER_H
