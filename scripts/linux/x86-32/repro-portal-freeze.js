'use strict';

process.env.MONITOR_PORT = process.env.MONITOR_PORT || '4462';
process.env.KEYBOARD_LAYOUT = process.env.KEYBOARD_LAYOUT || 'en-US';
process.env.ARCH = process.env.ARCH || 'x86-32';
process.env.FS = process.env.FS || 'ext2';
process.env.CONFIG = process.env.CONFIG || 'debug';

const fs = require('node:fs');
const path = require('node:path');
const { spawn, execFileSync } = require('node:child_process');
const config = require('../utils/smoke-test/config');
const monitor = require('../utils/smoke-test/monitor');
const { SetImageKeyboardLayout, SetImagePortalAutoRun } = require('../utils/smoke-test/image');
const { WaitForShellReady, WaitForLogPattern } = require('../utils/smoke-test/guest-runner');
const { MouseMoveSmooth, MouseHomeTopLeft, SelectQemuMouseDevice } = require('../utils/smoke-test/mouse');
const { Sleep } = require('../utils/smoke-test/util');

const INPUT_BACKEND = process.env.INPUT_BACKEND || 'monitor';
const AFTER_BOOT_DELAY_SECONDS = Number(process.env.AFTER_BOOT_DELAY_SECONDS || 1.5);
const MOVE_DELAY_SECONDS = Number(process.env.MOVE_DELAY_SECONDS || 0.008);
const OUTER_CYCLES = Number(process.env.OUTER_CYCLES || 0);
const INNER_SWEEPS_PER_CYCLE = Number(process.env.INNER_SWEEPS_PER_CYCLE || 150);
const SHELL_READY_PATTERN = process.env.SHELL_READY_PATTERN || '[InitializeKernel] Shell task created';
const PORTAL_READY_PATTERN = process.env.PORTAL_READY_PATTERN || '[PortalShowDesktop] desktop ready';
const SHELL_READY_TIMEOUT_SECONDS = Number(process.env.SHELL_READY_TIMEOUT_SECONDS || 20);
const PORTAL_READY_TIMEOUT_SECONDS = Number(process.env.PORTAL_READY_TIMEOUT_SECONDS || 15);
const KERNEL_STALL_TIMEOUT_SECONDS = Number(process.env.KERNEL_STALL_TIMEOUT_SECONDS || 18);
const QEMU_WINDOW_NAME_PATTERN = process.env.QEMU_WINDOW_NAME_PATTERN || '^QEMU$';
const XDOTOOL_GRAB_X = Number(process.env.XDOTOOL_GRAB_X || 640);
const XDOTOOL_GRAB_Y = Number(process.env.XDOTOOL_GRAB_Y || 512);
const FOCUS_SETTLE_DELAY_SECONDS = Number(process.env.FOCUS_SETTLE_DELAY_SECONDS || 0.2);

const USE_XDOTOOL = INPUT_BACKEND === 'xdotool';

const ARCH = process.env.ARCH;
const FS = process.env.FS;
const CONFIG = process.env.CONFIG;

if (ARCH !== 'x86-32') {
    console.error('This script is intended for ARCH=x86-32.');
    process.exit(1);
}
if (CONFIG !== 'debug' && CONFIG !== 'release') {
    console.error('CONFIG must be debug or release.');
    process.exit(1);
}

const BUILD_CORE_NAME = `${ARCH}-mbr-${CONFIG}`;
const BUILD_IMAGE_NAME = `${BUILD_CORE_NAME}-${FS}`;
const IMG_PATH = path.join(config.ROOT_DIR, 'build', 'image', BUILD_IMAGE_NAME, 'exos.img');
const LOG_KERNEL = path.join(config.ROOT_DIR, 'log', `kernel-${BUILD_CORE_NAME}.log`);
const RUN_LOG = path.join('/tmp', `repro-portal-freeze-${BUILD_CORE_NAME}.log`);

const mouseState = {
    useXdotool: USE_XDOTOOL,
    pointerX: 0,
    pointerY: 0,
    windowId: '',
};

function PrintUsage() {
    console.log(`Usage: bash scripts/linux/x86-32/repro-portal-freeze.sh

Environment overrides:
  INPUT_BACKEND                 xdotool or monitor
  MONITOR_PORT                  QEMU monitor port
  KEYBOARD_LAYOUT               Keyboard layout to inject in image
  FS                            Filesystem, default ext2
  CONFIG                        debug or release
  MOVE_DELAY_SECONDS            Delay between monitor mouse_move packets
  OUTER_CYCLES                  0 means infinite, otherwise finite cycle count
  INNER_SWEEPS_PER_CYCLE        Number of border sweeps per outer cycle`);
}

if (process.argv.includes('--help')) {
    PrintUsage();
    process.exit(0);
}

if (!fs.existsSync(IMG_PATH)) {
    console.error(`Missing image: ${IMG_PATH}`);
    console.error(`Build first: bash scripts/linux/build/build --arch ${ARCH} --fs ${FS} --${CONFIG}`);
    process.exit(1);
}

let qemuWrapperPid = null;

function IsQemuAlive() {
    if (!qemuWrapperPid) {
        return false;
    }
    try {
        process.kill(qemuWrapperPid, 0);
        return true;
    } catch (error) {
        return false;
    }
}

function Cleanup() {
    if (IsQemuAlive()) {
        try {
            process.kill(qemuWrapperPid, 'SIGTERM');
        } catch (error) {
            // Already gone.
        }
    }
    try {
        execFileSync('killall', ['-q', 'qemu-system-i386', 'qemu-system-x86_64'], { stdio: 'ignore' });
    } catch (error) {
        // Nothing to kill.
    }
}

