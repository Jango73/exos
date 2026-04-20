#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
EXTRACT="$SCRIPT_DIR/../utils/extract-exos-file.sh"
LOG_PATH="/exos/apps/terminal-tactics.log"
BUILD_IMAGE_NAME="${BUILD_IMAGE_NAME:-x86-32-mbr-debug-ext2}"
IMG="$SCRIPT_DIR/../../build/image/$BUILD_IMAGE_NAME/exos.img"

if [ ! -x "$EXTRACT" ]; then
    echo "Missing helper: $EXTRACT"
    exit 1
fi

sh "$EXTRACT" "$IMG" "$LOG_PATH" "$SCRIPT_DIR/../../temp/terminal-tactics.log"
