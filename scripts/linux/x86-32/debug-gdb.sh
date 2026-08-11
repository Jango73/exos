#!/bin/bash

BUILD_CORE_NAME="${BUILD_CORE_NAME:-x86-32-mbr-debug}"
gdb-multiarch "./build/core/${BUILD_CORE_NAME}/kernel/exos.elf" -ex "set architecture x86-32" -ex "show architecture" -ex "target remote localhost:1234" -ex "break RAMStorage.c:467"
