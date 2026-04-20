#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT_DIR"

MONITOR_PORT="${MONITOR_PORT:-4463}"
GDB_PORT="${GDB_PORT:-1234}"
KEYBOARD_LAYOUT="${KEYBOARD_LAYOUT:-en-US}"
BUILD_CORE_NAME="${BUILD_CORE_NAME:-x86-32-mbr-debug}"
BUILD_IMAGE_NAME="${BUILD_IMAGE_NAME:-x86-32-mbr-debug-ext2}"
DRAG_CYCLES="${DRAG_CYCLES:-20}"
SHELL_READY_TIMEOUT_SECONDS="${SHELL_READY_TIMEOUT_SECONDS:-25}"

DEBUG_ELF="build/core/${BUILD_CORE_NAME}/kernel/exos.elf"
IMG_PATH="build/image/${BUILD_IMAGE_NAME}/exos.img"
USB_IMG_PATH="build/image/${BUILD_IMAGE_NAME}/usb-3.img"
FS_TEST_EXT2_IMG_PATH="build/image/${BUILD_IMAGE_NAME}/fs-test-ext2.img"
FS_TEST_FAT32_IMG_PATH="build/image/${BUILD_IMAGE_NAME}/fs-test-fat32.img"
FS_TEST_NTFS_IMG_PATH="build/image/${BUILD_IMAGE_NAME}/fs-test-ntfs.img"
LOG_KERNEL="log/kernel-x86-32-mbr-debug.log"
LOG_COM1="log/debug-com1-x86-32-mbr-debug.log"
QEMU_STDOUT_LOG="/tmp/qemu-x86-32-windowing-gdb.out"
GDB_COMMANDS_FILE="/tmp/exos-windowing-gdb.cmd"

if [ ! -f "$DEBUG_ELF" ] || [ ! -f "$IMG_PATH" ]; then
    echo "Missing build artifacts. Build first: bash scripts/linux/build/build.sh --arch x86-32 --fs ext2 --debug"
    exit 1
fi

source scripts/linux/utils/smoke-test-common.sh

killall -q qemu-system-i386 qemu-system-x86_64 gdb || true
rm -f "$LOG_KERNEL" "$LOG_COM1"
SetImageKeyboardLayout "$IMG_PATH" "1048576" "$KEYBOARD_LAYOUT"

setsid qemu-system-i386 \
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
    >"$QEMU_STDOUT_LOG" 2>&1 < /dev/null &
QEMU_PID=$!

cleanup() {
    if kill -0 "$QEMU_PID" 2>/dev/null; then
        kill "$QEMU_PID" || true
    fi
}
trap cleanup EXIT

(
    local_ready=0
    WaitForMonitor || exit 0

    # Wait shell-ready log then trigger windowing stress.
    for _ in $(seq 1 $((SHELL_READY_TIMEOUT_SECONDS * 10))); do
        if rg -F -n "[InitializeKernel] Shell task created" "$LOG_KERNEL" >/dev/null 2>&1; then
            local_ready=1
            break
        fi
        sleep 0.1
    done

    if [ "$local_ready" -eq 1 ]; then
        SendKey "ret"
        sleep 0.3
        SendCommand "desktop show"
        sleep 0.8
        SendCommand "desktop stressdrag $DRAG_CYCLES"
    else
        echo "BOOT_NOT_READY_WITHIN_TIMEOUT"
        if kill -0 "$QEMU_PID" 2>/dev/null; then
            kill "$QEMU_PID" || true
        fi
    fi
) &

cat > "$GDB_COMMANDS_FILE" <<EOF
set architecture i386
set pagination off
set confirm off
target remote :$GDB_PORT

break DoubleFaultHandler
commands
silent
printf "\\n*** HIT DoubleFaultHandler ***\\n"
bt
info reg
continue
end

break GeneralProtectionHandler
commands
silent
printf "\\n*** HIT GeneralProtectionHandler ***\\n"
bt
info reg
continue
end

break PageFaultHandler
commands
silent
printf "\\n*** HIT PageFaultHandler ***\\n"
bt
info reg
continue
end

break StackFaultHandler
commands
silent
printf "\\n*** HIT StackFaultHandler ***\\n"
bt
info reg
continue
end

break SegmentFaultHandler
commands
silent
printf "\\n*** HIT SegmentFaultHandler ***\\n"
bt
info reg
continue
end

break DesktopInternalRunStressDrag
commands
silent
printf "\\n*** HIT DesktopInternalRunStressDrag ***\\n"
bt 8
continue
end

continue
EOF

gdb -q "$DEBUG_ELF" -x "$GDB_COMMANDS_FILE"
