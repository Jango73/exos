'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const config = require('./config');
const { GetLogSize, Sleep } = require('./util');
const { ParseCommandSpec, ExpandCommandsContent } = require('./commands-parser');
const { SendCommandWithMode, SendText, SendHotkey, WaitForMonitor, MonitorCommand } = require('./monitor');
const { WaitForExpectedLog, VerifySpawnCommandLine, AssertNoFailures } = require('./validation');
const { SetImageKeyboardLayout, AssertDownloadedFileSize, AssertDownloadedFileHash } = require('./image');
const {
    WaitForImageReady,
    SpawnDetached,
    WaitForProcessExit,
    StopQemu,
    StopActiveQemuSession,
    StopActiveQemuSessionOnFailureIfNeeded,
} = require('./qemu');
const { EnsureLocalHttpServer, StopLocalHttpServer } = require('./http-server');
const { ArchiveCurrentRunLogs } = require('./log-archive');

function Usage() {
    console.log(`Usage: ${config.SCRIPT_DISPLAY_NAME} [--only <x86-32|x86-32-rtl8139|x86-64-uefi>] [--commands-file <path>] [--no-build] [--stop-after-shell] [--keep-qemu-on-fail] [--no-keyboard-layout-patch] [--hash-compare] [--key-delay <seconds>] [--command-delay <seconds>] [--boot-input-delay <seconds>] [--help]`);
}

function ParseArguments(args) {
    let index = 0;

    while (index < args.length) {
        const argument = args[index];
        switch (argument) {
            case '--only':
                index += 1;
                if (index >= args.length) {
                    console.log('Missing value for --only');
                    Usage();
                    process.exit(1);
                }
                config.RUN_X86_32 = 0;
                config.RUN_X86_32_RTL8139 = 0;
                config.RUN_X86_64_UEFI = 0;
                switch (args[index]) {
                    case 'x86-32':
                        config.RUN_X86_32 = 1;
                        break;
                    case 'x86-32-rtl8139':
                        config.RUN_X86_32_RTL8139 = 1;
                        break;
                    case 'x86-64-uefi':
                        config.RUN_X86_64_UEFI = 1;
                        break;
                    default:
                        console.log(`Invalid --only target: ${args[index]}`);
                        Usage();
                        process.exit(1);
                }
                break;
            case '--help':
            case '-h':
                Usage();
                return 2;
            case '--commands-file':
                index += 1;
                if (index >= args.length) {
                    console.log('Missing value for --commands-file');
                    Usage();
                    process.exit(1);
                }
                config.COMMANDS_FILE = args[index];
                config.COMMANDS_FILE_EXPLICIT = true;
                break;
            case '--key-delay':
                index += 1;
                if (index >= args.length) {
                    console.log('Missing value for --key-delay');
                    Usage();
                    process.exit(1);
                }
                config.KEY_DELAY_SECONDS = Number(args[index]);
                break;
            case '--command-delay':
                index += 1;
                if (index >= args.length) {
                    console.log('Missing value for --command-delay');
                    Usage();
                    process.exit(1);
                }
                config.COMMAND_DELAY_SECONDS = Number(args[index]);
                break;
            case '--boot-input-delay':
                index += 1;
                if (index >= args.length) {
                    console.log('Missing value for --boot-input-delay');
                    Usage();
                    process.exit(1);
                }
                config.BOOT_INPUT_DELAY_SECONDS = Number(args[index]);
                break;
            case '--no-build':
                config.SKIP_BUILD = 1;
                break;
            case '--hash-compare':
                config.ENABLE_HASH_COMPARE = 1;
                break;
            case '--stop-after-shell':
                config.STOP_AFTER_SHELL_READY = 1;
                break;
            case '--keep-qemu-on-fail':
                config.KEEP_QEMU_ON_FAIL = 1;
                break;
            case '--no-keyboard-layout-patch':
                config.PATCH_KEYBOARD_LAYOUT = 0;
                break;
            default:
                console.log(`Unknown option: ${argument}`);
                Usage();
                process.exit(1);
        }
        index += 1;
    }

    return 0;
}

