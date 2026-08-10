'use strict';

process.env.MONITOR_PORT = process.env.MONITOR_PORT || '4465';
process.env.GDB_PORT = process.env.GDB_PORT || '1234';
process.env.KEYBOARD_LAYOUT = process.env.KEYBOARD_LAYOUT || 'en-US';

const fs = require('node:fs');
const path = require('node:path');
const { spawn, execFileSync } = require('node:child_process');
const config = require('../utils/smoke-test/config');
const monitor = require('../utils/smoke-test/monitor');
const { SetImageKeyboardLayout } = require('../utils/smoke-test/image');
const { Sleep } = require('../utils/smoke-test/util');

const SHELL_READY_TIMEOUT_SECONDS = Number(process.env.SHELL_READY_TIMEOUT_SECONDS || 30);
const DESKTOP_SETTLE_SECONDS = Number(process.env.DESKTOP_SETTLE_SECONDS || 5);
const POST_F12_WAIT_SECONDS = Number(process.env.POST_F12_WAIT_SECONDS || 6);
const BUILD_CORE_NAME = process.env.BUILD_CORE_NAME || 'x86-64-uefi-debug';
const BUILD_IMAGE_NAME = process.env.BUILD_IMAGE_NAME || 'x86-64-uefi-debug-ext2';

const DEBUG_ELF = path.join(config.ROOT_DIR, 'build', 'core', BUILD_CORE_NAME, 'kernel', 'exos.elf');
const IMG_PATH = path.join(config.ROOT_DIR, 'build', 'image', BUILD_IMAGE_NAME, 'exos-uefi.img');
const USB_IMG_PATH = path.join(config.ROOT_DIR, 'build', 'image', BUILD_IMAGE_NAME, 'usb-3.img');
const FS_TEST_EXT2_IMG_PATH = path.join(config.ROOT_DIR, 'build', 'image', BUILD_IMAGE_NAME, 'fs-test-ext2.img');
const FS_TEST_FAT32_IMG_PATH = path.join(config.ROOT_DIR, 'build', 'image', BUILD_IMAGE_NAME, 'fs-test-fat32.img');
const FS_TEST_NTFS_IMG_PATH = path.join(config.ROOT_DIR, 'build', 'image', BUILD_IMAGE_NAME, 'fs-test-ntfs.img');
const LOG_KERNEL = path.join(config.ROOT_DIR, 'log', 'kernel-x86-64-uefi-debug.log');
const LOG_COM1 = path.join(config.ROOT_DIR, 'log', 'debug-com1-x86-64-uefi-debug.log');
const QEMU_STDOUT_LOG = path.join('/tmp', 'qemu-x86-64-uefi-f12-gdb.out');
const GDB_DUMP_LOG = path.join('/tmp', 'gdb-x86-64-uefi-f12-freeze.log');

const OVMF_CODE_CANDIDATES = [
    '/usr/share/OVMF/OVMF_CODE.fd',
    '/usr/share/edk2/ovmf/OVMF_CODE.fd',
    '/usr/share/qemu/OVMF_CODE.fd',
];
const OVMF_VARS_CANDIDATES = [
    '/usr/share/OVMF/OVMF_VARS.fd',
    '/usr/share/edk2/ovmf/OVMF_VARS.fd',
    '/usr/share/qemu/OVMF_VARS.fd',
];

function FindFirmwareFile(envValue, candidates) {
    if (envValue && fs.existsSync(envValue)) {
        return envValue;
    }
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }
    return '';
}

if (!fs.existsSync(DEBUG_ELF) || !fs.existsSync(IMG_PATH)) {
    console.error('Missing build artifacts. Build first: ./scripts/linux/build/build --arch x86-64 --fs ext2 --debug --uefi');
    process.exit(1);
}
for (const imagePath of [USB_IMG_PATH, FS_TEST_EXT2_IMG_PATH, FS_TEST_FAT32_IMG_PATH, FS_TEST_NTFS_IMG_PATH]) {
    if (!fs.existsSync(imagePath)) {
        console.error(`Missing one or more support images under build/image/${BUILD_IMAGE_NAME}`);
        process.exit(1);
    }
}

const OVMF_CODE_PATH = FindFirmwareFile(process.env.OVMF_CODE || '', OVMF_CODE_CANDIDATES);
if (!OVMF_CODE_PATH) {
    console.error('OVMF code firmware not found. Set OVMF_CODE to a valid file.');
    process.exit(1);
}
const OVMF_VARS_PATH = FindFirmwareFile(process.env.OVMF_VARS || '', OVMF_VARS_CANDIDATES);
if (!OVMF_VARS_PATH) {
    console.error('OVMF vars firmware not found. Set OVMF_VARS to a valid file.');
    process.exit(1);
}

