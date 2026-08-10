'use strict';

process.env.MONITOR_PORT = process.env.MONITOR_PORT || '4463';
process.env.GDB_PORT = process.env.GDB_PORT || '1234';
process.env.KEYBOARD_LAYOUT = process.env.KEYBOARD_LAYOUT || 'en-US';

const fs = require('node:fs');
const path = require('node:path');
const { spawn, execFileSync } = require('node:child_process');
const config = require('../utils/smoke-test/config');
const monitor = require('../utils/smoke-test/monitor');
const { SetImageKeyboardLayout } = require('../utils/smoke-test/image');
const { Sleep } = require('../utils/smoke-test/util');

const DRAG_CYCLES = Number(process.env.DRAG_CYCLES || 20);
const SHELL_READY_TIMEOUT_SECONDS = Number(process.env.SHELL_READY_TIMEOUT_SECONDS || 25);
const BUILD_CORE_NAME = process.env.BUILD_CORE_NAME || 'x86-32-mbr-debug';
const BUILD_IMAGE_NAME = process.env.BUILD_IMAGE_NAME || 'x86-32-mbr-debug-ext2';

const DEBUG_ELF = path.join(config.ROOT_DIR, 'build', 'core', BUILD_CORE_NAME, 'kernel', 'exos.elf');
const IMG_PATH = path.join(config.ROOT_DIR, 'build', 'image', BUILD_IMAGE_NAME, 'exos.img');
const USB_IMG_PATH = path.join(config.ROOT_DIR, 'build', 'image', BUILD_IMAGE_NAME, 'usb-3.img');
const FS_TEST_EXT2_IMG_PATH = path.join(config.ROOT_DIR, 'build', 'image', BUILD_IMAGE_NAME, 'fs-test-ext2.img');
const FS_TEST_FAT32_IMG_PATH = path.join(config.ROOT_DIR, 'build', 'image', BUILD_IMAGE_NAME, 'fs-test-fat32.img');
const FS_TEST_NTFS_IMG_PATH = path.join(config.ROOT_DIR, 'build', 'image', BUILD_IMAGE_NAME, 'fs-test-ntfs.img');
const LOG_KERNEL = path.join(config.ROOT_DIR, 'log', 'kernel-x86-32-mbr-debug.log');
const LOG_COM1 = path.join(config.ROOT_DIR, 'log', 'debug-com1-x86-32-mbr-debug.log');
const QEMU_STDOUT_LOG = path.join('/tmp', 'qemu-x86-32-windowing-gdb.out');
const GDB_COMMANDS_FILE = path.join('/tmp', 'exos-windowing-gdb.cmd');

if (!fs.existsSync(DEBUG_ELF) || !fs.existsSync(IMG_PATH)) {
    console.error('Missing build artifacts. Build first: bash scripts/linux/build/build.sh --arch x86-32 --fs ext2 --debug');
    process.exit(1);
}

let qemuPid = null;

function Cleanup() {
    if (qemuPid) {
        try {
            process.kill(-qemuPid, 'SIGTERM');
        } catch (error) {
            try {
                process.kill(qemuPid, 'SIGTERM');
            } catch (killError) {
                // Already gone.
            }
        }
    }
    try {
        execFileSync('killall', ['-q', 'qemu-system-i386', 'qemu-system-x86_64', 'gdb'], { stdio: 'ignore' });
    } catch (error) {
        // Nothing to kill.
    }
}

function WaitForGdbExit(gdbProcess) {
    return new Promise((resolve) => {
        gdbProcess.once('exit', resolve);
        gdbProcess.once('error', resolve);
    });
}

async function DriveWindowingStress() {
    if (!(await monitor.WaitForMonitor())) {
        return;
    }

    let ready = 0;
    for (let index = 0; index < SHELL_READY_TIMEOUT_SECONDS * 10; index++) {
        try {
            if (fs.readFileSync(LOG_KERNEL, 'utf8').includes('[InitializeKernel] Shell task created')) {
                ready = 1;
                break;
            }
        } catch (error) {
            // Log may not exist yet.
        }
        await Sleep(100);
    }

    if (ready === 1) {
        await monitor.SendKey('ret');
        await Sleep(300);
        await monitor.SendCommand('desktop show');
        await Sleep(800);
        await monitor.SendCommand(`desktop stressdrag ${DRAG_CYCLES}`);
    } else {
        console.log('BOOT_NOT_READY_WITHIN_TIMEOUT');
        if (qemuPid) {
            try {
                process.kill(qemuPid, 'SIGTERM');
            } catch (error) {
                // Already gone.
            }
        }
    }
}

