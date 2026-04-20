#!/bin/bash
set -euo pipefail

# Common USB creation functions

check_device() {
    local DEVICE_PATH="$1"
    
    (( $(id -u) == 0 )) || { echo "ERROR: Run as root (sudo)."; exit 1; }

    [[ -b "$DEVICE_PATH" ]] || {
        echo "ERROR: $DEVICE_PATH is not a block device."
        echo "USB drives detected:"
        lsblk -dpo NAME,SIZE,MODEL,TRAN | grep -E 'sd|nvme'
        exit 1
    }

    if udevadm info -q property -n "$DEVICE_PATH" | grep -q '^ID_BUS=usb$'; then
        :  # c’est une clé USB → OK
    else
        echo "ERROR: $DEVICE_PATH is an internal drive. Refused."
        exit 1
    fi

    if lsblk -no TYPE "$DEVICE_PATH" 2>/dev/null | grep -q partition; then
        echo "ERROR: $DEVICE_PATH is a partition."
        echo "Give the whole device, e.g. /dev/sdb (not /dev/sdb1)."
        exit 1
    fi
}

confirm_flash() {
    local ARCH="$1"
    local IMAGE_PATH="$2"
    local DEVICE_PATH="$3"
    
    cat <<EOF

Ready to flash the kernel
Architecture : $ARCH
Image        : $IMAGE_PATH
Target       : $DEVICE_PATH  ($(lsblk -no SIZE "$DEVICE_PATH" | head -1))

/!\ THIS WILL ERASE EVERYTHING ON $DEVICE_PATH /!\\

EOF

    read -r -p "Type YES to continue: " REPLY
    [[ "$REPLY" == "YES" ]] || { echo "Aborted."; exit 0; }
}

flash_image() {
    local IMAGE_PATH="$1"
    local DEVICE_PATH="$2"
    
    echo "Writing image (this takes 10–30 seconds)..."
    dd if="$IMAGE_PATH" of="$DEVICE_PATH" bs=4M conv=fsync status=progress oflag=direct
    sync

    partprobe "$DEVICE_PATH" 2>/dev/null || true
    sleep 1
}

show_success() {
    local DEVICE_PATH="$1"
    local EJECTED="$2"

    cat <<EOF

SUCCESS! $DEVICE_PATH is now bootable.

Ejection: $EJECTED

1. Plug it into the target machine
2. Boot → select USB in BIOS/UEFI

EOF
}

eject_device() {
    local DEVICE_PATH="$1"

    if command -v udisksctl >/dev/null 2>&1; then
        if udisksctl power-off -b "$DEVICE_PATH" >/dev/null 2>&1; then
            return 0
        fi
    fi

    if command -v eject >/dev/null 2>&1; then
        if eject "$DEVICE_PATH" >/dev/null 2>&1; then
            return 0
        fi
    fi

    return 1
}

compute_usb_build_image_name() {
    local ARCH="$1"
    local BOOT_MODE="$2"
    local BUILD_CONFIGURATION="$3"
    local FILE_SYSTEM="$4"
    local DEBUG_SPLIT="$5"
    local SUFFIX=""

    if [ "$DEBUG_SPLIT" -eq 1 ]; then
        SUFFIX="-split"
    fi

    echo "${ARCH}-${BOOT_MODE}-${BUILD_CONFIGURATION}${SUFFIX}-${FILE_SYSTEM}"
}

parse_usb_flash_args() {
    local ARCH="$1"
    local BOOT_MODE="$2"
    shift 2

    USB_DEVICE_PATH=""
    USB_BUILD_CONFIGURATION="release"
    USB_FILE_SYSTEM="ext2"
    USB_DEBUG_SPLIT=0
    USB_BUILD_IMAGE_NAME=""

    while [ $# -gt 0 ]; do
        case "$1" in
            --debug)
                USB_BUILD_CONFIGURATION="debug"
                ;;
            --release)
                USB_BUILD_CONFIGURATION="release"
                ;;
            --fs)
                shift
                if [ $# -eq 0 ]; then
                    echo "Missing value for --fs"
                    return 1
                fi
                USB_FILE_SYSTEM="$1"
                ;;
            --split)
                USB_DEBUG_SPLIT=1
                ;;
            --build-image-name)
                shift
                if [ $# -eq 0 ]; then
                    echo "Missing value for --build-image-name"
                    return 1
                fi
                USB_BUILD_IMAGE_NAME="$1"
                ;;
            --help|-h)
                return 2
                ;;
            -*)
                echo "Unknown option: $1"
                return 1
                ;;
            *)
                if [ -n "$USB_DEVICE_PATH" ]; then
                    echo "Unexpected extra argument: $1"
                    return 1
                fi
                USB_DEVICE_PATH="$1"
                ;;
        esac
        shift
    done

    case "$USB_FILE_SYSTEM" in
        ext2|fat32)
            ;;
        *)
            echo "Unknown file system: $USB_FILE_SYSTEM"
            return 1
            ;;
    esac

    if [ -z "$USB_DEVICE_PATH" ]; then
        echo "Missing target device path."
        return 1
    fi

    if [ -z "$USB_BUILD_IMAGE_NAME" ]; then
        USB_BUILD_IMAGE_NAME="$(compute_usb_build_image_name \
            "$ARCH" \
            "$BOOT_MODE" \
            "$USB_BUILD_CONFIGURATION" \
            "$USB_FILE_SYSTEM" \
            "$USB_DEBUG_SPLIT")"
    fi

    if [ "$BOOT_MODE" = "uefi" ]; then
        USB_IMAGE_PATH="build/image/${USB_BUILD_IMAGE_NAME}/exos-uefi.img"
    else
        USB_IMAGE_PATH="build/image/${USB_BUILD_IMAGE_NAME}/exos.img"
    fi

    return 0
}
