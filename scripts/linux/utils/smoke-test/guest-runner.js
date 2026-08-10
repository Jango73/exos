'use strict';

const fs = require('node:fs');
const { IsProcessAlive } = require('./qemu');
const { Sleep } = require('./util');

const FAULT_REGEX = /#PF|#GP|#UD|#SS|#NP|#TS|#DE|#DF|#MF|#AC|#MC/;

function GetLastKernelTick(logKernelPath) {
    try {
        const text = fs.readFileSync(logKernelPath, 'utf8');
        const matches = [...text.matchAll(/T([0-9]+)>/g)];
        if (matches.length === 0) {
            return 0;
        }
        return Number(matches[matches.length - 1][1]);
    } catch (error) {
        return 0;
    }
}

function LogContains(logKernelPath, pattern) {
    try {
        return fs.readFileSync(logKernelPath, 'utf8').includes(pattern);
    } catch (error) {
        return false;
    }
}

function LogContainsFault(logKernelPath) {
    try {
        return FAULT_REGEX.test(fs.readFileSync(logKernelPath, 'utf8'));
    } catch (error) {
        return false;
    }
}

async function PollForCondition(options) {
    const {
        qemuChild,
        logKernelPath,
        timeoutSeconds,
        kernelStallTimeoutSeconds,
        predicate,
        runLogPath,
        runLogFailurePattern,
    } = options;

    const startTime = Date.now();
    let lastTick = GetLastKernelTick(logKernelPath);
    let lastTickChange = Date.now();

    while (true) {
        if (!IsProcessAlive(qemuChild)) {
            return { ok: false, reason: 'qemu-exit' };
        }

        if (runLogPath && runLogFailurePattern) {
            try {
                const runLog = fs.readFileSync(runLogPath, 'utf8');
                if (runLogFailurePattern.test(runLog)) {
                    return { ok: false, reason: 'runlog-failure' };
                }
            } catch (error) {
                // Run log may not exist yet.
            }
        }

        if (predicate()) {
            return { ok: true };
        }

        const currentTick = GetLastKernelTick(logKernelPath);
        if (currentTick > lastTick) {
            lastTick = currentTick;
            lastTickChange = Date.now();
        } else if (Date.now() - lastTickChange >= kernelStallTimeoutSeconds * 1000) {
            return { ok: false, reason: 'stall' };
        }

        if (Date.now() - startTime >= timeoutSeconds * 1000) {
            return { ok: false, reason: 'timeout' };
        }

        await Sleep(200);
    }
}

async function WaitForShellReady(options) {
    const {
        qemuChild,
        logKernelPath,
        shellReadyPattern,
        shellReadyTimeoutSeconds,
        kernelStallTimeoutSeconds,
        runLogPath,
    } = options;

    const result = await PollForCondition({
        qemuChild,
        logKernelPath,
        timeoutSeconds: shellReadyTimeoutSeconds,
        kernelStallTimeoutSeconds,
        runLogPath,
        runLogFailurePattern: /Checksum mismatch\. Halting\.|FileSize too small for checksum\. Halting\./,
        predicate: () => LogContains(logKernelPath, shellReadyPattern),
    });

    if (!result.ok) {
        const reasons = {
            'qemu-exit': 'QEMU exited before shell-ready.',
            'runlog-failure': 'Bootloader checksum failure detected.',
            stall: 'Kernel tick stalled before shell-ready.',
            timeout: 'Timed out waiting for shell-ready log pattern.',
        };
        console.error(`[guest] ${reasons[result.reason]}`);
        return false;
    }
    return true;
}

async function WaitForLogPattern(options) {
    const {
        qemuChild,
        logKernelPath,
        pattern,
        timeoutSeconds,
        kernelStallTimeoutSeconds,
    } = options;

    const result = await PollForCondition({
        qemuChild,
        logKernelPath,
        timeoutSeconds,
        kernelStallTimeoutSeconds,
        predicate: () => LogContains(logKernelPath, pattern),
    });

    if (!result.ok) {
        console.error(`[guest] Timed out waiting for log pattern: ${pattern}`);
        return false;
    }
    return true;
}

async function SendShellCommandAndWait(options) {
    const {
        qemuChild,
        logKernelPath,
        commandText,
        validationPattern,
        timeoutSeconds,
        kernelStallTimeoutSeconds,
        afterCommandDelaySeconds,
        sendCommand,
        sendKey,
    } = options;

    await sendKey('ret');
    await Sleep(300);

    const startLine = GetLogLineCount(logKernelPath);
    await sendCommand(commandText);
    await Sleep(afterCommandDelaySeconds * 1000);

    if (!validationPattern) {
        return true;
    }

    const result = await PollForCondition({
        qemuChild,
        logKernelPath,
        timeoutSeconds,
        kernelStallTimeoutSeconds,
        predicate: () => TailContainsPattern(logKernelPath, startLine, validationPattern),
    });

    if (!result.ok) {
        console.error(`[guest] Command validation failed: ${commandText}`);
        return false;
    }
    return true;
}

function GetLogLineCount(logKernelPath) {
    try {
        const text = fs.readFileSync(logKernelPath, 'utf8');
        return text.split('\n').length;
    } catch (error) {
        return 0;
    }
}

function TailContainsPattern(logKernelPath, startLine, pattern) {
    try {
        const text = fs.readFileSync(logKernelPath, 'utf8');
        const lines = text.split('\n');
        const tail = lines.slice(startLine).join('\n');
        return tail.includes(pattern);
    } catch (error) {
        return false;
    }
}

function CheckFaults(logKernelPath) {
    if (LogContainsFault(logKernelPath)) {
        console.error('[guest] fault detected in kernel log');
        try {
            const text = fs.readFileSync(logKernelPath, 'utf8');
            for (const line of text.split('\n')) {
                if (FAULT_REGEX.test(line)) {
                    console.error(line);
                }
            }
        } catch (error) {
            // Log gone.
        }
        return false;
    }
    return true;
}

module.exports = {
    GetLastKernelTick,
    WaitForShellReady,
    WaitForLogPattern,
    SendShellCommandAndWait,
    CheckFaults,
};
