#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd "$(dirname "$0")/.." && pwd)
trace_dir=$(mktemp -d -t chronoscope-e2e.XXXXXX)
cd "$project_root"
exec "$project_root/build/ttd-server" 8092 "$trace_dir" "$project_root/frontend/dist"
