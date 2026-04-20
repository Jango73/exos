#!/bin/bash

set -euo pipefail

ScriptFolder="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RepositoryRoot="$(cd "${ScriptFolder}/../../.." && pwd)"

ReadmePath="${RepositoryRoot}/README.md"
CountCodeScript="${RepositoryRoot}/scripts/linux/utils/count-code.sh"
TemporaryReadme="$(mktemp)"
TemporaryCodeTable="$(mktemp)"
TemporaryKernelSize="$(mktemp)"

cleanup() {
    rm -f "${TemporaryReadme}" "${TemporaryCodeTable}" "${TemporaryKernelSize}"
}

trap cleanup EXIT

format_binary_size() {
    local BinaryPath="$1"

    stat -c "%s" "${BinaryPath}" | awk '
        {
            Megabytes = $1 / 1024 / 1024;
            printf "%.1f mb\n", Megabytes;
        }
    '
}

write_code_table() {
    "${CountCodeScript}" | awk '
        /^-+$/ {
            HasTableStarted = 1;
        }

        HasTableStarted {
            print;
        }
    ' > "${TemporaryCodeTable}"

    if [ ! -s "${TemporaryCodeTable}" ]; then
        echo "Unable to extract cloc table from ${CountCodeScript}" >&2
        exit 1
    fi
}

write_kernel_size_table() {
    local Kernel32Path="${RepositoryRoot}/build/core/x86-32-mbr-release/kernel/exos.bin"
    local Kernel64Path="${RepositoryRoot}/build/core/x86-64-mbr-release/kernel/exos.bin"

    if [ ! -f "${Kernel32Path}" ]; then
        echo "Missing kernel binary: ${Kernel32Path}" >&2
        exit 1
    fi

    if [ ! -f "${Kernel64Path}" ]; then
        echo "Missing kernel binary: ${Kernel64Path}" >&2
        exit 1
    fi

    {
        printf -- "- 32 bit : %s\n" "$(format_binary_size "${Kernel32Path}")"
        printf -- "- 64 bit : %s\n" "$(format_binary_size "${Kernel64Path}")"
    } > "${TemporaryKernelSize}"
}

update_readme() {
    awk \
        -v CodeTablePath="${TemporaryCodeTable}" \
        -v KernelSizePath="${TemporaryKernelSize}" '
        function print_file(FilePath) {
            while ((getline Line < FilePath) > 0) {
                print Line;
            }

            close(FilePath);
        }

        /^### Lines of code in this project/ {
            IsInCodeSection = 1;
            print;
            next;
        }

        /^### Kernel size$/ {
            IsInKernelSizeSection = 1;
            print;
            next;
        }

        IsInCodeSection && /^```$/ {
            print;
            print_file(CodeTablePath);

            while ((getline Line) > 0) {
                if (Line == "```") {
                    print Line;
                    IsInCodeSection = 0;
                    break;
                }
            }

            next;
        }

        IsInKernelSizeSection && /^```$/ {
            print;
            print_file(KernelSizePath);

            while ((getline Line) > 0) {
                if (Line == "```") {
                    print Line;
                    IsInKernelSizeSection = 0;
                    break;
                }
            }

            next;
        }

        {
            print;
        }
    ' "${ReadmePath}" > "${TemporaryReadme}"

    mv "${TemporaryReadme}" "${ReadmePath}"
}

write_code_table
write_kernel_size_table
update_readme
