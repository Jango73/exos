#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
export SMOKE_TEST_SCRIPT_NAME="$0"
exec node "$ROOT_DIR/scripts/linux/test/smoke-test-scripts.js" "$@"
