#!/bin/bash
set -e

function Usage() {
    echo "Usage: $0 --arch <x86-32|x86-64> [--fs <ext2|fat32>] [--debug|--release] [--split] [--build-core-name <name>] [--build-image-name <name>] [--gdb] [--usb3|--no-usb3] [--uefi] [--nvme|--no-nvme] [--ntfs-live] [--net-card <e1000|rtl8139> ...]"
}

ARCH="x86-32"
FILE_SYSTEM="ext2"
USE_GDB=0
USE_UEFI=0
USB3_ENABLED=1
NVME_ENABLED=1
NTFS_LIVE_ENABLED=0
MONITOR_PORT="${MONITOR_PORT:-4444}"
BOOT_MODE="mbr"
BUILD_CONFIGURATION="release"
DEBUG_SPLIT=0
BUILD_CORE_NAME=""
BUILD_IMAGE_NAME=""
CORE_BUILD_DIR=""
IMAGE_BUILD_DIR=""
BUILD_LOCK_DIR=""
LOG_CONFIGURATION=""
NETWORK_CARDS=()

function ComputeBuildNames() {
    local Suffix=""

    if [ "$DEBUG_SPLIT" -eq 1 ]; then
        Suffix="-split"
    fi

    if [ -z "$BUILD_CORE_NAME" ]; then
        BUILD_CORE_NAME="${ARCH}-${BOOT_MODE}-${BUILD_CONFIGURATION}${Suffix}"
    fi

    if [ -z "$BUILD_IMAGE_NAME" ]; then
        BUILD_IMAGE_NAME="${BUILD_CORE_NAME}-${FILE_SYSTEM}"
    fi
}

function WaitForBuildIfNeeded() {
    BUILD_LOCK_DIR="build/core/$BUILD_CORE_NAME/.build-lock"
    local WaitLoops=0
    local MaxWaitLoops=300

    while [ -d "$BUILD_LOCK_DIR" ]; do
        local BuildPid=""

        if [ -f "$BUILD_LOCK_DIR/pid" ]; then
            BuildPid="$(cat "$BUILD_LOCK_DIR/pid" 2>/dev/null || true)"
        fi

        if [ -n "$BuildPid" ] && ! kill -0 "$BuildPid" 2>/dev/null; then
            rm -rf "$BUILD_LOCK_DIR"
            break
        fi

        if [ "$WaitLoops" -eq 0 ]; then
            echo "Build lock detected for $BUILD_CORE_NAME, waiting for image generation..."
        fi

        sleep 0.2
        WaitLoops=$((WaitLoops + 1))
        if [ "$WaitLoops" -ge "$MaxWaitLoops" ]; then
            echo "Timed out waiting for build lock: $BUILD_LOCK_DIR"
            exit 1
        fi
    done
}

function ResolveLogConfiguration() {
    if [[ "$BUILD_CORE_NAME" == *-debug* ]]; then
        LOG_CONFIGURATION="debug"
    elif [[ "$BUILD_CORE_NAME" == *-release* ]]; then
        LOG_CONFIGURATION="release"
    else
        LOG_CONFIGURATION="$BUILD_CONFIGURATION"
    fi
}

while [ $# -gt 0 ]; do
    case "$1" in
        --arch)
            shift
            ARCH="$1"
            ;;
        --gdb)
            USE_GDB=1
            ;;
        --fs)
            shift
            FILE_SYSTEM="$1"
            ;;
        --debug)
            BUILD_CONFIGURATION="debug"
            ;;
        --release)
            BUILD_CONFIGURATION="release"
            ;;
        --split)
            DEBUG_SPLIT=1
            ;;
        --usb3)
            USB3_ENABLED=1
            ;;
        --no-usb3)
            USB3_ENABLED=0
            ;;
        --uefi)
            USE_UEFI=1
            BOOT_MODE="uefi"
            ;;
        --nvme)
            NVME_ENABLED=1
            ;;
        --no-nvme)
            NVME_ENABLED=0
            ;;
        --ntfs-live)
            NTFS_LIVE_ENABLED=1
            ;;
        --net-card)
            shift
            NETWORK_CARDS+=("$1")
            ;;
        --build-core-name)
            shift
            BUILD_CORE_NAME="$1"
            ;;
        --build-image-name)
            shift
            BUILD_IMAGE_NAME="$1"
            ;;
        --help|-h)
            Usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            Usage
            exit 1
            ;;
    esac
    shift
done

case "$FILE_SYSTEM" in
    ext2|fat32)
        ;;
    *)
        echo "Unknown file system: $FILE_SYSTEM"
        Usage
        exit 1
        ;;