function CommandExists(tool) {
    try {
        execFileSync('which', [tool], { stdio: 'ignore' });
        return true;
    } catch (error) {
        return false;
    }
}

function ValidatePrerequisites() {
    if (!CommandExists('debugfs')) {
        console.log('Missing debugfs. Aborting.');
        process.exit(1);
    }
    if (!CommandExists('mdir')) {
        console.log('Missing mtools (mdir). Aborting.');
        process.exit(1);
    }
}

async function RunCommandSpec(actionType, actionText, expectedText, compareSource, compareDownloaded, timeoutSeconds, preActionDelaySeconds) {
    if (!actionType) {
        throw new Error('Invalid empty action type in command specification.');
    }
    if (!actionText) {
        throw new Error('Invalid empty action in command specification.');
    }

    if (preActionDelaySeconds) {
        await Sleep(parseFloat(preActionDelaySeconds) * 1000);
    }

    const isPipelineCommand = actionText.includes('|');

    let offset = 0;
    console.log(`Running ${actionType}: ${actionText}`);
    if (actionType === 'command') {
        let monitorModeUsed = config.MONITOR_MODE;
        if (config.MONITOR_MODE === 'auto' && config.ACTIVE_MONITOR_MODE === 'persistent') {
            monitorModeUsed = 'persistent';
        } else if (monitorModeUsed === 'auto') {
            monitorModeUsed = 'transient';
        }

        offset = GetLogSize(config.LOG_FILE);
        await SendCommandWithMode(actionText, monitorModeUsed);

        let spawnVerified = 0;
        if (actionText.startsWith('/') && !isPipelineCommand) {
            const waitStatus = await VerifySpawnCommandLine(actionText, offset, config.MONITOR_FALLBACK_TIMEOUT_SECONDS);
            if (waitStatus !== 0) {
                if (config.MONITOR_MODE === 'auto' && monitorModeUsed === 'transient' && waitStatus === 2) {
                    console.log(`Retrying command with persistent monitor connection: ${actionText}`);
                    config.ACTIVE_MONITOR_MODE = 'persistent';
                    offset = GetLogSize(config.LOG_FILE);
                    await SendCommandWithMode(actionText, 'persistent');
                    await VerifySpawnCommandLine(actionText, offset);
                    spawnVerified = 1;
                } else {
                    throw new Error(`Spawn verification failed for command: ${actionText}`);
                }
            } else {
                spawnVerified = 1;
            }
        }

        if (expectedText) {
            let waitStatus;
            if (!isPipelineCommand && config.MONITOR_MODE === 'auto' && monitorModeUsed === 'transient' && spawnVerified === 0) {
                const probeTimeout = timeoutSeconds ? Number(timeoutSeconds) : config.MONITOR_FALLBACK_TIMEOUT_SECONDS;
                waitStatus = await WaitForExpectedLog(expectedText, offset, probeTimeout);
            } else {
                waitStatus = await WaitForExpectedLog(expectedText, offset, timeoutSeconds ? Number(timeoutSeconds) : undefined);
            }
            if (waitStatus !== 0) {
                if (!isPipelineCommand && config.MONITOR_MODE === 'auto' && monitorModeUsed === 'transient' && waitStatus === 2) {
                    console.log(`Retrying command with persistent monitor connection: ${actionText}`);
                    config.ACTIVE_MONITOR_MODE = 'persistent';
                    offset = GetLogSize(config.LOG_FILE);
                    await SendCommandWithMode(actionText, 'persistent');
                    if (actionText.startsWith('/')) {
                        await VerifySpawnCommandLine(actionText, offset);
                    }
                    await WaitForExpectedLog(expectedText, offset, timeoutSeconds ? Number(timeoutSeconds) : undefined);
                } else {
                    throw new Error(`Expected log not found for command: ${actionText}`);
                }
            }
        }
    } else if (actionType === 'hotkey') {
        offset = GetLogSize(config.LOG_FILE);
        await SendHotkey(actionText);
    } else if (actionType === 'type') {
        const sourcePath = path.isAbsolute(actionText) ? actionText : path.join(config.ROOT_DIR, actionText);
        const sourceText = fs.readFileSync(sourcePath, 'utf8');
        offset = GetLogSize(config.LOG_FILE);
        await SendText(sourceText);
    } else {
        throw new Error(`Invalid action type in command specification: ${actionType}`);
    }

    if (actionType !== 'command' && expectedText) {
        await WaitForExpectedLog(expectedText, offset, timeoutSeconds ? Number(timeoutSeconds) : undefined);
    } else {
        await Sleep(500);
    }
    await Sleep(200);

    if (AssertNoFailures(offset) !== 0) {
        throw new Error(`Failures detected after command: ${actionText}`);
    }
    if (compareSource || compareDownloaded) {
        AssertDownloadedFileSize(offset, compareSource, compareDownloaded);
        if (config.ENABLE_HASH_COMPARE === 1) {
            AssertDownloadedFileHash(offset, compareSource, compareDownloaded);
        }
    }
}

