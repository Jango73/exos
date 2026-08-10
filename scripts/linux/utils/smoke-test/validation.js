'use strict';

const config = require('./config');
const { TailFromOffset, SearchRegex, SearchFixed, NormalizeSpaces, Sleep } = require('./util');

function TailFromOffsetForErrorCheck(offset) {
    // Ignore ERROR lines emitted while autotests are running.
    const text = TailFromOffset(config.LOG_FILE, offset);
    const lines = text.split('\n');
    const output = [];
    let inAutotestScope = 0;

    for (const line of lines) {
        if (line.includes(config.AUTOTEST_ERROR_SCOPE_BEGIN)) {
            inAutotestScope = 1;
            output.push(line);
            continue;
        }
        if (line.includes(config.AUTOTEST_ERROR_SCOPE_END)) {
            inAutotestScope = 0;
            output.push(line);
            continue;
        }
        if (inAutotestScope === 1 && line.includes(config.ERROR_PATTERN)) {
            continue;
        }
        output.push(line);
    }

    return output.join('\n');
}

function GetFatalErrorLines(errorLines) {
    return errorLines.filter((match) => !config.NON_FATAL_ERROR_PATTERN.test(match.text));
}

async function WaitForExpectedLog(expected, offset, timeoutSeconds) {
    const timeout = timeoutSeconds === undefined ? config.DEFAULT_TIMEOUT_SECONDS : timeoutSeconds;
    const startTime = Date.now();

    while (Date.now() - startTime < timeout * 1000) {
        const slice = TailFromOffset(config.LOG_FILE, offset);

        const faults = SearchRegex(slice, config.FAULT_PATTERN);
        if (faults.length > 0) {
            console.error('Fault detected in kernel log.');
            for (const match of faults) {
                console.error(match.text);
            }
            return 1;
        }

        const kos = SearchRegex(slice, config.TEST_KO_PATTERN);
        if (kos.length > 0) {
            console.error('Test reported KO in kernel log.');
            for (const match of kos) {
                console.error(match.text);
            }
            return 1;
        }

        const errorCheck = TailFromOffsetForErrorCheck(offset);
        const errorLines = SearchFixed(errorCheck, config.ERROR_PATTERN);
        if (errorLines.length > 0) {
            const fatalErrorLines = GetFatalErrorLines(errorLines);
            if (fatalErrorLines.length > 0) {
                console.error('Kernel fatal error detected in log.');
                for (const match of fatalErrorLines) {
                    console.error(match.text);
                }
                return 1;
            }
        }

        if (expected && SearchFixed(slice, expected).length > 0) {
            return 0;
        }

        await Sleep(200);
    }

    console.error(`Timed out waiting for expected log: ${expected}`);
    return 2;
}

async function VerifySpawnCommandLine(expectedCommand, offset, timeoutSeconds) {
    const timeout = timeoutSeconds === undefined ? config.COMMAND_FORMATION_TIMEOUT_SECONDS : timeoutSeconds;
    const expectedNormalized = NormalizeSpaces(expectedCommand);
    const startTime = Date.now();

    while (Date.now() - startTime < timeout * 1000) {
        const slice = TailFromOffset(config.LOG_FILE, offset);
        const launchMatch = SearchFixed(slice, '[Spawn] Launching :')[0];

        if (launchMatch) {
            const launchCommand = launchMatch.text.replace(/^.*\[Spawn\] Launching : /, '');
            const launchNormalized = NormalizeSpaces(launchCommand);
            if (launchNormalized === expectedNormalized) {
                return 0;
            }

            console.error('Command launch mismatch detected.');
            console.error(`Expected: ${expectedNormalized}`);
            console.error(`Actual:   ${launchNormalized}`);
            return 1;
        }

        await Sleep(100);
    }

    console.error(`Timed out waiting for spawn launch log for command: ${expectedCommand}`);
    return 2;
}

function AssertNoFailures(offset) {
    const slice = TailFromOffset(config.LOG_FILE, offset);

    const faults = SearchRegex(slice, config.FAULT_PATTERN);
    if (faults.length > 0) {
        console.error('Fault detected in kernel log.');
        for (const match of faults) {
            console.error(match.text);
        }
        return 1;
    }

    const kos = SearchRegex(slice, config.TEST_KO_PATTERN);
    if (kos.length > 0) {
        console.error('Test reported KO in kernel log.');
        for (const match of kos) {
            console.error(match.text);
        }
        return 1;
    }

    const errorCheck = TailFromOffsetForErrorCheck(offset);
    const errorLines = SearchFixed(errorCheck, config.ERROR_PATTERN);
    if (errorLines.length > 0) {
        const fatalErrorLines = GetFatalErrorLines(errorLines);
        if (fatalErrorLines.length > 0) {
            console.error('Kernel fatal error detected in kernel log.');
            for (const match of fatalErrorLines) {
                console.error(match.text);
            }
            return 1;
        }

        console.error('Kernel non-fatal errors detected in kernel log.');
        for (const match of errorLines) {
            console.error(match.text);
        }
    }

    return 0;
}

module.exports = {
    TailFromOffsetForErrorCheck,
    WaitForExpectedLog,
    VerifySpawnCommandLine,
    AssertNoFailures,
};
