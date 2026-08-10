'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { ExtractPartitionSync, DebugfsDump } = require('./image');

function ExtractTinyCcOutputFile(imagePath, fileSystemOffset, guestPath) {
    const partitionImage = ExtractPartitionSync(imagePath, fileSystemOffset);
    const hostPath = path.join(require('node:os').tmpdir(), `exos-tinycc-${process.pid}-${Date.now()}-${Math.random().toString(36).slice(2)}`);

    try {
        DebugfsDump(partitionImage, guestPath, hostPath);
    } catch (error) {
        fs.rmSync(partitionImage, { force: true });
        throw new Error(`TinyCC output file was not created in the guest image: ${guestPath}`);
    }
    fs.rmSync(partitionImage, { force: true });

    if (!fs.existsSync(hostPath) || fs.statSync(hostPath).size === 0) {
        fs.rmSync(hostPath, { force: true });
        throw new Error(`TinyCC output file was not created in the guest image: ${guestPath}`);
    }

    return hostPath;
}

function ValidateElfHeaders(buffer) {
    if (buffer.length < 52 || buffer.readUInt32LE(0) !== 0x7f454c46) {
        throw new Error('TinyCC output executable is not a valid ELF file.');
    }

    const elfClass = buffer[4];
    const littleEndian = buffer[5] === 1;
    const eType = littleEndian ? buffer.readUInt16LE(16) : buffer.readUInt16BE(16);
    const eMachine = littleEndian ? buffer.readUInt16LE(18) : buffer.readUInt16BE(18);

    let ePhoff;
    let ePhentsize;
    let ePhnum;
    if (elfClass === 1) {
        ePhoff = littleEndian ? buffer.readUInt32LE(28) : buffer.readUInt32BE(28);
        ePhentsize = littleEndian ? buffer.readUInt16LE(42) : buffer.readUInt16BE(42);
        ePhnum = littleEndian ? buffer.readUInt16LE(44) : buffer.readUInt16BE(44);
    } else if (elfClass === 2) {
        ePhoff = Number(littleEndian ? buffer.readBigUInt64LE(32) : buffer.readBigUInt64BE(32));
        ePhentsize = littleEndian ? buffer.readUInt16LE(54) : buffer.readUInt16BE(54);
        ePhnum = littleEndian ? buffer.readUInt16LE(56) : buffer.readUInt16BE(56);
    } else {
        throw new Error('TinyCC output executable has an invalid ELF class.');
    }

    return {
        elfClass,
        littleEndian,
        eType,
        eMachine,
        ePhoff,
        ePhentsize,
        ePhnum,
    };
}

function ValidateTinyCcExecutable(imagePath, fileSystemOffset, guestPath, expectedElfClass, expectedMachine) {
    const hostPath = ExtractTinyCcOutputFile(imagePath, fileSystemOffset, guestPath);
    const buffer = fs.readFileSync(hostPath);
    fs.rmSync(hostPath, { force: true });

    const header = ValidateElfHeaders(buffer);

    const expectedClassNumber = expectedElfClass === 'ELF32' ? 1 : 2;
    if (header.elfClass !== expectedClassNumber) {
        throw new Error(`TinyCC output executable is not ${expectedElfClass}.`);
    }
    if (header.eType !== 2) {
        throw new Error('TinyCC output executable is not ET_EXEC.');
    }

    const machineNames = {
        3: 'Intel 80386',
        62: 'Advanced Micro Devices X86-64',
    };
    if (machineNames[header.eMachine] !== expectedMachine) {
        throw new Error(`TinyCC output executable machine is not ${expectedMachine}.`);
    }

    let hasLoad = 0;
    let hasInterp = 0;
    for (let index = 0; index < header.ePhnum; index++) {
        const phOffset = header.ePhoff + index * header.ePhentsize;
        if (phOffset + 8 > buffer.length) {
            break;
        }
        const pType = header.littleEndian ? buffer.readUInt32LE(phOffset) : buffer.readUInt32BE(phOffset);
        if (pType === 1) {
            hasLoad = 1;
        } else if (pType === 3) {
            hasInterp = 1;
        }
    }

    if (hasLoad === 0) {
        throw new Error('TinyCC output executable has no PT_LOAD segment.');
    }
    if (hasInterp === 1) {
        throw new Error('TinyCC output executable unexpectedly has a PT_INTERP segment.');
    }
}

function ValidateTinyCcArchive(imagePath, fileSystemOffset) {
    const hostPath = ExtractTinyCcOutputFile(imagePath, fileSystemOffset, '/exos/apps/tcc/libworkflow.a');
    const buffer = fs.readFileSync(hostPath);
    fs.rmSync(hostPath, { force: true });

    const arMagic = Buffer.from('!<arch>\n', 'ascii');
    if (buffer.length < 8 || !buffer.subarray(0, 8).equals(arMagic)) {
        throw new Error('TinyCC output archive does not use the ar format.');
    }

    const archiveText = buffer.toString('latin1');
    if (!archiveText.includes('WorkflowCalculateChecksum') || !archiveText.includes('WorkflowScaleValue')) {
        throw new Error('TinyCC output archive is missing workflow symbols.');
    }
}

function ValidateTinyCcMissingOutput(imagePath, fileSystemOffset, guestPath) {
    let hostPath = '';
    try {
        hostPath = ExtractTinyCcOutputFile(imagePath, fileSystemOffset, guestPath);
        fs.rmSync(hostPath, { force: true });
        throw new Error(`TinyCC failure-path output file was unexpectedly created: ${guestPath}`);
    } catch (error) {
        if (hostPath) {
            fs.rmSync(hostPath, { force: true });
        }
        if (error.message.startsWith('TinyCC failure-path output file')) {
            throw error;
        }
    }
}

function ValidateTinyCcOutputsForTarget(targetName, imagePath, fileSystemOffset, elfClass, machine) {
    console.log(`Validating TinyCC generated files for ${targetName}`);

    ValidateTinyCcExecutable(imagePath, fileSystemOffset, '/exos/apps/tcc/hello', elfClass, machine);
    ValidateTinyCcExecutable(imagePath, fileSystemOffset, '/exos/apps/tcc/workflow', elfClass, machine);
    ValidateTinyCcExecutable(imagePath, fileSystemOffset, '/exos/apps/tcc/source-warning', elfClass, machine);
    ValidateTinyCcArchive(imagePath, fileSystemOffset);
    ValidateTinyCcMissingOutput(imagePath, fileSystemOffset, '/exos/apps/tcc/source-too-large');
    ValidateTinyCcMissingOutput(imagePath, fileSystemOffset, '/exos/apps/tcc/heap-limited');
}

module.exports = {
    ValidateElfHeaders,
    ValidateTinyCcOutputsForTarget,
};
