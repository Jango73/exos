#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

IMAGE_PATH="$ROOT_DIR/build/image/x86-64-mbr-debug-ext2/fs-test-ntfs.img"
FS_TEST_READ_SOURCE="$ROOT_DIR/boot-mbr/assets/fs-test-read.txt"
PARTITION_INDEX=1
LOOP_DEVICE=""
PARTITION_DEVICE=""
MOUNT_DIR=""

usage() {
    cat <<EOF
Usage: $0 [options]

Options:
  --image <path>        Target NTFS image
                        (default: build/image/x86-64-mbr-debug-ext2/fs-test-ntfs.img)
  --partition <index>   Partition index to mount (default: 1)
  --help                Show this help

Notes:
  - Must be run as root (sudo).
  - Populates Program Files test tree for NTFS driver validation.
EOF
}

require_command() {
    local command_name="$1"
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "Missing command: $command_name"
        exit 1
    }
}

set_output_ownership() {
    local target="$1"
    local uid="${SUDO_UID:-}"
    local gid="${SUDO_GID:-}"

    if [ -n "$uid" ] && [ -n "$gid" ]; then
        chown "$uid":"$gid" "$target" || true
    elif [ -n "${SUDO_USER:-}" ]; then
        chown "$SUDO_USER":"$SUDO_USER" "$target" || true
    fi
}

cleanup() {
    set +e
    if [ -n "$MOUNT_DIR" ] && mountpoint -q "$MOUNT_DIR"; then
        umount "$MOUNT_DIR"
    fi
    if [ -n "$LOOP_DEVICE" ]; then
        losetup -d "$LOOP_DEVICE"
    fi
    if [ -n "$MOUNT_DIR" ] && [ -d "$MOUNT_DIR" ]; then
        rmdir "$MOUNT_DIR" >/dev/null 2>&1 || true
    fi
    if [ -f "$IMAGE_PATH" ]; then
        set_output_ownership "$IMAGE_PATH"
    fi
    if [ -d "$(dirname "$IMAGE_PATH")" ]; then
        set_output_ownership "$(dirname "$IMAGE_PATH")"
    fi
}

attach_image() {
    local image="$1"
    local partition="$2"

    LOOP_DEVICE="$(losetup --find --show --partscan "$image")"
    PARTITION_DEVICE="${LOOP_DEVICE}p${partition}"
    if [ ! -b "$PARTITION_DEVICE" ]; then
        sleep 0.2
    fi
    if [ ! -b "$PARTITION_DEVICE" ]; then
        echo "Unable to find partition device ${PARTITION_DEVICE} for image $image"
        exit 1
    fi
}

mount_ntfs_partition() {
    local device="$1"

    MOUNT_DIR="$(mktemp -d /tmp/exos-ntfs-image.XXXXXX)"
    if mount -t ntfs3 "$device" "$MOUNT_DIR" 2>/dev/null; then
        return 0
    fi
    if mount -t ntfs "$device" "$MOUNT_DIR" 2>/dev/null; then
        return 0
    fi

    echo "Unable to mount NTFS partition ($device). Ensure ntfs3 support is available."
    exit 1
}

populate_ntfs_tree() {
    local root="$1"

    mkdir -p "$root/Program Files/PixelPaint"
    mkdir -p "$root/Program Files/HyperTerm"
    mkdir -p "$root/Program Files/AudioLab"
    mkdir -p "$root/Program Files/VectorNote"

    printf '%s\n' "PixelPaint executable placeholder" > "$root/Program Files/PixelPaint/pixelpaint.bin"
    printf '%s\n' "PixelPaint configuration placeholder" > "$root/Program Files/PixelPaint/pixelpaint.ini"
    printf '%s\n' "HyperTerm executable placeholder" > "$root/Program Files/HyperTerm/hyperterm.bin"
    printf '%s\n' "HyperTerm profile placeholder" > "$root/Program Files/HyperTerm/profile.txt"
    printf '%s\n' "AudioLab executable placeholder" > "$root/Program Files/AudioLab/audiolab.bin"
    printf '%s\n' "AudioLab presets placeholder" > "$root/Program Files/AudioLab/presets.txt"
    printf '%s\n' "VectorNote executable placeholder" > "$root/Program Files/VectorNote/vectornote.bin"
    printf '%s\n' "VectorNote templates placeholder" > "$root/Program Files/VectorNote/templates.txt"

    if [ ! -f "$root/read.txt" ]; then
        cp "$FS_TEST_READ_SOURCE" "$root/read.txt"
    fi
    sync
}

while [ $# -gt 0 ]; do
    case "$1" in
        --image)
            shift
            IMAGE_PATH="$1"
            ;;
        --partition)
            shift
            PARTITION_INDEX="$1"
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
    shift
done

if [ "$EUID" -ne 0 ]; then
    echo "Run as root (sudo)."
    exit 1
fi

if [ ! -f "$IMAGE_PATH" ]; then
    echo "Image not found: $IMAGE_PATH"
    exit 1
fi

require_command losetup
require_command mount
require_command umount
require_command sync

trap cleanup EXIT

attach_image "$IMAGE_PATH" "$PARTITION_INDEX"
mount_ntfs_partition "$PARTITION_DEVICE"
populate_ntfs_tree "$MOUNT_DIR"

echo "NTFS image populated: $IMAGE_PATH"
