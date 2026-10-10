#!/usr/bin/env bash
# Source this from Linux scripts; never use Windows node_modules in the guest.
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
if [[ -x "$project_root/.tools/node/bin/node" ]]; then
    export PATH="$project_root/.tools/node/bin:$PATH"
fi
export NODE_OPTIONS="${NODE_OPTIONS:+$NODE_OPTIONS }--dns-result-order=ipv4first"
