#include "catch.hpp"

#include "TestSupport.hpp"
#include "pl0/Driver.hpp"

#include <iostream>
#include <sstream>

TEST_CASE("Virtual machine executes program and produces expected output") {
  const char* source = "var x; begin x := 1; x := x + 2; write(x); end.";
  pl0::CompilerOptions compiler_options;
  pl0::DiagnosticSink diagnostics;
  auto instructions = pl0::test::compile_source(source, compiler_options, diagnostics);
  REQUIRE(!diagnostics.has_errors());

  pl0::RunnerOptions runner_options;
  std::ostringstream capture;
  auto* previous_buf = std::cout.rdbuf(capture.rdbuf());
  auto result = pl0::run_instructions(instructions, diagnostics, runner_options);
  std::cout.rdbuf(previous_buf);
  REQUIRE(!diagnostics.has_errors());
  REQUIRE(result.success);
  REQUIRE(result.last_value == 3);
  REQUIRE(capture.str() == "3");
}

TEST_CASE("Virtual machine handles compound assignments and increments") {
  const char* source =
      "var x; begin x := 10; x -= 3; x *= 2; x /= 7; x %= 3; x++; x--; write(x); end.";
  pl0::CompilerOptions compiler_options;
  pl0::DiagnosticSink diagnostics;
  auto instructions = pl0::test::compile_source(source, compiler_options, diagnostics);
  REQUIRE(!diagnostics.has_errors());

  pl0::RunnerOptions runner_options;
  std::ostringstream capture;
  auto* previous_buf = std::cout.rdbuf(capture.rdbuf());
  auto result = pl0::run_instructions(instructions, diagnostics, runner_options);
  std::cout.rdbuf(previous_buf);
  REQUIRE(!diagnostics.has_errors());
  REQUIRE(result.success);
  REQUIRE(result.last_value == 2);
  REQUIRE(capture.str() == "2");
}

TEST_CASE("Debugger step executes exactly one instruction and exposes registers") {
  const char* source = "var x; begin x := 2 + 3 * 4; write(x) end.";
  pl0::CompilerOptions compiler_options;
  pl0::DiagnosticSink diagnostics;
  auto instructions = pl0::test::compile_source(source, compiler_options, diagnostics);
  REQUIRE(!diagnostics.has_errors());

  pl0::RunnerOptions runner_options;
  pl0::VirtualMachine vm(diagnostics, runner_options);
  vm.load_program(instructions);
  const auto before = vm.snapshot();
  REQUIRE(before.step == 0);
  REQUIRE(before.pc == 0);
  REQUIRE(before.status == pl0::VirtualMachine::Status::Paused);

  REQUIRE(vm.step());
  const auto after = vm.snapshot();
  REQUIRE(after.step == 1);
  REQUIRE(after.current_instruction == 0);
  REQUIRE(after.pc >= 0);
}

TEST_CASE("Debugger pauses for input and resumes with provided value") {
  const char* source = "var x; begin read(x); write(x) end.";
  pl0::CompilerOptions compiler_options;
  pl0::DiagnosticSink diagnostics;
  auto instructions = pl0::test::compile_source(source, compiler_options, diagnostics);
  REQUIRE(!diagnostics.has_errors());

  pl0::RunnerOptions runner_options;
  pl0::VirtualMachine vm(diagnostics, runner_options);
  vm.load_program(instructions);
  while (vm.status() == pl0::VirtualMachine::Status::Paused) {
    vm.step();
  }
  REQUIRE(vm.status() == pl0::VirtualMachine::Status::NeedInput);
  const auto waiting = vm.snapshot();

  vm.provide_input(42);
  REQUIRE(vm.step());
  REQUIRE(vm.snapshot().step == waiting.step + 1);
  while (vm.status() == pl0::VirtualMachine::Status::Paused) {
    vm.step();
  }
  REQUIRE(vm.status() == pl0::VirtualMachine::Status::Finished);
  REQUIRE(vm.snapshot().output == "42");
}

TEST_CASE("Procedure calls preserve frame links and access non-local variables") {
  const char* source =
      "var x; procedure increase; begin x := x + 1; writeln(x) end; "
      "begin x := 40; call increase; call increase end.";
  pl0::CompilerOptions compiler_options;
  pl0::DiagnosticSink diagnostics;
  auto instructions = pl0::test::compile_source(source, compiler_options, diagnostics);
  REQUIRE(!diagnostics.has_errors());

  pl0::RunnerOptions runner_options;
  pl0::VirtualMachine vm(diagnostics, runner_options);
  vm.load_program(instructions);

  bool observed_child_frame = false;
  while (vm.status() == pl0::VirtualMachine::Status::Paused) {
    vm.step();
    const auto state = vm.snapshot();
    if (state.frames.size() > 1) {
      observed_child_frame = true;
      const auto& child = state.frames.back();
      REQUIRE(child.entry_address > 0);
      REQUIRE(child.dynamic_link == 0);
      REQUIRE(child.static_link == 0);
      REQUIRE(child.return_address > 0);
    }
  }

  REQUIRE(observed_child_frame);
  REQUIRE(vm.status() == pl0::VirtualMachine::Status::Finished);
  REQUIRE(vm.snapshot().output == "41\n42\n");
  REQUIRE(!diagnostics.has_errors());
}
