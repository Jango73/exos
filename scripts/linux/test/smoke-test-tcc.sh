#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
SMOKE_TEST_SCRIPT_NAME="$0"
SMOKE_TEST_DEFAULT_COMMANDS_FILE="$ROOT_DIR/scripts/common/smoke-test-tcc-commands.txt"
SMOKE_TEST_REQUIRE_LOCAL_HTTP_SERVER=0

# shellcheck source=/dev/null
source "$ROOT_DIR/scripts/linux/utils/smoke-test-common.sh"

RUN_X86_32=1
RUN_X86_32_RTL8139=0
RUN_X86_64=1
RUN_X86_64_UEFI=0

TINYCC_VALIDATION_IMAGE_PATH=""
TINYCC_VALIDATION_FILE_SYSTEM_OFFSET=0
TINYCC_VALIDATION_ELF_CLASS=""
TINYCC_VALIDATION_MACHINE=""

function ExtractTinyCcOutputFile() {
    local PartitionImage
    local GuestPath="$1"
    local HostPath="$2"

    PartitionImage="$(mktemp)"

    if ! dd if="$TINYCC_VALIDATION_IMAGE_PATH" of="$PartitionImage" iflag=skip_bytes skip="$TINYCC_VALIDATION_FILE_SYSTEM_OFFSET" bs=1M status=none 2>/dev/null; then
        dd if="$TINYCC_VALIDATION_IMAGE_PATH" of="$PartitionImage" bs=1 skip="$TINYCC_VALIDATION_FILE_SYSTEM_OFFSET" status=none
    fi

    debugfs -R "dump $GuestPath $HostPath" "$PartitionImage" >/dev/null 2>&1
    rm -f "$PartitionImage"

    if [ ! -s "$HostPath" ]; then
        echo "TinyCC output file was not created in the guest image: $GuestPath"
        return 1
    fi
}

function ValidateTinyCcExecutable() {
    local ExecutablePath
    local ReadElfTool
    local ElfHeader
    local ProgramHeaders
    local GuestPath="$1"

    ExecutablePath="$(mktemp)"

    if ! ExtractTinyCcOutputFile "$GuestPath" "$ExecutablePath"; then
        rm -f "$ExecutablePath"
        return 1
    fi

    if command -v i686-elf-readelf >/dev/null 2>&1; then
        ReadElfTool="i686-elf-readelf"
    else
        ReadElfTool="readelf"
    fi

    ElfHeader="$("$ReadElfTool" -h "$ExecutablePath")"
    ProgramHeaders="$("$ReadElfTool" -l "$ExecutablePath")"
    rm -f "$ExecutablePath"

    echo "$ElfHeader" | grep -q "Class:[[:space:]]*$TINYCC_VALIDATION_ELF_CLASS" || {
        echo "TinyCC output executable is not $TINYCC_VALIDATION_ELF_CLASS."
        return 1
    }
    echo "$ElfHeader" | grep -q "Type:[[:space:]]*EXEC" || {
        echo "TinyCC output executable is not ET_EXEC."
        return 1
    }
    echo "$ElfHeader" | grep -q "Machine:[[:space:]]*$TINYCC_VALIDATION_MACHINE" || {
        echo "TinyCC output executable machine is not $TINYCC_VALIDATION_MACHINE."
        return 1
    }
    echo "$ProgramHeaders" | grep -q "LOAD" || {
        echo "TinyCC output executable has no PT_LOAD segment."
        return 1
    }
    if echo "$ProgramHeaders" | grep -q "INTERP"; then
        echo "TinyCC output executable unexpectedly has a PT_INTERP segment."
        return 1
    fi
}

function ValidateTinyCcArchive() {
    local ArchivePath

    ArchivePath="$(mktemp)"

    if ! ExtractTinyCcOutputFile "/exos/apps/tcc/libworkflow.a" "$ArchivePath"; then
        rm -f "$ArchivePath"
        return 1
    fi

    if ! head -c 8 "$ArchivePath" | cmp -s - <(printf "!<arch>\n"); then
        rm -f "$ArchivePath"
        echo "TinyCC output archive does not use the ar format."
        return 1
    fi

    if ! grep -a -q "WorkflowCalculateChecksum" "$ArchivePath" || ! grep -a -q "WorkflowScaleValue" "$ArchivePath"; then
        rm -f "$ArchivePath"
        echo "TinyCC output archive is missing workflow symbols."
        return 1
    fi

    rm -f "$ArchivePath"
}

function ValidateTinyCcMissingOutput() {
    local OutputPath
    local GuestPath="$1"

    OutputPath="$(mktemp)"
    if ExtractTinyCcOutputFile "$GuestPath" "$OutputPath" >/dev/null 2>&1; then
        rm -f "$OutputPath"
        echo "TinyCC failure-path output file was unexpectedly created: $GuestPath"
        return 1
    fi

    rm -f "$OutputPath"
}

function ValidateTinyCcOutputsForTarget() {
    local TargetName="$1"
    local ImagePath="$2"
    local FileSystemOffset="$3"
    local ElfClass="$4"
    local Machine="$5"

    echo "Validating TinyCC generated files for $TargetName"

    TINYCC_VALIDATION_IMAGE_PATH="$ImagePath"
    TINYCC_VALIDATION_FILE_SYSTEM_OFFSET="$FileSystemOffset"
    TINYCC_VALIDATION_ELF_CLASS="$ElfClass"
    TINYCC_VALIDATION_MACHINE="$Machine"

    ValidateTinyCcExecutable "/exos/apps/tcc/hello"
    ValidateTinyCcExecutable "/exos/apps/tcc/workflow"
    ValidateTinyCcExecutable "/exos/apps/tcc/source-warning"
    ValidateTinyCcArchive
    ValidateTinyCcMissingOutput "/exos/apps/tcc/source-too-large"
    ValidateTinyCcMissingOutput "/exos/apps/tcc/heap-limited"
}

SmokeTestMain "$@"

if [ "$RUN_X86_32" -eq 1 ]; then
    ValidateTinyCcOutputsForTarget "x86-32" "$ROOT_DIR/build/image/x86-32-mbr-debug-ext2/exos.img" 1048576 "ELF32" "Intel 80386"
fi

if [ "$RUN_X86_64" -eq 1 ]; then
    ValidateTinyCcOutputsForTarget "x86-64" "$ROOT_DIR/build/image/x86-64-mbr-debug-ext2/exos.img" 1048576 "ELF64" "Advanced Micro Devices X86-64"
fi
