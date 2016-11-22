#include "queue.h"

#include "../utils/format.h"

#define DEFAULT_QUEUE_SIZE 1024

const Commands<Module> Queue::cmds = {
    {"set_burst", MODULE_FUNC &Queue::CommandSetBurst, 1},
    {"set_size", MODULE_FUNC &Queue::CommandSetSize, 0},
};

const PbCommands Queue::pb_cmds = {
    {"set_burst", "QueueCommandSetBurstArg",
     MODULE_CMD_FUNC(&Queue::CommandSetBurstPb), 1},
    {"set_size", "QueueCommandSetSizeArg",
     MODULE_CMD_FUNC(&Queue::CommandSetSizePb), 0},
};

int Queue::Resize(int slots) {
  struct llring *old_queue = queue_;
  struct llring *new_queue;

  int bytes = llring_bytes_with_slots(slots);

  int ret;

  new_queue = static_cast<llring *>(mem_alloc(bytes));
  if (!new_queue) {
    return -ENOMEM;
  }

  ret = llring_init(new_queue, slots, 0, 1);
  if (ret) {
    mem_free(new_queue);
    return -EINVAL;
  }

  /* migrate packets from the old queue */
  if (old_queue) {
    bess::Packet *pkt;

    while (llring_sc_dequeue(old_queue, (void **)&pkt) == 0) {
      ret = llring_sp_enqueue(new_queue, pkt);
      if (ret == -LLRING_ERR_NOBUF) {
        bess::Packet::Free(pkt);
      }
    }

    mem_free(old_queue);
  }

  queue_ = new_queue;

  return 0;
}

pb_error_t Queue::InitPb(const bess::pb::QueueArg &arg) {
  task_id_t tid;
  pb_error_t err;

  burst_ = bess::PacketBatch::kMaxBurst;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return pb_error(ENOMEM, "Task creation failed");

  if (arg.burst() != 0) {
    err = SetBurst(arg.burst());
    if (err.err() != 0) {
      return err;
    }
  } else {
    burst_ = MAX_PKT_BURST;
  }

  if (arg.size() != 0) {
    err = SetSize(arg.size());
    if (err.err() != 0) {
      return err;
    }
  } else {
    int ret = Resize(DEFAULT_QUEUE_SIZE);
    if (ret) {
      return pb_errno(-ret);
    }
  }

  if (arg.prefetch()) {
    prefetch_ = true;
  }

  return pb_errno(0);
}

struct snobj *Queue::Init(struct snobj *arg) {
  task_id_t tid;

  struct snobj *t;
  struct snobj *err;

  burst_ = bess::PacketBatch::kMaxBurst;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return snobj_err(ENOMEM, "Task creation failed");

  if ((t = snobj_eval(arg, "burst")) != nullptr) {
    err = CommandSetBurst(t);
    if (err) {
      return err;
    }
  }

  if ((t = snobj_eval(arg, "size")) != nullptr) {
    err = CommandSetSize(t);
    if (err) {
      return err;
    }
  } else {
    int ret = Resize(DEFAULT_QUEUE_SIZE);
    if (ret) {
      return snobj_errno(-ret);
    }
  }

  if (snobj_eval_int(arg, "prefetch")) {
    prefetch_ = true;
  }

  return nullptr;
}

void Queue::Deinit() {
  bess::Packet *pkt;

  if (queue_) {
    while (llring_sc_dequeue(queue_, (void **)&pkt) == 0) {
      bess::Packet::Free(pkt);
    }
    mem_free(queue_);
  }
}

std::string Queue::GetDesc() const {
  const struct llring *ring = queue_;

  return bess::utils::Format("%u/%u", llring_count(ring), ring->common.slots);
}

/* from upstream */
void Queue::ProcessBatch(bess::PacketBatch *batch) {
  int queued =
      llring_mp_enqueue_burst(queue_, (void **)batch->pkts(), batch->cnt());

  if (queued < batch->cnt()) {
    bess::Packet::Free(batch->pkts() + queued, batch->cnt() - queued);
  }
}

/* to downstream */
struct task_result Queue::RunTask(void *) {
  bess::PacketBatch batch;
  struct task_result ret;

  const int burst = ACCESS_ONCE(burst_);
  const int pkt_overhead = 24;

  uint64_t total_bytes = 0;

  uint64_t cnt = llring_sc_dequeue_burst(queue_, (void **)batch.pkts(), burst);

  if (cnt > 0) {
    batch.set_cnt(cnt);
    RunNextModule(&batch);
  }

  if (prefetch_) {
    for (uint64_t i = 0; i < cnt; i++) {
      total_bytes += batch.pkts()[i]->total_len();
      rte_prefetch0(batch.pkts()[i]->head_data());
    }
  } else {
    for (uint64_t i = 0; i < cnt; i++)
      total_bytes += batch.pkts()[i]->total_len();
  }

  ret = (struct task_result){
      .packets = cnt, .bits = (total_bytes + cnt * pkt_overhead) * 8,
  };

  return ret;
}

struct snobj *Queue::CommandSetBurst(struct snobj *arg) {
  uint64_t val;

  if (snobj_type(arg) != TYPE_INT) {
    return snobj_err(EINVAL, "burst must be an integer");
  }

  val = snobj_uint_get(arg);

  if (val == 0 || val > bess::PacketBatch::kMaxBurst) {
    return snobj_err(EINVAL, "burst size must be [1,%lu]",
                     bess::PacketBatch::kMaxBurst);
  }

  burst_ = val;

  return nullptr;
}

struct snobj *Queue::CommandSetSize(struct snobj *arg) {
  uint64_t val;
  int ret;

  if (snobj_type(arg) != TYPE_INT) {
    return snobj_err(EINVAL, "argument must be an integer");
  }

  val = snobj_uint_get(arg);

  if (val < 4 || val > 16384) {
    return snobj_err(EINVAL, "must be in [4, 16384]");
  }

  if (val & (val - 1)) {
    return snobj_err(EINVAL, "must be a power of 2");
  }

  ret = Resize(val);
  if (ret) {
    return snobj_errno(-ret);
  }

  return nullptr;
}

pb_error_t Queue::SetBurst(int64_t burst) {
  if (burst == 0 ||
      burst > static_cast<int64_t>(bess::PacketBatch::kMaxBurst)) {
    return pb_error(EINVAL, "burst size must be [1,%lu]",
                    bess::PacketBatch::kMaxBurst);
  }

  burst_ = burst;
  return pb_errno(0);
}

pb_error_t Queue::SetSize(uint64_t size) {
  if (size < 4 || size > 16384) {
    return pb_error(EINVAL, "must be in [4, 16384]");
  }

  if (size & (size - 1)) {
    return pb_error(EINVAL, "must be a power of 2");
  }

  int ret = Resize(size);
  if (ret) {
    return pb_errno(-ret);
  }
  return pb_errno(0);
}

pb_cmd_response_t Queue::CommandSetBurstPb(
    const bess::pb::QueueCommandSetBurstArg &arg) {
  pb_cmd_response_t response;
  set_cmd_response_error(&response, SetBurst(arg.burst()));
  return response;
}
pb_cmd_response_t Queue::CommandSetSizePb(
    const bess::pb::QueueCommandSetSizeArg &arg) {
  pb_cmd_response_t response;
  set_cmd_response_error(&response, SetSize(arg.size()));
  return response;
}

ADD_MODULE(Queue, "queue",
           "terminates current task and enqueue packets for new task")
