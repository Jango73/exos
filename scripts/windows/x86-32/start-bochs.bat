@echo off
setlocal enabledelayedexpansion

set "BOCHS=c:\program files\bochs-3.0\bochs.exe"
if "%BUILD_IMAGE_NAME%"=="" set "BUILD_IMAGE_NAME=x86-32-mbr-debug-ext2"
set "IMG_1_PATH=build/image/%BUILD_IMAGE_NAME%/exos.img"

if not exist "%IMG_1_PATH%" (
    echo Image not found: %IMG_1_PATH%
    exit /b 1
)

echo Starting Bochs with image : %IMG_1_PATH%

"%BOCHS%" -q -f scripts/common/bochs/bochs.windows.txt -rc scripts/common/bochs/bochs_debug_commands.txt -unlock
