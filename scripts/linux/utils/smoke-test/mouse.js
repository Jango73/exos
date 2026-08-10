'use strict';

const { execFileSync } = require('node:child_process');
const { Sleep } = require('./util');
const { SendRawToMonitorWithResponse } = require('./monitor');

function StripAnsiEscapeCodes(text) {
    return text.replace(/\x1b\[[0-9;]*[A-Za-z]/g, '');
}

async function GetQemuMouseIndex(monitor) {
    // QEMU monitor: "  Mouse #4: QEMU HID Mouse". Prefer the HID (USB) mouse,
    // which is the device the kernel's USB HID driver actually reads.
    const response = StripAnsiEscapeCodes(await SendRawToMonitorWithResponse('info mice', 0.6));
    const lines = response.split('\r\n');
    for (const line of lines) {
        if (line.includes('HID Mouse')) {
            const match = /Mouse #([0-9]+):/.exec(line);
            if (match) {
                return Number(match[1]);
            }
        }
    }
    return -1;
}

async function SelectQemuMouseDevice(monitor, useXdotool) {
    if (useXdotool) {
        return;
    }
    const index = await GetQemuMouseIndex(monitor);
    if (index < 0) {
        for (const candidate of [0, 1, 2, 0]) {
            monitor.MonitorCommand(`mouse_set ${candidate}`, 5, 1);
        }
        return;
    }
    monitor.MonitorCommand(`mouse_set ${index}`, 5, 1);
}

async function MouseButtonState(monitor, mask) {
    await monitor.MonitorCommand(`mouse_button ${mask}`);
    await Sleep(20);
}

async function MouseMoveSmooth(monitor, state, totalX, totalY, segments, stepDelaySeconds) {
    if (segments <= 0) {
        segments = 1;
    }

    for (let index = 0; index < segments; index++) {
        let stepX = Math.floor(totalX / segments);
        let stepY = Math.floor(totalY / segments);

        if (index === segments - 1) {
            stepX = totalX - stepX * (segments - 1);
            stepY = totalY - stepY * (segments - 1);
        }

        if (state.useXdotool) {
            state.pointerX += stepX;
            state.pointerY += stepY;
            execFileSync('xdotool', ['mousemove', '--sync', '--window', String(state.windowId), String(state.pointerX), String(state.pointerY)], { stdio: 'ignore' });
        } else {
            await monitor.MonitorCommand(`mouse_move ${stepX} ${stepY}`);
        }
        await Sleep(stepDelaySeconds * 1000);
    }
}

async function MouseHomeTopLeft(monitor, state) {
    if (state.useXdotool) {
        state.pointerX = 5;
        state.pointerY = 5;
        execFileSync('xdotool', ['mousemove', '--sync', '--window', String(state.windowId), '5', '5'], { stdio: 'ignore' });
        await Sleep(20);
        return;
    }

    for (let index = 0; index < 70; index++) {
        await monitor.MonitorCommand('mouse_move -20 -20');
        await Sleep(5);
    }
}

module.exports = {
    MouseButtonState,
    MouseMoveSmooth,
    MouseHomeTopLeft,
    SelectQemuMouseDevice,
};
