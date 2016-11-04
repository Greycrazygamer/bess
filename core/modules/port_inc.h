#ifndef __PORT_INC_H__
#define __PORT_INC_H__

#include "../module.h"
#include "../port.h"

class PortInc : public Module {
 public:
  PortInc() : Module(), port_(), prefetch_(), burst_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const google::protobuf::Any &arg);

  virtual void Deinit();

  virtual struct task_result RunTask(void *arg);

  virtual std::string GetDesc() const;

  struct snobj *CommandSetBurst(struct snobj *arg);
  bess::protobuf::ModuleCommandResponse CommandSetBurst(
      const google::protobuf::Any &arg);

  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  Port *port_ = {};
  int prefetch_ = {};
  int burst_ = {};
  pb_error_t SetBurst(int64_t burst);
};

#endif