async function RunCommandList(commandsFilePath) {
    let resolvedCommandsFile = commandsFilePath || config.COMMANDS_FILE;

    if (!fs.existsSync(resolvedCommandsFile) && !path.isAbsolute(resolvedCommandsFile)) {
        resolvedCommandsFile = path.join(config.ROOT_DIR, resolvedCommandsFile);
    }
    if (!fs.existsSync(resolvedCommandsFile)) {
        throw new Error(`Commands file not found: ${commandsFilePath}`);
    }

    let content = fs.readFileSync(resolvedCommandsFile, 'utf8');
    content = ExpandCommandsContent(content, config.SMOKE_TEST_LOCAL_HTTP_BASE_URL);

    const lines = content.split('\n').map((line) => line.replace(/\r$/, ''));
    for (const line of lines) {
        if (!line.trim() || line.trim().startsWith('#')) {
            continue;
        }
        const spec = ParseCommandSpec(line);
        await RunCommandSpec(
            spec.actionType,
            spec.actionText,
            spec.expectedText,
            spec.compareSource,
            spec.compareDownloaded,
            spec.timeoutSeconds,
            spec.preActionDelaySeconds,
        );
    }
}

function ResolveCommandsFileOverride(target) {
    if (!target.commandsFileOverride) {
        return '';
    }
    const envValue = process.env[target.commandsFileOverride] || '';
    return envValue;
}

