# Chronoscope: Linux Process Time-Travel Debugger

A working local debugger with a custom **C++ / Linux ptrace / CMake** backend, **REST + WebSockets**, and **React + TypeScript** UI. It records controlled demo programs, captures their faults and states, restores complete writable-memory/CPU checkpoints into a new live process, and actually executes forward to a selected recorded step. Historical inspection is a separate operation.

The project requirements were extracted from the provided two-page PDF. Scope, gaps, syscall semantics, design decisions and acceptance criteria are in [SPEC.md](SPEC.md); implementation milestones in [PLAN.md](PLAN.md); verification evidence and limitations in [STATUS.md](STATUS.md).

## Windows setup (tested path)

Prerequisites: Windows with **Ubuntu WSL2**, and Windows **Node.js 20+** (tested with 24.18.1). No Docker daemon is needed. If Ubuntu is absent, run `wsl --install -d Ubuntu`, finish its initial account setup, and confirm `wsl --list --verbose` reports version 2. Backend binaries must be built inside Linux.

From PowerShell:

For a fresh checkout, clone the repository and enter its directory first:

```powershell
git clone https://github.com/Piyush-Sagar/web-based-linux-process-time-travel-debugger.git
Set-Location web-based-linux-process-time-travel-debugger
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run.ps1
```

For the existing workspace on this machine:

```powershell
Set-Location 'C:\Users\sagar\Documents\ChatGPT\OS project'
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run.ps1
```

Setup installs GCC, CMake, Ninja, nlohmann-json and Boost in Ubuntu, runs the actual ptrace preflight, builds the Linux binaries, and installs/builds the frontend using Windows Node. It uses the WSL root account for package installation; the backend and traced programs run as the default normal Linux user. The `Bypass` option applies only to these process invocations and does not change machine policy.

Open **http://localhost:8080** in your Windows browser. Keep the run terminal open. Ctrl+C stops the server. The server serves the compiled frontend; no separate frontend server is necessary. Port 8080 is loopback-only, and WSL localhost forwarding connects the Windows browser to it. If another application uses 8080, use the manual command with another port and open that exact origin.

Manual backend commands from PowerShell:

```powershell
wsl -d Ubuntu --cd 'C:\Users\sagar\Documents\ChatGPT\OS project' -- python3 scripts/probe_linux.py
wsl -d Ubuntu --cd 'C:\Users\sagar\Documents\ChatGPT\OS project' -- cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
wsl -d Ubuntu --cd 'C:\Users\sagar\Documents\ChatGPT\OS project' -- cmake --build build -j 2
wsl -d Ubuntu --cd 'C:\Users\sagar\Documents\ChatGPT\OS project' -- ./build/ttd-server 8080 traces
```

Manual frontend commands, **using Windows Node**:

```powershell
Set-Location 'C:\Users\sagar\Documents\ChatGPT\OS project\frontend'
npm.cmd ci
npm.cmd run build
```

Ubuntu's distribution Node 18 can build the UI but is too old for the pinned browser test runner. Use the Windows Node 20+ workflow above, or install Node 20+ inside Linux for an entirely Linux frontend workflow. Do not reuse `node_modules` across Windows and Linux; run `npm ci` for the platform you use.

## Runnable crash/replay demonstration

1. Choose `crash.cpp`, click **Launch process**, then **Continue**. The tested GCC Debug build records SIGSEGV at step **141**, after capturing `getrandom`, `clock_gettime`, and the actual output `recorded once`.
2. Click **Deliver signal** to terminate the original process. Select step **39** using the timeline or numeric step input. The amber banner says **Historical inspection** and explicitly says execution has not been restored.
3. Click **Restore & replay to step**. A new PID is created; checkpoint **20** is restored; **19 instructions actually execute**. The green banner says **Live restored execution**, and the proof shows the checkpoint, target, and executed-instruction count. Every captured GPR and all 64 selected memory bytes are compared to recording.
4. Repeat at **1**, **57**, **140**, and **141**. At 141 the fault is reproduced. At checkpoint 140 itself no forward instructions are needed, but its CPU/memory state is restored into a live process and verified. Continue from a restored earlier state to replay the remaining recorded execution. The UI does not create branches beyond recorded history.
5. Filter **Syscalls & events** to see the nondeterministic inputs, captured output and signals. Registers and memory highlight changes relative to the previous recorded state.

