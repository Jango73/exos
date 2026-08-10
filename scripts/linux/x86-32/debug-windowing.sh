#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
exec node "$ROOT_DIR/scripts/linux/x86-32/debug-windowing.js" "$@"
