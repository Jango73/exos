'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const config = require('./config');
const { ComputeSha256 } = require('./util');

function MakeTempFile() {
    return path.join(config.ROOT_DIR, 'temp', `exos-smoke-${process.pid}-${Date.now()}-${Math.random().toString(36).slice(2)}`);
}

function RunCommand(binary, args, options) {
    return execFileSync(binary, args, {
        encoding: 'utf8',
        stdio: ['ignore', 'pipe', 'ignore'],
        ...options,
    });
}

function ExtractPartitionSync(imagePath, fileSystemOffset) {
    const partitionImage = MakeTempFile();
    const offsetMegabytes = Math.floor(fileSystemOffset / 1048576);
    const offsetRemainder = fileSystemOffset % 1048576;

    try {
        RunCommand('dd', ['if=' + imagePath, 'of=' + partitionImage, 'iflag=skip_bytes', `skip=${fileSystemOffset}`, 'bs=1M', 'status=none']);
    } catch (error) {
        if (offsetRemainder === 0) {
            RunCommand('dd', ['if=' + imagePath, 'of=' + partitionImage, 'bs=1M', `skip=${offsetMegabytes}`, 'status=none']);
        } else {
            RunCommand('dd', ['if=' + imagePath, 'of=' + partitionImage, 'bs=1', `skip=${fileSystemOffset}`, 'status=none']);
        }
    }

    return partitionImage;
}

function RestorePartitionSync(imagePath, partitionImage, fileSystemOffset) {
    const offsetMegabytes = Math.floor(fileSystemOffset / 1048576);
    const offsetRemainder = fileSystemOffset % 1048576;

    if (offsetRemainder === 0) {
        RunCommand('dd', ['if=' + partitionImage, 'of=' + imagePath, 'bs=1M', `seek=${offsetMegabytes}`, 'conv=notrunc', 'status=none']);
    } else {
        RunCommand('dd', ['if=' + partitionImage, 'of=' + imagePath, 'bs=1', `seek=${fileSystemOffset}`, 'conv=notrunc', 'status=none']);
    }
}

function DebugfsCat(partitionImage, guestPath) {
    return RunCommand('debugfs', ['-R', `cat ${guestPath}`, partitionImage]);
}

function DebugfsWrite(partitionImage, hostPath, guestPath) {
    RunCommand('debugfs', ['-w', '-R', `write ${hostPath} ${guestPath}`, partitionImage]);
}

function DebugfsRemove(partitionImage, guestPath) {
    try {
        RunCommand('debugfs', ['-w', '-R', `rm ${guestPath}`, partitionImage]);
    } catch (error) {
        // Removal is best effort.
    }
}

function DebugfsDump(partitionImage, guestPath, hostPath) {
    RunCommand('debugfs', ['-R', `dump ${guestPath} ${hostPath}`, partitionImage]);
}

function DebugfsStatSize(partitionImage, guestPath) {
    try {
        const output = RunCommand('debugfs', ['-R', `stat ${guestPath}`, partitionImage]);
        const match = /.*Size:[ \t]*([0-9][0-9]*).*/.exec(output);
        return match ? match[1] : '';
    } catch (error) {
        return '';
    }
}

