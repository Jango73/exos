'use strict';

process.env.SMOKE_TEST_REQUIRE_LOCAL_HTTP_SERVER = '1';

const path = require('node:path');
const config = require('../utils/smoke-test/config');
const { SmokeTestMain } = require('../utils/smoke-test/orchestrator');

config.COMMANDS_FILE = path.join(config.ROOT_DIR, 'scripts/common/smoke-test-network-commands.txt');

SmokeTestMain(process.argv.slice(2)).then((exitCode) => {
    process.exitCode = exitCode;
});
