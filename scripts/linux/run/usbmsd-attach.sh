#!/usr/bin/env bash
set -euo pipefail

MONITOR_HOST="${MONITOR_HOST:-127.0.0.1}"
MONITOR_PORT="${MONITOR_PORT:-4444}"
ARCH="${ARCH:-x86-32}"
FILE_SYSTEM="${FILE_SYSTEM:-ext2}"
BOOT_MODE="${BOOT_MODE:-mbr}"
BUILD_CONFIGURATION="${BUILD_CONFIGURATION:-debug}"
DEBUG_SPLIT="${DEBUG_SPLIT:-0}"
BUILD_SPLIT_SUFFIX=""
if [ "$DEBUG_SPLIT" = "1" ]; then
    BUILD_SPLIT_SUFFIX="-split"
fi
BUILD_CORE_NAME="${BUILD_CORE_NAME:-${ARCH}-${BOOT_MODE}-${BUILD_CONFIGURATION}${BUILD_SPLIT_SUFFIX}}"
BUILD_IMAGE_NAME="${BUILD_IMAGE_NAME:-${BUILD_CORE_NAME}-${FILE_SYSTEM}}"
IMG_PATH="${IMG_PATH:-build/image/${BUILD_IMAGE_NAME}/usb-3.img}"

if ! command -v nc >/dev/null 2>&1; then
    echo "nc not found. Install netcat or set up another monitor client."
    exit 1
fi

printf "drive_add 0 if=none,id=usbdrive0,file=%s,format=raw\n" "$IMG_PATH" | \
    nc -w 1 "$MONITOR_HOST" "$MONITOR_PORT" >/dev/null

printf "device_add usb-storage,drive=usbdrive0,bus=xhci.0,id=usbmsd0\n" | \
    nc -w 1 "$MONITOR_HOST" "$MONITOR_PORT" >/dev/null
