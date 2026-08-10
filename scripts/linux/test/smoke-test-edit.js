'use strict';

const fs = require('node:fs');
const path = require('node:path');
const config = require('../utils/smoke-test/config');
const { SmokeTestMain } = require('../utils/smoke-test/orchestrator');

config.RUN_X86_32_RTL8139 = 0;
config.RUN_X86_64_UEFI = 0; // UEFI edit redraw is catastrophically slow; re-enable when the kernel edit redraw is fixed.

const PROGRAMS_FOLDER = 'scripts/linux/test/edit-programs';
const GUEST_FOLDER = '/temp/edit-smoke';
const COMMANDS_FILE_NAME = 'smoke-test-edit-commands.txt';

function BuildEditSmokeCommandsFile() {
    const programsRoot = path.join(config.ROOT_DIR, PROGRAMS_FOLDER);
    const programs = fs.readdirSync(programsRoot, { withFileTypes: true })
        .filter((entry) => entry.isDirectory())
        .map((entry) => entry.name)
        .sort();

    if (programs.length === 0) {
        throw new Error(`No edit smoke-test programs found in ${programsRoot}`);
    }

    const lines = [
        'command: "pause off" | log: "TEST > [CMD_pause] pause off : OK"',
        'command: "makeFolder /temp/edit-smoke" | log: "TEST > [CMD_makeFolder] makeFolder /temp/edit-smoke : OK"',
    ];

    for (const name of programs) {
        const sourceHostPath = path.join(programsRoot, name, 'source.c');
        const expectedHostPath = path.join(programsRoot, name, 'expected.txt');

        if (!fs.existsSync(sourceHostPath)) {
            throw new Error(`Missing source.c for edit smoke-test program ${name}`);
        }
        if (!fs.existsSync(expectedHostPath)) {
            throw new Error(`Missing expected.txt for edit smoke-test program ${name}`);
        }

        const expectedOutput = fs.readFileSync(expectedHostPath, 'utf8').trim();
        if (!expectedOutput) {
            throw new Error(`Empty expected.txt for edit smoke-test program ${name}`);
        }

        const guestSource = `${GUEST_FOLDER}/${name}.c`;
        const guestBinary = `${GUEST_FOLDER}/${name}`;

        lines.push(`command: "edit -c ${guestSource}"`);
        lines.push(`type: "${sourceHostPath}" | before: 1`);
        lines.push('hotkey: "ctrl-s" | before: 1');
        lines.push('hotkey: "esc" | before: 1');
        lines.push(
            `command: "tcc ${guestSource} -o ${guestBinary}" | log: "DEBUG > [Spawn] Process completed successfully, exit code: 0" | timeout: 60 | before: 1`,
        );
        lines.push(`command: "${guestBinary}" | log: "${expectedOutput}" | timeout: 30`);
        lines.push(`command: "${guestBinary}" | log: "TEST > [Spawn] Executable finished normally : ${guestBinary}" | timeout: 30`);
    }

    lines.push('command: "shutdown"');

    const commandsFile = path.join(config.ROOT_DIR, 'temp', COMMANDS_FILE_NAME);
    fs.mkdirSync(path.dirname(commandsFile), { recursive: true });
    fs.writeFileSync(commandsFile, lines.join('\n') + '\n');
    config.COMMANDS_FILE = commandsFile;
}

BuildEditSmokeCommandsFile();

SmokeTestMain(process.argv.slice(2)).then((exitCode) => {
    process.exitCode = exitCode;
});
