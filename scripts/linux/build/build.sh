#!/bin/bash
set -e

function AddPathPrefix() {
    local PathPrefix="$1"

    case ":$PATH:" in
        *":$PathPrefix:"*)
            ;;
        *)
            export PATH="$PathPrefix:$PATH"
            ;;
    esac
}

function ConfigureCrossToolchainPath() {
    local ToolchainPath=""

    if [ -d "/opt/i686-elf-toolchain/bin" ]; then
        ToolchainPath="/opt/i686-elf-toolchain/bin"
    elif [ -d "/opt/i686-elf-toolchain/i686-elf-tools-linux/bin" ]; then
        ToolchainPath="/opt/i686-elf-toolchain/i686-elf-tools-linux/bin"
    fi

    if [ -n "$ToolchainPath" ]; then
        AddPathPrefix "$ToolchainPath"
    fi
}

function ValidateCrossCompiler() {
    local ErrorLog=""

    if ! command -v i686-elf-gcc >/dev/null 2>&1; then
        echo "Missing cross-compiler: i686-elf-gcc"
        echo "Expected it in PATH or under /opt/i686-elf-toolchain/bin"
        echo "Run ./scripts/linux/setup/setup-deps.sh, then retry."
        exit 1
    fi

    ErrorLog="$(mktemp)"

    if i686-elf-gcc --version >/dev/null 2>"$ErrorLog"; then
        rm -f "$ErrorLog"
        return 0
    fi

    echo "Cross-compiler found but unusable: $(command -v i686-elf-gcc)"
    sed 's/^/ERROR: /' "$ErrorLog" >&2 || true
    rm -f "$ErrorLog"
    echo "The installed i686-elf toolchain is incompatible with this host system."
    echo "This usually means the downloaded prebuilt toolchain requires a newer glibc than the host distribution provides."
    echo "Install a compatible i686-elf toolchain, or build it locally, then retry."
    exit 1
}

function ValidateArchitectureToolchain() {
    if [ "$ARCH" = "x86-32" ]; then
        ConfigureCrossToolchainPath
        ValidateCrossCompiler
    fi
}

function Usage() {
    echo "Usage: $0 --arch <x86-32|x86-64> --fs <ext2|fat32> [--boot-stage-markers] [--clean] [--debug|--release] [--force-pic] [--kernel-log-tag-filter <filter>] [--log-udp-dest <ip:port>] [--log-udp-source <ip:port>] [--no-images] [--profiling] [--scheduling-debug] [--split] [--system-data-view] [--uefi] [--use-log-udp] [--use-syscall]"
}

function ParseIpPort() {
    local Value="$1"
    local Prefix="$2"
    local Address Port
    local Ip0 Ip1 Ip2 Ip3 Extra

    Address="${Value%:*}"
    Port="${Value##*:}"
    if [ "$Address" = "$Value" ] || [ -z "$Address" ] || [ -z "$Port" ]; then
        echo "Invalid value for $Prefix: $Value (expected ip:port)"
        exit 1
    fi

    IFS='.' read -r Ip0 Ip1 Ip2 Ip3 Extra <<< "$Address"
    if [ -n "$Extra" ] || [ -z "$Ip0" ] || [ -z "$Ip1" ] || [ -z "$Ip2" ] || [ -z "$Ip3" ]; then
        echo "Invalid IP for $Prefix: $Address"
        exit 1
    fi

    for Part in "$Ip0" "$Ip1" "$Ip2" "$Ip3"; do
        if ! [[ "$Part" =~ ^[0-9]+$ ]] || [ "$Part" -lt 0 ] || [ "$Part" -gt 255 ]; then
            echo "Invalid IP for $Prefix: $Address"
            exit 1
        fi
    done

    if ! [[ "$Port" =~ ^[0-9]+$ ]] || [ "$Port" -lt 1 ] || [ "$Port" -gt 65535 ]; then
        echo "Invalid port for $Prefix: $Port"
        exit 1
    fi

    eval "${Prefix}_IP_0=$Ip0"
    eval "${Prefix}_IP_1=$Ip1"
    eval "${Prefix}_IP_2=$Ip2"
    eval "${Prefix}_IP_3=$Ip3"
    eval "${Prefix}_PORT=$Port"
}

