#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/linux-env.sh"
cd "$project_root"
python3 scripts/probe_linux.py
cmake --build build -j 2
ctest --test-dir build --output-on-failure
cd frontend
npm run build
# Installs Chromium and required Ubuntu libraries; may ask for sudo locally.
npx playwright install --with-deps chromium
npm run test:e2e
