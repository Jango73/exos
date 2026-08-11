#!/bin/bash
set -euo pipefail

INSTALL_DIR="/opt/i686-elf-toolchain"
TOOLCHAIN_VERSION="7.1.0"
TOOLCHAIN_URL="https://github.com/lordmilko/i686-elf-tools/releases/download/${TOOLCHAIN_VERSION}/i686-elf-tools-linux.zip"
FORCE_TOOLCHAIN_REINSTALL=0
APT_PACKAGES=(
    automake
    binutils
    clang
    clang-format
    cloc
    coreutils
    dosfstools
    findutils
    gawk
    grep
    gzip
    lld
    make
    mtools
    nasm
    parted
    qemu-system-x86
    sed
    unzip
    wget
    xxd
)

ShowError()
{
    local Message="$1"

    echo "ERROR: $Message" >&2
}

# ////////////////////////////////////////////////////////////////////////////
ShowWarning()
{
    local Message="$1"

    echo "WARNING: $Message"
}

# ////////////////////////////////////////////////////////////////////////////
Usage()
{
    echo "Usage: $0 [--force-toolchain-reinstall]"
}

# ////////////////////////////////////////////////////////////////////////////
RunOrFail()
{
    local FailureMessage="$1"

    shift

    if ! "$@"; then
        ShowError "$FailureMessage"
        exit 1
    fi
}

# ////////////////////////////////////////////////////////////////////////////
CommandExists()
{
    local CommandName="$1"

    command -v "$CommandName" >/dev/null 2>&1
}

# ////////////////////////////////////////////////////////////////////////////
CheckCrossCompilerRunnable()
{
    local CrossCompilerPath="$1"
    local ErrorLog=""

    ErrorLog="$(mktemp)"

    if "$CrossCompilerPath" --version >/dev/null 2>"$ErrorLog"; then
        rm -f "$ErrorLog"
        return 0
    fi

    ShowError "Cross-compiler validation failed: $CrossCompilerPath"
    sed 's/^/ERROR: /' "$ErrorLog" >&2 || true
    rm -f "$ErrorLog"

    return 1
}

# ////////////////////////////////////////////////////////////////////////////
PackageNameExists()
{
    local PackageName="$1"
    local FirstMatch=""

    FirstMatch="$(apt-cache pkgnames "$PackageName" 2>/dev/null | sed -n '1p')"
    [ "$FirstMatch" = "$PackageName" ]
}

# ////////////////////////////////////////////////////////////////////////////
InstallOptionalDebuggerPackages()
{
    local DebuggerPackage=""

    if PackageNameExists "cgdb-multiarch"; then
        DebuggerPackage="cgdb-multiarch"
    elif PackageNameExists "cgdb"; then
        DebuggerPackage="cgdb"
    fi

    if [ -n "$DebuggerPackage" ]; then
        echo "Installing optional debugger package: $DebuggerPackage"
        RunOrFail \
            "APT failed while installing optional debugger package '$DebuggerPackage'." \
            sudo apt-get install -y "$DebuggerPackage"
    else
        ShowWarning "Neither 'cgdb-multiarch' nor 'cgdb' is available in APT. Continuing without CGDB."
    fi

    if PackageNameExists "gdb-multiarch"; then
        echo "Installing optional debugger package: gdb-multiarch"
        RunOrFail \
            "APT failed while installing optional debugger package 'gdb-multiarch'." \
            sudo apt-get install -y gdb-multiarch
    else
        ShowWarning "Package 'gdb-multiarch' is unavailable. Multi-architecture debugging may be limited."
    fi
}

