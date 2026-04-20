#!/bin/sh

BUILD_IMAGE_NAME="${BUILD_IMAGE_NAME:-x86-32-mbr-debug-ext2}"

pkill -9 bochs
rm -f "build/image/${BUILD_IMAGE_NAME}/exos.img.lock"