esac

if [ "${#NETWORK_CARDS[@]}" -eq 0 ]; then
    NETWORK_CARDS=("e1000")
fi

for NetworkCard in "${NETWORK_CARDS[@]}"; do
    case "$NetworkCard" in
        e1000|rtl8139)
            ;;
        *)
            echo "Unknown network card: $NetworkCard"
            Usage
            exit 1
            ;;
    esac
done

if [ -z "$BUILD_CORE_NAME" ] && [ -n "$BUILD_IMAGE_NAME" ]; then
    case "$BUILD_IMAGE_NAME" in
        *-ext2)
            BUILD_CORE_NAME="${BUILD_IMAGE_NAME%-ext2}"
            FILE_SYSTEM="ext2"
            ;;
        *-fat32)
            BUILD_CORE_NAME="${BUILD_IMAGE_NAME%-fat32}"
            FILE_SYSTEM="fat32"
            ;;
        *)
            echo "Cannot derive build core name from image name: $BUILD_IMAGE_NAME"
            echo "Use --build-core-name explicitly."
            exit 1
            ;;
    esac
fi

ComputeBuildNames
ResolveLogConfiguration
CORE_BUILD_DIR="build/core/$BUILD_CORE_NAME"
IMAGE_BUILD_DIR="build/image/$BUILD_IMAGE_NAME"

case "$ARCH" in
    x86-32)
        QEMU_BIN_DEFAULT="qemu-system-i386"
        OVMF_CODE_CANDIDATES=(
            "/usr/share/OVMF/OVMF32_CODE.fd"
            "/usr/share/edk2/ovmf/OVMF32_CODE.fd"
            "/usr/share/qemu/OVMF32_CODE.fd"
        )
        OVMF_VARS_CANDIDATES=(
            "/usr/share/OVMF/OVMF32_VARS.fd"
            "/usr/share/edk2/ovmf/OVMF32_VARS.fd"
            "/usr/share/qemu/OVMF32_VARS.fd"
        )
        ;;
    x86-64)
        QEMU_BIN_DEFAULT="qemu-system-x86_64"
        DEBUG_GDB="scripts/common/x86-64/debug.gdb"
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
        ;;
    *)
        echo "Unknown architecture: $ARCH"
        Usage
        exit 1
        ;;
esac

IMG_PATH="$IMAGE_BUILD_DIR/exos.img"
USB_3_PATH="$IMAGE_BUILD_DIR/usb-3.img"
FS_TEST_EXT2_IMG_PATH="$IMAGE_BUILD_DIR/fs-test-ext2.img"
FS_TEST_FAT32_IMG_PATH="$IMAGE_BUILD_DIR/fs-test-fat32.img"
FS_TEST_NTFS_IMG_PATH="$IMAGE_BUILD_DIR/fs-test-ntfs.img"
NTFS_LIVE_IMG_PATH="build/test-images/ntfs-live.img"
FLOPPY_35_PATH="${FLOPPY_35_PATH:-$IMAGE_BUILD_DIR/floppy-3.5.img}"
CYCLE_BIN="$CORE_BUILD_DIR/tools/cycle"
DEBUG_ELF="$CORE_BUILD_DIR/kernel/exos.elf"

WaitForBuildIfNeeded

QEMU_BIN="${QEMU_BIN:-$QEMU_BIN_DEFAULT}"

if [ "$USE_UEFI" -eq 1 ]; then
    IMG_PATH="$IMAGE_BUILD_DIR/exos-uefi.img"
fi

if [ ! -f "$IMG_PATH" ]; then
    echo "Image not found: $IMG_PATH"
    exit 1
fi

if [ "$USB3_ENABLED" -eq 1 ] && [ ! -f "$USB_3_PATH" ]; then
    echo "Image not found: $USB_3_PATH"
    exit 1
fi

if [ ! -f "$FLOPPY_35_PATH" ]; then
    echo "Floppy image not found: $FLOPPY_35_PATH"
    exit 1
fi

mkdir -p log

LOG_DEBUG_COM1="log/debug-com1-${ARCH}-${BOOT_MODE}-${LOG_CONFIGURATION}.log"
LOG_KERNEL="log/kernel-${ARCH}-${BOOT_MODE}-${LOG_CONFIGURATION}.log"
LOG_NET_PCAP="log/kernel-net-${ARCH}-${BOOT_MODE}-${LOG_CONFIGURATION}.pcap"

USB_ARGUMENTS=()
NVME_ARGUMENTS=()
STORAGE_ARGUMENTS=()
FLOPPY_ARGUMENTS=()
AUDIO_ARGUMENTS=()
NETWORK_ARGUMENTS=()
UEFI_ARGUMENTS=()

