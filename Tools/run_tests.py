"""P02 SpawnPacer - run the project's automation tests headless and fail unless every expected test passed.

Usage (through tools/ue_queue.py on the development machine, never directly):
    python -I -u run_tests.py --expect N [--filter SpawnPacer] [--timeout 900]

Needs the Development editor target (build.bat SpawnPacerEditor Development).

Why a script and not just the editor's exit code: the automation runner does exit with -1 (a forced exit) when a test
reported an error (Developer/AutomationController/Private/AutomationCommandline.cpp:493-504). But exit code 0 can't tell
WHICH tests ran: a stale build without a new test, or a filter that matches fewer tests than meant, still exits 0. So the
script also reads the result lines from the log, one per test, "Test Completed. Result={...} Name={...} Path={...}"
(AutomationControllerManager.cpp:43, 57). The run fails on a non-zero exit, a timeout, any result that isn't "Success",
or fewer than --expect distinct tests (required: the caller states how many tests it means to run).
"""
from __future__ import annotations

import argparse
import os
import re
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
from run_bench import run_and_reap  # noqa: E402  (same folder: one launch-wait-kill for every Unreal process)
PROJECT_DIR = os.path.join(os.path.dirname(TOOLS_DIR), "SpawnPacer")
PROJECT = os.path.join(PROJECT_DIR, "SpawnPacer.uproject")
EDITOR = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
LOG = os.path.join(PROJECT_DIR, "Saved", "Logs", "RunTests.log")
RESULT = re.compile(r"Test Completed\. Result=\{(?P<result>[^}]*)\} Name=\{(?P<name>[^}]*)\} Path=\{(?P<path>[^}]*)\}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--filter", default="SpawnPacer", help="test path prefix (default: every test of this project)")
    parser.add_argument("--expect", type=int, required=True, help="fewest distinct tests that must run")
    parser.add_argument("--timeout", type=float, default=900.0)
    args = parser.parse_args(argv)

    try:
        if os.path.exists(LOG):
            os.remove(LOG)   # never read a previous run's results
    except OSError as error:
        print(f"run_tests: FAILED, can't remove the previous log (is an editor still running?): {error}", flush=True)
        return 1
    # One string, so the quotes reach Unreal exactly as written (-Key="value with spaces"), as run_bench.py does.
    command = (f'"{EDITOR}" "{PROJECT}" -ExecCmds="Automation RunTests {args.filter};Quit" -unattended -nullrhi '
               f'-nosplash -nosound -nopause -log -abslog="{LOG}"')
    exit_code = run_and_reap(command, args.timeout)

    results = []
    if os.path.exists(LOG):
        with open(LOG, encoding="utf-8", errors="replace") as handle:
            results = [m.groupdict() for m in (RESULT.search(line) for line in handle) if m]
    for result in results:
        print(f"  {result['result']:<8} {result['path']}", flush=True)
    failed = [r for r in results if r["result"] != "Success"]
    distinct = len({r["path"] for r in results})   # a repeated line must not make up for a test that didn't run
    print(f"run_tests: {distinct} tests (expected at least {args.expect}), {len(failed)} not passed, "
          f"editor exit code {exit_code}, log {LOG}", flush=True)
    if exit_code != 0 or failed or distinct < args.expect:
        print("run_tests: FAILED (timeout, non-zero exit, a test that didn't pass, or fewer tests than --expect)", flush=True)
        return 1
    print("run_tests: PASSED", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
