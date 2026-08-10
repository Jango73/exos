'use strict';

const path = require('node:path');
const config = require('../utils/smoke-test/config');
const { SmokeTestMain } = require('../utils/smoke-test/orchestrator');

config.COMMANDS_FILE = path.join(config.ROOT_DIR, 'scripts/common/smoke-test-vesa-commands.txt');
config.RUN_X86_32_RTL8139 = 0;
config.RUN_X86_64_UEFI = 0;

SmokeTestMain(process.argv.slice(2)).then((exitCode) => {
    process.exitCode = exitCode;
});
