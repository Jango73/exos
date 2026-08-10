'use strict';

const path = require('node:path');

const ROOT_DIR = process.env.SMOKE_TEST_ROOT_DIR || path.resolve(__dirname, '../../../../');

const config = {
    ROOT_DIR,
    LOG_FILE: path.join(ROOT_DIR, 'log', 'kernel.log'),
    COMMANDS_FILE: process.env.SMOKE_TEST_DEFAULT_COMMANDS_FILE || path.join(ROOT_DIR, 'scripts/common', 'smoke-test-global-commands.txt'),
    COMMANDS_FILE_EXPLICIT: false,
    LOCAL_HTTP_SERVER_SCRIPT: process.env.SMOKE_TEST_LOCAL_HTTP_SERVER_SCRIPT || path.join(ROOT_DIR, 'scripts', 'linux', 'net', 'start-server.sh'),
    LOCAL_HTTP_SERVER_PORT: Number(process.env.LOCAL_HTTP_SERVER_PORT || 8081),
    SMOKE_TEST_LOCAL_HTTP_BASE_URL: '',
    SMOKE_TEST_REQUIRE_LOCAL_HTTP_SERVER: process.env.SMOKE_TEST_REQUIRE_LOCAL_HTTP_SERVER === '1' ? 1 : 0,
    SKIP_LOCAL_HTTP_SERVER: 1,
    MONITOR_HOST: '127.0.0.1',
    MONITOR_PORT: Number(process.env.MONITOR_PORT || 4444),
    MONITOR_MODE: process.env.SMOKE_TEST_MONITOR_MODE || 'auto',
    MONITOR_CONNECT_MAX_ATTEMPTS: 50,
    DEFAULT_TIMEOUT_SECONDS: 15,
    BOOT_READY_TIMEOUT_SECONDS: 45,
    COMMAND_FORMATION_TIMEOUT_SECONDS: 45,
    MONITOR_FALLBACK_TIMEOUT_SECONDS: 3,
    MONITOR_COMMAND_HOLD_OPEN_SECONDS: 0.1,
    KEY_DELAY_SECONDS: 0.16,
    TYPE_KEY_DELAY_SECONDS: 0.16,
    COMMAND_DELAY_SECONDS: 0.25,
    BOOT_INPUT_DELAY_SECONDS: 1.0,
    IMAGE_READY_TIMEOUT_SECONDS: 15,
    IMAGE_READY_POLL_SECONDS: 0.5,
    IMAGE_READY_STABLE_POLLS: 3,
    TEST_KEYBOARD_LAYOUT: 'en-US',
    GENERAL_DO_LOGIN_DISABLED_LINE: 'DoLogin=0',
    KEYBOARD_LAYOUT_KEY: 'Layout',
    KEYBOARD_LAYOUT_PATTERN: /^Layout="/m,
    GENERAL_DO_LOGIN_DISABLED_PATTERN: /^DoLogin=0$/m,
    BOOT_READY_PATTERN: '[InitializeKernel] Shell task created',
    FAULT_PATTERN: /#PF|#GP|#UD|#SS|#NP|#TS|#DE|#DF|#MF|#AC|#MC/,
    TEST_KO_PATTERN: /TEST > .* : KO/,
    ERROR_PATTERN: 'ERROR >',
    NON_FATAL_ERROR_PATTERN: /^ERROR > \[NVMeAttach\] Failed to allocate admin queues$/,
    AUTOTEST_ERROR_SCOPE_BEGIN: 'AUTOTEST_ERROR_SCOPE_BEGIN',
    AUTOTEST_ERROR_SCOPE_END: 'AUTOTEST_ERROR_SCOPE_END',
    RUN_X86_32: 1,
    RUN_X86_32_RTL8139: 1,
    RUN_X86_64_UEFI: 1,
    SKIP_BUILD: 0,
    STOP_AFTER_SHELL_READY: 0,
    ENABLE_HASH_COMPARE: 0,
    KEEP_QEMU_ON_FAIL: 0,
    PATCH_KEYBOARD_LAYOUT: 1,
    CURRENT_IMAGE_PATH: '',
    CURRENT_FS_OFFSET: 0,
    CURRENT_ARCHIVE_NAME: '',
    CURRENT_KERNEL_LOG_PATH: '',
    CURRENT_COM1_LOG_PATH: '',
    CURRENT_LOGS_ARCHIVED: 0,
    SCRIPT_DISPLAY_NAME: process.env.SMOKE_TEST_SCRIPT_NAME || process.argv[1],
    SMOKE_TEST_SUMMARY_ENABLED: 0,
    SMOKE_TEST_FAILED_TARGET: '',
    ACTIVE_MONITOR_MODE: '',
    ACTIVE_QEMU_SESSION_PID: '',
    LOCAL_HTTP_SERVER_PID: '',
};

config.SMOKE_TEST_LOCAL_HTTP_BASE_URL = `http://10.0.2.2:${config.LOCAL_HTTP_SERVER_PORT}`;

const BUILD_X86_32 = "scripts/linux/build/build.sh --arch x86-32 --fs ext2 --debug --clean --kernel-log-tag-filter ''";
const BUILD_X86_64_UEFI = "scripts/linux/build/build.sh --arch x86-64 --fs ext2 --debug --clean --uefi --kernel-log-tag-filter ''";

config.TARGETS = [
    {
        name: 'x86-32',
        enabled: 'RUN_X86_32',
        buildScript: BUILD_X86_32,
        qemuScript: 'scripts/linux/run/run.sh --arch x86-32 --fs ext2 --debug',
        kernelLog: 'log/kernel-x86-32-mbr-debug.log',
        image: 'build/image/x86-32-mbr-debug-ext2/exos.img',
        fileSystemOffset: 1048576,
        commandsFileOverride: '',
    },
    {
        name: 'x86-32 rtl8139',
        enabled: 'RUN_X86_32_RTL8139',
        buildScript: BUILD_X86_32,
        qemuScript: 'scripts/linux/run/run.sh --arch x86-32 --fs ext2 --debug --net-card rtl8139',
        kernelLog: 'log/kernel-x86-32-mbr-debug.log',
        image: 'build/image/x86-32-mbr-debug-ext2/exos.img',
        fileSystemOffset: 1048576,
        commandsFileOverride: 'SMOKE_TEST_X86_32_RTL8139_COMMANDS_FILE',
    },
    {
        name: 'x86-64 UEFI',
        enabled: 'RUN_X86_64_UEFI',
        buildScript: BUILD_X86_64_UEFI,
        qemuScript: 'scripts/linux/run/run.sh --arch x86-64 --fs ext2 --debug --uefi',
        kernelLog: 'log/kernel-x86-64-uefi-debug.log',
        image: 'build/image/x86-64-uefi-debug-ext2/exos-uefi.img',
        fileSystemOffset: 4194304,
        commandsFileOverride: '',
    },
];

function InitLocalHttpServerSetting() {
    if (config.SMOKE_TEST_REQUIRE_LOCAL_HTTP_SERVER === 1) {
        config.SKIP_LOCAL_HTTP_SERVER = Number(process.env.SKIP_LOCAL_HTTP_SERVER || 0);
    } else {
        config.SKIP_LOCAL_HTTP_SERVER = Number(process.env.SKIP_LOCAL_HTTP_SERVER || 1);
    }
}

InitLocalHttpServerSetting();

module.exports = config;
