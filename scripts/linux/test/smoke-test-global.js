'use strict';

process.env.SMOKE_TEST_REQUIRE_LOCAL_HTTP_SERVER = '1';

const path = require('node:path');
const config = require('../utils/smoke-test/config');
const { SmokeTestMain } = require('../utils/smoke-test/orchestrator');

process.env.SMOKE_TEST_X86_32_RTL8139_COMMANDS_FILE = path.join(config.ROOT_DIR, 'scripts/common/smoke-test-global-rtl8139-commands.txt');

SmokeTestMain(process.argv.slice(2)).then((exitCode) => {
    process.exitCode = exitCode;
});
