// 文件: VM.hpp
// 功能: 声明支持整段运行与教学单步调试的 P-Code 虚拟机
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "pl0/Diagnostics.hpp"
#include "pl0/Options.hpp"
#include "pl0/PCode.hpp"

namespace pl0 {

class VirtualMachine {
 public:
  enum class Status { Ready, Paused, NeedInput, Finished, RuntimeError };

  struct FrameSnapshot {
    int base = 0;
    int entry_address = 0;
    int static_link = 0;
    int dynamic_link = 0;
    int return_address = 0;
    bool current = false;
  };

  struct Snapshot {
    Status status = Status::Ready;
    std::size_t step = 0;
    int pc = 0;
    int bp = 0;
    int sp = 0;  // 下一空闲栈单元
    int current_instruction = -1;
    std::vector<std::int64_t> stack;
    std::vector<int> changed_addresses;
    std::vector<FrameSnapshot> frames;
    std::string output;
  };

  struct Result {
    bool success = true;
    std::int64_t last_value = 0;
  };

  VirtualMachine(DiagnosticSink& diagnostics, const RunnerOptions& options);
  Result execute(const InstructionSequence& code);

  void load_program(const InstructionSequence& code);
  void reset();
  bool step();  // 成功时恰好执行一条 P-Code
  void provide_input(std::int64_t value);
  [[nodiscard]] Snapshot snapshot() const;
  [[nodiscard]] Status status() const { return status_; }

 private:
  void push(std::int64_t value);
  std::int64_t pop();
  std::int64_t& at(int index);
  void write_at(int index, std::int64_t value);
  void ensure_capacity(int index);
  int base(int level, int b) const;
  void runtime_error(DiagnosticCode code, const std::string& message);

  DiagnosticSink& diagnostics_;
  const RunnerOptions& options_;
  InstructionSequence code_;
  std::vector<std::int64_t> stack_;
  std::deque<std::int64_t> input_;
  std::vector<int> changed_addresses_;
  std::string output_;
  std::int64_t last_value_ = 0;
  std::size_t step_count_ = 0;
  int stack_top_ = 0;
  int base_pointer_ = 0;
  int program_counter_ = 0;
  int current_instruction_ = -1;
  Status status_ = Status::Ready;
  std::unordered_map<int, int> frame_entries_;
};

}  // namespace pl0