async function RunArchitecture(target) {
    const effectiveCommandsFile = (() => {
        if (config.COMMANDS_FILE_EXPLICIT === 0) {
            const override = ResolveCommandsFileOverride(target);
            if (override) {
                return override;
            }
        }
        return config.COMMANDS_FILE;
    })();

    config.SMOKE_TEST_FAILED_TARGET = target.name;

    if (typeof config.OnTargetStart === 'function') {
        config.OnTargetStart(target);
    }

    if (config.SKIP_BUILD === 0) {
        console.log(`Building ${target.name}...`);
        execFileSync('bash', ['-c', `cd "${config.ROOT_DIR}" && ${target.buildScript}`], { stdio: 'inherit' });
        await Sleep(2000);
    } else {
        console.log(`Skipping build for ${target.name} (--no-build)`);
    }

    console.log(`Starting QEMU for ${target.name}...`);
    fs.mkdirSync(path.join(config.ROOT_DIR, 'log'), { recursive: true });
    config.LOG_FILE = path.join(config.ROOT_DIR, target.kernelLog);
    config.CURRENT_IMAGE_PATH = path.join(config.ROOT_DIR, target.image);
    config.CURRENT_FS_OFFSET = target.fileSystemOffset;
    config.CURRENT_ARCHIVE_NAME = target.name;
    config.CURRENT_KERNEL_LOG_PATH = path.join(config.ROOT_DIR, target.kernelLog);
    config.CURRENT_COM1_LOG_PATH = path.join(config.ROOT_DIR, target.kernelLog.replace(/log\/kernel-/, 'log/debug-com1-'));
    config.CURRENT_LOGS_ARCHIVED = 0;

    await WaitForImageReady(config.CURRENT_IMAGE_PATH);
    if (config.PATCH_KEYBOARD_LAYOUT === 1) {
        SetImageKeyboardLayout(config.CURRENT_IMAGE_PATH, config.CURRENT_FS_OFFSET, config.TEST_KEYBOARD_LAYOUT);
    }
    fs.writeFileSync(config.LOG_FILE, '');

    const qemuChild = SpawnDetached(target.qemuScript, config.ROOT_DIR);
    config.ACTIVE_QEMU_SESSION_PID = qemuChild;

    if (!(await WaitForMonitor())) {
        console.error('QEMU monitor did not start.');
        await StopActiveQemuSessionOnFailureIfNeeded();
        throw new Error('QEMU monitor did not start.');
    }

    await Sleep(2000);
    await Sleep(config.BOOT_INPUT_DELAY_SECONDS * 1000);
    AssertNoFailures(0);

    const bootStatus = await WaitForExpectedLog(config.BOOT_READY_PATTERN, 0, config.BOOT_READY_TIMEOUT_SECONDS);
    if (bootStatus !== 0) {
        await StopActiveQemuSessionOnFailureIfNeeded();
        throw new Error(`Boot ready log not detected for ${target.name}.`);
    }

    if (config.STOP_AFTER_SHELL_READY === 1) {
        console.log('Shell ready detected, stopping early (--stop-after-shell).');
        await StopQemu();
        if (!(await WaitForProcessExit(qemuChild, 20))) {
            await StopActiveQemuSessionOnFailureIfNeeded();
            throw new Error('Timed out waiting for QEMU shutdown after shell-ready stop.');
        }
        config.ACTIVE_QEMU_SESSION_PID = '';
        ArchiveCurrentRunLogs('pass');
        config.SMOKE_TEST_FAILED_TARGET = '';
        return;
    }

    await RunCommandList(effectiveCommandsFile);
    AssertNoFailures(0);

    if (!(await WaitForProcessExit(qemuChild, 20))) {
        await StopActiveQemuSessionOnFailureIfNeeded();
        throw new Error('Timed out waiting for QEMU shutdown from shell command.');
    }
    config.ACTIVE_QEMU_SESSION_PID = '';
    ArchiveCurrentRunLogs('pass');
    config.SMOKE_TEST_FAILED_TARGET = '';
}

async function OnScriptExit(exitCode) {
    if (exitCode !== 0) {
        ArchiveCurrentRunLogs('fail');
        await StopActiveQemuSessionOnFailureIfNeeded();
    } else {
        await StopActiveQemuSession();
    }
    StopLocalHttpServer();

    if (config.SMOKE_TEST_SUMMARY_ENABLED === 1) {
        if (exitCode === 0) {
            console.log('Smoke test completed successfully.');
        } else if (config.SMOKE_TEST_FAILED_TARGET) {
            console.log(`Smoke test failed on target: ${config.SMOKE_TEST_FAILED_TARGET}`);
        } else {
            console.log('Smoke test failed.');
        }
    }
}

async function SmokeTestMain(args) {
    const parseResult = ParseArguments(args);
    if (parseResult === 2) {
        return 0;
    }

    ValidatePrerequisites();
    config.SMOKE_TEST_SUMMARY_ENABLED = 1;

    let exitCode = 0;
    try {
        await EnsureLocalHttpServer();

        for (const target of config.TARGETS) {
            if (config[target.enabled] === 1) {
                await RunArchitecture(target);
            }
        }

        if (typeof config.PostMainHook === 'function') {
            await config.PostMainHook();
        }
    } catch (error) {
        exitCode = 1;
        console.error(error.message);
    } finally {
        await OnScriptExit(exitCode);
    }

    return exitCode;
}

module.exports = {
    Usage,
    ParseArguments,
    ValidatePrerequisites,
    RunCommandSpec,
    RunCommandList,
    RunArchitecture,
    OnScriptExit,
    SmokeTestMain,
};
