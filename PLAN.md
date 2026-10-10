# Implementation plan

1. Inspect PDF, workspace, WSL/toolchain. Define supported contract in SPEC.md. Install missing Linux build dependencies. Probe ptrace, ASLR personality and rr on the actual host.
2. CMake, controlled demos, C++ engine and CLI: launch at exec stop, single-step, pause, bounded continue, exit and SIGSEGV. Verify CLI control transcript and rejection before unsupported syscalls.
3. JSON trace persistence: registers, selected memory, syscall data, signals; periodic full writable-memory and CPU-state checkpoints. Test save/load, bounds, malformed files and executable identity.
4. Restore checkpoint into fresh tracee, replay actual instructions with mediated recorded syscalls. Prove multiple target states including fault and restored continuation, log PIDs/checkpoint origins/comparison results. This milestone gates substantial frontend implementation.
5. C++ REST/WebSocket session API, serialized ptrace worker, asynchronous bounded continue/pause, origin checks, lifecycle cleanup. Test with real HTTP and WebSocket clients.
6. React/TypeScript frontend: session/demo controls, timeline, historical/live labels, explicit restore action, register and memory inspectors, event list, error states. Build and drive in browser.
7. End-to-end and negative tests; scripts for Windows/WSL setup, run and verify; final evidence/limits in STATUS.md.

## Verification commands (from repo inside target Ubuntu Linux)
`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`

`cmake --build build`

`ctest --test-dir build --output-on-failure`

`python3 tests/api_test.py`

Windows PowerShell, in `frontend`: `npm.cmd ci`, `npm.cmd run build`, `npx.cmd playwright install chromium`, `npm.cmd run test:e2e` (Windows Node >=20; tested 24). Entirely Linux frontend work also requires Node >=20 and a platform-specific npm ci.

CLI/API/setup commands are finalized in README. PowerShell scripts/setup.ps1, scripts/run.ps1 and scripts/verify.ps1 provide the supported Windows workflow. Update STATUS after every milestone and retain actual failures and fixes.

Native Ubuntu / VirtualBox: `bash scripts/setup.sh`, `bash scripts/run.sh`, `bash scripts/verify.sh`. Setup validates ptrace and replay in that guest before frontend build. Keep VirtualBox verification evidence separate from WSL evidence.
