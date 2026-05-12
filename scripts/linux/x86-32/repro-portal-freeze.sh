#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT_DIR"

MONITOR_PORT="${MONITOR_PORT:-4462}"
KEYBOARD_LAYOUT="${KEYBOARD_LAYOUT:-en-US}"
ARCH="${ARCH:-x86-32}"
FS="${FS:-ext2}"
CONFIG="${CONFIG:-debug}"
INPUT_BACKEND="${INPUT_BACKEND:-monitor}"
AFTER_BOOT_DELAY_SECONDS="${AFTER_BOOT_DELAY_SECONDS:-1.5}"
MOVE_DELAY_SECONDS="${MOVE_DELAY_SECONDS:-0.008}"
OUTER_CYCLES="${OUTER_CYCLES:-0}"
INNER_SWEEPS_PER_CYCLE="${INNER_SWEEPS_PER_CYCLE:-150}"
SHELL_READY_PATTERN="${SHELL_READY_PATTERN:-[InitializeKernel] Shell task created}"
PORTAL_READY_PATTERN="${PORTAL_READY_PATTERN:-[PortalShowDesktop] desktop ready}"
SHELL_READY_TIMEOUT_SECONDS="${SHELL_READY_TIMEOUT_SECONDS:-20}"
PORTAL_READY_TIMEOUT_SECONDS="${PORTAL_READY_TIMEOUT_SECONDS:-15}"
KERNEL_STALL_TIMEOUT_SECONDS="${KERNEL_STALL_TIMEOUT_SECONDS:-18}"
QEMU_WINDOW_NAME_PATTERN="${QEMU_WINDOW_NAME_PATTERN:-^QEMU$}"
XDOTOOL_GRAB_X="${XDOTOOL_GRAB_X:-640}"
XDOTOOL_GRAB_Y="${XDOTOOL_GRAB_Y:-512}"
FOCUS_SETTLE_DELAY_SECONDS="${FOCUS_SETTLE_DELAY_SECONDS:-0.2}"
QEMU_WINDOW_ID=""
POINTER_X=0
POINTER_Y=0

if [ "$ARCH" != "x86-32" ]; then
    echo "This script is intended for ARCH=x86-32."
    exit 1
fi

if [ "$CONFIG" != "debug" ] && [ "$CONFIG" != "release" ]; then
    echo "CONFIG must be debug or release."
    exit 1
fi

BUILD_CORE_NAME="${ARCH}-mbr-${CONFIG}"
BUILD_IMAGE_NAME="${BUILD_CORE_NAME}-${FS}"
IMG_PATH="build/image/${BUILD_IMAGE_NAME}/exos.img"
LOG_KERNEL="log/kernel-${BUILD_CORE_NAME}.log"
RUN_LOG="/tmp/repro-portal-freeze-${BUILD_CORE_NAME}.log"
QEMU_WRAPPER_PID=""

if [ ! -f "$IMG_PATH" ]; then
    echo "Missing image: $IMG_PATH"
    echo "Build first: bash scripts/linux/build/build --arch $ARCH --fs $FS --$CONFIG"
    exit 1
fi

source scripts/linux/utils/smoke-test-common.sh

function IsQemuAlive() {
    if [ -z "$QEMU_WRAPPER_PID" ]; then
        return 1
    fi
    kill -0 "$QEMU_WRAPPER_PID" 2>/dev/null
}

function GetLastKernelTick() {
    local TickText

    TickText="$(rg -o "T[0-9]+>" "$LOG_KERNEL" 2>/dev/null | tail -n 1 || true)"
    if [ -z "$TickText" ]; then
        echo "0"
        return 0
    fi

    echo "$TickText" | tr -cd '0-9'
}

