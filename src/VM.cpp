// 文件: VM.cpp
// 功能: 实现支持状态快照的 P-Code 虚拟机
#include "pl0/VM.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace pl0 {

namespace {
constexpr std::size_t kInitialStackSize = 1024;
}

VirtualMachine::VirtualMachine(DiagnosticSink& diagnostics,
                               const RunnerOptions& options)
    : diagnostics_(diagnostics), options_(options) {}

void VirtualMachine::load_program(const InstructionSequence& code) {
  code_ = code;
  reset();
}

void VirtualMachine::reset() {
  stack_.assign(kInitialStackSize, 0);
  input_.clear();
  changed_addresses_.clear();
  output_.clear();
  last_value_ = 0;
  step_count_ = 0;
  stack_top_ = 0;
  base_pointer_ = 0;
  program_counter_ = 0;
  current_instruction_ = -1;
  status_ = code_.empty() ? Status::Ready : Status::Paused;
  frame_entries_.clear();
  frame_entries_.emplace(0, 0);
  stack_[0] = stack_[1] = stack_[2] = 0;
}

void VirtualMachine::provide_input(std::int64_t value) {
  input_.push_back(value);
  if (status_ == Status::NeedInput) status_ = Status::Paused;
}

bool VirtualMachine::step() {
  if (status_ == Status::Finished || status_ == Status::RuntimeError ||
      status_ == Status::Ready || status_ == Status::NeedInput) {
    return false;
  }
  if (program_counter_ < 0 ||
      program_counter_ >= static_cast<int>(code_.size())) {
    status_ = Status::Finished;
    return false;
  }

  const Instruction instr = code_[static_cast<std::size_t>(program_counter_)];
  if (instr.op == Op::OPR && static_cast<Opr>(instr.argument) == Opr::READ &&
      input_.empty()) {
    current_instruction_ = program_counter_;
    status_ = Status::NeedInput;
    return false;
  }

  changed_addresses_.clear();
  current_instruction_ = program_counter_;
  ++program_counter_;

  try {
    switch (instr.op) {
      case Op::LIT:
        push(instr.argument);
        break;
      case Op::OPR: {
        const auto operation = static_cast<Opr>(instr.argument);
        switch (operation) {
          case Opr::RET: {
            const int old_base = base_pointer_;
            const int return_addr = static_cast<int>(at(base_pointer_ + 2));
            base_pointer_ = static_cast<int>(at(base_pointer_ + 1));
            stack_top_ = old_base;
            program_counter_ = return_addr;
            if (old_base != 0) frame_entries_.erase(old_base);
            if (base_pointer_ == 0 && program_counter_ == 0)
              status_ = Status::Finished;
            break;
          }
          case Opr::NEG:
            write_at(stack_top_ - 1, -at(stack_top_ - 1));
            break;
          case Opr::ADD: {
            const auto r = pop(), l = pop(); push(l + r); last_value_ = at(stack_top_ - 1); break;
          }
          case Opr::SUB: {
            const auto r = pop(), l = pop(); push(l - r); last_value_ = at(stack_top_ - 1); break;
          }
          case Opr::MUL: {
            const auto r = pop(), l = pop(); push(l * r); last_value_ = at(stack_top_ - 1); break;
          }
          case Opr::DIV: {
            const auto r = pop();
            if (r == 0) { runtime_error(DiagnosticCode::DivisionByZero, "division by zero"); return false; }
            const auto l = pop(); push(l / r); last_value_ = at(stack_top_ - 1); break;
          }
          case Opr::ODD: { const auto value = pop(); push(value % 2 != 0); break; }
          case Opr::MOD: {
            const auto r = pop();
            if (r == 0) { runtime_error(DiagnosticCode::DivisionByZero, "modulo by zero"); return false; }
            const auto l = pop(); push(l % r); last_value_ = at(stack_top_ - 1); break;
          }
          case Opr::EQ: { const auto r = pop(), l = pop(); push(l == r); break; }
          case Opr::NE: { const auto r = pop(), l = pop(); push(l != r); break; }
          case Opr::LT: { const auto r = pop(), l = pop(); push(l < r); break; }
          case Opr::GE: { const auto r = pop(), l = pop(); push(l >= r); break; }
          case Opr::GT: { const auto r = pop(), l = pop(); push(l > r); break; }
          case Opr::LE: { const auto r = pop(), l = pop(); push(l <= r); break; }
          case Opr::WRITE: {
            const auto value = pop(); output_ += std::to_string(value); last_value_ = value; break;
          }
          case Opr::WRITELN: output_ += '\n'; break;
          case Opr::READ: {
            const auto value = input_.front(); input_.pop_front(); push(value); break;
          }
          case Opr::AND: { const auto r = pop(), l = pop(); push(l != 0 && r != 0); break; }
          case Opr::OR: { const auto r = pop(), l = pop(); push(l != 0 || r != 0); break; }
          case Opr::NOT: { const auto value = pop(); push(value == 0); break; }
        }
        break;
      }
      case Op::LOD: push(at(base(instr.level, base_pointer_) + instr.argument)); break;
      case Op::STO: {
        const auto value = pop(); write_at(base(instr.level, base_pointer_) + instr.argument, value); break;
      }
      case Op::CAL:
        write_at(stack_top_, base(instr.level, base_pointer_));
        write_at(stack_top_ + 1, base_pointer_);
        write_at(stack_top_ + 2, program_counter_);
        base_pointer_ = stack_top_;
        program_counter_ = instr.argument;
        frame_entries_[base_pointer_] = instr.argument;
        break;
      case Op::INT:
        // The first three cells were prepared by CAL as SL, DL and RA.  INT's
        // argument is the complete frame size, so only initialize local cells.
        for (int i = 3; i < instr.argument; ++i) write_at(stack_top_ + i, 0);
        stack_top_ += instr.argument;
        break;
      case Op::JMP: program_counter_ = instr.argument; break;
      case Op::JPC: if (pop() == 0) program_counter_ = instr.argument; break;
      case Op::LDA: push(base(instr.level, base_pointer_) + instr.argument); break;
      case Op::IDX: { const auto index = pop(), address = pop(); push(address + index); break; }
      case Op::LDI: { const auto address = pop(); push(at(static_cast<int>(address))); break; }
      case Op::STI: {
        const auto value = pop(), address = pop(); write_at(static_cast<int>(address), value); break;
      }
      case Op::CHK: {
        const auto index = pop();
        if (index < 0 || index >= instr.argument) {
          runtime_error(DiagnosticCode::InvalidArraySubscript, "array index out of bounds"); return false;
        }
        push(index); break;
      }
      case Op::DUP:
        if (stack_top_ == 0) { runtime_error(DiagnosticCode::StackUnderflow, "stack underflow on dup"); return false; }
        push(at(stack_top_ - 1)); break;
      case Op::NOP: break;
    }
  } catch (const std::exception& ex) {
    runtime_error(DiagnosticCode::RuntimeError, ex.what());
    return false;
  }

  ++step_count_;
  if (status_ != Status::Finished &&
      (program_counter_ < 0 || program_counter_ >= static_cast<int>(code_.size()))) {
    status_ = Status::Finished;
  } else if (status_ != Status::Finished) {
    status_ = Status::Paused;
  }
  return true;
}