ARCH="x86-32"
BOOT_STAGE_MARKERS=0
BUILD_UEFI=0
BUILD_CONFIGURATION="release"
BUILD_CORE_NAME=""
BUILD_IMAGE_NAME=""
BUILD_IMAGES=1
CLEAN=0
DEBUG_OUTPUT=0
DEBUG_SPLIT=0
FILE_SYSTEM="ext2"
FORCE_PIC=0
UEFI_LOG_UDP_DEST_IP_0=192
UEFI_LOG_UDP_DEST_IP_1=168
UEFI_LOG_UDP_DEST_IP_2=50
UEFI_LOG_UDP_DEST_IP_3=1
UEFI_LOG_UDP_DEST_PORT=18194
UEFI_LOG_UDP_SOURCE_IP_0=192
UEFI_LOG_UDP_SOURCE_IP_1=168
UEFI_LOG_UDP_SOURCE_IP_2=50
UEFI_LOG_UDP_SOURCE_IP_3=2
UEFI_LOG_UDP_SOURCE_PORT=18195
UEFI_LOG_USE_UDP=0
PROFILING=0
SCHEDULING_DEBUG=0
SYSTEM_DATA_VIEW=0
USE_SYSCALL=0
KERNEL_LOG_DEFAULT_TAG_FILTER_SET="${KERNEL_LOG_DEFAULT_TAG_FILTER_SET:-0}"
KERNEL_LOG_DEFAULT_TAG_FILTER="${KERNEL_LOG_DEFAULT_TAG_FILTER:-}"
BUILD_LOCK_DIR=""

function ComputeBuildNames() {
    local BootMode
    local Suffix=""

    BootMode="mbr"
    if [ "$BUILD_UEFI" -eq 1 ]; then
        BootMode="uefi"
    fi

    if [ "$DEBUG_SPLIT" -eq 1 ]; then
        Suffix="-split"
    fi

    BUILD_CORE_NAME="${ARCH}-${BootMode}-${BUILD_CONFIGURATION}${Suffix}"
    BUILD_IMAGE_NAME="${BUILD_CORE_NAME}-${FILE_SYSTEM}"
}

function ResolveBootMode() {
    if [ "$BUILD_UEFI" -eq 1 ]; then
        echo "uefi"
    else
        echo "mbr"
    fi
}

function AcquireBuildLock() {
    BUILD_LOCK_DIR="build/core/$BUILD_CORE_NAME/.build-lock"

    mkdir -p "build/core/$BUILD_CORE_NAME"
    if ! mkdir "$BUILD_LOCK_DIR" 2>/dev/null; then
        if [ -f "$BUILD_LOCK_DIR/pid" ]; then
            ExistingPid="$(cat "$BUILD_LOCK_DIR/pid" 2>/dev/null || true)"
            if [ -n "$ExistingPid" ] && kill -0 "$ExistingPid" 2>/dev/null; then
                echo "A build is already running for $BUILD_CORE_NAME (pid: $ExistingPid)."
                exit 1
            fi
        fi

        rm -rf "$BUILD_LOCK_DIR"
        if ! mkdir "$BUILD_LOCK_DIR" 2>/dev/null; then
            echo "Could not acquire build lock for $BUILD_CORE_NAME."
            exit 1
        fi
    fi

    echo "$$" > "$BUILD_LOCK_DIR/pid"
}

function ReleaseBuildLock() {
    if [ -n "$BUILD_LOCK_DIR" ] && [ -d "$BUILD_LOCK_DIR" ]; then
        rm -rf "$BUILD_LOCK_DIR"
    fi
}

function FlushPathToDisk() {
    local TargetPath="$1"

    if [ ! -e "$TargetPath" ]; then
        return 0
    fi

    if sync -f "$TargetPath" 2>/dev/null; then
        return 0
    fi

    if sync "$TargetPath" 2>/dev/null; then
        return 0
    fi

    sync
}