function WaitForShellReady() {
    local StartTime="$SECONDS"
    local LastTick="0"
    local CurrentTick="0"
    local LastTickChange="$SECONDS"

    LastTick="$(GetLastKernelTick)"

    while ! rg -F -n "$SHELL_READY_PATTERN" "$LOG_KERNEL" >/dev/null 2>&1; do
        if ! IsQemuAlive; then
            echo "QEMU exited before shell-ready."
            return 1
        fi

        CurrentTick="$(GetLastKernelTick)"
        if [ "$CurrentTick" -gt "$LastTick" ]; then
            LastTick="$CurrentTick"
            LastTickChange="$SECONDS"
        elif [ $((SECONDS - LastTickChange)) -ge "$KERNEL_STALL_TIMEOUT_SECONDS" ]; then
            echo "Kernel tick stalled before shell-ready."
            return 1
        fi

        if [ $((SECONDS - StartTime)) -ge "$SHELL_READY_TIMEOUT_SECONDS" ]; then
            echo "Timed out waiting for shell-ready log pattern."
            return 1
        fi

        sleep 0.2
    done

    return 0
}

function WaitForLogPattern() {
    local Pattern="$1"
    local TimeoutSeconds="$2"
    local StartTime="$SECONDS"
    local LastTick
    local CurrentTick
    local LastTickChange="$SECONDS"

    LastTick="$(GetLastKernelTick)"

    while [ $((SECONDS - StartTime)) -lt "$TimeoutSeconds" ]; do
        if ! IsQemuAlive; then
            echo "QEMU exited while waiting for log pattern: $Pattern"
            return 1
        fi

        if rg -F -n "$Pattern" "$LOG_KERNEL" >/dev/null 2>&1; then
            return 0
        fi

        CurrentTick="$(GetLastKernelTick)"
        if [ "$CurrentTick" -gt "$LastTick" ]; then
            LastTick="$CurrentTick"
            LastTickChange="$SECONDS"
        elif [ $((SECONDS - LastTickChange)) -ge "$KERNEL_STALL_TIMEOUT_SECONDS" ]; then
            echo "Kernel tick stalled while waiting for log pattern: $Pattern"
            return 1
        fi

        sleep 0.2
    done

    echo "Timed out waiting for log pattern: $Pattern"
    return 1
}

function SetImagePortalAutoRun() {
    local ImagePath="$1"
    local FileSystemOffset="$2"
    local PartitionImage
    local ConfigFile
    local PatchedConfigFile
    local OffsetMegabytes
    local OffsetRemainder

    PartitionImage="$(mktemp)"
    ConfigFile="$(mktemp)"
    PatchedConfigFile="$(mktemp)"

    OffsetMegabytes=$((FileSystemOffset / 1048576))
    OffsetRemainder=$((FileSystemOffset % 1048576))

    if ! dd if="$ImagePath" of="$PartitionImage" iflag=skip_bytes skip="$FileSystemOffset" bs=1M status=none 2>/dev/null; then
        if [ "$OffsetRemainder" -eq 0 ]; then
            dd if="$ImagePath" of="$PartitionImage" bs=1M skip="$OffsetMegabytes" status=none
        else
            dd if="$ImagePath" of="$PartitionImage" bs=1 skip="$FileSystemOffset" status=none
        fi
    fi

    if ! debugfs -R "cat /exos.toml" "$PartitionImage" > "$ConfigFile" 2>/dev/null; then
        rm -f "$PartitionImage" "$ConfigFile" "$PatchedConfigFile"
        echo "Could not read /exos.toml from image: $ImagePath"
        return 1
    fi

    awk '
    BEGIN {
        has_portal = 0;
    }
    {
        print $0;
        if ($0 ~ /^Command[[:space:]]*=[[:space:]]*"\/system\/apps\/portal"$/) {
            has_portal = 1;
        }
    }
    END {
        if (has_portal == 0) {
            print "";
            print "[[Run]]";
            print "Command=\"/system/apps/portal\"";
        }
    }
    ' "$ConfigFile" > "$PatchedConfigFile"

    debugfs -w -R "rm /exos.toml" "$PartitionImage" >/dev/null 2>&1 || true
    if ! debugfs -w -R "write $PatchedConfigFile /exos.toml" "$PartitionImage" >/dev/null 2>&1; then
        rm -f "$PartitionImage" "$ConfigFile" "$PatchedConfigFile"
        echo "Could not write patched /exos.toml into image: $ImagePath"
        return 1
    fi

    if [ "$OffsetRemainder" -eq 0 ]; then
        dd if="$PartitionImage" of="$ImagePath" bs=1M seek="$OffsetMegabytes" conv=notrunc status=none
    else
        dd if="$PartitionImage" of="$ImagePath" bs=1 seek="$FileSystemOffset" conv=notrunc status=none
    fi

    if ! debugfs -R "cat /exos.toml" "$PartitionImage" 2>/dev/null | rg -F 'Command="/system/apps/portal"' >/dev/null; then
        rm -f "$PartitionImage" "$ConfigFile" "$PatchedConfigFile"
        echo "Portal auto-run verification failed for image: $ImagePath"
        return 1
    fi

    rm -f "$PartitionImage" "$ConfigFile" "$PatchedConfigFile"
}