const OVMF_VARS_COPY = path.join(config.ROOT_DIR, 'build', 'image', BUILD_IMAGE_NAME, 'work-uefi', 'ovmf-vars-debug-f12.fd');
fs.mkdirSync(path.dirname(OVMF_VARS_COPY), { recursive: true });
fs.copyFileSync(OVMF_VARS_PATH, OVMF_VARS_COPY);

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
        execFileSync('killall', ['-q', 'qemu-system-x86_64', 'gdb', 'cgdb'], { stdio: 'ignore' });
    } catch (error) {
        // Nothing to kill.
    }
}

function WaitForProcessExit(child) {
    return new Promise((resolve) => {
        child.once('exit', resolve);
        child.once('error', resolve);
    });
}

async function Main() {
    Cleanup();
    fs.rmSync(LOG_KERNEL, { force: true });
    fs.rmSync(LOG_COM1, { force: true });
    fs.rmSync(GDB_DUMP_LOG, { force: true });

    // UEFI image filesystem starts at 4194304 in this build layout.
    SetImageKeyboardLayout(IMG_PATH, 4194304, process.env.KEYBOARD_LAYOUT);

    const qemuOutFd = fs.openSync(QEMU_STDOUT_LOG, 'w');
    const qemuProcess = spawn('qemu-system-x86_64', [
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
        '-drive', `if=pflash,format=raw,readonly=on,file=${OVMF_CODE_PATH}`,
        '-drive', `if=pflash,format=raw,file=${OVMF_VARS_COPY}`,
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
        '-s',
    ], {
        detached: true,
        stdio: ['ignore', qemuOutFd, qemuOutFd],
    });
    qemuPid = qemuProcess.pid;

    if (!(await monitor.WaitForMonitor())) {
        console.error('QEMU monitor did not start.');
        return 1;
    }

    const startTime = Date.now();
    while (!fs.readFileSync(LOG_KERNEL, 'utf8').includes('[InitializeKernel] Shell task created')) {
        if (!isProcessAlive(qemuProcess)) {
            console.error('QEMU exited before shell-ready');
            return 1;
        }
        if (Date.now() - startTime >= SHELL_READY_TIMEOUT_SECONDS * 1000) {
            console.error('Timed out waiting for shell-ready log');
            return 1;
        }
        await Sleep(200);
    }

    console.log('[f12-debug] shell ready detected');
    await monitor.SendKey('ret');
    await Sleep(300);
    await monitor.SendCommand('desktop show');
    console.log('[f12-debug] desktop show sent');
    await Sleep(DESKTOP_SETTLE_SECONDS * 1000);
    await monitor.SendKey('f12');
    console.log('[f12-debug] F12 sent');
    await Sleep(POST_F12_WAIT_SECONDS * 1000);

    const gdbProcess = spawn('gdb', [
        '-q', DEBUG_ELF,
        '-ex', 'set architecture i386:x86-64',
        '-ex', 'set pagination off',
        '-ex', 'set confirm off',
        '-ex', `target remote :${config.GDB_PORT}`,
        '-ex', 'interrupt',
        '-ex', 'printf "\\n==== GDB SNAPSHOT AFTER F12 ====\\n"',
        '-ex', 'bt',
        '-ex', 'bt 20',
        '-ex', 'info reg',
        '-ex', 'x/16i $rip',
        '-ex', 'info threads',
        '-ex', 'detach',
        '-ex', 'quit',
    ], {
        stdio: ['ignore', 'pipe', 'pipe'],
    });

    const gdbDumpFd = fs.openSync(GDB_DUMP_LOG, 'w');
    gdbProcess.stdout.pipe(fs.createWriteStream(GDB_DUMP_LOG, { flags: 'a' }));
    gdbProcess.stderr.pipe(fs.createWriteStream(GDB_DUMP_LOG, { flags: 'a' }));
    await WaitForProcessExit(gdbProcess);
    gdbDumpFd;

    console.log(`[f12-debug] gdb snapshot saved: ${GDB_DUMP_LOG}`);
    console.log(`[f12-debug] kernel log: ${LOG_KERNEL}`);
    console.log(`[f12-debug] qemu stdout: ${QEMU_STDOUT_LOG}`);

    const dumpLines = fs.readFileSync(GDB_DUMP_LOG, 'utf8').split('\n');
    console.log(dumpLines.slice(-120).join('\n'));
    return 0;
}

function isProcessAlive(child) {
    if (!child || child.exitCode !== null) {
        return false;
    }
    try {
        process.kill(child.pid, 0);
        return true;
    } catch (error) {
        return false;
    }
}

Main()
    .then((exitCode) => {
        process.exitCode = exitCode;
    })
    .catch((error) => {
        console.error(error.message);
        process.exitCode = 1;
    })
    .finally(() => {
        Cleanup();
    });
