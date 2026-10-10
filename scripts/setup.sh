#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/linux-env.sh"
cd "$project_root"
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo 'Setup requires x86-64 Linux. Run this in your Ubuntu VirtualBox guest.' >&2
    exit 1
fi
case "${1:-}" in
    '')
        if [[ $EUID -eq 0 ]]; then
            echo 'Run as your normal Ubuntu user; sudo is used only to install packages.' >&2
            exit 1
        fi
        sudo apt-get -o Acquire::ForceIPv4=true update
        sudo apt-get -o Acquire::ForceIPv4=true install -y build-essential cmake ninja-build nlohmann-json3-dev libboost-system-dev python3 curl xz-utils
        ;;
    --skip-system-packages) ;; # For a machine whose build dependencies are installed.
    *) echo 'Usage: bash scripts/setup.sh [--skip-system-packages]' >&2; exit 2 ;;
esac
node_major=0
if command -v node >/dev/null 2>&1; then
    node_major=$(node -p 'Number(process.versions.node.split(".")[0])')
fi
if (( node_major < 20 )); then
    # Official pinned Linux binary, validated against its published SHA-256.
    archive=node-v22.23.3-linux-x64.tar.xz
    expected_sha=df450af89261115ef9f9e3830c3eeb2cc9213b63c720b1af623cb5dcbe2e02de
    mkdir -p "$project_root/.tools/node"
    download_dir=$(mktemp -d "$project_root/.tools/node-download.XXXXXX")
    trap 'rm -f -- "$download_dir/$archive"; rmdir -- "$download_dir"' EXIT
    curl -4 --fail --location --retry 2 --connect-timeout 20 "https://nodejs.org/dist/v22.23.3/$archive" --output "$download_dir/$archive"
    printf '%s  %s\n' "$expected_sha" "$download_dir/$archive" | sha256sum --check -
    tar -xJf "$download_dir/$archive" -C "$project_root/.tools/node" --strip-components=1 --no-same-owner
    export PATH="$project_root/.tools/node/bin:$PATH"
fi
printf 'Using Linux Node %s\n' "$(node --version)"
python3 scripts/probe_linux.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
cd frontend
npm ci
npm run build
printf '\nSetup and Linux replay tests passed. Start: bash scripts/run.sh (from the repo root).\n'