# ////////////////////////////////////////////////////////////////////////////
InstallAptPackages()
{
    echo "Updating package lists..."
    RunOrFail \
        "APT update failed. Check your network connection and repository configuration." \
        sudo apt-get update

    echo "Installing EXOS development dependencies..."
    echo "APT packages: ${APT_PACKAGES[*]}"
    if ! sudo apt-get install -y "${APT_PACKAGES[@]}"; then
        ShowError "APT install failed. Review the package manager error above."
        exit 1
    fi

    if CommandExists "npm"; then
        echo "npm command available: $(command -v npm)"
    else
        echo "Installing npm..."
        if ! sudo apt-get install -y npm; then
            ShowError "APT failed while installing 'npm'."
            ShowError "If you already use NodeSource or another Node.js source, keep the existing npm command instead of installing the Ubuntu npm package."
            exit 1
        fi
    fi

    InstallOptionalDebuggerPackages

    echo "All required APT dependencies for EXOS are installed."
}

# ////////////////////////////////////////////////////////////////////////////
InstallNodePackages()
{
    echo "Installing Node.js tooling..."
    RunOrFail \
        "npm failed to install the required global packages ('jscpd', 'rimraf'). Check your Node.js/npm installation and permissions." \
        sudo npm install -g jscpd rimraf
}

# ////////////////////////////////////////////////////////////////////////////
ExtractToolchainArchive()
{
    local ArchivePath="$1"

    if [[ "$ArchivePath" =~ \.tar\.gz$ ]]; then
        echo "Extracting tar.gz archive..."
        RunOrFail \
            "Failed to extract $ArchivePath. The archive may be corrupted." \
            sudo tar -xzf "$ArchivePath" -C "$INSTALL_DIR" --strip-components=1
    elif [[ "$ArchivePath" =~ \.tar\.xz$ ]]; then
        echo "Extracting tar.xz archive..."
        RunOrFail \
            "Failed to extract $ArchivePath. The archive may be corrupted." \
            sudo tar -xJf "$ArchivePath" -C "$INSTALL_DIR" --strip-components=1
    elif [[ "$ArchivePath" =~ \.zip$ ]]; then
        echo "Extracting zip archive..."
        RunOrFail \
            "Failed to extract $ArchivePath. The archive may be corrupted." \
            sudo unzip -o "$ArchivePath" -d "$INSTALL_DIR"
    else
        ShowError "Unknown archive extension for $ArchivePath"
        exit 1
    fi
}

# ////////////////////////////////////////////////////////////////////////////
GetInstalledToolchainPath()
{
    local ToolchainPath=""

    if [ -d "$INSTALL_DIR/i686-elf-tools-linux/bin" ]; then
        ToolchainPath="$INSTALL_DIR/i686-elf-tools-linux/bin"
    elif [ -d "$INSTALL_DIR/bin" ]; then
        ToolchainPath="$INSTALL_DIR/bin"
    else
        ShowError "Cannot find the toolchain bin folder in $INSTALL_DIR after extraction."
        exit 1
    fi

    echo "$ToolchainPath"
}

# ////////////////////////////////////////////////////////////////////////////
IsToolchainInstallValid()
{
    local ToolchainPath=""

    if [ ! -d "$INSTALL_DIR" ]; then
        return 1
    fi

    if [ -d "$INSTALL_DIR/i686-elf-tools-linux/bin" ]; then
        ToolchainPath="$INSTALL_DIR/i686-elf-tools-linux/bin"
    elif [ -d "$INSTALL_DIR/bin" ]; then
        ToolchainPath="$INSTALL_DIR/bin"
    else
        return 1
    fi

    if [ ! -x "$ToolchainPath/i686-elf-gcc" ]; then
        return 1
    fi

    return 0
}

# ////////////////////////////////////////////////////////////////////////////
ConfigureToolchainPath()
{
    local ToolchainPath=""

    ToolchainPath="$(GetInstalledToolchainPath)"

    if ! grep -Fq "$ToolchainPath" "$HOME/.bashrc"; then
        echo "export PATH=\"\$PATH:$ToolchainPath\"" >> "$HOME/.bashrc"
        echo "Added $ToolchainPath to PATH in ~/.bashrc."
    else
        echo "$ToolchainPath already in PATH."
    fi

    echo "i686-elf toolchain installed to $ToolchainPath."
    echo "Restart your shell, or run: export PATH=\"\$PATH:$ToolchainPath\""
}

