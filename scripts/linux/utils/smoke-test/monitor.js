'use strict';

const net = require('node:net');
const config = require('./config');
const { Sleep } = require('./util');

function ParseTelnetNegotiation(buffer) {
    // QEMU monitor is a telnet server: it negotiates options on connect.
    // Refuse every option with WONT/DONT so it falls back to raw mode.
    const responses = [];
    for (let index = 0; index < buffer.length; index++) {
        if (buffer[index] !== 0xff) {
            continue;
        }
        if (index + 2 >= buffer.length) {
            break;
        }
        const command = buffer[index + 1];
        const option = buffer[index + 2];
        if (command === 0xff) {
            index += 1;
            continue;
        }
        if (command === 0xfb) {
            responses.push(Buffer.from([0xff, 0xfc, option]));
        } else if (command === 0xfd) {
            responses.push(Buffer.from([0xff, 0xfe, option]));
        }
        index += 2;
    }
    return responses;
}

function AttachTelnetNegotiation(socket) {
    socket.on('data', (buffer) => {
        const responses = ParseTelnetNegotiation(buffer);
        for (const response of responses) {
            if (socket.writable && !socket.destroyed) {
                try {
                    socket.write(response);
                } catch (error) {
                    // Socket is closing; the response is best effort.
                }
            }
        }
    });
}

function SendRawToMonitor(data, holdOpenSeconds) {
    return new Promise((resolve, reject) => {
        const socket = net.createConnection({
            host: config.MONITOR_HOST,
            port: config.MONITOR_PORT,
        });

        let settled = false;

        const onError = (error) => {
            if (!settled) {
                settled = true;
                socket.destroy();
                reject(error);
            }
        };

        const onConnect = () => {
            socket.write(data + '\r\n');
            if (holdOpenSeconds && holdOpenSeconds > 0) {
                setTimeout(() => {
                    socket.end();
                }, holdOpenSeconds * 1000);
            } else {
                socket.end();
            }
        };

        const onClose = () => {
            if (!settled) {
                settled = true;
                resolve();
            }
        };

        AttachTelnetNegotiation(socket);
        socket.once('error', onError);
        socket.once('connect', onConnect);
        socket.once('close', onClose);
    });
}

function SendRawToMonitorWithResponse(data, holdOpenSeconds) {
    return new Promise((resolve, reject) => {
        const socket = net.createConnection({
            host: config.MONITOR_HOST,
            port: config.MONITOR_PORT,
        });

        let settled = false;
        let output = '';

        const onError = (error) => {
            if (!settled) {
                settled = true;
                socket.destroy();
                reject(error);
            }
        };

        const onData = (buffer) => {
            const responses = ParseTelnetNegotiation(buffer);
            for (const response of responses) {
                if (socket.writable && !socket.destroyed) {
                    try {
                        socket.write(response);
                    } catch (error) {
                        // Socket is closing; the response is best effort.
                    }
                }
            }
            output += buffer.toString('latin1');
        };

        const onConnect = () => {
            socket.write(data + '\r\n');
            if (holdOpenSeconds && holdOpenSeconds > 0) {
                setTimeout(() => {
                    socket.end();
                }, holdOpenSeconds * 1000);
            } else {
                socket.end();
            }
        };

        const onClose = () => {
            if (!settled) {
                settled = true;
                resolve(output);
            }
        };

        socket.on('data', onData);
        socket.once('error', onError);
        socket.once('connect', onConnect);
        socket.once('close', onClose);
    });
}

async function MonitorCommand(cmd, maxAttempts, quiet, holdOpenSeconds) {
    const attempts = maxAttempts === undefined ? config.MONITOR_CONNECT_MAX_ATTEMPTS : maxAttempts;
    const isQuiet = quiet === undefined ? 0 : quiet;
    const holdOpen = holdOpenSeconds === undefined ? 0 : holdOpenSeconds;
    let attempt = 0;
    let delay = 50;

    while (attempt < attempts) {
        try {
            await SendRawToMonitor(cmd, holdOpen);
            return true;
        } catch (error) {
            attempt += 1;
            if (attempt >= 10 && attempt < 30) {
                delay = 100;
            } else if (attempt >= 30) {
                delay = 200;
            }
            await Sleep(delay);
        }
    }

    if (isQuiet !== 1) {
        console.error(`Failed to connect to QEMU monitor at ${config.MONITOR_HOST}:${config.MONITOR_PORT} after ${attempts} attempts`);
    }
    return false;
}

async function WaitForMonitor() {
    let index = 0;
    let delay = 50;

    while (index < config.MONITOR_CONNECT_MAX_ATTEMPTS) {
        try {
            await SendRawToMonitor('', 0);
            return true;
        } catch (error) {
            index += 1;
            if (index >= 10 && index < 30) {
                delay = 100;
            } else if (index >= 30) {
                delay = 200;
            }
            await Sleep(delay);
        }
    }

    return false;
}