Step 0 follows one bootstrap instruction at `_start`, making reserved EFLAGS bits consistent at real instruction stops. Steps are assembly instructions, not source lines. The exact crash step depends on compiler/build flags; query the recorded signal event if you change them.

Other demos: `clean.cpp` exits normally, `forbidden.cpp` attempts unsupported getpid and is rejected **before** that syscall executes, and `loop.cpp` demonstrates asynchronous pause and the recording limit.

With the server running, this PowerShell command also performs the full crash/history/termination/multiple-target replay demonstration, asserts matching registers/memory and zero duplicate output, and leaves a real session restored at step 39 for the browser:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\demo.ps1
```

## Verification

From the project root in PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify.ps1
```

This builds the backend, runs CTest, builds the UI, installs Playwright Chromium, and drives the real browser against a separate Linux backend on port **8092**. CTest uses **8091** for API tests. Both must be free. Tests use temporary trace directories, leaving normal demo traces untouched.

Individual checks in Ubuntu, from the repo root:

```bash
python3 scripts/probe_linux.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
python3 tests/engine_test.py build/ttd
python3 tests/api_test.py
```

Browser checks from Windows PowerShell, in `frontend`:

```powershell
npm.cmd run build
npx.cmd playwright install chromium
npm.cmd run test:e2e
```

Tests verify: crash and termination, earlier historical states, multiple live restored targets with exact GPR/memory matches, persisted traces reopened in a new tracer and after server restart, syscall input replay, no duplicate actual pipe output, normal exit, pause, recording limits, unsupported operations, corrupted trace checksums, deliberately inconsistent replay data, mapping mismatch, executable identity mismatch, WebSocket updates, browser control flow, keyboard timeline selection, reduced motion and narrow layouts.

Evidence files: `evidence/replay.json`, `evidence/api.json`, `evidence/browser-tests.json`, and browser screenshots. To refresh CLI/API evidence, redirect the corresponding Python commands into their JSON files. Browser tests refresh their report and screenshots automatically. Evidence contains PIDs from actual runs, which will differ next time.

## CLI

Run `./build/ttd` inside Ubuntu. Send one JSON command per line on stdin; each response is one JSON object with `ok`, `result` or `error` plus current state. Each process owns one session. Example:

```json
{"action":"launch","demo":"crash"}
{"action":"step"}
{"action":"continue"}
{"action":"history","step":39}
{"action":"step"}
{"action":"save","path":"traces/cli-crash.json"}
{"action":"restore","step":39}
{"action":"restore","step":57}
{"action":"restore","step":141}
```

Other commands: `state`, `events`, `pause`, `terminate`, `load` with a `path`; `continue` optionally accepts a bounded `count`. Default CLI continue is synchronous for scripted use. Send `{"action":"continue","async":true}` for interruptible execution in short batches, then `pause` to stop and inspect. `state` includes `running` and any `background_error`; no unsolicited JSON lines are emitted. All CLI ptrace operations run on one owning worker thread. On a crash, `step` delivers the fatal signal and captures termination. No editing variables, arbitrary memory writes or branching.

## HTTP/WebSocket contract

Server arguments: `ttd-server [port=8080] [trace-directory=traces] [frontend-dist-directory]`.

| Endpoint | Behavior |
|---|---|
| `GET /api/health` | Engine, platform and recording limit |
| `GET /api/demos` | Controlled executable IDs/descriptions |
| `GET /api/sessions` | Actual state for each session |
| `POST /api/sessions` | `{ "demo": "crash" }`, returns 201 and launched session |
| `GET /api/sessions/s1` | Live status, PID, cursor, checkpoint steps, GPRs/memory |
| `GET /api/sessions/s1/events` | Immutable recorded events |
| `GET /api/sessions/s1/state?step=39` | History only, `execution_restored: false` |
| `POST /api/sessions/s1/control` | `action`: step, continue, pause, terminate, restore; restore requires numeric `step` |
| `DELETE /api/sessions/s1` | Kill its tracee and remove this session's persisted trace |
| `WS /ws` | Connected message, then `{ "type": "session", "session": ... }` updates |

