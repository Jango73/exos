#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

usage() {
    cat << 'USAGE'
Usage: bash scripts/linux/setup/update-submodules.sh [--remote]

Options:
  --remote   Update each submodule to the latest remote-tracking commit.
             Without this flag, the script checks out the commit pinned by
             the superproject.
USAGE
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi

if [ "${1:-}" != "" ] && [ "${1:-}" != "--remote" ]; then
    echo "[update-submodules] Unsupported argument: ${1}" >&2
    usage
    exit 1
fi

cd "${REPO_ROOT}"

echo "[update-submodules] Repository root: ${REPO_ROOT}"

git submodule sync --recursive

if [ "${1:-}" = "--remote" ]; then
    echo "[update-submodules] Running: git submodule update --init --recursive --remote"
    git submodule update --init --recursive --remote
else
    echo "[update-submodules] Running: git submodule update --init --recursive"
    git submodule update --init --recursive
fi

echo "[update-submodules] Done"
