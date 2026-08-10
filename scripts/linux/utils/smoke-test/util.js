'use strict';

const crypto = require('node:crypto');
const fs = require('node:fs');
const { execFileSync } = require('node:child_process');

function Trim(value) {
    return String(value).replace(/^\s+/, '').replace(/\s+$/, '');
}

function NormalizeSpaces(value) {
    return Trim(String(value).replace(/\t/g, ' ').replace(/[ ]+/g, ' '));
}

function GetShortCommitId(rootDir) {
    try {
        return execFileSync('git', ['-C', rootDir, 'rev-parse', '--short', 'HEAD'], {
            encoding: 'utf8',
            stdio: ['ignore', 'pipe', 'ignore'],
        }).trim();
    } catch (error) {
        return 'unknown';
    }
}

function GetLogSize(logFile) {
    try {
        return fs.statSync(logFile).size;
    } catch (error) {
        return 0;
    }
}

function TailFromOffset(logFile, offset) {
    try {
        const fd = fs.openSync(logFile, 'r');
        const size = fs.fstatSync(fd).size;
        if (offset >= size) {
            fs.closeSync(fd);
            return '';
        }
        const length = size - offset;
        const buffer = Buffer.alloc(length);
        fs.readSync(fd, buffer, 0, length, offset);
        fs.closeSync(fd);
        return buffer.toString('utf8');
    } catch (error) {
        return '';
    }
}

function ComputeSha256(filePath) {
    const buffer = fs.readFileSync(filePath);
    return crypto.createHash('sha256').update(buffer).digest('hex');
}

function SearchRegex(text, pattern) {
    const regex = pattern instanceof RegExp ? pattern : new RegExp(pattern);
    const matches = [];
    const lines = String(text).split('\n');
    for (let index = 0; index < lines.length; index++) {
        if (regex.test(lines[index])) {
            matches.push({ index: index + 1, text: lines[index] });
        }
    }
    return matches;
}

function SearchFixed(text, needle) {
    const matches = [];
    const lines = String(text).split('\n');
    for (let index = 0; index < lines.length; index++) {
        if (lines[index].includes(needle)) {
            matches.push({ index: index + 1, text: lines[index] });
        }
    }
    return matches;
}

function Sleep(milliseconds) {
    return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

module.exports = {
    Trim,
    NormalizeSpaces,
    GetShortCommitId,
    GetLogSize,
    TailFromOffset,
    ComputeSha256,
    SearchRegex,
    SearchFixed,
    Sleep,
};
