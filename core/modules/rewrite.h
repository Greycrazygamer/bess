#ifndef BESS_MODULES_REWRITE_H_
#define BESS_MODULES_REWRITE_H_

#include "../module.h"
#include "../module_msg.pb.h"

class Rewrite : public Module {
 public:
  static const size_t kNumSlots = bess::PacketBatch::kMaxBurst * 2 - 1;
  static const size_t kMaxTemplateSize = 1536;
  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

  Rewrite()
      : Module(),
        next_turn_(),
        num_templates_(),
        template_size_(),
        templates_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::RewriteArg &arg);

  virtual void ProcessBatch(bess::PacketBatch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  pb_cmd_response_t CommandAddPb(const bess::pb::RewriteArg &arg);
  pb_cmd_response_t CommandClearPb(const bess::pb::EmptyArg &arg);

 private:
  inline void DoRewrite(bess::PacketBatch *batch);
  inline void DoRewriteSingle(bess::PacketBatch *batch);

  /* For fair round robin we remember the next index for later.
   * [0, num_templates - 1] */
  int next_turn_;

  int num_templates_;
  uint16_t template_size_[kNumSlots];
  unsigned char templates_[kNumSlots][kMaxTemplateSize] __ymm_aligned;
};

#endif  // BESS_MODULES_REWRITE_H_