async function Main() {
    Cleanup();
    fs.rmSync(LOG_KERNEL, { force: true });
    fs.rmSync(LOG_COM1, { force: true });

    SetImageKeyboardLayout(IMG_PATH, 1048576, process.env.KEYBOARD_LAYOUT);

    const qemuOutFd = fs.openSync(QEMU_STDOUT_LOG, 'w');
    const qemuProcess = spawn('qemu-system-i386', [
        '-machine', 'q35,acpi=on,kernel-irqchip=split',
        '-nodefaults',
        '-smp', 'cpus=1,cores=1,threads=1',
        '-device', 'qemu-xhci,id=xhci',
        '-device', 'usb-kbd,bus=xhci.0',
        '-device', 'usb-mouse,bus=xhci.0',
        '-drive', `format=raw,file=${USB_IMG_PATH},if=none,id=usbdrive0`,
        '-device', 'usb-storage,drive=usbdrive0,bus=xhci.0,id=usbmsd0',
        '-drive', `format=raw,file=${FS_TEST_EXT2_IMG_PATH},if=none,id=fsxt0`,
        '-device', 'nvme,drive=fsxt0,serial=exosfs0',
        '-drive', `format=raw,file=${FS_TEST_FAT32_IMG_PATH},if=none,id=fsxt1`,
        '-device', 'nvme,drive=fsxt1,serial=exosfs1',
        '-drive', `format=raw,file=${FS_TEST_NTFS_IMG_PATH},if=none,id=fsxt2`,
        '-device', 'nvme,drive=fsxt2,serial=exosfs2',
        '-device', 'ahci,id=ahci',
        '-drive', `format=raw,file=${IMG_PATH},if=none,id=drive0`,
        '-device', 'ide-hd,drive=drive0,bus=ahci.0',
        '-netdev', 'user,id=net0',
        '-device', 'e1000,netdev=net0',
        '-monitor', `telnet:127.0.0.1:${config.MONITOR_PORT},server,nowait`,
        '-serial', `file:${LOG_COM1}`,
        '-serial', `file:${LOG_KERNEL}`,
        '-vga', 'std',
        '-no-reboot',
        '-s', '-S',
    ], {
        detached: true,
        stdio: ['ignore', qemuOutFd, qemuOutFd],
    });
    qemuPid = qemuProcess.pid;

    const gdbCommands = `set architecture i386
set pagination off
set confirm off
target remote :${config.GDB_PORT}

break DoubleFaultHandler
commands
silent
printf "\\n*** HIT DoubleFaultHandler ***\\n"
bt
info reg
continue
end

break GeneralProtectionHandler
commands
silent
printf "\\n*** HIT GeneralProtectionHandler ***\\n"
bt
info reg
continue
end

break PageFaultHandler
commands
silent
printf "\\n*** HIT PageFaultHandler ***\\n"
bt
info reg
continue
end

break StackFaultHandler
commands
silent
printf "\\n*** HIT StackFaultHandler ***\\n"
bt
info reg
continue
end

break SegmentFaultHandler
commands
silent
printf "\\n*** HIT SegmentFaultHandler ***\\n"
bt
info reg
continue
end

break DesktopInternalRunStressDrag
commands
silent
printf "\\n*** HIT DesktopInternalRunStressDrag ***\\n"
bt 8
continue
end

continue
`;
    fs.writeFileSync(GDB_COMMANDS_FILE, gdbCommands);

    const driveTask = DriveWindowingStress();
    const gdbProcess = spawn('gdb', ['-q', DEBUG_ELF, '-x', GDB_COMMANDS_FILE], { stdio: 'inherit' });
    await WaitForGdbExit(gdbProcess);
    await driveTask;
}

Main()
    .then(() => {
        process.exitCode = 0;
    })
    .catch((error) => {
        console.error(error.message);
        process.exitCode = 1;
    })
    .finally(() => {
        Cleanup();
    });