VirtualMachine::Snapshot VirtualMachine::snapshot() const {
  Snapshot state;
  state.status = status_;
  state.step = step_count_;
  state.pc = program_counter_;
  state.bp = base_pointer_;
  state.sp = stack_top_;
  state.current_instruction = current_instruction_;
  state.changed_addresses = changed_addresses_;
  state.output = output_;

  int visible_end = std::max(stack_top_, base_pointer_ + 3);
  visible_end = std::clamp(visible_end, 0, static_cast<int>(stack_.size()));
  state.stack.assign(stack_.begin(), stack_.begin() + visible_end);

  int cursor = base_pointer_;
  std::vector<int> visited;
  while (cursor >= 0 && cursor + 2 < static_cast<int>(stack_.size()) &&
         std::find(visited.begin(), visited.end(), cursor) == visited.end()) {
    visited.push_back(cursor);
    FrameSnapshot frame;
    frame.base = cursor;
    if (const auto entry = frame_entries_.find(cursor);
        entry != frame_entries_.end()) {
      frame.entry_address = entry->second;
    }
    frame.static_link = static_cast<int>(stack_[static_cast<std::size_t>(cursor)]);
    frame.dynamic_link = static_cast<int>(stack_[static_cast<std::size_t>(cursor + 1)]);
    frame.return_address = static_cast<int>(stack_[static_cast<std::size_t>(cursor + 2)]);
    frame.current = cursor == base_pointer_;
    state.frames.push_back(frame);
    if (cursor == 0 || frame.dynamic_link == cursor) break;
    cursor = frame.dynamic_link;
  }
  std::reverse(state.frames.begin(), state.frames.end());
  return state;
}

