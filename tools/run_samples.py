#!/usr/bin/env python3

from __future__ import annotations

import datetime
import argparse
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
  parser = argparse.ArgumentParser(description="Compile and run all PL/0 samples")
  parser.add_argument("--build-dir", default="build",
                      help="CMake build directory, relative to the project root")
  parser.add_argument("--runtime-dir",
                      help="Directory containing compiler runtime DLLs (Windows)")
  args = parser.parse_args()

  # Keep a drive-letter path such as Q:\ intact. Path.resolve() expands a
  # substituted drive back to its non-ASCII target, which older MinGW argv /
  # std::filesystem combinations cannot reliably open on Windows.
  repo_root = Path(os.path.abspath(__file__)).parent.parent
  executable_suffix = ".exe" if os.name == "nt" else ""
  build_dir = Path(os.path.abspath(repo_root / args.build_dir))
  pl0c = build_dir / f"pl0c{executable_suffix}"
  pl0run = build_dir / f"pl0run{executable_suffix}"
  child_environment = os.environ.copy()
  if args.runtime_dir:
    child_environment["PATH"] = str(Path(args.runtime_dir)) + os.pathsep + child_environment.get("PATH", "")
  samples_dir = repo_root / "tests" / "samples"
  report_path = repo_root / "tests" / "sample_report.txt"

  missing = [cmd for cmd in (pl0c, pl0run) if not cmd.exists()]
  if missing:
    print(f"[run_samples] Missing binaries: {', '.join(str(p) for p in missing)}", file=sys.stderr)
    print("Please build the project (e.g. `cmake --build build`) before running this script.", file=sys.stderr)
    return 1

  sample_files = sorted(samples_dir.glob("*.pl0"))
  if not sample_files:
    print(f"[run_samples] No .pl0 files found in {samples_dir}", file=sys.stderr)
    return 1

  lines: list[str] = []
  now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
  lines.append("PL/0 Sample Batch Report")
  lines.append(f"Generated: {now}")
  lines.append(f"Build: {build_dir}")
  lines.append("")

  failures: list[str] = []
  sample_input = {
      "io_mix.pl0": "20 22\n",
  }

  for sample in sample_files:
    name = sample.name
    expects_failure = name.startswith("error_")
    pcode_path = sample.with_suffix(".pcode")
    if pcode_path.exists():
      pcode_path.unlink()

    lines.append(f"=== {name} ===")
    lines.append("-- Source --")
    lines.append(sample.read_text().rstrip())
    lines.append("")

    sample_argument = sample.relative_to(repo_root)
    pcode_argument = pcode_path.relative_to(repo_root)
    compile_cmd = [str(pl0c), str(sample_argument), "-o", str(pcode_argument)]
    try:
      compile_proc = subprocess.run(
          compile_cmd, cwd=repo_root, capture_output=True, text=True,
          timeout=10, env=child_environment)
    except subprocess.TimeoutExpired:
      failures.append(f"{name}: compiler timed out")
      lines.append("-- Result --\ncompiler timed out")
      lines.append("")
      continue

    stderr = compile_proc.stderr.strip()
    if stderr:
      lines.append("-- Compile Diagnostics --")
      lines.append(stderr)
      lines.append("")

    if compile_proc.returncode != 0:
      outcome = "expected failure" if expects_failure else "compilation failed"
      lines.append(f"-- Result --\n{outcome}")
      lines.append("")
      if not expects_failure:
        failures.append(f"{name}: compilation failed unexpectedly")
      continue

    if expects_failure:
      failures.append(f"{name}: expected compilation failure, but compilation succeeded")

    if pcode_path.exists():
      lines.append("-- P-Code --")
      lines.append(pcode_path.read_text().rstrip())
      lines.append("")

    try:
      run_proc = subprocess.run(
          [str(pl0run), str(pcode_argument)], cwd=repo_root,
          input=sample_input.get(name, ""), capture_output=True, text=True,
          timeout=5, env=child_environment)
    except subprocess.TimeoutExpired:
      failures.append(f"{name}: runtime timed out")
      lines.append("-- Output --\n(runtime timed out)")
      lines.append("-- Result --\nruntime timed out")
      lines.append("")
      continue
    output_chunks = []
    if run_proc.stdout.strip():
      output_chunks.append(run_proc.stdout.strip())
    if run_proc.stderr.strip():
      output_chunks.append(run_proc.stderr.strip())

    lines.append("-- Output --")
    lines.append("\n".join(output_chunks) if output_chunks else "(no output)")
    lines.append(f"-- Result --\n{'passed' if run_proc.returncode == 0 else 'runtime failed'}")
    lines.append("")
    if run_proc.returncode != 0:
      failures.append(f"{name}: runtime failed")

  lines.append("=== Summary ===")
  lines.append(f"Samples: {len(sample_files)}")
  lines.append(f"Failures: {len(failures)}")
  lines.extend(f"- {failure}" for failure in failures)

  report_path.write_text("\n".join(lines).rstrip() + "\n")
  print(f"[run_samples] Report written to {report_path}")
  if failures:
    print("[run_samples] Failures:", file=sys.stderr)
    for failure in failures:
      print(f"  - {failure}", file=sys.stderr)
    return 1
  print(f"[run_samples] {len(sample_files)} samples passed")
  return 0


if __name__ == "__main__":
  sys.exit(main())
