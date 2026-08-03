#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
SMOKE_TEST_SCRIPT_NAME="$0"
SMOKE_TEST_DEFAULT_COMMANDS_FILE="$ROOT_DIR/scripts/common/smoke-test-scripts-commands.txt"
SMOKE_TEST_REQUIRE_LOCAL_HTTP_SERVER=0

# shellcheck source=/dev/null
source "$ROOT_DIR/scripts/linux/utils/smoke-test-common.sh"

RUN_X86_32=1
RUN_X86_32_RTL8139=0
RUN_X86_64=1
RUN_X86_64_UEFI=0

SmokeTestMain "$@"
