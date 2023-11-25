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

struct CpuState {
  DynamicTask* current = nullptr;
  std::unique_ptr<Channel> channel = nullptr;
  DynamicRq run_queue;
};

class DynamicScheduler : public BasicDispatchScheduler<DynamicTask> {
  public:
  explicit DynamicScheduler(Enclave* enclave, CpuList cpulist,
                         std::shared_ptr<TaskAllocator<DynamicTask>> allocator,
                         absl::Duration scheduler_swap_granularity, absl::Duration rr_min_granularity);
  ~DynamicScheduler() final;
  void EnclaveReady() final;
  Channel& GetDefaultChannel() final { return *default_channel_; };

  static constexpr int kDebugRunqueue = 1;
  static constexpr int kCountAllTasks = 2;


  bool Empty(const Cpu& cpu) {
    CpuState* cs = &cpu_states_[cpu.id()];
    return cs->run_queue.Empty();
  }

  void DumpState(const Cpu& cpu, int flags) final;
  std::atomic<bool> debug_runqueue_ = false;

    int CountAllTasks() const {
    int num_tasks = 0;
    allocator()->ForEachTask([&num_tasks](Gtid gtid, const DynamicTask* task) {
      ++num_tasks;
      return true;
    });
    return num_tasks;
  }

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
  CpuState cpu_states_[MAX_CPUS];
  Channel* default_channel_ = nullptr;
};

std::unique_ptr<DynamicScheduler> MultiThreadedDynamicScheduler(
    Enclave* enclave, CpuList cpulist, absl::Duration scheduler_swap_granularity,
    absl::Duration rr_min_granularity);

class DynamicAgent : public LocalAgent {
 public:
  DynamicAgent(Enclave* enclave, Cpu cpu, DynamicScheduler* scheduler)
      : LocalAgent(enclave, cpu), scheduler_(scheduler) {}

  void AgentThread() override;
  Scheduler* AgentScheduler() const override { return scheduler_; }

 private:
  DynamicScheduler* scheduler_;
};

class DynamicConfig : public AgentConfig {
 public:
  DynamicConfig() {}
  DynamicConfig(Topology* topology, CpuList cpulist)
      : AgentConfig(topology, std::move(cpulist)) {}
  DynamicConfig(Topology* topology, CpuList cpulist, absl::Duration scheduler_swap_granularity,
            absl::Duration rr_min_granularity)
      : AgentConfig(topology, std::move(cpulist)),
        scheduler_swap_granularity_(scheduler_swap_granularity),
        rr_min_granularity_(rr_min_granularity) {}

  absl::Duration scheduler_swap_granularity_;
  absl::Duration rr_min_granularity_;
};

template <class EnclaveType>
class FullDynamicAgent : public FullAgent<EnclaveType> {
  public:
  explicit FullDynamicAgent(DynamicConfig config) : FullAgent<EnclaveType>(config) {
    scheduler_ =
        MultiThreadedDynamicScheduler(&this->enclave_, *this->enclave_.cpus(),
                                  config.scheduler_swap_granularity_, config.rr_min_granularity_);
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
