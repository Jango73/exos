#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT_DIR"

MONITOR_PORT="${MONITOR_PORT:-4458}"
GDB_PORT="${GDB_PORT:-1234}"
KEYBOARD_LAYOUT="${KEYBOARD_LAYOUT:-en-US}"
QEMU_STDOUT_LOG="${QEMU_STDOUT_LOG:-/tmp/qemu-x86-64-debug-auto.out}"
SHELL_READY_PATTERN="${SHELL_READY_PATTERN:-[InitializeKernel] Shell task created}"
TARGET_ARCH_GDB="${TARGET_ARCH_GDB:-i386:x86-64}"

BUILD_CORE_NAME="${BUILD_CORE_NAME:-x86-64-mbr-debug}"
BUILD_IMAGE_NAME="${BUILD_IMAGE_NAME:-x86-64-mbr-debug-ext2}"

DEBUG_ELF="build/core/${BUILD_CORE_NAME}/kernel/exos.elf"
IMG_PATH="build/image/${BUILD_IMAGE_NAME}/exos.img"
USB_IMG_PATH="build/image/${BUILD_IMAGE_NAME}/usb-3.img"
FS_TEST_EXT2_IMG_PATH="build/image/${BUILD_IMAGE_NAME}/fs-test-ext2.img"
FS_TEST_FAT32_IMG_PATH="build/image/${BUILD_IMAGE_NAME}/fs-test-fat32.img"
FS_TEST_NTFS_IMG_PATH="build/image/${BUILD_IMAGE_NAME}/fs-test-ntfs.img"
LOG_KERNEL="log/kernel-x86-64-mbr-debug.log"
LOG_COM1="log/debug-com1-x86-64-mbr-debug.log"

COMMAND_TO_SEND="${1:-}"
VALIDATION_PATTERN="${2:-${VALIDATION_PATTERN:-}}"
GDB_BREAKPOINTS="${GDB_BREAKPOINTS:-SetVideoMode;RealModeCall}"
GDB_DISABLE_INDEXES="${GDB_DISABLE_INDEXES:-2}"

if [ ! -f "$DEBUG_ELF" ]; then
    echo "Missing debug ELF: $DEBUG_ELF"
    echo "Build first: bash scripts/linux/build/build.sh --arch x86-64 --fs ext2 --debug"
    exit 1
fi

if [ ! -f "$IMG_PATH" ]; then
    echo "Missing image: $IMG_PATH"
    echo "Build first: bash scripts/linux/build/build.sh --arch x86-64 --fs ext2 --debug"
    exit 1
fi

source scripts/linux/utils/smoke-test-common.sh

killall -q qemu-system-x86_64 cgdb gdb || true
rm -f "$LOG_KERNEL" "$LOG_COM1"

# Ensure monitor sendkey maps to expected characters.
SetImageKeyboardLayout "$IMG_PATH" "1048576" "$KEYBOARD_LAYOUT"

setsid qemu-system-x86_64 \
    -machine q35,acpi=on,kernel-irqchip=split \
    -nodefaults \
    -smp cpus=1,cores=1,threads=1 \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-mouse,bus=xhci.0 \
    -drive format=raw,file="$USB_IMG_PATH",if=none,id=usbdrive0 \
    -device usb-storage,drive=usbdrive0,bus=xhci.0,id=usbmsd0 \
    -drive format=raw,file="$FS_TEST_EXT2_IMG_PATH",if=none,id=fsxt0 \
    -device nvme,drive=fsxt0,serial=exosfs0 \
    -drive format=raw,file="$FS_TEST_FAT32_IMG_PATH",if=none,id=fsxt1 \
    -device nvme,drive=fsxt1,serial=exosfs1 \
    -drive format=raw,file="$FS_TEST_NTFS_IMG_PATH",if=none,id=fsxt2 \
    -device nvme,drive=fsxt2,serial=exosfs2 \
    -device ahci,id=ahci \
    -drive format=raw,file="$IMG_PATH",if=none,id=drive0 \
    -device ide-hd,drive=drive0,bus=ahci.0 \
    -netdev user,id=net0 \
    -device e1000,netdev=net0 \
    -monitor telnet:127.0.0.1:${MONITOR_PORT},server,nowait \
    -serial file:"$LOG_COM1" \
    -serial file:"$LOG_KERNEL" \
    -vga std \
    -no-reboot \
    -s -S \
    >/dev/null 2>"$QEMU_STDOUT_LOG" < /dev/null &
