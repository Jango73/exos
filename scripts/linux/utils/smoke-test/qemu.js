'use strict';

const fs = require('node:fs');
const { spawn } = require('node:child_process');
const config = require('./config');
const { MonitorCommand } = require('./monitor');
const { Sleep } = require('./util');

async function WaitForImageReady(imagePath) {
    const startTime = Date.now();
    let stableCount = 0;
    let lastSize = '';
    let lastMTime = '';

    while (Date.now() - startTime < config.IMAGE_READY_TIMEOUT_SECONDS * 1000) {
        if (fs.existsSync(imagePath)) {
            const stats = fs.statSync(imagePath);
            const currentSize = String(stats.size);
            const currentMTime = String(Math.floor(stats.mtimeMs / 1000));

            if (currentSize === lastSize && currentMTime === lastMTime) {
                stableCount += 1;
            } else {
                stableCount = 0;
            }
            lastSize = currentSize;
            lastMTime = currentMTime;

            if (stableCount >= config.IMAGE_READY_STABLE_POLLS) {
                return;
            }
        }
        await Sleep(config.IMAGE_READY_POLL_SECONDS * 1000);
    }

    throw new Error(`Timed out waiting for image to finish writing: ${imagePath}`);
}

function SpawnDetached(command, workingDirectory) {
    const child = spawn('bash', ['-c', command], {
        cwd: workingDirectory,
        detached: true,
        stdio: 'inherit',
    });
    child.unref();
    return child;
}

function IsProcessAlive(child) {
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

async function WaitForProcessExit(child, timeoutSeconds) {
    const startTime = Date.now();
    while (IsProcessAlive(child)) {
        if (Date.now() - startTime >= timeoutSeconds * 1000) {
            return false;
        }
        await Sleep(200);
    }
    return true;
}

async function StopQemu() {
    await MonitorCommand('quit', 1, 1, config.MONITOR_COMMAND_HOLD_OPEN_SECONDS);
}

async function StopActiveQemuSession() {
    const child = config.ACTIVE_QEMU_SESSION_PID;
    if (!child) {
        return;
    }

    if (IsProcessAlive(child)) {
        await StopQemu();
    }

    if (!(await WaitForProcessExit(child, 4))) {
        try {
            process.kill(-child.pid, 'SIGTERM');
        } catch (error) {
            try {
                process.kill(child.pid, 'SIGTERM');
            } catch (killError) {
                // Process already gone.
            }
        }
    }

    if (!(await WaitForProcessExit(child, 4))) {
        try {
            process.kill(-child.pid, 'SIGKILL');
        } catch (error) {
            try {
                process.kill(child.pid, 'SIGKILL');
            } catch (killError) {
                // Process already gone.
            }
        }
    }

    config.ACTIVE_QEMU_SESSION_PID = '';
}

async function StopActiveQemuSessionOnFailureIfNeeded() {
    if (config.KEEP_QEMU_ON_FAIL === 1) {
        return;
    }
    await StopActiveQemuSession();
}

module.exports = {
    WaitForImageReady,
    SpawnDetached,
    IsProcessAlive,
    WaitForProcessExit,
    StopQemu,
    StopActiveQemuSession,
    StopActiveQemuSessionOnFailureIfNeeded,
};
