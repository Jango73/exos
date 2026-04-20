#!/bin/bash
set -e

function Usage() {
    echo "Usage: $0 [--arch <x86-32|x86-64>] [--source <path>] [--http-root <path>] [--slot <latest|previous|safe>] [--rotate]"
}

ARCH="x86-64"
SOURCE_DIR=""
HTTP_ROOT="/var/www/html/exos"
SLOT="latest"
ROTATE=0

while [ $# -gt 0 ]; do
    case "$1" in
        --arch)
            shift
            [ $# -gt 0 ] || { echo "Missing value for --arch"; Usage; exit 1; }
            ARCH="$1"
            ;;
        --source)
            shift
            [ $# -gt 0 ] || { echo "Missing value for --source"; Usage; exit 1; }
            SOURCE_DIR="$1"
            ;;
        --http-root)
            shift
            [ $# -gt 0 ] || { echo "Missing value for --http-root"; Usage; exit 1; }
            HTTP_ROOT="$1"
            ;;
        --slot)
            shift
            [ $# -gt 0 ] || { echo "Missing value for --slot"; Usage; exit 1; }
            SLOT="$1"
            ;;
        --rotate)
            ROTATE=1
            ;;
        --help|-h)
            Usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1"
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
        echo "Unsupported architecture: $ARCH"
        exit 1
        ;;
esac

case "$SLOT" in
    latest|previous|safe)
        ;;
    *)
        echo "Unsupported slot: $SLOT"
        exit 1
        ;;
esac

if ! command -v rsync >/dev/null 2>&1; then
    echo "Missing command: rsync"
    exit 1
fi

if [ -z "$SOURCE_DIR" ]; then
    SOURCE_DIR="build/image/${ARCH}-uefi-debug-split-ext2/work-uefi/esp-root"
fi

if [ ! -d "$SOURCE_DIR" ]; then
    echo "Source folder not found: $SOURCE_DIR"
    echo "Build first: ./scripts/linux/build/build --arch $ARCH --fs ext2 --debug --split --uefi"
    exit 1
fi

DEST_DIR="$HTTP_ROOT/$SLOT/$ARCH"
PREVIOUS_DIR="$HTTP_ROOT/previous/$ARCH"
LATEST_DIR="$HTTP_ROOT/latest/$ARCH"

if [ ! -w "$HTTP_ROOT" ] && [ "$EUID" -ne 0 ]; then
    echo "No write access to $HTTP_ROOT. Run with sudo or change --http-root."
    exit 1
fi

mkdir -p "$DEST_DIR"

if [ "$ROTATE" -eq 1 ] && [ "$SLOT" = "latest" ] && [ -d "$LATEST_DIR" ]; then
    rm -rf "$PREVIOUS_DIR"
    mkdir -p "$(dirname "$PREVIOUS_DIR")"
    mv "$LATEST_DIR" "$PREVIOUS_DIR"
    mkdir -p "$LATEST_DIR"
fi

rsync -a --delete "$SOURCE_DIR/" "$DEST_DIR/"

echo "[deploy-local-netboot] Source : $SOURCE_DIR"
echo "[deploy-local-netboot] Target : $DEST_DIR"
echo "[deploy-local-netboot] Done."