QEMU_PID=$!

cleanup() {
    if kill -0 "$QEMU_PID" 2>/dev/null; then
        kill "$QEMU_PID" || true
    fi
}
trap cleanup EXIT

(
    function WaitForShellReady() {
        local StartTime="$SECONDS"
        while ! rg -F -n "$SHELL_READY_PATTERN" "$LOG_KERNEL" >/dev/null 2>&1; do
            if [ $((SECONDS - StartTime)) -ge 30 ]; then
                echo "[auto] Timed out waiting for shell-ready log."
                return 1
            fi
            sleep 0.2
        done
        return 0
    }

    function WaitForNewLogPattern() {
        local Pattern="$1"
        local StartLine="$2"
        local TimeoutSeconds="${3:-10}"
        local StartTime="$SECONDS"

        while [ $((SECONDS - StartTime)) -lt "$TimeoutSeconds" ]; do
            if awk -v Start="$StartLine" 'NR > Start { print }' "$LOG_KERNEL" | rg -F -n "$Pattern" >/dev/null 2>&1; then
                return 0
            fi
            sleep 0.2
        done

        return 1
    }

    function AutoSendCommandRobust() {
        local Attempt=1
        local StartLine=0

        if [ -z "$COMMAND_TO_SEND" ]; then
            return 0
        fi

        MONITOR_PORT="$MONITOR_PORT"
        WaitForMonitor

        while [ "$Attempt" -le 4 ]; do
            case "$Attempt" in
                1)
                    KEY_DELAY_SECONDS=0.14
                    COMMAND_DELAY_SECONDS=1.4
                    ;;
                2)
                    KEY_DELAY_SECONDS=0.18
                    COMMAND_DELAY_SECONDS=1.8
                    ;;
                3)
                    KEY_DELAY_SECONDS=0.24
                    COMMAND_DELAY_SECONDS=2.2
                    ;;
                *)
                    KEY_DELAY_SECONDS=0.28
                    COMMAND_DELAY_SECONDS=2.4
                    ;;
            esac

            # Resync shell on a fresh prompt line.
            SendKey "ret"
            sleep 0.4
            SendKey "ret"
            sleep 0.6

            StartLine="$(wc -l < "$LOG_KERNEL" 2>/dev/null || echo 0)"
            SendCommand "$COMMAND_TO_SEND"
            echo "[auto] Sent command attempt $Attempt: $COMMAND_TO_SEND"

            if [ -z "$VALIDATION_PATTERN" ]; then
                return 0
            fi

            if WaitForNewLogPattern "$VALIDATION_PATTERN" "$StartLine" 12; then
                echo "[auto] Command validated from kernel log."
                return 0
            fi

            echo "[auto] Command not validated, retrying..."
            Attempt=$((Attempt + 1))
        done

        echo "[auto] Failed to validate command after retries."
        return 1
    }

    WaitForShellReady
    AutoSendCommandRobust
) &

GDB_ARGS=(
    -ex "set architecture $TARGET_ARCH_GDB"
    -ex "set pagination off"
    -ex "set confirm off"
    -ex "target remote :$GDB_PORT"
)

IFS=';' read -r -a BreakpointList <<< "$GDB_BREAKPOINTS"
for Breakpoint in "${BreakpointList[@]}"; do
    if [ -n "$Breakpoint" ]; then
        GDB_ARGS+=(-ex "break $Breakpoint")
    fi
done

for BreakIndex in $GDB_DISABLE_INDEXES; do
    if [ -n "$BreakIndex" ]; then
        GDB_ARGS+=(-ex "disable $BreakIndex")
    fi
done

GDB_ARGS+=(-ex "continue")

gdb -q "$DEBUG_ELF" "${GDB_ARGS[@]}"