Restore returns `restoration.verified`, `checkpoint_step`, `target_step`, and `executed_instructions`. Background continue failures appear in session `error` and stop recording. Unsupported controls return 409; invalid requests 400; missing sessions 404; wrong Origin/Host 403. Browser and backend must share the same loopback origin; the optional Vite dev command does not bypass these checks. Use production build + C++ serving for the supported flow.

## Restoration, storage and limits

At checkpoints (every 20 steps), the engine stores **all writable private mappings**, GPRs and x86 extended CPU state. Selected memory shown in the UI is `watch_state[8]`, 64 bytes; it is only the inspector subset. Replay launches the same static non-PIE demo with fixed argv/environment and ASLR disabled, checks its writable mapping layout, writes the full checkpoint pages, sets CPU state, then steps forward. Ordinary instructions compute their results. Supported syscalls are replaced with an invalid syscall during replay and their recorded result/output buffer injected; ordinary registers are never copied from future recorded steps.

`getrandom` and `clock_gettime` provide captured nondeterministic input. Writes to stdout/stderr are captured through a real child pipe during recording and suppressed during replay; tests assert zero new output bytes. Exit syscalls and fatal signals can terminate execution. Every replayed stop compares GPRs and selected memory; divergence kills the replay child and reports an error.

Versioned JSON traces contain events, full checkpoint bytes, CPU state and executable identities with an accidental-corruption checksum. Saves use temporary-file replacement, not crash-durable fsync. Sessions save after explicit controls, pause, fault, exhaustion, and graceful shutdown; an abrupt server kill can lose the latest running batch. On restart, traces open as **history**, requiring explicit restore. Trace files are local trusted data, not an import format for hostile inputs. A build-time identity manifest restricts executable images to the controlled build. Checkpoints are tied to this Linux architecture, executable build and absolute path; moving/rebuilding images invalidates existing traces.

Limits: 8 sessions, 2,000 instructions/session, 256 KiB writable snapshot memory, 4 KiB per syscall payload, 64 KiB captured output, 32 MiB per stored trace, 8 KiB HTTP body, 16 WebSocket clients with bounded queues. Removing sessions frees their stored traces.

## Honest limitations

- This is an educational first version for four bundled **single-threaded, no-libc C++ demos**, using the same facilities that a small no-libc C demo could use. It does not replay normal dynamically linked applications or arbitrary C/C++ executables.
- Supported syscalls: bounded getrandom/clock_gettime into the selected demo buffer, write to stdout/stderr, exit/exit_group. Files, stdin reads, network, fork/clone, mapping changes, signal handlers and exec are rejected. There is no kernel-resource reconstruction.
- No source-level stepping, symbols beyond the selected demo memory symbol, breakpoints, arbitrary memory selection, attach, SIMD/x87 demos, asynchronous signal replay, vDSO time, CPU random/time instructions, shared memory, or threads. These paths are absent from approved demos and outside the replay contract. The service is not an untrusted-code sandbox.
- Every instruction incurs ptrace overhead. Complete snapshots use considerably more storage than optimized production record/replay tools. Pause waits for the current small step batch; restore is synchronous and bounded.
- Observed replay equality covers all 27 captured GPR fields and selected memory at each stop. Extended state and complete writable memory are restored, but replay comparison does not hash every full mapping at each instruction.
- `rr` was evaluated, not adopted. Installed rr 5.7 failed here due to perf-event permissions and an unrecognized CPU model under `-n`. General rr support would require a different engine integration and hardware validation.
- Tested on Ubuntu 24.04 / x86-64 WSL2 kernel 6.6.87.2, GCC 13.3, Windows Node 24 and Chromium. Other Linux kernels/CPUs are not claimed verified. A failed personality/ptrace/layout check stops restoration explicitly.

No temporary mocks are present in the delivered app.
