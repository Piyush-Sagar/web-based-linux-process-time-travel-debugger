# Status

**Current result, 2026-10-10:** all requested first-version milestones implemented and verified. Runnable local C++/ptrace + React debugger, actual checkpoint restoration and replay, no mocks. Full Windows setup and verify scripts pass. Scope remains controlled no-libc single-threaded demos; this is not general Linux application replay.

## 2026-10-10: requirements/environment
- Workspace initially empty except Git metadata. Extracted both PDF pages.
- Windows host has Ubuntu WSL2, Ubuntu 24.04.3, Linux 6.6.87.2-microsoft-standard-WSL2, x86-64. Default Linux user piyush, Yama ptrace_scope=1.
- Docker CLI exists but daemon unavailable; using WSL2 directly.
- Linux initially missing g++, CMake, Node; dependency installation in progress. ptrace/replay not yet verified.
- SPEC/PLAN/AGENTS written before implementation. Narrow custom engine retained; no rr substitution.
- Linux preflight `python3 scripts/probe_linux.py` PASS: real TRACEME, exec SIGTRAP, SINGLESTEP, and ADDR_NO_RANDOMIZE on this WSL kernel. This proves primitives only, not replay.
- apt initially stalled with no downloads; restarted with Acquire::ForceIPv4=true, downloads progressing. Root used only for dependency installation; tracing runs as normal Linux user.

## Milestones 1-3: CLI, persistence, checkpoint/replay proof
- Installed GCC 13.3, CMake 3.28, Ninja, nlohmann-json 3.11.3, Boost 1.83, Node 18/npm, rr 5.7 in Ubuntu.
- `cmake --build build -j 4` and `ctest --test-dir build --output-on-failure`: PASS, 1/1 engine integration test, 3 seconds.
- Recorded crash at step 141, checkpoints 0/20/40/60/80/100/120/140. Original PID 627; restored live PIDs 628-632 at targets 1, 19, 39, 140, 141. All 27 general registers and 64 selected bytes matched. Complete replay from checkpoint 0 reproduced random/time-dependent memory and SIGSEGV. Original fatal signal delivered and termination captured before restoring.
- Saved trace loaded in a new tracer and replayed to 39. Clean exit, unsupported getpid rejection before execution, unknown executable rejection, corrupted trace checksum, recording bound and exited-target rejection tested.
- Evidence: `evidence/replay.json`, reproducible via `python3 tests/engine_test.py build/ttd`.
- Fixes discovered through Linux tests: anonymous static BSS appears as `[heap]`; spaces in proc maps paths are octal escaped; synthetic exec-stop reserved EFLAGS bit cannot be restored. Step 0 now follows one bootstrap instruction; comparisons remain exact and unmasked.
- rr probes: normal recording rejected perf_event_paranoid=2; `rr record -n /bin/true` rejected Intel CPU type 0xa06a0 as unknown. No sysctl changes made. Installed rr is not a working verified alternative here.
- Replay gate satisfied before API/frontend implementation. API/frontend still pending.

## Milestone 4: REST and WebSockets
- C++ Boost.Beast service builds and runs on one io_context thread, owning every ptrace operation. Continue runs in 8-instruction batches every 25 ms; pause stops between batches.
- `python3 tests/api_test.py`: PASS. Actual HTTP launch, step, continue/crash, signal delivery/termination, historical inspection, restored targets 1/39/140/141; all GPRs and selected memory matched.
- Real WebSocket upgrade and live session frame received. Pause verified by unchanged cursor after waiting. Service restart loaded persisted session and restored step 39.
- Unknown actions/targets/executables, cross-origin request and unsupported syscall tested. Loopback-only binding, restricted Host/Origin, bounded payloads and session count.
- Evidence: `evidence/api.json`. React implementation begun only after CLI replay proof; build/browser verification pending.