function EnsureInputBackendReady() {
    let windowIds;
    try {
        windowIds = execFileSync('xdotool', ['search', '--name', QEMU_WINDOW_NAME_PATTERN], {
            encoding: 'utf8',
            stdio: ['ignore', 'pipe', 'ignore'],
        }).trim();
    } catch (error) {
        if (!USE_XDOTOOL) {
            return;
        }
        console.error('xdotool not found but INPUT_BACKEND=' + INPUT_BACKEND);
        process.exit(1);
    }

    const ids = windowIds.split('\n').filter((value) => value.length > 0);
    mouseState.windowId = ids[ids.length - 1];
    if (!mouseState.windowId) {
        if (!USE_XDOTOOL) {
            return;
        }
        console.error(`Could not find QEMU window matching ${QEMU_WINDOW_NAME_PATTERN}`);
        process.exit(1);
    }

    execFileSync('xdotool', ['windowactivate', '--sync', mouseState.windowId], { stdio: 'ignore' });
    Sleep(FOCUS_SETTLE_DELAY_SECONDS * 1000);
    execFileSync('xdotool', ['mousemove', '--sync', '--window', mouseState.windowId, String(XDOTOOL_GRAB_X), String(XDOTOOL_GRAB_Y)], { stdio: 'ignore' });
    Sleep(FOCUS_SETTLE_DELAY_SECONDS * 1000);
    execFileSync('xdotool', ['mousedown', '--window', mouseState.windowId, '1'], { stdio: 'ignore' });
    Sleep(50);
    execFileSync('xdotool', ['mouseup', '--window', mouseState.windowId, '1'], { stdio: 'ignore' });
    Sleep(FOCUS_SETTLE_DELAY_SECONDS * 1000);
}

async function MovePointerToOnScreenDebugBorder() {
    await MouseHomeTopLeft(monitor, mouseState);
    await MouseMoveSmooth(monitor, mouseState, 560, 200, 40, MOVE_DELAY_SECONDS);
}

async function SweepOnScreenDebugBorder() {
    for (let sweepIndex = 1; sweepIndex <= INNER_SWEEPS_PER_CYCLE; sweepIndex++) {
        await MouseMoveSmooth(monitor, mouseState, 90, 0, 12, MOVE_DELAY_SECONDS);
        await MouseMoveSmooth(monitor, mouseState, -90, 0, 12, MOVE_DELAY_SECONDS);
        await MouseMoveSmooth(monitor, mouseState, 0, 230, 20, MOVE_DELAY_SECONDS);
        await MouseMoveSmooth(monitor, mouseState, 0, -230, 20, MOVE_DELAY_SECONDS);
        await MouseMoveSmooth(monitor, mouseState, 90, 90, 18, MOVE_DELAY_SECONDS);
        await MouseMoveSmooth(monitor, mouseState, -90, -90, 18, MOVE_DELAY_SECONDS);
    }
}

async function Main() {
    Cleanup();
    fs.rmSync(LOG_KERNEL, { force: true });

    SetImageKeyboardLayout(IMG_PATH, 1048576, process.env.KEYBOARD_LAYOUT);
    SetImagePortalAutoRun(IMG_PATH, 1048576);

    const runLogFd = fs.openSync(RUN_LOG, 'w');
    const qemuChild = spawn('bash', ['-lc', `MONITOR_PORT=${config.MONITOR_PORT} scripts/linux/run/run --arch ${ARCH} --fs ${FS} --${CONFIG}`], {
        cwd: config.ROOT_DIR,
        detached: true,
        stdio: ['ignore', runLogFd, runLogFd],
    });
    qemuWrapperPid = qemuChild.pid;

    console.log(`[repro-portal-freeze] waiting for QEMU monitor on port ${config.MONITOR_PORT}`);
    if (!(await monitor.WaitForMonitor())) {
        console.error('[repro-portal-freeze] QEMU monitor did not start.');
        return 1;
    }

    console.log('[repro-portal-freeze] waiting for desktop boot');
    if (!(await WaitForShellReady({
        qemuChild,
        logKernelPath: LOG_KERNEL,
        shellReadyPattern: SHELL_READY_PATTERN,
        shellReadyTimeoutSeconds: SHELL_READY_TIMEOUT_SECONDS,
        kernelStallTimeoutSeconds: KERNEL_STALL_TIMEOUT_SECONDS,
    }))) {
        return 1;
    }
    console.log('[repro-portal-freeze] waiting for portal desktop');
    if (!(await WaitForLogPattern({
        qemuChild,
        logKernelPath: LOG_KERNEL,
        pattern: PORTAL_READY_PATTERN,
        timeoutSeconds: PORTAL_READY_TIMEOUT_SECONDS,
        kernelStallTimeoutSeconds: KERNEL_STALL_TIMEOUT_SECONDS,
    }))) {
        return 1;
    }
    await Sleep(AFTER_BOOT_DELAY_SECONDS * 1000);
    EnsureInputBackendReady();

    SelectQemuMouseDevice(monitor, USE_XDOTOOL);
    await MovePointerToOnScreenDebugBorder();

    console.log('[repro-portal-freeze] sweeping OnScreenDebugInfo border');
    console.log(`[repro-portal-freeze] kernel log: ${LOG_KERNEL}`);
    console.log(`[repro-portal-freeze] run log: ${RUN_LOG}`);

    if (OUTER_CYCLES === 0) {
        while (true) {
            await SweepOnScreenDebugBorder();
        }
    }

    for (let cycleIndex = 1; cycleIndex <= OUTER_CYCLES; cycleIndex++) {
        console.log(`[repro-portal-freeze] cycle ${cycleIndex}/${OUTER_CYCLES}`);
        await SweepOnScreenDebugBorder();
    }
    return 0;
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
