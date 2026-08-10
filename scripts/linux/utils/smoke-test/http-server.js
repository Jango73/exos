'use strict';

const fs = require('node:fs');
const net = require('node:net');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const config = require('./config');
const { Sleep } = require('./util');

function ProbePort(host, port) {
    return new Promise((resolve) => {
        const socket = net.createConnection({ host, port });
        const done = (result) => {
            socket.destroy();
            resolve(result);
        };
        socket.once('connect', () => done(true));
        socket.once('error', () => done(false));
    });
}

async function EnsureLocalHttpServer() {
    if (config.SKIP_LOCAL_HTTP_SERVER === 1) {
        return;
    }

    if (!fs.existsSync(config.LOCAL_HTTP_SERVER_SCRIPT)) {
        throw new Error(`Missing local HTTP server script: ${config.LOCAL_HTTP_SERVER_SCRIPT}`);
    }

    console.log('Starting local HTTP server for netget test...');
    execFileSync('bash', [config.LOCAL_HTTP_SERVER_SCRIPT], { stdio: 'ignore' });

    let index = 0;
    while (index < 30) {
        if (await ProbePort('127.0.0.1', config.LOCAL_HTTP_SERVER_PORT)) {
            break;
        }
        index += 1;
        await Sleep(100);
    }

    if (index >= 30) {
        throw new Error(`Local HTTP server did not start on port ${config.LOCAL_HTTP_SERVER_PORT}`);
    }

    try {
        const output = execFileSync('pgrep', ['-f', `http.server ${config.LOCAL_HTTP_SERVER_PORT}`], {
            encoding: 'utf8',
            stdio: ['ignore', 'pipe', 'ignore'],
        });
        config.LOCAL_HTTP_SERVER_PID = output.trim().split('\n')[0];
    } catch (error) {
        config.LOCAL_HTTP_SERVER_PID = '';
    }
}

function StopLocalHttpServer() {
    if (config.SKIP_LOCAL_HTTP_SERVER === 1) {
        return;
    }

    if (config.LOCAL_HTTP_SERVER_PID) {
        try {
            process.kill(Number(config.LOCAL_HTTP_SERVER_PID), 'SIGTERM');
        } catch (error) {
            // Server already gone.
        }
    }
}

module.exports = {
    EnsureLocalHttpServer,
    StopLocalHttpServer,
    ProbePort,
};
