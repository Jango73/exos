#!/usr/bin/env bash
# Thin shim: the dashboard engine lives in dashboard/ (git submodule).
# It runs with this folder as working directory, so the engine loads
# the project dashboard.json from here.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "$ROOT_DIR/dashboard/dashboard.sh"