function MouseMoveSmooth() {
    local TotalX="$1"
    local TotalY="$2"
    local Segments="$3"
    local Index
    local StepX
    local StepY

    if [ "$Segments" -le 0 ]; then
        Segments=1
    fi

    for ((Index = 0; Index < Segments; Index++)); do
        StepX=$((TotalX / Segments))
        StepY=$((TotalY / Segments))

        if [ "$Index" -eq $((Segments - 1)) ]; then
            StepX=$((TotalX - StepX * (Segments - 1)))
            StepY=$((TotalY - StepY * (Segments - 1)))
        fi

        if [ "$INPUT_BACKEND" = "xdotool" ]; then
            POINTER_X=$((POINTER_X + StepX))
            POINTER_Y=$((POINTER_Y + StepY))
            xdotool mousemove --sync --window "$QEMU_WINDOW_ID" "$POINTER_X" "$POINTER_Y"
        else
            MonitorCommand "mouse_move $StepX $StepY"
        fi
        sleep "$MOVE_DELAY_SECONDS"
    done
}

function MouseHomeTopLeft() {
    local Index

    if [ "$INPUT_BACKEND" = "xdotool" ]; then
        POINTER_X=5
        POINTER_Y=5
        xdotool mousemove --sync --window "$QEMU_WINDOW_ID" "$POINTER_X" "$POINTER_Y"
        sleep 0.02
        return 0
    fi

    for ((Index = 0; Index < 70; Index++)); do
        MonitorCommand "mouse_move -20 -20"
        sleep 0.005
    done
}

function SelectQemuMouseDevice() {
    if [ "$INPUT_BACKEND" = "xdotool" ]; then
        return 0
    fi

    MonitorCommand "mouse_set 0" 5 1 || true
    MonitorCommand "mouse_set 1" 5 1 || true
    MonitorCommand "mouse_set 2" 5 1 || true
    MonitorCommand "mouse_set 0" 5 1 || true
}

function EnsureInputBackendReady() {
    local WindowIDs

    if ! command -v xdotool >/dev/null 2>&1; then
        if [ "$INPUT_BACKEND" = "monitor" ]; then
            return 0
        fi
        echo "xdotool not found but INPUT_BACKEND=$INPUT_BACKEND"
        return 1
    fi

    WindowIDs="$(xdotool search --name "$QEMU_WINDOW_NAME_PATTERN" 2>/dev/null || true)"
    QEMU_WINDOW_ID="$(printf "%s\n" "$WindowIDs" | tail -n 1)"
    if [ -z "$QEMU_WINDOW_ID" ]; then
        if [ "$INPUT_BACKEND" = "monitor" ]; then
            return 0
        fi
        echo "Could not find QEMU window matching $QEMU_WINDOW_NAME_PATTERN"
        return 1
    fi

    xdotool windowactivate --sync "$QEMU_WINDOW_ID"
    sleep "$FOCUS_SETTLE_DELAY_SECONDS"
    xdotool mousemove --sync --window "$QEMU_WINDOW_ID" "$XDOTOOL_GRAB_X" "$XDOTOOL_GRAB_Y"
    sleep "$FOCUS_SETTLE_DELAY_SECONDS"
    xdotool mousedown --window "$QEMU_WINDOW_ID" 1
    sleep 0.05
    xdotool mouseup --window "$QEMU_WINDOW_ID" 1
    sleep "$FOCUS_SETTLE_DELAY_SECONDS"

    if [ "$INPUT_BACKEND" = "monitor" ]; then
        return 0
    fi

    return 0
}

