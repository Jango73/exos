#!/bin/sh
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_IMAGE_NAME="${BUILD_IMAGE_NAME:-x86-32-mbr-debug-ext2}"

sh "$SCRIPT_DIR/../utils/extract-exos-file.sh" \
    "$SCRIPT_DIR/../../build/image/$BUILD_IMAGE_NAME/exos.img" \
    "/exos/apps/terminal-tactics.sav" \
    "$SCRIPT_DIR/../../temp/terminal-tactics.sav"