function KeyForChar(character) {
    if (/[A-Z]/.test(character)) {
        return `shift-${character.toLowerCase()}`;
    }
    if (/[a-z0-9]/.test(character)) {
        return character;
    }
    switch (character) {
        case ' ':
            return 'spc';
        case '!':
            return 'shift-1';
        case '"':
            return 'shift-apostrophe';
        case '#':
            return 'shift-3';
        case '$':
            return 'shift-4';
        case '%':
            return 'shift-5';
        case '&':
            return 'shift-7';
        case "'":
            return 'apostrophe';
        case '(':
            return 'shift-9';
        case ')':
            return 'shift-0';
        case '*':
            return 'shift-8';
        case '+':
            return 'shift-equal';
        case ',':
            return 'comma';
        case '-':
            return 'minus';
        case '.':
            return 'dot';
        case '/':
            return 'slash';
        case ':':
            return 'shift-semicolon';
        case ';':
            return 'semicolon';
        case '<':
            return 'shift-comma';
        case '=':
            return 'equal';
        case '>':
            return 'shift-dot';
        case '?':
            return 'shift-slash';
        case '@':
            return 'shift-2';
        case '[':
            return 'bracket_left';
        case '\\':
            return 'backslash';
        case ']':
            return 'bracket_right';
        case '^':
            return 'shift-6';
        case '_':
            return 'shift-minus';
        case '`':
            return 'grave_accent';
        case '{':
            return 'shift-bracket_left';
        case '|':
            return 'shift-backslash';
        case '}':
            return 'shift-bracket_right';
        case '~':
            return 'shift-grave_accent';
        default:
            return '';
    }
}

async function SendKey(key, holdOpenSeconds, keyDelaySeconds) {
    const holdOpen = holdOpenSeconds === undefined ? 0 : holdOpenSeconds;
    const keyDelay = keyDelaySeconds === undefined ? config.KEY_DELAY_SECONDS : keyDelaySeconds;
    if (!key) {
        throw new Error('Unsupported key in command string.');
    }
    await MonitorCommand(`sendkey ${key}`, undefined, '', holdOpen);
    await Sleep(keyDelay * 1000);
}

async function SendText(text) {
    for (let index = 0; index < text.length; index++) {
        const character = text[index];

        if (character === '\r' && text[index + 1] === '\n') {
            index += 1;
        }

        if (character === '\r' || character === '\n') {
            await SendKey('ret', config.MONITOR_COMMAND_HOLD_OPEN_SECONDS, config.TYPE_KEY_DELAY_SECONDS);
            continue;
        }

        if (character === '\t') {
            await SendKey('spc', undefined, config.TYPE_KEY_DELAY_SECONDS);
            await SendKey('spc', undefined, config.TYPE_KEY_DELAY_SECONDS);
            await SendKey('spc', undefined, config.TYPE_KEY_DELAY_SECONDS);
            await SendKey('spc', undefined, config.TYPE_KEY_DELAY_SECONDS);
            continue;
        }

        await SendKey(KeyForChar(character), undefined, config.TYPE_KEY_DELAY_SECONDS);
    }
    await Sleep(config.COMMAND_DELAY_SECONDS * 1000);
}

async function SendHotkey(hotkey) {
    if (!hotkey) {
        throw new Error('Unsupported empty hotkey.');
    }
    await MonitorCommand(`sendkey ${hotkey}`, undefined, '', config.MONITOR_COMMAND_HOLD_OPEN_SECONDS);
    await Sleep(config.COMMAND_DELAY_SECONDS * 1000);
}

async function SendCommand(cmd) {
    await SendCommandWithMode(cmd, 'transient');
}

async function SendCommandWithPersistentMonitor(cmd) {
    return new Promise((resolve, reject) => {
        const socket = net.createConnection({
            host: config.MONITOR_HOST,
            port: config.MONITOR_PORT,
        });

        let settled = false;
        let index = 0;
        const length = cmd.length;

        const onError = (error) => {
            if (!settled) {
                settled = true;
                socket.destroy();
                reject(error);
            }
        };

        const onConnect = () => {
            const typeNextCharacter = async () => {
                while (index < length) {
                    const character = cmd[index];
                    const key = KeyForChar(character);
                    if (!key) {
                        socket.destroy();
                        reject(new Error('Unsupported key in command string.'));
                        return;
                    }
                    socket.write(`sendkey ${key}\r\n`);
                    index += 1;
                    await Sleep(config.KEY_DELAY_SECONDS * 1000);
                }
                socket.write('sendkey ret\r\n');
                await Sleep(config.KEY_DELAY_SECONDS * 1000);
                socket.end();
            };
            typeNextCharacter();
        };

        const onClose = () => {
            if (!settled) {
                settled = true;
                resolve();
            }
        };

        AttachTelnetNegotiation(socket);
        socket.once('error', onError);
        socket.once('connect', onConnect);
        socket.once('close', onClose);
    });
}

async function SendCommandWithMode(cmd, mode) {
    const activeMode = mode || 'transient';

    if (activeMode === 'transient') {
        for (let index = 0; index < cmd.length; index++) {
            const character = cmd[index];
            const key = KeyForChar(character);
            await SendKey(key);
        }
        await SendKey('ret', config.MONITOR_COMMAND_HOLD_OPEN_SECONDS);
        await Sleep(config.COMMAND_DELAY_SECONDS * 1000);
        return;
    }

    if (activeMode === 'persistent') {
        await SendCommandWithPersistentMonitor(cmd);
        return;
    }

    throw new Error(`Invalid monitor mode: ${activeMode}`);
}

module.exports = {
    SendRawToMonitor,
    SendRawToMonitorWithResponse,
    MonitorCommand,
    WaitForMonitor,
    KeyForChar,
    SendKey,
    SendText,
    SendHotkey,
    SendCommand,
    SendCommandWithPersistentMonitor,
    SendCommandWithMode,
};