# ////////////////////////////////////////////////////////////////////////////
ValidateInstalledToolchain()
{
    local ToolchainPath=""
    local CrossCompilerPath=""

    ToolchainPath="$(GetInstalledToolchainPath)"
    CrossCompilerPath="$ToolchainPath/i686-elf-gcc"

    if ! CheckCrossCompilerRunnable "$CrossCompilerPath"; then
        ShowError "The installed i686-elf toolchain is present but cannot run on this Linux system."
        ShowError "This usually means the downloaded prebuilt toolchain requires a newer glibc than the host distribution provides."
        ShowError "If you want to replace it with pinned release ${TOOLCHAIN_VERSION}, re-run this script with --force-toolchain-reinstall."
        ShowError "Otherwise install a compatible i686-elf toolchain, or build it locally, then retry."
        exit 1
    fi
}

# ////////////////////////////////////////////////////////////////////////////
HandleExistingToolchainInstall()
{
    if [ ! -d "$INSTALL_DIR" ]; then
        return 1
    fi

    if ! IsToolchainInstallValid; then
        ShowError "Existing i686-elf toolchain install is incomplete or invalid: $INSTALL_DIR"
        if [ "$FORCE_TOOLCHAIN_REINSTALL" -ne 1 ]; then
            ShowError "Refusing to remove it automatically."
            ShowError "Re-run with --force-toolchain-reinstall to replace it with pinned release ${TOOLCHAIN_VERSION}."
            exit 1
        fi

        echo "Removing existing invalid toolchain install..."
        RunOrFail \
            "Failed to remove invalid contents from $INSTALL_DIR. Check sudo permissions and folder access." \
            sudo rm -rf "$INSTALL_DIR"
        return 1
    fi

    echo "i686-elf-toolchain already present in storage: $INSTALL_DIR"
    if [ "$FORCE_TOOLCHAIN_REINSTALL" -eq 1 ]; then
        echo "Forcing toolchain reinstall to pinned release ${TOOLCHAIN_VERSION}..."
        RunOrFail \
            "Failed to remove existing contents from $INSTALL_DIR. Check sudo permissions and folder access." \
            sudo rm -rf "$INSTALL_DIR"
        return 1
    fi

    ValidateInstalledToolchain
    ConfigureToolchainPath
    return 0
}

# ////////////////////////////////////////////////////////////////////////////
InstallToolchain()
{
    local ArchivePath=""
    local ArchiveFileName=""

    if HandleExistingToolchainInstall; then
        return 0
    fi

    ArchiveFileName="i686-elf-tools-linux-${TOOLCHAIN_VERSION}.zip"
    ArchivePath="/tmp/${ArchiveFileName}"

    echo "Downloading i686-elf toolchain (lordmilko)..."
    echo "Pinned release: GCC ${TOOLCHAIN_VERSION}"
    echo "Download URL: $TOOLCHAIN_URL"
    echo "Install dir: $INSTALL_DIR"
    echo "Archive: $ArchivePath"

    if [ -f "$ArchivePath" ]; then
        echo "Archive already present: $ArchivePath"
    else
        echo "Downloading toolchain archive..."
        RunOrFail \
            "Failed to download the i686-elf toolchain archive from GitHub." \
            wget -O "$ArchivePath" "$TOOLCHAIN_URL"
    fi

    RunOrFail \
        "Failed to create $INSTALL_DIR. Check sudo permissions and folder access." \
        sudo mkdir -p "$INSTALL_DIR"

    ExtractToolchainArchive "$ArchivePath"
    ValidateInstalledToolchain
    ConfigureToolchainPath
}

# ////////////////////////////////////////////////////////////////////////////
main()
{
    while [ $# -gt 0 ]; do
        case "$1" in
            --force-toolchain-reinstall)
                FORCE_TOOLCHAIN_REINSTALL=1
                ;;
            --help|-h)
                Usage
                exit 0
                ;;
            *)
                ShowError "Unknown option: $1"
                Usage
                exit 1
                ;;
        esac
        shift
    done

    InstallAptPackages
    InstallNodePackages
    InstallToolchain
}

main "$@"