function MovePointerToOnScreenDebugBorder() {
    MouseHomeTopLeft

    # Near the right/bottom edge of the 600x400 debug overlay.
    MouseMoveSmooth 560 200 40
}

function SweepOnScreenDebugBorder() {
    local SweepIndex

    for ((SweepIndex = 1; SweepIndex <= INNER_SWEEPS_PER_CYCLE; SweepIndex++)); do
        MouseMoveSmooth 90 0 12
        MouseMoveSmooth -90 0 12
        MouseMoveSmooth 0 230 20
        MouseMoveSmooth 0 -230 20
        MouseMoveSmooth 90 90 18
        MouseMoveSmooth -90 -90 18
    done
}

function PrintUsage() {
    cat <<'EOF'
Usage: bash scripts/linux/x86-32/repro-portal-freeze.sh

Environment overrides:
  INPUT_BACKEND                 xdotool or monitor
  MONITOR_PORT                  QEMU monitor port
  KEYBOARD_LAYOUT               Keyboard layout to inject in image
  FS                            Filesystem, default ext2
  CONFIG                        debug or release
  MOVE_DELAY_SECONDS            Delay between monitor mouse_move packets
  OUTER_CYCLES                  0 means infinite, otherwise finite cycle count
  INNER_SWEEPS_PER_CYCLE        Number of border sweeps per outer cycle
EOF
}

if [ "${1:-}" = "--help" ]; then
    PrintUsage
    exit 0
fi

killall -q qemu-system-i386 qemu-system-x86_64 || true
rm -f "$LOG_KERNEL"

SetImageKeyboardLayout "$IMG_PATH" "1048576" "$KEYBOARD_LAYOUT"
SetImagePortalAutoRun "$IMG_PATH" "1048576"

setsid /bin/bash -lc "MONITOR_PORT=$MONITOR_PORT scripts/linux/run/run --arch $ARCH --fs $FS --$CONFIG" >"$RUN_LOG" 2>&1 &
QEMU_WRAPPER_PID=$!

cleanup() {
    if kill -0 "$QEMU_WRAPPER_PID" 2>/dev/null; then
        kill "$QEMU_WRAPPER_PID" || true
    fi
    killall -q qemu-system-i386 qemu-system-x86_64 || true
}
trap cleanup EXIT

echo "[repro-portal-freeze] waiting for QEMU monitor on port $MONITOR_PORT"
WaitForMonitor

echo "[repro-portal-freeze] waiting for desktop boot"
WaitForShellReady
echo "[repro-portal-freeze] waiting for portal desktop"
WaitForLogPattern "$PORTAL_READY_PATTERN" "$PORTAL_READY_TIMEOUT_SECONDS"
sleep "$AFTER_BOOT_DELAY_SECONDS"
EnsureInputBackendReady

SelectQemuMouseDevice
MovePointerToOnScreenDebugBorder

echo "[repro-portal-freeze] sweeping OnScreenDebugInfo border"
echo "[repro-portal-freeze] kernel log: $LOG_KERNEL"
echo "[repro-portal-freeze] run log: $RUN_LOG"

if [ "$OUTER_CYCLES" -eq 0 ]; then
    while true; do
        SweepOnScreenDebugBorder
    done
fi

CycleIndex=1
while [ "$CycleIndex" -le "$OUTER_CYCLES" ]; do
    echo "[repro-portal-freeze] cycle $CycleIndex/$OUTER_CYCLES"
    SweepOnScreenDebugBorder
    CycleIndex=$((CycleIndex + 1))
done
