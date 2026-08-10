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
const { SetImageKeyboardLayout } = require('../utils/smoke-test/image');
const { WaitForShellReady, WaitForLogPattern, SendShellCommandAndWait, CheckFaults } = require('../utils/smoke-test/guest-runner');
const { MouseButtonState, MouseMoveSmooth, MouseHomeTopLeft, SelectQemuMouseDevice } = require('../utils/smoke-test/mouse');
const { Sleep } = require('../utils/smoke-test/util');

const ARCH = process.env.ARCH;
const FS = process.env.FS;
const CONFIG = process.env.CONFIG;
const DRAG_CYCLES = Number(process.env.DRAG_CYCLES || 20);
const DRAG_SEGMENTS = Number(process.env.DRAG_SEGMENTS || 70);
const DRAG_STEP_X = Number(process.env.DRAG_STEP_X || 8);
const DRAG_STEP_DELAY = Number(process.env.DRAG_STEP_DELAY || 0.015);
const AFTER_COMMAND_DELAY = Number(process.env.AFTER_COMMAND_DELAY || 1.0);
const SHELL_READY_PATTERN = process.env.SHELL_READY_PATTERN || '[InitializeKernel] Shell task created';
const PORTAL_READY_PATTERN = process.env.PORTAL_READY_PATTERN || '[PortalShowDesktop] desktop ready';
const SHELL_READY_TIMEOUT_SECONDS = Number(process.env.SHELL_READY_TIMEOUT_SECONDS || 20);
const PORTAL_READY_TIMEOUT_SECONDS = Number(process.env.PORTAL_READY_TIMEOUT_SECONDS || 15);
const KERNEL_STALL_TIMEOUT_SECONDS = Number(process.env.KERNEL_STALL_TIMEOUT_SECONDS || 18);

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
const RUN_LOG = path.join(config.ROOT_DIR, 'temp', `windowing-run-${BUILD_CORE_NAME}.log`);

const mouseState = {
    useXdotool: false,
    pointerX: 0,
    pointerY: 0,
    windowId: '',
};

if (!fs.existsSync(IMG_PATH)) {
    console.error(`Missing image: ${IMG_PATH}`);
    console.error(`Build first: bash scripts/linux/build/build.sh --arch ${ARCH} --fs ${FS} --${CONFIG}`);
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

async function DragLoop() {
    const sweepX = DRAG_SEGMENTS * DRAG_STEP_X;

    // Pointer to first window title bar (window A starts at 48,56 in Desktop-InternalTest).
    await MouseHomeTopLeft(monitor, mouseState);
    await MouseMoveSmooth(monitor, mouseState, 90, 70, 30, DRAG_STEP_DELAY);

    for (let cycle = 1; cycle <= DRAG_CYCLES; cycle++) {
        console.log(`[windowing] drag cycle ${cycle}/${DRAG_CYCLES}`);

        await MouseButtonState(monitor, 1);
        await MouseMoveSmooth(monitor, mouseState, sweepX, 0, DRAG_SEGMENTS, DRAG_STEP_DELAY);
        await MouseButtonState(monitor, 0);
        await Sleep(40);

        await MouseButtonState(monitor, 1);
        await MouseMoveSmooth(monitor, mouseState, -sweepX, 0, DRAG_SEGMENTS, DRAG_STEP_DELAY);
        await MouseButtonState(monitor, 0);
        await Sleep(60);
    }
}

async function Main() {
    Cleanup();
    fs.rmSync(LOG_KERNEL, { force: true });

    SetImageKeyboardLayout(IMG_PATH, 1048576, process.env.KEYBOARD_LAYOUT);

    const runLogFd = fs.openSync(RUN_LOG, 'w');
    const qemuChild = spawn('bash', ['-lc', `MONITOR_PORT=${config.MONITOR_PORT} scripts/linux/run/run --arch ${ARCH} --fs ${FS} --${CONFIG}`], {
        cwd: config.ROOT_DIR,
        detached: true,
        stdio: ['ignore', runLogFd, runLogFd],
    });
    qemuWrapperPid = qemuChild.pid;

    if (!(await monitor.WaitForMonitor())) {
        console.error('[windowing] QEMU monitor did not start.');
        return 1;
    }
    if (!(await WaitForShellReady({
        qemuChild,
        logKernelPath: LOG_KERNEL,
        runLogPath: RUN_LOG,
        shellReadyPattern: SHELL_READY_PATTERN,
        shellReadyTimeoutSeconds: SHELL_READY_TIMEOUT_SECONDS,
        kernelStallTimeoutSeconds: KERNEL_STALL_TIMEOUT_SECONDS,
    }))) {
        return 1;
    }

    await SendShellCommandAndWait({
        qemuChild,
        logKernelPath: LOG_KERNEL,
        commandText: 'run -b /system/apps/portal',
        validationPattern: '',
        timeoutSeconds: 15,
        kernelStallTimeoutSeconds: KERNEL_STALL_TIMEOUT_SECONDS,
        afterCommandDelaySeconds: AFTER_COMMAND_DELAY,
        sendCommand: monitor.SendCommand,
        sendKey: monitor.SendKey,
    });

    if (!(await WaitForLogPattern({
        qemuChild,
        logKernelPath: LOG_KERNEL,
        pattern: PORTAL_READY_PATTERN,
        timeoutSeconds: PORTAL_READY_TIMEOUT_SECONDS,
        kernelStallTimeoutSeconds: KERNEL_STALL_TIMEOUT_SECONDS,
    }))) {
        return 1;
    }

    SelectQemuMouseDevice(monitor, false);
    await DragLoop();

    if (!CheckFaults(LOG_KERNEL)) {
        return 1;
    }

    console.log(`[windowing] drag sequence completed (cycles=${DRAG_CYCLES}, config=${CONFIG}).`);
    console.log(`[windowing] logs: ${LOG_KERNEL}`);
    console.log(`[windowing] qemu wrapper log: ${RUN_LOG}`);
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