VirtualMachine::Result VirtualMachine::execute(const InstructionSequence& code) {
  load_program(code);
  std::size_t printed_output = 0;
  while (status_ != Status::Finished && status_ != Status::RuntimeError) {
    const int instruction_address = program_counter_;
    const bool executed = step();
    if (!executed && status_ == Status::NeedInput) {
      std::int64_t value = 0;
      if (!(std::cin >> value)) {
        runtime_error(DiagnosticCode::IOError, "failed to read integer input");
        break;
      }
      provide_input(value);
      continue;
    }
    if (executed && options_.trace_vm && instruction_address >= 0 &&
        instruction_address < static_cast<int>(code_.size())) {
      std::cout << instruction_address << ": "
                << to_string(code_[static_cast<std::size_t>(instruction_address)]) << '\n';
    }
    if (output_.size() > printed_output) {
      std::cout.write(output_.data() + printed_output,
                      static_cast<std::streamsize>(output_.size() - printed_output));
      printed_output = output_.size();
    }
  }
  return {status_ != Status::RuntimeError, last_value_};
}

void VirtualMachine::push(std::int64_t value) { write_at(stack_top_, value); ++stack_top_; }

std::int64_t VirtualMachine::pop() {
  if (stack_top_ == 0) throw std::runtime_error("stack underflow");
  --stack_top_;
  return at(stack_top_);
}

std::int64_t& VirtualMachine::at(int index) {
  if (index < 0) throw std::runtime_error("negative stack access");
  ensure_capacity(index);
  return stack_[static_cast<std::size_t>(index)];
}

void VirtualMachine::write_at(int index, std::int64_t value) {
  if (index < 0) throw std::runtime_error("negative stack access");
  ensure_capacity(index);
  if (stack_[static_cast<std::size_t>(index)] != value || index >= stack_top_)
    changed_addresses_.push_back(index);
  stack_[static_cast<std::size_t>(index)] = value;
}

void VirtualMachine::ensure_capacity(int index) {
  if (index >= static_cast<int>(stack_.size()))
    stack_.resize(static_cast<std::size_t>(index) + 1024, 0);
}

int VirtualMachine::base(int level, int b) const {
  int result = b;
  while (level-- > 0) {
    if (result < 0 || result >= static_cast<int>(stack_.size()))
      throw std::runtime_error("invalid static link");
    result = static_cast<int>(stack_[static_cast<std::size_t>(result)]);
  }
  return result;
}

void VirtualMachine::runtime_error(DiagnosticCode code, const std::string& message) {
  diagnostics_.report({DiagnosticLevel::Error, code, message, {}});
  status_ = Status::RuntimeError;
}

}  // namespace pl0
