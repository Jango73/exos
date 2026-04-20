#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT_DIR"

MONITOR_PORT="${MONITOR_PORT:-4462}"
KEYBOARD_LAYOUT="${KEYBOARD_LAYOUT:-en-US}"
ARCH="${ARCH:-x86-32}"
FS="${FS:-ext2}"
CONFIG="${CONFIG:-debug}"
DRAG_CYCLES="${DRAG_CYCLES:-20}"
DRAG_SEGMENTS="${DRAG_SEGMENTS:-70}"
DRAG_STEP_X="${DRAG_STEP_X:-8}"
DRAG_STEP_DELAY="${DRAG_STEP_DELAY:-0.015}"
AFTER_COMMAND_DELAY="${AFTER_COMMAND_DELAY:-1.0}"
SHELL_READY_PATTERN="${SHELL_READY_PATTERN:-[InitializeKernel] Shell task created}"
KERNEL_STALL_TIMEOUT_SECONDS="${KERNEL_STALL_TIMEOUT_SECONDS:-18}"
SHELL_READY_TIMEOUT_SECONDS="${SHELL_READY_TIMEOUT_SECONDS:-20}"

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
RUN_LOG="/tmp/windowing-run-${BUILD_CORE_NAME}.log"
QEMU_WRAPPER_PID=""

if [ ! -f "$IMG_PATH" ]; then
    echo "Missing image: $IMG_PATH"
    echo "Build first: bash scripts/linux/build/build.sh --arch $ARCH --fs $FS --$CONFIG"
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

        if rg -n "Checksum mismatch\\. Halting\\.|FileSize too small for checksum\\. Halting\\." "$RUN_LOG" >/dev/null 2>&1; then
            echo "Bootloader checksum failure detected (see $RUN_LOG)."
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
    local TimeoutSeconds="${2:-10}"
    local StartTime="$SECONDS"

    while [ $((SECONDS - StartTime)) -lt "$TimeoutSeconds" ]; do
        if rg -F -n "$Pattern" "$LOG_KERNEL" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done

    return 1
}

function SendShellCommandAndWait() {
    local CommandText="$1"
    local ValidationPattern="$2"
    local TimeoutSeconds="${3:-15}"
    local StartLine
    local LastTick
    local LastTickChange
    local CurrentTick

    SendKey "ret"
    sleep 0.3

    StartLine="$(wc -l < "$LOG_KERNEL" 2>/dev/null || echo 0)"
    SendCommand "$CommandText"
    sleep "$AFTER_COMMAND_DELAY"

    if [ -z "$ValidationPattern" ]; then
        return 0
    fi

    local StartTime="$SECONDS"
    LastTick="$(GetLastKernelTick)"
    LastTickChange="$SECONDS"
    while [ $((SECONDS - StartTime)) -lt "$TimeoutSeconds" ]; do
        if ! IsQemuAlive; then
            echo "QEMU exited while waiting for command completion: $CommandText"
            return 1
        fi
        if rg -n "#PF|#GP|#UD|#SS|#NP|#TS|#DE|#DF|#MF|#AC|#MC" "$LOG_KERNEL" >/dev/null 2>&1; then
            echo "Kernel fault detected while waiting for command completion: $CommandText"
            return 1
        fi
        CurrentTick="$(GetLastKernelTick)"
        if [ "$CurrentTick" -gt "$LastTick" ]; then
            LastTick="$CurrentTick"
            LastTickChange="$SECONDS"
        elif [ $((SECONDS - LastTickChange)) -ge "$KERNEL_STALL_TIMEOUT_SECONDS" ]; then
            echo "Kernel tick stalled while waiting for command completion: $CommandText"
            return 1
        fi

        if awk -v Start="$StartLine" 'NR > Start { print }' "$LOG_KERNEL" | rg -F -n "$ValidationPattern" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done

    echo "Command validation failed: $CommandText"
    return 1
}

function MouseButtonState() {
    local Mask="$1"
    MonitorCommand "mouse_button $Mask"
    sleep 0.02
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

        MonitorCommand "mouse_move $StepX $StepY"
        sleep "$DRAG_STEP_DELAY"
    done
}

function MouseHomeTopLeft() {
    local Index
    for ((Index = 0; Index < 70; Index++)); do
        MonitorCommand "mouse_move -20 -20"
        sleep 0.005
    done
}

function DragLoop() {
    local Cycle
    local SweepX=$((DRAG_SEGMENTS * DRAG_STEP_X))

    # Pointer to first window title bar (window A starts at 48,56 in Desktop-InternalTest).
    MouseHomeTopLeft
    MouseMoveSmooth 90 70 30

    for ((Cycle = 1; Cycle <= DRAG_CYCLES; Cycle++)); do
        echo "[windowing] drag cycle ${Cycle}/${DRAG_CYCLES}"

        MouseButtonState 1
        MouseMoveSmooth "$SweepX" 0 "$DRAG_SEGMENTS"
        MouseButtonState 0
        sleep 0.04

        MouseButtonState 1
        MouseMoveSmooth "$((-SweepX))" 0 "$DRAG_SEGMENTS"
        MouseButtonState 0
        sleep 0.06
    done
}

function SelectQemuMouseDevice() {
    # Try common mouse indexes. Invalid indexes are ignored.
    MonitorCommand "mouse_set 0" 5 1 || true
    MonitorCommand "mouse_set 1" 5 1 || true
    MonitorCommand "mouse_set 2" 5 1 || true
    MonitorCommand "mouse_set 0" 5 1 || true
}

function CheckFaults() {
    if rg -n "#PF|#GP|#UD|#SS|#NP|#TS|#DE|#DF|#MF|#AC|#MC" "$LOG_KERNEL" >/dev/null 2>&1; then
        echo "[windowing] fault detected in kernel log"
        rg -n "#PF|#GP|#UD|#SS|#NP|#TS|#DE|#DF|#MF|#AC|#MC" "$LOG_KERNEL" || true
        return 1
    fi
    return 0
}

killall -q qemu-system-i386 qemu-system-x86_64 || true
rm -f "$LOG_KERNEL"

SetImageKeyboardLayout "$IMG_PATH" "1048576" "$KEYBOARD_LAYOUT"

setsid /bin/bash -lc "MONITOR_PORT=$MONITOR_PORT scripts/linux/run/run --arch $ARCH --fs $FS --$CONFIG" >"$RUN_LOG" 2>&1 &
QEMU_WRAPPER_PID=$!

cleanup() {
    if kill -0 "$QEMU_WRAPPER_PID" 2>/dev/null; then
        kill "$QEMU_WRAPPER_PID" || true
    fi
    killall -q qemu-system-i386 qemu-system-x86_64 || true
}
trap cleanup EXIT

WaitForMonitor
WaitForShellReady

SendShellCommandAndWait "desktop show" "" || true
SendShellCommandAndWait "desktop stressdrag $DRAG_CYCLES" "[DesktopInternalRunStressDrag] Completed cycles=" "320"
sleep 1
CheckFaults

echo "[windowing] drag sequence completed (cycles=$DRAG_CYCLES, config=$CONFIG)."
echo "[windowing] logs: $LOG_KERNEL"
echo "[windowing] qemu wrapper log: $RUN_LOG"
