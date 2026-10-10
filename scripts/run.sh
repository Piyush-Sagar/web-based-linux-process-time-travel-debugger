#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/linux-env.sh"
cd "$project_root"
if [[ ! -x build/ttd-server || ! -f frontend/dist/index.html ]]; then
    echo 'Build the Linux backend and frontend first: bash scripts/setup.sh' >&2
    exit 1
fi
echo 'Open http://localhost:8080 in the browser INSIDE Ubuntu. Keep this terminal open; Ctrl+C stops the server.'
exec ./build/ttd-server 8080 traces
