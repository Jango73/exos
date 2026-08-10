'use strict';

const path = require('node:path');
const config = require('../utils/smoke-test/config');
const { SmokeTestMain } = require('../utils/smoke-test/orchestrator');
const { ValidateTinyCcOutputsForTarget } = require('../utils/smoke-test/tcc-validate');

config.COMMANDS_FILE = path.join(config.ROOT_DIR, 'scripts/common/smoke-test-tcc-commands.txt');
config.RUN_X86_32_RTL8139 = 0;

config.PostMainHook = async () => {
    if (config.RUN_X86_32 === 1) {
        ValidateTinyCcOutputsForTarget(
            'x86-32',
            path.join(config.ROOT_DIR, 'build/image/x86-32-mbr-debug-ext2/exos.img'),
            1048576,
            'ELF32',
            'Intel 80386',
        );
    }

    if (config.RUN_X86_64_UEFI === 1) {
        ValidateTinyCcOutputsForTarget(
            'x86-64 UEFI',
            path.join(config.ROOT_DIR, 'build/image/x86-64-uefi-debug-ext2/exos-uefi.img'),
            4194304,
            'ELF64',
            'Advanced Micro Devices X86-64',
        );
    }
};

SmokeTestMain(process.argv.slice(2)).then((exitCode) => {
    process.exitCode = exitCode;
});
