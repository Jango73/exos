'use strict';

const fs = require('node:fs');
const path = require('node:path');
const config = require('./config');
const { GetShortCommitId } = require('./util');

function FormatTimestamp() {
    const date = new Date();
    const pad = (value) => String(value).padStart(2, '0');
    return (
        `${date.getFullYear()}${pad(date.getMonth() + 1)}${pad(date.getDate())}` +
        `-${pad(date.getHours())}${pad(date.getMinutes())}${pad(date.getSeconds())}`
    );
}

function ArchiveCurrentRunLogs(status) {
    if (config.CURRENT_LOGS_ARCHIVED === 1) {
        return;
    }
    if (!config.CURRENT_ARCHIVE_NAME || !config.CURRENT_KERNEL_LOG_PATH) {
        return;
    }

    const timestamp = FormatTimestamp();
    const archiveDir = path.join(config.ROOT_DIR, 'log', 'archive');
    const safeName = config.CURRENT_ARCHIVE_NAME.split(' ').join('-');
    const shortCommitId = GetShortCommitId(config.ROOT_DIR);
    fs.mkdirSync(archiveDir, { recursive: true });

    if (fs.existsSync(config.CURRENT_KERNEL_LOG_PATH)) {
        const kernelArchivePath = path.join(archiveDir, `${timestamp}-${safeName}-${shortCommitId}-${status}-kernel.log`);
        fs.copyFileSync(config.CURRENT_KERNEL_LOG_PATH, kernelArchivePath);
        console.log(`Archived kernel log: ${kernelArchivePath}`);
    }
    if (config.CURRENT_COM1_LOG_PATH && fs.existsSync(config.CURRENT_COM1_LOG_PATH)) {
        const com1ArchivePath = path.join(archiveDir, `${timestamp}-${safeName}-${shortCommitId}-${status}-com1.log`);
        fs.copyFileSync(config.CURRENT_COM1_LOG_PATH, com1ArchivePath);
        console.log(`Archived com1 log: ${com1ArchivePath}`);
    }

    config.CURRENT_LOGS_ARCHIVED = 1;
}

module.exports = {
    ArchiveCurrentRunLogs,
};