function BuildUsbArguments() {
    USB_ARGUMENTS=(
        -device qemu-xhci,id=xhci
        -device usb-kbd,bus=xhci.0
        -device usb-mouse,bus=xhci.0
    )

    if [ "$USB3_ENABLED" -eq 1 ]; then
        USB_ARGUMENTS+=(
            -drive format=raw,file="$USB_3_PATH",if=none,id=usbdrive0
            -device usb-storage,drive=usbdrive0,bus=xhci.0,id=usbmsd0
        )
    fi
}

function BuildNvmeArguments() {
    NVME_ARGUMENTS=()

    if [ "$NVME_ENABLED" -eq 1 ]; then
        local NtfsImagePath="$FS_TEST_NTFS_IMG_PATH"

        if [ "$NTFS_LIVE_ENABLED" -eq 1 ] && [ -f "$NTFS_LIVE_IMG_PATH" ] && [ -r "$NTFS_LIVE_IMG_PATH" ] && [ -w "$NTFS_LIVE_IMG_PATH" ]; then
            NtfsImagePath="$NTFS_LIVE_IMG_PATH"
            echo "Using NTFS live image: $NtfsImagePath"
        elif [ "$NTFS_LIVE_ENABLED" -eq 1 ] && [ -f "$NTFS_LIVE_IMG_PATH" ]; then
            echo "NTFS live image exists but is not readable/writable, fallback to default: $NTFS_LIVE_IMG_PATH"
        fi

        if [ -z "${FS_TEST_EXT2_IMG_PATH:-}" ] || [ -z "${FS_TEST_FAT32_IMG_PATH:-}" ] || [ -z "${FS_TEST_NTFS_IMG_PATH:-}" ]; then
            echo "FS test image path not set"
            exit 1
        fi

        if [ ! -f "$FS_TEST_EXT2_IMG_PATH" ] || [ ! -f "$FS_TEST_FAT32_IMG_PATH" ] || [ ! -f "$NtfsImagePath" ]; then
            echo "Missing one or more FS test images:"
            echo "  $FS_TEST_EXT2_IMG_PATH"
            echo "  $FS_TEST_FAT32_IMG_PATH"
            echo "  $NtfsImagePath"
            echo "Build it with: ./scripts/linux/build/build.sh --arch $ARCH --fs ext2 --debug"
            exit 1
        fi

        NVME_ARGUMENTS=(
            -drive format=raw,file="$FS_TEST_EXT2_IMG_PATH",if=none,id=fsxt0
            -device nvme,drive=fsxt0,serial=exosfs0
            -drive format=raw,file="$FS_TEST_FAT32_IMG_PATH",if=none,id=fsxt1
            -device nvme,drive=fsxt1,serial=exosfs1
            -drive format=raw,file="$NtfsImagePath",if=none,id=fsxt2
            -device nvme,drive=fsxt2,serial=exosfs2
        )
    fi
}

function BuildStorageArguments() {
    STORAGE_ARGUMENTS=(
        -device ahci,id=ahci
        -drive format=raw,file="$IMG_PATH",if=none,id=drive0
        -device ide-hd,drive=drive0,bus=ahci.0
    )
}

function BuildFloppyArguments() {
    FLOPPY_ARGUMENTS=(
        -device isa-fdc,id=fdc0 \
        -drive if=none,file="$FLOPPY_35_PATH",format=raw,id=floppy0 \
        -device floppy,drive=floppy0
    )
}

function BuildAudioArguments() {
    AUDIO_ARGUMENTS=(
        -audiodev pa,id=audio0
        -device intel-hda,id=hda
        -device hda-duplex,bus=hda.0,audiodev=audio0
    )
}

function BuildNetworkArguments() {
    local NetworkCard=""
    local NetworkIndex=0

    NETWORK_ARGUMENTS=()

    for NetworkCard in "${NETWORK_CARDS[@]}"; do
        NETWORK_ARGUMENTS+=(
            -netdev "user,id=net${NetworkIndex}"
            -device "${NetworkCard},netdev=net${NetworkIndex}"
        )

        if [ "$NetworkIndex" -eq 0 ]; then
            NETWORK_ARGUMENTS+=(
                -object "filter-dump,id=dump${NetworkIndex},netdev=net${NetworkIndex},file=${LOG_NET_PCAP}"
            )
        fi

        NetworkIndex=$((NetworkIndex + 1))
    done
}

