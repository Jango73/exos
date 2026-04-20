#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT_DIR"

MONITOR_PORT="${MONITOR_PORT:-4465}"
GDB_PORT="${GDB_PORT:-1234}"
KEYBOARD_LAYOUT="${KEYBOARD_LAYOUT:-en-US}"
SHELL_READY_TIMEOUT_SECONDS="${SHELL_READY_TIMEOUT_SECONDS:-30}"
DESKTOP_SETTLE_SECONDS="${DESKTOP_SETTLE_SECONDS:-5}"
POST_F12_WAIT_SECONDS="${POST_F12_WAIT_SECONDS:-6}"

BUILD_CORE_NAME="${BUILD_CORE_NAME:-x86-64-uefi-debug}"
BUILD_IMAGE_NAME="${BUILD_IMAGE_NAME:-x86-64-uefi-debug-ext2}"
DEBUG_ELF="build/core/${BUILD_CORE_NAME}/kernel/exos.elf"
IMG_PATH="build/image/${BUILD_IMAGE_NAME}/exos-uefi.img"
USB_IMG_PATH="build/image/${BUILD_IMAGE_NAME}/usb-3.img"
FS_TEST_EXT2_IMG_PATH="build/image/${BUILD_IMAGE_NAME}/fs-test-ext2.img"
FS_TEST_FAT32_IMG_PATH="build/image/${BUILD_IMAGE_NAME}/fs-test-fat32.img"
FS_TEST_NTFS_IMG_PATH="build/image/${BUILD_IMAGE_NAME}/fs-test-ntfs.img"
LOG_KERNEL="log/kernel-x86-64-uefi-debug.log"
LOG_COM1="log/debug-com1-x86-64-uefi-debug.log"
QEMU_STDOUT_LOG="/tmp/qemu-x86-64-uefi-f12-gdb.out"
GDB_DUMP_LOG="/tmp/gdb-x86-64-uefi-f12-freeze.log"

OVMF_CODE_CANDIDATES=(
    "/usr/share/OVMF/OVMF_CODE.fd"
    "/usr/share/edk2/ovmf/OVMF_CODE.fd"
    "/usr/share/qemu/OVMF_CODE.fd"
)
OVMF_VARS_CANDIDATES=(
    "/usr/share/OVMF/OVMF_VARS.fd"
    "/usr/share/edk2/ovmf/OVMF_VARS.fd"
    "/usr/share/qemu/OVMF_VARS.fd"
)

function FindFirmwareFile() {
    local EnvValue="$1"
    shift

    if [ -n "$EnvValue" ] && [ -f "$EnvValue" ]; then
        echo "$EnvValue"
        return 0
    fi

    for Candidate in "$@"; do
        if [ -f "$Candidate" ]; then
            echo "$Candidate"
            return 0
        fi
    done

    return 1
}

if [ ! -f "$DEBUG_ELF" ] || [ ! -f "$IMG_PATH" ]; then
    echo "Missing build artifacts. Build first: ./scripts/linux/build/build --arch x86-64 --fs ext2 --debug --uefi"
    exit 1
fi

if [ ! -f "$USB_IMG_PATH" ] || [ ! -f "$FS_TEST_EXT2_IMG_PATH" ] || [ ! -f "$FS_TEST_FAT32_IMG_PATH" ] || [ ! -f "$FS_TEST_NTFS_IMG_PATH" ]; then
    echo "Missing one or more support images under build/image/${BUILD_IMAGE_NAME}"
    exit 1
fi

OVMF_CODE_PATH="$(FindFirmwareFile "${OVMF_CODE:-}" "${OVMF_CODE_CANDIDATES[@]}")" || {
    echo "OVMF code firmware not found. Set OVMF_CODE to a valid file."
    exit 1
}
OVMF_VARS_PATH="$(FindFirmwareFile "${OVMF_VARS:-}" "${OVMF_VARS_CANDIDATES[@]}")" || {
    echo "OVMF vars firmware not found. Set OVMF_VARS to a valid file."
    exit 1
}
OVMF_VARS_COPY="build/image/${BUILD_IMAGE_NAME}/work-uefi/ovmf-vars-debug-f12.fd"
cp -f "$OVMF_VARS_PATH" "$OVMF_VARS_COPY"

source scripts/linux/utils/smoke-test-common.sh

killall -q qemu-system-x86_64 gdb cgdb || true
rm -f "$LOG_KERNEL" "$LOG_COM1" "$GDB_DUMP_LOG"

# UEFI image filesystem starts at 4194304 in this build layout.
SetImageKeyboardLayout "$IMG_PATH" "4194304" "$KEYBOARD_LAYOUT"

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
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE_PATH" \
    -drive if=pflash,format=raw,file="$OVMF_VARS_COPY" \
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
    -s \
    >"$QEMU_STDOUT_LOG" 2>&1 < /dev/null &
QEMU_PID=$!

cleanup() {
    if kill -0 "$QEMU_PID" 2>/dev/null; then
        kill "$QEMU_PID" || true
    fi
}
trap cleanup EXIT

WaitForMonitor

StartTime="$SECONDS"
while ! rg -F -n "[InitializeKernel] Shell task created" "$LOG_KERNEL" >/dev/null 2>&1; do
    if ! kill -0 "$QEMU_PID" 2>/dev/null; then
        echo "QEMU exited before shell-ready"
        exit 1
    fi

    if [ $((SECONDS - StartTime)) -ge "$SHELL_READY_TIMEOUT_SECONDS" ]; then
        echo "Timed out waiting for shell-ready log"
        exit 1
    fi

    sleep 0.2
done

echo "[f12-debug] shell ready detected"
SendKey "ret"
sleep 0.3
SendCommand "desktop show"
echo "[f12-debug] desktop show sent"
sleep "$DESKTOP_SETTLE_SECONDS"
SendKey "f12"
echo "[f12-debug] F12 sent"
sleep "$POST_F12_WAIT_SECONDS"

# Capture the exact execution point after the expected freeze window.
gdb -q "$DEBUG_ELF" \
    -ex "set architecture i386:x86-64" \
    -ex "set pagination off" \
    -ex "set confirm off" \
    -ex "target remote :$GDB_PORT" \
    -ex "interrupt" \
    -ex "printf \"\\n==== GDB SNAPSHOT AFTER F12 ====\\n\"" \
    -ex "bt" \
    -ex "bt 20" \
    -ex "info reg" \
    -ex "x/16i \$rip" \
    -ex "info threads" \
    -ex "detach" \
    -ex "quit" \
    >"$GDB_DUMP_LOG" 2>&1 || true

echo "[f12-debug] gdb snapshot saved: $GDB_DUMP_LOG"
echo "[f12-debug] kernel log: $LOG_KERNEL"
echo "[f12-debug] qemu stdout: $QEMU_STDOUT_LOG"

tail -n 120 "$GDB_DUMP_LOG" || true
