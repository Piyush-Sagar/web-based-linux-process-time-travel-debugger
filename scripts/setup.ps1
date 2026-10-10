$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
& wsl -d Ubuntu -- uname -m
if ($LASTEXITCODE -ne 0) { throw 'Install Ubuntu WSL2 first: wsl --install -d Ubuntu' }
& wsl -d Ubuntu -u root -- apt-get -o Acquire::ForceIPv4=true update -qq
if ($LASTEXITCODE -ne 0) { throw 'Ubuntu package index update failed' }
& wsl -d Ubuntu -u root -- apt-get -o Acquire::ForceIPv4=true install -y build-essential cmake ninja-build nlohmann-json3-dev libboost-system-dev python3
if ($LASTEXITCODE -ne 0) { throw 'Ubuntu dependency installation failed' }
& wsl -d Ubuntu --cd $projectRoot -- python3 scripts/probe_linux.py
if ($LASTEXITCODE -ne 0) { throw 'Linux ptrace preflight failed' }
& wsl -d Ubuntu --cd $projectRoot -- cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
& wsl -d Ubuntu --cd $projectRoot -- cmake --build build -j 2
if ($LASTEXITCODE -ne 0) { throw 'Linux build failed' }
if (-not (Get-Command node -ErrorAction SilentlyContinue)) { throw 'Install Windows Node.js 20 or later, then rerun setup.' }
$nodeMajor = [int]((& node --version).TrimStart('v').Split('.')[0])
if ($nodeMajor -lt 20) { throw 'Windows Node.js 20 or later is required for browser tests.' }
Push-Location -LiteralPath (Join-Path $projectRoot 'frontend')
try {
    & npm.cmd ci
    if ($LASTEXITCODE -ne 0) { throw 'Frontend dependency installation failed' }
    & npm.cmd run build
    if ($LASTEXITCODE -ne 0) { throw 'Frontend build failed' }
} finally { Pop-Location }
Write-Output 'Setup complete. Start with .\scripts\run.ps1, then visit http://localhost:8080'
