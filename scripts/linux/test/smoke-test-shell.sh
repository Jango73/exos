#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
SMOKE_TEST_SCRIPT_NAME="$0"
SMOKE_TEST_DEFAULT_COMMANDS_FILE="$ROOT_DIR/scripts/common/smoke-test-shell-commands.txt"

# shellcheck source=/dev/null
source "$ROOT_DIR/scripts/linux/utils/smoke-test-common.sh"
SmokeTestMain "$@"