function PatchExosToml(content, layout) {
    // Force a deterministic keyboard layout and disable login directly in exos.toml.
    let inKeyboard = 0;
    let inGeneral = 0;
    let layoutSet = 0;
    let doLoginSet = 0;
    const output = [];

    const isGeneralHeader = (line) => /^\[General\]/.test(line);
    const isKeyboardHeader = (line) => /^\[Keyboard\]/.test(line);
    const isAnySectionHeader = (line) => /^\[/.test(line);
    const isDoLoginLine = (line) => inGeneral === 1 && /^DoLogin[ \t]*=/.test(line);
    const isLayoutLine = (line) => inKeyboard === 1 && /^Layout[ \t]*=/.test(line);

    for (const line of content.split('\n')) {
        if (isGeneralHeader(line)) {
            inGeneral = 1;
            inKeyboard = 0;
            output.push(line);
            continue;
        }

        if (isKeyboardHeader(line)) {
            inKeyboard = 1;
            if (inGeneral === 1 && doLoginSet === 0) {
                output.push(config.GENERAL_DO_LOGIN_DISABLED_LINE);
                doLoginSet = 1;
            }
            inGeneral = 0;
            output.push(line);
            continue;
        }

        if (isAnySectionHeader(line)) {
            if (inGeneral === 1 && doLoginSet === 0) {
                output.push(config.GENERAL_DO_LOGIN_DISABLED_LINE);
                doLoginSet = 1;
            }
            if (inKeyboard === 1 && layoutSet === 0) {
                output.push(`${config.KEYBOARD_LAYOUT_KEY}="${layout}"`);
                layoutSet = 1;
            }
            inGeneral = 0;
            inKeyboard = 0;
            output.push(line);
            continue;
        }

        if (isDoLoginLine(line)) {
            if (doLoginSet === 0) {
                output.push(config.GENERAL_DO_LOGIN_DISABLED_LINE);
                doLoginSet = 1;
            }
            continue;
        }

        if (isLayoutLine(line)) {
            if (layoutSet === 0) {
                output.push(`${config.KEYBOARD_LAYOUT_KEY}="${layout}"`);
                layoutSet = 1;
            }
            continue;
        }

        output.push(line);
    }

    if (inGeneral === 1 && doLoginSet === 0) {
        output.push(config.GENERAL_DO_LOGIN_DISABLED_LINE);
    }
    if (inKeyboard === 1 && layoutSet === 0) {
        output.push(`${config.KEYBOARD_LAYOUT_KEY}="${layout}"`);
    }

    return output.join('\n');
}

function SetImageKeyboardLayout(imagePath, fileSystemOffset, layout) {
    if (!fs.existsSync(imagePath)) {
        throw new Error(`Image not found for keyboard layout patch: ${imagePath}`);
    }

    const partitionImage = ExtractPartitionSync(imagePath, fileSystemOffset);
    const configFile = MakeTempFile();
    const patchedConfigFile = MakeTempFile();

    try {
        let content;
        try {
            content = DebugfsCat(partitionImage, '/exos.toml');
        } catch (error) {
            throw new Error(`Could not read /exos.toml from image: ${imagePath}`);
        }

        fs.writeFileSync(configFile, content);
        fs.writeFileSync(patchedConfigFile, PatchExosToml(content, layout));

        DebugfsRemove(partitionImage, '/exos.toml');
        try {
            DebugfsWrite(partitionImage, patchedConfigFile, '/exos.toml');
        } catch (error) {
            throw new Error(`Could not write patched /exos.toml into image: ${imagePath}`);
        }

        RestorePartitionSync(imagePath, partitionImage, fileSystemOffset);

        const verification = DebugfsCat(partitionImage, '/exos.toml');
        if (!config.KEYBOARD_LAYOUT_PATTERN.test(verification)) {
            throw new Error(`Keyboard layout verification failed for image: ${imagePath}`);
        }
        if (!config.GENERAL_DO_LOGIN_DISABLED_PATTERN.test(verification)) {
            throw new Error(`DoLogin patch verification failed for image: ${imagePath}`);
        }
    } finally {
        fs.rmSync(partitionImage, { force: true });
        fs.rmSync(configFile, { force: true });
        fs.rmSync(patchedConfigFile, { force: true });
    }
}

function SetImagePortalAutoRun(imagePath, fileSystemOffset) {
    // Ensure /system/apps/portal is auto-run so the desktop boots by itself.
    const partitionImage = ExtractPartitionSync(imagePath, fileSystemOffset);
    const configFile = MakeTempFile();
    const patchedConfigFile = MakeTempFile();

    try {
        let content;
        try {
            content = DebugfsCat(partitionImage, '/exos.toml');
        } catch (error) {
            throw new Error(`Could not read /exos.toml from image: ${imagePath}`);
        }

        let hasPortal = 0;
        const output = [];
        for (const line of content.split('\n')) {
            output.push(line);
            if (/^Command[ \t]*=[ \t]*"\/system\/apps\/portal"$/.test(line)) {
                hasPortal = 1;
            }
        }
        if (hasPortal === 0) {
            output.push('');
            output.push('[[Run]]');
            output.push('Command="/system/apps/portal"');
        }

        fs.writeFileSync(configFile, content);
        fs.writeFileSync(patchedConfigFile, output.join('\n'));

        DebugfsRemove(partitionImage, '/exos.toml');
        try {
            DebugfsWrite(partitionImage, patchedConfigFile, '/exos.toml');
        } catch (error) {
            throw new Error(`Could not write patched /exos.toml into image: ${imagePath}`);
        }

        RestorePartitionSync(imagePath, partitionImage, fileSystemOffset);

        const verification = DebugfsCat(partitionImage, '/exos.toml');
        if (!verification.includes('Command="/system/apps/portal"')) {
            throw new Error(`Portal auto-run verification failed for image: ${imagePath}`);
        }
    } finally {
        fs.rmSync(partitionImage, { force: true });
        fs.rmSync(configFile, { force: true });
        fs.rmSync(patchedConfigFile, { force: true });
    }
}

function ResolveSourcePath(sourcePath) {
    if (path.isAbsolute(sourcePath)) {
        return sourcePath;
    }
    return path.join(config.ROOT_DIR, sourcePath);
}

function ResolveDownloadedPath(downloadedName) {
    if (path.isAbsolute(downloadedName)) {
        return downloadedName;
    }
    return '/' + downloadedName;
}

function AssertDownloadedFileSize(offset, sourcePath, downloadedName) {
    const resolvedSourcePath = ResolveSourcePath(sourcePath);
    if (!fs.existsSync(resolvedSourcePath)) {
        throw new Error(`Source file not found for size compare: ${resolvedSourcePath}`);
    }
    if (!config.CURRENT_IMAGE_PATH || !fs.existsSync(config.CURRENT_IMAGE_PATH)) {
        throw new Error(`Guest disk image not available for size compare: ${config.CURRENT_IMAGE_PATH}`);
    }
    if (!downloadedName) {
        throw new Error('Missing downloaded file name in file-size-compare.');
    }

    const downloadedPath = ResolveDownloadedPath(downloadedName);
    const sourceSize = fs.statSync(resolvedSourcePath).size;

    const partitionImage = ExtractPartitionSync(config.CURRENT_IMAGE_PATH, config.CURRENT_FS_OFFSET);
    try {
        const downloadedSize = DebugfsStatSize(partitionImage, downloadedPath);
        if (!downloadedSize) {
            throw new Error(`Could not read downloaded file size from guest image: ${downloadedPath}`);
        }
        if (sourceSize !== Number(downloadedSize)) {
            throw new Error(`Downloaded size mismatch for ${downloadedName}: expected ${sourceSize} got ${downloadedSize}`);
        }
    } finally {
        fs.rmSync(partitionImage, { force: true });
    }
}

function AssertDownloadedFileHash(offset, sourcePath, downloadedName) {
    const resolvedSourcePath = ResolveSourcePath(sourcePath);
    if (!fs.existsSync(resolvedSourcePath)) {
        throw new Error(`Source file not found for hash compare: ${resolvedSourcePath}`);
    }
    if (!config.CURRENT_IMAGE_PATH || !fs.existsSync(config.CURRENT_IMAGE_PATH)) {
        throw new Error(`Guest disk image not available for hash compare: ${config.CURRENT_IMAGE_PATH}`);
    }
    if (!downloadedName) {
        throw new Error('Missing downloaded file name in hash compare.');
    }

    const downloadedPath = ResolveDownloadedPath(downloadedName);
    const partitionImage = ExtractPartitionSync(config.CURRENT_IMAGE_PATH, config.CURRENT_FS_OFFSET);
    const guestTemp = MakeTempFile();

    try {
        try {
            DebugfsDump(partitionImage, downloadedPath, guestTemp);
        } catch (error) {
            throw new Error(`Could not extract downloaded file from guest image: ${downloadedPath}`);
        }

        const sourceHash = ComputeSha256(resolvedSourcePath);
        const guestHash = ComputeSha256(guestTemp);
        if (!sourceHash || !guestHash) {
            throw new Error(`Hash calculation failed for ${downloadedName}`);
        }
        if (sourceHash !== guestHash) {
            throw new Error(`Downloaded hash mismatch for ${downloadedName}: expected ${sourceHash} got ${guestHash}`);
        }
    } finally {
        fs.rmSync(partitionImage, { force: true });
        fs.rmSync(guestTemp, { force: true });
    }
}

module.exports = {
    ExtractPartitionSync,
    RestorePartitionSync,
    DebugfsCat,
    DebugfsWrite,
    DebugfsRemove,
    DebugfsDump,
    DebugfsStatSize,
    PatchExosToml,
    SetImageKeyboardLayout,
    SetImagePortalAutoRun,
    AssertDownloadedFileSize,
    AssertDownloadedFileHash,
};