function FlushImageArtifacts() {
    local ImageBuildDir="build/image/$BUILD_IMAGE_NAME"
    local ImagePath=""

    while IFS= read -r -d '' ImagePath; do
        FlushPathToDisk "$ImagePath"
    done < <(find "$ImageBuildDir" -type f -name "*.img" -print0 2>/dev/null || true)

    FlushPathToDisk "$ImageBuildDir"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --arch)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --arch"
                Usage
                exit 1
            fi
            ARCH="$1"
            ;;
        --boot-stage-markers)
            BOOT_STAGE_MARKERS=1
            ;;
        --clean)
            CLEAN=1
            ;;
        --debug)
            DEBUG_OUTPUT=1
            BUILD_CONFIGURATION="debug"
            ;;
        --force-pic)
            FORCE_PIC=1
            ;;
        --fs)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --fs"
                Usage
                exit 1
            fi
            FILE_SYSTEM="$1"
            ;;
        --help|-h)
            Usage
            exit 0
            ;;
        --log-udp-dest)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --log-udp-dest"
                Usage
                exit 1
            fi
            ParseIpPort "$1" "UEFI_LOG_UDP_DEST"
            ;;
        --kernel-log-tag-filter)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --kernel-log-tag-filter"
                Usage
                exit 1
            fi
            KERNEL_LOG_DEFAULT_TAG_FILTER_SET=1
            KERNEL_LOG_DEFAULT_TAG_FILTER="$1"
            ;;
        --log-udp-source)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --log-udp-source"
                Usage
                exit 1
            fi
            ParseIpPort "$1" "UEFI_LOG_UDP_SOURCE"
            ;;
        --no-images)
            BUILD_IMAGES=0
            ;;
        --profiling)
            PROFILING=1
            ;;
        --release)
            BUILD_CONFIGURATION="release"
            ;;
        --scheduling-debug)
            SCHEDULING_DEBUG=1
            ;;
        --split)
            DEBUG_SPLIT=1
            ;;
        --system-data-view)
            SYSTEM_DATA_VIEW=1
            ;;
        --uefi)
            BUILD_UEFI=1
            ;;
        --use-log-udp)
            UEFI_LOG_USE_UDP=1
            ;;
        --use-syscall)
            USE_SYSCALL=1
            ;;
        *)
            echo "Unknown option: $1"
            Usage
            exit 1
            ;;
    esac
    shift
done

case "$ARCH" in
    x86-32|x86-64)
        ;;
    *)
        echo "Unknown architecture: $ARCH"
        Usage
        exit 1
        ;;
esac

case "$FILE_SYSTEM" in
    ext2|fat32)
        ;;
    *)
        echo "Unknown file system: $FILE_SYSTEM"
        Usage
        exit 1
        ;;
esac

ValidateArchitectureToolchain

AcquireBuildLock
trap ReleaseBuildLock EXIT

SCHEDULING_DEBUG_OUTPUT=0
TRACE_STACK_USAGE=0

if [ "$SCHEDULING_DEBUG" -eq 1 ]; then
    PROFILING=1
    DEBUG_OUTPUT=1
    BUILD_CONFIGURATION="debug"
    SCHEDULING_DEBUG_OUTPUT=1
    TRACE_STACK_USAGE=1
fi

ComputeBuildNames
BOOT_MODE="$(ResolveBootMode)"

export BOOT_STAGE_MARKERS
export BUILD_CONFIGURATION
export BUILD_CORE_NAME
export BUILD_IMAGE_NAME
export BOOT_MODE
export BUILD_IMAGES
export DEBUG_OUTPUT
export DEBUG_SPLIT
export FORCE_PIC
export PROFILING
export SCHEDULING_DEBUG_OUTPUT
export SYSTEM_DATA_VIEW
export TRACE_STACK_USAGE
export UEFI_LOG_UDP_DEST_IP_0
export UEFI_LOG_UDP_DEST_IP_1
export UEFI_LOG_UDP_DEST_IP_2
export UEFI_LOG_UDP_DEST_IP_3
export UEFI_LOG_UDP_DEST_PORT
export UEFI_LOG_UDP_SOURCE_IP_0
export UEFI_LOG_UDP_SOURCE_IP_1
export UEFI_LOG_UDP_SOURCE_IP_2
export UEFI_LOG_UDP_SOURCE_IP_3
export UEFI_LOG_UDP_SOURCE_PORT
export UEFI_LOG_USE_UDP
export USE_SYSCALL
export KERNEL_LOG_DEFAULT_TAG_FILTER_SET
export KERNEL_LOG_DEFAULT_TAG_FILTER
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM="$FILE_SYSTEM"

if [ "$CLEAN" -eq 1 ]; then
    make ARCH="$ARCH" clean
fi

make ARCH="$ARCH" BUILD_IMAGES="$BUILD_IMAGES" BOOT_MODE="$BOOT_MODE" -j"$(nproc)"

if [ "$BUILD_IMAGES" -eq 1 ]; then
    FlushImageArtifacts
fi