## Milestones 5-6: frontend and end-to-end verification
- React 19.3 / TypeScript 5.9.3 / Vite 6.4.4 implemented with real REST state/history and WebSocket live updates. Timeline plots actual selected memory; inspectors show all GPRs and eight memory words. Historical/live/terminated labels are distinct; restore is explicit. No mocks.
- Windows Node 24.18.1 used for frontend, because Ubuntu's distribution Node 18 does not meet Playwright 1.64's Node >=20 requirement. Lockfile `npm ci` verified; npm audit reported 0 vulnerabilities.
- `npm.cmd run build`: PASS, TypeScript strict checking and production bundle. C++ service serves that bundle on the same loopback origin.
- `npm.cmd run test:e2e`: PASS, 3/3 Chromium browser tests. Browser exercised original termination then restored targets 39/1/57/140/141, all 27 GPRs/all selected bytes matching; pause and unsupported syscall UI error; 375px layout, keyboard range selection, reduced motion, landscape overflow check.
- Browser launch initially failed for the full Chromium executable (`spawn UNKNOWN` on this host). Installed Chromium headless shell worked, so default headless Playwright configuration is used. A broad test label selector was fixed with exact matching. Neither issue changed replay semantics.
- Visually reviewed `evidence/demo.png` and `evidence/mobile.png`; no clipping or page overflow observed. Structured browser evidence in `evidence/browser-tests.json`.
- Strengthened live output proof: original pipe emitted `recorded once\n`, replay emitted zero new pipe bytes; checks run inside engine and tests. Build-time demo manifest added; checksum/identity/layout and deliberate replay divergence rejection tested. Failed restoration kills child and clears live state.
- CLI asynchronous continue and real pause now operate on one owning worker thread; pause cursor stability tested. Default synchronous continue remains for scripting.
- `scripts/setup.ps1`: PASS through WSL package/preflight/build and Windows npm ci/build. README contains exact Windows and Linux commands, demo flow, APIs, storage semantics and limits. Full verify script and runnable demo finalization underway.

## Final verification and runnable instance
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify.ps1`: PASS. CTest 2/2 (engine replay and API sessions), strict TypeScript/production build, Chromium end-to-end 3/3 (8.6 seconds).
- `scripts/run.ps1`: running loopback service at http://localhost:8080. Health returned custom-ptrace/linux-x86_64. Browser panel opening was queued by Codex; URL also works in a Windows browser.
- `scripts/demo.ps1`: PASS against this running instance. Original PID 618 crashed at step 141, SIGSEGV delivered and original terminated. Targets 1/39/57/140/141 restored and replayed with all GPRs/selected bytes matching. No duplicate child pipe output. Final session s1 is actually restored at step 39, live PID 625 (PIDs are evidence from this run, not stable identifiers).
- Latest evidence: `evidence/replay.json`, `evidence/api.json`, `evidence/browser-tests.json`, `evidence/demo-run.json`, `evidence/demo.png`, `evidence/mobile.png`. Reproduction commands and exact prerequisites in README.
- `scripts/demo.ps1` leaves a real restored process for interactive inspection. The browser can inspect the original history, restore further points, or continue recorded replay. Normal app data in ignored `traces/`; test servers use temporary directories.

## Verified limits and remaining product scope
- Verified platform is Ubuntu 24.04.3 on x86-64 WSL2 kernel 6.6.87.2, GCC 13.3, CMake 3.28, Boost 1.83, Windows Node 24.18.1 and Playwright Chromium headless shell. Native Windows backend and other CPUs/kernels unverified.
- Four approved static C++ demos, no libc or source-level stepping; supported syscall/memory/size contract in SPEC. Files, network, process/thread creation, changing mappings and other syscalls are rejected. No sandbox guarantee for untrusted code, no general kernel-resource snapshots, no branching after recorded history.
- Full writable mappings and extended CPU state are restored; equality checks cover all captured GPRs and selected 64 bytes at each replayed stop. No complete-memory equality check per instruction, although replay uses the complete checkpoint memory (including stack/output buffers).
- Local trusted JSON only, build/path-specific checkpoints, non-cryptographic corruption checksum, atomic replacement without fsync. Graceful shutdown saves traces; abrupt termination may lose current running batches. Inspection after restart is explicitly history until restore.
- No outstanding work within the documented first-version acceptance scope. General-program replay, source debugging, expanded syscalls and rr integration are future projects requiring additional design and verification.

## GitHub publication
- Requested repository target: `Piyush-Sagar/web-based-linux-process-time-travel-debugger`, private visibility. Source, documentation, setup/demo/verification scripts and test evidence included; generated traces, build outputs and dependencies excluded.
- `.gitattributes` enforces LF on checkout so Bash scripts work when cloning with Windows Git and running inside WSL. README now includes the clone/setup/run commands.
- Existing verification remains 2/2 Linux tests and 3/3 browser tests; publication changes affect documentation and Git line-ending metadata only.