function FindFirmwareFile() {
    local env_value="$1"
    shift

    if [ -n "$env_value" ] && [ -f "$env_value" ]; then
        echo "$env_value"
        return 0
    fi

    for candidate in "$@"; do
        if [ -f "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done

    return 1
}

function BuildUefiArguments() {
    UEFI_ARGUMENTS=()

    if [ "$USE_UEFI" -eq 0 ]; then
        return 0
    fi

    OVMF_CODE_PATH="$(FindFirmwareFile "${OVMF_CODE:-}" "${OVMF_CODE_CANDIDATES[@]}")" || {
        echo "OVMF code firmware not found. Set OVMF_CODE to a valid file."
        exit 1
    }

    OVMF_VARS_PATH="$(FindFirmwareFile "${OVMF_VARS:-}" "${OVMF_VARS_CANDIDATES[@]}")" || {
        echo "OVMF variables firmware not found. Set OVMF_VARS to a valid file."
        exit 1
    }

    OVMF_VARS_COPY="$IMAGE_BUILD_DIR/work-uefi/ovmf-vars.fd"
    mkdir -p "$IMAGE_BUILD_DIR/work-uefi"
    cp -f "$OVMF_VARS_PATH" "$OVMF_VARS_COPY"

    UEFI_ARGUMENTS=(
        -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE_PATH"
        -drive if=pflash,format=raw,file="$OVMF_VARS_COPY"
    )
}

function RunStandardQemu() {
    BuildUsbArguments
    BuildNvmeArguments
    BuildStorageArguments
    BuildFloppyArguments
    BuildAudioArguments
    BuildNetworkArguments
    BuildUefiArguments

    if [ ! -x "$CYCLE_BIN" ]; then
        echo "Cycle tool not found or not executable: $CYCLE_BIN"
        exit 1
    fi

    "$QEMU_BIN" \
    -machine q35,acpi=on,kernel-irqchip=split \
    -nodefaults \
    -smp cpus=1,cores=1,threads=1 \
    "${USB_ARGUMENTS[@]}" \
    "${STORAGE_ARGUMENTS[@]}" \
    "${FLOPPY_ARGUMENTS[@]}" \
    "${NVME_ARGUMENTS[@]}" \
    "${AUDIO_ARGUMENTS[@]}" \
    "${NETWORK_ARGUMENTS[@]}" \
    "${UEFI_ARGUMENTS[@]}" \
    -monitor telnet:127.0.0.1:${MONITOR_PORT},server,nowait \
    -serial file:"${LOG_DEBUG_COM1}" \
    -serial stdio \
    -vga std \
    -no-reboot \
    2>&1 | "$CYCLE_BIN" -o "${LOG_KERNEL}" -s 4000000
}

function RunGdbQemu() {
    BuildUsbArguments
    BuildNvmeArguments
    BuildStorageArguments
    BuildFloppyArguments
    BuildAudioArguments
    BuildNetworkArguments
    BuildUefiArguments

    if [ ! -f "$DEBUG_ELF" ]; then
        echo "Debug symbol file not found: $DEBUG_ELF"
        exit 1
    fi

    if [ "$ARCH" = "x86-64" ] && [ ! -f "$DEBUG_GDB" ]; then
        echo "Debug configuration file not found: $DEBUG_GDB"
        exit 1
    fi

    "$QEMU_BIN" \
    -machine q35,acpi=on,kernel-irqchip=split \
    -nodefaults \
    -smp cpus=1,cores=1,threads=1 \
    "${USB_ARGUMENTS[@]}" \
    "${STORAGE_ARGUMENTS[@]}" \
    "${FLOPPY_ARGUMENTS[@]}" \
    "${NVME_ARGUMENTS[@]}" \
    "${AUDIO_ARGUMENTS[@]}" \
    "${NETWORK_ARGUMENTS[@]}" \
    "${UEFI_ARGUMENTS[@]}" \
    -serial file:"${LOG_DEBUG_COM1}" \
    -serial file:"${LOG_KERNEL}" \
    -vga std \
    -no-reboot \
    -s -S &

    sleep 2

    if [ "$ARCH" = "x86-64" ]; then
        cgdb "$DEBUG_ELF" -ex "set architecture x86-64" -ex "target remote localhost:1234" \
        -ex "break SwitchToNextTask" -ex "source $DEBUG_GDB"
    else
        cgdb "$DEBUG_ELF" -ex "set architecture x86-32" -ex "target remote localhost:1234" \
        -ex "break EnableInterrupts"
    fi
}

if [ "$USE_GDB" -eq 1 ]; then
    echo "Starting QEMU with GDB for $ARCH"
    RunGdbQemu
else
    echo "Starting QEMU for $ARCH"
    RunStandardQemu
fi
